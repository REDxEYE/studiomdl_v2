// One model compile, from the QC/.mpp to the written files: everything between the setup of the process (the
// application in studiomdl_app.cpp, or a library host) and its teardown.
#include "studiomdl/compile.hpp"

#include <cmath>
#include <filesystem>
#include <numbers>
#include <numeric>
#include <print>

#include "common/cmdlib.h"
#include "common/scriplib.h"
#include "datamodel/idatamodel.h"
#include "dmxsupport.hpp"
#include "filesystem.h"
#include "studiomdl/collisionmodel.h"
#include "interfaces/interfaces.h"
#include "studiomdl/flexoptimize.h"
#include "studiomdl/output_files.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_commands.h"
#include "studiomdl_errors.h"
#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

//-----------------------------------------------------------------------------
// Purpose: search through the "GamePath" key and create a mirrored version in the content path searches
//-----------------------------------------------------------------------------
static void AddContentPaths(StudioMdlState &ctx) {
    // look for the "content" in the path to the initial QC file
    const std::string_view qdir = ctx.paths.qdir;
    constexpr std::string_view match = "content\\";
    const size_t contentPos = qdir.find(match);
    if (contentPos == std::string_view::npos)
        return;

    // everything before and including "content", and everything following the word after "content"
    const std::string_view pre = qdir.substr(0, contentPos + match.size());
    const size_t postPos = qdir.find('\\', contentPos + match.size() + 1);
    if (postPos == std::string_view::npos)
        return;
    const std::string_view post = qdir.substr(postPos);

    // get a copy of the game search paths
    std::string paths(g_pFullFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, nullptr, 0), '\0');
    g_pFullFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, paths.data(), static_cast<int>(paths.size()));
    paths.resize(std::strlen(paths.c_str()));
    if (!ctx.options.quiet)
        MdlPrint("all paths:{}\n", paths);

    // pull out the game names and insert them into a content path string
    constexpr std::string_view game = "game\\";
    for (size_t gamePos = paths.find(game); gamePos != std::string::npos; gamePos = paths.find(game, gamePos)) {
        const size_t nameStart = gamePos + game.size();
        const size_t nameEnd = paths.find('\\', nameStart);
        if (nameEnd == std::string::npos)
            return;

        std::string contentPath(pre);
        contentPath.append(paths, nameStart, nameEnd - nameStart);
        contentPath += post;
        CmdLib_AddBasePath(ctx.paths.content, contentPath);
        if (!ctx.options.quiet)
            MdlPrint("content:{}\n", contentPath);
        gamePos = nameEnd;
    }
}

static void SetSkinValues(StudioMdlState &ctx) {
    int i, j;
    int index;

    // Check all textures to see if we have relative paths specified
    for (i = 0; i < ctx.materials.textures.size(); i++) {
        if (ctx.materials.textures[i].flags & RELATIVE_TEXTURE_PATH_SPECIFIED) {
            // Add an empty path to prepend if anything specifies a relative path
            ctx.materials.cdtextures.emplace_back();
            break;
        }
    }

    if (ctx.materials.cdtextures.size() == 0) {
        // the QC directory from "models" on, else models/<model name>/
        std::string name;
        const size_t models = vstd::ifind(ctx.paths.fullpath, "models");
        if (models != std::string::npos) {
            name = vstd::paths::parent(std::string_view(ctx.paths.fullpath).substr(models));
        }
        else {
            name = "models/";
            name += vstd::paths::without_extension(ctx.paths.outname.view());
        }
        name += '/';
        ctx.materials.cdtextures.emplace_back(ctx.arena.CopyString(name));
    }

    for (i = 0; i < ctx.materials.textures.size(); i++) {
        char szName[256];
        Q_StripExtension(ctx.materials.textures[i].name, szName, sizeof(szName));
        Q_strncpy(ctx.materials.textures[i].name, szName, sizeof(ctx.materials.textures[i].name));
    }

    // build the skin families: one row per family, mapping every material of family 0 to the family's own;
    // only the first texture group is used
    auto &skinref = ctx.materials.skinref;
    const auto &groups = ctx.materials.texturegroups;
    const size_t numFamilies = groups.empty() || groups[0].empty() ? 1 : groups[0].size();
    skinref.clear();
    for (size_t family = 0; family < numFamilies; family++) {
        auto &row = skinref.emplace_back(ctx.materials.textures.size());
        std::iota(row.begin(), row.end(), 0);
    }
    if (!groups.empty() && !groups[0].empty()) {
        const auto &families = groups[0];
        for (size_t family = 0; family < families.size(); family++) {
            const size_t count = std::min(families[family].size(), families[0].size());
            for (size_t j = 0; j < count; j++) {
                skinref[family][families[0][j]] = families[family][j];
            }
        }
    }

    FlexOptimizeFixupSkinFamilies(ctx);

    // printf ("width: %i  height: %i\n",width, height);
    /*
	printf ("adjusted width: %i height: %i  top : %i  left: %i\n",
			pmesh->skinwidth, pmesh->skinheight, pmesh->skintop, pmesh->skinleft );
	*/
}

//-----------------------------------------------------------------------------
// Warns about $jointsurfaceprop joints that don't exist
//-----------------------------------------------------------------------------
static void ConsistencyCheckSurfaceProp(StudioMdlState &ctx) {
    for (int i = std::ssize(ctx.materials.JointSurfaceProp); --i >= 0;) {
        int j = findGlobalBone(ctx, ctx.materials.JointSurfaceProp[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint surface property for joint\n"
                       "    \"{}\" which either doesn't exist or was optimized out.\n",
                       ctx.materials.JointSurfaceProp[i].m_pJointName);
        }
    }
}

//-----------------------------------------------------------------------------
// Checks specified contents
//-----------------------------------------------------------------------------
static void ConsistencyCheckContents(StudioMdlState &ctx) {
    for (int i = std::ssize(ctx.core.jointContents); --i >= 0;) {
        int j = findGlobalBone(ctx, ctx.core.jointContents[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint contents for joint\n"
                       "    \"{}\" which either doesn't exist or was optimized out.\n",
                       ctx.core.jointContents[i].m_pJointName);
        }
    }
}

void SetCompileDefaults(StudioMdlState &ctx) {
    ctx.script.currentscale = ctx.script.defaultscale = 1.0;
    ctx.script.defaultrotation = RadianEuler(0, 0, std::numbers::pi / 2);

    // weightlist [0] is the default one ($defaultweightlist)
    ctx.anim.weightlist.resize(1);

    ctx.header.eyeposition = Vector(0, 0, 0);
    ctx.header.gflags = 0;

    ctx.options.normal_blend = std::cos(vstd::deg2rad(2.0f));

    ctx.options.gamma = 2.2;

    ctx.header.staticprop = false;
    ctx.header.centerstaticprop = false;

    ctx.options.realignbones = false;
    ctx.header.constdirectionalightdot = 0;

    ctx.write.dumpGLViewFiles = false;
    ctx.options.quiet = false;

    ctx.header.illumpositionattachment = 0;
    ctx.header.flMaxEyeDeflection = 0.0f;

    ctx.options.collapse_bones_message = false;
}

// Absolute, normalized, native separators, with a trailing separator
static std::string AbsoluteDirectory(const std::filesystem::path &path) {
    std::error_code ec;
    std::string dir = vstd::paths::to_utf8(std::filesystem::absolute(path, ec).lexically_normal());
    vstd::paths::fix_slashes(dir);
    vstd::paths::append_slash(dir);
    return dir;
}

void SetupCompileDirectories(StudioMdlState &ctx, const std::string_view qcPath, const std::string_view gameInfoPath) {
    std::error_code ec;
    const std::filesystem::path qc = std::filesystem::absolute(vstd::paths::to_fs(qcPath), ec).lexically_normal();
    ctx.paths.qdir = AbsoluteDirectory(qc.parent_path());
#if !(PLATFORM_LINUX)
    vstd::make_lower_ascii(ctx.paths.qdir);
#endif
    ctx.paths.gameDir = AbsoluteDirectory(vstd::paths::to_fs(gameInfoPath));
}

bool BuildModelFiles(StudioMdlState &ctx) {
    FlexOptimizeSources(ctx);

    const size_t count = ctx.anim.sources.size();
    for (size_t i = 0; i < count; i++) {
        if (ctx.anim.sources[i]->isActiveModel) {
            ClampMaxVerticesPerModel(ctx, ctx.anim.sources[i]);
        }
    }

    SetSkinValues(ctx);

    if (!SimplifyModel(ctx))
        return false;

    CheckFlexCacheBudget(ctx);

    ConsistencyCheckSurfaceProp(ctx);
    ConsistencyCheckContents(ctx);

    WriteModelFiles(ctx);
    CollisionModel_Build(ctx);
    CollisionModel_Write(ctx, ctx.header.checksum);
    return true;
}

int CompileModel(StudioMdlState &ctx, const CompileOutput output) {
    AddContentPaths(ctx);

    if (!ctx.options.quiet) {
        MdlPrint("qdir:    \"{}\"\n", ctx.paths.qdir);
        MdlPrint("gamedir: \"{}\"\n", ctx.paths.gameDir);
        MdlPrint("g_path:  \"{}\"\n", ctx.paths.qcPath);
    }

    const std::string ext(vstd::paths::extension(ctx.paths.qcPath));
    // .mpp: DMX model description instead of a QC (Valve's mdlcompile input)
    const bool bPreprocessed = vstd::iequals(ext, "mpp");

    // Only source files are built (the .mdl stripping modes needed Valve's mdllib)
    if (vstd::iequals(ext, "mdl")) {
        MdlPrint("ERROR: " SRC_FILE_EXT " or .mpp file should be specified to build.\n");
        return 1;
    }

    if (!ctx.options.quiet)
        MdlPrint("Building binary model files...\n");

    // the input is read from qdir (its directory)
    ctx.paths.qcPath = vstd::paths::file_base(ctx.paths.qcPath);
    vstd::paths::default_extension(ctx.paths.qcPath, bPreprocessed ? ".mpp" : SRC_FILE_EXT);

    if (!ctx.options.quiet) {
        MdlPrint("Working on \"{}\"\n", ctx.paths.qcPath);
    }

    ctx.paths.fullpath = ExpandArg(ExpandPath(ctx.paths.qdir, ctx.paths.qcPath));

    // default to having one entry in the LOD list that doesn't do anything so
    // that we don't have to do any special cases for the first LOD.
    ctx.body.ScriptLODs.clear();
    ctx.body.ScriptLODs.emplace_back(); // add an empty one
    ctx.body.ScriptLODs[0].switchValue = 0.0f;

    //
    // parse it
    //
    ClearModel();

    if (bPreprocessed) {
        if (!ParsePreprocessedFile(ctx, ctx.paths.fullpath)) {
            MdlError("Invalid MPP File: {}\n", ctx.paths.qcPath);
            return 1;
        }
    }
    else {
        ScriptTokenizer qc(ctx.paths.qdir, &ctx.paths.content);
        InstallMdlErrorHandler(qc);
        qc.LoadFile(ctx.paths.qcPath.c_str());
        ParseScript(ctx, qc);
    }

    if (!ctx.options.createMakefile) {
        if (!BuildModelFiles(ctx)) {
            // -definebones listed the bones: no files, no "Completed" (what ending the process there did)
            g_pDataModel->UnloadFile(DMFILEID_INVALID);
            return 0;
        }
        if (output == CompileOutput::WriteFiles)
            CommitOutputFiles(ctx);
    }

    if (ctx.options.createMakefile) {
        CreateMakefile_OutputMakefile(ctx);
    }

    if (!ctx.options.quiet) {
        MdlPrint("\nCompleted \"{}\"\n", ctx.paths.qcPath);
    }

    if (ctx.options.parseable_completion_output) {
        MdlPrint("\nRESULT: SUCCESS\n");
    }

    g_pDataModel->UnloadFile(DMFILEID_INVALID);
    return 0;
}

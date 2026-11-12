//
// Created by RED on 15.05.2024.
//

#include "studiomdl/platform_compat.h"

#include "studiomdl_app.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl_commands.h"
#include "common/cmdlib.h"
#include "studiomdl_errors.h"
#include "tier1/keyvalues.h"
#include "tier2/fileutils.h"
#include "studiomdl/optimize.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/flexoptimize.h"
#include "common/scriplib.h"
#include "studiomdl/perfstats.h"
#include "datamodel/idatamodel.h"
#include "dmserializers/idmserializers.h"
#include "tier2/tier2dm.h"
#include "filesystem/filesystem_stdio.h"
#include "studiomdl/collisionmodel.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

#include <filesystem>

#include "dmxsupport.hpp"
#include "tier1_v2/math_utils.hpp"

CMdlLoggingListener s_MdlLoggingListener;
CStudioMDLApp s_ApplicationObject;

class CClampedSource;

static bool
CStudioMDLApp_SuggestGameInfoDirFn(CFSSteamSetupInfo const*, char* pchPathBuffer, int nBufferLength,
                                   bool* pbBubbleDirectories) {
    std::optional<std::string_view> processFileName;
    uint32_t nParmCount = CommandLine()->ParmCount();
    if (nParmCount > 1) {
        processFileName = CommandLine()->GetParm(nParmCount - 1);
    }

    if (processFileName.has_value()) {
        Q_MakeAbsolutePath(pchPathBuffer, nBufferLength, processFileName.value().data());

        if (pbBubbleDirectories)
            *pbBubbleDirectories = true;

        return true;
    }

    return false;
}

int main(int argc, char** argv) {
    StudioMdlCompat::EnableUtf8Console();

    // Paths are UTF-8 everywhere inside, starting with the command line
    std::vector<std::string> args = StudioMdlCompat::Utf8Arguments(argc, argv);
    std::vector<char*> argPointers;
    argPointers.reserve(args.size() + 1);
    for (std::string &arg: args) {
        argPointers.push_back(arg.data());
    }
    argPointers.push_back(nullptr);

    SetSuggestGameInfoDirFn(CStudioMDLApp_SuggestGameInfoDirFn);

    return s_ApplicationObject.Run(static_cast<int>(args.size()), argPointers.data());
}

//-----------------------------------------------------------------------------
// Purpose: search through the "GamePath" key and create a mirrored version in the content path searches
//-----------------------------------------------------------------------------
void AddContentPaths() {
    // look for the "content" in the path to the initial QC file
    const std::string_view qdir = g_StudioMdlContext.qdir;
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
    std::string paths(g_pFullFileSystem->GetSearchPath("GAME", false, nullptr, 0), '\0');
    g_pFullFileSystem->GetSearchPath("GAME", false, paths.data(), static_cast<int>(paths.size()));
    paths.resize(std::strlen(paths.c_str()));
    if (!g_StudioMdlContext.quiet)
        std::print("all paths:{}\n", paths);

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
        CmdLib_AddBasePath(contentPath.c_str());
        if (!g_StudioMdlContext.quiet)
            std::print("content:{}\n", contentPath);
        gamePos = nameEnd;
    }
}

//-----------------------------------------------------------------------------
// Purpose: parses the game info file to retrieve relevant settings
//-----------------------------------------------------------------------------
struct GameInfo_t g_gameinfo;

void ParseGameInfo() {
    bool bParsed = false;

    auto* pKeyValues = new KeyValues("gameinfo.txt");
    if (g_pFileSystem && pKeyValues->LoadFromFile(g_pFileSystem, "gameinfo.txt")) {
        bParsed = true;
    }
    pKeyValues->deleteThis();

    if (!bParsed) {
        GameInfo_t gameinfoDefault{};
        g_gameinfo = gameinfoDefault;
    }
}

/*
===============
ParseScript
===============
*/
static void ParseScript(ScriptTokenizer &qc) {
    vstd::FunctionTimingRecorder _("Parse");
    while (qc.GetToken(true)) {
        // Check all the commands we know about.
        int i;
        for (i = 0; i < g_nMDLCommandCount; i++) {
            if (Q_stricmp(g_Commands[i].m_pName, qc.token))
                continue;

            g_Commands[i].m_pCmd(qc);
            break;
        }
        if (i == g_nMDLCommandCount && !g_StudioMdlContext.createMakefile) {
            qc.Error("bad command {}", qc.token);
        }
    }
}

//-----------------------------------------------------------------------------
// .mpp (DMX model description): the whole model comes from the DMX root (mppsupport.cpp).
// Without a root "mdlPath" the output name is the .mpp's directory under <game>/models (content mapped to
// game; a trailing "preprocess"/".preprocess" directory is skipped), as in Valve's mdlcompile.
//-----------------------------------------------------------------------------
static bool ParsePreprocessedFile(const std::string &fullPath) {
    if (!LoadPreprocessedFile(fullPath.c_str(), 1.0f))
        return false;

    if (g_StudioMdlContext.hasModelName)
        return true;

    char modPath[4096] = {};
    ComputeModFilename(fullPath.c_str(), modPath, sizeof(modPath));
    std::string outputName(vstd::paths::parent(modPath));
    const std::string_view dirName = vstd::paths::file_name(outputName);
    if (vstd::iequals(dirName, "preprocess") || vstd::iequals(dirName, ".preprocess")) {
        outputName = vstd::paths::parent(outputName);
    }

    if (vstd::paths::is_absolute(outputName)) {
        char relative[4096] = {};
        if (!g_pFullFileSystem->FullPathToRelativePathEx(outputName.c_str(), "GAME", relative, sizeof(relative))) {
            MdlError("Full path {} is not associated with the current mod!\n", outputName);
            return false;
        }
        std::string relativeName = relative;
        vstd::paths::fix_slashes(relativeName, '/');
        if (!vstd::istarts_with(relativeName, "models/")) {
            MdlError("Full path {} is not under the 'models' directory\n", outputName);
            return false;
        }
        outputName = relativeName.substr(7);
    }
    vstd::paths::set_extension(outputName, ".mdl");
    ProcessModelName(outputName.c_str());
    return true;
}

// Used by the CheckSurfaceProps.py script.
// They specify the .mdl file and it prints out all the surface props that the model uses.
bool HandlePrintSurfaceProps(int &returnValue) {
    const auto filename = CommandLine()->ParmValue("-PrintSurfaceProps");
    if (filename.has_value()) {
        CUtlVector<char> buf;
        FILE* fp = vstd::paths::open_file(*filename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            buf.SetSize(ftell(fp));
            fseek(fp, 0, SEEK_SET);
            fread(buf.Base(), 1, buf.Count(), fp);

            fclose(fp);

            studiohdr_t* pHdr = (studiohdr_t*) buf.Base();

            Studio_ConvertStudioHdrToNewVersion(pHdr);

            if (pHdr->version == STUDIO_VERSION) {
                for (int i = 0; i < pHdr->numbones; i++) {
                    const mstudiobone_t* pBone = pHdr->pBone(i);
                    std::print("{}\n", pBone->pszSurfaceProp());
                }

                returnValue = 0;
            }
            else {
                std::print("-PrintSurfaceProps: '{}' is wrong version ({} should be {}).\n",
                           *filename, pHdr->version, STUDIO_VERSION);
                returnValue = 1;
            }
        }
        else {
            std::print("-PrintSurfaceProps: can't open '{}'\n", *filename);
            returnValue = 1;
        }

        return true;
    }
    else {
        return false;
    }
}

// Used by the modelstats.pl script.
// They specify the .mdl file and it prints out perf info.
bool HandleMdlReport(int &returnValue) {
    const auto filename = CommandLine()->ParmValue("-mdlreport");
    if (filename.has_value()) {
        CUtlVector<char> buf;

        FILE* fp = vstd::paths::open_file(*filename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            buf.SetSize(ftell(fp));
            fseek(fp, 0, SEEK_SET);
            fread(buf.Base(), 1, buf.Count(), fp);

            fclose(fp);

            studiohdr_t* pHdr = (studiohdr_t*) buf.Base();

            Studio_ConvertStudioHdrToNewVersion(pHdr);

            if (pHdr->version == STUDIO_VERSION) {
                int flags = SPEWPERFSTATS_SHOWPERF;
                if (!CommandLine()->FindParm("-mdlreportspreadsheet").has_value()) {
                    flags |= SPEWPERFSTATS_SPREADSHEET;
                }
                SpewPerfStats(pHdr, *filename, flags);

                returnValue = 0;
            }
            else {
                std::print("-mdlreport: '{}' is wrong version ({} should be {}).\n",
                           *filename, pHdr->version, STUDIO_VERSION);
                returnValue = 1;
            }
        }
        else {
            std::print("-mdlreport: can't open '{}'\n", *filename);
            returnValue = 1;
        }

        return true;
    }
    else {
        return false;
    }
}

void CreateMakefile_OutputMakefile() {
    if (!g_StudioMdlContext.hasModelName) {
        MdlError("Can't write makefile since a target mdl hasn't been specified!");
    }
    FILE* fp = fopen("makefile.tmp", "a");
    if (!fp) {
        MdlError("can't open makefile.tmp!\n");
    }
    const std::string mdlname = OutputModelPath(".mdl");

    std::print(fp, "{}:", mdlname);
    int i;
    for (i = 0; i < g_StudioMdlContext.CreateMakefileDependencies.size(); i++) {
        std::print(fp, " {}", g_StudioMdlContext.CreateMakefileDependencies[i].String());
    }
    std::print(fp, "\n");
    std::print(fp, "\tmkdir \"{}\"\n", std::string(vstd::paths::parent(mdlname)));
    std::print(fp, "\t{} -quiet {}\n\n", CommandLine()->GetParm(0).value(), g_studiomdl.fullpath);
    fclose(fp);
}

void ClampMaxVerticesPerModel(s_source_t* pOrigSource) {
    // check for overage
    if (pOrigSource->numvertices < g_StudioMdlContext.maxVertexLimit)
        return;

    MdlWarning("model has too many verts, cutting into multiple models\n");

    CUtlVector<CClampedSource> newSource;

    int ns = newSource.AddToTail();
    newSource[ns].Init(pOrigSource->numvertices, pOrigSource);

    // Flex optimization: only meshes with flexes count against the engine's flex cache, so faces a flex can move are
    // put into the first pieces and the static faces into pieces of their own, which then carry no flex data.
    // Without it there is one pass over all faces, in source order.
    std::vector<bool> moving;
    bool bSortByFlex = FlexOptimizeIsActive() && pOrigSource->m_FlexKeys.Count() > 0;
    if (bSortByFlex)
        moving = FlexOptimizeMovingVertices(pOrigSource);

    auto isMovingFace = [&](const s_mesh_t* pMesh, const s_face_t &face) {
        const uint32_t base = pMesh->vertexoffset;
        return moving[base + face.a] || moving[base + face.b] || moving[base + face.c] ||
               (face.d != 0 && moving[base + face.d]);
    };

    if (bSortByFlex) {
        // a second pass is only needed if the source has both moving and static faces
        bool bAnyMoving = false, bAnyStatic = false;
        for (const s_mesh_t &mesh: pOrigSource->meshes) {
            const s_mesh_t* pMesh = &mesh;
            for (int faceId = pMesh->faceoffset; faceId < pMesh->faceoffset + pMesh->numfaces; faceId++) {
                if (isMovingFace(pMesh, pOrigSource->face[faceId]))
                    bAnyMoving = true;
                else
                    bAnyStatic = true;
            }
        }
        bSortByFlex = bAnyMoving && bAnyStatic;
    }

    const int nPasses = bSortByFlex ? 2 : 1;
    for (int pass = 0; pass < nPasses; pass++) {
        const bool bMovingPass = (pass == 0);
        if (pass == 1 && newSource[ns].m_face.Count() > 0) {
            // static faces never share a piece with moving ones; this also keeps the faces of every mesh of a piece
            // contiguous (each pass walks the meshes in order)
            ns = newSource.AddToTail();
            newSource[ns].Init(pOrigSource->numvertices, pOrigSource);
        }

        // Every piece has the original's meshes (same order and materials), so every face stays in the mesh of its
        // material and the per-material bookkeeping (texmap) of the original source is preserved.
        for (int nMesh = 0; nMesh < pOrigSource->meshes.size(); nMesh++) {
            const s_mesh_t* pOrigMesh = &pOrigSource->meshes[nMesh];
            for (int faceId = pOrigMesh->faceoffset; faceId < pOrigMesh->faceoffset + pOrigMesh->numfaces; faceId++) {
                // make sure all the total for all the meshes in the model don't go over limit
                const s_face_t &origFace = pOrigSource->face[faceId];
                if (bSortByFlex && isMovingFace(pOrigMesh, origFace) != bMovingPass)
                    continue;
                int nVertsInFace = (origFace.d == 0) ? 3 : 4;
                if ((newSource[ns].m_vertex.Count() + nVertsInFace) > g_StudioMdlContext.maxVertexClamp) {
                    // go to the next model
                    ns = newSource.AddToTail();
                    newSource[ns].Init(pOrigSource->numvertices, pOrigSource);
                }

                // build face
                s_face_t face;
                face.a = newSource[ns].AddNewVert(pOrigSource, origFace.a, nMesh, nMesh);
                face.b = newSource[ns].AddNewVert(pOrigSource, origFace.b, nMesh, nMesh);
                face.c = newSource[ns].AddNewVert(pOrigSource, origFace.c, nMesh, nMesh);
                if (nVertsInFace == 4) {
                    face.d = newSource[ns].AddNewVert(pOrigSource, origFace.d, nMesh, nMesh);
                    if (face.d == 0) {
                        // d == 0 means "triangle" in s_face_t, but this quad's last vertex is the first vertex of the
                        // mesh in this piece: rotate the quad (keeps the winding) so that it stays a quad
                        face.d = face.c;
                        face.c = face.b;
                        face.b = face.a;
                        face.a = 0;
                    }
                }
                else {
                    face.d = 0;
                }

                s_mesh_t &dstMesh = newSource[ns].m_meshes[nMesh];
                if (dstMesh.numfaces == 0) {
                    dstMesh.faceoffset = newSource[ns].m_face.Count();
                }
                newSource[ns].m_face.AddToTail(face);
                dstMesh.numfaces++;
            }
        }
    }

    // Split animations into the new sub-models
    for (int n = 0; n < newSource.Count(); n++) {
        newSource[n].AddAnimations(pOrigSource);
    }

    // Name of what was split, so the pieces can be told apart: the model using the source, or the source file
    char szOrigName[MAXSTUDIONAME];
    Q_FileBase(pOrigSource->filename.c_str(), szOrigName, sizeof(szOrigName));
    for (int i = 0; i < g_studiomdl.nummodels; i++) {
        if (g_studiomdl.model[i]->source == pOrigSource && g_studiomdl.model[i]->name[0]) {
            Q_FileBase(g_studiomdl.model[i]->name, szOrigName, sizeof(szOrigName));
            break;
        }
    }

    // copy over new meshes and animations back into initial source
    free(pOrigSource->face);
    free(pOrigSource->vertex);
    newSource[0].Copy(pOrigSource);

    for (int n = 1; n < newSource.Count(); n++) {
        // create a new internal "source"
        s_source_t* pSource = c_allocate<s_source_t>();
        g_studiomdl.sources.emplace_back(pSource);

        // copy all the members, in order
        pSource->filename = pOrigSource->filename;

        // copy over the faces/vertices/animations
        newSource[n].Copy(pSource);

        // copy settings
        pSource->isActiveModel = true;

        // copy skeleton
        pSource->numbones = pOrigSource->numbones;
        for (int i = 0; i < pSource->numbones; i++) {
            pSource->localBone[i] = pOrigSource->localBone[i];
            pSource->boneToPose[i] = pOrigSource->boneToPose[i];
        }


        // The following members are set up later on in the process, so we don't need to copy them here:
        //   pSource->boneflags
        //   pSource->boneref
        //   pSource->boneLocalToGlobal
        //   pSource->boneGlobalToLocal
        //   pSource->m_GlobalVertices


        // copy material data: texmap is indexed by material index, so it has to be copied as a whole
        // (meshindex/mesh/nummeshes were set up by CClampedSource::Copy)
        memcpy(pSource->texmap, pOrigSource->texmap, sizeof(pSource->texmap));

        // copy settings
        pSource->adjust = pOrigSource->adjust;
        pSource->scale = pOrigSource->scale;
        pSource->rotation = pOrigSource->rotation;
        pSource->bNoAutoDMXRules = pOrigSource->bNoAutoDMXRules;

        // allocate a model
        s_model_t* pModel = c_allocate<s_model_t>();
        pModel->source = pSource;
        pModel->name.format("{}_cmodel{}", szOrigName, n);
        pModel->filename = std::string(szOrigName) + "_cmesh" + std::to_string(n);
        int imodel = g_studiomdl.nummodels++;
        g_studiomdl.model[imodel] = pModel;

        // make it a new bodypart
        s_bodypart_t &bodypart = g_studiomdl.bodypart[g_studiomdl.numbodyparts];
        bodypart.nummodels = 1;
        bodypart.base = g_studiomdl.bodypart[g_studiomdl.numbodyparts - 1].base * g_studiomdl.bodypart[
                            g_studiomdl.numbodyparts - 1].nummodels;
        bodypart.name.format("{}_clamped{}", szOrigName, n);
        bodypart.pmodel[0] = pModel;
        g_studiomdl.numbodyparts++;

        // finally, copy flex keys
        newSource[n].CopyFlexKeys(pOrigSource, pSource, imodel);

        // NOTE: we leave attachments on the first sub-model, we don't want to duplicate those
    }
}

void SetSkinValues() {
    int i, j;
    int index;

    // Check all textures to see if we have relative paths specified
    for (i = 0; i < g_studiomdl.textures.size(); i++) {
        if (g_studiomdl.textures[i].flags & RELATIVE_TEXTURE_PATH_SPECIFIED) {
            // Add an empty path to prepend if anything specifies a relative path
            g_studiomdl.cdtextures.emplace_back();
            break;
        }
    }

    if (g_studiomdl.cdtextures.size() == 0) {
        // the QC directory from "models" on, else models/<model name>/
        std::string name;
        const size_t models = vstd::ifind(g_studiomdl.fullpath, "models");
        if (models != std::string::npos) {
            name = vstd::paths::parent(std::string_view(g_studiomdl.fullpath).substr(models));
        }
        else {
            name = "models/";
            name += vstd::paths::without_extension(g_studiomdl.outname.view());
        }
        name += '/';
        g_studiomdl.cdtextures.emplace_back(strdup(name.c_str()));
    }

    for (i = 0; i < g_studiomdl.textures.size(); i++) {
        char szName[256];
        Q_StripExtension(g_studiomdl.textures[i].name, szName, sizeof(szName));
        Q_strncpy(g_studiomdl.textures[i].name, szName, sizeof(g_studiomdl.textures[i].name));
    }

    // build texture groups
    for (i = 0; i < MAXSTUDIOSKINS; i++) {
        for (j = 0; j < MAXSTUDIOSKINS; j++) {
            g_studiomdl.skinref[i][j] = j;
        }
    }
    index = 0;
    for (i = 0; i < g_studiomdl.numtexturelayers[0]; i++) {
        for (j = 0; j < g_studiomdl.numtexturereps[0]; j++) {
            g_studiomdl.skinref[i][g_studiomdl.texturegroup[0][0][j]] = g_studiomdl.texturegroup[0][i][j];
        }
    }

    if (i != 0) {
        g_studiomdl.numskinfamilies = i;
    }
    else {
        g_studiomdl.numskinfamilies = 1;
    }
    g_studiomdl.numskinref = g_studiomdl.textures.size();

    FlexOptimizeFixupSkinFamilies();

    // printf ("width: %i  height: %i\n",width, height);
    /*
	printf ("adjusted width: %i height: %i  top : %i  left: %i\n",
			pmesh->skinwidth, pmesh->skinheight, pmesh->skintop, pmesh->skinleft );
	*/
}

//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
void ConsistencyCheckSurfaceProp() {
    for (int i = g_StudioMdlContext.JointSurfaceProp.Count(); --i >= 0;) {
        int j = findGlobalBone(g_StudioMdlContext.JointSurfaceProp[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint surface property for joint\n"
                       "    \"{}\" which either doesn't exist or was optimized out.\n",
                       g_StudioMdlContext.JointSurfaceProp[i].m_pJointName);
        }
    }
}

//-----------------------------------------------------------------------------
// Checks specified contents
//-----------------------------------------------------------------------------
void ConsistencyCheckContents() {
    for (int i = s_JointContents.Count(); --i >= 0;) {
        int j = findGlobalBone(s_JointContents[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint contents for joint\n"
                       "    \"{}\" which either doesn't exist or was optimized out.\n",
                       g_StudioMdlContext.JointSurfaceProp[i].m_pJointName);
        }
    }
}

void UsageAndExit() {
    MdlError("Bad or missing options\n"
        "usage: studiomdl [options] <file.qc>\n"
        "options:\n"
        "[-a <normal_blend_angle>]\n"
        "[-checklengths]\n"
        "[-d] - dump glview files\n"
        "[-definebones]\n"
        "[-f] - flip all triangles\n"
        "[-fullcollide] - don't truncate really big collisionmodels\n"
        "[-game <gamedir>]\n"
        "[-h] - dump hboxes\n"
        "[-i] - ignore warnings\n"
        "[-minlod <lod>] - truncate to highest detail <lod>\n"
        "[-fitweights [<poses> | <fitting poses>,<test poses>]] - experimental: DMX vertices with more than 3\n"
        "                bones get weights fitted to their motion instead of the 3 largest rescaled.\n"
        "                Poses default to 24,24; one number sets both\n"
        "[-n] - tag bad normals\n"
        "[-perf] report perf info upon compiling model\n"
        "[-printbones]\n"
        "[-printgraph]\n"
        "[-quiet] - operate silently\n"
        "[-r] - tag reversed\n"
        "[-t <texture>]\n"
        "[-fastbuild] - write a single vertex windings file\n"
        "[-nowarnings] - disable warnings\n"
        "[-flexopt auto|on|off] - split flexed meshes to fit the engine flex cache (default auto)\n"
        "[-flexopt_min_static <verts>] - smallest static part split off a flexed mesh (default 256)\n"
        "[-dumpmaterials] - dump out material names\n"
        "[-mdlreport] model.mdl - report perf info\n"
        "[-mdlreportspreadsheet] - report perf info as a comma-delimited spreadsheet\n"
        "[-striplods] - use only lod0\n"
        "[-overridedefinebones] - equivalent to specifying $unlockdefinebones in " SRC_FILE_EXT " file\n"
        "[-allowdebug]\n"
        "[-ihvtest]\n"
        "[-overridedefinebones]\n"
        "[-verbose]\n"
        "[-makefile]\n"
        "[-verify]\n"
        "[-fastbuild]\n"
        "[-maxwarnings]\n"
        "[-preview]\n"
        "[-dumpmaterials]\n"
        "[-basedir]\n"
        "[-tempcontent]\n"
    );
}

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
int CStudioMDLApp::Main() {
    {
        vstd::FunctionTimingRecorder _("main");
        g_StudioMdlContext.numverts = g_StudioMdlContext.numnormals = 0;
        g_StudioMdlContext.faces.clear();
        for (int &g_numtexcoord: g_StudioMdlContext.numtexcoords) {
            g_numtexcoord = 0;
        }

        // This bit of hackery allows us to access files on the harddrive
        g_pFullFileSystem->AddSearchPath("", "LOCAL", PATH_ADD_TO_HEAD);

        int nReturnValue;
        if (HandleMdlReport(nReturnValue))
            return false;

        AddContentPaths();

        ParseGameInfo();

        StudioMdlContext &context = g_StudioMdlContext;
        if (!context.quiet) {
            std::print("qdir:    \"{}\"\n", context.qdir);
            std::print("gamedir: \"{}\"\n", context.gameDir);
            std::print("g_path:  \"{}\"\n", context.qcPath);
        }

        const std::string ext(vstd::paths::extension(context.qcPath));
        // .mpp: DMX model description instead of a QC (Valve's mdlcompile input)
        const bool bPreprocessed = vstd::iequals(ext, "mpp");

        // Only source files are built (the .mdl stripping modes needed Valve's mdllib)
        if (vstd::iequals(ext, "mdl")) {
            std::print("ERROR: " SRC_FILE_EXT " or .mpp file should be specified to build.\n");
            return 1;
        }

        if (!context.quiet)
            std::print("Building binary model files...\n");

        // the input is read from qdir (its directory)
        context.qcPath = vstd::paths::file_base(context.qcPath);
        vstd::paths::default_extension(context.qcPath, bPreprocessed ? ".mpp" : SRC_FILE_EXT);

        if (!context.quiet) {
            std::print("Working on \"{}\"\n", context.qcPath);
        }

        g_studiomdl.fullpath = ExpandArg(ExpandPath(context.qcPath));

        // default to having one entry in the LOD list that doesn't do anything so
        // that we don't have to do any special cases for the first LOD.
        g_studiomdl.ScriptLODs.Purge();
        g_studiomdl.ScriptLODs.AddToTail(); // add an empty one
        g_studiomdl.ScriptLODs[0].switchValue = 0.0f;

        //
        // parse it
        //
        ClearModel();

        //	strcpy( g_pPlatformName, "" );
        if (bPreprocessed) {
            if (!ParsePreprocessedFile(g_studiomdl.fullpath)) {
                MdlError("Invalid MPP File: {}\n", context.qcPath);
                return 1;
            }
        }
        else {
            ScriptTokenizer qc;
            InstallMdlErrorHandler(qc);
            qc.LoadFile(context.qcPath.c_str());
            ParseScript(qc);
        }

        if (!g_StudioMdlContext.createMakefile) {
            FlexOptimizeSources();

            int nCount = g_studiomdl.sources.size();
            for (int i = 0; i < nCount; i++) {
                if (g_studiomdl.sources[i]->isActiveModel) {
                    ClampMaxVerticesPerModel(g_studiomdl.sources[i]);
                }
            }

            SetSkinValues();

            SimplifyModel();

            CheckFlexCacheBudget();

            ConsistencyCheckSurfaceProp();
            ConsistencyCheckContents();

            WriteModelFiles();
            CollisionModel_Build();
            CollisionModel_Write(g_studiomdl.checksum);
        }

        if (g_StudioMdlContext.createMakefile) {
            CreateMakefile_OutputMakefile();
        }

        if (!g_StudioMdlContext.quiet) {
            std::print("\nCompleted \"{}\"\n", g_StudioMdlContext.qcPath);
        }

        if (g_StudioMdlContext.parseable_completion_output) {
            std::print("\nRESULT: SUCCESS\n");
        }

        g_pDataModel->UnloadFile(DMFILEID_INVALID);
    }

    if (g_StudioMdlContext.perf) {
        vstd::print_function_timings();
    }

    return 0;
}

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
bool CStudioMDLApp::Create() {
    // Ensure that cmdlib spew function & associated state is initialized
    InstallSpewFunction();
    // Override the cmdlib spew function
    LoggingSystem_PushLoggingState();
    LoggingSystem_RegisterLoggingListener(&s_MdlLoggingListener);

    MathLib_Init(2.2f, 2.2f, 0.0f, 2.0f, false, true, true, true);

    SetUnhandledExceptionFilter(VExceptionFilter);


    if (CommandLine()->ParmCount() == 1) {
        UsageAndExit();
        return false;
    }

    int nReturnValue;
    if (HandlePrintSurfaceProps(nReturnValue))
        return false;

    if (!ParseArguments())
        return false;


    return true;
}

bool CStudioMDLApp::Init() {
    return InitDataModel();
}

void CStudioMDLApp::Shutdown() {
    ShutdownDataModel();
}

void CStudioMDLApp::Destroy() {
    LoggingSystem_PopLoggingState();
}

// Absolute, normalized, native separators, with a trailing separator
static std::string AbsoluteDirectory(const std::filesystem::path &path) {
    std::error_code ec;
    std::string dir = vstd::paths::to_utf8(std::filesystem::absolute(path, ec).lexically_normal());
    vstd::paths::fix_slashes(dir);
    vstd::paths::append_slash(dir);
    return dir;
}

// Sets qdir from the QC path and gameDir from the gameinfo.txt directory
static void SetupStandardDirectories(const char* pQCPath, const std::string_view pGameInfoPath) {
    std::error_code ec;
    const std::filesystem::path qc = std::filesystem::absolute(vstd::paths::to_fs(pQCPath), ec).lexically_normal();
    g_StudioMdlContext.qdir = AbsoluteDirectory(qc.parent_path());
#if !(PLATFORM_LINUX)
    vstd::make_lower_ascii(g_StudioMdlContext.qdir);
#endif
    g_StudioMdlContext.gameDir = AbsoluteDirectory(vstd::paths::to_fs(pGameInfoPath));
}

bool CStudioMDLApp::PreInit() {
    g_pFileSystem = g_pFullFileSystem;

    if (!SetupSearchPaths(g_StudioMdlContext.qcPath.c_str(), false, true))
        return false;

    SetupStandardDirectories(g_StudioMdlContext.qcPath.c_str(), GetGameInfoPath());
    return true;
}

static constexpr int kMaxFitWeightsPoses = 4096;

// The value of -fitweights: "<poses>" sets both counts, "<fitting poses>,<test poses>" sets each
static bool ParseFitWeightsPoses(const std::string_view text, int &fitPoses, int &testPoses) {
    const auto parseCount = [](const std::string_view part, int &count) {
        if (part.empty() || part.size() > 4 || part.find_first_not_of("0123456789") != std::string_view::npos)
            return false;
        count = vstd::str_to_int<int>(part);
        return count >= 1 && count <= kMaxFitWeightsPoses;
    };

    int fit, test;
    if (const size_t comma = text.find(','); comma == std::string_view::npos) {
        if (!parseCount(text, fit))
            return false;
        test = fit;
    }
    else if (!parseCount(text.substr(0, comma), fit) || !parseCount(text.substr(comma + 1), test)) {
        return false;
    }
    fitPoses = fit;
    testPoses = test;
    return true;
}


//-----------------------------------------------------------------------------
// Method which parses arguments
//-----------------------------------------------------------------------------
bool CStudioMDLApp::ParseArguments() {
    g_studiomdl.currentscale = g_studiomdl.defaultscale = 1.0;
    g_studiomdl.defaultrotation = RadianEuler(0, 0, std::numbers::pi / 2);

    // skip weightlist 0
    g_studiomdl.numweightlist = 1;

    g_studiomdl.eyeposition = Vector(0, 0, 0);
    g_studiomdl.gflags = 0;
    g_studiomdl.numrep = 0;

    g_studiomdl.normal_blend = std::cos(vstd::deg2rad(2.0f));

    g_studiomdl.gamma = 2.2;

    g_studiomdl.staticprop = false;
    g_studiomdl.centerstaticprop = false;

    g_studiomdl.realignbones = false;
    g_studiomdl.constdirectionalightdot = 0;

    g_bDumpGLViewFiles = false;
    g_StudioMdlContext.quiet = false;

    g_studiomdl.illumpositionattachment = 0;
    g_studiomdl.flMaxEyeDeflection = 0.0f;

    g_StudioMdlContext.collapse_bones_message = false;

    const uint32_t argc = CommandLine()->ParmCount();
    int i;
    for (i = 1; i < argc - 1; i++) {
        const auto optArgValue = CommandLine()->GetParm(i);
        if (!optArgValue) {
            break;
        }
        const std::string_view argValue = *optArgValue;
        if (argValue[0] != '-')
            continue;

        if (vstd::iequals(argValue, "-collapsereport")) {
            g_StudioMdlContext.collapse_bones_message = true;
            continue;
        }

        if (vstd::iequals(argValue, "-parsecompletion")) {
            // reliably prints output we can parse for automatically
            g_StudioMdlContext.parseable_completion_output = true;
            continue;
        }

        if (vstd::iequals(argValue, "-allowdebug")) {
            // Ignore, used by interface system to catch debug builds checked into release tree
            continue;
        }

        if (vstd::iequals(argValue, "-mdlreport")) {
            // Will reparse later, ignore rest of arguments.
            return true;
        }

        if (vstd::iequals(argValue, "-mdlreportspreadsheet")) {
            // Will reparse later, ignore for now.
            continue;
        }

        if (vstd::iequals(argValue, "-overridedefinebones")) {
            g_StudioMdlContext.defineBonesLockedByDefault = false;
            continue;
        }

        if (vstd::iequals(argValue, "-striplods")) {
            g_StudioMdlContext.stripLods = true;
            continue;
        }

        if (vstd::iequals(argValue, "-quiet")) {
            g_StudioMdlContext.quiet = true;
            g_StudioMdlContext.verbose = false;
            continue;
        }

        if (vstd::iequals(argValue, "-verbose")) {
            g_StudioMdlContext.quiet = false;
            g_StudioMdlContext.verbose = true;
            continue;
        }


        if (vstd::iequals(argValue, "-checklengths")) {
            g_StudioMdlContext.checkLengths = true;
            continue;
        }

        if (vstd::iequals(argValue, "-printbones")) {
            g_StudioMdlContext.printBones = true;
            continue;
        }

        if (vstd::iequals(argValue, "-perf")) {
            g_StudioMdlContext.perf = true;
            continue;
        }

        if (vstd::iequals(argValue, "-fitweights")) {
            g_StudioMdlContext.fitWeights = true;
            // optional pose counts (the last argument is the QC file, never a value)
            if (i + 1 < argc - 1) {
                const auto optCounts = CommandLine()->GetParm(i + 1);
                if (optCounts && !optCounts->empty() && (*optCounts)[0] >= '0' && (*optCounts)[0] <= '9') {
                    if (!ParseFitWeightsPoses(*optCounts, g_StudioMdlContext.fitWeightsFitPoses,
                                              g_StudioMdlContext.fitWeightsTestPoses)) {
                        MdlError("-fitweights: expected <poses> or <fitting poses>,<test poses> (1-{} each), got \"{}\"\n",
                                 kMaxFitWeightsPoses, *optCounts);
                    }
                    i++;
                }
            }
            continue;
        }

        if (vstd::iequals(argValue, "-printgraph")) {
            g_StudioMdlContext.dumpGraph = true;
            continue;
        }

        if (vstd::iequals(argValue, "-definebones")) {
            g_studiomdl.definebones = true;
            continue;
        }

        if (vstd::iequals(argValue, "-makefile")) {
            g_StudioMdlContext.createMakefile = true;
            g_StudioMdlContext.quiet = true;
            continue;
        }

        if (vstd::iequals(argValue, "-verify")) {
            g_StudioMdlContext.verifyOnly = true;
            continue;
        }

        if (vstd::iequals(argValue, "-minlod")) {
            g_StudioMdlContext.minLod = vstd::str_to_int<int32_t>(CommandLine()->GetParm(++i).value());
            continue;
        }

        if (vstd::iequals(argValue, "-fastbuild")) {
            // Does nothing now, it's already faster that original studiomdl with -fastbuild
            // sw and dx80 are also strippped
            // g_StudioMdlContext.fastBuild = true;
            continue;
        }

        if (vstd::iequals(argValue, "-nowarnings")) {
            g_StudioMdlContext.noWarnings = true;
            continue;
        }

        if (vstd::iequals(argValue, "-flexopt")) {
            const std::string_view pMode = CommandLine()->GetParm(++i).value();
            if (!ParseFlexOptimizeMode(pMode, g_StudioMdlContext.flexOptimizeMode)) {
                MdlError("-flexopt expects auto, on or off, got \"{}\"\n", pMode);
            }
            continue;
        }

        if (vstd::iequals(argValue, "-flexopt_min_static")) {
            g_StudioMdlContext.flexOptimizeMinStatic = vstd::str_to_int<int32_t>(CommandLine()->GetParm(++i).value());
            continue;
        }

        if (vstd::iequals(argValue, "-maxwarnings")) {
            g_StudioMdlContext.maxWarnings = vstd::str_to_int<int32_t>(CommandLine()->GetParm(++i).value());
            continue;
        }

        if (vstd::iequals(argValue, "-preview")) {
            g_StudioMdlContext.buildPreview = true;
            continue;
        }

        if (vstd::iequals(argValue, "-dumpmaterials")) {
            g_StudioMdlContext.dumpMaterials = true;
            continue;
        }

        if (argValue[1] && argValue[2] == '\0') {
            switch (argValue[1]) {
                case 't': {
                    i++;
                    auto& default_texture = g_studiomdl.defaulttextures[g_studiomdl.numrep];
                    default_texture = argValue;
                    if (i < argc - 2 && CommandLine()->GetParm(i + 1).value()[0] != '-') {
                        i++;
                        auto& source_texture = g_studiomdl.sourcetextures[g_studiomdl.numrep];
                        source_texture = argValue;
                        std::print("Replacing {} with {}\n", g_studiomdl.sourcetextures[g_studiomdl.numrep],
                                   g_studiomdl.defaulttextures[g_studiomdl.numrep]);
                    }
                    std::print("Using default texture: {}\n", g_studiomdl.defaulttextures[0]);
                    g_studiomdl.numrep++;
                    break;
                }
                case 'a':
                    i++;
                    g_studiomdl.normal_blend = cos(vstd::deg2rad(vstd::str_to_float<float>(argValue)));
                    break;
                case 'h':
                    g_studiomdl.dump_hboxes = 1;
                    break;
                case 'i':
                    g_studiomdl.ignore_warnings = 1;
                    break;
                case 'd':
                    g_bDumpGLViewFiles = true;
                    break;
                    //			case 'p':
                    //				i++;
                    //				strcpy( qproject, pArgv );
                    //				break;
            }
        }
    }

    if (i >= argc) {
        // misformed arguments
        // otherwise generating unintended results
        UsageAndExit();
        return false;
    }

    const auto qcArgument = CommandLine()->GetParm(i);
    if (!qcArgument) {
        UsageAndExit();
    }
    g_StudioMdlContext.qcPath = *qcArgument;
    if (vstd::paths::is_absolute(*qcArgument)) {
        // Set the working directory to be the path of the qc file
        // so the relative-file fopen code works
        std::error_code ec;
        std::filesystem::current_path(vstd::paths::to_fs(vstd::paths::directory(*qcArgument)), ec);
    }
    g_studiomdl.outname = vstd::paths::without_extension(*qcArgument);
    return true;
}

//
// Created by RED on 15.05.2024.
//

#include "studiomdl/apply_anim.hpp"
#include "studiomdl/apply_bones.hpp"
#include "studiomdl/apply_flex.hpp"
#include "studiomdl/apply_body.hpp"
#include "studiomdl/apply_globals.hpp"
#include "studiomdl/apply_lod.hpp"
#include "studiomdl/apply_bone_dynamics.hpp"
#include "studiomdl/platform_compat.h"
#include "studiomdl_commands.h"
#include "line_parser.h"
#include "studiomdl/flexoptimize.h"
#include "tier1_v2/function_timing.hpp"
#include "datamodel/idatamodel.h"
#include "mdlobjects/dmeboneflexdriver.h"
#include "common/scriplib.h"
#include "studiomdl_errors.h"
#include "movieobjects/movieobjects.h"
#include "movieobjects/dmemdlmakefile.h"

#include "bspflags.h"
#include "studiomdl/collisionmodel.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

#ifdef WIN32
#undef strdup
#define strdup _strdup
#endif

//-----------------------------------------------------------------------------
// Parse contents flags
//-----------------------------------------------------------------------------
static uint32_t ParseContents(ScriptTokenizer &qc) {
    uint32_t keywords = 0;
    do {
        qc.ExpectToken();
        keywords |= ParseContentsKeyword(qc.token.view());
    } while (qc.TokenAvailable());
    return keywords;
}

void ProcessModelName(StudioMdlState &ctx, std::string_view modelName) {
    // Abort early if modelname is too big
    std::string rawModelName(modelName);
    vstd::paths::strip_extension(rawModelName);
    // write.cpp strips extension then adds .mdl and writes that into studiohdr_t::name

    // Need one for sizeof operation to work...
    if (rawModelName.size() + 4 >= ctx.paths.outname.capacity()) {
        MdlError("Model Name \"{}.mdl\" Too Big, {} Characters, Max {} Characters\n",
                 rawModelName,
                 rawModelName.size() + 4,
                 ctx.paths.outname.capacity() - 1);
    }

    ctx.paths.hasModelName = true;
    ctx.paths.outname = modelName;
#ifdef POSIX
    // "$modelname dir\\name.mdl" is common in QCs written on Windows; the output path is built from ctx.paths.outname
    vstd::paths::fix_slashes(ctx.paths.outname);
#endif
}

//-----------------------------------------------------------------------------
// Parse the studio options from a .qc file
//-----------------------------------------------------------------------------
bool ParseOptionStudio(ScriptTokenizer &qc, CDmeSourceSkin* pSkin) {
    qc.ExpectToken("<reference mesh>");

    pSkin->SetRelativeFileName(qc.token);
    while (qc.TokenAvailable()) {
        qc.GetToken(false);
        if (qc.token.iequals("reverse")) {
            pSkin->m_bFlipTriangles = true;
            continue;
        }
        if (qc.token.iequals("scale")) {
            pSkin->m_flScale = qc.ExpectFloatToken<float32>("<scale>");
            continue;
        }

        if (qc.token.iequals("faces")) {
            qc.ExpectToken();
            qc.ExpectToken();
            continue;
        }

        if (qc.token.iequals("bias")) {
            qc.ExpectToken();
            continue;
        }

        if (qc.token.iequals("subd")) {
            pSkin->m_bQuadSubd = true;
            continue;
        }

        if (qc.token.iequals("{")) {
            // the caller ($model) parses the block
            qc.UnGetToken();
            break;
        }

        qc.Error("unknown command \"{}\"\n", qc.token);
    }
    return true;
}

//-----------------------------------------------------------------------------
// Parse the body command from a .qc file
//-----------------------------------------------------------------------------
void ProcessOptionStudio(StudioMdlState &ctx, s_model_t* pmodel, const char* pFullPath, float flScale, bool bFlipTriangles, bool bQuadSubd) {
    BeginStudioOption(ctx, pmodel, pFullPath, flScale);

    // load source
    pmodel->source = Load_Source(ctx, pmodel->filename.c_str(), "", bFlipTriangles, true);

    EndStudioOption(ctx);
}

//-----------------------------------------------------------------------------
// Process a body command
//-----------------------------------------------------------------------------
void ProcessCmdBody(StudioMdlState &ctx, const char* pFullPath, const char* pBodyPartName, float flScale, bool bFlipTriangles,
                    bool bQuadSubd) {
    s_bodypart_t &bodypart = ApplyBodyPart(ctx, pBodyPartName);

    int imodel;
    s_model_t* pmodel = ApplyBodyModel(ctx, bodypart, imodel);

    ProcessOptionStudio(ctx, pmodel, pFullPath, flScale, bFlipTriangles, bQuadSubd);

    // Body command should add any flex commands in the source loaded
    PostProcessSource(ctx, pmodel->source, imodel);
}


//-----------------------------------------------------------------------------
// Purpose: Handle the $boneflexdriver command
// QC: $boneflexdriver <bone name> <tx|ty|tz> <flex controller name> <min> <max>
//-----------------------------------------------------------------------------
void Cmd_BoneFlexDriver(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // <bone name>
    qc.ExpectToken();
    const std::string bone(qc.token.view());

    // <tx|ty|tz|rx|ry|rz>
    qc.ExpectToken();
    const char* ppszComponentTypeList[] = {"tx", "ty", "tz"};
    int nBoneComponent = -1;
    for (int i = 0; i < ARRAYSIZE(ppszComponentTypeList); ++i) {
        if (StringHasPrefix(qc.token, ppszComponentTypeList[i])) {
            nBoneComponent = i;
            break;
        }
    }

    if (nBoneComponent < STUDIO_BONE_FLEX_TX || nBoneComponent > STUDIO_BONE_FLEX_TZ) {
        qc.Error("{}: Invalid bone component, must be one of <tx|ty|tz>\n", "$boneflexdriver");
        return;
    }

    // <flex controller name>
    qc.ExpectToken();
    const std::string controller(qc.token.view());

    // <min>
    qc.ExpectToken();
    const float min = qc.TokenFloat();

    // <max>
    qc.ExpectToken();
    const float max = qc.TokenFloat();

    if (const std::string error = ApplyBoneFlexDriver(ctx, bone, nBoneComponent, controller, min, max); !error.empty()) {
        qc.Error("{}", error);
    }
}

void Cmd_PoseParameter(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken(); //[wills] unless you want a pose parameter named "poseparameter", should probably GetToken here

    const std::string name(qc.token.view());
    PoseParameterParams params;
    params.name = name;

    if (qc.TokenAvailable()) {
        // min
        params.min = qc.ExpectFloatToken<float>();
    }

    if (qc.TokenAvailable()) {
        // max
        params.max = qc.ExpectFloatToken<float>();
    }

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "wrap")) {
            params.loop = PoseParameterParams::Loop::Wrap;
        }
        else if (vstd::iequals(qc.token, "loop")) {
            params.loop = PoseParameterParams::Loop::Value;
            qc.ExpectFloatToken(params.loopValue);
        }
    }

    ApplyPoseParameter(ctx, params);
}

void Cmd_OverrideMaterial(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    ApplyOverrideMaterial(ctx, qc.token.view());
}

void Cmd_RenameMaterial(StudioMdlState &ctx, ScriptTokenizer &qc) {
    char from[256];
    char to[256];

    qc.ExpectToken();
    strcpy(from, qc.token);

    qc.ExpectToken();
    strcpy(to, qc.token);

    int i;
    for (i = 0; i < ctx.materials.textures.size(); i++) {
        if (vstd::iequals(ctx.materials.textures[i].name, from)) {
            strcpy(ctx.materials.textures[i].name, to);
            return;
        }
    }

    for (i = 0; i < ctx.materials.textures.size(); i++) {
        if (vstd::ifind(ctx.materials.textures[i].name, from) != std::string_view::npos) {
            Msg("$renamematerial fell back to partial match: Replacing {} with {}.\n",
                ctx.materials.textures[i].name.c_str(), to);
            strcpy(ctx.materials.textures[i].name, to);
            return;
        }
    }

    qc.Error("unknown material \"{}\" in rename\n", from);
}

void Cmd_Eyeposition(StudioMdlState &ctx, ScriptTokenizer &qc) {
    const float a = qc.ExpectFloatToken<float>();
    const float b = qc.ExpectFloatToken<float>();
    const float c = qc.ExpectFloatToken<float>();
    ApplyEyePosition(ctx, a, b, c);
}

void Cmd_MaxEyeDeflection(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    ApplyMaxEyeDeflection(ctx, qc.TokenFloat());
}

//-----------------------------------------------------------------------------
// Cmd_AddSearchDir: add the custom defined path to an array that we will add to the search paths
//-----------------------------------------------------------------------------
void Cmd_AddSearchDir(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    if (!ctx.options.quiet) {
        MdlPrint("New search path: {}\n", qc.token);
    }
    CmdLib_AddNewSearchPath(ctx.paths.content, qc.token.view());
}

//-----------------------------------------------------------------------------
// Cmd_Illumposition
//-----------------------------------------------------------------------------
void Cmd_Illumposition(StudioMdlState &ctx, ScriptTokenizer &qc) {
    const float x = qc.ExpectFloatToken<float>("illumposition <x>");
    const float y = qc.ExpectFloatToken<float>("illumposition <y>");
    const float z = qc.ExpectFloatToken<float>("illumposition <z>");

    std::string_view attachmentBone;
    if (qc.TokenAvailable()) {
        qc.ExpectToken();
        attachmentBone = qc.token.view();
    }
    ApplyIllumPosition(ctx, x, y, z, attachmentBone);
}

//-----------------------------------------------------------------------------
// Parse Cmd_Modelname
//-----------------------------------------------------------------------------
static void Cmd_Modelname(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken("<path>");

    if (qc.token[0] == '/' || qc.token[0] == '\\') {
        MdlWarning("$modelname key has slash as first character. Removing.\n");
        ProcessModelName(ctx, &qc.token[1]);
    }
    else {
        ProcessModelName(ctx, qc.token.view());
    }
}

static void Cmd_InternalName(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken("<path>");
    ApplyInternalName(ctx, qc.token.view());
}


static void Cmd_PreserveTriangleOrder(StudioMdlState &ctx, ScriptTokenizer &_) {
    ApplyPreserveTriangleOrder(ctx);
}

static void Cmd_Autocenter(StudioMdlState &ctx, ScriptTokenizer &_) {
    ApplyAutocenter(ctx);
}


//-----------------------------------------------------------------------------
// Parse + process the studio options from a .qc file
//-----------------------------------------------------------------------------
void Option_Studio(StudioMdlState &ctx, ScriptTokenizer &qc, s_model_t* pmodel) {
    const auto pSourceSkin = MakeScratchElement<CDmeSourceSkin>("");

    // Set defaults
    pSourceSkin->m_flScale = ctx.script.defaultscale;
    pSourceSkin->m_bQuadSubd = false;
    pSourceSkin->m_bFlipTriangles = false;

    if (ParseOptionStudio(qc, pSourceSkin.get())) {
        ProcessOptionStudio(ctx, pmodel, pSourceSkin->GetRelativeFileName(), pSourceSkin->m_flScale,
                            pSourceSkin->m_bFlipTriangles, pSourceSkin->m_bQuadSubd);
    }
}

// Adds a "blank" model to the bodypart
int Option_Blank(StudioMdlState &ctx, s_bodypart_t &bodypart) {
    ApplyBlankModel(ctx, bodypart);
    return 0;
}

void Cmd_Bodygroup(StudioMdlState &ctx, ScriptTokenizer &qc) {
    vstd::FunctionTimingRecorder _("Parse");
    int is_started = 0;

    qc.ExpectToken("bodygroup <name>");

    s_bodypart_t &bodypart = ApplyBodyPart(ctx, qc.token.view());

    do {
        if (!qc.GetToken(true))
            qc.Error("$bodygroup {}: missing '}}' at end of file", bodypart.name);
        if (qc.token[0] == '{') {
            is_started = 1;
        }
        else if (qc.token[0] == '}') {
            break;
        }
        else if (vstd::iequals("studio", qc.token)) {
            int imodel;
            auto* pmodel = ApplyBodyModel(ctx, bodypart, imodel);

            Option_Studio(ctx, qc, pmodel);

            // Body command should add any flex commands in the source loaded
            PostProcessSource(ctx, pmodel->source, imodel);
        }
        else if (vstd::iequals("blank", qc.token)) {
            Option_Blank(ctx, bodypart);
        }
        else {
            qc.Error("unknown bodygroup option: \"{}\"\n", qc.token);
        }
    } while (1);
}

void Cmd_AppendBlankBodygroup(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // stick a blank bodygroup on the end of the current part
    if (ctx.body.bodypart.empty()) {
        qc.Error("$appendblankbodygroup: no bodygroup to append to\n");
    }
    Option_Blank(ctx, ctx.body.bodypart.back());
}

void Cmd_BodygroupPreset(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken("preset <name>");

    // make sure this name is unused
    if (HasBodyGroupPreset(ctx, qc.token.view())) {
        qc.Error("Error: bodygroup preset \"{}\" already exists.\n", qc.token);
        return;
    }

    BodyGroupPresetBuilder newpreset;
    newpreset.name = qc.token.view();


    do {
        if (!qc.GetToken(true)) {
            qc.Error("$bodygrouppreset {}: missing '}}' at end of file", newpreset.name);
        }
        else if (qc.token[0] == '{') {
        }
        else if (qc.token[0] == '}') {
            break;
        }
        else {
            //gather up name:value pairs into a baked bodygroup value and mask

            bool bFoundPart = false;
            for (const s_bodypart_t &bodypart: ctx.body.bodypart) {
                if (!V_strcmp(bodypart.name, qc.token)) {
                    int iValue;
                    qc.ExpectIntToken("bodygroup value", iValue);
                    if (const std::string error = ApplyBodyGroupPresetValue(newpreset, bodypart, iValue); !error.empty()) {
                        qc.Error("{}", error);
                        return;
                    }

                    bFoundPart = true;
                }
            }

            if (!bFoundPart) {
                qc.Error("Error: can't find any bodygroups named \"{}\".\n", qc.token);
                return;
            }
        }
    } while (1);

    ApplyBodyGroupPreset(ctx, newpreset);
}

//-----------------------------------------------------------------------------
// Parse the body command from a .qc file
//-----------------------------------------------------------------------------
void Cmd_Body(StudioMdlState &ctx, ScriptTokenizer &qc) {
    vstd::FunctionTimingRecorder _("Parse");
    qc.ExpectToken("body <name>");

    const auto pSourceSkin = MakeScratchElement<CDmeSourceSkin>("");

    // Set defaults
    pSourceSkin->m_flScale = ctx.script.defaultscale;

    pSourceSkin->m_SkinName = qc.token;
    if (ParseOptionStudio(qc, pSourceSkin.get())) {
        ProcessCmdBody(ctx, pSourceSkin->GetRelativeFileName(), pSourceSkin->m_SkinName.Get(),
                       pSourceSkin->m_flScale, pSourceSkin->m_bFlipTriangles, pSourceSkin->m_bQuadSubd);
    }
}

//-----------------------------------------------------------------------------
// Add all attachments from the source to the global attachment list
// stopping when the ctx.attachment array is full.  Duplicate attachments
// will be purged later after they are linked to bones
//-----------------------------------------------------------------------------
void AddBodyAttachments(StudioMdlState &ctx, s_source_t* pSource) {
    for (size_t i = 0; i < pSource->m_Attachments.size(); ++i) {
        if (ctx.bones.attachments.size() >= ctx.bones.attachments.capacity()) {
            MdlWarning("Too Many Attachments (Max {}), Ignoring Attachment {}:{}\n",
                       ctx.bones.attachments.capacity(), pSource->filename,
                       pSource->m_Attachments[i].name);
            continue;;
        }
        ctx.bones.attachments.emplace_back(pSource->m_Attachments[i]);
    }
}

//-----------------------------------------------------------------------------
// Adds flex controller data to a particular source
//-----------------------------------------------------------------------------
void AddFlexControllers(StudioMdlState &ctx, s_source_t* pSource) {
    std::vector<int> &r2s = pSource->m_rawIndexToRemapSourceIndex;
    std::vector<int> &r2l = pSource->m_rawIndexToRemapLocalIndex;
    std::vector<int> &l2i = pSource->m_leftRemapIndexToGlobalFlexControllIndex;
    std::vector<int> &r2i = pSource->m_rightRemapIndexToGlobalFlexControllIndex;

    // Number of Raw controls in this source
    const int nRawControlCount = std::ssize(pSource->m_CombinationControls);
    // Initialize rawToRemapIndices
    r2s.assign(nRawControlCount, -1);
    r2l.assign(nRawControlCount, -1);

    // Number of Remapped Controls in this source
    const int nRemappedControlCount = std::ssize(pSource->m_FlexControllerRemaps);
    l2i.resize(nRemappedControlCount);
    r2i.resize(nRemappedControlCount);

    for (int i = 0; i < nRemappedControlCount; ++i) {
        s_flexcontrollerremap_t &remapControl = pSource->m_FlexControllerRemaps[i];

        // Number of Raw Controls In This Remapped Control
        const int nRemappedRawControlCount = remapControl.m_RawControls.size();

        // Figure out the mapping from raw to remapped
        for (int j = 0; j < nRemappedRawControlCount; ++j) {
            for (int k = 0; k < nRawControlCount; ++k) {
                if (remapControl.m_RawControls[j] == pSource->m_CombinationControls[k].name) {
                    Assert(r2s[k] == -1);
                    Assert(r2l[k] == -1);
                    r2s[k] = i; // The index of the remapped control
                    r2l[k] = j; // The index of which control this is in the remap
                    break;
                }
            }
        }

        if (remapControl.m_bIsStereo) {
            // The controls have to be named 'right_' and 'left_' and right has to be first for
            // hlfaceposer to recognize them

            // See if we can add two more flex controllers
            if ((ctx.flex.flexcontrollers.size() + 1) >= ctx.flex.flexcontrollers.capacity())
                MdlError("Line {}: Too many flex controllers, max {}, cannot add split control {} from source {}",
                         ctx.input.iLinecount, ctx.flex.flexcontrollers.capacity(),
                         remapControl.m_Name,
                         pSource->filename);


            const std::string rightName = "right_" + remapControl.m_Name;
            const std::string leftName = "left_" + remapControl.m_Name;

            r2i[i] = remapControl.m_RightIndex = ctx.flex.flexcontrollers.size();
            {
                auto &pController = ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                    .name = rightName.c_str(),
                    .type = rightName.c_str()
                });

                if (remapControl.m_RemapType == _2way ||
                    remapControl.m_RemapType == Eyelid) {
                    pController.min = -1.0f;
                    pController.max = 1.0f;
                }
                else {
                    pController.min = 0.0f;
                    pController.max = 1.0f;
                }
            }

            l2i[i] = remapControl.m_LeftIndex = ctx.flex.flexcontrollers.size();
            {
                auto &pController = ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                    .name = leftName.c_str(),
                    .type = leftName.c_str()
                });

                if (remapControl.m_RemapType == _2way ||
                    remapControl.m_RemapType == Eyelid) {
                    pController.min = -1.0f;
                    pController.max = 1.0f;
                }
                else {
                    pController.min = 0.0f;
                    pController.max = 1.0f;
                }
            }
        }
        else {
            // See if we can add one more flex controller
            if (ctx.flex.flexcontrollers.size() >= ctx.flex.flexcontrollers.capacity())
                MdlError("Line {}: Too many flex controllers, max {}, cannot add control {} from source {}",
                         ctx.input.iLinecount, ctx.flex.flexcontrollers.capacity(),
                         remapControl.m_Name,
                         pSource->filename);

            r2i[i] = l2i[i] = remapControl.m_Index = ctx.flex.flexcontrollers.size();
            s_flexcontroller_t &pController = ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = remapControl.m_Name.c_str(),
                .type = remapControl.m_Name.c_str()
            });

            if (remapControl.m_RemapType == _2way ||
                remapControl.m_RemapType == Eyelid) {
                pController.min = -1.0f;
                pController.max = 1.0f;
            }
            else {
                pController.min = 0.0f;
                pController.max = 1.0f;
            }
        }

        if (remapControl.m_RemapType == Nway ||
            remapControl.m_RemapType == Eyelid) {
            if (ctx.flex.flexcontrollers.size() >= ctx.flex.flexcontrollers.capacity())
                MdlError(
                    "Line {}: Too many flex controllers, max {}, cannot add value control for nWay {} from source {}",
                    ctx.input.iLinecount, ctx.flex.flexcontrollers.capacity(), remapControl.m_Name,
                    pSource->filename);

            remapControl.m_MultiIndex = ctx.flex.flexcontrollers.size();

            const std::string multiName = "multi_" + remapControl.m_Name;

            ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = multiName.c_str(),
                .type = multiName.c_str(),
                .min = -1.0f,
                .max = 1.0f
            });
        }

        if (remapControl.m_RemapType == Eyelid) {
            // Make a blink controller

            if (ctx.flex.flexcontrollers.size() >= ctx.flex.flexcontrollers.capacity())
                MdlError(
                    "Line {}: Too many flex controllers, max {}, cannot add value control for nWay {} from source {}",
                    ctx.input.iLinecount, ctx.flex.flexcontrollers.capacity(), remapControl.m_Name,
                    pSource->filename);

            remapControl.m_BlinkController = ctx.flex.flexcontrollers.size();
            ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = "blink",
                .type = "blink",
                .min = 0.0f,
                .max = 1.0f
            });
        }
    }

#ifdef _DEBUG
    for (int j = 0; j != nRawControlCount; ++j) {
        Assert(r2s[j] != -1);
        Assert(r2l[j] != -1);
    }
#endif // def _DEBUG
}


//-----------------------------------------------------------------------------
// Adds flex controller remappers
//-----------------------------------------------------------------------------
void AddBodyFlexRemaps(StudioMdlState &ctx, s_source_t* pSource) {
    const int nCount = std::ssize(pSource->m_FlexControllerRemaps);
    for (int i = 0; i < nCount; ++i) {
        ctx.flex.FlexControllerRemap.emplace_back();
        s_flexcontrollerremap_t &remap = ctx.flex.FlexControllerRemap.back();
        remap = pSource->m_FlexControllerRemaps[i];
    }
}

//-----------------------------------------------------------------------------
// Process a body command
//-----------------------------------------------------------------------------
void AddBodyFlexData(StudioMdlState &ctx, s_source_t* pSource, int imodel, bool bAddControllers) {
    vstd::FunctionTimingRecorder _("API");
    pSource->m_nKeyStartIndex = vstd::as_signed_checked<int>(ctx.flex.flexkeys.size());

    // Add flex keys
    const auto nCount = std::ssize(pSource->m_FlexKeys);
    for (int i = 0; i < nCount; ++i) {
        s_flexkey_t &key = pSource->m_FlexKeys[i];

        if (ctx.flex.flexkeys.size() >= ctx.flex.flexkeys.capacity())
            MdlError("Line {}: Too many flex keys, max {}, cannot add flexKey {} from source {}",
                     ctx.input.iLinecount, MAXSTUDIOFLEXKEYS, key.animationname,
                     pSource->filename);

        auto &new_flex = ctx.flex.flexkeys.emplace(key);
        new_flex.imodel = imodel;

        // flexpair was set up in AddFlexKey
        if (key.flexpair) {
            char mod[512];
            Q_snprintf(mod, sizeof(mod), "%sL", key.animationname.c_str());
            new_flex.flexdesc = Add_Flexdesc(ctx, mod);
            Q_snprintf(mod, sizeof(mod), "%sR", key.animationname.c_str());
            new_flex.flexpair = Add_Flexdesc(ctx, mod);
        }
        else {
            new_flex.flexdesc = Add_Flexdesc(ctx, key.animationname);
            new_flex.flexpair = 0;
        }
    }

    // Flex controllers and their remaps are global: pieces of a split model (ClampMaxVerticesPerModel) share the
    // ones that were added for the original source and must not add them again.
    if (bAddControllers) {
        AddFlexControllers(ctx, pSource);

        AddBodyFlexRemaps(ctx, pSource);
    }
}


void Option_Eyeball(StudioMdlState &ctx, ScriptTokenizer &qc, s_model_t* pmodel) {
    EyeballParams params;
    StudioName name;
    vstd::FixedString<256> szMeshMaterial;

    // name
    qc.ExpectStringToken(name);

    // bone name
    qc.ExpectToken();
    const std::string bone(qc.token.view());

    // X, Y, Z
    qc.ExpectFloatToken(params.position[0]);
    qc.ExpectFloatToken(params.position[1]);
    qc.ExpectFloatToken(params.position[2]);

    // mesh material
    qc.ExpectStringToken(szMeshMaterial);

    // diameter
    qc.ExpectToken();
    params.diameter = qc.TokenFloat();

    // Z angle offset
    qc.ExpectToken();
    params.zAngleDegrees = qc.TokenFloat();

    // iris material (no longer used, but we need to remove the token)
    qc.ExpectToken();

    // pupil scale
    qc.ExpectToken();
    params.pupilScale = qc.TokenFloat();

    params.name = name.view();
    params.bone = bone;
    params.material = szMeshMaterial.view();
    if (const std::string error = ApplyEyeball(ctx, pmodel, params); !error.empty()) {
        qc.Error("{}", error);
    }
}


//-----------------------------------------------------------------------------
//
// A vertex cache animation file is a special case of a VTA file
// Same format
// Frame 0 is the defaultflex frame
// All other frames will get a flexdesc of "f#" where # [0,frameCount-1]
// Then an NWAY controller will be defined to play back the flex data
// as an animation as the controller goes from [0,1]
//-----------------------------------------------------------------------------
static void Option_VertexCacheAnimationFile(StudioMdlState &ctx, const std::string_view vtaFilePath, const size_t modelIndex) {
    CheckVertexCacheAnimationAllowed(ctx);

    s_source_t* pVtaSource = Load_Source(ctx, vtaFilePath, "vta");

    if (pVtaSource->m_Animations.empty()) {
        MdlError("{}: No animations in VertexCacheAnimationFile \"{}\"\n", __FUNCTION__, vtaFilePath);
        return;
    }

    // frames 1.. of the VTA, one flex each
    std::vector<int> frames;
    const int nActualFrameCount = pVtaSource->m_Animations.front().numframes - 1;
    for (int i = 0; i < nActualFrameCount; ++i) {
        frames.push_back(i + 1);
    }

    ApplyVertexCacheAnimation(ctx, modelIndex, pVtaSource, vstd::paths::file_base(pVtaSource->filename), frames);
}


void Option_Spherenormals(StudioMdlState &ctx, ScriptTokenizer &qc, s_source_t* psource) {
    Vector pos;
    vstd::FixedString<256> szMeshMaterial;

    // mesh material
    qc.ExpectStringToken(szMeshMaterial);

    // X
    qc.ExpectFloatToken(pos[0]);

    // Y
    qc.ExpectFloatToken(pos[1]);

    // Z
    qc.ExpectFloatToken(pos[2]);

    if (const std::string error = ApplySphereNormals(ctx, psource, szMeshMaterial.view(), pos); !error.empty()) {
        qc.Error("{}", error);
    }
}


//-----------------------------------------------------------------------------
// Purpose:   Handle the eyelid option using a DMX instead of a VTA source
// QC Syntax: dmxeyelid <upper|lower> <source> lowerer <delta> <-0.20 neutral F00 0.19 raiser F02 0.28 righteyeball righteye lefteyeball lefteye
// e.g: dmxeyelid upper "coach_model_merged.dmx" lowerer F01 -0.20 neutral F00 0.19 raiser F02 0.28 righteyeball righteye lefteyeball lefteye
//-----------------------------------------------------------------------------
void Option_DmxEyelid(StudioMdlState &ctx, ScriptTokenizer &qc, int imodel) {
    DmxEyelidParams params;

    // upper | lower
    qc.ExpectToken();
    if (vstd::iequals("upper", qc.token)) {
        params.upper = true;
    }
    else if (vstd::iequals("lower", qc.token)) {
        params.upper = false;
    }
    else {
        qc.Error("$model dmxeyelid, expected one of \"upper\", \"lower\"");
        return;
    }

    // (exr)
    std::string sSourceFile;
    qc.ExpectToken();
    sSourceFile = qc.token;

    s_source_t* pSource = Load_Source(ctx, sSourceFile.c_str(), "dmx");
    if (!pSource) {
        qc.Error("({}) : {}:  Cannot load source file \"{}\"\n", ctx.input.iLinecount,
                 ctx.input.szLine, sSourceFile);
        return;
    }

    constexpr const char* kSuffix[kEyelidTypeCount] = {"lowerer", "neutral", "raiser"};

    std::string sRightEyeball;
    std::string sLeftEyeball;

    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        bool bTokenHandled = false;

        for (int i = 0; i < kEyelidTypeCount; ++i) {
            if (vstd::iequals(qc.token, kSuffix[i])) {
                bTokenHandled = true;

                qc.ExpectToken();
                const s_sourceanim_t* pSourceAnim = FindNewStyleVertexAnim(pSource, qc.token.view());
                if (pSourceAnim == nullptr) {
                    qc.Error("({}) : {}:  No DMX vertex animation named \"{}\" in source \"{}\"\n",
                             ctx.input.iLinecount,
                             ctx.input.szLine, qc.token, sSourceFile);
                    return;
                }
                params.animation[i] = pSourceAnim->animationname.view();

                // target
                qc.ExpectFloatToken(params.target[i]);

                break;
            }
        }

        if (bTokenHandled)
            continue;

        else if (vstd::iequals(qc.token, "righteyeball")) {
            qc.ExpectToken();
            sRightEyeball = qc.token;
        }
        else if (vstd::iequals(qc.token, "lefteyeball")) {
            qc.ExpectToken();
            sLeftEyeball = qc.token;
        }
    }

    params.rightEyeball = sRightEyeball;
    params.leftEyeball = sLeftEyeball;
    if (const std::string error = ApplyDmxEyelid(ctx, imodel, params, pSource); !error.empty()) {
        qc.Error("{}", error);
    }
}

int Option_Mouth(StudioMdlState &ctx, ScriptTokenizer &qc, s_model_t* pmodel) {
    // index
    qc.ExpectToken();
    const int index = qc.TokenInt();
    // flex controller name
    qc.ExpectToken();
    const std::string flex(qc.token.view());

    // bone name
    StudioName bone;
    qc.ExpectStringToken(bone);

    // vector
    Vector forward;
    qc.ExpectFloatToken(forward[0]);
    qc.ExpectFloatToken(forward[1]);
    qc.ExpectFloatToken(forward[2]);

    ApplyMouth(ctx, index, flex, bone.view(), forward);
    return 0;
}

void Option_Flexcontroller(StudioMdlState &ctx, ScriptTokenizer &qc, s_model_t* pmodel) {
    float range_min = 0.0f;
    float range_max = 1.0f;

    // g_flex
    qc.ExpectToken();
    const std::string type(qc.token.view());

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "range")) {
            qc.ExpectFloatToken(range_min);

            qc.ExpectFloatToken(range_max);
        }
        else if (const std::string error = ApplyFlexController(ctx, type, qc.token.view(), range_min, range_max);
                 !error.empty()) {
            qc.Error("{}", error);
        }
    }

    // this needs to be per model.
}

void Option_NoAutoDMXRules(StudioMdlState &ctx, s_source_t* pSource) {
    ApplyNoAutoDmxRules(ctx, pSource);
}

//-----------------------------------------------------------------------------
// Purpose: throw away all the options for a specific sequence or animation
//-----------------------------------------------------------------------------

int ParseEmpty(ScriptTokenizer &qc) {
    int depth = 0;

    while (1) {
        if (depth > 0) {
            if (!qc.GetToken(true))
                qc.Error("missing '}}' at end of file");
        }
        else {
            if (!qc.TokenAvailable()) {
                break;
            }
            else {
                qc.ExpectToken();
            }
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            return 1;
        }
        if (vstd::iequals("{", qc.token)) {
            depth++;
        }
        else if (vstd::iequals("}", qc.token)) {
            depth--;
        }

        if (depth < 0) {
            qc.Error("missing {{\n");
        }
    }

    return 0;
}

void Option_Flex(StudioMdlState &ctx, ScriptTokenizer &qc, std::string_view name, std::string_view vtafile,
                 int imodel, float pairsplit, bool isDefault = false) {
    FlexKeyParams params;
    params.name = name;
    params.stereo = pairsplit != 0;
    params.split = pairsplit;

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "frame")) {
            qc.ExpectToken();

            params.frame = qc.TokenInt();
        }
        else if (vstd::iequals(qc.token, "position")) {
            qc.ExpectFloatToken(params.position);
        }
        else if (vstd::iequals(qc.token, "split")) {
            qc.ExpectFloatToken(params.split);
        }
        else if (vstd::iequals(qc.token, "decay")) {
            qc.ExpectFloatToken(params.decay);
        }
        else {
            qc.Error("unknown option: {}", qc.token);
        }
    }

    // link to source
    s_source_t* pSource = Load_Source(ctx, vtafile, "vta");
    const std::string error = isDefault ? ApplyDefaultFlexKey(ctx, params, pSource, imodel)
                                        : ApplyFlexKey(ctx, params, pSource, imodel);
    if (!error.empty()) {
        qc.Error("{}", error);
    }
    // this needs to be per model.
}



void Option_Flexrule(StudioMdlState &ctx, ScriptTokenizer &qc, s_model_t* /* pmodel */, const char* name) {
    if (ctx.flex.flexrules.size() + 1 > ctx.flex.flexrules.capacity()) {
        qc.Error("Too many flex rules (max {})\n", ctx.flex.flexrules.capacity());
    }

    int flexdesc;
    for (flexdesc = 0; flexdesc < ctx.flex.flexdescs.size(); flexdesc++) {
        if (vstd::iequals(name, ctx.flex.flexdescs[flexdesc].FACS)) {
            break;
        }
    }

    if (flexdesc >= ctx.flex.flexdescs.size()) {
        qc.Error("Rule for unknown flex {}\n", name);
    }

    const FlexRuleLookup lookup{
        .flex = [&ctx](std::string_view flex) {
            for (int k = 0; k < ctx.flex.flexdescs.size(); k++) {
                if (vstd::iequals(flex, ctx.flex.flexdescs[k].FACS))
                    return k;
            }
            return -1;
        },
        .controller = [&ctx](std::string_view controller) {
            for (int k = 0; k < ctx.flex.flexcontrollers.size(); k++) {
                if (vstd::iequals(controller, ctx.flex.flexcontrollers[k].name))
                    return k;
            }
            return -1;
        },
    };

    s_flexrule_t rule{}; // operators have no operand: d must not carry stack garbage into the MDL
    ParseFlexRule(qc, ctx.flex.flexdescs[flexdesc].FACS.view(), flexdesc, lookup, rule);
    ctx.flex.flexrules.emplace_back(rule);
}

//-----------------------------------------------------------------------------
// Add A Body Flex Rule
//-----------------------------------------------------------------------------
void AddBodyFlexFetchRule(
    s_source_t* pSource,
    s_flexrule_t* pRule,
    int rawIndex,
    const std::vector<int> &pRawIndexToRemapSourceIndex,
    const std::vector<int> &pRawIndexToRemapLocalIndex,
    const std::vector<int> &pRemapSourceIndexToGlobalFlexControllerIndex) {
    // Lookup the various indices of the requested input to fetch
    // Relative to the remapped controls in the current s_source_t
    const int remapSourceIndex = pRawIndexToRemapSourceIndex[rawIndex];
    // Relative to the specific remapped control
    const int remapLocalIndex = pRawIndexToRemapLocalIndex[rawIndex];
    // The global flex controller index that the user ultimately twiddles
    const int globalFlexControllerIndex = pRemapSourceIndexToGlobalFlexControllerIndex[remapSourceIndex];

    // Get the Remap record
    s_flexcontrollerremap_t &remap = pSource->m_FlexControllerRemaps[remapSourceIndex];
    switch (remap.m_RemapType) {
        case Passthru:
            // Easy As!
            pRule->op[pRule->numops].op = StudioOpcode::Fetch1;
            pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
            pRule->numops++;
            break;

        case Eyelid:
            if (remapLocalIndex == 0) {
                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value =
                        remap.m_EyesUpDownFlexController >= 0 ? remap.m_EyesUpDownFlexController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = remap.m_BlinkController >= 0 ? remap.m_BlinkController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = globalFlexControllerIndex; // CloseLid
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::DmeLowerEyelid;
                pRule->op[pRule->numops].d.index = remap.m_MultiIndex; // CloseLidV
                pRule->numops++;
            }
            else {
                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value =
                        remap.m_EyesUpDownFlexController >= 0 ? remap.m_EyesUpDownFlexController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = remap.m_BlinkController >= 0 ? remap.m_BlinkController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = globalFlexControllerIndex; // CloseLid
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::DmeUpperEyelid;
                pRule->op[pRule->numops].d.index = remap.m_MultiIndex; // CloseLidV
                pRule->numops++;
            }
            break;

        case _2way:
            // A little trickier... local index 0 is on the left, local index 1 is on the right
            // Left Equivalent RemapVal( -1.0, 0.0, 0.0, 1.0 )
            // Right Equivalent RemapVal( 0.0, 1.0, 0.0, 1.0 )
            if (remapLocalIndex == 0) {
                pRule->op[pRule->numops].op = StudioOpcode::TwoWay0;
                pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
                pRule->numops++;
            }
            else {
                pRule->op[pRule->numops].op = StudioOpcode::TwoWay1;
                pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
                pRule->numops++;
            }
            break;

        case Nway: {
            int nRemapCount = remap.m_RawControls.size();
            float flStep = (nRemapCount > 2) ? 2.0f / (nRemapCount - 1) : 0.0f;

            if (remapLocalIndex == 0) {
                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = -11.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = -10.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = -1.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = -1.0f + flStep;
                pRule->numops++;
            }
            else if (remapLocalIndex == nRemapCount - 1) {
                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = 1.0f - flStep;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = 1.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = 10.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = 11.0f;
                pRule->numops++;
            }
            else {
                float flPeak = remapLocalIndex * flStep - 1.0f;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = flPeak - flStep;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = flPeak;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = flPeak;
                pRule->numops++;

                pRule->op[pRule->numops].op = StudioOpcode::Const;
                pRule->op[pRule->numops].d.value = flPeak + flStep;
                pRule->numops++;
            }

            pRule->op[pRule->numops].op = StudioOpcode::Const;
            pRule->op[pRule->numops].d.value = remap.m_MultiIndex;
            pRule->numops++;

            pRule->op[pRule->numops].op = StudioOpcode::NWay;
            pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
            pRule->numops++;
        }
        break;
        default:
            Assert(0);
            // This is an error condition
            pRule->op[pRule->numops].op = StudioOpcode::Const;
            pRule->op[pRule->numops].d.value = 1.0f;
            pRule->numops++;
            break;
    }
}


//-----------------------------------------------------------------------------
// Add A Body Flex Rule
//-----------------------------------------------------------------------------
void AddBodyFlexRule(StudioMdlState &ctx, 
    s_source_t* pSource,
    s_combinationrule_t &rule,
    int nFlexDesc,
    const std::vector<int> &pRawIndexToRemapSourceIndex,
    const std::vector<int> &pRawIndexToRemapLocalIndex,
    const std::vector<int> &pRemapSourceIndexToGlobalFlexControllerIndex) {
    if (ctx.flex.flexrules.size() >= ctx.flex.flexrules.capacity())
        MdlError("Line {}: Too many flex rules, max {}",
                 ctx.input.iLinecount, ctx.flex.flexrules.capacity());

    s_flexrule_t &pRule = ctx.flex.flexrules.emplace_back(s_flexrule_t{
        .flex = nFlexDesc,
    });

    // This will multiply the combination together
    const int nCombinationCount = std::ssize(rule.m_Combination);
    if (nCombinationCount) {
        for (int j = 0; j < nCombinationCount; ++j) {
            // Handle any controller remapping
            AddBodyFlexFetchRule(pSource, &pRule, rule.m_Combination[j],
                                 pRawIndexToRemapSourceIndex, pRawIndexToRemapLocalIndex,
                                 pRemapSourceIndexToGlobalFlexControllerIndex);
        }

        if (nCombinationCount > 1) {
            pRule.op[pRule.numops].op = StudioOpcode::Combo;
            pRule.op[pRule.numops].d.index = nCombinationCount;
            pRule.numops++;
        }
    }

    // This will multiply in the suppressors
    const int nDominators = std::ssize(rule.m_Dominators);
    for (int j = 0; j < nDominators; ++j) {
        const int nFactorCount = std::ssize(rule.m_Dominators[j]);
        if (nFactorCount) {
            for (int k = 0; k < nFactorCount; ++k) {
                AddBodyFlexFetchRule(pSource, &pRule, rule.m_Dominators[j][k],
                                     pRawIndexToRemapSourceIndex, pRawIndexToRemapLocalIndex,
                                     pRemapSourceIndexToGlobalFlexControllerIndex);
            }

            pRule.op[pRule.numops].op = StudioOpcode::Dominate;
            pRule.op[pRule.numops].d.index = nFactorCount;
            pRule.numops++;
        }
    }
}


void AddBodyFlexRules(StudioMdlState &ctx, s_source_t* pSource) {
    vstd::FunctionTimingRecorder _("API");
    for (int i = 0; i < std::ssize(pSource->m_FlexControllerRemaps); ++i) {
        s_flexcontrollerremap_t &remap = pSource->m_FlexControllerRemaps[i];
        if (remap.m_RemapType == Eyelid && !remap.m_EyesUpDownFlexName.empty()) {
            for (int j = 0; j < ctx.flex.flexcontrollers.size(); ++j) {
                if (ctx.flex.flexcontrollers[j].name.c_str() == remap.m_EyesUpDownFlexName) {
                    Assert(remap.m_EyesUpDownFlexController == -1);
                    remap.m_EyesUpDownFlexController = j;
                    break;
                }
            }
        }
    }

    for (int i = 0; i < std::ssize(pSource->m_CombinationRules); ++i) {
        s_combinationrule_t &rule = pSource->m_CombinationRules[i];
        const s_flexkey_t &flexKey = ctx.flex.flexkeys[pSource->m_nKeyStartIndex + rule.m_nFlex];
        AddBodyFlexRule(ctx, pSource, rule, flexKey.flexdesc,
                        pSource->m_rawIndexToRemapSourceIndex, pSource->m_rawIndexToRemapLocalIndex,
                        pSource->m_leftRemapIndexToGlobalFlexControllIndex);
        if (flexKey.flexpair != 0) {
            AddBodyFlexRule(ctx, pSource, rule, flexKey.flexpair,
                            pSource->m_rawIndexToRemapSourceIndex, pSource->m_rawIndexToRemapLocalIndex,
                            pSource->m_rightRemapIndexToGlobalFlexControllIndex);
        }
    }
}

void Option_Eyelid(StudioMdlState &ctx, ScriptTokenizer &qc, int imodel) {
    char type[256];
    char vtafile[256];

    // type
    qc.ExpectStringToken(type);

    // source
    qc.ExpectStringToken(vtafile);

    EyelidParams params;
    std::string eyeball;

    params.baseDesc = AddEyelidFlexdesc(ctx, type);

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        char localdesc[256];
        strcpy(localdesc, type);
        strcat(localdesc, "_");
        strcat(localdesc, qc.token);

        if (vstd::iequals(qc.token, "lowerer")) {
            qc.ExpectIntToken(params.frame[0]);
            qc.ExpectFloatToken(params.target[0]);
            params.desc[0] = AddEyelidFlexdesc(ctx, localdesc);
        }
        else if (vstd::iequals(qc.token, "neutral")) {
            qc.ExpectIntToken(params.frame[1]);
            qc.ExpectFloatToken(params.target[1]);
            params.desc[1] = AddEyelidFlexdesc(ctx, localdesc);
        }
        else if (vstd::iequals(qc.token, "raiser")) {
            qc.ExpectIntToken(params.frame[2]);
            qc.ExpectFloatToken(params.target[2]);
            params.desc[2] = AddEyelidFlexdesc(ctx, localdesc);
        }
        else if (vstd::iequals(qc.token, "split")) {
            qc.ExpectFloatToken(params.split);
        }
        else if (vstd::iequals(qc.token, "eyeball")) {
            qc.ExpectToken();
            eyeball = qc.token;
        }
        else {
            qc.Error("unknown option: {}", qc.token);
        }
    }

    params.type = type;
    params.kind = type[0] == 'u' ? EyelidKind::Upper : type[0] == 'l' ? EyelidKind::Lower : EyelidKind::Other;
    params.eyeball = eyeball;

    s_source_t* pSource = Load_Source(ctx, vtafile, "vta");
    if (const std::string error = ApplyEyelid(ctx, imodel, params, pSource); !error.empty()) {
        qc.Error("{}", error);
    }
}

void Cmd_Model(StudioMdlState &ctx, ScriptTokenizer &qc) {
    s_model_t* pmodel = ctx.arena.Create<s_model_t>();
    const int imodel = ctx.body.AddModel(pmodel);

    // name
    qc.ExpectStringToken("<name>", pmodel->name);

    // fake ctx.body.bodypart stuff
    s_bodypart_t &bodypart = ctx.body.AddBodyPart();
    strcpyn(bodypart.name, qc.token);
    bodypart.pmodel.push_back(pmodel);

    Option_Studio(ctx, qc, pmodel);

    if (pmodel->source) {
        // Body command should add any flex commands in the source loaded
        AddBodyFlexData(ctx, pmodel->source, imodel);
        AddBodyAttachments(ctx, pmodel->source);
    }

    int depth = 0;
    // vtafile is set by "flexfile" and used by the flex/defaultflex lines that follow it: keep it across tokens
    char FAC[256]{}, vtafile[256]{};
    while (1) {
        if (depth > 0) {
            if (!qc.GetToken(true))
                qc.Error("missing '}}' at end of file");
        }
        else {
            if (!qc.TokenAvailable()) {
                break;
            }
            else {
                qc.ExpectToken();
            }
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            return;
        }
        if (vstd::iequals("{", qc.token)) {
            depth++;
        }
        else if (vstd::iequals("}", qc.token)) {
            depth--;
        }
        else if (vstd::iequals("eyeball", qc.token)) {
            Option_Eyeball(ctx, qc, pmodel);
        }
        else if (vstd::iequals("eyelid", qc.token)) {
            Option_Eyelid(ctx, qc, imodel);
        }
        else if (vstd::iequals("dmxeyelid", qc.token)) {
            Option_DmxEyelid(ctx, qc, imodel);
        }
        else if (vstd::iequals("vcafile", qc.token)) {
            // vertex cache animation file
            qc.ExpectToken(); // file
            Option_VertexCacheAnimationFile(ctx, qc.token.view(), imodel);
        }
        else if (vstd::iequals("flex", qc.token)) {
            // g_flex
            qc.ExpectToken();
            strcpy(FAC, qc.token);
            if (depth == 0) {
                // file
                qc.ExpectToken();
                strcpy(vtafile, qc.token);
            }
            Option_Flex(ctx, qc, FAC, vtafile, imodel, 0.0);
            // FIXME: this needs to point to a model used, not loaded!!!
        }
        else if (vstd::iequals("flexpair", qc.token)) {
            // g_flex
            qc.ExpectToken();
            strcpy(FAC, qc.token);

            qc.ExpectToken();
            float split = qc.TokenFloat();

            if (depth == 0) {
                // file
                qc.ExpectToken();
                strcpy(vtafile, qc.token);
            }
            Option_Flex(ctx, qc, FAC, vtafile, imodel, split);
            // FIXME: this needs to point to a model used, not loaded!!!
        }
        else if (vstd::iequals("defaultflex", qc.token)) {
            if (depth == 0) {
                // file
                qc.ExpectToken();
                strcpy(vtafile, qc.token);
            }

            // g_flex
            // FIXME: this needs to point to a model used, not loaded!!!
            Option_Flex(ctx, qc, "default", vtafile, imodel, 0.0, true);
        }
        else if (vstd::iequals("flexfile", qc.token)) {
            // file
            qc.ExpectToken();
            strcpy(vtafile, qc.token);
        }
        else if (vstd::iequals("localvar", qc.token)) {
            while (qc.TokenAvailable()) {
                qc.ExpectToken();
                ApplyLocalVar(ctx, qc.token.view());
            }
        }
        else if (vstd::iequals("mouth", qc.token)) {
            Option_Mouth(ctx, qc, pmodel);
        }
        else if (vstd::iequals("flexcontroller", qc.token)) {
            Option_Flexcontroller(ctx, qc, pmodel);
        }
        else if (qc.token[0] == '%') {
            Option_Flexrule(ctx, qc, pmodel, &qc.token[1]);
        }
        else if (vstd::iequals("attachment", qc.token)) {
            // 	Option_Attachment( pmodel );
        }
        else if (vstd::iequals(qc.token, "spherenormals")) {
            Option_Spherenormals(ctx, qc, pmodel->source);
        }
        else if (vstd::iequals(qc.token, "noautodmxrules")) {
            Option_NoAutoDMXRules(ctx, pmodel->source);
        }
        else {
            qc.Error("unknown model option \"{}\"\n", qc.token);
        }

        if (depth < 0) {
            qc.Error("missing {{\n");
        }
    };

    if (!pmodel->source->bNoAutoDMXRules) {
        // Actually connect up the expressions between the Dme Flex Controllers & Flex Descriptors
        // In case there was data added by some other eyeball command (like eyelid)
        AddBodyFlexRules(ctx, pmodel->source);
    }
}

void Cmd_FakeVTA(StudioMdlState &ctx, ScriptTokenizer &qc) {
    int depth = 0;

    qc.ExpectToken();

    auto psource = ctx.arena.Create<s_source_t>();
    ctx.anim.sources.emplace_back(psource);
    psource->filename = qc.token;

    while (1) {
        if (depth > 0) {
            if (!qc.GetToken(true))
                qc.Error("missing '}}' at end of file");
        }
        else {
            if (!qc.TokenAvailable()) {
                break;
            }
            else {
                qc.ExpectToken();
            }
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            return;
        }
        if (vstd::iequals("{", qc.token)) {
            depth++;
        }
        else if (vstd::iequals("}", qc.token)) {
            depth--;
        }
        else if (vstd::iequals("appendvta", qc.token)) {
            char filename[256];
            // file
            qc.ExpectToken();
            strcpy(filename, qc.token);

            qc.ExpectToken();
            int frame = qc.TokenInt();

            AppendVTAtoOBJ(ctx, psource, filename, frame);
        }
    }
}


StudioMotionFlags lookupControl(char* string) {
    if (stricmp(string, "X") == 0) return StudioMotionFlags::X;
    if (stricmp(string, "Y") == 0) return StudioMotionFlags::Y;
    if (stricmp(string, "Z") == 0) return StudioMotionFlags::Z;
    if (stricmp(string, "XR") == 0) return StudioMotionFlags::XR;
    if (stricmp(string, "YR") == 0) return StudioMotionFlags::YR;
    if (stricmp(string, "ZR") == 0) return StudioMotionFlags::ZR;

    if (stricmp(string, "LX") == 0) return StudioMotionFlags::LX;
    if (stricmp(string, "LY") == 0) return StudioMotionFlags::LY;
    if (stricmp(string, "LZ") == 0) return StudioMotionFlags::LZ;
    if (stricmp(string, "LXR") == 0) return StudioMotionFlags::LXR;
    if (stricmp(string, "LYR") == 0) return StudioMotionFlags::LYR;
    if (stricmp(string, "LZR") == 0) return StudioMotionFlags::LZR;

    if (stricmp(string, "LM") == 0) return StudioMotionFlags::Linear;
    if (stricmp(string, "LQ") == 0) return StudioMotionFlags::QuadraticMotion;

    return StudioMotionFlags::Invalid;
}


void Cmd_IKChain(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    const std::string name(qc.token.view());
    StudioName boneName;
    qc.ExpectStringToken(boneName);

    IkChainParams params;
    params.name = name;
    params.boneName = boneName.view();

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (lookupControl(qc.token) != StudioMotionFlags::Invalid) {
            params.axis = lookupControl(qc.token);
            qc.ExpectFloatToken(params.value);
        }
        else if (vstd::iequals("height", qc.token)) {
            qc.ExpectFloatToken(params.height);
        }
        else if (vstd::iequals("pad", qc.token)) {
            qc.ExpectToken();
            params.radius = qc.TokenFloat() / 2.0;
        }
        else if (vstd::iequals("floor", qc.token)) {
            qc.ExpectFloatToken(params.floor);
        }
        else if (vstd::iequals("knee", qc.token)) {
            Vector knee;
            qc.ExpectFloatToken(knee.x);
            qc.ExpectFloatToken(knee.y);
            qc.ExpectFloatToken(knee.z);
            params.knee = knee;
        }
        else if (vstd::iequals("center", qc.token)) {
            Vector center;
            qc.ExpectFloatToken(center.x);
            qc.ExpectFloatToken(center.y);
            qc.ExpectFloatToken(center.z);
            params.center = center;
        }
    }

    ApplyIkChain(ctx, params);
}

void Cmd_IKAutoplayLock(StudioMdlState &ctx, ScriptTokenizer &qc) {
    StudioName chain;
    qc.ExpectStringToken(chain);

    const float positionWeight = qc.ExpectFloatToken<float>();
    const float localQWeight = qc.ExpectFloatToken<float>();
    ApplyIkAutoplayLock(ctx, chain.view(), positionWeight, localQWeight);
}

void Cmd_Root(StudioMdlState &ctx, ScriptTokenizer &qc) {
    StudioName bone;
    qc.ExpectStringToken("bone name", bone);
    ApplyRootBone(ctx, bone.view());
}

void Cmd_Controller(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken("controller <number>");
    int inputField;

    if (vstd::iequals("mouth", qc.token)) {
        inputField = 4;
    }
    else {
        inputField = qc.TokenInt();
    }
    StudioName bone;
    qc.ExpectStringToken("controller <bone_name>", bone);
    qc.ExpectToken("controller <axis>");
    const auto axis = lookupControl(qc.token);
    if (axis == StudioMotionFlags::Invalid) {
        MdlWarning("unknown ctx.bonecontroller type '{}'\n", qc.token);
        return;
    }
    float start, end;
    qc.ExpectFloatToken("controller <start>", start);
    qc.ExpectFloatToken("controller <end>", end);

    ApplyBoneController(ctx, inputField, bone.view(), axis, start, end);
}

void Cmd_ScreenAlign(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (qc.GetToken(false)) {
        const StudioName bone(qc.token.view());
        BoneFlags flags = BoneFlags::ScreenAlignSphere;

        if (qc.GetToken(false)) {
            if (vstd::iequals("sphere", qc.token)) {
                flags = BoneFlags::ScreenAlignSphere;
            }
            else if (vstd::iequals("cylinder", qc.token)) {
                flags = BoneFlags::ScreenAlignCylinder;
            }
        }
        ApplyScreenAlign(ctx, bone.view(), flags);
    }
    else {
        qc.Error("$screenalign: expected bone name\n");
    }
}

void Cmd_WorldAlign(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (qc.GetToken(false)) {
        ApplyWorldAlign(ctx, StudioName(qc.token.view()).view());
    }
    else {
        qc.Error("$worldalign: expected bone name\n");
    }
}

void Cmd_BBox(StudioMdlState &ctx, ScriptTokenizer &qc) {
    Vector mins, maxs;
    qc.ExpectFloatToken(mins[0]);

    qc.ExpectFloatToken(mins[1]);

    qc.ExpectFloatToken(mins[2]);

    qc.ExpectFloatToken(maxs[0]);

    qc.ExpectFloatToken(maxs[1]);

    qc.ExpectFloatToken(maxs[2]);

    ApplyBBox(ctx, mins, maxs);
}

void Cmd_BBoxOnlyVerts(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyBBoxOnlyVerts(ctx);
}

void Cmd_CBox(StudioMdlState &ctx, ScriptTokenizer &qc) {
    Vector mins, maxs;
    qc.ExpectFloatToken(mins[0]);

    qc.ExpectFloatToken(mins[1]);

    qc.ExpectFloatToken(mins[2]);

    qc.ExpectFloatToken(maxs[0]);

    qc.ExpectFloatToken(maxs[1]);

    qc.ExpectFloatToken(maxs[2]);

    ApplyCBox(ctx, mins, maxs);
}

void Cmd_Gamma(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyGamma(ctx, qc.ExpectFloatToken<float>());
}

void Cmd_TextureGroup(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (ctx.options.createMakefile) {
        return;
    }
    int depth = 0;
    size_t group = 0;


    qc.ExpectToken();

    // [skin family][index] = material name
    std::vector<std::vector<std::string>> rows;

    while (1) {
        if (!qc.GetToken(true)) {
            if (depth != 0)
                qc.Error("$texturegroup: missing '}}' at end of file");
            break;
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            break;
        }
        if (qc.token[0] == '{') {
            depth++;
        }
        else if (qc.token[0] == '}') {
            depth--;
            if (depth == 0)
                break;
            group++;
        }
        else if (depth == 2) {
            if (rows.size() <= group)
                rows.resize(group + 1);
            rows[group].emplace_back(qc.token.view());
        }
    }

    ApplyTextureGroup(ctx, rows, false);
}

void Cmd_Hitgroup(StudioMdlState &ctx, ScriptTokenizer &qc) {
    int group = 0;
    qc.ExpectIntToken(group);
    StudioName bone;
    qc.ExpectStringToken(bone);
    ApplyHitGroup(ctx, group, bone.view());
}

void Cmd_Hitbox(StudioMdlState &ctx, ScriptTokenizer &qc) {
    HitboxParams params;

    qc.ExpectToken("hitbox group number");
    params.group = qc.TokenInt();

    // Grab the bone name:
    StudioName boneName;
    qc.ExpectStringToken("hitbox name", boneName);
    params.boneName = boneName.view();

    qc.ExpectFloatToken("hitbox min X", params.mins[0]);
    qc.ExpectFloatToken("hitbox min Y", params.mins[1]);
    qc.ExpectFloatToken("hitbox min Z", params.mins[2]);
    qc.ExpectFloatToken("hitbox max X", params.maxs[0]);
    qc.ExpectFloatToken("hitbox max Y", params.maxs[1]);
    qc.ExpectFloatToken("hitbox max Z", params.maxs[2]);

    if (qc.TokenAvailable()) {
        qc.ExpectFloatToken(params.angles[0]);
        qc.ExpectFloatToken(params.angles[1]);
        qc.ExpectFloatToken(params.angles[2]);
    }

    if (qc.TokenAvailable()) {
        qc.ExpectFloatToken(params.capsuleRadius);
    }

    // Grab the hit box name if present:
    StudioName hitboxName;
    if (qc.TokenAvailable()) {
        qc.ExpectStringToken(hitboxName);
    }
    params.name = hitboxName.view();

    ApplyHitbox(ctx, params);
}

void Cmd_HitboxSet(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // Add a new hitboxset
    qc.ExpectToken();
    ApplyHitboxSet(ctx, qc.token.view());
}

//-----------------------------------------------------------------------------
// Assigns a default surface property to the entire model
//-----------------------------------------------------------------------------
void SetDefaultSurfaceProp(StudioMdlState &ctx, const char* pSurfaceProperty) {
    Q_strncpy(ctx.materials.pDefaultSurfaceProp, pSurfaceProperty,
              sizeof(ctx.materials.pDefaultSurfaceProp));
}

void Cmd_SurfaceProp(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    SetDefaultSurfaceProp(ctx, qc.token);
}

//-----------------------------------------------------------------------------
// Assigns a surface property to a particular joint
//-----------------------------------------------------------------------------
void Cmd_JointSurfaceProp(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // Get joint name...
    qc.ExpectToken();

    char pJointName[MAX_PATH];
    Q_strncpy(pJointName, qc.token, sizeof(pJointName));

    // surface property name
    qc.ExpectToken();
    AddSurfaceProp(ctx, pJointName, qc.token);
}

//-----------------------------------------------------------------------------
// Assigns a default contents to the entire model
//-----------------------------------------------------------------------------
void Cmd_Contents(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyContents(ctx, ParseContents(qc));
}

//-----------------------------------------------------------------------------
// Assigns contents to a particular joint
//-----------------------------------------------------------------------------
void Cmd_JointContents(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // Get joint name...
    qc.ExpectToken();
    const std::string jointName(qc.token.view());

    ApplyJointContents(ctx, jointName, ParseContents(qc));
}

void Cmd_BoneAlwaysSetup(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (ctx.options.createMakefile)
        return;

    // bone name
    StudioName bone;
    qc.ExpectStringToken(bone);
    ApplyBoneAlwaysSetup(ctx, bone.view());
}

void Internal_Cmd_Attachment(StudioMdlState &ctx, ScriptTokenizer &qc, std::optional<size_t> redefine) {
    if (ctx.options.createMakefile)
        return;

    // name
    qc.ExpectToken("attachment <name>");
    const std::string name(qc.token.view());

    AttachmentParams params;
    params.name = name;

    // bone name
    StudioName boneName;
    qc.ExpectStringToken("attachment <parent bone>", boneName);
    params.boneName = boneName.view();

    // position
    qc.ExpectFloatToken("attachment <X position>", params.position.x);
    qc.ExpectFloatToken("attachment <Y position>", params.position.y);
    qc.ExpectFloatToken("attachment <Z position>", params.position.z);

    // the options act in token order (each orientation replaces the one before it)
    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "absolute")) {
            params.locals.push_back({.kind = AttachmentLocal::Kind::Absolute});
        }
        else if (vstd::iequals(qc.token, "rigid")) {
            params.rigid = true;
        }
        else if (vstd::iequals(qc.token, "world_align")) {
            params.worldAlign = true;
        }
        else if (vstd::iequals(qc.token, "rotate")) {
            AttachmentLocal local{.kind = AttachmentLocal::Kind::Angles};
            for (int i = 0; i < 3; ++i) {
                if (!qc.TokenAvailable())
                    break;

                qc.ExpectFloatToken(local.angles[i]);
            }
            params.locals.push_back(local);
        }
        else if (vstd::iequals(qc.token, "x_and_z_axes")) {
            AttachmentLocal local{.kind = AttachmentLocal::Kind::XZAxes};
            local.xAxis = Vector(0, 0, 0);
            local.zAxis = Vector(0, 0, 0);
            for (int i = 0; i < 3; ++i) {
                if (!qc.TokenAvailable())
                    break;

                qc.ExpectFloatToken(local.xAxis[i]);
            }
            for (int i = 0; i < 3; ++i) {
                if (!qc.TokenAvailable())
                    break;

                qc.ExpectFloatToken(local.zAxis[i]);
            }
            params.locals.push_back(local);
        }
        else {
            qc.Error("unknown attachment ({}) option: ", name);
        }
    }

    ApplyAttachment(ctx, params, redefine);
}

void Cmd_RedefineAttachment(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // find a pre-existing attachment of the given name and re-populate its values

    if (ctx.options.createMakefile)
        return;

    // name
    qc.ExpectToken();

    qc.UnGetToken();

    for (int n = 0; n < ctx.bones.attachments.size(); n++) {
        if (vstd::iequals(qc.token, ctx.bones.attachments[n].name)) {
            Msg("Found pre-existing attachment matching name: {}\n", qc.token.c_str());
            MdlPrint("Found pre-existing attachment matching name: {}\n", qc.token);
            Internal_Cmd_Attachment(ctx, qc, static_cast<size_t>(n));
            return;
        }
    }

    qc.Error("Can't redefine attachment \"{}\" because it wasn't found.\n", qc.token);
}

void Cmd_Attachment(StudioMdlState &ctx, ScriptTokenizer &qc) {
    Internal_Cmd_Attachment(ctx, qc, std::nullopt);
}

void Cmd_Renamebone(StudioMdlState &ctx, ScriptTokenizer &qc) {
    s_renamebone_t &rename = ctx.bones.renamedbone.emplace_back();
    // from
    qc.ExpectStringToken(rename.from);

    // to
    qc.ExpectStringToken(rename.to);
}

void Cmd_StripBonePrefix(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (!ctx.bones.szStripBonePrefix.full()) {
        qc.ExpectToken();

        // make sure it's not a duplicate
        for (int k = 0; k < ctx.bones.szStripBonePrefix.size(); k++) {
            if (ctx.bones.szStripBonePrefix[k] == (const char*) qc.token) {
                MdlWarning("Ignoring duplicate $bonestripprefix for token {}\n", qc.token);
                return;
            }
        }

        ctx.bones.szStripBonePrefix.emplace_back(qc.token);
    }
    else {
        qc.Error("Too many bone strip prefixes!\n");
    }
}

void Cmd_RenameBoneSubstr(StudioMdlState &ctx, ScriptTokenizer &qc) {
    s_renamebone_t &rename = ctx.bones.szRenameBoneSubstr.emplace_back();
    // from
    qc.ExpectStringToken(rename.from);

    // to
    qc.ExpectStringToken(rename.to);
}

// Returns the 1-based id of the named transition node, adding it if needed
int LookupXNode(StudioMdlState &ctx, const char* name) {
    for (size_t i = 0; i < ctx.anim.xnodenames.size(); i++) {
        if (vstd::iequals(name, ctx.anim.xnodenames[i])) {
            return static_cast<int>(i + 1);
        }
    }
    if (ctx.anim.xnodenames.full()) {
        MdlError("Too many transition nodes, max {}\n", (int) ctx.anim.xnodenames.capacity());
    }
    ctx.anim.xnodenames.emplace_back(name);
    return static_cast<int>(ctx.anim.xnodenames.size());
}

void Cmd_Skiptransition(StudioMdlState &ctx, ScriptTokenizer &qc) {
    std::vector<std::string> nodes;

    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        nodes.emplace_back(qc.token.view());
    }

    ApplySkipTransition(ctx, nodes);
}


//-----------------------------------------------------------------------------
//
// The following code is all related to LODs
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Checks to see if the model source was already loaded
//-----------------------------------------------------------------------------
s_source_t* FindCachedSource(StudioMdlState &ctx, const char* name, const char* xext) {
    // source names are stored with the platform's slashes (LoadSource); QC tokens use either
    std::string relative(name);
    vstd::paths::fix_slashes(relative);
    const std::string base = ctx.paths.cddir.back() + relative + ".";
    const auto find = [&ctx, &base](const std::string_view ext) -> s_source_t* {
        ctx.input.szFilename = base;
        ctx.input.szFilename += ext;
        for (int i = 0; i < ctx.anim.sources.size(); i++) {
            if (vstd::iequals(ctx.input.szFilename, ctx.anim.sources[i]->filename))
                return ctx.anim.sources[i];
        }
        return nullptr;
    };

    // we know what extension is necessary. . look for it.
    if (xext[0]) {
        return find(xext);
    }

    // we don't know what extension to use, so look for all of 'em (not .vta).
    for (const char* pExt: {"vrm", "dmx", "smd", "xml", "obj"}) {
        if (s_source_t* pSource = find(pExt)) {
            return pSource;
        }
    }
    return nullptr;
}


//-----------------------------------------------------------------------------
// Parse replacemodel command, causes an LOD to use a new model
//-----------------------------------------------------------------------------

static void Cmd_ReplaceModel(StudioMdlState &ctx, ScriptTokenizer &qc, LodScriptData_t &lodData) {
    // from
    qc.ExpectToken();

    // Strip off extensions for the source...
    char* pDot = strrchr(qc.token, '.');
    if (pDot) {
        *pDot = 0;
    }

    if (!FindCachedSource(ctx, qc.token, "")) {
        // must have prior knowledge of the from
        qc.Error("Unknown replace model '{}'\n", qc.token);
    }

    const std::string from = qc.token.str();

    // to
    qc.ExpectToken();
    const std::string to = qc.token.str();

    // check for "reverse"
    bool reverse = false;
    if (qc.TokenAvailable() && qc.GetToken(false)) {
        if (vstd::iequals("reverse", qc.token)) {
            reverse = true;
        }
        else {
            qc.Error("\"{}\" unexpected\n", qc.token);
        }
    }

    ApplyLodReplaceModel(ctx, lodData, from, to, [&] { return Load_Source(ctx, to, "smd", reverse, false); });
}

//-----------------------------------------------------------------------------
// Parse removemodel command, causes an LOD to stop using a model
//-----------------------------------------------------------------------------

static void Cmd_RemoveModel(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    // from
    qc.ExpectToken();

    // Strip off extensions...
    char* pDot = strrchr(qc.token, '.');
    if (pDot)
        *pDot = 0;

    ApplyLodRemoveModel(lodData, qc.token.view());
}

//-----------------------------------------------------------------------------
// Parse replacebone command, causes a part of an LOD model to use a different bone
//-----------------------------------------------------------------------------

static void Cmd_ReplaceBone(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    // from
    qc.ExpectToken();
    const std::string from = qc.token.str();

    // to
    qc.ExpectToken();
    ApplyLodReplaceBone(lodData, from, qc.token.view());
}

//-----------------------------------------------------------------------------
// Parse bonetreecollapse command, causes the entire subtree to use the same bone as the node
//-----------------------------------------------------------------------------

static void Cmd_BoneTreeCollapse(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    // from
    qc.ExpectToken();
    ApplyLodBoneTreeCollapse(lodData, qc.token.view());
}

//-----------------------------------------------------------------------------
// Parse replacematerial command, causes a material to be used in place of another
//-----------------------------------------------------------------------------

static void Cmd_ReplaceMaterial(StudioMdlState &ctx, ScriptTokenizer &qc, LodScriptData_t &lodData) {
    // from
    qc.ExpectToken();
    const std::string from = qc.token.str();

    // to
    qc.ExpectToken();
    ApplyLodReplaceMaterial(ctx, lodData, from, qc.token.view());
}

//-----------------------------------------------------------------------------
// Parse removemesh command, causes a mesh to not be used anymore
//-----------------------------------------------------------------------------

static void Cmd_RemoveMesh(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    // from
    qc.ExpectToken();
    ApplyLodRemoveMesh(lodData, qc.token.view());
}

void Cmd_LOD(StudioMdlState &ctx, ScriptTokenizer &qc, const char* cmdname) {
    bool isShadowCall = (!stricmp(cmdname, "$shadowlod")) ? true : false;

    LodScriptData_t *lodPtr = nullptr;
    switch (ApplyLodBegin(ctx, isShadowCall, lodPtr)) {
        case LodBeginResult::Ok:
            break;
        case LodBeginResult::ShadowLodNotLast:
            qc.Error("Model can only have one $shadowlod and it must be the last lod in the " SRC_FILE_EXT " ({}) : {}\n",
                     ctx.input.iLinecount, ctx.input.szLine);
            break;
        case LodBeginResult::TooManyLods:
            qc.Error("Too many LODs (MAX_NUM_LODS=={})\n", (int) MAX_NUM_LODS);
            break;
    }
    LodScriptData_t &newLOD = *lodPtr;

    if (isShadowCall) {
        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            MdlWarning("({}) : {}:  Ignoring switch value on {} command line\n", cmdname, ctx.input.iLinecount,
                       ctx.input.szLine);
        }
    }
    else {
        if (qc.TokenAvailable()) {
            qc.ExpectFloatToken(newLOD.switchValue);
            if (newLOD.switchValue < 0.0f) {
                qc.Error("Negative switch value reserved for $shadowlod ({}) : {}\n", ctx.input.iLinecount,
                         ctx.input.szLine);
            }
        }
        else {
            qc.Error("expected {} switch value", cmdname);
        }
    }

    if (!qc.GetToken(true) || !vstd::iequals("{", qc.token)) {
        qc.Error("{}: expected '{{', got \"{}\"", cmdname, qc.token);
    }

    while (1) {
        if (!qc.GetToken(true))
            qc.Error("{}: missing '}}' at end of file", cmdname);
        if (vstd::iequals("replacemodel", qc.token)) {
            Cmd_ReplaceModel(ctx, qc, newLOD);
        }
        else if (vstd::iequals("removemodel", qc.token)) {
            Cmd_RemoveModel(qc, newLOD);
        }
        else if (vstd::iequals("replacebone", qc.token)) {
            Cmd_ReplaceBone(qc, newLOD);
        }
        else if (vstd::iequals("bonetreecollapse", qc.token)) {
            Cmd_BoneTreeCollapse(qc, newLOD);
        }
        else if (vstd::iequals("replacematerial", qc.token)) {
            Cmd_ReplaceMaterial(ctx, qc, newLOD);
        }
        else if (vstd::iequals("removemesh", qc.token)) {
            Cmd_RemoveMesh(qc, newLOD);
        }
        else if (vstd::iequals("nofacial", qc.token)) {
            ApplyLodFacialAnimation(newLOD, false);
        }
        else if (vstd::iequals("facial", qc.token)) {
            if (isShadowCall) {
                // facial animation has no reasonable purpose on a shadow lod
                qc.Error("Facial animation is not allowed for $shadowlod\n");
            }

            ApplyLodFacialAnimation(newLOD, true);
        }
        else if (vstd::iequals("use_shadowlod_materials", qc.token)) {
            if (isShadowCall) {
                ApplyLodUseShadowLodMaterials(ctx);
            }
        }
        else if (vstd::iequals("}", qc.token)) {
            break;
        }
        else {
            qc.Error("{}: unknown option \"{}\"", cmdname, qc.token);
        }
    }

    ApplyLodEnd(ctx, isShadowCall);
}

void Cmd_ShadowLOD(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // Act like it's a regular lod entry (marks the .mdl as having a shadow lod when it is done)
    Cmd_LOD(ctx, qc, "$shadowlod");
}


//-----------------------------------------------------------------------------
// A couple commands related to translucency sorting
//-----------------------------------------------------------------------------
void Cmd_Opaque(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyOpaque(ctx);
}

void Cmd_TranslucentTwoPass(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyMostlyOpaque(ctx);
}

//-----------------------------------------------------------------------------
// Indicates the model be rendered with ambient boost heuristic (first used on Alyx in Episode 1)
//-----------------------------------------------------------------------------
void Cmd_AmbientBoost(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyAmbientBoost(ctx);
}

//-----------------------------------------------------------------------------
// Indicates the model contains a quad-only Catmull-Clark subd mesh
//-----------------------------------------------------------------------------
void Cmd_SubdivisionSurface(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplySubdivisionSurface(ctx);
}


//-----------------------------------------------------------------------------
// Indicates the model should not cast shadows (useful for first-person models as used in L4D)
//-----------------------------------------------------------------------------
void Cmd_DoNotCastShadows(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyDoNotCastShadows(ctx);
}

//-----------------------------------------------------------------------------
// Indicates the model should cast texture-based shadows in vrad (NOTE: only applicable to prop_static)
//-----------------------------------------------------------------------------
void Cmd_CastTextureShadows(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyCastTextureShadows(ctx);
}


//-----------------------------------------------------------------------------
// Indicates the model should not fade out even if the level or fallback settings say to
//-----------------------------------------------------------------------------
void Cmd_NoForcedFade(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyNoForcedFade(ctx);
}


//-----------------------------------------------------------------------------
// Indicates the model should not use the bone origin when calculating bboxes, sequence boxes, etc.
//-----------------------------------------------------------------------------
void Cmd_SkipBoneInBBox(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplySkipBoneInBBox(ctx);
}


//-----------------------------------------------------------------------------
// Indicates the model will lengthen the viseme check to always include two phonemes
//-----------------------------------------------------------------------------
void Cmd_ForcePhonemeCrossfade(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyForcePhonemeCrossfade(ctx);
}

//-----------------------------------------------------------------------------
// Indicates the model should keep pre-defined bone lengths regardless of animation changes
//-----------------------------------------------------------------------------
void Cmd_LockBoneLengths(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyLockBoneLengths(ctx);
}

//-----------------------------------------------------------------------------
// Indicates the model should replace pre-defined bone bind poses
//-----------------------------------------------------------------------------
void Cmd_UnlockDefineBones(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyUnlockDefineBones(ctx);
}

//-----------------------------------------------------------------------------
// Mark this model as obsolete so that it'll show the obsolete material in game.
//-----------------------------------------------------------------------------
void Cmd_Obsolete(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyObsolete(ctx);
}

//-----------------------------------------------------------------------------
// The bones should be moved so that they center themselves on the verts they own.
//-----------------------------------------------------------------------------
void Cmd_CenterBonesOnVerts(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyCenterBonesOnVerts(ctx);
}

//-----------------------------------------------------------------------------
// How far back should simple motion extract pull back from the last frame
//-----------------------------------------------------------------------------
void Cmd_MotionExtractionRollBack(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyMotionRollback(ctx, qc.ExpectFloatToken<float>());
}

//-----------------------------------------------------------------------------
// rules for breaking up long animations into multiple sub anims
//-----------------------------------------------------------------------------
void Cmd_SectionFrames(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    const int frames = static_cast<int>(qc.TokenFloat());
    const int minFrameLimit = qc.ExpectIntToken<int>();
    ApplySectionFrames(ctx, frames, minFrameLimit);
}


//-----------------------------------------------------------------------------
// world space clamping boundaries for animations
//-----------------------------------------------------------------------------
void Cmd_ClampWorldspace(StudioMdlState &ctx, ScriptTokenizer &qc) {
    Vector mins, maxs;
    qc.ExpectFloatToken(mins[0]);

    qc.ExpectFloatToken(mins[1]);

    qc.ExpectFloatToken(mins[2]);

    qc.ExpectFloatToken(maxs[0]);

    qc.ExpectFloatToken(maxs[1]);

    qc.ExpectFloatToken(maxs[2]);

    ApplyClampWorldspace(ctx, mins, maxs);
}

//-----------------------------------------------------------------------------
// Purpose: force a specific parent child relationship
//-----------------------------------------------------------------------------

void Cmd_ForcedHierarchy(StudioMdlState &ctx, ScriptTokenizer &qc) {
    s_forcedhierarchy_t &hierarchy = ctx.bones.forcedhierarchy.emplace_back();
    // child name
    qc.ExpectStringToken(hierarchy.childname);

    // parent name
    qc.ExpectStringToken(hierarchy.parentname);
}


//-----------------------------------------------------------------------------
// Purpose: insert a virtual bone between a child and parent (currently unsupported)
//-----------------------------------------------------------------------------

void Cmd_InsertHierarchy(StudioMdlState &ctx, ScriptTokenizer &qc) {
    s_forcedhierarchy_t &hierarchy = ctx.bones.forcedhierarchy.emplace_back();
    // child name
    qc.ExpectStringToken(hierarchy.childname);

    // subparent name
    qc.ExpectStringToken(hierarchy.subparentname);

    // parent name
    qc.ExpectStringToken(hierarchy.parentname);
}


//-----------------------------------------------------------------------------
// Purpose: rotate a specific bone
//-----------------------------------------------------------------------------

void Cmd_ForceRealign(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // bone name
    StudioName bone;
    qc.ExpectStringToken(bone);

    // skip
    qc.ExpectToken();

    RadianEuler rot;

    // X axis
    qc.ExpectToken();
    rot.x = DEG2RAD(qc.TokenFloat());

    // Y axis
    qc.ExpectToken();
    rot.y = DEG2RAD(qc.TokenFloat());

    // Z axis
    qc.ExpectToken();
    rot.z = DEG2RAD(qc.TokenFloat());

    ApplyForceRealign(ctx, bone.view(), rot);
}


//-----------------------------------------------------------------------------
// Purpose: specify a bone to allow > 180 but < 360 rotation (forces a calculated "mid point" to rotation)
//-----------------------------------------------------------------------------

void Cmd_LimitRotation(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // bone name
    StudioName bone;
    qc.ExpectStringToken(bone);

    std::vector<std::string> sequences;
    while (qc.TokenAvailable()) {
        // sequence name
        qc.ExpectToken();
        sequences.emplace_back(qc.token.view());
    }

    ApplyLimitRotation(ctx, bone.view(), sequences);
}

//-----------------------------------------------------------------------------
// Purpose: artist controlled sanity check for expected state of the model.
// The idea is to allow artists to anticipate and prevent content errors by adding 'qc asserts'
// into commonly iterated models. This could be anything from bones that are expected (or not)
// to polycounts, material references, etc.- It's just an "Assert" for content.
//-----------------------------------------------------------------------------
void Cmd_QCAssert(StudioMdlState &ctx, ScriptTokenizer &qc) {
    //get the assert type
    qc.ExpectToken();

    //Msg( "Validating QC Assert '{}'\n", token.c_str() );

    //start building assert description line
    char szAssertLine[1024] = "QC Assert: ";
    strcat(szAssertLine, qc.token);

    bool bQueryValue = false;

    if (vstd::iequals(qc.token, "boneexists")) {
        // bone name
        qc.ExpectToken();
        char szBoneName[MAXSTUDIONAME];
        strcpyn(szBoneName, qc.token);

        strcat(szAssertLine, " ");
        strcat(szAssertLine, szBoneName);

        // src name
        qc.ExpectToken();
        s_source_t* pSrc = Load_Source(ctx, qc.token.view(), "");

        strcat(szAssertLine, " ");
        strcat(szAssertLine, qc.token);
        for (int n = 0; n < pSrc->numbones; n++) {
            if (vstd::iequals(szBoneName, pSrc->localBone[n].name)) {
                bQueryValue = true;
                break;
            }
        }
    }
    else if (vstd::iequals(qc.token, "importboneexists")) {
        // bone name
        qc.ExpectToken();
        char szBoneName[MAXSTUDIONAME];
        strcpyn(szBoneName, qc.token);

        strcat(szAssertLine, " ");
        strcat(szAssertLine, szBoneName);

        for (int n = 0; n < std::ssize(ctx.bones.importbone); n++) {
            if (vstd::iequals(szBoneName, ctx.bones.importbone[n].name)) {
                bQueryValue = true;
                break;
            }
        }
    }

    // add more possible qc asserts here...


    // is the assert value positive or negative
    qc.ExpectToken();
    strcat(szAssertLine, " ");
    strcat(szAssertLine, qc.token);

    bool bAssertValue = vstd::iequals(qc.token, "true");

    // print the result
    strcat(szAssertLine, " RESULT: ");
    if (bQueryValue != bAssertValue) {
        strcat(szAssertLine, "[Fail]\n");

        //// show helpful message, if one exists
        //if ( TokenAvailable() )
        //{
        //	GetToken (false);
        //	strcat( szAssertLine, token );
        //}

        qc.Error("{}", szAssertLine);
    }
    else {
        strcat(szAssertLine, "[Success]\n");
    }
    MdlPrint("{}", szAssertLine);
}

//-----------------------------------------------------------------------------
// Purpose: specify bones to store, even if nothing references them
//-----------------------------------------------------------------------------

void Cmd_DefineBone(StudioMdlState &ctx, ScriptTokenizer &qc) {
    StudioName name, parent;
    // bone name
    qc.ExpectStringToken(name);

    // parent name
    qc.ExpectStringToken(parent);

    DefineBoneParams params;
    params.name = name.view();
    params.parent = parent.view();

    qc.ExpectToken();
    if (!V_strcmp(qc.token, "unlocked")) {
        params.unlocked = true;
    }
    else if (!V_strcmp(qc.token, "locked")) {
        params.unlocked = false;
    }
    else {
        qc.UnGetToken();
    }

    Vector pos;
    QAngle angles;

    // default pos
    qc.ExpectFloatToken(pos.x);
    qc.ExpectFloatToken(pos.y);
    qc.ExpectFloatToken(pos.z);
    qc.ExpectFloatToken(angles.x);
    qc.ExpectFloatToken(angles.y);
    qc.ExpectFloatToken(angles.z);
    AngleMatrix(angles, pos, params.rawLocal);

    if (qc.TokenAvailable()) {
        // realign pos
        qc.ExpectFloatToken(pos.x);
        qc.ExpectFloatToken(pos.y);
        qc.ExpectFloatToken(pos.z);
        qc.ExpectFloatToken(angles.x);
        qc.ExpectFloatToken(angles.y);
        qc.ExpectFloatToken(angles.z);

        AngleMatrix(angles, pos, params.realign.emplace());
    }

    ApplyDefineBone(ctx, params);
}

bool ParseJiggleAngleConstraint(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    qc.ExpectToken("angle value");

    ApplyJiggleAngleConstraint(*jiggleInfo, qc.TokenFloat());

    return true;
}

bool ParseJiggleYawConstraint(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    qc.ExpectToken("minimum yaw value");

    const float minYaw = qc.TokenFloat();

    qc.ExpectToken("maximum yaw value");

    ApplyJiggleYawConstraint(*jiggleInfo, minYaw, qc.TokenFloat());

    return true;
}

bool ParseJigglePitchConstraint(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    qc.ExpectToken("minimum pitch value");

    const float minPitch = qc.TokenFloat();

    qc.ExpectToken("maximum pitch value");

    ApplyJigglePitchConstraint(*jiggleInfo, minPitch, qc.TokenFloat());

    return true;
}

void Grab_AxisInterpBones(StudioMdlState &ctx) {
    Vector basepos(0, 0, 0);
    s_axisinterpbone_t* axisBone = nullptr;

    while (GetLineInput(ctx)) {
        if (IsEnd(ctx.input.szLine)) {
            return;
        }
        LineParser line(ctx.input.szLine.view());
        const auto command = line.Word();
        if (vstd::iequals(command, "bone")) {
            const auto bone = line.Quoted();
            const auto boneDisplay = line.Quoted();
            const auto control = line.Quoted();
            const auto controlDisplay = line.Quoted();
            int axis;
            if (bone.empty() || boneDisplay.empty() || control.empty() || controlDisplay.empty() ||
                bone.size() > StudioName::capacity() || control.size() > StudioName::capacity() || !line.Read(axis))
                MdlError("Line {}: malformed procedural bone: {}", ctx.input.iLinecount, ctx.input.szLine);
            axisBone = &ApplyAxisInterpBegin(ctx, bone, control, axis);
        } else if (vstd::iequals(command, "basepos")) {
            if (!line.Read(basepos.x, basepos.y, basepos.z))
                MdlError("Line {}: malformed procedural basepos: {}", ctx.input.iLinecount, ctx.input.szLine);
        } else if (vstd::iequals(command, "axis")) {
            Vector position;
            QAngle rotation;
            int index;
            if (!axisBone || !line.Read(index, position.x, position.y, position.z, rotation.z, rotation.x, rotation.y))
                MdlError("Line {}: malformed procedural axis: {}", ctx.input.iLinecount, ctx.input.szLine);
            if (!ApplyAxisInterpAxis(*axisBone, index, basepos, position, rotation))
                MdlError("Line {}: procedural bone axis index {} out of range (0..5)", ctx.input.iLinecount, index);
        }
    }
}

//
// Parse common parameters.
// This assumes a token has already been read, and returns true if
// the token is recognized and parsed.
//
bool ParseCommonJiggle(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    if (vstd::iequals(qc.token, "tip_mass")) {
        qc.ExpectToken();

        ApplyJiggleTipMass(*jiggleInfo, qc.TokenFloat());
    }
    else if (vstd::iequals(qc.token, "length")) {
        qc.ExpectToken();

        ApplyJiggleLength(*jiggleInfo, qc.TokenFloat());
    }
    else if (vstd::iequals(qc.token, "angle_constraint")) {
        if (ParseJiggleAngleConstraint(qc, jiggleInfo) == false) {
            return false;
        }
    }
    else if (vstd::iequals(qc.token, "yaw_constraint")) {
        if (ParseJiggleYawConstraint(qc, jiggleInfo) == false) {
            return false;
        }
    }
    else if (vstd::iequals(qc.token, "yaw_friction")) {
        qc.ExpectToken();

        ApplyJiggleYawFriction(*jiggleInfo, qc.TokenFloat());
    }
    else if (vstd::iequals(qc.token, "yaw_bounce")) {
        qc.ExpectToken();

        ApplyJiggleYawBounce(*jiggleInfo, qc.TokenFloat());
    }
    else if (vstd::iequals(qc.token, "pitch_constraint")) {
        if (ParseJigglePitchConstraint(qc, jiggleInfo) == false) {
            return false;
        }
    }
    else if (vstd::iequals(qc.token, "pitch_friction")) {
        qc.ExpectToken();

        ApplyJigglePitchFriction(*jiggleInfo, qc.TokenFloat());
    }
    else if (vstd::iequals(qc.token, "pitch_bounce")) {
        qc.ExpectToken();

        ApplyJigglePitchBounce(*jiggleInfo, qc.TokenFloat());
    }
    else {
        // unknown token
        qc.Error("$jigglebone: invalid syntax '{}'\n", qc.token);
        return false;
    }

    return true;
}

//
// Parse parameters for is_rigid subsection
//
bool ParseRigidJiggle(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    ApplyJiggleRigid(*jiggleInfo);

    bool gotOpenBracket = false;
    while (true) {
        if (qc.GetToken(true) == false) {
            qc.Error("$jigglebone:is_rigid: parse error\n");
            return false;
        }

        if (vstd::iequals(qc.token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            qc.Error("$jigglebone:is_rigid: missing '{{'\n");
            return false;
        }
        else if (vstd::iequals(qc.token, "}")) {
            // definition complete
            break;
        }
        else if (ParseCommonJiggle(qc, jiggleInfo) == false) {
            qc.Error("$jigglebone:is_rigid: invalid syntax '{}'\n", qc.token);
            return false;
        }
    }

    return true;
}


float ParseJiggleStiffness(ScriptTokenizer &qc) {
    qc.ExpectToken("stiffness value");

    float stiffness = qc.TokenFloat();

    return stiffness; // clamped to 0..1000 by the apply functions
}

//
// Parse parameters for has_base_spring subsection
//
bool ParseBaseSpringJiggle(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    ApplyJiggleBaseSpring(*jiggleInfo);

    bool gotOpenBracket = false;
    while (true) {
        if (qc.GetToken(true) == false) {
            qc.Error("$jigglebone:is_rigid: parse error\n");
            return false;
        }

        if (vstd::iequals(qc.token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            qc.Error("$jigglebone:is_rigid: missing '{{'\n");
            return false;
        }
        else if (vstd::iequals(qc.token, "}")) {
            // definition complete
            break;
        }
        else if (vstd::iequals(qc.token, "stiffness")) {
            ApplyJiggleBaseStiffness(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "damping")) {
            ApplyJiggleBaseDamping(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "left_constraint")) {
            qc.ExpectToken();

            const float minLeft = qc.TokenFloat();

            qc.ExpectToken();

            ApplyJiggleBaseLeftConstraint(*jiggleInfo, minLeft, qc.TokenFloat());
        }
        else if (vstd::iequals(qc.token, "left_friction")) {
            qc.ExpectToken();

            ApplyJiggleBaseLeftFriction(*jiggleInfo, qc.TokenFloat());
        }
        else if (vstd::iequals(qc.token, "up_constraint")) {
            qc.ExpectToken();

            const float minUp = qc.TokenFloat();

            qc.ExpectToken();

            ApplyJiggleBaseUpConstraint(*jiggleInfo, minUp, qc.TokenFloat());
        }
        else if (vstd::iequals(qc.token, "up_friction")) {
            qc.ExpectToken();

            ApplyJiggleBaseUpFriction(*jiggleInfo, qc.TokenFloat());
        }
        else if (vstd::iequals(qc.token, "forward_constraint")) {
            qc.ExpectToken();

            const float minForward = qc.TokenFloat();

            qc.ExpectToken();

            ApplyJiggleBaseForwardConstraint(*jiggleInfo, minForward, qc.TokenFloat());
        }
        else if (vstd::iequals(qc.token, "forward_friction")) {
            qc.ExpectToken();

            ApplyJiggleBaseForwardFriction(*jiggleInfo, qc.TokenFloat());
        }
        else if (vstd::iequals(qc.token, "base_mass")) {
            qc.ExpectToken();

            ApplyJiggleBaseMass(*jiggleInfo, qc.TokenFloat());
        }
        else if (ParseCommonJiggle(qc, jiggleInfo) == false) {
            qc.Error("$jigglebone:has_base_spring: invalid syntax '{}'\n", qc.token);
            return false;
        }
    }

    return true;
}


//
// Parse parameters for is_flexible subsection
//
bool ParseFlexibleJiggle(ScriptTokenizer &qc, s_jigglebone_t* jiggleInfo) {
    ApplyJiggleFlexible(*jiggleInfo);

    bool gotOpenBracket = false;
    while (true) {
        if (qc.GetToken(true) == false) {
            qc.Error("$jigglebone:is_flexible: parse error\n");
            return false;
        }

        if (vstd::iequals(qc.token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            qc.Error("$jigglebone:is_flexible: missing '{{'\n");
            return false;
        }
        else if (vstd::iequals(qc.token, "}")) {
            // definition complete
            break;
        }
        else if (vstd::iequals(qc.token, "yaw_stiffness")) {
            ApplyJiggleYawStiffness(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "yaw_damping")) {
            ApplyJiggleYawDamping(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "pitch_stiffness")) {
            ApplyJigglePitchStiffness(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "pitch_damping")) {
            ApplyJigglePitchDamping(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "along_stiffness")) {
            ApplyJiggleAlongStiffness(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "along_damping")) {
            ApplyJiggleAlongDamping(*jiggleInfo, ParseJiggleStiffness(qc));
        }
        else if (vstd::iequals(qc.token, "allow_length_flex")) {
            ApplyJiggleAllowLengthFlex(*jiggleInfo);
        }
        else if (ParseCommonJiggle(qc, jiggleInfo) == false) {
            qc.Error("$jigglebone:is_flexible: invalid syntax '{}'\n", qc.token);
            return false;
        }
    }

    return true;
}

//
// Parse $jigglebone parameters
//
void Cmd_JiggleBone(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // bone name
    StudioName bone;
    qc.ExpectStringToken(bone);
    s_jigglebone_t* jiggleInfo = &ApplyJiggleBegin(ctx, bone.view());

    bool gotOpenBracket = false;
    while (true) {
        if (qc.GetToken(true) == false) {
            qc.Error("$jigglebone: parse error\n");
            return;
        }

        if (vstd::iequals(qc.token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            qc.Error("$jigglebone: missing '{{'\n");
            return;
        }
        else if (vstd::iequals(qc.token, "}")) {
            // definition complete
            break;
        }
        else if (vstd::iequals(qc.token, "is_flexible")) {
            if (ParseFlexibleJiggle(qc, jiggleInfo) == false) {
                return;
            }
        }
        else if (vstd::iequals(qc.token, "is_rigid")) {
            if (ParseRigidJiggle(qc, jiggleInfo) == false) {
                return;
            }
        }
        else if (vstd::iequals(qc.token, "has_base_spring")) {
            if (ParseBaseSpringJiggle(qc, jiggleInfo) == false) {
                return;
            }
        }
        else {
            qc.Error("$jigglebone: invalid syntax '{}'\n", qc.token);
            return;
        }
    }

    ApplyJiggleEnd(ctx, *jiggleInfo);
}


//-----------------------------------------------------------------------------
// Purpose: specify bones to store, even if nothing references them
//-----------------------------------------------------------------------------

void Cmd_IncludeModel(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    ApplyIncludeModel(ctx, qc.token.view());
}

void Cmd_CD(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (ctx.paths.cdset)
        qc.Error("Two $cd in one model");
    ctx.paths.cdset = true;
    qc.ExpectToken("<path>");
    ctx.paths.cddir.clear();
    ctx.paths.cddir.push_back(std::string(qc.token) + "/");
}

void Cmd_ContentRootRelative(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ctx.paths.contentRootRelative = true;
}

void Cmd_CDMaterials(StudioMdlState &ctx, ScriptTokenizer &qc) {
    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        ApplyCdMaterial(ctx, qc.token.view());
    }
}

void Cmd_Pushd(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    std::string newDir = ctx.paths.cddir.back() + qc.token.str() + "/";
    ctx.paths.cddir.push_back(std::move(newDir));
}

void Cmd_Popd(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (ctx.paths.cddir.size() > 1)
        ctx.paths.cddir.pop_back();
}


void Cmd_ExternalTextures(StudioMdlState &ctx, ScriptTokenizer &qc) {
    MdlWarning("ignoring $externaltextures, obsolete...");
}

void Cmd_ClipToTextures(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyClipToTextures(ctx);
}

void Cmd_CollapseBones(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyCollapseBones(ctx);
}

void Cmd_SkinnedLODs(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ctx.options.bSkinnedLODs = true;
}

void Cmd_CollapseBonesAggressive(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyCollapseBonesAggressive(ctx);
}

void Cmd_AlwaysCollapse(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    ApplyAlwaysCollapse(ctx, qc.token.view());
}

void Cmd_CalcTransitions(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyCalcTransitions(ctx);
}

void ProcessStaticProp(StudioMdlState &ctx) {
    ctx.header.staticprop = true;
    ctx.header.gflags |= STUDIOHDR_FLAGS_STATIC_PROP;
}

void Cmd_StaticProp(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ProcessStaticProp(ctx);
}

void Cmd_ZBrush(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyZBrush(ctx);
}

void Cmd_RealignBones(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyRealignBones(ctx);
}

void Cmd_BaseLOD(StudioMdlState &ctx, ScriptTokenizer &qc) {
    Cmd_LOD(ctx, qc, "$lod");
}

//-----------------------------------------------------------------------------
// Key value block
//-----------------------------------------------------------------------------
static void AppendKeyValueText(std::string &pKeyValue, const char* pString) {
    int nLen = strlen(pString);
    pKeyValue.append(pString, nLen);
}

int KeyValueTextSize(const std::string &pKeyValue) {
    return pKeyValue.size();
}

const char* KeyValueText(const std::string &pKeyValue) {
    return pKeyValue.data();
}

void Option_KeyValues(ScriptTokenizer &qc, std::string &pKeyValue);

//-----------------------------------------------------------------------------
// Key value block!
//-----------------------------------------------------------------------------
void Option_KeyValues(ScriptTokenizer &qc, std::string &pKeyValue) {
    // Simply read in the block between { }s as text
    // and plop it out unchanged into the .mdl file.
    // Make sure to respect the fact that we may have nested {}s
    int nLevel = 1;

    if (!qc.GetToken(true) || qc.token[0] != '{')
        qc.Error("keyvalues: expected '{{', got \"{}\"", qc.token);


    while (qc.GetToken(true)) {
        if (vstd::iequals(qc.token, "}")) {
            nLevel--;
            if (nLevel <= 0)
                break;
            AppendKeyValueText(pKeyValue, " }\n");
        }
        else if (vstd::iequals(qc.token, "{")) {
            AppendKeyValueText(pKeyValue, "{\n");
            nLevel++;
        }
        else {
            // tokens inside braces are quoted
            if (nLevel > 1) {
                AppendKeyValueText(pKeyValue, "\"");
                AppendKeyValueText(pKeyValue, qc.token);
                AppendKeyValueText(pKeyValue, "\" ");
            }
            else {
                AppendKeyValueText(pKeyValue, qc.token);
                AppendKeyValueText(pKeyValue, " ");
            }
        }
    }

    if (nLevel >= 1) {
        qc.Error("Keyvalue block missing matching braces.\n");
    }
}

void Cmd_KeyValues(StudioMdlState &ctx, ScriptTokenizer &qc) {
    std::string text;
    Option_KeyValues(qc, text);
    ApplyKeyValues(ctx, text);
}

void Cmd_ConstDirectionalLight(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    ApplyConstantDirectionalLight(ctx, qc.TokenFloat());
}

void Cmd_MinLOD(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyMinLod(ctx, qc.ExpectIntToken<int>());
}

void Cmd_AllowRootLODs(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyAllowRootLods(ctx, qc.ExpectIntToken<int>());
}

void Cmd_BoneSaveFrame(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // bone name
    vstd::FixedString<MAXSTUDIOHITBOXSETNAME> bone;
    qc.ExpectStringToken(bone);

    bool savePos = false;
    bool saveRot = false;
    bool saveRot64 = false;
    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (vstd::iequals("position", qc.token)) {
            savePos = true;
        }
        else if (vstd::iequals("rotation", qc.token)) {
            saveRot = true;
        }
        else if (vstd::iequals("rotation64", qc.token)) {
            saveRot64 = true;
        }
        else {
            qc.Error("unknown option \"{}\" on $bonesaveframe : {}\n", qc.token, bone.view());
        }
    }

    ApplyBoneSaveFrame(ctx, bone.view(), savePos, saveRot, saveRot64);
}


void Cmd_SetDefaultFadeInTime(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    ApplyDefaultFadeInTime(ctx, qc.TokenFloat());
}

void Cmd_SetDefaultFadeOutTime(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    ApplyDefaultFadeOutTime(ctx, qc.TokenFloat());
}

void Cmd_LCaseAllSequences(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyLCaseAllSequences(ctx);
}

void Cmd_AllowActivityName(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    ApplyAllowActivityName(ctx, qc.token.view());
}

void Cmd_CollisionPrecision(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    ApplyCollisionPrecision(ctx, qc.TokenFloat());
}

void Cmd_ErrorOnSeqRemapFail(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyErrorOnSeqRemapFail(ctx);
}


void Cmd_SetModelIntentionallyHasZeroSequences(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyModelIntentionallyHasZeroSequences(ctx);
}


void Cmd_BoneMerge(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (ctx.options.createMakefile)
        return;

    // bone name
    StudioName bone;
    qc.ExpectStringToken(bone);
    ApplyBoneMerge(ctx, bone.view());
}

//-----------------------------------------------------------------------------
// Purpose: create named list of boneweights
//-----------------------------------------------------------------------------
namespace {
    struct ParsedWeight {
        std::string bone;
        float weight = 0.0f;
        float positionWeight = 0.0f;
    };

    std::vector<ParsedWeight> Option_Weightlist(ScriptTokenizer &qc) {
        std::vector<ParsedWeight> weights;
        int depth = 0;

        while (1) {
            if (depth > 0) {
                if (!qc.GetToken(true))
                    qc.Error("missing '}}' at end of file");
            }
            else {
                if (!qc.TokenAvailable()) {
                    break;
                }
                else {
                    qc.ExpectToken();
                }
            }

            if (qc.EndOfScript()) {
                if (depth != 0) {
                    qc.Error("missing }}\n");
                }
                return weights;
            }
            if (vstd::iequals("{", qc.token)) {
                depth++;
            }
            else if (vstd::iequals("}", qc.token)) {
                depth--;
            }
            else if (vstd::iequals("posweight", qc.token)) {
                if (weights.empty()) {
                    qc.Error("Error with specifing bone Position weight\n");
                }
                ParsedWeight &last = weights.back();
                qc.ExpectFloatToken(last.positionWeight);
            }
            else {
                ParsedWeight &weight = weights.emplace_back();
                weight.bone = qc.token.view();
                qc.ExpectFloatToken(weight.weight);
                weight.positionWeight = weight.weight;
            }

            if (depth < 0) {
                qc.Error("missing {{\n");
            }
        };
        return weights;
    }

    std::vector<WeightListEntry> WeightListEntries(const std::vector<ParsedWeight> &weights) {
        std::vector<WeightListEntry> entries;
        entries.reserve(weights.size());
        for (const ParsedWeight &weight: weights) {
            entries.push_back({weight.bone, weight.weight, weight.positionWeight});
        }
        return entries;
    }
}

void Cmd_Weightlist(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    const std::string name(qc.token.view());
    const std::vector<ParsedWeight> weights = Option_Weightlist(qc);
    ApplyWeightList(ctx, name, WeightListEntries(weights));
}

void Cmd_DefaultWeightlist(StudioMdlState &ctx, ScriptTokenizer &qc) {
    const std::vector<ParsedWeight> weights = Option_Weightlist(qc);
    ApplyDefaultWeightList(ctx, WeightListEntries(weights));
}

/*
=================
Cmd_Origin
=================
*/
void Cmd_Origin(StudioMdlState &ctx, ScriptTokenizer &qc) {
    const float x = qc.ExpectFloatToken<float>();
    const float y = qc.ExpectFloatToken<float>();
    const float z = qc.ExpectFloatToken<float>();
    ApplyOrigin(ctx, x, y, z);

    if (qc.TokenAvailable()) {
        qc.ExpectToken();
        ApplyOriginAngle(ctx, qc.TokenFloat());
    }
}

//-----------------------------------------------------------------------------
// Purpose: Set the default root rotation so that the Y axis is up instead of the Z axis (for Maya)
//-----------------------------------------------------------------------------
void ProcessUpAxis(StudioMdlState &ctx, const RadianEuler &angles) {
    ctx.script.defaultrotation = angles;
}


//-----------------------------------------------------------------------------
// Purpose: Set the default root rotation so that the Y axis is up instead of the Z axis (for Maya)
//-----------------------------------------------------------------------------
void Cmd_UpAxis(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    UpAxis axis;
    if (!ParseUpAxis(qc.token.view(), axis)) {
        qc.Error("unknown $upaxis option: \"{}\"\n", qc.token);
        return;
    }

    ApplyUpAxis(ctx, axis);
}


void Cmd_ScaleUp(StudioMdlState &ctx, ScriptTokenizer &qc) {
    ApplyScale(ctx, qc.ExpectFloatToken<float>());
}

//-----------------------------------------------------------------------------
// Purpose: Sets how what size chunks to cut the animations into
//-----------------------------------------------------------------------------
void Cmd_AnimBlockSize(StudioMdlState &ctx, ScriptTokenizer &qc) {
    AnimBlockSettings settings;
    qc.ExpectIntToken(settings.size);
    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (vstd::iequals(qc.token, "nostall")) {
            settings.noStall = true;
        }
        else if (vstd::iequals(qc.token, "highres")) {
            settings.resolution = AnimBlockSettings::Resolution::High;
        }
        else if (vstd::iequals(qc.token, "lowres")) {
            settings.resolution = AnimBlockSettings::Resolution::Low;
        }
        else if (vstd::iequals(qc.token, "numframes")) {
            qc.ExpectToken();
            settings.numFrames = qc.TokenInt();
        }
        else if (vstd::iequals(qc.token, "cachehighres")) {
            settings.cacheHighRes = true;
        }
        else if (vstd::iequals(qc.token, "posdelta")) {
            settings.posDelta = qc.ExpectFloatToken<float>();
        }
        else {
            qc.Error("unknown option \"{}\" on $animblocksize command\n", qc.token);
        }
    }
    ApplyAnimBlockSize(ctx, settings);
}

bool ParseSourceOffset(std::string_view text, AppendSourceOffset &offset) {
    LineParser line(text);
    return line.Consume("offset") && line.Consume("pos[") &&
           line.Read(offset.position.x, offset.position.y) && line.Number(offset.position.z, ']') &&
           line.Consume("]") && line.Consume("angle[") &&
           line.Read(offset.angles.x, offset.angles.y) && line.Number(offset.angles.z, ']') &&
           line.Consume("]") && line.Consume("scale[") &&
           line.Number(offset.scale, ']') && line.Consume("]") && line.Empty();
}

void Cmd_AppendSource(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    s_source_t* pOrigSource = ctx.body.model[0]->source;
    s_source_t* pAppendSource = Load_Source(ctx, qc.token.view(), "", false, false,
                                            false
                                            /* don't use cached lookup, since this might be a dup of the starting src */);

    std::optional<AppendSourceOffset> offset;

    if (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (qc.token.view().starts_with("offset")) {
            AppendSourceOffset parsed;
            if (ParseSourceOffset(qc.token.view(), parsed)) {
                offset = parsed;
            }
            else {
                qc.Error("Malformed offset parameters to $appendsource.");
                return;
            }
        }
        else {
            qc.UnGetToken();
        }
    }

    ApplyAppendSource(ctx, pOrigSource, pAppendSource, offset);
}

void Cmd_FlexOptimize(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // $flexoptimize auto|on|off [min static verts]
    qc.ExpectToken();
    FlexOptimizeMode_t mode = ctx.options.flexOptimizeMode;
    if (!ParseFlexOptimizeMode(qc.token.view(), mode)) {
        qc.Error("$flexoptimize expects auto, on or off, got \"{}\"\n", qc.token);
    }
    ApplyFlexOptimizeMode(ctx, mode);
    if (qc.TokenAvailable()) {
        ApplyFlexOptimizeMinStatic(ctx, qc.ExpectIntToken<int>());
    }
}

void Cmd_maxVerts(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // first limit
    qc.ExpectToken();
    const int limit = qc.TokenInt();

    std::optional<int> clampTarget;
    if (qc.TokenAvailable()) {
        // actual target limit
        qc.ExpectToken();
        clampTarget = qc.TokenInt();
    }
    ApplyMaxVerts(ctx, limit, clampTarget);
}

s_sequence_t* LookupSequence(StudioMdlState &ctx, const char* name) {
    int i;
    for (i = 0; i < std::ssize(ctx.anim.sequence); ++i) {
        if (vstd::iequals(ctx.anim.sequence[i].name, name))
            return &ctx.anim.sequence[i];
    }
    return nullptr;
}


s_animation_t* LookupAnimation(StudioMdlState &ctx, const char* name, int nFallbackRecursionDepth) {
    int i;
    for (i = 0; i < ctx.anim.animations.size(); i++) {
        if (vstd::iequals(ctx.anim.animations[i]->name, name))
            return ctx.anim.animations[i];
    }

    s_sequence_t* pseq = LookupSequence(ctx, name);

    // pseq->panim grows on access, so check the sizes instead of reading panim[0][0]: this is only a lookup
    if (pseq && !pseq->panim.empty() && !pseq->panim.at(0).empty()) {
        return pseq->panim.at(0).at(0);
    }

    // check optional fallbacks or reserved name syntax

    if (nFallbackRecursionDepth == 0 && !V_strcmp(name, "this")) {
        return LookupAnimation(ctx, ctx.script.szInCurrentSeqName, 1);
    }

    return nullptr;
}

s_animation_t* LookupAnimation(StudioMdlState &ctx, const char* name) {
    return LookupAnimation(ctx, name, 0);
}


int Option_AnimTag(ScriptTokenizer &qc, s_sequence_t* psequence) {
    qc.ExpectToken("animtag name");
    const std::string name(qc.token.view());

    const float cycle = qc.ExpectFloatToken<float>("animtag cycle");
    ApplySequenceAnimTag(psequence, name, cycle);

    return 0;
}

int Option_Event(ScriptTokenizer &qc, s_sequence_t* psequence) {
    qc.ExpectToken("event name");
    const std::string name(qc.token.view());

    int frame = 0;
    qc.ExpectIntToken("event frame", frame);

    // option token
    std::string options;
    if (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (qc.token[0] == '}') { // opps, hit the end
            ApplySequenceEvent(psequence, name, frame, options);
            return 1;
        }
        // found an option
        options = qc.token.view();
    }

    ApplySequenceEvent(psequence, name, frame, options);
    return 0;
}


s_ikrule_t* Option_IKRule(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // chain
    qc.ExpectToken();

    IkRuleInit init;
    init.chain = FindIkChain(ctx, qc.token.view());
    if (init.chain < 0) {
        qc.Error("unknown chain \"{}\" in ikrule\n", qc.token);
    }

    // type
    qc.ExpectToken();

    StudioName bone;
    StudioName attachment;
    if (vstd::iequals(qc.token, "autosteps")) {
        init.kind = IkRuleKind::AutoSteps;
        qc.ExpectIntToken(init.autoStepsEnd);
        qc.ExpectStringToken(bone);
    }
    else if (vstd::iequals(qc.token, "touch")) {
        init.kind = IkRuleKind::Touch;

        // bone
        qc.ExpectStringToken(bone);
    }
    else if (vstd::iequals(qc.token, "footstep")) {
        init.kind = IkRuleKind::Footstep;
    }
    else if (vstd::iequals(qc.token, "attachment")) {
        init.kind = IkRuleKind::Attachment;

        // name of attachment
        qc.ExpectStringToken(attachment);
    }
    else if (vstd::iequals(qc.token, "release")) {
        init.kind = IkRuleKind::Release;
    }
    else if (vstd::iequals(qc.token, "unlatch")) {
        init.kind = IkRuleKind::Unlatch;
    }
    init.bone = bone.view();
    init.attachment = attachment.view();

    s_ikrule_t* pRule = InitIkRule(ctx, init);

    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (vstd::iequals(qc.token, "height")) {
            qc.ExpectFloatToken(pRule->height);
        }
        else if (vstd::iequals(qc.token, "target")) {
            // slot
            qc.ExpectIntToken(pRule->slot);
        }
        else if (vstd::iequals(qc.token, "range")) {
            // ramp
            qc.ExpectToken();
            if (qc.token[0] == '.')
                pRule->start = -1;
            else
                pRule->start = qc.TokenInt();

            qc.ExpectToken();
            if (qc.token[0] == '.')
                pRule->peak = -1;
            else
                pRule->peak = qc.TokenInt();

            qc.ExpectToken();
            if (qc.token[0] == '.')
                pRule->tail = -1;
            else
                pRule->tail = qc.TokenInt();

            qc.ExpectToken();
            if (qc.token[0] == '.')
                pRule->end = -1;
            else
                pRule->end = qc.TokenInt();
        }
        else if (vstd::iequals(qc.token, "floor")) {
            qc.ExpectFloatToken(pRule->floor);
        }
        else if (vstd::iequals(qc.token, "pad")) {
            qc.ExpectToken();
            pRule->radius = qc.TokenFloat() / 2.0f;
        }
        else if (vstd::iequals(qc.token, "radius")) {
            qc.ExpectFloatToken(pRule->radius);
        }
        else if (vstd::iequals(qc.token, "contact")) {
            qc.ExpectIntToken(pRule->contact);
        }
        else if (vstd::iequals(qc.token, "usesequence")) {
            pRule->usesequence = true;
            pRule->usesource = false;
        }
        else if (vstd::iequals(qc.token, "usesource")) {
            pRule->usesequence = false;
            pRule->usesource = true;
        }
        else if (vstd::iequals(qc.token, "fakeorigin")) {
            qc.ExpectFloatToken(pRule->pos.x);
            qc.ExpectFloatToken(pRule->pos.y);
            qc.ExpectFloatToken(pRule->pos.z);

            pRule->bone = -1;
        }
        else if (vstd::iequals(qc.token, "fakerotate")) {
            QAngle ang;

            qc.ExpectFloatToken(ang.x);
            qc.ExpectFloatToken(ang.y);
            qc.ExpectFloatToken(ang.z);

            AngleQuaternion(ang, pRule->q);

            pRule->bone = -1;
        }
        else if (vstd::iequals(qc.token, "bone")) {
            // (this copies the token itself, "bone", as the option always did)
            pRule->bonename = qc.token.view();
        }
        else {
            qc.UnGetToken();
            return pRule;
        }
    }
    return pRule;
}


// The current token as a float; ".." stands for "unset" (-1).
static float TokenFloatOrNull(ScriptTokenizer &qc) {
    if (qc.token == "..")
        return -1;
    return qc.TokenFloat();
}


//-----------------------------------------------------------------------------
// Purpose: parse order dependant s_animcmd_t token for $animations
//-----------------------------------------------------------------------------
int ParseCmdlistToken(StudioMdlState &ctx, ScriptTokenizer &qc, s_cmdlist_t::AnimCmdVector &cmds) {
    // The animation named by the next token; `what` names the command in the error
    const auto expectAnimation = [&](const char *what) {
        qc.ExpectToken();
        s_animation_t* extanim = LookupAnimation(ctx, qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown {} animation '{}\'\n", what, qc.token);
        }
        return extanim;
    };
    // `align` / `alignbone` controls: at least one motion type, then the current token is the source frame
    const auto expectAlignControls = [&]() {
        StudioMotionFlags motion = StudioMotionFlags::None;
        qc.ExpectToken();
        StudioMotionFlags ctrl;
        while ((ctrl = lookupControl(qc.token)) != StudioMotionFlags::Invalid) {
            motion |= ctrl;
            qc.ExpectToken();
        }
        if (motion == StudioMotionFlags::None) {
            qc.Error("missing controls on align\n");
        }
        return motion;
    };
    // controls after walkframe / walkalignto: stops (and gives the token back) at the first one that is not
    const auto readMotionControls = [&]() {
        StudioMotionFlags motion = StudioMotionFlags::None;
        while (qc.TokenAvailable()) {
            qc.ExpectToken();
            const StudioMotionFlags ctrl = lookupControl(qc.token);
            if (ctrl != StudioMotionFlags::Invalid) {
                motion |= ctrl;
            }
            else {
                qc.UnGetToken();
                break;
            }
        }
        return motion;
    };

    if (vstd::iequals("fixuploop", qc.token)) {
        const int start = qc.ExpectIntToken<int>();
        const int end = qc.ExpectIntToken<int>();
        AddCmdFixupLoop(cmds, start, end);
    }
    else if (vstd::istarts_with(qc.token, "weight")) {
        qc.ExpectToken();

        int i;
        for (i = 1; i < std::ssize(ctx.anim.weightlist); i++) {
            if (vstd::iequals(ctx.anim.weightlist[i].name, qc.token)) {
                break;
            }
        }
        if (i == std::ssize(ctx.anim.weightlist)) {
            qc.Error("unknown weightlist '{}\'\n", qc.token);
        }
        AddCmdWeights(cmds, i);
    }
    else if (vstd::iequals("subtract", qc.token)) {
        s_animation_t* extanim = expectAnimation("subtract");
        const int frame = qc.ExpectIntToken<int>();
        AddCmdSubtract(cmds, extanim, frame, true);
    }
    else if (vstd::iequals("presubtract", qc.token)) // FIXME: rename this to something better
    {
        s_animation_t* extanim = expectAnimation("presubtract");
        const int frame = qc.ExpectIntToken<int>();
        AddCmdSubtract(cmds, extanim, frame, false);
    }
    else if (vstd::iequals("alignto", qc.token)) {
        s_animation_t* extanim = expectAnimation("alignto");
        AddCmdAlign(ctx, cmds, extanim, StudioMotionFlags::X | StudioMotionFlags::Y, 0, 0, {});
    }
    else if (vstd::iequals("align", qc.token)) {
        s_animation_t* extanim = expectAnimation("align");

        // motion type to match
        const StudioMotionFlags motion = expectAlignControls();

        // frame of reference animation to match
        const int srcframe = qc.TokenInt();

        // against what frame of the current animation
        const int destframe = qc.ExpectIntToken<int>();
        AddCmdAlign(ctx, cmds, extanim, motion, srcframe, destframe, {});
    }
    else if (vstd::iequals("alignboneto", qc.token)) {
        qc.ExpectToken();
        const std::string bone(qc.token.view());

        s_animation_t* extanim = expectAnimation("alignboneto");
        AddCmdAlign(ctx, cmds, extanim, StudioMotionFlags::X | StudioMotionFlags::Y, 0, 0, bone);
    }
    else if (vstd::iequals("alignbone", qc.token)) {
        qc.ExpectToken();
        const std::string bone(qc.token.view());

        s_animation_t* extanim = expectAnimation("alignboneto");

        // motion type to match
        const StudioMotionFlags motion = expectAlignControls();

        // frame of reference animation to match
        const int srcframe = qc.TokenInt();

        // against what frame of the current animation
        const int destframe = qc.ExpectIntToken<int>();
        AddCmdAlign(ctx, cmds, extanim, motion, srcframe, destframe, bone);
    }
    else if (vstd::iequals("match", qc.token)) {
        s_animation_t* extanim = expectAnimation("match");
        AddCmdMatch(cmds, extanim);
    }
    else if (vstd::iequals("matchblend", qc.token)) {
        s_animation_t* extanim = expectAnimation("match");

        // frame of reference animation to match
        const int srcframe = qc.ExpectIntToken<int>();

        // against what frame of the current animation
        const int destframe = qc.ExpectIntToken<int>();

        // backup and starting match in here
        const int destpre = qc.ExpectIntToken<int>();

        // continue blending match till here
        const int destpost = qc.ExpectIntToken<int>();
        AddCmdMatchBlend(cmds, extanim, srcframe, destframe, destpre, destpost);
    }
    else if (vstd::iequals("worldspaceblend", qc.token)) {
        s_animation_t* extanim = expectAnimation("worldspaceblend");
        AddCmdWorldSpaceBlend(cmds, extanim, 0, false);
    }
    else if (vstd::iequals("worldspaceblendloop", qc.token)) {
        s_animation_t* extanim = expectAnimation("worldspaceblend");
        const int startframe = qc.ExpectIntToken<int>();
        AddCmdWorldSpaceBlend(cmds, extanim, startframe, true);
    }
    else if (vstd::iequals("rotateto", qc.token)) {
        AddCmdRotateTo(cmds, qc.ExpectFloatToken<float>());
    }
    else if (vstd::iequals("ikrule", qc.token)) {
        AddCmdIkRule(cmds, CMD_IKRULE, Option_IKRule(ctx, qc));
    }
    else if (vstd::iequals("ikfixup", qc.token)) {
        AddCmdIkRule(cmds, CMD_IKFIXUP, Option_IKRule(ctx, qc));
    }
    else if (vstd::iequals("walkframe", qc.token)) {
        // frame
        const int endframe = qc.ExpectIntToken<int>();

        // motion type to match
        AddCmdWalkFrame(cmds, endframe, readMotionControls());
    }
    else if (vstd::iequals("walkalignto", qc.token)) {
        const int endframe = qc.ExpectIntToken<int>();

        // reference animation
        s_animation_t* extanim = expectAnimation("alignto");

        // motion type to match
        const StudioMotionFlags motion = readMotionControls();
        AddCmdWalkAlign(cmds, endframe, extanim, motion, 0, endframe);
    }
    else if (vstd::iequals("walkalign", qc.token)) {
        // end frame to apply motion over
        const int endframe = qc.ExpectIntToken<int>();

        // reference animation
        s_animation_t* extanim = expectAnimation("alignto");

        // motion type to match
        StudioMotionFlags motion = StudioMotionFlags::None;
        while (qc.TokenAvailable()) {
            qc.ExpectToken();
            const StudioMotionFlags ctrl = lookupControl(qc.token);
            if (ctrl != StudioMotionFlags::Invalid) {
                motion |= ctrl;
            }
            else {
                break;
            }
        }
        if (motion == StudioMotionFlags::None) {
            qc.Error("missing controls on walkalign\n");
        }

        // frame of reference animation to match
        const int refframe = qc.TokenInt();

        // against what frame of the current animation
        const int srcframe = qc.ExpectIntToken<int>();
        AddCmdWalkAlign(cmds, endframe, extanim, motion, refframe, srcframe);
    }
    else if (vstd::iequals("derivative", qc.token)) {
        // get scale
        AddCmdDerivative(cmds, qc.ExpectFloatToken<float>());
    }
    else if (vstd::iequals("noanimation", qc.token)) {
        AddCmdNoAnimation(cmds, false);
    }
    else if (vstd::iequals("noanim_keepduration", qc.token)) {
        AddCmdNoAnimation(cmds, true);
    }
    else if (vstd::iequals("lineardelta", qc.token)) {
        AddCmdLinearDelta(cmds, false);
    }
    else if (vstd::iequals("splinedelta", qc.token)) {
        AddCmdLinearDelta(cmds, true);
    }
    else if (vstd::iequals("compress", qc.token)) {
        // get frames to skip
        AddCmdCompress(cmds, qc.ExpectIntToken<int>());
    }
    else if (vstd::iequals("numframes", qc.token)) {
        // get frames to force
        AddCmdNumFrames(cmds, qc.ExpectIntToken<int>());
    }
    else if (vstd::iequals("counterrotate", qc.token)) {
        // get bone name
        qc.ExpectToken();
        AddCmdCounterRotate(ctx, cmds, qc.token.view(), std::nullopt);
    }
    else if (vstd::iequals("counterrotateto", qc.token)) {
        std::array<float, 3> target{};

        // get pitch
        qc.ExpectFloatToken(target[0]);

        // get yaw
        qc.ExpectFloatToken(target[1]);

        // get roll
        qc.ExpectFloatToken(target[2]);

        // get bone name
        qc.ExpectToken();
        AddCmdCounterRotate(ctx, cmds, qc.token.view(), target);
    }
    else if (vstd::iequals("localhierarchy", qc.token)) {
        // get bone name
        qc.ExpectToken();
        const std::string bone(qc.token.view());

        // get parent name
        qc.ExpectToken();
        const std::string parent(qc.token.view());

        int start = -1, peak = -1, tail = -1, end = -1;

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "range")) {
                //
                qc.ExpectToken();
                start = static_cast<int>(TokenFloatOrNull(qc));

                //
                qc.ExpectToken();
                peak = static_cast<int>(TokenFloatOrNull(qc));

                //
                qc.ExpectToken();
                tail = static_cast<int>(TokenFloatOrNull(qc));

                //
                qc.ExpectToken();
                end = static_cast<int>(TokenFloatOrNull(qc));
            }
            else {
                qc.UnGetToken();
            }
        }
        AddCmdLocalHierarchy(ctx, cmds, bone, parent, start, peak, tail, end);
    }
    else if (vstd::iequals("forceboneposrot", qc.token)) {
        // get bone name
        qc.ExpectToken();
        const std::string bone(qc.token.view());

        std::optional<std::array<float, 3>> position;
        std::optional<std::array<float, 3>> rotation;
        bool rotationIsLocal = false;

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "pos")) {
                std::array<float, 3> value{};
                for (float &component: value) {
                    qc.ExpectToken();
                    component = TokenFloatOrNull(qc);
                }
                position = value;
            }
            else {
                qc.UnGetToken();
            }

            if (qc.TokenAvailable()) {
                qc.ExpectToken();
                if (vstd::iequals(qc.token, "rot")) {
                    std::array<float, 3> value{};
                    for (float &component: value) {
                        qc.ExpectToken();
                        component = TokenFloatOrNull(qc);
                    }
                    rotation = value;

                    if (qc.TokenAvailable()) {
                        qc.ExpectToken();
                        if (vstd::iequals(qc.token, "local")) {
                            rotationIsLocal = true;
                        }
                        else {
                            qc.UnGetToken();
                        }
                    }
                }
                else {
                    qc.UnGetToken();
                }
            }
        }
        AddCmdForceBonePosRot(ctx, cmds, bone, position, rotation, rotationIsLocal);
    }
    else if (vstd::iequals("bonedriver", qc.token)) {
        int axis = 0;
        float value = 1.0f;
        std::optional<BoneDriverRange> range;

        // get bone name
        qc.ExpectToken();
        const std::string bone(qc.token.view());

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "axis")) {
                qc.ExpectToken();
                if (vstd::iequals(qc.token, "x")) {
                    axis = 0;
                }
                else if (vstd::iequals(qc.token, "y")) {
                    axis = 1;
                }
                else if (vstd::iequals(qc.token, "z")) {
                    axis = 2;
                }
                else {
                    qc.Error("Unknown bonedriver axis.\n");
                }
            }
            else {
                qc.UnGetToken();
            }
        }

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "value")) {
                qc.ExpectToken();
                value = TokenFloatOrNull(qc);
            }
            else {
                qc.UnGetToken();
            }
        }

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "range")) {
                BoneDriverRange driverRange;
                qc.ExpectIntToken(driverRange.start);
                qc.ExpectIntToken(driverRange.peak);
                qc.ExpectIntToken(driverRange.tail);
                qc.ExpectIntToken(driverRange.end);
                range = driverRange;
            }
            else {
                qc.UnGetToken();
            }
        }
        AddCmdBoneDriver(ctx, cmds, bone, axis, value, range);
    }
    else if (vstd::iequals("reverse", qc.token)) {
        AddCmdReverse(cmds);
    }
    else if (vstd::iequals("appendanim", qc.token)) {
        // reference animation
        s_animation_t* extanim = expectAnimation("appendanim");
        AddCmdAppendAnim(cmds, extanim);
    }
    else {
        return false;
    }
    return true;
}

void Cmd_Cmdlist(StudioMdlState &ctx, ScriptTokenizer &qc) {
    int depth = 0;

    // name
    qc.ExpectToken();
    auto &cmd = ctx.anim.cmdlists.emplace_back(s_cmdlist_t{
        .name = qc.token,
    });

    while (1) {
        if (depth > 0) {
            if (!qc.GetToken(true))
                qc.Error("missing '}}' at end of file");
        }
        else {
            if (!qc.TokenAvailable()) {
                break;
            }
            qc.ExpectToken();
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            return;
        }
        if (vstd::iequals("{", qc.token)) {
            depth++;
        }
        else if (vstd::iequals("}", qc.token)) {
            depth--;
        }
        else if (ParseCmdlistToken(ctx, qc, cmd.cmds)) {
        }
        else {
            qc.Error("unknown command: {}\n", qc.token);
        }

        if (depth < 0) {
            qc.Error("missing {{\n");
        }
    };
}

//-----------------------------------------------------------------------------
// Purpose: parse order independant s_animation_t token for $animations
//-----------------------------------------------------------------------------
bool ParseAnimationToken(StudioMdlState &ctx, ScriptTokenizer &qc, s_animation_t* panim) {
    if (vstd::iequals("if", qc.token)) {
        // fixme: add expression evaluation
        qc.ExpectToken();
        if (atoi(qc.token) == 0 && !vstd::iequals(qc.token, "true")) {
            qc.GetToken(true);
            if (qc.token[0] == '{') {
                int depth = 1;
                while (qc.TokenAvailable() && depth > 0) {
                    qc.GetToken(true);
                    if (vstd::iequals("{", qc.token)) {
                        depth++;
                    }
                    else if (vstd::iequals("}", qc.token)) {
                        depth--;
                    }
                }
            }
        }
        return true;
    }

    if (vstd::iequals("fps", qc.token)) {
        const float fps = qc.ExpectFloatToken<float>();
        if (fps <= 0.0f) {
            qc.Error("ParseAnimationToken:  fps ({:f} from '{}') <= 0.0\n", fps, qc.token);
        }
        ApplyAnimationFps(panim, fps);
        return true;
    }

    if (vstd::iequals("origin", qc.token)) {
        const float x = qc.ExpectFloatToken<float>();
        const float y = qc.ExpectFloatToken<float>();
        const float z = qc.ExpectFloatToken<float>();
        ApplyAnimationOrigin(panim, x, y, z);
        return true;
    }

    if (vstd::iequals("rotate", qc.token)) {
        qc.ExpectToken();
        ApplyAnimationRotate(panim, qc.TokenFloat());
        return true;
    }

    if (vstd::iequals("angles", qc.token)) {
        qc.ExpectToken();
        const float x = qc.TokenFloat();
        qc.ExpectToken();
        const float y = qc.TokenFloat();
        qc.ExpectToken();
        const float z = qc.TokenFloat();
        ApplyAnimationAngles(panim, x, y, z);
        return true;
    }

    if (vstd::iequals("scale", qc.token)) {
        ApplyAnimationScale(panim, qc.ExpectFloatToken<float>());
        return true;
    }

    if (vstd::istarts_with(qc.token, "loop")) {
        ApplyAnimationLoop(panim);
        return true;
    }

    if (vstd::iequals("noforceloop", qc.token)) {
        ApplyAnimationNoForceLoop(panim);
        return true;
    }

    if (!Q_strcmp("startloop", qc.token)) {
        ApplyAnimationStartLoop(panim, qc.ExpectIntToken<int>());
        return true;
    }

    if (!Q_strcmp("percentstartloop", qc.token)) {
        ApplyAnimationPercentStartLoop(panim, qc.ExpectFloatToken<float>());
        return true;
    }

    if (vstd::iequals("fudgeloop", qc.token)) {
        ApplyAnimationFudgeLoop(panim);
        return true;
    }

    if (vstd::istarts_with(qc.token, "snap")) {
        ApplyAnimationSnap(panim);
        return true;
    }

    if (vstd::istarts_with(qc.token, "frame") || vstd::istarts_with(qc.token, "framestart")) {
        // framestart assumes the animation's end frame is ok to use no matter what it is. This is better than finding 'frame 9 10000' in qc script
        bool bUseDefaultEndFrame = (vstd::istarts_with(qc.token, "framestart"));

        const int startFrame = qc.ExpectIntToken<int>();
        int endFrame = panim->endframe;

        if (!bUseDefaultEndFrame) {
            qc.ExpectIntToken(endFrame);
        }

        ApplyAnimationFrameRange(ctx, panim, startFrame, endFrame, bUseDefaultEndFrame);
        return true;
    }

    if (vstd::iequals("blockname", qc.token)) {
        qc.ExpectToken();
        ApplyAnimationBlockName(ctx, panim, qc.token.view());
        return true;
    }

    if (vstd::iequals("post", qc.token)) {
        ApplyAnimationPost(panim);
        return true;
    }

    if (vstd::iequals("noautoik", qc.token)) {
        ApplyAnimationNoAutoIk(panim, true);
        return true;
    }

    if (vstd::iequals("autoik", qc.token)) {
        ApplyAnimationNoAutoIk(panim, false);
        return true;
    }

    if (ParseCmdlistToken(ctx, qc, panim->cmds))
        return true;

    if (vstd::iequals("cmdlist", qc.token)) {
        qc.ExpectToken(); // A

        int i;
        for (i = 0; i < ctx.anim.cmdlists.size(); i++) {
            if (vstd::iequals(ctx.anim.cmdlists[i].name, qc.token)) {
                break;
            }
        }
        if (i == ctx.anim.cmdlists.size())
            qc.Error("unknown cmdlist {}\n", qc.token);

        for (auto & cmd : ctx.anim.cmdlists[i].cmds) {
            if (panim->cmds.full()) {
                qc.Error("Too many cmds in {}\n", panim->name);
            }
            panim->cmds.emplace_back(cmd);
        }
        return true;
    }

    if (vstd::iequals("motionrollback", qc.token)) {
        ApplyAnimationMotionRollback(panim, qc.ExpectFloatToken<float>());
        return true;
    }

    if (vstd::iequals("noanimblock", qc.token)) {
        ApplyAnimationDisableAnimBlocks(panim);
        return true;
    }

    if (vstd::iequals("noanimblockstall", qc.token)) {
        ApplyAnimationFirstSectionLocal(panim);
        return true;
    }

    if (vstd::iequals("nostallframes", qc.token)) {
        qc.ExpectToken();
        ApplyAnimationNoStallFrames(panim, qc.TokenFloat());
        return true;
    }

    if (lookupControl(qc.token) != StudioMotionFlags::Invalid) {
        ApplyAnimationMotion(panim, lookupControl(qc.token));
        return true;
    }

    return false;
}


//-----------------------------------------------------------------------------
// Purpose: wrapper for parsing $animation tokens
//-----------------------------------------------------------------------------

int ParseAnimation(StudioMdlState &ctx, ScriptTokenizer &qc, s_animation_t* panim, bool isAppend) {
    int depth = 0;

    while (true) {
        if (depth > 0) {
            if (!qc.GetToken(true))
                qc.Error("missing '}}' at end of file");
        }
        else {
            if (!qc.TokenAvailable()) {
                break;
            }
            else {
                qc.ExpectToken();
            }
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            return 1;
        }
        if (vstd::iequals("{", qc.token)) {
            depth++;
        }
        else if (vstd::iequals("}", qc.token)) {
            depth--;
        }
        else if (ParseAnimationToken(ctx, qc, panim)) {
        }
        else {
            qc.Error("Unknown animation option\'{}\'\n", qc.token);
        }

        if (depth < 0) {
            qc.Error("missing {{\n");
        }
    };

    return 0;
}


//-----------------------------------------------------------------------------
// Purpose: allocate an entry for $animation
//-----------------------------------------------------------------------------
void Cmd_Animation(StudioMdlState &ctx, ScriptTokenizer &qc) {
    // name
    qc.ExpectToken();

    s_animation_t* panim = LookupAnimation(ctx, qc.token);

    if (panim != nullptr) {
        if (!panim->isOverride) {
            qc.Error("Duplicate animation name \"{}\"\n", qc.token);
        }
        else {
            panim->doesOverride = true;
            ParseEmpty(qc);
            return;
        }
    }

    // allocate animation entry
    StudioName name;
    name = qc.token.view();

    // filename
    std::string filename;
    qc.ExpectStringToken(filename);

    panim = ApplyAnimation(ctx, name.view(), filename);

    ApplyAnimationSource(ctx, panim, Load_Source(ctx, panim->filename.c_str(), ""));

    ParseAnimation(ctx, qc, panim, false);

    FinishAnimation(panim);

    //CheckAutoShareAnimationGroup( panim->name );
}

//-----------------------------------------------------------------------------
// Purpose: allocate an entry for $sequence
//-----------------------------------------------------------------------------
s_sequence_t* ProcessCmdSequence(StudioMdlState &ctx, const char* pSequenceName) {
    s_animation_t* panim = LookupAnimation(ctx, pSequenceName);

    // allocate sequence
    if (panim != nullptr) {
        if (!panim->isOverride) {
            MdlError("Duplicate sequence name \"{}\"\n", pSequenceName);
        }
        else {
            panim->doesOverride = true;
            return nullptr;
        }
    }

    if (std::ssize(ctx.anim.sequence) >= MAXSTUDIOSEQUENCES) {
        MdlError("Too many sequences ({} max)\n", MAXSTUDIOSEQUENCES);
    }

    // value-initialized: everything zero
    s_sequence_t* pseq = &ctx.anim.sequence.emplace_back();

    // initialize sequence
    Q_strncpy(pseq->name, pSequenceName, sizeof(pseq->name));

    pseq->actweight = 0;
    pseq->activityname[0] = '\0';
    pseq->activity = -1; // -1 is the default for 'no activity'

    pseq->paramindex[0] = -1;
    pseq->paramindex[1] = -1;

    pseq->groupsize[0] = 0;
    pseq->groupsize[1] = 0;

    pseq->fadeintime = ctx.options.defaultFadeInTime;
    pseq->fadeouttime = ctx.options.defaultFadeOutTime;
    return pseq;
}


int Option_Activity(StudioMdlState &ctx, ScriptTokenizer &qc, s_sequence_t* psequence) {
    qc.ExpectToken();
    const std::string name(qc.token.view());

    const int weight = qc.ExpectIntToken<int>();
    ApplySequenceActivity(ctx, psequence, name, weight);

    return 0;
}


int Option_ActivityModifier(ScriptTokenizer &qc, s_sequence_t* psequence) {
    qc.ExpectToken();

    if (qc.token[0] == '{') {
        while (qc.TokenAvailable()) {
            qc.GetToken(true);
            if (vstd::iequals("}", qc.token))
                break;

            ApplySequenceActivityModifier(psequence, qc.token.view());
        }
    }
    else {
        ApplySequenceActivityModifier(psequence, qc.token.view());
    }

    return 0;
}

//-----------------------------------------------------------------------------
// Purpose: create a virtual $animation command from a $sequence reference
//-----------------------------------------------------------------------------
s_animation_t* ProcessImpliedAnimation(StudioMdlState &ctx, s_sequence_t* psequence, const char* filename) {
    // allocate animation entry
    s_animation_t* panim = ctx.anim.animations.emplace_back(ctx.arena.Create<s_animation_t>());
    panim->index = ctx.anim.animations.size() - 1;

    panim->isImplied = true;

    panim->startframe = 0;
    panim->endframe = MAXSTUDIOANIMFRAMES - 1;

    strcpy(panim->name, "@");
    strcat(panim->name, psequence->name);
    panim->filename = filename;

    VectorCopy(ctx.script.defaultadjust, panim->adjust);
    panim->scale = 1.0f;
    panim->rotation = ctx.script.defaultrotation;
    panim->fps = 30;
    panim->motionrollback = ctx.options.defaultMotionRollback;

    //panim->source = Load_Source( panim->filename, "smd" );
    panim->source = Load_Source(ctx, panim->filename.c_str(), "");
    if (!panim->source->m_Animations.empty()) {
        s_sourceanim_t* pSourceAnim = &panim->source->m_Animations[0];
        Q_strncpy(panim->animationname, panim->source->m_Animations[0].animationname, sizeof(panim->animationname));
        if (panim->startframe < pSourceAnim->startframe) {
            panim->startframe = pSourceAnim->startframe;
        }

        if (panim->endframe > pSourceAnim->endframe) {
            panim->endframe = pSourceAnim->endframe;
        }
    }
    else {
        Q_strncpy(panim->animationname, "", sizeof(panim->animationname));
    }

    if (!ctx.options.createMakefile && panim->endframe < panim->startframe) {
        MdlError("end frame before start frame in {}", panim->name);
    }

    panim->numframes = panim->endframe - panim->startframe + 1;

    //CheckAutoShareAnimationGroup( panim->name );

    return panim;
}

//-----------------------------------------------------------------------------
// Purpose: copy globally reavent $animation options from one $animation to another
//-----------------------------------------------------------------------------

void CopyAnimationSettings(s_animation_t* pdest, s_animation_t* psrc) {
    pdest->fps = psrc->fps;

    VectorCopy(psrc->adjust, pdest->adjust);
    pdest->scale = psrc->scale;
    pdest->rotation = psrc->rotation;

    pdest->motiontype = psrc->motiontype;

    //Adrian - Hey! Revisit me later.
    /*if (pdest->startframe < psrc->startframe)
		pdest->startframe = psrc->startframe;

	if (pdest->endframe > psrc->endframe)
		pdest->endframe = psrc->endframe;

	if (pdest->endframe < pdest->startframe)
		TokenError( "fixedup end frame before start frame in %s", pdest->name );

	pdest->numframes = pdest->endframe - pdest->startframe + 1;*/

    for (int i = 0; i < psrc->cmds.size(); i++) {
        if (pdest->cmds.size() >= MAXSTUDIOCMDS) {
            MdlError("Too many cmds in {}\n", pdest->name);
        }
        pdest->cmds.emplace_back(psrc->cmds[i]);
    }
}


//-----------------------------------------------------------------------------
// Performs processing on a sequence
//-----------------------------------------------------------------------------
void ProcessSequence(s_sequence_t* pseq, int numblends, s_animation_t** animations, bool isAppend) {
    if (isAppend)
        return;

    if (numblends == 0) {
        MdlError("sequence \"{}\": no animations found\n", pseq->name);
    }

    if (pseq->groupsize[0] == 0) {
        if (numblends < 4) {
            pseq->groupsize[0] = numblends;
            pseq->groupsize[1] = 1;
        }
        else {
            int i = sqrt((float) numblends);
            if (i * i == numblends) {
                pseq->groupsize[0] = i;
                pseq->groupsize[1] = i;
            }
            else {
                MdlError("sequence \"{}\": non-square ({}) number of blends without \"blendwidth\" set\n", pseq->name,
                         numblends);
            }
        }
    }
    else {
        pseq->groupsize[1] = numblends / pseq->groupsize[0];

        if (pseq->groupsize[0] * pseq->groupsize[1] != numblends) {
            MdlError("missing animation blends. Expected {}, found {}\n",
                     pseq->groupsize[0] * pseq->groupsize[1], numblends);
        }
    }

    for (int i = 0; i < numblends; i++) {
        int j = i % pseq->groupsize[0];
        int k = i / pseq->groupsize[0];

        pseq->panim[j][k] = animations[i];

        if (i > 0 && animations[i]->isImplied) {
            CopyAnimationSettings(animations[i], animations[0]);
        }
        animations[i]->isImplied = false; // don't copy any more commands
        pseq->flags |= animations[i]->flags;
    }

    pseq->numblends = numblends;
}


//-----------------------------------------------------------------------------
// Purpose: parse options unique to $sequence
//-----------------------------------------------------------------------------
int ParseSequence(StudioMdlState &ctx, ScriptTokenizer &qc, s_sequence_t* pseq, bool isAppend) {
    ctx.script.szInCurrentSeqName = pseq->name;

    int depth = 0;
    s_animation_t* animations[64];
    int i, j, n;
    int numblends = 0;

    if (isAppend) {
        animations[0] = pseq->panim[0][0];
    }

    while (1) {
        if (depth > 0) {
            if (!qc.GetToken(true))
                qc.Error("missing '}}' at end of file");
        }
        else {
            if (!qc.TokenAvailable()) {
                break;
            }
            else {
                qc.ExpectToken();
            }
        }

        if (qc.EndOfScript()) {
            if (depth != 0) {
                qc.Error("missing }}\n");
            }
            return 1;
        }
        if (vstd::iequals("{", qc.token)) {
            depth++;
        }
        else if (vstd::iequals("}", qc.token)) {
            depth--;
        }
        /*
    else if (stricmp("deform", token ) == 0)
    {
        Option_Deform( pseq );
    }
    */
        else if (vstd::iequals("animtag", qc.token)) {
            depth -= Option_AnimTag(qc, pseq);
        }
        else if (vstd::iequals("event", qc.token)) {
            depth -= Option_Event(qc, pseq);
        }
        else if (vstd::iequals("activity", qc.token)) {
            Option_Activity(ctx, qc, pseq);
        }
        else if ((vstd::iequals("activitymodifier", qc.token)) || (vstd::iequals("actmod", qc.token))) {
            Option_ActivityModifier(qc, pseq);
        }
        else if (vstd::istarts_with(qc.token, "ACT_")) {
            qc.UnGetToken();
            Option_Activity(ctx, qc, pseq);
        }
        else if (vstd::iequals("snap", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Snap;
        }
        else if (vstd::iequals("blendwidth", qc.token)) {
            qc.ExpectIntToken(pseq->groupsize[0]);
        }
        else if (vstd::iequals("blend", qc.token)) {
            qc.ExpectToken();
            const int poseParameter = LookupPoseParameter(ctx, qc.token);
            const float start = qc.ExpectFloatToken<float>();
            const float end = qc.ExpectFloatToken<float>();
            ApplySequenceBlend(ctx, pseq, poseParameter, start, end);
        }
        else if (vstd::iequals("calcblend", qc.token)) {
            qc.ExpectToken();
            const int poseParameter = LookupPoseParameter(ctx, qc.token);

            qc.ExpectToken();
            const int attachment = LookupAttachment(ctx, qc.token);
            if (attachment == -1) {
                qc.Error("Unknown calcblend attachment \"{}\"\n", qc.token);
            }

            qc.ExpectToken();
            ApplySequenceCalcBlend(pseq, poseParameter, attachment, lookupControl(qc.token));
        }
        else if (vstd::iequals("blendref", qc.token)) {
            qc.ExpectToken();
            pseq->paramanim = LookupAnimation(ctx, qc.token);
            if (pseq->paramanim == nullptr) {
                qc.Error("Unknown blendref animation \"{}\"\n", qc.token);
            }
        }
        else if (vstd::iequals("blendcomp", qc.token)) {
            qc.ExpectToken();
            pseq->paramcompanim = LookupAnimation(ctx, qc.token);
            if (pseq->paramcompanim == nullptr) {
                qc.Error("Unknown blendcomp animation \"{}\"\n", qc.token);
            }
        }
        else if (vstd::iequals("blendcenter", qc.token)) {
            qc.ExpectToken();
            pseq->paramcenter = LookupAnimation(ctx, qc.token);
            if (pseq->paramcenter == nullptr) {
                qc.Error("Unknown blendcenter animation \"{}\"\n", qc.token);
            }
        }
        else if (vstd::iequals("node", qc.token)) {
            qc.ExpectToken();
            ApplySequenceNode(ctx, pseq, qc.token.view());
        }
        else if (vstd::iequals("transition", qc.token)) {
            qc.ExpectToken();
            const std::string from(qc.token.view());
            qc.ExpectToken();
            ApplySequenceTransition(ctx, pseq, from, qc.token.view(), false);
        }
        else if (vstd::iequals("rtransition", qc.token)) {
            qc.ExpectToken();
            const std::string from(qc.token.view());
            qc.ExpectToken();
            ApplySequenceTransition(ctx, pseq, from, qc.token.view(), true);
        }
        else if (vstd::iequals("exitphase", qc.token)) {
            qc.ExpectFloatToken(pseq->exitphase);
        }
        else if (vstd::iequals("delta", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Delta;
            pseq->flags |= StudioSequenceFlags::Post;
        }
        else if (vstd::iequals("worldspace", qc.token)) {
            pseq->flags |= StudioSequenceFlags::World;
            pseq->flags |= StudioSequenceFlags::Post;
        }
        else if (vstd::iequals("worldrelative", qc.token)) {
            pseq->flags |= StudioSequenceFlags::WorldAndRelative;
            pseq->flags |= StudioSequenceFlags::Post;
        }
        else if (vstd::iequals("rootdriver", qc.token)) {
            // get bone name
            qc.ExpectToken();

            ApplySequenceRootDriver(pseq, qc.token.view());
        }
        else if (vstd::iequals("post", qc.token)) // remove
        {
            pseq->flags |= StudioSequenceFlags::Post;
        }
        else if (vstd::iequals("predelta", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Delta;
        }
        else if (vstd::iequals("autoplay", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Autoplay;
        }
        else if (vstd::iequals("fadein", qc.token)) {
            qc.ExpectFloatToken(pseq->fadeintime);
        }
        else if (vstd::iequals("fadeout", qc.token)) {
            qc.ExpectFloatToken(pseq->fadeouttime);
        }
        else if (vstd::iequals("realtime", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Realtime;
        }
        else if (vstd::iequals("posecycle", qc.token)) {
            qc.ExpectToken();
            ApplySequencePoseCycle(pseq, LookupPoseParameter(ctx, qc.token));
        }
        else if (vstd::iequals("hidden", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Hidden;
        }
        else if (vstd::iequals("addlayer", qc.token)) {
            qc.ExpectToken();
            const std::string layer(qc.token.view());

            bool local = false;
            while (qc.TokenAvailable()) {
                qc.ExpectToken();
                if (vstd::iequals("local", qc.token)) {
                    local = true;
                }
                else {
                    qc.UnGetToken();
                    break;
                }
            }
            ApplySequenceAddLayer(pseq, layer, local);
        }
        else if (vstd::iequals("iklock", qc.token)) {
            qc.ExpectToken();
            const std::string chain(qc.token.view());

            const float positionWeight = qc.ExpectFloatToken<float>();
            const float localQWeight = qc.ExpectFloatToken<float>();
            ApplySequenceIkLock(pseq, chain, positionWeight, localQWeight);
        }
        else if (vstd::iequals("keyvalues", qc.token)) {
            Option_KeyValues(qc, pseq->KeyValue);
        }
        else if (vstd::iequals("blendlayer", qc.token)) {
            BlendLayerParams params;

            StudioName layer;
            qc.ExpectStringToken(layer);
            params.layer = layer.view();

            qc.ExpectFloatToken(params.start);

            qc.ExpectFloatToken(params.peak);

            qc.ExpectFloatToken(params.tail);

            qc.ExpectFloatToken(params.end);

            while (qc.TokenAvailable()) {
                qc.ExpectToken();
                if (vstd::iequals("xfade", qc.token)) {
                    params.xfade = true;
                }
                else if (vstd::iequals("spline", qc.token)) {
                    params.spline = true;
                }
                else if (vstd::iequals("noblend", qc.token)) {
                    params.noBlend = true;
                }
                else if (vstd::iequals("poseparameter", qc.token)) {
                    qc.ExpectToken();
                    params.poseParameter = LookupPoseParameter(ctx, qc.token);
                }
                else if (vstd::iequals("local", qc.token)) {
                    params.local = true;
                }
                else {
                    qc.UnGetToken();
                    break;
                }
            }
            ApplySequenceBlendLayer(pseq, params);
        }
        else if ((numblends || isAppend) && ParseAnimationToken(ctx, qc, animations[0])) {
        }
        else if (!isAppend) {
            // assume it's an animation reference
            // first look up an existing animation
            for (n = 0; n < ctx.anim.animations.size(); n++) {
                if (vstd::iequals(qc.token, ctx.anim.animations[n]->name)) {
                    animations[numblends++] = ctx.anim.animations[n];
                    break;
                }
            }

            if (n >= ctx.anim.animations.size()) {
                // assume it's an implied animation
                animations[numblends++] = ProcessImpliedAnimation(ctx, pseq, qc.token);
            }
            // hack to allow animation commands to refer to same sequence
            if (numblends == 1) {
                pseq->panim[0][0] = animations[0];
            }
        }
        else {
            qc.Error("unknown command \"{}\"\n", qc.token);
        }

        if (depth < 0) {
            qc.Error("missing {{\n");
        }
    }

    ProcessSequence(pseq, numblends, animations, isAppend);
    return 0;
}

//-----------------------------------------------------------------------------
// Process the sequence command
//-----------------------------------------------------------------------------
void Cmd_Sequence(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    if (ctx.options.lCaseAllSequences)
        vstd::make_lower_ascii(qc.token);

    // Find existing sequences
    const char* pSequenceName = qc.token;
    s_animation_t* panim = LookupAnimation(ctx, pSequenceName);
    if (panim != nullptr && panim->isOverride) {
        ParseEmpty(qc);
    }

    s_sequence_t* pseq = ProcessCmdSequence(ctx, pSequenceName);
    if (pseq) {
        ParseSequence(ctx, qc, pseq, false);
    }
}

//-----------------------------------------------------------------------------
// Purpose: append commands to either a sequence or an animation
//-----------------------------------------------------------------------------
void Cmd_Append(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();


    s_sequence_t* pseq = LookupSequence(ctx, qc.token);

    if (pseq) {
        ParseSequence(ctx, qc, pseq, true);
        return;
    }
    else {
        s_animation_t* panim = LookupAnimation(ctx, qc.token);

        if (panim) {
            ParseAnimation(ctx, qc, panim, true);
            return;
        }
    }
    qc.Error("unknown append animation {}\n", qc.token);
}

void Cmd_Prepend(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    s_sequence_t* pseq = LookupSequence(ctx, qc.token);
    int count = 0;
    s_animation_t* panim = nullptr;
    int iRet = false;

    if (pseq) {
        panim = pseq->panim[0][0];
        count = panim->cmds.size();
        iRet = ParseSequence(ctx, qc, pseq, true);
    }
    else {
        panim = LookupAnimation(ctx, qc.token);
        if (panim) {
            count = panim->cmds.size();
            iRet = ParseAnimation(ctx, qc, panim, true);
        }
    }
    if (panim && count != panim->cmds.size()) {
        s_animcmd_t tmp;
        tmp = panim->cmds[panim->cmds.size() - 1];
        int i;
        for (i = panim->cmds.size() - 1; i > 0; i--) {
            panim->cmds[i] = panim->cmds[i - 1];
        }
        panim->cmds[0] = tmp;
        return;
    }
    qc.Error("unknown prepend animation \"{}\"\n", qc.token);
}

void Cmd_Continue(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();

    s_sequence_t* pseq = LookupSequence(ctx, qc.token);

    if (pseq) {
        if (!qc.GetToken(true))
            return;
        qc.UnGetToken();
        if (qc.token[0] != '$')
            ParseSequence(ctx, qc, pseq, true);
        return;
    }
    else {
        s_animation_t* panim = LookupAnimation(ctx, qc.token);

        if (panim) {
            if (!qc.GetToken(true))
                return;
            qc.UnGetToken();
            if (qc.token[0] != '$')
                ParseAnimation(ctx, qc, panim, true);
            return;
        }
    }
    qc.Error("unknown continue animation {}\n", qc.token);
}

//-----------------------------------------------------------------------------
// Purpose: foward declare an empty sequence
//-----------------------------------------------------------------------------
void Cmd_DeclareSequence(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (std::ssize(ctx.anim.sequence) >= MAXSTUDIOSEQUENCES) {
        qc.Error("Too many sequences ({} max)\n", MAXSTUDIOSEQUENCES);
    }

    // value-initialized: everything zero
    s_sequence_t* pseq = &ctx.anim.sequence.emplace_back();
    pseq->flags = StudioSequenceFlags::Override;

    // initialize sequence
    qc.ExpectStringToken(pseq->name);
}


//-----------------------------------------------------------------------------
// Purpose: forward declare an empty sequence
//-----------------------------------------------------------------------------
void Cmd_DeclareAnimation(StudioMdlState &ctx, ScriptTokenizer &qc) {
    if (ctx.anim.animations.size() >= ctx.anim.animations.capacity()) {
        qc.Error("Too many animations ({} max)\n", MAXSTUDIOANIMS);
    }

    // allocate animation entry
    s_animation_t* panim = ctx.anim.animations.emplace_back(ctx.arena.Create<s_animation_t>());
    panim->index = ctx.anim.animations.size() - 1;
    panim->flags = StudioSequenceFlags::Override;

    // initialize animation
    qc.ExpectStringToken(panim->name);
}

bool Grab_AimAtBones(StudioMdlState &ctx) {
    LineParser header(ctx.input.szLine.view());
    header.Word(); // <aimconstraint>
    const auto bone = header.Word();
    const auto parent = header.Word();
    const auto aim = header.Word();
    if (bone.empty() || parent.empty() || aim.empty() || !header.Empty() ||
        bone.size() > StudioName::capacity() || parent.size() > StudioName::capacity() ||
        aim.size() > StudioName::capacity())
        MdlError("Line {}: malformed procedural aim constraint: {}", ctx.input.iLinecount, ctx.input.szLine);
    auto &aimBone = ApplyAimAtBegin(ctx, bone, parent, aim);

    while (GetLineInput(ctx)) {
        ctx.input.iLinecount++;
        if (IsEnd(ctx.input.szLine))
            return false;

        LineParser line(ctx.input.szLine.view());
        const auto command = line.Word();
        if (command.empty())
            continue;
        if (!vstd::iequals(command, "<aimvector>") && !vstd::iequals(command, "<upvector>") &&
            !vstd::iequals(command, "<basepos>"))
            return true; // Leave the next record for Grab_QuatInterpBones.

        Vector vector;
        if (!line.Read(vector.x, vector.y, vector.z))
            MdlError("Line {}: malformed procedural aim vector: {}", ctx.input.iLinecount, ctx.input.szLine);
        if (vstd::iequals(command, "<aimvector>"))
            ApplyAimAtAimVector(aimBone, vector);
        else if (vstd::iequals(command, "<upvector>"))
            ApplyAimAtUpVector(aimBone, vector);
        else
            ApplyAimAtBasePosition(aimBone, vector);
    }
    return false;
}

void Grab_QuatInterpBones(StudioMdlState &ctx) {
    Vector basepos(0, 0, 0);
    Vector rotateaxis(0, 0, 0); // degrees, as written
    Vector jointorient(0, 0, 0); // degrees, as written
    s_quatinterpbone_t* axisBone = nullptr;

    while (GetLineInput(ctx)) {
        ctx.input.iLinecount++;
        if (IsEnd(ctx.input.szLine)) {
            return;
        }

        LineParser line(ctx.input.szLine.view());
        auto command = line.Word();
        while (vstd::iequals(command, "<aimconstraint>")) {
            if (!Grab_AimAtBones(ctx))
                return;
            line = LineParser(ctx.input.szLine.view());
            command = line.Word();
        }

        if (command.empty())
            continue;
        if (vstd::iequals(command, "<helper>")) {
            const auto bone = line.Word();
            const auto parent = line.Word();
            const auto controlParent = line.Word();
            const auto control = line.Word();
            if (bone.empty() || parent.empty() || controlParent.empty() || control.empty() ||
                bone.size() > StudioName::capacity() || parent.size() > StudioName::capacity() ||
                controlParent.size() > StudioName::capacity() || control.size() > StudioName::capacity())
                MdlError("Line {}: malformed procedural helper: {}", ctx.input.iLinecount, ctx.input.szLine);
            axisBone = &ApplyQuatInterpBegin(ctx, bone, parent, controlParent, control);
        } else if (vstd::iequals(command, "<display>")) {
            Vector size;
            float distance;
            if (!axisBone || !line.Read(size.x, size.y, size.z, distance))
                MdlError("Line {}: Unable to parse procedural <display> bone: {}", ctx.input.iLinecount, ctx.input.szLine);
            ApplyQuatInterpDisplay(*axisBone, size, distance);
        } else if (vstd::iequals(command, "<basepos>")) {
            if (!line.Read(basepos.x, basepos.y, basepos.z))
                MdlError("Line {}: malformed procedural <basepos>: {}", ctx.input.iLinecount, ctx.input.szLine);
        } else if (vstd::iequals(command, "<rotateaxis>")) {
            if (!line.Read(rotateaxis.x, rotateaxis.y, rotateaxis.z))
                MdlError("Line {}: malformed procedural <rotateaxis>: {}", ctx.input.iLinecount, ctx.input.szLine);
        } else if (vstd::iequals(command, "<jointorient>")) {
            if (!line.Read(jointorient.x, jointorient.y, jointorient.z))
                MdlError("Line {}: malformed procedural <jointorient>: {}", ctx.input.iLinecount, ctx.input.szLine);
        } else if (vstd::iequals(command, "<trigger>")) {
            float tolerance;
            Vector trigger, angles, position;
            if (!axisBone || !line.Read(tolerance, trigger.x, trigger.y, trigger.z, angles.x, angles.y, angles.z,
                                        position.x, position.y, position.z))
                MdlError("Line {}: Unable to parse procedural <trigger> bone: {}", ctx.input.iLinecount, ctx.input.szLine);
            if (!ApplyQuatInterpTrigger(*axisBone, basepos, rotateaxis, jointorient, tolerance, trigger, angles, position))
                MdlError("Line {}: too many procedural <trigger> entries (at most 32): {}",
                         ctx.input.iLinecount, ctx.input.szLine);
        } else {
            MdlError("Line {}: Unable to parse procedural bone data: {}", ctx.input.iLinecount, ctx.input.szLine);
        }
    }
}

void Load_ProceduralBones(StudioMdlState &ctx, ScriptTokenizer &qc) {
    qc.ExpectToken();
    const std::string filename = qc.token.str();

    if (!OpenGlobalFile(ctx, filename)) {
        Error("unknown $procedural file \"{}\"\n", filename.c_str());
        return;
    }

    ctx.input.iLinecount = 0;

    if (vstd::iequals(vstd::paths::extension(filename), "vrd")) {
        Grab_QuatInterpBones(ctx);
    }
    else {
        while (GetLineInput(ctx)) {
            ctx.input.iLinecount++;
            LineParser line(ctx.input.szLine.view());
            const auto command = line.Word();
            if (vstd::iequals(command, "version")) {
                int version;
                if (!line.Read(version) || version != 1)
                    qc.Error("bad version\n");
            } else if (vstd::iequals(command, "proceduralbones")) {
                Grab_AxisInterpBones(ctx);
            }
        }
    }
    ctx.input.inputFile.reset();
}

void Cmd_CollisionModel(StudioMdlState &ctx, ScriptTokenizer &qc) {
    DoCollisionModel(ctx, qc, false);
}

void Cmd_CollisionJoints(StudioMdlState &ctx, ScriptTokenizer &qc) {
    DoCollisionModel(ctx, qc, true);
}

namespace {
    struct MDLCommand_t {
        const char *m_pName;
        void (*m_pCmd)(StudioMdlState &ctx, ScriptTokenizer &qc);
    };
}

//
// This is the master list of the commands a QC file supports.
// To add a new command to the QC files, add it here.
//
static constexpr MDLCommand_t g_Commands[] =
{

    {"$collisionmodel", Cmd_CollisionModel},
    {"$collisionjoints", Cmd_CollisionJoints},
    // {"$collisiontext", Cmd_CollisionText},
    {"$cd", Cmd_CD,},
    {"$modelname", Cmd_Modelname,},
    {"$internalname", Cmd_InternalName,},
    {"$cdmaterials", Cmd_CDMaterials,},
    {"$pushd", Cmd_Pushd,},
    {"$popd", Cmd_Popd,},
    {"$scale", Cmd_ScaleUp,},
    {"$root", Cmd_Root,},
    {"$controller", Cmd_Controller,},
    {"$screenalign", Cmd_ScreenAlign,},
    {"$worldalign", Cmd_WorldAlign,},
    {"$model", Cmd_Model,},
    {"$appendsource", Cmd_AppendSource,},
    {"$body", Cmd_Body,},
    {"$bodygroup", Cmd_Bodygroup,},
    {"$appendblankbodygroup", Cmd_AppendBlankBodygroup,},
    {"$bodygrouppreset", Cmd_BodygroupPreset,},
    {"$animation", Cmd_Animation,},
    {"$autocenter", Cmd_Autocenter,},
    {"$sequence", Cmd_Sequence,},
    {"$append", Cmd_Append,},
    {"$prepend", Cmd_Prepend,},
    {"$continue", Cmd_Continue,},
    {"$declaresequence", Cmd_DeclareSequence,},
    {"$declareanimation", Cmd_DeclareAnimation,},
    {"$cmdlist", Cmd_Cmdlist,},
    {"$animblocksize", Cmd_AnimBlockSize,},
    {"$weightlist", Cmd_Weightlist,},
    {"$defaultweightlist", Cmd_DefaultWeightlist,},
    {"$ikchain", Cmd_IKChain,},
    {"$ikautoplaylock", Cmd_IKAutoplayLock,},
    {"$eyeposition", Cmd_Eyeposition,},
    {"$illumposition", Cmd_Illumposition,},
    {"$origin", Cmd_Origin,},
    {"$upaxis", Cmd_UpAxis,},
    {"$bbox", Cmd_BBox,},
    {"$bboxonlyverts", Cmd_BBoxOnlyVerts,},
    {"$cbox", Cmd_CBox,},
    {"$gamma", Cmd_Gamma,},
    {"$texturegroup", Cmd_TextureGroup,},
    {"$hgroup", Cmd_Hitgroup,},
    {"$hbox", Cmd_Hitbox,},
    {"$hboxset", Cmd_HitboxSet,},
    {"$surfaceprop", Cmd_SurfaceProp,},
    {"$jointsurfaceprop", Cmd_JointSurfaceProp,},
    {"$contents", Cmd_Contents,},
    {"$jointcontents", Cmd_JointContents,},
    {"$attachment", Cmd_Attachment,},
    {"$redefineattachment", Cmd_RedefineAttachment,},
    {"$bonemerge", Cmd_BoneMerge,},
    {"$bonealwayssetup", Cmd_BoneAlwaysSetup,},
    {"$externaltextures", Cmd_ExternalTextures,},
    {"$cliptotextures", Cmd_ClipToTextures,},
    {"$skinnedLODs", Cmd_SkinnedLODs,},
    {"$renamebone", Cmd_Renamebone,},
    {"$stripboneprefix", Cmd_StripBonePrefix,},
    {"$renamebonesubstr", Cmd_RenameBoneSubstr,},
    {"$collapsebones", Cmd_CollapseBones,},
    {"$collapsebonesaggressive", Cmd_CollapseBonesAggressive,},
    {"$alwayscollapse", Cmd_AlwaysCollapse,},
    {"$proceduralbones", Load_ProceduralBones,},
    {"$skiptransition", Cmd_Skiptransition,},
    {"$calctransitions", Cmd_CalcTransitions,},
    {"$staticprop", Cmd_StaticProp,},
    {"$zbrush", Cmd_ZBrush,},
    {"$realignbones", Cmd_RealignBones,},
    {"$forcerealign", Cmd_ForceRealign,},
    {"$lod", Cmd_BaseLOD,},
    {"$shadowlod", Cmd_ShadowLOD,},
    {"$poseparameter", Cmd_PoseParameter,},
    {"$heirarchy", Cmd_ForcedHierarchy,},
    {"$hierarchy", Cmd_ForcedHierarchy,},
    {"$insertbone", Cmd_InsertHierarchy,},
    {"$limitrotation", Cmd_LimitRotation,},
    {"$definebone", Cmd_DefineBone,},
    {"$jigglebone", Cmd_JiggleBone,},
    {"$includemodel", Cmd_IncludeModel,},
    {"$opaque", Cmd_Opaque,},
    {"$mostlyopaque", Cmd_TranslucentTwoPass,},
    {"$keyvalues", Cmd_KeyValues,},
    {"$obsolete", Cmd_Obsolete,},
    {"$renamematerial", Cmd_RenameMaterial,},
    {"$overridematerial", Cmd_OverrideMaterial,},
    {"$fakevta", Cmd_FakeVTA,},
    {"$noforcedfade", Cmd_NoForcedFade,},
    {"$skipboneinbbox", Cmd_SkipBoneInBBox,},
    {"$forcephonemecrossfade", Cmd_ForcePhonemeCrossfade,},
    {"$lockbonelengths", Cmd_LockBoneLengths,},
    {"$unlockdefinebones", Cmd_UnlockDefineBones,},
    {"$constantdirectionallight", Cmd_ConstDirectionalLight,},
    {"$minlod", Cmd_MinLOD,},
    {"$allowrootlods", Cmd_AllowRootLODs,},
    {"$bonesaveframe", Cmd_BoneSaveFrame,},
    {"$ambientboost", Cmd_AmbientBoost,},
    {"$centerbonesonverts", Cmd_CenterBonesOnVerts,},
    {"$donotcastshadows", Cmd_DoNotCastShadows,},
    {"$casttextureshadows", Cmd_CastTextureShadows,},
    {"$motionrollback", Cmd_MotionExtractionRollBack,},
    {"$sectionframes", Cmd_SectionFrames,},
    {"$clampworldspace", Cmd_ClampWorldspace,},
    {"$maxeyedeflection", Cmd_MaxEyeDeflection,},
    {"$addsearchdir", Cmd_AddSearchDir,},
    {"$subd", Cmd_SubdivisionSurface,},
    {"$boneflexdriver", Cmd_BoneFlexDriver,},
    {"$maxverts", Cmd_maxVerts,},
    {"$flexoptimize", Cmd_FlexOptimize,},
    {"$preservetriangleorder", Cmd_PreserveTriangleOrder,},
    {"$qcassert", Cmd_QCAssert,},
    {"$lcaseallsequences", Cmd_LCaseAllSequences,},
    {"$defaultfadein", Cmd_SetDefaultFadeInTime,},
    {"$defaultfadeout", Cmd_SetDefaultFadeOutTime,},
    {"$allowactivityname", Cmd_AllowActivityName,},
    {"$collisionprecision", Cmd_CollisionPrecision,},
    {"$erroronsequenceremappingfailure", Cmd_ErrorOnSeqRemapFail,},
    {"$modelhasnosequences", Cmd_SetModelIntentionallyHasZeroSequences,},
    {"$contentrootrelative", Cmd_ContentRootRelative,},
};

void ParseScript(StudioMdlState &ctx, ScriptTokenizer &qc) {
    vstd::FunctionTimingRecorder _("Parse");
    while (qc.GetToken(true)) {
        // Check all the commands we know about.
        const auto command = std::ranges::find_if(g_Commands, [&](const MDLCommand_t &cmd) {
            return vstd::iequals(cmd.m_pName, qc.token);
        });
        if (command != std::end(g_Commands)) {
            ScopedQcPosition position(qc);
            command->m_pCmd(ctx, qc);
        }
        else if (!ctx.options.createMakefile) {
            qc.Error("bad command {}", qc.token);
        }
    }
}

//
// Created by RED on 15.05.2024.
//

#include "studiomdl/platform_compat.h"
#include "studiomdl_commands.h"
#include "studiomdl/flexoptimize.h"
#include "tier1_v2/function_timing.hpp"
#include "datamodel/idatamodel.h"
#include "mdlobjects/dmeboneflexdriver.h"
#include "common/scriplib.h"
#include "studiomdl_errors.h"
#include "movieobjects/movieobjects.h"
#include "movieobjects/dmemdlmakefile.h"
#include "tier1/fmtstr.h"
#include "bspflags.h"
#include "studiomdl/collisionmodel.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

#ifdef WIN32
#undef strdup
#define strdup _strdup
#endif

//-----------------------------------------------------------------------------
// Parse contents flags
//-----------------------------------------------------------------------------
static void ParseContents(ScriptTokenizer &qc, ContentType &addFlags, ContentType &removeFlags) {
    addFlags = ContentType::EMPTY;
    removeFlags = ContentType::EMPTY;
    do {
        qc.ExpectToken();

        if (qc.token.iequals("grate")) {
            addFlags |= ContentType::GRATE;
            removeFlags |= ContentType::SOLID;
        }
        else if (qc.token.iequals("ladder")) {
            addFlags |= ContentType::LADDER;
        }
        else if (qc.token.iequals("solid")) {
            addFlags |= ContentType::SOLID;
        }
        else if (qc.token.iequals("monster")) {
            addFlags |= ContentType::MONSTER;
        }
        else if (qc.token.iequals("notsolid")) {
            removeFlags |= ContentType::SOLID;
        }
    } while (qc.TokenAvailable());
}

void ProcessModelName(const char *pModelName) {
    // Abort early if modelname is too big

    const int nModelNameLen = Q_strlen(pModelName);
    char *pTmpBuf = reinterpret_cast<char *>(_alloca((nModelNameLen + 1) * sizeof(char)));
    Q_StripExtension(pModelName, pTmpBuf, nModelNameLen + 1);
    // write.cpp strips extension then adds .mdl and writes that into studiohdr_t::name

    // Need one for sizeof operation to work...
    studiohdr_t shdr;
    if (Q_strlen(pTmpBuf) + 4 >= (sizeof(g_studiomdl.paths.outname) / sizeof(g_studiomdl.paths.outname[0]))) {
        MdlError("Model Name \"{}.mdl\" Too Big, {} Characters, Max {} Characters\n",
                 pTmpBuf,
                 Q_strlen(pTmpBuf) + 4,
                 (sizeof(g_studiomdl.paths.outname) / sizeof(g_studiomdl.paths.outname)) - 1);
    }

    g_studiomdl.paths.hasModelName = true;
    Q_strncpy(g_studiomdl.paths.outname, pModelName, sizeof(g_studiomdl.paths.outname));
#ifdef POSIX
    // "$modelname dir\\name.mdl" is common in QCs written on Windows; the output path is built from g_studiomdl.paths.outname
    Q_FixSlashes(g_studiomdl.paths.outname, '/');
#endif
}

//-----------------------------------------------------------------------------
// Parse the studio options from a .qc file
//-----------------------------------------------------------------------------
bool ParseOptionStudio(ScriptTokenizer &qc, CDmeSourceSkin *pSkin) {
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
void ProcessOptionStudio(s_model_t *pmodel, const char *pFullPath, float flScale, bool bFlipTriangles, bool bQuadSubd) {
    pmodel->filename = pFullPath;

    if (flScale != 0.0f) {
        pmodel->scale = g_studiomdl.script.currentscale = flScale;
    }
    else {
        pmodel->scale = g_studiomdl.script.currentscale = g_studiomdl.script.defaultscale;
    }

    g_pCurrentModel = pmodel;

    // load source
    pmodel->source = Load_Source(pmodel->filename.c_str(), "", bFlipTriangles, true);

    g_pCurrentModel = nullptr;

    // Reset currentscale to whatever global we currently have set
    // g_studiomdl.script.defaultscale gets set in Cmd_ScaleUp everytime the $scale command is used.
    g_studiomdl.script.currentscale = g_studiomdl.script.defaultscale;
}

//-----------------------------------------------------------------------------
// Process a body command
//-----------------------------------------------------------------------------
void ProcessCmdBody(const char *pFullPath, const char *pBodyPartName, float flScale, bool bFlipTriangles,
                    bool bQuadSubd) {
    s_bodypart_t &bodypart = g_studiomdl.body.AddBodyPart();
    Q_strncpy(bodypart.name, pBodyPartName, sizeof(bodypart.name));

    s_model_t *pmodel = c_allocate<s_model_t>();
    const int imodel = g_studiomdl.body.AddModel(pmodel);
    bodypart.pmodel.push_back(pmodel);

    ProcessOptionStudio(pmodel, pFullPath, flScale, bFlipTriangles, bQuadSubd);

    // Body command should add any flex commands in the source loaded
    PostProcessSource(pmodel->source, imodel);
}


//-----------------------------------------------------------------------------
// Purpose: Handle the $boneflexdriver command
// QC: $boneflexdriver <bone name> <tx|ty|tz> <flex controller name> <min> <max>
//-----------------------------------------------------------------------------
void Cmd_BoneFlexDriver(ScriptTokenizer &qc) {
    // Find or create the DmeBoneFlexDriverList
    CDmeBoneFlexDriverList *pDmeBoneFlexDriverList = GetElement<CDmeBoneFlexDriverList>(
        g_studiomdl.flex.hDmeBoneFlexDriverList);
    if (!pDmeBoneFlexDriverList) {
        pDmeBoneFlexDriverList = CreateElement<CDmeBoneFlexDriverList>("boneDriverFlexList", DMFILEID_INVALID);
        if (pDmeBoneFlexDriverList) {
            g_studiomdl.flex.hDmeBoneFlexDriverList = pDmeBoneFlexDriverList->GetHandle();
        }
    }

    if (!pDmeBoneFlexDriverList) {
        qc.Error("{}: Couldn't find or create DmeBoneDriverFlexList\n", "$boneflexdriver");
        return;
    }

    // <bone name>
    qc.ExpectToken();
    CDmeBoneFlexDriver *pDmeBoneFlexDriver = pDmeBoneFlexDriverList->FindOrCreateBoneFlexDriver(qc.token);
    if (!pDmeBoneFlexDriver) {
        qc.Error("{}: Couldn't find or create DmeBoneFlexDriver for bone \"{}\"\n", "$boneflexdriver", qc.token);
        return;
    }

    // <tx|ty|tz|rx|ry|rz>
    qc.ExpectToken();
    const char *ppszComponentTypeList[] = {"tx", "ty", "tz"};
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
    CDmeBoneFlexDriverControl *pDmeBoneFlexDriverControl = pDmeBoneFlexDriver->FindOrCreateControl(qc.token);
    if (!pDmeBoneFlexDriverControl) {
        qc.Error("{}: Couldn't find or create DmeBoneFlexDriverControl for bone \"{}\"\n", "$boneflexdriver",
                 qc.token);
        return;
    }

    pDmeBoneFlexDriverControl->m_nBoneComponent = nBoneComponent;

    // <min>
    qc.ExpectToken();
    pDmeBoneFlexDriverControl->m_flMin = qc.TokenFloat();

    // <max>
    qc.ExpectToken();
    pDmeBoneFlexDriverControl->m_flMax = qc.TokenFloat();
}

void Cmd_PoseParameter(ScriptTokenizer &qc) {
    if (std::ssize(g_studiomdl.anim.pose) >= MAXSTUDIOPOSEPARAM) {
        qc.Error("too many pose parameters (max {})\n", MAXSTUDIOPOSEPARAM);
    }

    qc.ExpectToken(); //[wills] unless you want a pose parameter named "poseparameter", should probably GetToken here

    int i = LookupPoseParameter(qc.token);
    strcpyn(g_studiomdl.anim.pose[i].name, qc.token);

    if (qc.TokenAvailable()) {
        // min
        qc.ExpectFloatToken(g_studiomdl.anim.pose[i].min);
    }

    if (qc.TokenAvailable()) {
        // max
        qc.ExpectFloatToken(g_studiomdl.anim.pose[i].max);
    }

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "wrap")) {
            g_studiomdl.anim.pose[i].flags |= StudioSequenceFlags::Looping;
            g_studiomdl.anim.pose[i].loop = g_studiomdl.anim.pose[i].max - g_studiomdl.anim.pose[i].min;
        }
        else if (vstd::iequals(qc.token, "loop")) {
            g_studiomdl.anim.pose[i].flags |= StudioSequenceFlags::Looping;
            qc.ExpectFloatToken(g_studiomdl.anim.pose[i].loop);
        }
    }
}

void Cmd_OverrideMaterial(ScriptTokenizer &qc) {
    char to[256];

    qc.ExpectToken();
    strcpy(to, qc.token);

    Msg("$overridematerial is replacing ALL material references with %s.\n", to);

    int i;
    for (i = 0; i < g_studiomdl.materials.textures.size(); i++) {
        strcpy(g_studiomdl.materials.textures[i].name, to);
    }
}

void Cmd_RenameMaterial(ScriptTokenizer &qc) {
    char from[256];
    char to[256];

    qc.ExpectToken();
    strcpy(from, qc.token);

    qc.ExpectToken();
    strcpy(to, qc.token);

    int i;
    for (i = 0; i < g_studiomdl.materials.textures.size(); i++) {
        if (vstd::iequals(g_studiomdl.materials.textures[i].name, from)) {
            strcpy(g_studiomdl.materials.textures[i].name, to);
            return;
        }
    }

    for (i = 0; i < g_studiomdl.materials.textures.size(); i++) {
        if (vstd::ifind(g_studiomdl.materials.textures[i].name, from) != std::string_view::npos) {
            Msg("$renamematerial fell back to partial match: Replacing %s with %s.\n",
                g_studiomdl.materials.textures[i].name.c_str(), to);
            strcpy(g_studiomdl.materials.textures[i].name, to);
            return;
        }
    }

    qc.Error("unknown material \"{}\" in rename\n", from);
}

void Cmd_Eyeposition(ScriptTokenizer &qc) {
    // rotate points into frame of reference so g_studiomdl.body.model points down the positive x
    // axis
    //	FIXME: these coords are bogus
    qc.ExpectFloatToken(g_studiomdl.header.eyeposition[1]);

    qc.ExpectToken();
    g_studiomdl.header.eyeposition[0] = -qc.TokenFloat();

    qc.ExpectFloatToken(g_studiomdl.header.eyeposition[2]);
}

void Cmd_MaxEyeDeflection(ScriptTokenizer &qc) {
    qc.ExpectToken();
    g_studiomdl.header.flMaxEyeDeflection = cosf(qc.TokenFloat() * M_PI / 180.0f);
}

//-----------------------------------------------------------------------------
// Cmd_AddSearchDir: add the custom defined path to an array that we will add to the search paths
//-----------------------------------------------------------------------------
void Cmd_AddSearchDir(ScriptTokenizer &qc) {
    qc.ExpectToken();
    if (!g_studiomdl.options.quiet) {
        std::print("New search path: {}\n", qc.token);
    }
    CmdLib_AddNewSearchPath(qc.token);
}

//-----------------------------------------------------------------------------
// Cmd_Illumposition
//-----------------------------------------------------------------------------
void Cmd_Illumposition(ScriptTokenizer &qc) {
    qc.ExpectFloatToken("illumposition <x>", g_studiomdl.header.illumposition[0]);

    qc.ExpectFloatToken("illumposition <y>", g_studiomdl.header.illumposition[1]);

    qc.ExpectFloatToken("illumposition <z>", g_studiomdl.header.illumposition[2]);

    if (qc.TokenAvailable()) {
        qc.ExpectToken();

        auto &attachment = g_studiomdl.bones.attachments.emplace_back(s_attachment_t{
            .name = "__illumPosition",
            .bonename = qc.token,
            .type = AttachmentType::Rigid,
        });

        AngleMatrix(QAngle(0, 0, 0), g_studiomdl.header.illumposition, attachment.local);

        g_studiomdl.header.illumpositionattachment = g_studiomdl.bones.attachments.size();
        // No -1 because 0 is used as no illumposiotion attachment
    }
    else {
        g_studiomdl.header.illumpositionattachment = 0;

        // rotate points into frame of reference so
        // g_studiomdl.body.model points down the positive x axis
        // FIXME: these coords are bogus
        float flTemp = g_studiomdl.header.illumposition[0];
        g_studiomdl.header.illumposition[0] = -g_studiomdl.header.illumposition[1];
        g_studiomdl.header.illumposition[1] = flTemp;
    }

    g_studiomdl.header.illumpositionset = true;
}

//-----------------------------------------------------------------------------
// Parse Cmd_Modelname
//-----------------------------------------------------------------------------
static void Cmd_Modelname(ScriptTokenizer &qc) {
    qc.ExpectToken("<path>");

    if (qc.token[0] == '/' || qc.token[0] == '\\') {
        MdlWarning("$modelname key has slash as first character. Removing.\n");
        ProcessModelName(&qc.token[1]);
    }
    else {
        ProcessModelName(qc.token);
    }
}

static void Cmd_InternalName(ScriptTokenizer &qc) {
    qc.ExpectToken("<path>");
    Q_strncpy(g_studiomdl.paths.internalName, qc.token, sizeof(g_studiomdl.paths.internalName));
}


static void Cmd_PreserveTriangleOrder(ScriptTokenizer &qc) {
    g_studiomdl.options.preserveTriangleOrder = true;
}

static void Cmd_Autocenter(ScriptTokenizer &qc) {
    g_studiomdl.header.centerstaticprop = true;
}


//-----------------------------------------------------------------------------
// Parse + process the studio options from a .qc file
//-----------------------------------------------------------------------------
void Option_Studio(ScriptTokenizer &qc, s_model_t *pmodel) {
    CDmeSourceSkin *pSourceSkin = CreateElement<CDmeSourceSkin>("", DMFILEID_INVALID);

    // Set defaults
    pSourceSkin->m_flScale = g_studiomdl.script.defaultscale;
    pSourceSkin->m_bQuadSubd = false;
    pSourceSkin->m_bFlipTriangles = false;

    if (ParseOptionStudio(qc, pSourceSkin)) {
        ProcessOptionStudio(pmodel, pSourceSkin->GetRelativeFileName(), pSourceSkin->m_flScale,
                            pSourceSkin->m_bFlipTriangles, pSourceSkin->m_bQuadSubd);
    }
    DestroyElement(pSourceSkin);
}

// Adds a "blank" model to the bodypart
int Option_Blank(s_bodypart_t &bodypart) {
    s_model_t *pmodel = c_allocate<s_model_t>();
    g_studiomdl.body.AddModel(pmodel);

    auto *source = c_allocate<s_source_t>();
    g_studiomdl.anim.sources.emplace_back(source);

    pmodel->source = source;
    bodypart.pmodel.push_back(pmodel);

    strcpyn(pmodel->name, "blank");
    return 0;
}

void Cmd_Bodygroup(ScriptTokenizer &qc) {
    vstd::FunctionTimingRecorder _("Parse");
    int is_started = 0;

    qc.ExpectToken("bodygroup <name>");

    s_bodypart_t &bodypart = g_studiomdl.body.AddBodyPart();
    strcpyn(bodypart.name, qc.token);

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
            s_model_t *pmodel = c_allocate<s_model_t>();
            const int imodel = g_studiomdl.body.AddModel(pmodel);
            bodypart.pmodel.push_back(pmodel);

            Option_Studio(qc, pmodel);

            // Body command should add any flex commands in the source loaded
            PostProcessSource(pmodel->source, imodel);
        }
        else if (vstd::iequals("blank", qc.token)) {
            Option_Blank(bodypart);
        }
        else {
            qc.Error("unknown bodygroup option: \"{}\"\n", qc.token);
        }
    } while (1);
}

void Cmd_AppendBlankBodygroup(ScriptTokenizer &qc) {
    // stick a blank bodygroup on the end of the current part
    if (g_studiomdl.body.bodypart.empty()) {
        qc.Error("$appendblankbodygroup: no bodygroup to append to\n");
    }
    Option_Blank(g_studiomdl.body.bodypart.back());
}

void Cmd_BodygroupPreset(ScriptTokenizer &qc) {
    qc.ExpectToken("preset <name>");

    // make sure this name is unused
    for (size_t i = 0; i < g_studiomdl.body.bodygrouppresets.size(); i++) {
        if (!V_strcmp(qc.token, g_studiomdl.body.bodygrouppresets[i].name)) {
            qc.Error("Error: bodygroup preset \"{}\" already exists.\n", qc.token);
            return;
        }
    }

    s_bodygrouppreset_t newpreset;
    V_strcpy_safe(newpreset.name, qc.token);

    int nAccumValue = 0;
    int nAccumMask = 0;

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
            for (const s_bodypart_t &bodypart: g_studiomdl.body.bodypart) {
                if (!V_strcmp(bodypart.name, qc.token)) {
                    const int numModels = static_cast<int>(bodypart.pmodel.size());
                    int iValue;
                    qc.ExpectIntToken("bodygroup value", iValue);
                    if (iValue >= 0 && iValue < numModels) {
                        int iCurrentVal = (nAccumValue / bodypart.base) % numModels;
                        nAccumValue = (nAccumValue - (iCurrentVal * bodypart.base) + (iValue * bodypart.base));

                        int iCurrentMask = (nAccumMask / bodypart.base) % numModels;
                        nAccumMask = (nAccumMask - (iCurrentMask * bodypart.base) + (1 * bodypart.base));
                    }
                    else {
                        qc.Error(
                            "Error: can't assign value \"{}\" to bodygroup preset \"{}\" (out of available range).\n",
                            iValue, newpreset.name);
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

    newpreset.iValue = nAccumValue;
    newpreset.iMask = nAccumMask;

    //Msg( "Built bodygroup preset: %s, value: %i, mask:%i\n", newpreset.name, newpreset.iValue, newpreset.iMask );

    g_studiomdl.body.bodygrouppresets.push_back(newpreset);
}

//-----------------------------------------------------------------------------
// Parse the body command from a .qc file
//-----------------------------------------------------------------------------
void Cmd_Body(ScriptTokenizer &qc) {
    vstd::FunctionTimingRecorder _("Parse");
    qc.ExpectToken("body <name>");

    CDmeSourceSkin *pSourceSkin = CreateElement<CDmeSourceSkin>("", DMFILEID_INVALID);

    // Set defaults
    pSourceSkin->m_flScale = g_studiomdl.script.defaultscale;

    pSourceSkin->m_SkinName = qc.token;
    if (ParseOptionStudio(qc, pSourceSkin)) {
        ProcessCmdBody(pSourceSkin->GetRelativeFileName(), pSourceSkin->m_SkinName.Get(),
                       pSourceSkin->m_flScale, pSourceSkin->m_bFlipTriangles, pSourceSkin->m_bQuadSubd);
    }
    DestroyElement(pSourceSkin);
}

//-----------------------------------------------------------------------------
// Add all attachments from the source to the global attachment list
// stopping when the g_studiomdl.attachment array is full.  Duplicate attachments
// will be purged later after they are linked to bones
//-----------------------------------------------------------------------------
void AddBodyAttachments(s_source_t *pSource) {
    for (int i = 0; i < pSource->m_Attachments.Count(); ++i) {
        if (g_studiomdl.bones.attachments.size() >= g_studiomdl.bones.attachments.capacity()) {
            MdlWarning("Too Many Attachments (Max {}), Ignoring Attachment {}:{}\n",
                       g_studiomdl.bones.attachments.capacity(), pSource->filename,
                       pSource->m_Attachments[i].name);
            continue;;
        }
        g_studiomdl.bones.attachments.emplace_back(pSource->m_Attachments[i]);
    }
}

//-----------------------------------------------------------------------------
// Adds flex controller data to a particular source
//-----------------------------------------------------------------------------
void AddFlexControllers(s_source_t *pSource) {
    CUtlVector<int> &r2s = pSource->m_rawIndexToRemapSourceIndex;
    CUtlVector<int> &r2l = pSource->m_rawIndexToRemapLocalIndex;
    CUtlVector<int> &l2i = pSource->m_leftRemapIndexToGlobalFlexControllIndex;
    CUtlVector<int> &r2i = pSource->m_rightRemapIndexToGlobalFlexControllIndex;

    // Number of Raw controls in this source
    const int nRawControlCount = pSource->m_CombinationControls.Count();
    // Initialize rawToRemapIndices
    r2s.SetSize(nRawControlCount);
    r2l.SetSize(nRawControlCount);
    for (int i = 0; i < nRawControlCount; ++i) {
        r2s[i] = -1;
        r2l[i] = -1;
    }

    // Number of Remapped Controls in this source
    const int nRemappedControlCount = pSource->m_FlexControllerRemaps.Count();
    l2i.SetSize(nRemappedControlCount);
    r2i.SetSize(nRemappedControlCount);

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
            if ((g_studiomdl.flex.flexcontrollers.size() + 1) >= g_studiomdl.flex.flexcontrollers.capacity())
                MdlError("Line {}: Too many flex controllers, max {}, cannot add split control {} from source {}",
                         g_studiomdl.input.iLinecount, g_studiomdl.flex.flexcontrollers.capacity(),
                         remapControl.m_Name.Get(),
                         pSource->filename);


            int nLen = remapControl.m_Name.Length();
            char *pTemp = (char *) _alloca(nLen + 7); // 'left_' && 'right_'

            memcpy(pTemp + 6, remapControl.m_Name.Get(), nLen + 1);
            memcpy(pTemp, "right_", 6);
            pTemp[nLen + 6] = '\0';

            r2i[i] = remapControl.m_RightIndex = g_studiomdl.flex.flexcontrollers.size();
            {
                auto &pController = g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                    .name = pTemp,
                    .type = pTemp
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

                memcpy(pTemp + 5, remapControl.m_Name.Get(), nLen + 1);
                memcpy(pTemp, "left_", 5);
                pTemp[nLen + 5] = '\0';
            }

            l2i[i] = remapControl.m_LeftIndex = g_studiomdl.flex.flexcontrollers.size();
            {
                auto &pController = g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                    .name = pTemp,
                    .type = pTemp
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
            if (g_studiomdl.flex.flexcontrollers.size() >= g_studiomdl.flex.flexcontrollers.capacity())
                MdlError("Line {}: Too many flex controllers, max {}, cannot add control {} from source {}",
                         g_studiomdl.input.iLinecount, g_studiomdl.flex.flexcontrollers.capacity(),
                         remapControl.m_Name.Get(),
                         pSource->filename);

            r2i[i] = l2i[i] = remapControl.m_Index = g_studiomdl.flex.flexcontrollers.size();
            s_flexcontroller_t &pController = g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = remapControl.m_Name.Get(),
                .type = remapControl.m_Name.Get()
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
            if (g_studiomdl.flex.flexcontrollers.size() >= g_studiomdl.flex.flexcontrollers.capacity())
                MdlError(
                    "Line {}: Too many flex controllers, max {}, cannot add value control for nWay {} from source {}",
                    g_studiomdl.input.iLinecount, g_studiomdl.flex.flexcontrollers.capacity(), remapControl.m_Name.Get(),
                    pSource->filename);

            remapControl.m_MultiIndex = g_studiomdl.flex.flexcontrollers.size();

            const int nLen = remapControl.m_Name.Length();
            char *pTemp = (char *) _alloca(nLen + 6 + 1); // 'multi_' + 1 for the NULL

            memcpy(pTemp, "multi_", 6);
            memcpy(pTemp + 6, remapControl.m_Name.Get(), nLen + 1);
            pTemp[nLen + 6] = '\0';

            g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = pTemp,
                .type = pTemp,
                .min = -1.0f,
                .max = 1.0f
            });
        }

        if (remapControl.m_RemapType == Eyelid) {
            // Make a blink controller

            if (g_studiomdl.flex.flexcontrollers.size() >= g_studiomdl.flex.flexcontrollers.capacity())
                MdlError(
                    "Line {}: Too many flex controllers, max {}, cannot add value control for nWay {} from source {}",
                    g_studiomdl.input.iLinecount, g_studiomdl.flex.flexcontrollers.capacity(), remapControl.m_Name.Get(),
                    pSource->filename);

            remapControl.m_BlinkController = g_studiomdl.flex.flexcontrollers.size();
            g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
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
void AddBodyFlexRemaps(s_source_t *pSource) {
    int nCount = pSource->m_FlexControllerRemaps.Count();
    for (int i = 0; i < nCount; ++i) {
        g_studiomdl.flex.FlexControllerRemap.emplace_back();
        s_flexcontrollerremap_t &remap = g_studiomdl.flex.FlexControllerRemap.back();
        remap = pSource->m_FlexControllerRemaps[i];
    }
}

//-----------------------------------------------------------------------------
// Process a body command
//-----------------------------------------------------------------------------
void AddBodyFlexData(s_source_t *pSource, int imodel, bool bAddControllers) {
    pSource->m_nKeyStartIndex = g_studiomdl.flex.flexkeys.size();

    // Add flex keys
    int nCount = pSource->m_FlexKeys.Count();
    for (int i = 0; i < nCount; ++i) {
        s_flexkey_t &key = pSource->m_FlexKeys[i];

        if (g_studiomdl.flex.flexkeys.size() >= g_studiomdl.flex.flexkeys.capacity())
            MdlError("Line {}: Too many flex keys, max {}, cannot add flexKey {} from source {}",
                     g_studiomdl.input.iLinecount, MAXSTUDIOFLEXKEYS, key.animationname,
                     pSource->filename);

        auto &new_flex = g_studiomdl.flex.flexkeys.emplace(key);
        new_flex.imodel = imodel;

        // flexpair was set up in AddFlexKey
        if (key.flexpair) {
            char mod[512];
            Q_snprintf(mod, sizeof(mod), "%sL", key.animationname.c_str());
            new_flex.flexdesc = Add_Flexdesc(mod);
            Q_snprintf(mod, sizeof(mod), "%sR", key.animationname.c_str());
            new_flex.flexpair = Add_Flexdesc(mod);
        }
        else {
            new_flex.flexdesc = Add_Flexdesc(key.animationname);
            new_flex.flexpair = 0;
        }
    }

    // Flex controllers and their remaps are global: pieces of a split model (ClampMaxVerticesPerModel) share the
    // ones that were added for the original source and must not add them again.
    if (bAddControllers) {
        AddFlexControllers(pSource);

        AddBodyFlexRemaps(pSource);
    }
}


void Option_Eyeball(ScriptTokenizer &qc, s_model_t *pmodel) {
    Vector tmp;
    int i, j;
    vstd::FixedString<256> szMeshMaterial;

    s_eyeball_t &eyeball = pmodel->eyeballs.emplace_back();

    // name
    qc.ExpectStringToken(eyeball.name);

    // bone name
    qc.ExpectToken();
    for (i = 0; i < pmodel->source->numbones; i++) {
        if (vstd::iequals(pmodel->source->localBone[i].name, qc.token)) {
            eyeball.bone = i;
            break;
        }
    }
    if (!g_studiomdl.options.createMakefile && i >= pmodel->source->numbones) {
        qc.Error("unknown eyeball bone \"{}\"\n", qc.token);
    }

    // X
    qc.ExpectFloatToken(tmp[0]);

    // Y
    qc.ExpectFloatToken(tmp[1]);

    // Z
    qc.ExpectFloatToken(tmp[2]);

    // mesh material
    qc.ExpectStringToken(szMeshMaterial);
    int mesh_material = UseTextureAsMaterial(LookupTexture(szMeshMaterial));

    // diameter
    qc.ExpectToken();
    eyeball.radius = qc.TokenFloat() / 2.0;

    // Z angle offset
    qc.ExpectToken();
    eyeball.zoffset = tan(DEG2RAD(qc.TokenFloat()));

    // iris material (no longer used, but we need to remove the token)
    qc.ExpectToken();

    // pupil scale
    qc.ExpectToken();
    eyeball.iris_scale = 1.0 / qc.TokenFloat();

    VectorCopy(tmp, eyeball.org);

    for (i = 0; i < pmodel->source->meshes.size(); i++) {
        j = pmodel->source->meshes[i].material;

        if (j == mesh_material) {
            eyeball.mesh = i; // FIXME: should this be pre-adjusted?
            break;
        }
    }

    if (!g_studiomdl.options.createMakefile && i >= pmodel->source->meshes.size()) {
        qc.Error("can't find eyeball texture \"{}\" on model\n", szMeshMaterial);
    }

    // translate eyeball into bone space
    VectorITransform(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.org);

    matrix3x4_t vtmp;
    AngleMatrix(g_studiomdl.script.defaultrotation, vtmp);

    VectorIRotate(Vector(0, 0, 1), vtmp, tmp);
    VectorIRotate(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.up);

    VectorIRotate(Vector(1, 0, 0), vtmp, tmp);
    VectorIRotate(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.forward);

    // these get overwritten by "eyelid" data
    eyeball.upperlidflexdesc = -1;
    eyeball.lowerlidflexdesc = -1;
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
void Option_VertexCacheAnimationFile(char *pszVtaFile, int nModelIndex) {
    if (g_studiomdl.flex.flexkeys.size() > 0) {
        MdlError("{}: Flexes already defined.  vcafile can be only flex option in $model block\n", __FUNCTION__);
        return;
    }

    s_source_t *pSource = g_studiomdl.body.model[nModelIndex]->source;
    s_source_t *pVtaSource = Load_Source(pszVtaFile, "vta");

    if (pVtaSource->m_Animations.Count() <= 0) {
        MdlError("{}: No animations in VertexCacheAnimationFile \"{}\"\n", __FUNCTION__, pszVtaFile);
        return;
    }

    {
        g_studiomdl.flex.flexkeys.emplace(s_flexkey_t{
            .flexdesc = Add_Flexdesc("default"),
            .flexpair = 0,
            .source = pVtaSource,
            .animationname = pVtaSource->m_Animations[0].animationname,
            .imodel = nModelIndex,
            .frame = 0,
            .target0 = 0.0,
            .target1 = 1.0,
            .target2 = 10,
            .target3 = 11,
            .split = 0,
            .decay = 0.0
        });
    }

    CFmtStr sTmp;

    const int nActualFrameCount = pVtaSource->m_Animations.Head().numframes - 1;

    for (int i = 0; i < nActualFrameCount; ++i) {
        sTmp.sprintf("f%d", i);

        {
            g_studiomdl.flex.flexkeys.emplace(s_flexkey_t{
                .flexdesc = Add_Flexdesc(sTmp.Access()),
                .flexpair = 0,
                .source = pVtaSource,
                .animationname = pVtaSource->m_Animations[0].animationname,
                .imodel = nModelIndex,
                .frame = (i + 1),
                .target0 = 0.0,
                .target1 = 1.0,
                .target2 = 10,
                .target3 = 11,
                .split = 0,
                .decay = 0.0,
            });
        }
    }

    s_flexcontrollerremap_t &flexRemap = pSource->m_FlexControllerRemaps[pSource->m_FlexControllerRemaps.AddToTail()];

    flexRemap.m_RemapType = Nway;
    flexRemap.m_bIsStereo = false;
    flexRemap.m_Index = -1; // Don't know this right now
    flexRemap.m_LeftIndex = -1; // Don't know this right now
    flexRemap.m_RightIndex = -1; // Don't know this right now
    flexRemap.m_MultiIndex = -1; // Don't know this right now
    flexRemap.m_EyesUpDownFlexController = -1;
    flexRemap.m_BlinkController = -1;

    char szBuf[MAX_PATH];
    V_FileBase(pVtaSource->filename.c_str(), szBuf, ARRAYSIZE(szBuf));
    flexRemap.m_Name = szBuf;

    for (int i = 0; i < nActualFrameCount; ++i) {
        sTmp.sprintf("f%d", i);
        flexRemap.m_RawControls.emplace_back(sTmp.Access());
    }

    for (int i = 0; i < flexRemap.m_RawControls.size(); ++i) {
        int nFlexKey = -1;
        for (int j = 0; j < g_studiomdl.flex.flexkeys.size(); ++j) {
            if (vstd::iequals(g_studiomdl.flex.flexdescs[g_studiomdl.flex.flexkeys[j].flexdesc].FACS, flexRemap.m_RawControls[i].Get())) {
                nFlexKey = j;
                break;
            }
        }

        if (nFlexKey < 0) {
            MdlError("{} Cannot find flex to group \"{}\"\n", __FUNCTION__, flexRemap.m_RawControls[i].Get());
            pSource->m_FlexControllerRemaps.RemoveMultipleFromTail(1);
            return;
        }

        s_combinationcontrol_t &combinationControl = pSource->m_CombinationControls[pSource->m_CombinationControls.
            AddToTail()];
        V_strncpy(combinationControl.name, flexRemap.m_RawControls[i].Get(), ARRAYSIZE(combinationControl.name));

        s_combinationrule_t &combinationRule = pSource->m_CombinationRules[pSource->m_CombinationRules.AddToTail()];
        combinationRule.m_nFlex = nFlexKey;
        combinationRule.m_Combination.AddToTail(nFlexKey - 1);
    }

    AddFlexControllers(pSource);

    AddBodyFlexRemaps(pSource);
}


void Option_Spherenormals(ScriptTokenizer &qc, s_source_t *psource) {
    Vector pos;
    int i, j;
    int mesh_material;
    char szMeshMaterial[256];

    // mesh material
    qc.ExpectStringToken(szMeshMaterial);
    mesh_material = UseTextureAsMaterial(LookupTexture(qc.token));

    // X
    qc.ExpectFloatToken(pos[0]);

    // Y
    qc.ExpectFloatToken(pos[1]);

    // Z
    qc.ExpectFloatToken(pos[2]);

    for (i = 0; i < psource->meshes.size(); i++) {
        const s_mesh_t &mesh = psource->meshes[i];

        if (mesh.material == mesh_material) {
            s_vertexinfo_t *vertex = &psource->vertex[mesh.vertexoffset];

            for (int k = 0; k < mesh.numvertices; k++) {
                Vector n = vertex[k].position - pos;
                VectorNormalize(n);
                if (DotProduct(n, vertex[k].normal) < 0.0) {
                    vertex[k].normal = -1 * n;
                }
                else {
                    vertex[k].normal = n;
                }
#if 0
                vertex[k].normal[0] += 0.5f * (2.0f * ((float) rand()) / (float) VALVE_RAND_MAX) - 1.0f;
                vertex[k].normal[1] += 0.5f * (2.0f * ((float) rand()) / (float) VALVE_RAND_MAX) - 1.0f;
                vertex[k].normal[2] += 0.5f * (2.0f * ((float) rand()) / (float) VALVE_RAND_MAX) - 1.0f;
                VectorNormalize(vertex[k].normal);
#endif
            }
            break;
        }
    }

    if (i >= psource->meshes.size()) {
        qc.Error("can't find spherenormal texture \"{}\" on model\n", szMeshMaterial);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Returns an s_sourceanim_t * from the specified s_source_t *
//          that matches the specified animation name (case insensitive)
//          and is also a new style (i.e. DMX) vertex animation
//-----------------------------------------------------------------------------
const s_sourceanim_t *GetNewStyleSourceVertexAnim(s_source_t *pSource, const char *pszVertexAnimName) {
    for (int i = 0; i < pSource->m_Animations.Count(); ++i) {
        const s_sourceanim_t *pSourceAnim = &(pSource->m_Animations[i]);
        if (!pSourceAnim || !pSourceAnim->newStyleVertexAnimations)
            continue;

        if (vstd::iequals(pszVertexAnimName, pSourceAnim->animationname))
            return pSourceAnim;
    }

    return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose:   Handle the eyelid option using a DMX instead of a VTA source
// QC Syntax: dmxeyelid <upper|lower> <source> lowerer <delta> <-0.20 neutral F00 0.19 raiser F02 0.28 righteyeball righteye lefteyeball lefteye
// e.g: dmxeyelid upper "coach_model_merged.dmx" lowerer F01 -0.20 neutral F00 0.19 raiser F02 0.28 righteyeball righteye lefteyeball lefteye
//-----------------------------------------------------------------------------
void Option_DmxEyelid(ScriptTokenizer &qc, int imodel) {
    // upper | lower
    const char *pszType = nullptr;
    qc.ExpectToken();
    if (vstd::iequals("upper", qc.token)) {
        pszType = "upper";
    }
    else if (vstd::iequals("lower", qc.token)) {
        pszType = "lower";
    }
    else {
        qc.Error("$model dmxeyelid, expected one of \"upper\", \"lower\"");
        return;
    }

    // (exr)
    CUtlString sSourceFile;
    qc.ExpectToken();
    sSourceFile = qc.token;

    s_source_t *pSource = Load_Source(sSourceFile.Get(), "dmx");
    if (!pSource) {
        qc.Error("({}) : {}:  Cannot load source file \"{}\"\n", g_studiomdl.input.iLinecount,
                 g_studiomdl.input.szLine, sSourceFile.Get());
        return;
    }

    enum RightLeftType_t {
        kLeft = 0,
        kRight = 1,
        kRightLeftTypeCount = 2
    };

    struct EyelidData_t {
        int m_nFlexDesc[kRightLeftTypeCount];
        const s_sourceanim_t *m_pSourceAnim;
        float m_flTarget;
        const char *m_pszSuffix;
    };

    EyelidData_t eyelidData[3] =
    {
        {{-1, -1}, nullptr, 0.0f, "lowerer"},
        {{-1, -1}, nullptr, 0.0f, "neutral"},
        {{-1, -1}, nullptr, 0.0f, "raiser"}
    };

    CUtlString sRightEyeball;
    CUtlString sLeftEyeball;

    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        bool bTokenHandled = false;

        for (int i = 0; i < kEyelidTypeCount; ++i) {
            if (vstd::iequals(qc.token, eyelidData[i].m_pszSuffix)) {
                bTokenHandled = true;

                qc.ExpectToken();
                eyelidData[i].m_pSourceAnim = GetNewStyleSourceVertexAnim(pSource, qc.token);
                if (eyelidData[i].m_pSourceAnim == nullptr) {
                    qc.Error("({}) : {}:  No DMX vertex animation named \"{}\" in source \"{}\"\n",
                             g_studiomdl.input.iLinecount,
                             g_studiomdl.input.szLine, qc.token, sSourceFile.Get());
                    return;
                }

                // target
                qc.ExpectFloatToken(eyelidData[i].m_flTarget);

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

    // Add a flexdesc for <type>_right & <type>_left
    // Where <type> is "upper" or "lower"
    int nRightLeftBaseDesc[kRightLeftTypeCount] = {-1, -1};

    CUtlString sRightBaseDesc = pszType;
    sRightBaseDesc += "_right";
    nRightLeftBaseDesc[kRight] = Add_Flexdesc(sRightBaseDesc.Get());

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        CUtlString sRightLocalDesc = sRightBaseDesc;
        sRightLocalDesc += "_";
        sRightLocalDesc += eyelidData[i].m_pszSuffix;
        eyelidData[i].m_nFlexDesc[kRight] = Add_Flexdesc(sRightLocalDesc.Get());
    }

    CUtlString sLeftBaseDesc = pszType;
    sLeftBaseDesc += "_left";
    nRightLeftBaseDesc[kLeft] = Add_Flexdesc(sLeftBaseDesc.Get());

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        CUtlString sLeftLocalDesc = sLeftBaseDesc;
        sLeftLocalDesc += "_";
        sLeftLocalDesc += eyelidData[i].m_pszSuffix;
        eyelidData[i].m_nFlexDesc[kLeft] = Add_Flexdesc(sLeftLocalDesc.Get());
    }

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        s_flexkey_t *pFlexKey = &g_studiomdl.flex.flexkeys.emplace_back(s_flexkey_t{
            .flexdesc = nRightLeftBaseDesc[kLeft],
            .flexpair = nRightLeftBaseDesc[kRight],
            .source = pSource,
            .animationname = eyelidData[i].m_pSourceAnim->animationname,
            .imodel = imodel,
            .frame = 0,
            .split = 0.0f,
            .decay = 1.0
        });
        switch (i) {
            case kLowerer:
                pFlexKey->target0 = -11;
                pFlexKey->target1 = -10;
                pFlexKey->target2 = eyelidData[kLowerer].m_flTarget;
                pFlexKey->target3 = eyelidData[kNeutral].m_flTarget;
                break;
            case kNeutral:
                pFlexKey->target0 = eyelidData[kLowerer].m_flTarget;
                pFlexKey->target1 = eyelidData[kNeutral].m_flTarget;
                pFlexKey->target2 = eyelidData[kNeutral].m_flTarget;
                pFlexKey->target3 = eyelidData[kRaiser].m_flTarget;
                break;
            case kRaiser:
                pFlexKey->target0 = eyelidData[kNeutral].m_flTarget;
                pFlexKey->target1 = eyelidData[kRaiser].m_flTarget;
                pFlexKey->target2 = 10;
                pFlexKey->target3 = 11;
                break;
            default: {
                throw std::runtime_error("Invalid eyelid type");
            };
        }
    }

    bool bRightOk = false;
    bool bLeftOk = false;

    s_model_t *pModel = g_studiomdl.body.model[imodel];
    for (int i = 0; i < pModel->eyeballs.size(); ++i) {
        s_eyeball_t *pEyeball = &(pModel->eyeballs[i]);
        if (!pEyeball)
            continue;

        RightLeftType_t nRightLeftIndex = kRight;
        if (vstd::iequals(sRightEyeball, pEyeball->name)) {
            nRightLeftIndex = kRight;
            bRightOk = true;
        }
        else if (vstd::iequals(sLeftEyeball, pEyeball->name)) {
            nRightLeftIndex = kLeft;
            bLeftOk = true;
        }
        else {
            MdlWarning("Unknown Eyeball: {}\n", pEyeball->name);
            continue;
        }

        for (int j = 0; j < kEyelidTypeCount; ++j) {
            if (fabs(eyelidData[j].m_flTarget) > pEyeball->radius) {
                qc.Error("Eyelid \"{}\" {} {:.1f} out of range (+-{:.1f})\n", pszType, eyelidData[j].m_pszSuffix,
                         eyelidData[j].m_flTarget, pEyeball->radius);
            }
        }

        switch (*pszType) {
            case 'u': // upper
                pEyeball->upperlidflexdesc = nRightLeftBaseDesc[nRightLeftIndex];
                for (int j = 0; j < kEyelidTypeCount; ++j) {
                    pEyeball->upperflexdesc[j] = eyelidData[j].m_nFlexDesc[nRightLeftIndex];
                    pEyeball->uppertarget[j] = eyelidData[j].m_flTarget;
                }
                break;
            case 'l': // lower
                pEyeball->lowerlidflexdesc = nRightLeftBaseDesc[nRightLeftIndex];
                for (int j = 0; j < kEyelidTypeCount; ++j) {
                    pEyeball->lowerflexdesc[j] = eyelidData[j].m_nFlexDesc[nRightLeftIndex];
                    pEyeball->lowertarget[j] = eyelidData[j].m_flTarget;
                }
                break;
            default:
                Assert(0);
                break;
        }
    }

    if (!bRightOk) {
        qc.Error("Could not find right eye \"{}\"\n", sRightEyeball.Get());
    }

    if (!bLeftOk) {
        qc.Error("Could not find left eye \"{}\"\n", sRightEyeball.Get());
    }
}

int Option_Mouth(ScriptTokenizer &qc, s_model_t *pmodel) {
    // index
    qc.ExpectToken();
    int index = qc.TokenInt();
    // flex controller name
    qc.ExpectToken();
    g_studiomdl.flex.mouth[index].flexdesc = Add_Flexdesc(qc.token);

    // bone name
    qc.ExpectStringToken(g_studiomdl.flex.mouth[index].bonename);

    // vector
    qc.ExpectFloatToken(g_studiomdl.flex.mouth[index].forward[0]);
    qc.ExpectFloatToken(g_studiomdl.flex.mouth[index].forward[1]);
    qc.ExpectFloatToken(g_studiomdl.flex.mouth[index].forward[2]);
    return 0;
}

void Option_Flexcontroller(ScriptTokenizer &qc, s_model_t *pmodel) {
    char type[256];
    float range_min = 0.0f;
    float range_max = 1.0f;

    // g_flex
    qc.ExpectToken();
    strcpy(type, qc.token);

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "range")) {
            qc.ExpectFloatToken(range_min);

            qc.ExpectFloatToken(range_max);
        }
        else {
            if (g_studiomdl.flex.flexcontrollers.size() >= g_studiomdl.flex.flexcontrollers.capacity()) {
                qc.Error("Too many flex controllers, max {}\n", g_studiomdl.flex.flexcontrollers.capacity());
            }

            g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = qc.token,
                .type = type,
                .min = range_min,
                .max = range_max
            });
        }
    }

    // this needs to be per model.
}

void Option_NoAutoDMXRules(s_source_t *pSource) {
    // zero out the automatic flex controllers
    g_studiomdl.flex.flexcontrollers.clear();
    pSource->bNoAutoDMXRules = true;
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

void Option_Flex(ScriptTokenizer &qc, char *name, char *vtafile, int imodel, float pairsplit) {
    if (g_studiomdl.flex.flexkeys.size() >= MAXSTUDIOFLEXKEYS) {
        qc.Error("Too many flexes, max {}\n", MAXSTUDIOFLEXKEYS);
    }

    int flexdesc, flexpair;

    if (pairsplit != 0) {
        char mod[256];
        sprintf(mod, "%sR", name);
        flexdesc = Add_Flexdesc(mod);

        sprintf(mod, "%sL", name);
        flexpair = Add_Flexdesc(mod);
    }
    else {
        flexdesc = Add_Flexdesc(name);
        flexpair = 0;
    }

    // initialize
    auto &new_flexkey = g_studiomdl.flex.flexkeys.emplace(s_flexkey_t{
        .flexdesc = flexdesc,
        .flexpair = flexpair,
        .imodel = imodel,
        .target0 = 0.0,
        .target1 = 1.0,
        .target2 = 10,
        .target3 = 11,
        .split = pairsplit,
        .decay = 1.0,
    });

    const auto current_count = g_studiomdl.flex.flexkeys.size() - 1;

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "frame")) {
            qc.ExpectToken();

            new_flexkey.frame = qc.TokenInt();
        }
        else if (vstd::iequals(qc.token, "position")) {
            qc.ExpectFloatToken(new_flexkey.target1);
        }
        else if (vstd::iequals(qc.token, "split")) {
            qc.ExpectFloatToken(new_flexkey.split);
        }
        else if (vstd::iequals(qc.token, "decay")) {
            qc.ExpectFloatToken(new_flexkey.decay);
        }
        else {
            qc.Error("unknown option: {}", qc.token);
        }
    }

    if (current_count > 1) {
        if (g_studiomdl.flex.flexkeys[current_count - 1].flexdesc == new_flexkey.flexdesc) {
            g_studiomdl.flex.flexkeys[current_count - 1].target2 = g_studiomdl.flex.flexkeys[current_count - 1].target1;
            g_studiomdl.flex.flexkeys[current_count - 1].target3 = new_flexkey.target1;
            new_flexkey.target0 = g_studiomdl.flex.flexkeys[current_count - 1].target1;
        }
    }

    // link to source
    s_source_t *pSource = Load_Source(vtafile, "vta");
    new_flexkey.source = pSource;
    if (pSource->m_Animations.Count()) {
        new_flexkey.animationname = pSource->m_Animations[0].animationname;
    }
    else {
        new_flexkey.animationname[0] = 0;
    }
    // this needs to be per model.
}


void PrintFlexrule(s_flexrule_t *pRule) {
    std::print("{} = ", g_studiomdl.flex.flexdescs[pRule->flex].FACS);
    for (int i = 0; i < pRule->numops; i++) {
        switch (pRule->op[i].op) {
            case StudioOpcode::Const:
                std::print("{:f} ", pRule->op[i].d.value);
                break;
            case StudioOpcode::Fetch1:
                std::print("{} ", g_studiomdl.flex.flexcontrollers[pRule->op[i].d.index].name);
                break;
            case StudioOpcode::Fetch2:
                std::print("[{}] ", pRule->op[i].d.index);
                break;
            case StudioOpcode::Add:
                std::print("+ ");
                break;
            case StudioOpcode::Sub:
                std::print("- ");
                break;
            case StudioOpcode::Mul:
                std::print("* ");
                break;
            case StudioOpcode::Div:
                std::print("/ ");
                break;
            case StudioOpcode::Neg:
                std::print("neg ");
                break;
            case StudioOpcode::Max:
                std::print("max ");
                break;
            case StudioOpcode::Min:
                std::print("min ");
                break;
            case StudioOpcode::Comma:
                std::print(", ");
                break; // error
            case StudioOpcode::Open:
                std::print("( ");
                break; // error
            case StudioOpcode::Close:
                std::print(") ");
                break; // error
            case StudioOpcode::TwoWay0:
                std::print("2WAY_0 ");
                break;
            case StudioOpcode::TwoWay1:
                std::print("2WAY_1 ");
                break;
            case StudioOpcode::NWay:
                std::print("NWAY ");
                break;
            case StudioOpcode::Combo:
                std::print("COMBO ");
                break;
            case StudioOpcode::Dominate:
                std::print("DOMINATE ");
                break;
            case StudioOpcode::DmeLowerEyelid:
                std::print("DME_LOWER_EYELID ");
                break;
            case StudioOpcode::DmeUpperEyelid:
                std::print("DME_UPPER_EYELID ");
                break;
            default:
                std::print("err{} ", std::to_underlying(pRule->op[i].op));
                break;
        }
    }
    std::print("\n");
}


void Option_Flexrule(ScriptTokenizer &qc, s_model_t * /* pmodel */, const char *name) {
    int precedence[32];
    auto prec = [&precedence](StudioOpcode op) -> int & { return precedence[std::to_underlying(op)]; };
    prec(StudioOpcode::Const) = 0;
    prec(StudioOpcode::Fetch1) = 0;
    prec(StudioOpcode::Fetch2) = 0;
    prec(StudioOpcode::Add) = 1;
    prec(StudioOpcode::Sub) = 1;
    prec(StudioOpcode::Mul) = 2;
    prec(StudioOpcode::Div) = 2;
    prec(StudioOpcode::Neg) = 4;
    prec(StudioOpcode::Exp) = 3;
    prec(StudioOpcode::Open) = 0; // only used in token parsing
    prec(StudioOpcode::Close) = 0;
    prec(StudioOpcode::Comma) = 0;
    prec(StudioOpcode::Max) = 5;
    prec(StudioOpcode::Min) = 5;

    s_flexop_t stream[MAX_OPS] = {}; // operators have no operand: d must not carry stack garbage into the MDL
    int i = 0;
    s_flexop_t stack[MAX_OPS] = {};
    int j = 0;
    int k = 0;

    if (g_studiomdl.flex.flexrules.size() + 1 > g_studiomdl.flex.flexrules.capacity()) {
        qc.Error("Too many flex rules (max {})\n", g_studiomdl.flex.flexrules.capacity());
    }
    auto &pRule = g_studiomdl.flex.flexrules.emplace_back();

    int flexdesc;
    for (flexdesc = 0; flexdesc < g_studiomdl.flex.flexdescs.size(); flexdesc++) {
        if (vstd::iequals(name, g_studiomdl.flex.flexdescs[flexdesc].FACS)) {
            break;
        }
    }

    if (flexdesc >= g_studiomdl.flex.flexdescs.size()) {
        qc.Error("Rule for unknown flex {}\n", name);
    }

    pRule.flex = flexdesc;
    pRule.numops = 0;

    // =
    qc.ExpectToken();

    // parse all the tokens
    bool linecontinue = false;
    while (linecontinue || qc.TokenAvailable()) {
        if (!qc.GetExprToken(linecontinue)) {
            qc.Error("unexpected end of file in flex rule for {}\n", name);
        }

        linecontinue = false;

        if (qc.token[0] == '\\') {
            if (!qc.GetToken(false) || qc.token[0] != '\\') {
                qc.Error("unknown expression token '\\{}\n", qc.token);
            }
            linecontinue = true;
        }
        else if (qc.token[0] == '(') {
            stream[i++].op = StudioOpcode::Open;
        }
        else if (qc.token[0] == ')') {
            stream[i++].op = StudioOpcode::Close;
        }
        else if (qc.token[0] == '+') {
            stream[i++].op = StudioOpcode::Add;
        }
        else if (qc.token[0] == '-') {
            stream[i].op = StudioOpcode::Sub;
            if (i > 0) {
                switch (stream[i - 1].op) {
                    case StudioOpcode::Open:
                    case StudioOpcode::Add:
                    case StudioOpcode::Sub:
                    case StudioOpcode::Mul:
                    case StudioOpcode::Div:
                    case StudioOpcode::Comma:
                        // it's a unary if it's preceded by a "(+-*/,"?
                        stream[i].op = StudioOpcode::Neg;
                        break;
                }
            }
            i++;
        }
        else if (qc.token[0] == '*') {
            stream[i++].op = StudioOpcode::Mul;
        }
        else if (qc.token[0] == '/') {
            stream[i++].op = StudioOpcode::Div;
        }
        else if (V_isdigit(qc.token[0])) {
            stream[i].op = StudioOpcode::Const;
            stream[i++].d.value = qc.TokenFloat();
        }
        else if (qc.token[0] == ',') {
            stream[i++].op = StudioOpcode::Comma;
        }
        else if (vstd::iequals(qc.token, "max")) {
            stream[i++].op = StudioOpcode::Max;
        }
        else if (vstd::iequals(qc.token, "min")) {
            stream[i++].op = StudioOpcode::Min;
        }
        else {
            if (qc.token[0] == '%') {
                if (!qc.GetExprToken(false)) {
                    qc.Error("expected flex name after '%'\n");
                }

                for (k = 0; k < g_studiomdl.flex.flexdescs.size(); k++) {
                    if (vstd::iequals(qc.token, g_studiomdl.flex.flexdescs[k].FACS)) {
                        stream[i].op = StudioOpcode::Fetch2;
                        stream[i++].d.index = k;
                        break;
                    }
                }
                if (k >= g_studiomdl.flex.flexdescs.size()) {
                    qc.Error("unknown flex {}\n", qc.token);
                }
            }
            else {
                for (k = 0; k < g_studiomdl.flex.flexcontrollers.size(); k++) {
                    if (vstd::iequals(qc.token, g_studiomdl.flex.flexcontrollers[k].name)) {
                        stream[i].op = StudioOpcode::Fetch1;
                        stream[i++].d.index = k;
                        break;
                    }
                }
                if (k >= g_studiomdl.flex.flexcontrollers.size()) {
                    qc.Error("unknown controller {}\n", qc.token);
                }
            }
        }
    }

    if (i > MAX_OPS) {
        qc.Error("expression {} too complicated\n", g_studiomdl.flex.flexdescs[pRule.flex].FACS);
    }

    if (0) {
        std::print("{} = ", g_studiomdl.flex.flexdescs[pRule.flex].FACS);
        for (k = 0; k < i; k++) {
            switch (stream[k].op) {
                case StudioOpcode::Const:
                    std::print("{:f} ", stream[k].d.value);
                    break;
                case StudioOpcode::Fetch1:
                    std::print("{} ", g_studiomdl.flex.flexcontrollers[stream[k].d.index].name);
                    break;
                case StudioOpcode::Fetch2:
                    std::print("[{}] ", stream[k].d.index);
                    break;
                case StudioOpcode::Add:
                    std::print("+ ");
                    break;
                case StudioOpcode::Sub:
                    std::print("- ");
                    break;
                case StudioOpcode::Mul:
                    std::print("* ");
                    break;
                case StudioOpcode::Div:
                    std::print("/ ");
                    break;
                case StudioOpcode::Neg:
                    std::print("neg ");
                    break;
                case StudioOpcode::Max:
                    std::print("max ");
                    break;
                case StudioOpcode::Min:
                    std::print("min ");
                    break;
                case StudioOpcode::Comma:
                    std::print(", ");
                    break; // error
                case StudioOpcode::Open:
                    std::print("( ");
                    break; // error
                case StudioOpcode::Close:
                    std::print(") ");
                    break; // error
                default:
                    std::print("err{} ", std::to_underlying(stream[k].op));
                    break;
            }
        }
        std::print("\n");
        // exit(1);
    }

    j = 0;
    for (k = 0; k < i; k++) {
        if (j >= MAX_OPS) {
            qc.Error("expression {} too complicated\n", g_studiomdl.flex.flexdescs[pRule.flex].FACS);
        }
        switch (stream[k].op) {
            case StudioOpcode::Const:
            case StudioOpcode::Fetch1:
            case StudioOpcode::Fetch2:
                pRule.op[pRule.numops++] = stream[k];
                break;
            case StudioOpcode::Open:
                stack[j++] = stream[k];
                break;
            case StudioOpcode::Close:
                // pop all operators off of the stack until an open paren
                while (j > 0 && stack[j - 1].op != StudioOpcode::Open) {
                    pRule.op[pRule.numops++] = stack[j - 1];
                    j--;
                }
                if (j == 0) {
                    qc.Error("unmatched closed parentheses\n");
                }
                if (j > 0)
                    j--;
                break;
            case StudioOpcode::Comma:
                // pop all operators off of the stack until an open paren
                while (j > 0 && stack[j - 1].op != StudioOpcode::Open) {
                    pRule.op[pRule.numops++] = stack[j - 1];
                    j--;
                }
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
            case StudioOpcode::Add:
            case StudioOpcode::Sub:
            case StudioOpcode::Mul:
            case StudioOpcode::Div:
                // pop all operators off of the stack that have equal or higher precedence
                while (j > 0 && prec(stream[k].op) <= prec(stack[j - 1].op)) {
                    pRule.op[pRule.numops++] = stack[j - 1];
                    j--;
                }
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
            case StudioOpcode::Neg:
                if (stream[k + 1].op == StudioOpcode::Const) {
                    // change sign of constant, skip op
                    stream[k + 1].d.value = -stream[k + 1].d.value;
                }
                else {
                    // push operator onto the stack
                    stack[j++] = stream[k];
                }
                break;
            case StudioOpcode::Max:
            case StudioOpcode::Min:
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
        }
        if (pRule.numops >= MAX_OPS)
            qc.Error("expression for \"{}\" too complicated\n", g_studiomdl.flex.flexdescs[pRule.flex].FACS);
    }
    // pop all operators off of the stack
    while (j > 0) {
        pRule.op[pRule.numops++] = stack[j - 1];
        j--;
        if (pRule.numops >= MAX_OPS)
            qc.Error("expression for \"{}\" too complicated\n", g_studiomdl.flex.flexdescs[pRule.flex].FACS);
    }

    // reprocess the operands, eating commas for all functions
    int numCommas = 0;
    j = 0;
    for (k = 0; k < pRule.numops; k++) {
        switch (pRule.op[k].op) {
            case StudioOpcode::Max:
            case StudioOpcode::Min:
                if (pRule.op[j - 1].op != StudioOpcode::Comma) {
                    qc.Error("missing comma\n");
                }
                // eat the comma operator
                numCommas--;
                pRule.op[j - 1] = pRule.op[k];
                break;
            case StudioOpcode::Comma:
                numCommas++;
                pRule.op[j++] = pRule.op[k];
                break;
            default:
                pRule.op[j++] = pRule.op[k];
                break;
        }
    }
    pRule.numops = j;
    if (numCommas != 0) {
        qc.Error("too many comma's\n");
    }

    if (pRule.numops > MAX_OPS) {
        qc.Error("expression {} too complicated\n", g_studiomdl.flex.flexdescs[pRule.flex].FACS);
    }

    if (0) {
        // TODO REDxEYE: Should be a cli-flag maybe
        PrintFlexrule(&pRule);
    }
}

//-----------------------------------------------------------------------------
// Add A Body Flex Rule
//-----------------------------------------------------------------------------
void AddBodyFlexFetchRule(
    s_source_t *pSource,
    s_flexrule_t *pRule,
    int rawIndex,
    const CUtlVector<int> &pRawIndexToRemapSourceIndex,
    const CUtlVector<int> &pRawIndexToRemapLocalIndex,
    const CUtlVector<int> &pRemapSourceIndexToGlobalFlexControllerIndex) {
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
void AddBodyFlexRule(
    s_source_t *pSource,
    s_combinationrule_t &rule,
    int nFlexDesc,
    const CUtlVector<int> &pRawIndexToRemapSourceIndex,
    const CUtlVector<int> &pRawIndexToRemapLocalIndex,
    const CUtlVector<int> &pRemapSourceIndexToGlobalFlexControllerIndex) {
    if (g_studiomdl.flex.flexrules.size() >= g_studiomdl.flex.flexrules.capacity())
        MdlError("Line {}: Too many flex rules, max {}",
                 g_studiomdl.input.iLinecount, g_studiomdl.flex.flexrules.capacity());

    s_flexrule_t &pRule = g_studiomdl.flex.flexrules.emplace_back(s_flexrule_t{
        .flex = nFlexDesc,
    });

    // This will multiply the combination together
    const int nCombinationCount = rule.m_Combination.Count();
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
    int nDominators = rule.m_Dominators.Count();
    for (int j = 0; j < nDominators; ++j) {
        const int nFactorCount = rule.m_Dominators[j].Count();
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


void AddBodyFlexRules(s_source_t *pSource) {
    const int nRemapCount = pSource->m_FlexControllerRemaps.Count();
    for (int i = 0; i < nRemapCount; ++i) {
        s_flexcontrollerremap_t &remap = pSource->m_FlexControllerRemaps[i];
        if (remap.m_RemapType == Eyelid && !remap.m_EyesUpDownFlexName.IsEmpty()) {
            for (int j = 0; j < g_studiomdl.flex.flexcontrollers.size(); ++j) {
                if (!Q_strcmp(g_studiomdl.flex.flexcontrollers[j].name.c_str(), remap.m_EyesUpDownFlexName.Get())) {
                    Assert(remap.m_EyesUpDownFlexController == -1);
                    remap.m_EyesUpDownFlexController = j;
                    break;
                }
            }
        }
    }

    const int nCount = pSource->m_CombinationRules.Count();
    for (int i = 0; i < nCount; ++i) {
        s_combinationrule_t &rule = pSource->m_CombinationRules[i];
        s_flexkey_t &flexKey = g_studiomdl.flex.flexkeys[pSource->m_nKeyStartIndex + rule.m_nFlex];
        AddBodyFlexRule(pSource, rule, flexKey.flexdesc,
                        pSource->m_rawIndexToRemapSourceIndex, pSource->m_rawIndexToRemapLocalIndex,
                        pSource->m_leftRemapIndexToGlobalFlexControllIndex);
        if (flexKey.flexpair != 0) {
            AddBodyFlexRule(pSource, rule, flexKey.flexpair,
                            pSource->m_rawIndexToRemapSourceIndex, pSource->m_rawIndexToRemapLocalIndex,
                            pSource->m_rightRemapIndexToGlobalFlexControllIndex);
        }
    }
}

void Option_Eyelid(ScriptTokenizer &qc, int imodel) {
    char type[256];
    char vtafile[256];

    // type
    qc.ExpectStringToken(type);

    // source
    qc.ExpectStringToken(vtafile);

    int lowererframe = 0;
    int neutralframe = 0;
    int raiserframe = 0;
    float lowerertarget = 0.0f;
    float neutraltarget = 0.0f;
    float raisertarget = 0.0f;
    int lowererdesc = 0;
    int neutraldesc = 0;
    int raiserdesc = 0;
    int basedesc;
    float split = 0;
    char szEyeball[64] = {""};

    basedesc = g_studiomdl.flex.flexdescs.size();
    g_studiomdl.flex.flexdescs.emplace_back(type);

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        char localdesc[256];
        strcpy(localdesc, type);
        strcat(localdesc, "_");
        strcat(localdesc, qc.token);

        if (vstd::iequals(qc.token, "lowerer")) {
            qc.ExpectIntToken(lowererframe);
            qc.ExpectFloatToken(lowerertarget);
            lowererdesc = g_studiomdl.flex.flexdescs.size();
            g_studiomdl.flex.flexdescs.emplace_back(localdesc);
        }
        else if (vstd::iequals(qc.token, "neutral")) {
            qc.ExpectIntToken(neutralframe);
            qc.ExpectFloatToken(neutraltarget);
            neutraldesc = g_studiomdl.flex.flexdescs.size();
            g_studiomdl.flex.flexdescs.emplace_back(localdesc);
        }
        else if (vstd::iequals(qc.token, "raiser")) {
            qc.ExpectIntToken(raiserframe);
            qc.ExpectFloatToken(raisertarget);
            raiserdesc = g_studiomdl.flex.flexdescs.size();
            g_studiomdl.flex.flexdescs.emplace_back(localdesc);
        }
        else if (vstd::iequals(qc.token, "split")) {
            qc.ExpectFloatToken(split);
        }
        else if (vstd::iequals(qc.token, "eyeball")) {
            qc.ExpectToken();
            strcpy(szEyeball, qc.token);
        }
        else {
            qc.Error("unknown option: {}", qc.token);
        }
    }

    s_source_t *pSource = Load_Source(vtafile, "vta");
    auto &lower_flexkey = g_studiomdl.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = basedesc,
        .source = pSource,
        .imodel = imodel,
        .frame = lowererframe,
        .target0 = -11,
        .target1 = -10,
        .target2 = lowerertarget,
        .target3 = neutraltarget,
        .split = split,
        .decay = 0.0,
    });

    if (pSource->m_Animations.Count() > 0) {
        lower_flexkey.animationname = pSource->m_Animations[0].animationname;
    }
    else {
        lower_flexkey.animationname = "";
    }
    g_studiomdl.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = basedesc,
        .source = lower_flexkey.source,
        .animationname = lower_flexkey.animationname,
        .imodel = imodel,
        .frame = neutralframe,
        .target0 = lowerertarget,
        .target1 = neutraltarget,
        .target2 = neutraltarget,
        .target3 = raisertarget,
        .split = split,
        .decay = 0.0,
    });

    g_studiomdl.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = basedesc,
        .source = lower_flexkey.source,
        .animationname = lower_flexkey.animationname,
        .imodel = imodel,
        .frame = raiserframe,
        .target0 = neutraltarget,
        .target1 = raisertarget,
        .target2 = 10,
        .target3 = 11,
        .split = split,
        .decay = 0.0,
    });

    s_model_t *pmodel = g_studiomdl.body.model[imodel];
    for (int i = 0; i < pmodel->eyeballs.size(); i++) {
        s_eyeball_t *peyeball = &(pmodel->eyeballs[i]);

        if (szEyeball[0] != '\0') {
            if (!vstd::iequals(peyeball->name, szEyeball))
                continue;
        }

        if (fabs(lowerertarget) > peyeball->radius) {
            qc.Error("Eyelid \"{}\" lowerer out of range (+-{:.1f})\n", type, peyeball->radius);
        }
        if (fabs(neutraltarget) > peyeball->radius) {
            qc.Error("Eyelid \"{}\" neutral out of range (+-{:.1f})\n", type, peyeball->radius);
        }
        if (fabs(raisertarget) > peyeball->radius) {
            qc.Error("Eyelid \"{}\" raiser  out of range (+-{:.1f})\n", type, peyeball->radius);
        }

        switch (type[0]) {
            case 'u':
                peyeball->upperlidflexdesc = basedesc;
                peyeball->upperflexdesc[0] = lowererdesc;
                peyeball->uppertarget[0] = lowerertarget;
                peyeball->upperflexdesc[1] = neutraldesc;
                peyeball->uppertarget[1] = neutraltarget;
                peyeball->upperflexdesc[2] = raiserdesc;
                peyeball->uppertarget[2] = raisertarget;
                break;
            case 'l':
                peyeball->lowerlidflexdesc = basedesc;
                peyeball->lowerflexdesc[0] = lowererdesc;
                peyeball->lowertarget[0] = lowerertarget;
                peyeball->lowerflexdesc[1] = neutraldesc;
                peyeball->lowertarget[1] = neutraltarget;
                peyeball->lowerflexdesc[2] = raiserdesc;
                peyeball->lowertarget[2] = raisertarget;
                break;
        }
    }
}

void Cmd_Model(ScriptTokenizer &qc) {
    s_model_t *pmodel = c_allocate<s_model_t>();
    const int imodel = g_studiomdl.body.AddModel(pmodel);

    // name
    qc.ExpectStringToken("<name>", pmodel->name);

    // fake g_studiomdl.body.bodypart stuff
    s_bodypart_t &bodypart = g_studiomdl.body.AddBodyPart();
    strcpyn(bodypart.name, qc.token);
    bodypart.pmodel.push_back(pmodel);

    Option_Studio(qc, pmodel);

    if (pmodel->source) {
        // Body command should add any flex commands in the source loaded
        AddBodyFlexData(pmodel->source, imodel);
        AddBodyAttachments(pmodel->source);
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
            Option_Eyeball(qc, pmodel);
        }
        else if (vstd::iequals("eyelid", qc.token)) {
            Option_Eyelid(qc, imodel);
        }
        else if (vstd::iequals("dmxeyelid", qc.token)) {
            Option_DmxEyelid(qc, imodel);
        }
        else if (vstd::iequals("vcafile", qc.token)) {
            // vertex cache animation file
            qc.ExpectToken(); // file
            Option_VertexCacheAnimationFile(qc.token, imodel);
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
            Option_Flex(qc, FAC, vtafile, imodel, 0.0);
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
            Option_Flex(qc, FAC, vtafile, imodel, split);
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
            Option_Flex(qc, "default", vtafile, imodel, 0.0);
            g_studiomdl.flex.defaultflexkey = &g_studiomdl.flex.flexkeys.back();
        }
        else if (vstd::iequals("flexfile", qc.token)) {
            // file
            qc.ExpectToken();
            strcpy(vtafile, qc.token);
        }
        else if (vstd::iequals("localvar", qc.token)) {
            while (qc.TokenAvailable()) {
                qc.ExpectToken();
                Add_Flexdesc(qc.token);
            }
        }
        else if (vstd::iequals("mouth", qc.token)) {
            Option_Mouth(qc, pmodel);
        }
        else if (vstd::iequals("flexcontroller", qc.token)) {
            Option_Flexcontroller(qc, pmodel);
        }
        else if (qc.token[0] == '%') {
            Option_Flexrule(qc, pmodel, &qc.token[1]);
        }
        else if (vstd::iequals("attachment", qc.token)) {
            // 	Option_Attachment( pmodel );
        }
        else if (vstd::iequals(qc.token, "spherenormals")) {
            Option_Spherenormals(qc, pmodel->source);
        }
        else if (vstd::iequals(qc.token, "noautodmxrules")) {
            Option_NoAutoDMXRules(pmodel->source);
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
        AddBodyFlexRules(pmodel->source);
    }

}

void Cmd_FakeVTA(ScriptTokenizer &qc) {
    int depth = 0;

    qc.ExpectToken();

    auto psource = c_allocate<s_source_t>();
    g_studiomdl.anim.sources.emplace_back(psource);
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

            AppendVTAtoOBJ(psource, filename, frame);
        }
    }
}


StudioMotionFlags lookupControl(char *string) {
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


void Cmd_IKChain(ScriptTokenizer &qc) {
    qc.ExpectToken();

    int i;
    for (i = 0; i < std::ssize(g_studiomdl.anim.ikchain); i++) {
        if (vstd::iequals(qc.token, g_studiomdl.anim.ikchain[i].name)) {
            break;
        }
    }
    if (i < std::ssize(g_studiomdl.anim.ikchain)) {
        if (!g_studiomdl.options.quiet) {
            std::print("duplicate ikchain \"{}\" ignored\n", qc.token);
        }
        while (qc.TokenAvailable()) {
            qc.ExpectToken();
        }
        return;
    }

    s_ikchain_t &ikchain = g_studiomdl.anim.ikchain.emplace_back();
    strcpyn(ikchain.name, qc.token);

    qc.ExpectStringToken(ikchain.bonename);

    ikchain.axis = StudioMotionFlags::Z;
    ikchain.value = 0.0;
    ikchain.height = 18.0;
    ikchain.floor = 0.0;
    ikchain.radius = 0.0;

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (lookupControl(qc.token) != StudioMotionFlags::Invalid) {
            ikchain.axis = lookupControl(qc.token);
            qc.ExpectFloatToken(ikchain.value);
        }
        else if (vstd::iequals("height", qc.token)) {
            qc.ExpectFloatToken(ikchain.height);
        }
        else if (vstd::iequals("pad", qc.token)) {
            qc.ExpectToken();
            ikchain.radius = qc.TokenFloat() / 2.0;
        }
        else if (vstd::iequals("floor", qc.token)) {
            qc.ExpectFloatToken(ikchain.floor);
        }
        else if (vstd::iequals("knee", qc.token)) {
            qc.ExpectFloatToken(ikchain.link[0].kneeDir.x);
            qc.ExpectFloatToken(ikchain.link[0].kneeDir.y);
            qc.ExpectFloatToken(ikchain.link[0].kneeDir.z);
        }
        else if (vstd::iequals("center", qc.token)) {
            qc.ExpectFloatToken(ikchain.center.x);
            qc.ExpectFloatToken(ikchain.center.y);
            qc.ExpectFloatToken(ikchain.center.z);
        }
    }
}

void Cmd_IKAutoplayLock(ScriptTokenizer &qc) {
    s_iklock_t &lock = g_studiomdl.anim.ikautoplaylock.emplace_back();
    qc.ExpectStringToken(lock.name);

    qc.ExpectFloatToken(lock.flPosWeight);

    qc.ExpectFloatToken(lock.flLocalQWeight);
}

void Cmd_Root(ScriptTokenizer &qc) {
    qc.ExpectStringToken("bone name", g_studiomdl.script.rootname);
}

void Cmd_Controller(ScriptTokenizer &qc) {
    qc.ExpectToken("controller <number>");
    s_bonecontroller_t controller;

    if (vstd::iequals("mouth", qc.token)) {
        controller.inputfield = 4;
    }
    else {
        controller.inputfield = qc.TokenInt();
    }
    qc.ExpectStringToken("controller <bone_name>", controller.name);
    qc.ExpectToken("controller <axis>");
    const auto axis = lookupControl(qc.token);
    if (axis == StudioMotionFlags::Invalid) {
        MdlWarning("unknown g_studiomdl.bonecontroller type '{}'\n", qc.token);
        return;
    }
    controller.type = axis;
    qc.ExpectFloatToken("controller <start>", controller.start);
    qc.ExpectFloatToken("controller <end>", controller.end);

    if (HasFlag(controller.type, StudioMotionFlags::XR | StudioMotionFlags::YR | StudioMotionFlags::ZR)) {
        if (((int) (controller.start + 360) % 360) ==
            ((int) (controller.end + 360) % 360)) {
            controller.type |= StudioMotionFlags::RLoop;
        }
    }
    g_studiomdl.bones.bonecontrollers.emplace_back(controller);
}

void Cmd_ScreenAlign(ScriptTokenizer &qc) {
    if (qc.GetToken(false)) {
        s_screenalignedbone_t &bone = g_studiomdl.bones.screenalignedbone.emplace_back();
        strcpyn(bone.name, qc.token);
        bone.flags = BoneFlags::ScreenAlignSphere;

        if (qc.GetToken(false)) {
            if (vstd::iequals("sphere", qc.token)) {
                bone.flags = BoneFlags::ScreenAlignSphere;
            }
            else if (vstd::iequals("cylinder", qc.token)) {
                bone.flags = BoneFlags::ScreenAlignCylinder;
            }
        }
    }
    else {
        qc.Error("$screenalign: expected bone name\n");
    }
}

void Cmd_WorldAlign(ScriptTokenizer &qc) {
    if (qc.GetToken(false)) {
        s_worldalignedbone_t &bone = g_studiomdl.bones.worldalignedbone.emplace_back();
        strcpyn(bone.name, qc.token);
        bone.flags = BoneFlags::WorldAlign;
    }
    else {
        qc.Error("$worldalign: expected bone name\n");
    }
}

void Cmd_BBox(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.header.bbox[0][0]);

    qc.ExpectFloatToken(g_studiomdl.header.bbox[0][1]);

    qc.ExpectFloatToken(g_studiomdl.header.bbox[0][2]);

    qc.ExpectFloatToken(g_studiomdl.header.bbox[1][0]);

    qc.ExpectFloatToken(g_studiomdl.header.bbox[1][1]);

    qc.ExpectFloatToken(g_studiomdl.header.bbox[1][2]);

    g_studiomdl.header.wrotebbox = true;
}

void Cmd_BBoxOnlyVerts(ScriptTokenizer &qc) {
    g_studiomdl.header.bboxonlyverts = true;
}

void Cmd_CBox(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.header.cbox[0][0]);

    qc.ExpectFloatToken(g_studiomdl.header.cbox[0][1]);

    qc.ExpectFloatToken(g_studiomdl.header.cbox[0][2]);

    qc.ExpectFloatToken(g_studiomdl.header.cbox[1][0]);

    qc.ExpectFloatToken(g_studiomdl.header.cbox[1][1]);

    qc.ExpectFloatToken(g_studiomdl.header.cbox[1][2]);

    g_studiomdl.header.wrotecbox = true;
}

void Cmd_Gamma(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.options.gamma);
}

void Cmd_TextureGroup(ScriptTokenizer &qc) {
    if (g_studiomdl.options.createMakefile) {
        return;
    }
    int i;
    int depth = 0;
    int index = 0;
    int group = 0;


    qc.ExpectToken();

    // [skin family][index] = material
    auto &families = g_studiomdl.materials.texturegroups.emplace_back();

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
            return;
        }
        if (qc.token[0] == '{') {
            depth++;
        }
        else if (qc.token[0] == '}') {
            depth--;
            if (depth == 0)
                break;
            group++;
            index = 0;
        }
        else if (depth == 2) {
            i = UseTextureAsMaterial(LookupTexture(qc.token));
            if (families.size() <= static_cast<size_t>(group))
                families.resize(group + 1);
            families[group].push_back(i);
            if (group != 0)
                g_studiomdl.materials.textures[i].parent = index < std::ssize(families[0]) ? families[0][index] : 0;
            index++;
        }
    }
}

void Cmd_Hitgroup(ScriptTokenizer &qc) {
    auto &hitgroup = g_studiomdl.bones.hitgroups.emplace_back();
    qc.ExpectIntToken(hitgroup.group);
    qc.ExpectStringToken(hitgroup.name);
}

void Cmd_Hitbox(ScriptTokenizer &qc) {
    bool autogenerated = false;
    if (g_studiomdl.bones.hitboxsets.size() == 0) {
        g_studiomdl.bones.hitboxsets.emplace_back();
        autogenerated = true;
    }

    // Last one
    s_hitboxset *set = &g_studiomdl.bones.hitboxsets[g_studiomdl.bones.hitboxsets.size() - 1];
    if (autogenerated) {
        memset(set, 0, sizeof(*set));

        // fill in name if it wasn't specified in the .qc
        strcpy(set->hitboxsetname, "default");
    }

    qc.ExpectToken("hitbox group number");

    auto &hitbox = set->hitboxes.emplace_back(s_bbox_t{
        .group = qc.TokenInt()
    });

    // Grab the bone name:
    qc.ExpectStringToken("hitbox name", hitbox.name);

    qc.ExpectFloatToken("hitbox min X", hitbox.bmin[0]);
    qc.ExpectFloatToken("hitbox min Y", hitbox.bmin[1]);
    qc.ExpectFloatToken("hitbox min Z", hitbox.bmin[2]);
    qc.ExpectFloatToken("hitbox max X", hitbox.bmax[0]);
    qc.ExpectFloatToken("hitbox max Y", hitbox.bmax[1]);
    qc.ExpectFloatToken("hitbox max Z", hitbox.bmax[2]);

    if (qc.TokenAvailable()) {
        qc.ExpectFloatToken(hitbox.angOffsetOrientation[0]);
        qc.ExpectFloatToken(hitbox.angOffsetOrientation[1]);
        qc.ExpectFloatToken(hitbox.angOffsetOrientation[2]);
    }
    else {
        hitbox.angOffsetOrientation = QAngle(0, 0, 0);
    }

    if (qc.TokenAvailable()) {
        qc.ExpectFloatToken(hitbox.flCapsuleRadius);
    }
    else {
        hitbox.flCapsuleRadius = -1;
    }

    //Scale hitboxes
    scale_vertex(hitbox.bmin);
    scale_vertex(hitbox.bmax);
    // clear out the hitboxname:
    memset(hitbox.hitboxname, 0, sizeof(hitbox.hitboxname));

    // Grab the hit box name if present:
    if (qc.TokenAvailable()) {
        qc.ExpectStringToken(hitbox.hitboxname);
    }
}

void Cmd_HitboxSet(ScriptTokenizer &qc) {
    // Add a new hitboxset
    g_studiomdl.bones.hitboxsets.emplace_back();
    s_hitboxset *set = &g_studiomdl.bones.hitboxsets.back();
    qc.ExpectToken();
    memset(set, 0, sizeof(*set));
    strcpy(set->hitboxsetname, qc.token);
}

//-----------------------------------------------------------------------------
// Assigns a default surface property to the entire model
//-----------------------------------------------------------------------------
void SetDefaultSurfaceProp(const char *pSurfaceProperty) {
    Q_strncpy(g_studiomdl.materials.pDefaultSurfaceProp, pSurfaceProperty, sizeof(g_studiomdl.materials.pDefaultSurfaceProp));
}

void Cmd_SurfaceProp(ScriptTokenizer &qc) {
    qc.ExpectToken();
    SetDefaultSurfaceProp(qc.token);
}

//-----------------------------------------------------------------------------
// Assigns a surface property to a particular joint
//-----------------------------------------------------------------------------
void Cmd_JointSurfaceProp(ScriptTokenizer &qc) {
    // Get joint name...
    qc.ExpectToken();

    char pJointName[MAX_PATH];
    Q_strncpy(pJointName, qc.token, sizeof(pJointName));

    // surface property name
    qc.ExpectToken();
    AddSurfaceProp(pJointName, qc.token);
}

//-----------------------------------------------------------------------------
// Assigns a default contents to the entire model
//-----------------------------------------------------------------------------
void Cmd_Contents(ScriptTokenizer &qc) {
    ContentType nAddFlags, nRemoveFlags;
    ParseContents(qc, nAddFlags, nRemoveFlags);
    s_nDefaultContents |= nAddFlags;
    s_nDefaultContents &= ~nRemoveFlags;
}

//-----------------------------------------------------------------------------
// Assigns contents to a particular joint
//-----------------------------------------------------------------------------
void Cmd_JointContents(ScriptTokenizer &qc) {
    // Get joint name...
    qc.ExpectToken();

    // Search for the name in our list
    int i;
    for (i = s_JointContents.Count(); --i >= 0;) {
        if (vstd::iequals(s_JointContents[i].m_pJointName, qc.token)) {
            break;
        }
    }

    // Add new entry if we haven't seen this name before
    if (i < 0) {
        i = s_JointContents.AddToTail();
        strcpyn(s_JointContents[i].m_pJointName, qc.token);
    }

    ContentType nAddFlags, nRemoveFlags;
    ParseContents(qc, nAddFlags, nRemoveFlags);
    s_JointContents[i].m_nContents = ContentType::SOLID;
    s_JointContents[i].m_nContents |= nAddFlags;
    s_JointContents[i].m_nContents &= ~nRemoveFlags;
}

void Cmd_BoneAlwaysSetup(ScriptTokenizer &qc) {
    if (g_studiomdl.options.createMakefile)
        return;

    g_studiomdl.bones.bone_always_setup.emplace_back();

    // bone name
    qc.ExpectStringToken(g_studiomdl.bones.bone_always_setup.back().bonename);
}

void Internal_Cmd_Attachment(ScriptTokenizer &qc, int nAttachmentTarget = g_studiomdl.bones.attachments.size()) {
    if (g_studiomdl.options.createMakefile)
        return;

    // name
    qc.ExpectToken("attachment <name>");

    s_attachment_t *attachment;

    if (nAttachmentTarget == g_studiomdl.bones.attachments.size()) {
        if (g_studiomdl.bones.attachments.full()) {
            qc.Error("Too many attachments");
            return;
        }
        attachment = &g_studiomdl.bones.attachments.emplace_back();
    }
    else {
        attachment = &g_studiomdl.bones.attachments[nAttachmentTarget];
    }

    attachment->name = qc.token;

    // bone name
    qc.ExpectStringToken("attachment <parent bone>", attachment->bonename);

    Vector tmp;

    // position
    qc.ExpectFloatToken("attachment <X position>", tmp.x);
    qc.ExpectFloatToken("attachment <Y position>", tmp.y);
    qc.ExpectFloatToken("attachment <Z position>", tmp.z);

    scale_vertex(tmp);
    // identity matrix
    AngleMatrix(QAngle(0, 0, 0), attachment->local);

    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (vstd::iequals(qc.token, "absolute")) {
            attachment->type |= AttachmentType::Absolute;
            AngleIMatrix(g_studiomdl.script.defaultrotation, attachment->local);
            // AngleIMatrix( Vector( 0, 0, 0 ), g_studiomdl.attachment[nAttachmentTarget].local );
        }
        else if (vstd::iequals(qc.token, "rigid")) {
            attachment->type |= AttachmentType::Rigid;
        }
        else if (vstd::iequals(qc.token, "world_align")) {
            attachment->flags |= AttachmentFlags::WorldAlign;
        }
        else if (vstd::iequals(qc.token, "rotate")) {
            QAngle angles;
            for (int i = 0; i < 3; ++i) {
                if (!qc.TokenAvailable())
                    break;

                qc.ExpectFloatToken(angles[i]);
            }
            AngleMatrix(angles, attachment->local);
        }
        else if (vstd::iequals(qc.token, "x_and_z_axes")) {
            int i;
            Vector xaxis, yaxis, zaxis;
            for (i = 0; i < 3; ++i) {
                if (!qc.TokenAvailable())
                    break;

                qc.ExpectFloatToken(xaxis[i]);
            }
            for (i = 0; i < 3; ++i) {
                if (!qc.TokenAvailable())
                    break;

                qc.ExpectFloatToken(zaxis[i]);
            }
            VectorNormalize(xaxis);
            VectorMA(zaxis, -DotProduct(zaxis, xaxis), xaxis, zaxis);
            VectorNormalize(zaxis);
            CrossProduct(zaxis, xaxis, yaxis);
            MatrixSetColumn(xaxis, 0, attachment->local);
            MatrixSetColumn(yaxis, 1, attachment->local);
            MatrixSetColumn(zaxis, 2, attachment->local);
            MatrixSetColumn(vec3_origin, 3, attachment->local);
        }
        else {
            qc.Error("unknown attachment ({}) option: ", attachment->name);
        }
    }

    attachment->local[0][3] = tmp.x;
    attachment->local[1][3] = tmp.y;
    attachment->local[2][3] = tmp.z;
}

void Cmd_RedefineAttachment(ScriptTokenizer &qc) {
    // find a pre-existing attachment of the given name and re-populate its values

    if (g_studiomdl.options.createMakefile)
        return;

    // name
    qc.ExpectToken();

    qc.UnGetToken();

    for (int n = 0; n < g_studiomdl.bones.attachments.size(); n++) {
        if (vstd::iequals(qc.token, g_studiomdl.bones.attachments[n].name)) {
            Msg("Found pre-existing attachment matching name: %s\n", qc.token.c_str());
            std::print("Found pre-existing attachment matching name: {}\n", qc.token);
            Internal_Cmd_Attachment(qc, n);
            return;
        }
    }

    qc.Error("Can't redefine attachment \"{}\" because it wasn't found.\n", qc.token);
}

void Cmd_Attachment(ScriptTokenizer &qc) {
    Internal_Cmd_Attachment(qc, g_studiomdl.bones.attachments.size());
}

void Cmd_Renamebone(ScriptTokenizer &qc) {
    s_renamebone_t &rename = g_studiomdl.bones.renamedbone.emplace_back();
    // from
    qc.ExpectStringToken(rename.from);

    // to
    qc.ExpectStringToken(rename.to);
}

void Cmd_StripBonePrefix(ScriptTokenizer &qc) {
    if (!g_studiomdl.bones.szStripBonePrefix.full()) {
        qc.ExpectToken();

        // make sure it's not a duplicate
        for (int k = 0; k < g_studiomdl.bones.szStripBonePrefix.size(); k++) {
            if (g_studiomdl.bones.szStripBonePrefix[k] == (const char *) qc.token) {
                MdlWarning("Ignoring duplicate $bonestripprefix for token {}\n", qc.token);
                return;
            }
        }

        g_studiomdl.bones.szStripBonePrefix.emplace_back(qc.token);
    }
    else {
        qc.Error("Too many bone strip prefixes!\n");
    }
}

void Cmd_RenameBoneSubstr(ScriptTokenizer &qc) {
    s_renamebone_t &rename = g_studiomdl.bones.szRenameBoneSubstr.emplace_back();
    // from
    qc.ExpectStringToken(rename.from);

    // to
    qc.ExpectStringToken(rename.to);
}

// Returns the 1-based id of the named transition node, adding it if needed
int LookupXNode(const char *name) {
    for (size_t i = 0; i < g_studiomdl.anim.xnodenames.size(); i++) {
        if (vstd::iequals(name, g_studiomdl.anim.xnodenames[i])) {
            return static_cast<int>(i + 1);
        }
    }
    if (g_studiomdl.anim.xnodenames.full()) {
        MdlError("Too many transition nodes, max {}\n", (int) g_studiomdl.anim.xnodenames.capacity());
    }
    g_studiomdl.anim.xnodenames.emplace_back(name);
    return static_cast<int>(g_studiomdl.anim.xnodenames.size());
}

void Cmd_Skiptransition(ScriptTokenizer &qc) {
    int nskips = 0;
    int list[10];

    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        list[nskips++] = LookupXNode(qc.token);
    }

    for (int i = 0; i < nskips; i++) {
        for (int j = 0; j < nskips; j++) {
            if (list[i] != list[j]) {
                g_studiomdl.anim.xnodeskips.emplace_back(std::array{list[i], list[j]});
            }
        }
    }
}


//-----------------------------------------------------------------------------
//
// The following code is all related to LODs
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Checks to see if the model source was already loaded
//-----------------------------------------------------------------------------
s_source_t *FindCachedSource(const char *name, const char *xext) {
    const std::string base = g_studiomdl.paths.cddir.back() + name + ".";
    const auto find = [&base](const std::string_view ext) -> s_source_t * {
        g_studiomdl.input.szFilename = base;
        g_studiomdl.input.szFilename += ext;
        for (int i = 0; i < g_studiomdl.anim.sources.size(); i++) {
            if (vstd::iequals(g_studiomdl.input.szFilename, g_studiomdl.anim.sources[i]->filename))
                return g_studiomdl.anim.sources[i];
        }
        return nullptr;
    };

    // we know what extension is necessary. . look for it.
    if (xext[0]) {
        return find(xext);
    }

    // we don't know what extension to use, so look for all of 'em (not .vta).
    for (const char *pExt: {"vrm", "dmx", "smd", "xml", "obj"}) {
        if (s_source_t *pSource = find(pExt)) {
            return pSource;
        }
    }
    return nullptr;
}


//-----------------------------------------------------------------------------
// Parse replacemodel command, causes an LOD to use a new model
//-----------------------------------------------------------------------------

static void Cmd_ReplaceModel(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    CLodScriptReplacement_t &newReplacement = lodData.modelReplacements.emplace_back();

    // from
    qc.ExpectToken();

    // Strip off extensions for the source...
    char *pDot = strrchr(qc.token, '.');
    if (pDot) {
        *pDot = 0;
    }

    if (!FindCachedSource(qc.token, "")) {
        // must have prior knowledge of the from
        qc.Error("Unknown replace model '{}'\n", qc.token);
    }

    newReplacement.SetSrcName(qc.token);

    // to
    qc.ExpectToken();
    newReplacement.SetDstName(qc.token);

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

    // If the LOD system tells us to replace "blank", let's forget
    // we ever read this. Have to do it here so parsing works
    if (vstd::iequals(newReplacement.GetSrcName(), "blank")) {
        lodData.modelReplacements.pop_back(); // the entry just added
        return;
    }

    // Load the source right here baby! That way its bones will get converted
    if (!lodData.IsStrippedFromModel()) {
        newReplacement.m_pSource = Load_Source(newReplacement.GetDstName(), "smd", reverse, false);
    }
    else if (!g_studiomdl.options.quiet) {
        std::print("Stripped lod \"{}\" @ {:.1f}\n", newReplacement.GetDstName(), lodData.switchValue);
    }
}

//-----------------------------------------------------------------------------
// Parse removemodel command, causes an LOD to stop using a model
//-----------------------------------------------------------------------------

static void Cmd_RemoveModel(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    CLodScriptReplacement_t &newReplacement = lodData.modelReplacements.emplace_back();

    // from
    qc.ExpectToken();

    // Strip off extensions...
    char *pDot = strrchr(qc.token, '.');
    if (pDot)
        *pDot = 0;

    newReplacement.SetSrcName(qc.token);

    // to
    newReplacement.SetDstName("");

    // If the LOD system tells us to replace "blank", let's forget
    // we ever read this. Have to do it here so parsing works
    if (vstd::iequals(newReplacement.GetSrcName(), "blank")) {
        lodData.modelReplacements.pop_back(); // the entry just added
    }
}

//-----------------------------------------------------------------------------
// Parse replacebone command, causes a part of an LOD model to use a different bone
//-----------------------------------------------------------------------------

static void Cmd_ReplaceBone(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    CLodScriptReplacement_t &newReplacement = lodData.boneReplacements.emplace_back();

    // from
    qc.ExpectToken();
    newReplacement.SetSrcName(qc.token);

    // to
    qc.ExpectToken();
    newReplacement.SetDstName(qc.token);
}

//-----------------------------------------------------------------------------
// Parse bonetreecollapse command, causes the entire subtree to use the same bone as the node
//-----------------------------------------------------------------------------

static void Cmd_BoneTreeCollapse(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    CLodScriptReplacement_t &newCollapse = lodData.boneTreeCollapses.emplace_back();

    // from
    qc.ExpectToken();
    newCollapse.SetSrcName(qc.token);
}

//-----------------------------------------------------------------------------
// Parse replacematerial command, causes a material to be used in place of another
//-----------------------------------------------------------------------------

static void Cmd_ReplaceMaterial(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    CLodScriptReplacement_t &newReplacement = lodData.materialReplacements.emplace_back();

    // from
    qc.ExpectToken();
    newReplacement.SetSrcName(qc.token);

    // to
    qc.ExpectToken();
    newReplacement.SetDstName(qc.token);

    if (!lodData.IsStrippedFromModel()) {
        // make sure it goes into the master list
        UseTextureAsMaterial(LookupTexture(qc.token));
    }
}

//-----------------------------------------------------------------------------
// Parse removemesh command, causes a mesh to not be used anymore
//-----------------------------------------------------------------------------

static void Cmd_RemoveMesh(ScriptTokenizer &qc, LodScriptData_t &lodData) {
    CLodScriptReplacement_t &newReplacement = lodData.meshRemovals.emplace_back();

    // from
    qc.ExpectToken();
    Q_FixSlashes(qc.token);
    newReplacement.SetSrcName(qc.token);
}

void Cmd_LOD(ScriptTokenizer &qc, const char *cmdname) {
    if (g_studiomdl.header.gflags & STUDIOHDR_FLAGS_HASSHADOWLOD) {
        qc.Error("Model can only have one $shadowlod and it must be the last lod in the " SRC_FILE_EXT " ({}) : {}\n",
                 g_studiomdl.input.iLinecount, g_studiomdl.input.szLine);
    }

    LodScriptData_t &newLOD = g_studiomdl.body.ScriptLODs.emplace_back();

    if (std::ssize(g_studiomdl.body.ScriptLODs) > MAX_NUM_LODS) {
        qc.Error("Too many LODs (MAX_NUM_LODS=={})\n", (int) MAX_NUM_LODS);
    }

    // Shadow lod reserves -1 as switch value
    // which uniquely identifies a shadow lod
    newLOD.switchValue = -1.0f;

    bool isShadowCall = (!stricmp(cmdname, "$shadowlod")) ? true : false;

    if (isShadowCall) {
        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            MdlWarning("({}) : {}:  Ignoring switch value on {} command line\n", cmdname, g_studiomdl.input.iLinecount,
                       g_studiomdl.input.szLine);
        }

        // Disable facial animation by default
        newLOD.EnableFacialAnimation(false);
    }
    else {
        if (qc.TokenAvailable()) {
            qc.ExpectFloatToken(newLOD.switchValue);
            if (newLOD.switchValue < 0.0f) {
                qc.Error("Negative switch value reserved for $shadowlod ({}) : {}\n", g_studiomdl.input.iLinecount,
                         g_studiomdl.input.szLine);
            }
        }
        else {
            qc.Error("expected {} switch value", cmdname);
        }
    }

    if (!qc.GetToken(true) || !vstd::iequals("{", qc.token)) {
        qc.Error("{}: expected '{{', got \"{}\"", cmdname, qc.token);
    }

    // In case we are stripping all lods and it's not Lod0, strip it
    if (g_studiomdl.body.ScriptLODs.size() > 1 && g_studiomdl.options.stripLods)
        newLOD.StripFromModel(true);

    while (1) {
        if (!qc.GetToken(true))
            qc.Error("{}: missing '}}' at end of file", cmdname);
        if (vstd::iequals("replacemodel", qc.token)) {
            Cmd_ReplaceModel(qc, newLOD);
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
            Cmd_ReplaceMaterial(qc, newLOD);
        }
        else if (vstd::iequals("removemesh", qc.token)) {
            Cmd_RemoveMesh(qc, newLOD);
        }
        else if (vstd::iequals("nofacial", qc.token)) {
            newLOD.EnableFacialAnimation(false);
        }
        else if (vstd::iequals("facial", qc.token)) {
            if (isShadowCall) {
                // facial animation has no reasonable purpose on a shadow lod
                qc.Error("Facial animation is not allowed for $shadowlod\n");
            }

            newLOD.EnableFacialAnimation(true);
        }
        else if (vstd::iequals("use_shadowlod_materials", qc.token)) {
            if (isShadowCall) {
                g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS;
            }
        }
        else if (vstd::iequals("}", qc.token)) {
            break;
        }
        else {
            qc.Error("{}: unknown option \"{}\"", cmdname, qc.token);
        }
    }

    // If the LOD is stripped, then forget we saw it
    if (newLOD.IsStrippedFromModel()) {
        g_studiomdl.body.ScriptLODs.pop_back(); // the entry just added
    }
}

void Cmd_ShadowLOD(ScriptTokenizer &qc) {
    if (!g_studiomdl.options.quiet) {
        std::print("Processing $shadowlod\n");
    }

    // Act like it's a regular lod entry
    Cmd_LOD(qc, "$shadowlod");

    // Mark .mdl as having shadow lod (we also check above that we have only one of these
    // and that it's the last entered lod )
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_HASSHADOWLOD;
}


//-----------------------------------------------------------------------------
// A couple commands related to translucency sorting
//-----------------------------------------------------------------------------
void Cmd_Opaque(ScriptTokenizer &qc) {
    // Force Opaque has precedence
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_FORCE_OPAQUE;
    g_studiomdl.header.gflags &= ~STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
}

void Cmd_TranslucentTwoPass(ScriptTokenizer &qc) {
    // Force Opaque has precedence
    if ((g_studiomdl.header.gflags & STUDIOHDR_FLAGS_FORCE_OPAQUE) == 0) {
        g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
    }
}

//-----------------------------------------------------------------------------
// Indicates the model be rendered with ambient boost heuristic (first used on Alyx in Episode 1)
//-----------------------------------------------------------------------------
void Cmd_AmbientBoost(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_AMBIENT_BOOST;
}

//-----------------------------------------------------------------------------
// Indicates the model contains a quad-only Catmull-Clark subd mesh
//-----------------------------------------------------------------------------
void Cmd_SubdivisionSurface(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_SUBDIVISION_SURFACE;
}


//-----------------------------------------------------------------------------
// Indicates the model should not cast shadows (useful for first-person models as used in L4D)
//-----------------------------------------------------------------------------
void Cmd_DoNotCastShadows(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS;
}

//-----------------------------------------------------------------------------
// Indicates the model should cast texture-based shadows in vrad (NOTE: only applicable to prop_static)
//-----------------------------------------------------------------------------
void Cmd_CastTextureShadows(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS;
}


//-----------------------------------------------------------------------------
// Indicates the model should not fade out even if the level or fallback settings say to
//-----------------------------------------------------------------------------
void Cmd_NoForcedFade(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_NO_FORCED_FADE;
}


//-----------------------------------------------------------------------------
// Indicates the model should not use the bone origin when calculating bboxes, sequence boxes, etc.
//-----------------------------------------------------------------------------
void Cmd_SkipBoneInBBox(ScriptTokenizer &qc) {
    g_studiomdl.options.useBoneInBBox = false;
}


//-----------------------------------------------------------------------------
// Indicates the model will lengthen the viseme check to always include two phonemes
//-----------------------------------------------------------------------------
void Cmd_ForcePhonemeCrossfade(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE;
}

//-----------------------------------------------------------------------------
// Indicates the model should keep pre-defined bone lengths regardless of animation changes
//-----------------------------------------------------------------------------
void Cmd_LockBoneLengths(ScriptTokenizer &qc) {
    g_studiomdl.options.lockBoneLengths = true;
}

//-----------------------------------------------------------------------------
// Indicates the model should replace pre-defined bone bind poses
//-----------------------------------------------------------------------------
void Cmd_UnlockDefineBones(ScriptTokenizer &qc) {
    g_studiomdl.options.defineBonesLockedByDefault = false;
}

//-----------------------------------------------------------------------------
// Mark this model as obsolete so that it'll show the obsolete material in game.
//-----------------------------------------------------------------------------
void Cmd_Obsolete(ScriptTokenizer &qc) {
    // Force Opaque has precedence
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_OBSOLETE;
}

//-----------------------------------------------------------------------------
// The bones should be moved so that they center themselves on the verts they own.
//-----------------------------------------------------------------------------
void Cmd_CenterBonesOnVerts(ScriptTokenizer &qc) {
    // force centering on bones
    g_studiomdl.options.centerBonesOnVerts = true;
}

//-----------------------------------------------------------------------------
// How far back should simple motion extract pull back from the last frame
//-----------------------------------------------------------------------------
void Cmd_MotionExtractionRollBack(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.options.defaultMotionRollback);
}

//-----------------------------------------------------------------------------
// rules for breaking up long animations into multiple sub anims
//-----------------------------------------------------------------------------
void Cmd_SectionFrames(ScriptTokenizer &qc) {
    qc.ExpectToken();
    g_studiomdl.options.sectionFrames = qc.TokenFloat();
    qc.ExpectIntToken(g_studiomdl.options.minSectionFrameLimit);
}


//-----------------------------------------------------------------------------
// world space clamping boundaries for animations
//-----------------------------------------------------------------------------
void Cmd_ClampWorldspace(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.options.vecMinWorldspace[0]);

    qc.ExpectFloatToken(g_studiomdl.options.vecMinWorldspace[1]);

    qc.ExpectFloatToken(g_studiomdl.options.vecMinWorldspace[2]);

    qc.ExpectFloatToken(g_studiomdl.options.vecMaxWorldspace[0]);

    qc.ExpectFloatToken(g_studiomdl.options.vecMaxWorldspace[1]);

    qc.ExpectFloatToken(g_studiomdl.options.vecMaxWorldspace[2]);
}

//-----------------------------------------------------------------------------
// Purpose: force a specific parent child relationship
//-----------------------------------------------------------------------------

void Cmd_ForcedHierarchy(ScriptTokenizer &qc) {
    s_forcedhierarchy_t &hierarchy = g_studiomdl.bones.forcedhierarchy.emplace_back();
    // child name
    qc.ExpectStringToken(hierarchy.childname);

    // parent name
    qc.ExpectStringToken(hierarchy.parentname);
}


//-----------------------------------------------------------------------------
// Purpose: insert a virtual bone between a child and parent (currently unsupported)
//-----------------------------------------------------------------------------

void Cmd_InsertHierarchy(ScriptTokenizer &qc) {
    s_forcedhierarchy_t &hierarchy = g_studiomdl.bones.forcedhierarchy.emplace_back();
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

void Cmd_ForceRealign(ScriptTokenizer &qc) {
    s_forcedrealign_t &realign = g_studiomdl.bones.forcedrealign.emplace_back();
    // bone name
    qc.ExpectStringToken(realign.name);

    // skip
    qc.ExpectToken();

    // X axis
    qc.ExpectToken();
    realign.rot.x = DEG2RAD(qc.TokenFloat());

    // Y axis
    qc.ExpectToken();
    realign.rot.y = DEG2RAD(qc.TokenFloat());

    // Z axis
    qc.ExpectToken();
    realign.rot.z = DEG2RAD(qc.TokenFloat());
}


//-----------------------------------------------------------------------------
// Purpose: specify a bone to allow > 180 but < 360 rotation (forces a calculated "mid point" to rotation)
//-----------------------------------------------------------------------------

void Cmd_LimitRotation(ScriptTokenizer &qc) {
    s_limitrotation_t &limit = g_studiomdl.bones.limitrotation.emplace_back();
    // bone name
    qc.ExpectStringToken(limit.name);

    while (qc.TokenAvailable()) {
        // sequence name
        qc.ExpectToken();
        strcpyn(limit.sequencename[limit.numseq++], qc.token);
    }
}

//-----------------------------------------------------------------------------
// Purpose: artist controlled sanity check for expected state of the model.
// The idea is to allow artists to anticipate and prevent content errors by adding 'qc asserts'
// into commonly iterated models. This could be anything from bones that are expected (or not)
// to polycounts, material references, etc.- It's just an "Assert" for content.
//-----------------------------------------------------------------------------
void Cmd_QCAssert(ScriptTokenizer &qc) {
    //get the assert type
    qc.ExpectToken();

    //Msg( "Validating QC Assert '%s'\n", token.c_str() );

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
        s_source_t *pSrc = Load_Source(qc.token.view(), "");

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

        for (int n = 0; n < std::ssize(g_studiomdl.bones.importbone); n++) {
            if (vstd::iequals(szBoneName, g_studiomdl.bones.importbone[n].name)) {
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
    std::print("{}", szAssertLine);
}

//-----------------------------------------------------------------------------
// Purpose: specify bones to store, even if nothing references them
//-----------------------------------------------------------------------------

void Cmd_DefineBone(ScriptTokenizer &qc) {
    s_importbone_t &bone = g_studiomdl.bones.importbone.emplace_back();
    // bone name
    qc.ExpectStringToken(bone.name);

    // parent name
    qc.ExpectStringToken(bone.parent);

    bone.bUnlocked = !g_studiomdl.options.defineBonesLockedByDefault;

    qc.ExpectToken();
    if (!V_strcmp(qc.token, "unlocked")) {
        bone.bUnlocked = true;
    }
    else if (!V_strcmp(qc.token, "locked")) {
        bone.bUnlocked = false;
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
    AngleMatrix(angles, pos, bone.rawLocal);

    if (qc.TokenAvailable()) {
        bone.bPreAligned = true;
        // realign pos
        qc.ExpectFloatToken(pos.x);
        qc.ExpectFloatToken(pos.y);
        qc.ExpectFloatToken(pos.z);
        qc.ExpectFloatToken(angles.x);
        qc.ExpectFloatToken(angles.y);
        qc.ExpectFloatToken(angles.z);

        AngleMatrix(angles, pos, bone.srcRealign);
    }
    else {
        SetIdentityMatrix(bone.srcRealign);
    }
}

bool ParseJiggleAngleConstraint(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JiggleBoneFlags::HasAngleConstraint;

    qc.ExpectToken("angle value");

    jiggleInfo->data.angleLimit = qc.TokenFloat() * M_PI / 180.0f;

    return true;
}

bool ParseJiggleYawConstraint(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JiggleBoneFlags::HasYawConstraint;

    qc.ExpectToken("minimum yaw value");

    jiggleInfo->data.minYaw = qc.TokenFloat() * M_PI / 180.0f;

    qc.ExpectToken("maximum yaw value");

    jiggleInfo->data.maxYaw = qc.TokenFloat() * M_PI / 180.0f;

    return true;
}

bool ParseJigglePitchConstraint(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JiggleBoneFlags::HasPitchConstraint;

    qc.ExpectToken("minimum pitch value");

    jiggleInfo->data.minPitch = qc.TokenFloat() * M_PI / 180.0f;

    qc.ExpectToken("maximum pitch value");

    jiggleInfo->data.maxPitch = qc.TokenFloat() * M_PI / 180.0f;

    return true;
}

void Grab_AxisInterpBones() {
    char cmd[1024], tmp[1025];
    Vector basepos;
    s_axisinterpbone_t *pAxis = nullptr;
    // parsed into a scratch entry that is appended once its "bone" line matched
    auto scratch = s_axisinterpbone_t(); // () zero-fills; {} would leave the Vector members uninitialized
    s_axisinterpbone_t *pBone = &scratch;

    while (GetLineInput()) {
        if (IsEnd(g_studiomdl.input.szLine)) {
            return;
        }
        int i = sscanf(g_studiomdl.input.szLine, "%1023s \"%[^\"]\" \"%[^\"]\" \"%[^\"]\" \"%[^\"]\" %d", cmd,
                       pBone->bonename.data(), tmp,
                       pBone->controlname.data(), tmp, &pBone->axis);
        if (i == 6 && stricmp(cmd, "bone") == 0) {
            // printf( "\"%s\" \"%s\" \"%s\" \"%s\"\n", cmd, pBone->bonename, tmp, pBone->controlname );
            pBone->axis = pBone->axis - 1; // MAX uses 1..3, engine 0..2
            pAxis = &g_studiomdl.bones.axisinterpbones.emplace_back(scratch);
            scratch = s_axisinterpbone_t();
        }
        else if (stricmp(cmd, "display") == 0) {
            // skip all display info
        }
        else if (stricmp(cmd, "type") == 0) {
            // skip all type info
        }
        else if (stricmp(cmd, "basepos") == 0) {
            i = sscanf(g_studiomdl.input.szLine, "basepos %f %f %f", &basepos.x, &basepos.y, &basepos.z);
            // skip all type info
        }
        else if (stricmp(cmd, "axis") == 0) {
            Vector pos;
            QAngle rot;
            int j;
            i = sscanf(g_studiomdl.input.szLine, "axis %d %f %f %f %f %f %f", &j, &pos[0], &pos[1], &pos[2], &rot[2],
                       &rot[0], &rot[1]);
            if (i == 7) {
                VectorAdd(basepos, pos, pAxis->pos[j]);
                AngleQuaternion(rot, pAxis->quat[j]);
            }
        }
    }
}

//
// Parse common parameters.
// This assumes a token has already been read, and returns true if
// the token is recognized and parsed.
//
bool ParseCommonJiggle(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    if (vstd::iequals(qc.token, "tip_mass")) {
        qc.ExpectToken();

        jiggleInfo->data.tipMass = qc.TokenFloat();
    }
    else if (vstd::iequals(qc.token, "length")) {
        qc.ExpectToken();

        jiggleInfo->data.length = qc.TokenFloat();
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

        jiggleInfo->data.yawFriction = qc.TokenFloat();
    }
    else if (vstd::iequals(qc.token, "yaw_bounce")) {
        qc.ExpectToken();

        jiggleInfo->data.yawBounce = qc.TokenFloat();
    }
    else if (vstd::iequals(qc.token, "pitch_constraint")) {
        if (ParseJigglePitchConstraint(qc, jiggleInfo) == false) {
            return false;
        }
    }
    else if (vstd::iequals(qc.token, "pitch_friction")) {
        qc.ExpectToken();

        jiggleInfo->data.pitchFriction = qc.TokenFloat();
    }
    else if (vstd::iequals(qc.token, "pitch_bounce")) {
        qc.ExpectToken();

        jiggleInfo->data.pitchBounce = qc.TokenFloat();
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
bool ParseRigidJiggle(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= (JiggleBoneFlags::IsRigid | JiggleBoneFlags::HasLengthConstraint);

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

    const float minStiffness = 0.0f;
    const float maxStiffness = 1000.0f;

    return clamp(stiffness, minStiffness, maxStiffness);
}

float ParseJiggleDamping(ScriptTokenizer &qc) {
    qc.ExpectToken("damping value");

    float damping = qc.TokenFloat();

    const float minDamping = 0.0f;
    const float maxDamping = 10.0f;

    return clamp(damping, minDamping, maxDamping);
}

//
// Parse parameters for has_base_spring subsection
//
bool ParseBaseSpringJiggle(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JiggleBoneFlags::HasBaseSpring;

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
            jiggleInfo->data.baseStiffness = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "damping")) {
            jiggleInfo->data.baseDamping = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "left_constraint")) {
            qc.ExpectToken();

            jiggleInfo->data.baseMinLeft = qc.TokenFloat();

            qc.ExpectToken();

            jiggleInfo->data.baseMaxLeft = qc.TokenFloat();
        }
        else if (vstd::iequals(qc.token, "left_friction")) {
            qc.ExpectToken();

            jiggleInfo->data.baseLeftFriction = qc.TokenFloat();
        }
        else if (vstd::iequals(qc.token, "up_constraint")) {
            qc.ExpectToken();

            jiggleInfo->data.baseMinUp = qc.TokenFloat();

            qc.ExpectToken();

            jiggleInfo->data.baseMaxUp = qc.TokenFloat();
        }
        else if (vstd::iequals(qc.token, "up_friction")) {
            qc.ExpectToken();

            jiggleInfo->data.baseUpFriction = qc.TokenFloat();
        }
        else if (vstd::iequals(qc.token, "forward_constraint")) {
            qc.ExpectToken();

            jiggleInfo->data.baseMinForward = qc.TokenFloat();

            qc.ExpectToken();

            jiggleInfo->data.baseMaxForward = qc.TokenFloat();
        }
        else if (vstd::iequals(qc.token, "forward_friction")) {
            qc.ExpectToken();

            jiggleInfo->data.baseForwardFriction = qc.TokenFloat();
        }
        else if (vstd::iequals(qc.token, "base_mass")) {
            qc.ExpectToken();

            jiggleInfo->data.baseMass = qc.TokenFloat();
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
bool ParseFlexibleJiggle(ScriptTokenizer &qc, s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= (JiggleBoneFlags::IsFlexible | JiggleBoneFlags::HasLengthConstraint);

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
            jiggleInfo->data.yawStiffness = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "yaw_damping")) {
            jiggleInfo->data.yawDamping = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "pitch_stiffness")) {
            jiggleInfo->data.pitchStiffness = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "pitch_damping")) {
            jiggleInfo->data.pitchDamping = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "along_stiffness")) {
            jiggleInfo->data.alongStiffness = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "along_damping")) {
            jiggleInfo->data.alongDamping = ParseJiggleStiffness(qc);
        }
        else if (vstd::iequals(qc.token, "allow_length_flex")) {
            jiggleInfo->data.flags &= ~JiggleBoneFlags::HasLengthConstraint;
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
void Cmd_JiggleBone(ScriptTokenizer &qc) {
    s_jigglebone_t *jiggleInfo = &g_studiomdl.bones.jigglebones.emplace_back();

    // bone name
    qc.ExpectStringToken(jiggleInfo->bonename);

    // default values
    memset(&jiggleInfo->data, 0, sizeof(mstudiojigglebone_t));
    jiggleInfo->data.length = 10.0f;
    jiggleInfo->data.yawStiffness = 100.0f;
    jiggleInfo->data.pitchStiffness = 100.0f;
    jiggleInfo->data.alongStiffness = 100.0f;
    jiggleInfo->data.baseStiffness = 100.0f;
    jiggleInfo->data.baseMinUp = -100.0f;
    jiggleInfo->data.baseMaxUp = 100.0f;
    jiggleInfo->data.baseMinLeft = -100.0f;
    jiggleInfo->data.baseMaxLeft = 100.0f;
    jiggleInfo->data.baseMinForward = -100.0f;
    jiggleInfo->data.baseMaxForward = 100.0f;

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

    if (!g_studiomdl.options.quiet)
        Msg("Marking bone %s as a jiggle bone\n", jiggleInfo->bonename.c_str());
}


//-----------------------------------------------------------------------------
// Purpose: specify bones to store, even if nothing references them
//-----------------------------------------------------------------------------

void Cmd_IncludeModel(ScriptTokenizer &qc) {
    qc.ExpectToken();
    g_studiomdl.body.includemodels.emplace_back(StudioName::from_format("models/{}", qc.token));
}

void Cmd_CD(ScriptTokenizer &qc) {
    if (g_studiomdl.paths.cdset)
        qc.Error("Two $cd in one model");
    g_studiomdl.paths.cdset = true;
    qc.ExpectToken("<path>");
    g_studiomdl.paths.cddir.clear();
    g_studiomdl.paths.cddir.push_back(std::string(qc.token) + "/");
}

void Cmd_ContentRootRelative(ScriptTokenizer &qc) {
    g_studiomdl.paths.contentRootRelative = true;
}

void Cmd_CDMaterials(ScriptTokenizer &qc) {
    while (qc.TokenAvailable()) {
        qc.ExpectToken();

        char szPath[512];
        Q_strncpy(szPath, qc.token, sizeof(szPath));

        int len = strlen(szPath);
        if (len > 0 && szPath[len - 1] != '/' && szPath[len - 1] != '\\') {
            Q_strncat(szPath, "/", sizeof(szPath), COPY_ALL_CHARACTERS);
        }

        Q_FixSlashes(szPath);
        g_studiomdl.materials.cdtextures.emplace_back(strdup(szPath));
    }
}

void Cmd_Pushd(ScriptTokenizer &qc) {
    qc.ExpectToken();

    std::string newDir = g_studiomdl.paths.cddir.back() + qc.token.str() + "/";
    g_studiomdl.paths.cddir.push_back(std::move(newDir));
}

void Cmd_Popd(ScriptTokenizer &qc) {
    if (g_studiomdl.paths.cddir.size() > 1)
        g_studiomdl.paths.cddir.pop_back();
}


void Cmd_ExternalTextures(ScriptTokenizer &qc) {
    MdlWarning("ignoring $externaltextures, obsolete...");
}

void Cmd_ClipToTextures(ScriptTokenizer &qc) {
    g_studiomdl.options.clip_texcoords = 1;
}

void Cmd_CollapseBones(ScriptTokenizer &qc) {
    g_studiomdl.options.collapse_bones = true;
}

void Cmd_SkinnedLODs(ScriptTokenizer &qc) {
    g_studiomdl.options.bSkinnedLODs = true;
}

void Cmd_CollapseBonesAggressive(ScriptTokenizer &qc) {
    g_studiomdl.options.collapse_bones = true;
    g_studiomdl.options.collapse_bones_aggressive = true;
}

void Cmd_AlwaysCollapse(ScriptTokenizer &qc) {
    g_studiomdl.options.collapse_bones = true;
    qc.ExpectToken();
    g_studiomdl.bones.collapse.emplace_back(qc.token);
}

void Cmd_CalcTransitions(ScriptTokenizer &qc) {
    g_studiomdl.options.multistageGraph = true;
}

void ProcessStaticProp() {
    g_studiomdl.header.staticprop = true;
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_STATIC_PROP;
}

void Cmd_StaticProp(ScriptTokenizer &qc) {
    ProcessStaticProp();
}

void Cmd_ZBrush(ScriptTokenizer &qc) {
    g_studiomdl.options.ZBrush = true;
}

void Cmd_RealignBones(ScriptTokenizer &qc) {
    g_studiomdl.options.realignbones = true;
}

void Cmd_BaseLOD(ScriptTokenizer &qc) {
    Cmd_LOD(qc, "$lod");
}

//-----------------------------------------------------------------------------
// Key value block
//-----------------------------------------------------------------------------
static void AppendKeyValueText(std::string &pKeyValue, const char *pString) {
    int nLen = strlen(pString);
    pKeyValue.append(pString, nLen);
}

int KeyValueTextSize(const std::string &pKeyValue) {
    return pKeyValue.size();
}

const char *KeyValueText(const std::string &pKeyValue) {
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

void Cmd_KeyValues(ScriptTokenizer &qc) {
    Option_KeyValues(qc, g_studiomdl.header.KeyValueText);
}

void Cmd_ConstDirectionalLight(ScriptTokenizer &qc) {
    g_studiomdl.header.gflags |= STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT;

    qc.ExpectToken();
    g_studiomdl.header.constdirectionalightdot = (uint8_t) (qc.TokenFloat() * 255.0f);
}

void Cmd_MinLOD(ScriptTokenizer &qc) {
    qc.ExpectIntToken(g_studiomdl.options.minLod);

    // "minlod" rules over "allowrootlods"
    if (g_studiomdl.options.numAllowedRootLODs > 0 && g_studiomdl.options.numAllowedRootLODs < g_studiomdl.options.minLod) {
        MdlWarning("$minlod {} overrides $allowrootlods {}, proceeding with $allowrootlods {}.\n",
                   g_studiomdl.options.minLod,
                   g_studiomdl.options.numAllowedRootLODs, g_studiomdl.options.minLod);
        g_studiomdl.options.numAllowedRootLODs = g_studiomdl.options.minLod;
    }
}

void Cmd_AllowRootLODs(ScriptTokenizer &qc) {
    qc.ExpectIntToken(g_studiomdl.options.numAllowedRootLODs);

    // Root LOD restriction has to obey "minlod" request
    if (g_studiomdl.options.numAllowedRootLODs > 0 && g_studiomdl.options.numAllowedRootLODs < g_studiomdl.options.minLod) {
        MdlWarning("$allowrootlods {} is conflicting with $minlod {}, proceeding with $allowrootlods {}.\n",
                   g_studiomdl.options.numAllowedRootLODs, g_studiomdl.options.minLod, g_studiomdl.options.minLod);
        g_studiomdl.options.numAllowedRootLODs = g_studiomdl.options.minLod;
    }
}

void Cmd_BoneSaveFrame(ScriptTokenizer &qc) {
    s_bonesaveframe_t tmp;

    // bone name
    qc.ExpectStringToken(tmp.name);

    tmp.bSavePos = false;
    tmp.bSaveRot = false;
    tmp.bSaveRot64 = false;
    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (vstd::iequals("position", qc.token)) {
            tmp.bSavePos = true;
        }
        else if (vstd::iequals("rotation", qc.token)) {
            tmp.bSaveRot = true;
        }
        else if (vstd::iequals("rotation64", qc.token)) {
            tmp.bSaveRot64 = true;
        }
        else {
            qc.Error("unknown option \"{}\" on $bonesaveframe : {}\n", qc.token, tmp.name);
        }
    }

    g_studiomdl.bones.bonesaveframe.push_back(tmp);
}


static bool s_bFlexClothBorderJoints = false;
QAngle s_angClothPrerotate(0, 0, 0);


void Cmd_SetDefaultFadeInTime(ScriptTokenizer &qc) {
    qc.ExpectToken();

    g_studiomdl.options.defaultFadeInTime = qc.TokenFloat();
}

void Cmd_SetDefaultFadeOutTime(ScriptTokenizer &qc) {
    qc.ExpectToken();

    g_studiomdl.options.defaultFadeOutTime = qc.TokenFloat();
}

void Cmd_LCaseAllSequences(ScriptTokenizer &qc) {
    g_studiomdl.options.lCaseAllSequences = true;
}

void Cmd_AllowActivityName(ScriptTokenizer &qc) {
    qc.ExpectToken();

    g_studiomdl.anim.AllowedActivityNames.emplace_back(qc.token);
}

void Cmd_CollisionPrecision(ScriptTokenizer &qc) {
    qc.ExpectToken();

    g_studiomdl.header.collisionPrecision = qc.TokenFloat();
}

void Cmd_ErrorOnSeqRemapFail(ScriptTokenizer &qc) {
    g_studiomdl.options.errorOnSeqRemapFail = true;
}


void Cmd_SetModelIntentionallyHasZeroSequences(ScriptTokenizer &qc) {
    g_studiomdl.options.modelIntentionallyHasZeroSequences = true;
}


void Cmd_BoneMerge(ScriptTokenizer &qc) {
    if (g_studiomdl.options.createMakefile)
        return;

    g_studiomdl.bones.bone_merge.emplace_back();

    // bone name
    qc.ExpectStringToken(g_studiomdl.bones.bone_merge.back().bonename);
}

//-----------------------------------------------------------------------------
// Purpose: create named list of boneweights
//-----------------------------------------------------------------------------
void Option_Weightlist(ScriptTokenizer &qc, s_weightlist_t *pweightlist) {
    int depth = 0;
    int i;

    pweightlist->numbones = 0;

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
        else if (vstd::iequals("posweight", qc.token)) {
            i = pweightlist->numbones - 1;
            if (i < 0) {
                qc.Error("Error with specifing bone Position weight \'{}:{}\'\n", pweightlist->name,
                         pweightlist->bonename[i]);
            }
            qc.ExpectFloatToken(pweightlist->boneposweight[i]);
            if (pweightlist->boneweight[i] == 0 && pweightlist->boneposweight[i] > 0) {
                qc.Error("Non-zero Position weight with zero Rotation weight not allowed \'{}:{} {:f} {:f}\'\n",
                         pweightlist->name, pweightlist->bonename[i], pweightlist->boneweight[i],
                         pweightlist->boneposweight[i]);
            }
        }
        else {
            i = pweightlist->numbones++;
            if (i >= MAXWEIGHTSPERLIST) {
                qc.Error("Too many bones ({}) in weightlist '{}'\n", i, pweightlist->name);
            }
            pweightlist->bonename[i] = strdup(qc.token);
            qc.ExpectFloatToken(pweightlist->boneweight[i]);
            pweightlist->boneposweight[i] = pweightlist->boneweight[i];
        }

        if (depth < 0) {
            qc.Error("missing {{\n");
        }
    };
}

void Cmd_Weightlist(ScriptTokenizer &qc) {
    qc.ExpectToken();

    if (g_studiomdl.anim.weightlist.size() >= MAXWEIGHTLISTS) {
        qc.Error("Too many weightlist commands ({})\n", MAXWEIGHTLISTS);
    }

    // [0] is the default weightlist
    for (size_t i = 1; i < g_studiomdl.anim.weightlist.size(); i++) {
        if (vstd::iequals(g_studiomdl.anim.weightlist[i].name, qc.token)) {
            qc.Error("Duplicate weightlist '{}'\n", qc.token);
        }
    }

    s_weightlist_t &weightlist = g_studiomdl.anim.weightlist.emplace_back();
    strcpyn(weightlist.name, qc.token);

    Option_Weightlist(qc, &weightlist);
}

void Cmd_DefaultWeightlist(ScriptTokenizer &qc) {
    Option_Weightlist(qc, &g_studiomdl.anim.weightlist[0]);
}

/*
=================
Cmd_Origin
=================
*/
void Cmd_Origin(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.script.defaultadjust.x);

    qc.ExpectFloatToken(g_studiomdl.script.defaultadjust.y);

    qc.ExpectFloatToken(g_studiomdl.script.defaultadjust.z);

    if (qc.TokenAvailable()) {
        qc.ExpectToken();
        g_studiomdl.script.defaultrotation.z = DEG2RAD(qc.TokenFloat() + 90);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Set the default root rotation so that the Y axis is up instead of the Z axis (for Maya)
//-----------------------------------------------------------------------------
void ProcessUpAxis(const RadianEuler &angles) {
    g_studiomdl.script.defaultrotation = angles;
}


//-----------------------------------------------------------------------------
// Purpose: Set the default root rotation so that the Y axis is up instead of the Z axis (for Maya)
//-----------------------------------------------------------------------------
void Cmd_UpAxis(ScriptTokenizer &qc) {
    // We want to create a rotation that rotates from the art space
    // (specified by the up direction) to a z up space
    // Note: x, -x, -y are untested
    RadianEuler angles(0.0f, 0.0f, M_PI / 2.0f);
    qc.ExpectToken();
    if (vstd::iequals(qc.token, "x")) {
        // rotate 90 degrees around y to move x into z
        angles.x = 0.0f;
        angles.y = M_PI / 2.0f;
    }
    else if (vstd::iequals(qc.token, "-x")) {
        // untested
        angles.x = 0.0f;
        angles.y = -M_PI / 2.0f;
    }
    else if (vstd::iequals(qc.token, "y")) {
        // rotate 90 degrees around x to move y into z
        angles.x = M_PI / 2.0f;
        angles.y = 0.0f;
    }
    else if (vstd::iequals(qc.token, "-y")) {
        // untested
        angles.x = -M_PI / 2.0f;
        angles.y = 0.0f;
    }
    else if (vstd::iequals(qc.token, "z")) {
        // there's still a built in 90 degree Z rotation :(
        angles.x = 0.0f;
        angles.y = 0.0f;
    }
    else if (vstd::iequals(qc.token, "-z")) {
        // there's still a built in 90 degree Z rotation :(
        angles.x = 0.0f;
        angles.y = 0.0f;
    }
    else {
        qc.Error("unknown $upaxis option: \"{}\"\n", qc.token);
        return;
    }

    ProcessUpAxis(angles);
}


void Cmd_ScaleUp(ScriptTokenizer &qc) {
    qc.ExpectFloatToken(g_studiomdl.script.defaultscale);

    g_studiomdl.script.currentscale = g_studiomdl.script.defaultscale;
}

//-----------------------------------------------------------------------------
// Purpose: Sets how what size chunks to cut the animations into
//-----------------------------------------------------------------------------
void Cmd_AnimBlockSize(ScriptTokenizer &qc) {
    qc.ExpectIntToken(g_studiomdl.anim.animblocksize);
    if (g_studiomdl.anim.animblocksize < 1024) {
        g_studiomdl.anim.animblocksize *= 1024;
    }
    while (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (vstd::iequals(qc.token, "nostall")) {
            g_studiomdl.options.noAnimblockStall = true;
        }
        else if (vstd::iequals(qc.token, "highres")) {
            g_studiomdl.options.animblockHighRes = true;
            g_studiomdl.options.animblockLowRes = false;
        }
        else if (vstd::iequals(qc.token, "lowres")) {
            g_studiomdl.options.animblockLowRes = true;
            g_studiomdl.options.animblockHighRes = false;
        }
        else if (vstd::iequals(qc.token, "numframes")) {
            qc.ExpectToken();
            g_studiomdl.options.maxZeroFrames = clamp(qc.TokenInt(), 1, 4);
        }
        else if (vstd::iequals(qc.token, "cachehighres")) {
            g_studiomdl.options.zeroFramesHighres = true;
        }
        else if (vstd::iequals(qc.token, "posdelta")) {
            qc.ExpectFloatToken(g_studiomdl.options.minZeroFramePosDelta);
        }
        else {
            qc.Error("unknown option \"{}\" on $animblocksize command\n", qc.token);
        }
    }
}

void Cmd_AppendSource(ScriptTokenizer &qc) {
    qc.ExpectToken();

    s_source_t *pOrigSource = g_studiomdl.body.model[0]->source;
    s_source_t *pAppendSource = Load_Source(qc.token.view(), "", false, false,
                                            false
                                            /* don't use cached lookup, since this might be a dup of the starting src */);

    matrix3x4_t matTemp;
    matTemp.SetToIdentity();
    matTemp.ScaleUpper3x3Matrix(g_studiomdl.script.currentscale);

    if (qc.TokenAvailable()) {
        qc.ExpectToken();

        if (!V_strncmp(qc.token, "offset", 6)) {
            Vector vecOffsetPosition;
            vecOffsetPosition.Init();
            QAngle angOffsetAngle;
            angOffsetAngle.Init();
            float flScale = 1;

            int nCount = sscanf(qc.token, "offset pos[ %f %f %f ] angle[ %f %f %f ] scale[ %f ]",
                                &vecOffsetPosition.x, &vecOffsetPosition.y, &vecOffsetPosition.z,
                                &angOffsetAngle.x, &angOffsetAngle.y, &angOffsetAngle.z,
                                &flScale);

            if (nCount == 7) {
                AngleMatrix(angOffsetAngle, vecOffsetPosition, matTemp);
                matTemp.ScaleUpper3x3Matrix(flScale * (1.0f / g_studiomdl.script.currentscale));
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

    AddSrcToSrc(pOrigSource, pAppendSource, matTemp);
}

void Cmd_FlexOptimize(ScriptTokenizer &qc) {
    // $flexoptimize auto|on|off [min static verts]
    qc.ExpectToken();
    if (!ParseFlexOptimizeMode(qc.token.view(), g_studiomdl.options.flexOptimizeMode)) {
        qc.Error("$flexoptimize expects auto, on or off, got \"{}\"\n", qc.token);
    }
    if (qc.TokenAvailable()) {
        qc.ExpectIntToken(g_studiomdl.options.flexOptimizeMinStatic);
    }
}

void Cmd_maxVerts(ScriptTokenizer &qc) {
    // first limit
    qc.ExpectToken();
    g_studiomdl.options.maxVertexLimit = clamp(qc.TokenInt(), 1024, MAXSTUDIOVERTS);
    g_studiomdl.options.maxVertexClamp = MIN(g_studiomdl.options.maxVertexLimit, MAXSTUDIOVERTS / 2);

    if (qc.TokenAvailable()) {
        // actual target limit
        qc.ExpectToken();
        g_studiomdl.options.maxVertexClamp = clamp(qc.TokenInt(), 1024, MAXSTUDIOVERTS);
    }
}

s_sequence_t *LookupSequence(const char *name) {
    int i;
    for (i = 0; i < std::ssize(g_studiomdl.anim.sequence); ++i) {
        if (vstd::iequals(g_studiomdl.anim.sequence[i].name, name))
            return &g_studiomdl.anim.sequence[i];
    }
    return nullptr;
}


s_animation_t *LookupAnimation(const char *name, int nFallbackRecursionDepth) {
    int i;
    for (i = 0; i < g_studiomdl.anim.animations.size(); i++) {
        if (vstd::iequals(g_studiomdl.anim.animations[i]->name, name))
            return g_studiomdl.anim.animations[i];
    }

    s_sequence_t *pseq = LookupSequence(name);

    // pseq->panim grows on access, so check the sizes instead of reading panim[0][0]: this is only a lookup
    if (pseq && !pseq->panim.empty() && !pseq->panim.at(0).empty()) {
        return pseq->panim.at(0).at(0);
    }

    // check optional fallbacks or reserved name syntax

    if (nFallbackRecursionDepth == 0 && !V_strcmp(name, "this")) {
        return LookupAnimation(g_studiomdl.script.szInCurrentSeqName, 1);
    }

    return nullptr;
}

s_animation_t *LookupAnimation(const char *name) {
    return LookupAnimation(name, 0);
}


int Option_AnimTag(ScriptTokenizer &qc, s_sequence_t *psequence) {
    if (psequence->animtags.full()) {
        qc.Error("too many animtags\n");
    }

    qc.ExpectToken("animtag name");

    auto &animtag = psequence->animtags.emplace_back(s_animtag_t{
        .tagname = qc.token
    });


    qc.ExpectFloatToken("animtag cycle", animtag.cycle);


    return 0;
}

int Option_Event(ScriptTokenizer &qc, s_sequence_t *psequence) {
    if (psequence->events.full()) {
        qc.Error("too many events\n");
    }

    qc.ExpectToken("event name");

    auto &event = psequence->events.emplace_back(s_event_t{
        .eventname = qc.token
    });

    qc.ExpectIntToken("event frame", event.frame);

    // option token
    if (qc.TokenAvailable()) {
        qc.ExpectToken();
        if (qc.token[0] == '}') // opps, hit the end
            return 1;
        // found an option
        event.options = qc.token;
    }

    return 0;
}


void Option_IKRule(ScriptTokenizer &qc, s_ikrule_t *pRule) {
    // chain
    qc.ExpectToken();

    int i;
    for (i = 0; i < std::ssize(g_studiomdl.anim.ikchain); i++) {
        if (vstd::iequals(qc.token, g_studiomdl.anim.ikchain[i].name)) {
            break;
        }
    }
    if (i >= std::ssize(g_studiomdl.anim.ikchain)) {
        qc.Error("unknown chain \"{}\" in ikrule\n", qc.token);
    }
    pRule->chain = i;
    // default slot
    pRule->slot = i;

    // type
    qc.ExpectToken();

    if (vstd::iequals(qc.token, "autosteps")) {
        qc.ExpectIntToken(pRule->end);

        qc.ExpectStringToken(pRule->bonename);

        pRule->type = IkRuleType::Ground;

        pRule->height = g_studiomdl.anim.ikchain[pRule->chain].height;
        pRule->floor = g_studiomdl.anim.ikchain[pRule->chain].floor;
        pRule->radius = g_studiomdl.anim.ikchain[pRule->chain].radius;

        pRule->start = -2;
        pRule->peak = -1;
        pRule->tail = -1;
    }
    else if (vstd::iequals(qc.token, "touch")) {
        pRule->type = IkRuleType::Self;

        // bone
        qc.ExpectStringToken(pRule->bonename);
    }
    else if (vstd::iequals(qc.token, "footstep")) {
        pRule->type = IkRuleType::Ground;

        pRule->height = g_studiomdl.anim.ikchain[pRule->chain].height;
        pRule->floor = g_studiomdl.anim.ikchain[pRule->chain].floor;
        pRule->radius = g_studiomdl.anim.ikchain[pRule->chain].radius;
    }
    else if (vstd::iequals(qc.token, "attachment")) {
        pRule->type = IkRuleType::Attachment;

        // name of attachment
        qc.ExpectStringToken(pRule->attachment);
    }
    else if (vstd::iequals(qc.token, "release")) {
        pRule->type = IkRuleType::Release;
    }
    else if (vstd::iequals(qc.token, "unlatch")) {
        pRule->type = IkRuleType::Unlatch;
    }

    pRule->contact = -1;

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
            strcpy(pRule->bonename, qc.token);
        }
        else {
            qc.UnGetToken();
            return;
        }
    }
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
int ParseCmdlistToken(ScriptTokenizer &qc, s_cmdlist_t::AnimCmdVector &cmds) {
    if (cmds.full()) {
        return false;
    }

    s_animcmd_t &cmd = cmds.emplace_back();
    if (vstd::iequals("fixuploop", qc.token)) {
        cmd.cmd = CMD_FIXUP;

        qc.ExpectIntToken(cmd.u.fixuploop.start);
        qc.ExpectIntToken(cmd.u.fixuploop.end);
    }
    else if (vstd::istarts_with(qc.token, "weight")) {
        qc.ExpectToken();

        int i;
        for (i = 1; i < std::ssize(g_studiomdl.anim.weightlist); i++) {
            if (vstd::iequals(g_studiomdl.anim.weightlist[i].name, qc.token)) {
                break;
            }
        }
        if (i == std::ssize(g_studiomdl.anim.weightlist)) {
            qc.Error("unknown weightlist '{}\'\n", qc.token);
        }
        cmd.cmd = CMD_WEIGHTS;
        cmd.u.weightlist.index = i;
    }
    else if (vstd::iequals("subtract", qc.token)) {
        cmd.cmd = CMD_SUBTRACT;

        qc.ExpectToken();

        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown subtract animation '{}\'\n", qc.token);
        }

        cmd.u.subtract.ref = extanim;

        qc.ExpectIntToken(cmd.u.subtract.frame);

        cmd.u.subtract.flags |= StudioSequenceFlags::Post;
    }
    else if (vstd::iequals("presubtract", qc.token)) // FIXME: rename this to something better
    {
        cmd.cmd = CMD_SUBTRACT;

        qc.ExpectToken();

        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown presubtract animation '{}\'\n", qc.token);
        }

        cmd.u.subtract.ref = extanim;

        qc.ExpectIntToken(cmd.u.subtract.frame);
    }
    else if (vstd::iequals("alignto", qc.token)) {
        cmd.cmd = CMD_AO;

        cmd.u.ao.pBonename = nullptr;

        qc.ExpectToken();
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown alignto animation '{}\'\n", qc.token);
        }

        cmd.u.ao.ref = extanim;
        cmd.u.ao.motiontype = StudioMotionFlags::X | StudioMotionFlags::Y;
        cmd.u.ao.srcframe = 0;
        cmd.u.ao.destframe = 0;
    }
    else if (vstd::iequals("align", qc.token)) {
        cmd.cmd = CMD_AO;

        cmd.u.ao.pBonename = nullptr;

        qc.ExpectToken();
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown align animation '{}\'\n", qc.token);
        }

        cmd.u.ao.ref = extanim;

        // motion type to match
        cmd.u.ao.motiontype = StudioMotionFlags::None;
        qc.ExpectToken();
        StudioMotionFlags ctrl;
        while ((ctrl = lookupControl(qc.token)) != StudioMotionFlags::Invalid) {
            cmd.u.ao.motiontype |= ctrl;
            qc.ExpectToken();
        }
        if (cmd.u.ao.motiontype == StudioMotionFlags::None) {
            qc.Error("missing controls on align\n");
        }

        // frame of reference animation to match
        cmd.u.ao.srcframe = qc.TokenInt();

        // against what frame of the current animation
        qc.ExpectIntToken(cmd.u.ao.destframe);
    }
    else if (vstd::iequals("alignboneto", qc.token)) {
        cmd.cmd = CMD_AO;

        qc.ExpectToken();
        cmd.u.ao.pBonename = strdup(qc.token);

        qc.ExpectToken();
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown alignboneto animation '{}\'\n", qc.token);
        }

        cmd.u.ao.ref = extanim;
        cmd.u.ao.motiontype = StudioMotionFlags::X | StudioMotionFlags::Y;
        cmd.u.ao.srcframe = 0;
        cmd.u.ao.destframe = 0;
    }
    else if (vstd::iequals("alignbone", qc.token)) {
        cmd.cmd = CMD_AO;

        qc.ExpectToken();
        cmd.u.ao.pBonename = strdup(qc.token);

        qc.ExpectToken();
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown alignboneto animation '{}\'\n", qc.token);
        }

        cmd.u.ao.ref = extanim;

        // motion type to match
        cmd.u.ao.motiontype = StudioMotionFlags::None;
        qc.ExpectToken();
        StudioMotionFlags ctrl;
        while ((ctrl = lookupControl(qc.token)) != StudioMotionFlags::Invalid) {
            cmd.u.ao.motiontype |= ctrl;
            qc.ExpectToken();
        }
        if (cmd.u.ao.motiontype == StudioMotionFlags::None) {
            qc.Error("missing controls on align\n");
        }

        // frame of reference animation to match
        cmd.u.ao.srcframe = qc.TokenInt();

        // against what frame of the current animation
        qc.ExpectIntToken(cmd.u.ao.destframe);
    }
    else if (vstd::iequals("match", qc.token)) {
        cmd.cmd = CMD_MATCH;

        qc.ExpectToken();

        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown match animation '{}\'\n", qc.token);
        }

        cmd.u.match.ref = extanim;
    }
    else if (vstd::iequals("matchblend", qc.token)) {
        cmd.cmd = CMD_MATCHBLEND;

        qc.ExpectToken();

        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown match animation '{}\'\n", qc.token);
        }

        cmd.u.match.ref = extanim;

        // frame of reference animation to match
        qc.ExpectIntToken(cmd.u.match.srcframe);

        // against what frame of the current animation
        qc.ExpectIntToken(cmd.u.match.destframe);

        // backup and starting match in here
        qc.ExpectIntToken(cmd.u.match.destpre);

        // continue blending match till here
        qc.ExpectIntToken(cmd.u.match.destpost);
    }
    else if (vstd::iequals("worldspaceblend", qc.token)) {
        cmd.cmd = CMD_WORLDSPACEBLEND;

        qc.ExpectToken();

        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown worldspaceblend animation '{}\'\n", qc.token);
        }

        cmd.u.world.ref = extanim;
        cmd.u.world.startframe = 0;
        cmd.u.world.loops = false;
    }
    else if (vstd::iequals("worldspaceblendloop", qc.token)) {
        cmd.cmd = CMD_WORLDSPACEBLEND;

        qc.ExpectToken();

        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown worldspaceblend animation '{}\'\n", qc.token);
        }

        cmd.u.world.ref = extanim;

        qc.ExpectIntToken(cmd.u.world.startframe);

        cmd.u.world.loops = true;
    }
    else if (vstd::iequals("rotateto", qc.token)) {
        cmd.cmd = CMD_ANGLE;

        qc.ExpectFloatToken(cmd.u.angle.angle);
    }
    else if (vstd::iequals("ikrule", qc.token)) {
        cmd.cmd = CMD_IKRULE;

        cmd.u.ikrule.pRule = (s_ikrule_t *) calloc(1, sizeof(s_ikrule_t));

        Option_IKRule(qc, cmd.u.ikrule.pRule);
    }
    else if (vstd::iequals("ikfixup", qc.token)) {
        cmd.cmd = CMD_IKFIXUP;

        cmd.u.ikfixup.pRule = (s_ikrule_t *) calloc(1, sizeof(s_ikrule_t));

        Option_IKRule(qc, cmd.u.ikrule.pRule);
    }
    else if (vstd::iequals("walkframe", qc.token)) {
        cmd.cmd = CMD_MOTION;

        // frame
        qc.ExpectIntToken(cmd.u.motion.iEndFrame);

        // motion type to match
        cmd.u.motion.motiontype = StudioMotionFlags::None;
        while (qc.TokenAvailable()) {
            qc.ExpectToken();
            const StudioMotionFlags ctrl = lookupControl(qc.token);
            if (ctrl != StudioMotionFlags::Invalid) {
                cmd.u.motion.motiontype |= ctrl;
            }
            else {
                qc.UnGetToken();
                break;
            }
        }

        /*
		ExpectToken(); // X
		cmd.u.motion.x = verify_atof( token );

		ExpectToken(); // Y
		cmd.u.motion.y = verify_atof( token );

		ExpectToken(); // A
		cmd.u.motion.zr = verify_atof( token );
		*/
    }
    else if (vstd::iequals("walkalignto", qc.token)) {
        cmd.cmd = CMD_REFMOTION;

        qc.ExpectIntToken(cmd.u.motion.iEndFrame);

        cmd.u.motion.iSrcFrame = cmd.u.motion.iEndFrame;

        qc.ExpectToken(); // reference animation
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown alignto animation '{}\'\n", qc.token);
        }
        cmd.u.motion.pRefAnim = extanim;

        cmd.u.motion.iRefFrame = 0;

        // motion type to match
        cmd.u.motion.motiontype = StudioMotionFlags::None;
        while (qc.TokenAvailable()) {
            qc.ExpectToken();
            const StudioMotionFlags ctrl = lookupControl(qc.token);
            if (ctrl != StudioMotionFlags::Invalid) {
                cmd.u.motion.motiontype |= ctrl;
            }
            else {
                qc.UnGetToken();
                break;
            }
        }


        /*
		ExpectToken(); // X
		cmd.u.motion.x = verify_atof( token );

		ExpectToken(); // Y
		cmd.u.motion.y = verify_atof( token );

		ExpectToken(); // A
		cmd.u.motion.zr = verify_atof( token );
		*/
    }
    else if (vstd::iequals("walkalign", qc.token)) {
        cmd.cmd = CMD_REFMOTION;

        // end frame to apply motion over
        qc.ExpectIntToken(cmd.u.motion.iEndFrame);

        // reference animation
        qc.ExpectToken();
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown alignto animation '{}\'\n", qc.token);
        }
        cmd.u.motion.pRefAnim = extanim;

        // motion type to match
        cmd.u.motion.motiontype = StudioMotionFlags::None;
        while (qc.TokenAvailable()) {
            qc.ExpectToken();
            const StudioMotionFlags ctrl = lookupControl(qc.token);
            if (ctrl != StudioMotionFlags::Invalid) {
                cmd.u.motion.motiontype |= ctrl;
            }
            else {
                break;
            }
        }
        if (cmd.u.motion.motiontype == StudioMotionFlags::None) {
            qc.Error("missing controls on walkalign\n");
        }

        // frame of reference animation to match
        cmd.u.motion.iRefFrame = qc.TokenInt();

        // against what frame of the current animation
        qc.ExpectIntToken(cmd.u.motion.iSrcFrame);
    }
    else if (vstd::iequals("derivative", qc.token)) {
        cmd.cmd = CMD_DERIVATIVE;

        // get scale
        qc.ExpectFloatToken(cmd.u.derivative.scale);
    }
    else if (vstd::iequals("noanimation", qc.token)) {
        cmd.cmd = CMD_NOANIMATION;
    }
    else if (vstd::iequals("noanim_keepduration", qc.token)) {
        cmd.cmd = CMD_NOANIM_KEEPDURATION;
    }
    else if (vstd::iequals("lineardelta", qc.token)) {
        cmd.cmd = CMD_LINEARDELTA;
        cmd.u.linear.flags |= AutoLayerFlags::Post;
    }
    else if (vstd::iequals("splinedelta", qc.token)) {
        cmd.cmd = CMD_LINEARDELTA;
        cmd.u.linear.flags |= AutoLayerFlags::Post;
        cmd.u.linear.flags |= AutoLayerFlags::Spline;
    }
    else if (vstd::iequals("compress", qc.token)) {
        cmd.cmd = CMD_COMPRESS;

        // get frames to skip
        qc.ExpectIntToken(cmd.u.compress.frames);
    }
    else if (vstd::iequals("numframes", qc.token)) {
        cmd.cmd = CMD_NUMFRAMES;

        // get frames to force
        qc.ExpectIntToken(cmd.u.compress.frames);
    }
    else if (vstd::iequals("counterrotate", qc.token)) {
        cmd.cmd = CMD_COUNTERROTATE;

        // get bone name
        qc.ExpectToken();
        cmd.u.counterrotate.pBonename = strdup(qc.token);
    }
    else if (vstd::iequals("counterrotateto", qc.token)) {
        cmd.cmd = CMD_COUNTERROTATE;

        cmd.u.counterrotate.bHasTarget = true;

        // get pitch
        qc.ExpectFloatToken(cmd.u.counterrotate.targetAngle[0]);

        // get yaw
        qc.ExpectFloatToken(cmd.u.counterrotate.targetAngle[1]);

        // get roll
        qc.ExpectFloatToken(cmd.u.counterrotate.targetAngle[2]);

        // get bone name
        qc.ExpectToken();
        cmd.u.counterrotate.pBonename = strdup(qc.token);
    }
    else if (vstd::iequals("localhierarchy", qc.token)) {
        cmd.cmd = CMD_LOCALHIERARCHY;

        // get bone name
        qc.ExpectToken();
        cmd.u.localhierarchy.pBonename = strdup(qc.token);

        // get parent name
        qc.ExpectToken();
        cmd.u.localhierarchy.pParentname = strdup(qc.token);

        cmd.u.localhierarchy.start = -1;
        cmd.u.localhierarchy.peak = -1;
        cmd.u.localhierarchy.tail = -1;
        cmd.u.localhierarchy.end = -1;

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "range")) {
                //
                qc.ExpectToken();
                cmd.u.localhierarchy.start = TokenFloatOrNull(qc);

                //
                qc.ExpectToken();
                cmd.u.localhierarchy.peak = TokenFloatOrNull(qc);

                //
                qc.ExpectToken();
                cmd.u.localhierarchy.tail = TokenFloatOrNull(qc);

                //
                qc.ExpectToken();
                cmd.u.localhierarchy.end = TokenFloatOrNull(qc);
            }
            else {
                qc.UnGetToken();
            }
        }
    }
    else if (vstd::iequals("forceboneposrot", qc.token)) {
        cmd.cmd = CMD_FORCEBONEPOSROT;

        // get bone name
        qc.ExpectToken();
        cmd.u.forceboneposrot.pBonename = strdup(qc.token);

        cmd.u.forceboneposrot.bDoPos = false;
        cmd.u.forceboneposrot.bDoRot = false;

        cmd.u.forceboneposrot.pos[0] = 0;
        cmd.u.forceboneposrot.pos[1] = 0;
        cmd.u.forceboneposrot.pos[2] = 0;

        cmd.u.forceboneposrot.rot[0] = 0;
        cmd.u.forceboneposrot.rot[1] = 0;
        cmd.u.forceboneposrot.rot[2] = 0;

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "pos")) {
                cmd.u.forceboneposrot.bDoPos = true;

                qc.ExpectToken();
                cmd.u.forceboneposrot.pos[0] = TokenFloatOrNull(qc);

                qc.ExpectToken();
                cmd.u.forceboneposrot.pos[1] = TokenFloatOrNull(qc);

                qc.ExpectToken();
                cmd.u.forceboneposrot.pos[2] = TokenFloatOrNull(qc);
            }
            else {
                qc.UnGetToken();
            }

            if (qc.TokenAvailable()) {
                qc.ExpectToken();
                if (vstd::iequals(qc.token, "rot")) {
                    cmd.u.forceboneposrot.bDoRot = true;

                    qc.ExpectToken();
                    cmd.u.forceboneposrot.rot[0] = TokenFloatOrNull(qc);

                    qc.ExpectToken();
                    cmd.u.forceboneposrot.rot[1] = TokenFloatOrNull(qc);

                    qc.ExpectToken();
                    cmd.u.forceboneposrot.rot[2] = TokenFloatOrNull(qc);

                    cmd.u.forceboneposrot.bRotIsLocal = false;
                    if (qc.TokenAvailable()) {
                        qc.ExpectToken();
                        if (vstd::iequals(qc.token, "local")) {
                            cmd.u.forceboneposrot.bRotIsLocal = true;
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
    }
    else if (vstd::iequals("bonedriver", qc.token)) {
        cmd.cmd = CMD_BONEDRIVER;

        cmd.u.bonedriver.iAxis = 0;
        cmd.u.bonedriver.value = 1.0f;
        cmd.u.bonedriver.all = true;

        // get bone name
        qc.ExpectToken();
        cmd.u.bonedriver.pBonename = strdup(qc.token);

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "axis")) {
                qc.ExpectToken();
                if (vstd::iequals(qc.token, "x")) {
                    cmd.u.bonedriver.iAxis = 0;
                }
                else if (vstd::iequals(qc.token, "y")) {
                    cmd.u.bonedriver.iAxis = 1;
                }
                else if (vstd::iequals(qc.token, "z")) {
                    cmd.u.bonedriver.iAxis = 2;
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
                cmd.u.bonedriver.value = TokenFloatOrNull(qc);
            }
            else {
                qc.UnGetToken();
            }
        }

        if (qc.TokenAvailable()) {
            qc.ExpectToken();
            if (vstd::iequals(qc.token, "range")) {
                cmd.u.bonedriver.all = false;

                qc.ExpectIntToken(cmd.u.bonedriver.start);

                qc.ExpectIntToken(cmd.u.bonedriver.peak);

                qc.ExpectIntToken(cmd.u.bonedriver.tail);

                qc.ExpectIntToken(cmd.u.bonedriver.end);
            }
            else {
                qc.UnGetToken();
            }
        }
    }
    else if (vstd::iequals("reverse", qc.token)) {
        cmd.cmd = CMD_REVERSE;
    }
    else if (vstd::iequals("appendanim", qc.token)) {
        cmd.cmd = CMD_APPENDANIM;

        qc.ExpectToken(); // reference animation
        s_animation_t *extanim = LookupAnimation(qc.token);
        if (extanim == nullptr) {
            qc.Error("unknown appendanim '{}\'\n", qc.token);
        }
        cmd.u.appendanim.ref = extanim;
    }
    else {
        return false;
    }
    return true;
}

void Cmd_Cmdlist(ScriptTokenizer &qc) {
    int depth = 0;

    // name
    qc.ExpectToken();
    auto &cmd = g_studiomdl.anim.cmdlists.emplace_back(s_cmdlist_t{
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
        else if (ParseCmdlistToken(qc, cmd.cmds)) {
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
bool ParseAnimationToken(ScriptTokenizer &qc, s_animation_t *panim) {
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
        qc.ExpectFloatToken(panim->fps);
        if (panim->fps <= 0.0f) {
            qc.Error("ParseAnimationToken:  fps ({:f} from '{}') <= 0.0\n", panim->fps, qc.token);
        }
        return true;
    }

    if (vstd::iequals("origin", qc.token)) {
        qc.ExpectFloatToken(panim->adjust.x);

        qc.ExpectFloatToken(panim->adjust.y);

        qc.ExpectFloatToken(panim->adjust.z);
        return true;
    }

    if (vstd::iequals("rotate", qc.token)) {
        qc.ExpectToken();
        // FIXME: broken for Maya
        panim->rotation.z = DEG2RAD(qc.TokenFloat() + 90);
        return true;
    }

    if (vstd::iequals("angles", qc.token)) {
        qc.ExpectToken();
        panim->rotation.x = DEG2RAD(qc.TokenFloat());
        qc.ExpectToken();
        panim->rotation.y = DEG2RAD(qc.TokenFloat());
        qc.ExpectToken();
        panim->rotation.z = DEG2RAD(qc.TokenFloat() + 90.0f);
        return true;
    }

    if (vstd::iequals("scale", qc.token)) {
        qc.ExpectFloatToken(panim->scale);
        return true;
    }

    if (vstd::istarts_with(qc.token, "loop")) {
        panim->flags |= StudioSequenceFlags::Looping;
        return true;
    }

    if (vstd::iequals("noforceloop", qc.token)) {
        panim->flags |= StudioSequenceFlags::NoForceLoop;
        return true;
    }

    if (!Q_strcmp("startloop", qc.token)) {
        qc.ExpectIntToken(panim->looprestart);
        if (panim->looprestartpercent != 0) {
            qc.Error("Can't specify startloop for animation {}, percentstartloop already specified.",
                     panim->name);
        }
        panim->flags |= StudioSequenceFlags::Looping;
        return true;
    }

    if (!Q_strcmp("percentstartloop", qc.token)) {
        qc.ExpectFloatToken(panim->looprestartpercent);
        if (panim->looprestart != 0) {
            qc.Error("Can't specify percentstartloop for animation {}, looprestart already specified.",
                     panim->name);
        }
        panim->flags |= StudioSequenceFlags::Looping;
        return true;
    }

    if (vstd::iequals("fudgeloop", qc.token)) {
        panim->fudgeloop = true;
        panim->flags |= StudioSequenceFlags::Looping;
        return true;
    }

    if (vstd::istarts_with(qc.token, "snap")) {
        panim->flags |= StudioSequenceFlags::Snap;
        return true;
    }

    if (vstd::istarts_with(qc.token, "frame") || vstd::istarts_with(qc.token, "framestart")) {
        // framestart assumes the animation's end frame is ok to use no matter what it is. This is better than finding 'frame 9 10000' in qc script
        bool bUseDefaultEndFrame = (vstd::istarts_with(qc.token, "framestart"));

        qc.ExpectIntToken(panim->startframe);

        if (!bUseDefaultEndFrame) {
            qc.ExpectIntToken(panim->endframe);
        }

        // NOTE: This always affects the first source anim read in
        s_sourceanim_t *pSourceAnim = FindSourceAnim(panim->source, panim->animationname);
        if (pSourceAnim) {
            if (panim->startframe < pSourceAnim->startframe) {
                panim->startframe = pSourceAnim->startframe;
            }

            if (panim->endframe > pSourceAnim->endframe || bUseDefaultEndFrame) {
                panim->endframe = pSourceAnim->endframe;
            }
        }

        if (!g_studiomdl.options.createMakefile && panim->endframe < panim->startframe) {
            qc.Error("end frame before start frame in {}", panim->name);
        }

        panim->numframes = panim->endframe - panim->startframe + 1;

        return true;
    }

    if (vstd::iequals("blockname", qc.token)) {
        qc.ExpectToken();
        s_sourceanim_t *pSourceAnim = FindSourceAnim(panim->source, qc.token);

        // NOTE: This always affects the first source anim read in
        if (pSourceAnim) {
            panim->startframe = pSourceAnim->startframe;
            panim->endframe = pSourceAnim->endframe;

            if (!g_studiomdl.options.createMakefile && panim->endframe < panim->startframe) {
                qc.Error("end frame before start frame in {}", panim->name);
            }

            panim->numframes = panim->endframe - panim->startframe + 1;
            Q_strncpy(panim->animationname, qc.token, sizeof(panim->animationname));
        }
        else {
            qc.Error("Requested unknown animation block name {}\n", qc.token);
        }
        return true;
    }

    if (vstd::iequals("post", qc.token)) {
        panim->flags |= StudioSequenceFlags::Post;
        return true;
    }

    if (vstd::iequals("noautoik", qc.token)) {
        panim->noAutoIK = true;
        return true;
    }

    if (vstd::iequals("autoik", qc.token)) {
        panim->noAutoIK = false;
        return true;
    }

    if (ParseCmdlistToken(qc, panim->cmds))
        return true;

    if (vstd::iequals("cmdlist", qc.token)) {
        qc.ExpectToken(); // A

        int i;
        for (i = 0; i < g_studiomdl.anim.cmdlists.size(); i++) {
            if (vstd::iequals(g_studiomdl.anim.cmdlists[i].name, qc.token)) {
                break;
            }
        }
        if (i == g_studiomdl.anim.cmdlists.size())
            qc.Error("unknown cmdlist {}\n", qc.token);

        for (int j = 0; j < g_studiomdl.anim.cmdlists[i].cmds.size(); j++) {
            if (panim->cmds.size() >= MAXSTUDIOCMDS) {
                qc.Error("Too many cmds in {}\n", panim->name);
            }
            panim->cmds.emplace_back(g_studiomdl.anim.cmdlists[i].cmds[j]);
        }
        return true;
    }

    if (vstd::iequals("motionrollback", qc.token)) {
        qc.ExpectFloatToken(panim->motionrollback);
        return true;
    }

    if (vstd::iequals("noanimblock", qc.token)) {
        panim->disableAnimblocks = true;
        return true;
    }

    if (vstd::iequals("noanimblockstall", qc.token)) {
        panim->isFirstSectionLocal = true;
        return true;
    }

    if (vstd::iequals("nostallframes", qc.token)) {
        qc.ExpectToken();
        panim->numNostallFrames = qc.TokenFloat();
        return true;
    }

    if (lookupControl(qc.token) != StudioMotionFlags::Invalid) {
        panim->motiontype |= lookupControl(qc.token);
        return true;
    }

    return false;
}


//-----------------------------------------------------------------------------
// Purpose: wrapper for parsing $animation tokens
//-----------------------------------------------------------------------------

int ParseAnimation(ScriptTokenizer &qc, s_animation_t *panim, bool isAppend) {
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
        else if (ParseAnimationToken(qc, panim)) {
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
void Cmd_Animation(ScriptTokenizer &qc) {
    // name
    qc.ExpectToken();

    s_animation_t *panim = LookupAnimation(qc.token);

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
    panim = g_studiomdl.anim.animations.emplace_back(c_allocate<s_animation_t>());

    panim->index = g_studiomdl.anim.animations.size() - 1;
    panim->name = qc.token;

    // filename
    qc.ExpectStringToken(panim->filename);

    panim->source = Load_Source(panim->filename.c_str(), "");
    if (panim->source->m_Animations.Count()) {
        s_sourceanim_t *pSourceAnim = &panim->source->m_Animations[0];
        panim->startframe = pSourceAnim->startframe;
        panim->endframe = pSourceAnim->endframe;
        Q_strncpy(panim->animationname, pSourceAnim->animationname, sizeof(panim->animationname));
    }
    else {
        panim->startframe = 0;
        panim->endframe = 0;
        Q_strncpy(panim->animationname, "", sizeof(panim->animationname));
    }
    VectorCopy(g_studiomdl.script.defaultadjust, panim->adjust);
    panim->rotation = g_studiomdl.script.defaultrotation;
    panim->scale = 1.0f;
    panim->fps = 30.0;
    panim->motionrollback = g_studiomdl.options.defaultMotionRollback;

    ParseAnimation(qc, panim, false);

    panim->numframes = panim->endframe - panim->startframe + 1;

    //CheckAutoShareAnimationGroup( panim->name );
}

//-----------------------------------------------------------------------------
// Purpose: allocate an entry for $sequence
//-----------------------------------------------------------------------------
s_sequence_t *ProcessCmdSequence(const char *pSequenceName) {
    s_animation_t *panim = LookupAnimation(pSequenceName);

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

    if (std::ssize(g_studiomdl.anim.sequence) >= MAXSTUDIOSEQUENCES) {
        MdlError("Too many sequences ({} max)\n", MAXSTUDIOSEQUENCES);
    }

    // value-initialized: everything zero
    s_sequence_t *pseq = &g_studiomdl.anim.sequence.emplace_back();

    // initialize sequence
    Q_strncpy(pseq->name, pSequenceName, sizeof(pseq->name));

    pseq->actweight = 0;
    pseq->activityname[0] = '\0';
    pseq->activity = -1; // -1 is the default for 'no activity'

    pseq->paramindex[0] = -1;
    pseq->paramindex[1] = -1;

    pseq->groupsize[0] = 0;
    pseq->groupsize[1] = 0;

    pseq->fadeintime = g_studiomdl.options.defaultFadeInTime;
    pseq->fadeouttime = g_studiomdl.options.defaultFadeOutTime;
    return pseq;
}


int Option_Activity(ScriptTokenizer &qc, s_sequence_t *psequence) {
    qboolean found;

    found = false;

    qc.ExpectToken();
    strcpy(psequence->activityname, qc.token);

    if (g_studiomdl.anim.AllowedActivityNames.size()) {
        if (std::ranges::find(g_studiomdl.anim.AllowedActivityNames, qc.token.view()) ==
            g_studiomdl.anim.AllowedActivityNames.end()) {
            qc.Error("Unknown sequence activity \"{}\" in \"{}\".", qc.token, psequence->name);
        }
    }

    qc.ExpectIntToken(psequence->actweight);

    if (psequence->actweight == 0) {
        qc.Error("Activity {} has a zero weight (weights must be integers > 0)\n", psequence->activityname);
    }

    return 0;
}


int Option_ActivityModifier(ScriptTokenizer &qc, s_sequence_t *psequence) {
    qc.ExpectToken();

    if (qc.token[0] == '{') {
        while (qc.TokenAvailable()) {
            qc.GetToken(true);
            if (vstd::iequals("}", qc.token))
                break;

            vstd::make_lower_ascii(qc.token);
            strcpyn(psequence->activitymodifier[psequence->numactivitymodifiers++].name, qc.token);
        }
    }
    else {
        vstd::make_lower_ascii(qc.token);
        strcpyn(psequence->activitymodifier[psequence->numactivitymodifiers++].name, qc.token);
    }

    return 0;
}

//-----------------------------------------------------------------------------
// Purpose: create a virtual $animation command from a $sequence reference
//-----------------------------------------------------------------------------
s_animation_t *ProcessImpliedAnimation(s_sequence_t *psequence, const char *filename) {
    // allocate animation entry
    s_animation_t *panim = g_studiomdl.anim.animations.emplace_back(c_allocate<s_animation_t>());
    panim->index = g_studiomdl.anim.animations.size() - 1;

    panim->isImplied = true;

    panim->startframe = 0;
    panim->endframe = MAXSTUDIOANIMFRAMES - 1;

    strcpy(panim->name, "@");
    strcat(panim->name, psequence->name);
    panim->filename = filename;

    VectorCopy(g_studiomdl.script.defaultadjust, panim->adjust);
    panim->scale = 1.0f;
    panim->rotation = g_studiomdl.script.defaultrotation;
    panim->fps = 30;
    panim->motionrollback = g_studiomdl.options.defaultMotionRollback;

    //panim->source = Load_Source( panim->filename, "smd" );
    panim->source = Load_Source(panim->filename.c_str(), "");
    if (panim->source->m_Animations.Count()) {
        s_sourceanim_t *pSourceAnim = &panim->source->m_Animations[0];
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

    if (!g_studiomdl.options.createMakefile && panim->endframe < panim->startframe) {
        MdlError("end frame before start frame in {}", panim->name);
    }

    panim->numframes = panim->endframe - panim->startframe + 1;

    //CheckAutoShareAnimationGroup( panim->name );

    return panim;
}

//-----------------------------------------------------------------------------
// Purpose: copy globally reavent $animation options from one $animation to another
//-----------------------------------------------------------------------------

void CopyAnimationSettings(s_animation_t *pdest, s_animation_t *psrc) {
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
void ProcessSequence(s_sequence_t *pseq, int numblends, s_animation_t **animations, bool isAppend) {
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
int ParseSequence(ScriptTokenizer &qc, s_sequence_t *pseq, bool isAppend) {
    g_studiomdl.script.szInCurrentSeqName = pseq->name;

    int depth = 0;
    s_animation_t *animations[64];
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
            Option_Activity(qc, pseq);
        }
        else if ((vstd::iequals("activitymodifier", qc.token)) || (vstd::iequals("actmod", qc.token))) {
            Option_ActivityModifier(qc, pseq);
        }
        else if (vstd::istarts_with(qc.token, "ACT_")) {
            qc.UnGetToken();
            Option_Activity(qc, pseq);
        }
        else if (vstd::iequals("snap", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Snap;
        }
        else if (vstd::iequals("blendwidth", qc.token)) {
            qc.ExpectIntToken(pseq->groupsize[0]);
        }
        else if (vstd::iequals("blend", qc.token)) {
            i = 0;
            if (pseq->paramindex[0] != -1) {
                i = 1;
            }

            qc.ExpectToken();
            j = LookupPoseParameter(qc.token);
            pseq->paramindex[i] = j;
            pseq->paramattachment[i] = -1;
            qc.ExpectFloatToken(pseq->paramstart[i]);
            qc.ExpectFloatToken(pseq->paramend[i]);

            g_studiomdl.anim.pose[j].min = std::min(g_studiomdl.anim.pose[j].min, pseq->paramstart[i]);
            g_studiomdl.anim.pose[j].min = std::min(g_studiomdl.anim.pose[j].min, pseq->paramend[i]);
            g_studiomdl.anim.pose[j].max = std::max(g_studiomdl.anim.pose[j].max, pseq->paramstart[i]);
            g_studiomdl.anim.pose[j].max = std::max(g_studiomdl.anim.pose[j].max, pseq->paramend[i]);
        }
        else if (vstd::iequals("calcblend", qc.token)) {
            i = 0;
            if (pseq->paramindex[0] != -1) {
                i = 1;
            }

            qc.ExpectToken();
            j = LookupPoseParameter(qc.token);
            pseq->paramindex[i] = j;

            qc.ExpectToken();
            pseq->paramattachment[i] = LookupAttachment(qc.token);
            if (pseq->paramattachment[i] == -1) {
                qc.Error("Unknown calcblend attachment \"{}\"\n", qc.token);
            }

            qc.ExpectToken();
            pseq->paramcontrol[i] = lookupControl(qc.token);
        }
        else if (vstd::iequals("blendref", qc.token)) {
            qc.ExpectToken();
            pseq->paramanim = LookupAnimation(qc.token);
            if (pseq->paramanim == nullptr) {
                qc.Error("Unknown blendref animation \"{}\"\n", qc.token);
            }
        }
        else if (vstd::iequals("blendcomp", qc.token)) {
            qc.ExpectToken();
            pseq->paramcompanim = LookupAnimation(qc.token);
            if (pseq->paramcompanim == nullptr) {
                qc.Error("Unknown blendcomp animation \"{}\"\n", qc.token);
            }
        }
        else if (vstd::iequals("blendcenter", qc.token)) {
            qc.ExpectToken();
            pseq->paramcenter = LookupAnimation(qc.token);
            if (pseq->paramcenter == nullptr) {
                qc.Error("Unknown blendcenter animation \"{}\"\n", qc.token);
            }
        }
        else if (vstd::iequals("node", qc.token)) {
            qc.ExpectToken();
            pseq->entrynode = pseq->exitnode = LookupXNode(qc.token);
        }
        else if (vstd::iequals("transition", qc.token)) {
            qc.ExpectToken();
            pseq->entrynode = LookupXNode(qc.token);
            qc.ExpectToken();
            pseq->exitnode = LookupXNode(qc.token);
        }
        else if (vstd::iequals("rtransition", qc.token)) {
            qc.ExpectToken();
            pseq->entrynode = LookupXNode(qc.token);
            qc.ExpectToken();
            pseq->exitnode = LookupXNode(qc.token);
            pseq->nodeflags |= 1;
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
            pseq->flags |= StudioSequenceFlags::RootXform;

            // get bone name
            qc.ExpectToken();

            strcpyn(pseq->rootDriverBoneName, qc.token);
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
            pseq->flags |= StudioSequenceFlags::CyclePose;

            qc.ExpectToken();
            pseq->cycleposeindex = LookupPoseParameter(qc.token);
        }
        else if (vstd::iequals("hidden", qc.token)) {
            pseq->flags |= StudioSequenceFlags::Hidden;
        }
        else if (vstd::iequals("addlayer", qc.token)) {
            qc.ExpectToken();
            auto &auto_layer = pseq->autolayers.emplace_back(s_autolayer_t{.name = qc.token});

            while (qc.TokenAvailable()) {
                qc.ExpectToken();
                if (vstd::iequals("local", qc.token)) {
                    auto_layer.flags |= AutoLayerFlags::Local;
                    pseq->flags |= StudioSequenceFlags::Local;
                }
                else {
                    qc.UnGetToken();
                    break;
                }
            }
        }
        else if (vstd::iequals("iklock", qc.token)) {
            qc.ExpectToken();
            auto &iklock = pseq->iklocks.emplace_back(s_iklock_t{.name = qc.token});

            qc.ExpectFloatToken(iklock.flPosWeight);

            qc.ExpectFloatToken(iklock.flLocalQWeight);
        }
        else if (vstd::iequals("keyvalues", qc.token)) {
            Option_KeyValues(qc, pseq->KeyValue);
        }
        else if (vstd::iequals("blendlayer", qc.token)) {
            auto &auto_layer = pseq->autolayers.emplace_back();
            auto_layer.flags = AutoLayerFlags::None;

            qc.ExpectStringToken(auto_layer.name);

            qc.ExpectFloatToken(auto_layer.start);

            qc.ExpectFloatToken(auto_layer.peak);

            qc.ExpectFloatToken(auto_layer.tail);

            qc.ExpectFloatToken(auto_layer.end);

            while (qc.TokenAvailable()) {
                qc.ExpectToken();
                if (vstd::iequals("xfade", qc.token)) {
                    auto_layer.flags |= AutoLayerFlags::XFade;
                }
                else if (vstd::iequals("spline", qc.token)) {
                    auto_layer.flags |= AutoLayerFlags::Spline;
                }
                else if (vstd::iequals("noblend", qc.token)) {
                    auto_layer.flags |= AutoLayerFlags::NoBlend;
                }
                else if (vstd::iequals("poseparameter", qc.token)) {
                    auto_layer.flags |= AutoLayerFlags::Pose;
                    qc.ExpectToken();
                    auto_layer.pose = LookupPoseParameter(qc.token);
                }
                else if (vstd::iequals("local", qc.token)) {
                    auto_layer.flags |= AutoLayerFlags::Local;
                    pseq->flags |= StudioSequenceFlags::Local;
                }
                else {
                    qc.UnGetToken();
                    break;
                }
            }
        }
        else if ((numblends || isAppend) && ParseAnimationToken(qc, animations[0])) {
        }
        else if (!isAppend) {
            // assume it's an animation reference
            // first look up an existing animation
            for (n = 0; n < g_studiomdl.anim.animations.size(); n++) {
                if (vstd::iequals(qc.token, g_studiomdl.anim.animations[n]->name)) {
                    animations[numblends++] = g_studiomdl.anim.animations[n];
                    break;
                }
            }

            if (n >= g_studiomdl.anim.animations.size()) {
                // assume it's an implied animation
                animations[numblends++] = ProcessImpliedAnimation(pseq, qc.token);
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
void Cmd_Sequence(ScriptTokenizer &qc) {
    qc.ExpectToken();

    if (g_studiomdl.options.lCaseAllSequences)
        vstd::make_lower_ascii(qc.token);

    // Find existing sequences
    const char *pSequenceName = qc.token;
    s_animation_t *panim = LookupAnimation(pSequenceName);
    if (panim != nullptr && panim->isOverride) {
        ParseEmpty(qc);
    }

    s_sequence_t *pseq = ProcessCmdSequence(pSequenceName);
    if (pseq) {
        ParseSequence(qc, pseq, false);
    }
}

//-----------------------------------------------------------------------------
// Purpose: append commands to either a sequence or an animation
//-----------------------------------------------------------------------------
void Cmd_Append(ScriptTokenizer &qc) {
    qc.ExpectToken();


    s_sequence_t *pseq = LookupSequence(qc.token);

    if (pseq) {
        ParseSequence(qc, pseq, true);
        return;
    }
    else {
        s_animation_t *panim = LookupAnimation(qc.token);

        if (panim) {
            ParseAnimation(qc, panim, true);
            return;
        }
    }
    qc.Error("unknown append animation {}\n", qc.token);
}

void Cmd_Prepend(ScriptTokenizer &qc) {
    qc.ExpectToken();

    s_sequence_t *pseq = LookupSequence(qc.token);
    int count = 0;
    s_animation_t *panim = nullptr;
    int iRet = false;

    if (pseq) {
        panim = pseq->panim[0][0];
        count = panim->cmds.size();
        iRet = ParseSequence(qc, pseq, true);
    }
    else {
        panim = LookupAnimation(qc.token);
        if (panim) {
            count = panim->cmds.size();
            iRet = ParseAnimation(qc, panim, true);
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

void Cmd_Continue(ScriptTokenizer &qc) {
    qc.ExpectToken();

    s_sequence_t *pseq = LookupSequence(qc.token);

    if (pseq) {
        if (!qc.GetToken(true))
            return;
        qc.UnGetToken();
        if (qc.token[0] != '$')
            ParseSequence(qc, pseq, true);
        return;
    }
    else {
        s_animation_t *panim = LookupAnimation(qc.token);

        if (panim) {
            if (!qc.GetToken(true))
                return;
            qc.UnGetToken();
            if (qc.token[0] != '$')
                ParseAnimation(qc, panim, true);
            return;
        }
    }
    qc.Error("unknown continue animation {}\n", qc.token);
}

//-----------------------------------------------------------------------------
// Purpose: foward declare an empty sequence
//-----------------------------------------------------------------------------
void Cmd_DeclareSequence(ScriptTokenizer &qc) {
    if (std::ssize(g_studiomdl.anim.sequence) >= MAXSTUDIOSEQUENCES) {
        qc.Error("Too many sequences ({} max)\n", MAXSTUDIOSEQUENCES);
    }

    // value-initialized: everything zero
    s_sequence_t *pseq = &g_studiomdl.anim.sequence.emplace_back();
    pseq->flags = StudioSequenceFlags::Override;

    // initialize sequence
    qc.ExpectStringToken(pseq->name);
}


//-----------------------------------------------------------------------------
// Purpose: forward declare an empty sequence
//-----------------------------------------------------------------------------
void Cmd_DeclareAnimation(ScriptTokenizer &qc) {
    if (g_studiomdl.anim.animations.size() >= g_studiomdl.anim.animations.capacity()) {
        qc.Error("Too many animations ({} max)\n", MAXSTUDIOANIMS);
    }

    // allocate animation entry
    s_animation_t *panim = g_studiomdl.anim.animations.emplace_back(c_allocate<s_animation_t>());
    panim->index = g_studiomdl.anim.animations.size() - 1;
    panim->flags = StudioSequenceFlags::Override;

    // initialize animation
    qc.ExpectStringToken(panim->name);
}

bool Grab_AimAtBones() {
    // parsed into a scratch entry that is appended once the names matched
    auto scratch = s_aimatbone_t(); // () zero-fills; {} would leave the Vector members uninitialized

    // Already know it's <aimconstraint> in the first string, otherwise wouldn't be here
    if (sscanf(g_studiomdl.input.szLine, "%*s %127s %127s %127s", scratch.bonename.data(),
               scratch.parentname.data(),
               scratch.aimname.data()) ==
        3) {
        s_aimatbone_t *pAimAtBone = &g_studiomdl.bones.aimatbones.emplace_back(scratch);

        char cmd[1024];
        Vector vector;

        while (GetLineInput()) {
            g_studiomdl.input.iLinecount++;

            if (IsEnd(g_studiomdl.input.szLine)) {
                return false;
            }

            if (sscanf(g_studiomdl.input.szLine, "%1024s %f %f %f", cmd, &vector[0], &vector[1], &vector[2]) != 4) {
                // Allow blank lines to be skipped without error
                bool allSpace(true);
                for (const char *pC(g_studiomdl.input.szLine); *pC != '\0' && pC < (g_studiomdl.input.szLine + 4096);
                     ++pC) {
                    if (!V_isspace(*pC)) {
                        allSpace = false;
                        break;
                    }
                }

                if (allSpace) {
                    continue;
                }

                return true;
            }

            if (stricmp(cmd, "<aimvector>") == 0) {
                // Make sure these are unit length on read
                VectorNormalize(vector);
                pAimAtBone->aimvector = vector;
            }
            else if (stricmp(cmd, "<upvector>") == 0) {
                // Make sure these are unit length on read
                VectorNormalize(vector);
                pAimAtBone->upvector = vector;
            }
            else if (stricmp(cmd, "<basepos>") == 0) {
                pAimAtBone->basepos = vector;
            }
            else {
                return true;
            }
        }
    }

    // If we get here, we're at EOF
    return false;
}

void Grab_QuatInterpBones() {
    char cmd[1024];
    Vector basepos;
    RadianEuler rotateaxis(0.0f, 0.0f, 0.0f);
    RadianEuler jointorient(0.0f, 0.0f, 0.0f);
    s_quatinterpbone_t *pAxis = nullptr;
    // parsed into a scratch entry that is appended once its "<helper>" line matched
    auto scratch = s_quatinterpbone_t(); // () zero-fills; {} would leave the Vector members uninitialized
    s_quatinterpbone_t *pBone = &scratch;

    while (GetLineInput()) {
        g_studiomdl.input.iLinecount++;
        if (IsEnd(g_studiomdl.input.szLine)) {
            return;
        }

        int i = sscanf(g_studiomdl.input.szLine, "%s %s %s %s %s", cmd, pBone->bonename.data(),
                       pBone->parentname.data(),
                       pBone->controlparentname.data(),
                       pBone->controlname.data());

        while (i == 4 && stricmp(cmd, "<aimconstraint>") == 0) {
            // If Grab_AimAtBones() returns false, there file is at EOF
            if (!Grab_AimAtBones()) {
                return;
            }

            // Grab_AimAtBones will read input into g_studiomdl.input.szLine same as here until it gets a line it doesn't understand, at which point
            // it will exit leaving that line in g_studiomdl.input.szLine, so check for the end and scan the current buffer again and continue on with
            // the normal QuatInterpBones process

            i = sscanf(g_studiomdl.input.szLine, "%s %s %s %s %s", cmd, pBone->bonename.data(),
                       pBone->parentname.data(),
                       pBone->controlparentname.data(),
                       pBone->controlname.data());
        }

        if (i == 5 && stricmp(cmd, "<helper>") == 0) {
            // printf( "\"%s\" \"%s\" \"%s\" \"%s\"\n", cmd, pBone->bonename, tmp, pBone->controlname );
            pAxis = &g_studiomdl.bones.quatinterpbones.emplace_back(scratch);
            scratch = s_quatinterpbone_t();
        }
        else if (i > 0) {
            // There was a bug before which could cause the same command to be parsed twice
            // because if the sscanf above completely fails, it will return 0 and not
            // change the contents of cmd, so i should be greater than 0 in order for
            // any of these checks to be valid... Still kind of buggy as these checks
            // do case insensitive stricmp but then the sscanf does case sensitive
            // matching afterwards... Should probably change those to
            // sscanf( g_studiomdl.input.szLine, "%*s %f ... ) etc...

            if (stricmp(cmd, "<display>") == 0) {
                // skip all display info
                Vector size;
                float distance;

                i = sscanf(g_studiomdl.input.szLine, "<display> %f %f %f %f",
                           &size[0], &size[1], &size[2],
                           &distance);

                if (i == 4) {
                    pAxis->percentage = distance / 100.0;
                    pAxis->size = size;
                }
                else {
                    MdlError("Line {}: Unable to parse procedual <display> bone: {}", g_studiomdl.input.iLinecount,
                             g_studiomdl.input.szLine);
                }
            }
            else if (stricmp(cmd, "<basepos>") == 0) {
                i = sscanf(g_studiomdl.input.szLine, "<basepos> %f %f %f", &basepos.x, &basepos.y, &basepos.z);
                // skip all type info
            }
            else if (stricmp(cmd, "<rotateaxis>") == 0) {
                i = sscanf(g_studiomdl.input.szLine, "%*s %f %f %f", &rotateaxis.x, &rotateaxis.y, &rotateaxis.z);
                rotateaxis.x = DEG2RAD(rotateaxis.x);
                rotateaxis.y = DEG2RAD(rotateaxis.y);
                rotateaxis.z = DEG2RAD(rotateaxis.z);
            }
            else if (stricmp(cmd, "<jointorient>") == 0) {
                i = sscanf(g_studiomdl.input.szLine, "%*s %f %f %f", &jointorient.x, &jointorient.y, &jointorient.z);
                jointorient.x = DEG2RAD(jointorient.x);
                jointorient.y = DEG2RAD(jointorient.y);
                jointorient.z = DEG2RAD(jointorient.z);
            }
            else if (stricmp(cmd, "<trigger>") == 0) {
                float tolerance;
                RadianEuler trigger;
                Vector pos;
                RadianEuler ang;

                QAngle rot;
                int j;
                i = sscanf(g_studiomdl.input.szLine, "<trigger> %f %f %f %f %f %f %f %f %f %f",
                           &tolerance,
                           &trigger.x, &trigger.y, &trigger.z,
                           &ang.x, &ang.y, &ang.z,
                           &pos.x, &pos.y, &pos.z);

                if (i == 10) {
                    trigger.x = DEG2RAD(trigger.x);
                    trigger.y = DEG2RAD(trigger.y);
                    trigger.z = DEG2RAD(trigger.z);
                    ang.x = DEG2RAD(ang.x);
                    ang.y = DEG2RAD(ang.y);
                    ang.z = DEG2RAD(ang.z);

                    Quaternion q;
                    AngleQuaternion(ang, q);

                    if (rotateaxis.x != 0.0 || rotateaxis.y != 0.0 || rotateaxis.z != 0.0) {
                        Quaternion q1;
                        Quaternion q2;
                        AngleQuaternion(rotateaxis, q1);
                        QuaternionMult(q1, q, q2);
                        q = q2;
                    }

                    if (jointorient.x != 0.0 || jointorient.y != 0.0 || jointorient.z != 0.0) {
                        Quaternion q1;
                        Quaternion q2;
                        AngleQuaternion(jointorient, q1);
                        QuaternionMult(q, q1, q2);
                        q = q2;
                    }

                    j = pAxis->numtriggers++;
                    pAxis->tolerance[j] = DEG2RAD(tolerance);
                    AngleQuaternion(trigger, pAxis->trigger[j]);
                    VectorAdd(basepos, pos, pAxis->pos[j]);
                    pAxis->quat[j] = q;
                }
                else {
                    MdlError("Line {}: Unable to parse procedual <trigger> bone: {}", g_studiomdl.input.iLinecount,
                             g_studiomdl.input.szLine);
                }
            }
            else {
                MdlError("Line {}: Unable to parse procedual bone data: {}", g_studiomdl.input.iLinecount,
                         g_studiomdl.input.szLine);
            }
        }
        else {
            // Allow blank lines to be skipped without error
            bool allSpace(true);
            for (const char *pC(g_studiomdl.input.szLine); *pC != '\0' && pC < (g_studiomdl.input.szLine + 4096); ++
                 pC) {
                if (!V_isspace(*pC)) {
                    allSpace = false;
                    break;
                }
            }

            if (!allSpace) {
                MdlError("Line {}: Unable to parse procedual bone data: {}", g_studiomdl.input.iLinecount,
                         g_studiomdl.input.szLine);
            }
        }
    }
}

void Load_ProceduralBones(ScriptTokenizer &qc) {
    char cmd[1024];
    int option;

    qc.ExpectToken();
    const std::string filename = qc.token.str();

    if (!OpenGlobalFile(filename)) {
        Error("unknown $procedural file \"%s\"\n", filename.c_str());
        return;
    }

    g_studiomdl.input.iLinecount = 0;

    if (vstd::iequals(vstd::paths::extension(filename), "vrd")) {
        Grab_QuatInterpBones();
    }
    else {
        while (GetLineInput()) {
            g_studiomdl.input.iLinecount++;
            const int numRead = sscanf(g_studiomdl.input.szLine, "%s", cmd, &option);

            // No Command Was Parsed, Blank Line Usually
            if ((numRead == EOF) || (numRead == 0))
                continue;

            if (stricmp(cmd, "version") == 0) {
                if (option != 1) {
                    qc.Error("bad version\n");
                }
            }
            else if (stricmp(cmd, "proceduralbones") == 0) {
                Grab_AxisInterpBones();
            }
        }
    }
    g_studiomdl.input.inputFile.reset();
}

void Cmd_CollisionModel(ScriptTokenizer &qc) {
    DoCollisionModel(qc, false);
}

void Cmd_CollisionJoints(ScriptTokenizer &qc) {
    DoCollisionModel(qc, true);
}

//
// This is the master list of the commands a QC file supports.
// To add a new command to the QC files, add it here.
//
MDLCommand_t g_Commands[] =
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

int g_nMDLCommandCount = ARRAYSIZE(g_Commands);

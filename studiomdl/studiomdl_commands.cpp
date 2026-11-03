//
// Created by RED on 15.05.2024.
//

#include "studiomdl/platform_compat.h"
#include "studiomdl_commands.h"
#include "studiomdl/flexoptimize.h"
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

#ifdef WIN32
#undef strdup
#define strdup _strdup
#define strlwr _strlwr
#endif

//-----------------------------------------------------------------------------
// Parse contents flags
//-----------------------------------------------------------------------------
static void ParseContents(int *pAddFlags, int *pRemoveFlags) {
    *pAddFlags = 0;
    *pRemoveFlags = 0;
    do {
        GetToken(false);

        if (!stricmp(token, "grate")) {
            *pAddFlags |= CONTENTS_GRATE;
            *pRemoveFlags |= CONTENTS_SOLID;
        }
        else if (!stricmp(token, "ladder")) {
            *pAddFlags |= CONTENTS_LADDER;
        }
        else if (!stricmp(token, "solid")) {
            *pAddFlags |= CONTENTS_SOLID;
        }
        else if (!stricmp(token, "monster")) {
            *pAddFlags |= CONTENTS_MONSTER;
        }
        else if (!stricmp(token, "notsolid")) {
            *pRemoveFlags |= CONTENTS_SOLID;
        }
    } while (TokenAvailable());
}

void ProcessModelName(const char *pModelName) {
    // Abort early if modelname is too big

    const int nModelNameLen = Q_strlen(pModelName);
    char *pTmpBuf = reinterpret_cast<char *>(_alloca((nModelNameLen + 1) * sizeof(char)));
    Q_StripExtension(pModelName, pTmpBuf, nModelNameLen + 1);
    // write.cpp strips extension then adds .mdl and writes that into studiohdr_t::name

    // Need one for sizeof operation to work...
    studiohdr_t shdr;
    if (Q_strlen(pTmpBuf) + 4 >= (sizeof(g_studiomdl.outname) / sizeof(g_studiomdl.outname[0]))) {
        MdlError("Model Name \"%s.mdl\" Too Big, %d Characters, Max %d Characters\n",
                 pTmpBuf,
                 Q_strlen(pTmpBuf) + 4,
                 (sizeof(g_studiomdl.outname) / sizeof(g_studiomdl.outname)) - 1);
    }

    g_StudioMdlContext.bHasModelName = true;
    Q_strncpy(g_studiomdl.outname, pModelName, sizeof(g_studiomdl.outname));
#ifdef POSIX
    // "$modelname dir\\name.mdl" is common in QCs written on Windows; the output path is built from g_studiomdl.outname
    Q_FixSlashes(g_studiomdl.outname, '/');
#endif
}

//-----------------------------------------------------------------------------
// Parse the studio options from a .qc file
//-----------------------------------------------------------------------------
bool ParseOptionStudio(CDmeSourceSkin *pSkin) {
    if (!GetToken(false)) {
        MdlError("Error: missing <reference mesh>.\n");
        return false;
    }

    pSkin->SetRelativeFileName(token);
    while (TokenAvailable()) {
        GetToken(false);
        if (!Q_stricmp("reverse", token)) {
            pSkin->m_bFlipTriangles = true;
            continue;
        }

        if (!Q_stricmp("scale", token)) {
            GetToken(false);
            pSkin->m_flScale = verify_atof(token);
            continue;
        }

        if (!Q_stricmp("faces", token)) {
            GetToken(false);
            GetToken(false);
            continue;
        }

        if (!Q_stricmp("bias", token)) {
            GetToken(false);
            continue;
        }

        if (!Q_stricmp("subd", token)) {
            pSkin->m_bQuadSubd = true;
            continue;
        }

        if (!Q_stricmp("{", token)) {
            UnGetToken();
            break;
        }

        MdlError("unknown command \"%s\"\n", token);
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Parse the body command from a .qc file
//-----------------------------------------------------------------------------
void ProcessOptionStudio(s_model_t *pmodel, const char *pFullPath, float flScale, bool bFlipTriangles, bool bQuadSubd) {
    Q_strncpy(pmodel->filename, pFullPath, sizeof(pmodel->filename));

    if (flScale != 0.0f) {
        pmodel->scale = g_studiomdl.currentscale = flScale;
    }
    else {
        pmodel->scale = g_studiomdl.currentscale = g_studiomdl.defaultscale;
    }

    g_pCurrentModel = pmodel;

    // load source
    pmodel->source = Load_Source(pmodel->filename, "", bFlipTriangles, true);

    g_pCurrentModel = nullptr;

    // Reset currentscale to whatever global we currently have set
    // g_studiomdl.defaultscale gets set in Cmd_ScaleUp everytime the $scale command is used.
    g_studiomdl.currentscale = g_studiomdl.defaultscale;
}

//-----------------------------------------------------------------------------
// Process a body command
//-----------------------------------------------------------------------------
void ProcessCmdBody(const char *pFullPath, const char *pBodyPartName, float flScale, bool bFlipTriangles,
                    bool bQuadSubd) {
    g_studiomdl.bodypart[g_studiomdl.numbodyparts].base = 1;
    if (g_studiomdl.numbodyparts != 0) {
        g_studiomdl.bodypart[g_studiomdl.numbodyparts].base =
                g_studiomdl.bodypart[g_studiomdl.numbodyparts - 1].base * g_studiomdl.bodypart[
                    g_studiomdl.numbodyparts - 1].nummodels;
    }
    Q_strncpy(g_studiomdl.bodypart[g_studiomdl.numbodyparts].name, pBodyPartName,
              sizeof(g_studiomdl.bodypart[g_studiomdl.numbodyparts].name));

    g_studiomdl.model[g_studiomdl.nummodels] = (s_model_t *) calloc(1, sizeof(s_model_t));
    g_studiomdl.bodypart[g_studiomdl.numbodyparts].pmodel[0] = g_studiomdl.model[g_studiomdl.nummodels];
    g_studiomdl.bodypart[g_studiomdl.numbodyparts].nummodels = 1;

    ProcessOptionStudio(g_studiomdl.model[g_studiomdl.nummodels], pFullPath, flScale, bFlipTriangles, bQuadSubd);

    // Body command should add any flex commands in the source loaded
    PostProcessSource(g_studiomdl.model[g_studiomdl.nummodels]->source, g_studiomdl.nummodels);

    g_studiomdl.nummodels++;
    g_studiomdl.numbodyparts++;
}


//-----------------------------------------------------------------------------
// Purpose: Handle the $boneflexdriver command
// QC: $boneflexdriver <bone name> <tx|ty|tz> <flex controller name> <min> <max>
//-----------------------------------------------------------------------------
void Cmd_BoneFlexDriver() {
    // Find or create the DmeBoneFlexDriverList
    CDmeBoneFlexDriverList *pDmeBoneFlexDriverList = GetElement<CDmeBoneFlexDriverList>(
        g_StudioMdlContext.hDmeBoneFlexDriverList);
    if (!pDmeBoneFlexDriverList) {
        pDmeBoneFlexDriverList = CreateElement<CDmeBoneFlexDriverList>("boneDriverFlexList", DMFILEID_INVALID);
        if (pDmeBoneFlexDriverList) {
            g_StudioMdlContext.hDmeBoneFlexDriverList = pDmeBoneFlexDriverList->GetHandle();
        }
    }

    if (!pDmeBoneFlexDriverList) {
        MdlError("%s: Couldn't find or create DmeBoneDriverFlexList\n", "$boneflexdriver");
        return;
    }

    // <bone name>
    GetToken(false);
    CDmeBoneFlexDriver *pDmeBoneFlexDriver = pDmeBoneFlexDriverList->FindOrCreateBoneFlexDriver(token);
    if (!pDmeBoneFlexDriver) {
        MdlError("%s: Couldn't find or create DmeBoneFlexDriver for bone \"%s\"\n", "$boneflexdriver", token);
        return;
    }

    // <tx|ty|tz|rx|ry|rz>
    GetToken(false);
    const char *ppszComponentTypeList[] = {"tx", "ty", "tz"};
    int nBoneComponent = -1;
    for (int i = 0; i < ARRAYSIZE(ppszComponentTypeList); ++i) {
        if (StringHasPrefix(token, ppszComponentTypeList[i])) {
            nBoneComponent = i;
            break;
        }
    }

    if (nBoneComponent < STUDIO_BONE_FLEX_TX || nBoneComponent > STUDIO_BONE_FLEX_TZ) {
        TokenError("%s: Invalid bone component, must be one of <tx|ty|tz>\n", "$boneflexdriver");
        return;
    }

    // <flex controller name>
    GetToken(false);
    CDmeBoneFlexDriverControl *pDmeBoneFlexDriverControl = pDmeBoneFlexDriver->FindOrCreateControl(token);
    if (!pDmeBoneFlexDriverControl) {
        MdlError("%s: Couldn't find or create DmeBoneFlexDriverControl for bone \"%s\"\n", "$boneflexdriver", token);
        return;
    }

    pDmeBoneFlexDriverControl->m_nBoneComponent = nBoneComponent;

    // <min>
    GetToken(false);
    pDmeBoneFlexDriverControl->m_flMin = verify_atof(token);

    // <max>
    GetToken(false);
    pDmeBoneFlexDriverControl->m_flMax = verify_atof(token);
}

void Cmd_PoseParameter() {
    if (g_studiomdl.numposeparameters >= MAXSTUDIOPOSEPARAM) {
        TokenError("too many pose parameters (max %d)\n", MAXSTUDIOPOSEPARAM);
    }

    GetToken(false); //[wills] unless you want a pose parameter named "poseparameter", should probably GetToken here

    int i = LookupPoseParameter(token);
    strcpyn(g_studiomdl.pose[i].name, token);

    if (TokenAvailable()) {
        // min
        GetToken(false);
        g_studiomdl.pose[i].min = verify_atof(token);
    }

    if (TokenAvailable()) {
        // max
        GetToken(false);
        g_studiomdl.pose[i].max = verify_atof(token);
    }

    while (TokenAvailable()) {
        GetToken(false);

        if (!Q_stricmp(token, "wrap")) {
            g_studiomdl.pose[i].flags |= STUDIO_LOOPING;
            g_studiomdl.pose[i].loop = g_studiomdl.pose[i].max - g_studiomdl.pose[i].min;
        }
        else if (!Q_stricmp(token, "loop")) {
            g_studiomdl.pose[i].flags |= STUDIO_LOOPING;
            GetToken(false);
            g_studiomdl.pose[i].loop = verify_atof(token);
        }
    }
}

void Cmd_OverrideMaterial() {
    char to[256];

    GetToken(false);
    strcpy(to, token);

    Msg("$overridematerial is replacing ALL material references with %s.\n", to);

    int i;
    for (i = 0; i < g_studiomdl.textures.size(); i++) {
        strcpy(g_studiomdl.textures[i].name, to);
    }
}

void Cmd_RenameMaterial() {
    char from[256];
    char to[256];

    GetToken(false);
    strcpy(from, token);

    GetToken(false);
    strcpy(to, token);

    int i;
    for (i = 0; i < g_studiomdl.textures.size(); i++) {
        if (stricmp(g_studiomdl.textures[i].name, from) == 0) {
            strcpy(g_studiomdl.textures[i].name, to);
            return;
        }
    }

    for (i = 0; i < g_studiomdl.textures.size(); i++) {
        if (V_stristr(g_studiomdl.textures[i].name, from)) {
            Msg("$renamematerial fell back to partial match: Replacing %s with %s.\n",
                g_studiomdl.textures[i].name.c_str(), to);
            strcpy(g_studiomdl.textures[i].name, to);
            return;
        }
    }

    MdlError("unknown material \"%s\" in rename\n", from);
}

void Cmd_Eyeposition() {
    // rotate points into frame of reference so g_studiomdl.model points down the positive x
    // axis
    //	FIXME: these coords are bogus
    GetToken(false);
    g_studiomdl.eyeposition[1] = verify_atof(token);

    GetToken(false);
    g_studiomdl.eyeposition[0] = -verify_atof(token);

    GetToken(false);
    g_studiomdl.eyeposition[2] = verify_atof(token);
}

void Cmd_MaxEyeDeflection() {
    GetToken(false);
    g_studiomdl.flMaxEyeDeflection = cosf(verify_atof(token) * M_PI / 180.0f);
}

//-----------------------------------------------------------------------------
// Cmd_AddSearchDir: add the custom defined path to an array that we will add to the search paths
//-----------------------------------------------------------------------------
void Cmd_AddSearchDir() {
    GetToken(false);
    if (!g_StudioMdlContext.quiet) {
        printf("New search path: %s\n", token);
    }
    CmdLib_AddNewSearchPath(token);
}

//-----------------------------------------------------------------------------
// Cmd_Illumposition
//-----------------------------------------------------------------------------
void Cmd_Illumposition() {
    if (!GetToken(false)) {
        MdlError("Missing illumposition <x> coordinate");
    }
    g_studiomdl.illumposition[0] = verify_atof(token);

    if (!GetToken(false)) {
        MdlError("Missing illumposition <y> coordinate");
    }
    g_studiomdl.illumposition[1] = verify_atof(token);

    if (!GetToken(false)) {
        MdlError("Missing illumposition <z> coordinate");
    }
    g_studiomdl.illumposition[2] = verify_atof(token);

    if (TokenAvailable()) {
        GetToken(false);

        auto &attachment = g_studiomdl.attachments.emplace_back(s_attachment_t{
            .name = "__illumPosition",
            .bonename = token,
            .type = IS_RIGID,
        });

        AngleMatrix(QAngle(0, 0, 0), g_studiomdl.illumposition, attachment.local);

        g_studiomdl.illumpositionattachment = g_studiomdl.attachments.size();
        // No -1 because 0 is used as no illumposiotion attachment
    }
    else {
        g_studiomdl.illumpositionattachment = 0;

        // rotate points into frame of reference so
        // g_studiomdl.model points down the positive x axis
        // FIXME: these coords are bogus
        float flTemp = g_studiomdl.illumposition[0];
        g_studiomdl.illumposition[0] = -g_studiomdl.illumposition[1];
        g_studiomdl.illumposition[1] = flTemp;
    }

    g_studiomdl.illumpositionset = true;
}

//-----------------------------------------------------------------------------
// Parse Cmd_Modelname
//-----------------------------------------------------------------------------
static void Cmd_Modelname() {
    if (!GetToken(false)) {
        MdlError("Missing <path> argument for $modelname");
        return;
    }

    if (token[0] == '/' || token[0] == '\\') {
        MdlWarning("$modelname key has slash as first character. Removing.\n");
        ProcessModelName(&token[1]);
    }
    else {
        ProcessModelName(token);
    }
}

static void Cmd_InternalName() {
    if (!GetToken(false)) {
        MdlError("Missing <path> argument for $internalname");
        return;
    }
    Q_strncpy(g_studiomdl.szInternalName, token, sizeof(g_studiomdl.szInternalName));
}


static void Cmd_PreserveTriangleOrder() {
    g_StudioMdlContext.preserveTriangleOrder = true;
}

static void Cmd_Autocenter() {
    g_studiomdl.centerstaticprop = true;
}


//-----------------------------------------------------------------------------
// Parse + process the studio options from a .qc file
//-----------------------------------------------------------------------------
void Option_Studio(s_model_t *pmodel) {
    CDmeSourceSkin *pSourceSkin = CreateElement<CDmeSourceSkin>("", DMFILEID_INVALID);

    // Set defaults
    pSourceSkin->m_flScale = g_studiomdl.defaultscale;
    pSourceSkin->m_bQuadSubd = false;
    pSourceSkin->m_bFlipTriangles = false;

    if (ParseOptionStudio(pSourceSkin)) {
        ProcessOptionStudio(pmodel, pSourceSkin->GetRelativeFileName(), pSourceSkin->m_flScale,
                            pSourceSkin->m_bFlipTriangles, pSourceSkin->m_bQuadSubd);
    }
    DestroyElement(pSourceSkin);
}

int Option_Blank() {
    g_studiomdl.model[g_studiomdl.nummodels] = (s_model_t *) calloc(1, sizeof(s_model_t));

    auto *source = (s_source_t *) calloc(1, sizeof(s_source_t));
    g_studiomdl.sources.emplace_back(source);

    g_studiomdl.model[g_studiomdl.nummodels]->source = source;

    g_studiomdl.bodypart[g_studiomdl.numbodyparts].pmodel[g_studiomdl.bodypart[g_studiomdl.numbodyparts].nummodels] =
            g_studiomdl.model[g_studiomdl.nummodels];

    strcpyn(g_studiomdl.model[g_studiomdl.nummodels]->name, "blank");

    g_studiomdl.bodypart[g_studiomdl.numbodyparts].nummodels++;
    g_studiomdl.nummodels++;
    return 0;
}

void Cmd_Bodygroup() {
    FunctionTimingRecorder _("Parse");
    int is_started = 0;

    if (!GetToken(false))
        return;

    g_studiomdl.bodypart[g_studiomdl.numbodyparts].base = 1;
    if (g_studiomdl.numbodyparts != 0) {
        g_studiomdl.bodypart[g_studiomdl.numbodyparts].base =
                g_studiomdl.bodypart[g_studiomdl.numbodyparts - 1].base * g_studiomdl.bodypart[
                    g_studiomdl.numbodyparts - 1].nummodels;
    }
    strcpyn(g_studiomdl.bodypart[g_studiomdl.numbodyparts].name, token);

    do {
        GetToken(true);
        if (endofscript)
            return;
        if (token[0] == '{') {
            is_started = 1;
        }
        else if (token[0] == '}') {
            break;
        }
        else if (stricmp("studio", token) == 0) {
            g_studiomdl.model[g_studiomdl.nummodels] = (s_model_t *) calloc(1, sizeof(s_model_t));
            g_studiomdl.bodypart[g_studiomdl.numbodyparts].pmodel[g_studiomdl.bodypart[g_studiomdl.numbodyparts].
                nummodels] = g_studiomdl.model[g_studiomdl.nummodels];
            g_studiomdl.bodypart[g_studiomdl.numbodyparts].nummodels++;

            Option_Studio(g_studiomdl.model[g_studiomdl.nummodels]);

            // Body command should add any flex commands in the source loaded
            PostProcessSource(g_studiomdl.model[g_studiomdl.nummodels]->source, g_studiomdl.nummodels);

            g_studiomdl.nummodels++;
        }
        else if (stricmp("blank", token) == 0) {
            Option_Blank();
        }
        else {
            MdlError("unknown bodygroup option: \"%s\"\n", token);
        }
    } while (1);

    g_studiomdl.numbodyparts++;
}

void Cmd_AppendBlankBodygroup() {
    // stick a blank bodygroup on the end of the current part
    g_studiomdl.numbodyparts--;
    Option_Blank();
    g_studiomdl.numbodyparts++;
    return;
}

void Cmd_BodygroupPreset() {
    if (!GetToken(false)) {
        MdlError("Error: missing bodygroup <preset name>.\n");
        return;
    }

    // make sure this name is unused
    for (int i = 0; i < g_studiomdl.numbodygrouppresets; i++) {
        if (!V_strcmp(token, g_studiomdl.bodygrouppresets[i].name)) {
            MdlError("Error: bodygroup preset \"%s\" already exists.\n", token);
            return;
        }
    }

    s_bodygrouppreset_t newpreset;
    V_strcpy_safe(newpreset.name, token);

    int nAccumValue = 0;
    int nAccumMask = 0;

    do {
        GetToken(true);

        if (endofscript) {
            return;
        }
        else if (token[0] == '{') {
        }
        else if (token[0] == '}') {
            break;
        }
        else {
            //gather up name:value pairs into a baked bodygroup value and mask

            bool bFoundPart = false;
            for (int i = 0; i < g_studiomdl.numbodyparts; i++) {
                if (!V_strcmp(g_studiomdl.bodypart[i].name, token)) {
                    GetToken(true);

                    int iValue = atoi(token);
                    if (iValue >= 0 && iValue < g_studiomdl.bodypart[i].nummodels) {
                        int iCurrentVal = (nAccumValue / g_studiomdl.bodypart[i].base) % g_studiomdl.bodypart[i].
                                          nummodels;
                        nAccumValue = (nAccumValue - (iCurrentVal * g_studiomdl.bodypart[i].base) +
                                       (iValue * g_studiomdl.bodypart[i].base));

                        int iCurrentMask = (nAccumMask / g_studiomdl.bodypart[i].base) % g_studiomdl.bodypart[i].
                                           nummodels;
                        nAccumMask = (nAccumMask - (iCurrentMask * g_studiomdl.bodypart[i].base) + (
                                          1 * g_studiomdl.bodypart[i].base));
                    }
                    else {
                        MdlError(
                            "Error: can't assign value \"%i\" to bodygroup preset \"%s\" (out of available range).\n",
                            iValue, newpreset.name.c_str());
                        return;
                    }

                    bFoundPart = true;
                }
            }

            if (!bFoundPart) {
                MdlError("Error: can't find any bodygroups named \"%s\".\n", token);
                return;
            }
        }
    } while (1);

    newpreset.iValue = nAccumValue;
    newpreset.iMask = nAccumMask;

    //Msg( "Built bodygroup preset: %s, value: %i, mask:%i\n", newpreset.name, newpreset.iValue, newpreset.iMask );

    g_studiomdl.bodygrouppresets.AddToTail(newpreset);
    g_studiomdl.numbodygrouppresets++;
}

//-----------------------------------------------------------------------------
// Parse the body command from a .qc file
//-----------------------------------------------------------------------------
void Cmd_Body() {
    FunctionTimingRecorder _("Parse");
    if (!GetToken(false)) {
        MdlError("Error: missing body <name>.\n");
        return;
    }

    CDmeSourceSkin *pSourceSkin = CreateElement<CDmeSourceSkin>("", DMFILEID_INVALID);

    // Set defaults
    pSourceSkin->m_flScale = g_studiomdl.defaultscale;

    pSourceSkin->m_SkinName = token;
    if (ParseOptionStudio(pSourceSkin)) {
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
        if (g_studiomdl.attachments.size() >= g_studiomdl.attachments.capacity()) {
            MdlWarning("Too Many Attachments (Max %d), Ignoring Attachment %s:%s\n",
                       g_studiomdl.attachments.capacity(), pSource->filename.c_str(),
                       pSource->m_Attachments[i].name.c_str());
            continue;;
        }
        g_studiomdl.attachments.emplace_back(pSource->m_Attachments[i]);
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
            if ((g_studiomdl.flexcontrollers.size() + 1) >= g_studiomdl.flexcontrollers.capacity())
                MdlError("Line %d: Too many flex controllers, max %d, cannot add split control %s from source %s",
                         g_StudioMdlContext.iLinecount, g_studiomdl.flexcontrollers.capacity(),
                         remapControl.m_Name.Get(),
                         pSource->filename.c_str());


            int nLen = remapControl.m_Name.Length();
            char *pTemp = (char *) _alloca(nLen + 7); // 'left_' && 'right_'

            memcpy(pTemp + 6, remapControl.m_Name.Get(), nLen + 1);
            memcpy(pTemp, "right_", 6);
            pTemp[nLen + 6] = '\0';

            r2i[i] = remapControl.m_RightIndex = g_studiomdl.flexcontrollers.size();
            {
                auto &pController = g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
                    .name = pTemp,
                    .type = pTemp
                });

                if (remapControl.m_RemapType == FLEXCONTROLLER_REMAP_2WAY ||
                    remapControl.m_RemapType == FLEXCONTROLLER_REMAP_EYELID) {
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

            l2i[i] = remapControl.m_LeftIndex = g_studiomdl.flexcontrollers.size();
            {
                auto &pController = g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
                    .name = pTemp,
                    .type = pTemp
                });

                if (remapControl.m_RemapType == FLEXCONTROLLER_REMAP_2WAY ||
                    remapControl.m_RemapType == FLEXCONTROLLER_REMAP_EYELID) {
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
            if (g_studiomdl.flexcontrollers.size() >= g_studiomdl.flexcontrollers.capacity())
                MdlError("Line %d: Too many flex controllers, max %d, cannot add control %s from source %s",
                         g_StudioMdlContext.iLinecount, g_studiomdl.flexcontrollers.capacity(),
                         remapControl.m_Name.Get(),
                         pSource->filename.c_str());

            r2i[i] = l2i[i] = remapControl.m_Index = g_studiomdl.flexcontrollers.size();
            s_flexcontroller_t &pController = g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = remapControl.m_Name.Get(),
                .type = remapControl.m_Name.Get()
            });

            if (remapControl.m_RemapType == FLEXCONTROLLER_REMAP_2WAY ||
                remapControl.m_RemapType == FLEXCONTROLLER_REMAP_EYELID) {
                pController.min = -1.0f;
                pController.max = 1.0f;
            }
            else {
                pController.min = 0.0f;
                pController.max = 1.0f;
            }
        }

        if (remapControl.m_RemapType == FLEXCONTROLLER_REMAP_NWAY ||
            remapControl.m_RemapType == FLEXCONTROLLER_REMAP_EYELID) {
            if (g_studiomdl.flexcontrollers.size() >= g_studiomdl.flexcontrollers.capacity())
                MdlError(
                    "Line %d: Too many flex controllers, max %d, cannot add value control for nWay %s from source %s",
                    g_StudioMdlContext.iLinecount, g_studiomdl.flexcontrollers.capacity(), remapControl.m_Name.Get(),
                    pSource->filename.c_str());

            remapControl.m_MultiIndex = g_studiomdl.flexcontrollers.size();

            const int nLen = remapControl.m_Name.Length();
            char *pTemp = (char *) _alloca(nLen + 6 + 1); // 'multi_' + 1 for the NULL

            memcpy(pTemp, "multi_", 6);
            memcpy(pTemp + 6, remapControl.m_Name.Get(), nLen + 1);
            pTemp[nLen + 6] = '\0';

            g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = pTemp,
                .type = pTemp,
                .min = -1.0f,
                .max = 1.0f
            });
        }

        if (remapControl.m_RemapType == FLEXCONTROLLER_REMAP_EYELID) {
            // Make a blink controller

            if (g_studiomdl.flexcontrollers.size() >= g_studiomdl.flexcontrollers.capacity())
                MdlError(
                    "Line %d: Too many flex controllers, max %d, cannot add value control for nWay %s from source %s",
                    g_StudioMdlContext.iLinecount, g_studiomdl.flexcontrollers.capacity(), remapControl.m_Name.Get(),
                    pSource->filename.c_str());

            remapControl.m_BlinkController = g_studiomdl.flexcontrollers.size();
            g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
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
        g_StudioMdlContext.FlexControllerRemap.emplace_back();
        s_flexcontrollerremap_t &remap = g_StudioMdlContext.FlexControllerRemap.back();
        remap = pSource->m_FlexControllerRemaps[i];
    }
}

//-----------------------------------------------------------------------------
// Process a body command
//-----------------------------------------------------------------------------
void AddBodyFlexData(s_source_t *pSource, int imodel, bool bAddControllers) {
    pSource->m_nKeyStartIndex = g_studiomdl.flexkeys.size();

    // Add flex keys
    int nCount = pSource->m_FlexKeys.Count();
    for (int i = 0; i < nCount; ++i) {
        s_flexkey_t &key = pSource->m_FlexKeys[i];

        if (g_studiomdl.flexkeys.size() >= g_studiomdl.flexkeys.capacity())
            MdlError("Line %d: Too many flex keys, max %d, cannot add flexKey %s from source %s",
                     g_StudioMdlContext.iLinecount, MAXSTUDIOFLEXKEYS, key.animationname.c_str(),
                     pSource->filename.c_str());

        auto &new_flex = g_studiomdl.flexkeys.emplace(key);
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


void Option_Eyeball(s_model_t *pmodel) {
    Vector tmp;
    int i, j;
    vstd::FixedString<256> szMeshMaterial;

    s_eyeball_t &eyeball = pmodel->eyeballs.emplace_back();

    // name
    GetToken(false);
    strcpyn(eyeball.name, token);

    // bone name
    GetToken(false);
    for (i = 0; i < pmodel->source->numbones; i++) {
        if (!Q_stricmp(pmodel->source->localBone[i].name, token)) {
            eyeball.bone = i;
            break;
        }
    }
    if (!g_StudioMdlContext.createMakefile && i >= pmodel->source->numbones) {
        TokenError("unknown eyeball bone \"%s\"\n", token);
    }

    // X
    GetToken(false);
    tmp[0] = verify_atof(token);

    // Y
    GetToken(false);
    tmp[1] = verify_atof(token);

    // Z
    GetToken(false);
    tmp[2] = verify_atof(token);

    // mesh material
    GetToken(false);
    szMeshMaterial = token;
    int mesh_material = UseTextureAsMaterial(LookupTexture(szMeshMaterial));

    // diameter
    GetToken(false);
    eyeball.radius = verify_atof(token) / 2.0;

    // Z angle offset
    GetToken(false);
    eyeball.zoffset = tan(DEG2RAD(verify_atof(token)));

    // iris material (no longer used, but we need to remove the token)
    GetToken(false);

    // pupil scale
    GetToken(false);
    eyeball.iris_scale = 1.0 / verify_atof(token);

    VectorCopy(tmp, eyeball.org);

    for (i = 0; i < pmodel->source->nummeshes; i++) {
        j = pmodel->source->meshindex[i]; // meshes are internally stored by material index

        if (j == mesh_material) {
            eyeball.mesh = i; // FIXME: should this be pre-adjusted?
            break;
        }
    }

    if (!g_StudioMdlContext.createMakefile && i >= pmodel->source->nummeshes) {
        TokenError("can't find eyeball texture \"%s\" on model\n", szMeshMaterial.c_str());
    }

    // translate eyeball into bone space
    VectorITransform(tmp, pmodel->source->boneToPose[eyeball.bone], eyeball.org);

    matrix3x4_t vtmp;
    AngleMatrix(g_studiomdl.defaultrotation, vtmp);

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
    if (g_studiomdl.flexkeys.size() > 0) {
        MdlError("%s: Flexes already defined.  vcafile can be only flex option in $model block\n", __FUNCTION__);
        return;
    }

    s_source_t *pSource = g_studiomdl.model[nModelIndex]->source;
    s_source_t *pVtaSource = Load_Source(pszVtaFile, "vta");

    if (pVtaSource->m_Animations.Count() <= 0) {
        MdlError("%s: No animations in VertexCacheAnimationFile \"%s\"\n", __FUNCTION__, pszVtaFile);
        return;
    }

    {
        g_studiomdl.flexkeys.emplace(s_flexkey_t{
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
            g_studiomdl.flexkeys.emplace(s_flexkey_t{
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

    flexRemap.m_RemapType = FLEXCONTROLLER_REMAP_NWAY;
    flexRemap.m_bIsStereo = false;
    flexRemap.m_Index = -1; // Don't know this right now
    flexRemap.m_LeftIndex = -1; // Don't know this right now
    flexRemap.m_RightIndex = -1; // Don't know this right now
    flexRemap.m_MultiIndex = -1; // Don't know this right now
    flexRemap.m_EyesUpDownFlexController = -1;
    flexRemap.m_BlinkController = -1;

    char szBuf[MAX_PATH];
    V_FileBase(pVtaSource->filename, szBuf, ARRAYSIZE(szBuf));
    flexRemap.m_Name = szBuf;

    for (int i = 0; i < nActualFrameCount; ++i) {
        sTmp.sprintf("f%d", i);
        flexRemap.m_RawControls.emplace_back(sTmp.Access());
    }

    for (int i = 0; i < flexRemap.m_RawControls.size(); ++i) {
        int nFlexKey = -1;
        for (int j = 0; j < g_studiomdl.flexkeys.size(); ++j) {
            if (!V_stricmp(g_studiomdl.flexdescs[g_studiomdl.flexkeys[j].flexdesc].FACS,
                           flexRemap.m_RawControls[i].Get())) {
                nFlexKey = j;
                break;
            }
        }

        if (nFlexKey < 0) {
            MdlError("%s Cannot find flex to group \"%s\"\n", __FUNCTION__, flexRemap.m_RawControls[i].Get());
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


void Option_Spherenormals(s_source_t *psource) {
    Vector pos;
    int i, j;
    int mesh_material;
    char szMeshMaterial[256];

    // mesh material
    GetToken(false);
    strcpyn(szMeshMaterial, token);
    mesh_material = UseTextureAsMaterial(LookupTexture(token));

    // X
    GetToken(false);
    pos[0] = verify_atof(token);

    // Y
    GetToken(false);
    pos[1] = verify_atof(token);

    // Z
    GetToken(false);
    pos[2] = verify_atof(token);

    for (i = 0; i < psource->nummeshes; i++) {
        j = psource->meshindex[i]; // meshes are internally stored by material index

        if (j == mesh_material) {
            s_vertexinfo_t *vertex = &psource->vertex[psource->mesh[i].vertexoffset];

            for (int k = 0; k < psource->mesh[i].numvertices; k++) {
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

    if (i >= psource->nummeshes) {
        TokenError("can't find spherenormal texture \"%s\" on model\n", szMeshMaterial);
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

        if (!Q_stricmp(pszVertexAnimName, pSourceAnim->animationname))
            return pSourceAnim;
    }

    return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose:   Handle the eyelid option using a DMX instead of a VTA source
// QC Syntax: dmxeyelid <upper|lower> <source> lowerer <delta> <-0.20 neutral F00 0.19 raiser F02 0.28 righteyeball righteye lefteyeball lefteye
// e.g: dmxeyelid upper "coach_model_merged.dmx" lowerer F01 -0.20 neutral F00 0.19 raiser F02 0.28 righteyeball righteye lefteyeball lefteye
//-----------------------------------------------------------------------------
void Option_DmxEyelid(int imodel) {
    // upper | lower
    const char *pszType = nullptr;
    GetToken(false);
    if (!Q_stricmp("upper", token)) {
        pszType = "upper";
    }
    else if (!Q_stricmp("lower", token)) {
        pszType = "lower";
    }
    else {
        TokenError("$model dmxeyelid, expected one of \"upper\", \"lower\"");
        return;
    }

    // (exr)
    CUtlString sSourceFile;
    GetToken(false);
    sSourceFile = token;

    s_source_t *pSource = Load_Source(sSourceFile.Get(), "dmx");
    if (!pSource) {
        MdlError("(%d) : %s:  Cannot load source file \"%s\"\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str(), sSourceFile.Get());
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

    while (TokenAvailable()) {
        GetToken(false);
        bool bTokenHandled = false;

        for (int i = 0; i < kEyelidTypeCount; ++i) {
            if (!Q_stricmp(token, eyelidData[i].m_pszSuffix)) {
                bTokenHandled = true;

                GetToken(false);
                eyelidData[i].m_pSourceAnim = GetNewStyleSourceVertexAnim(pSource, token);
                if (eyelidData[i].m_pSourceAnim == nullptr) {
                    MdlError("(%d) : %s:  No DMX vertex animation named \"%s\" in source \"%s\"\n",
                             g_StudioMdlContext.iLinecount,
                             g_StudioMdlContext.szLine.c_str(), token, sSourceFile.Get());
                    return;
                }

                // target
                GetToken(false);
                eyelidData[i].m_flTarget = verify_atof(token);

                break;
            }
        }

        if (bTokenHandled)
            continue;

        else if (!Q_stricmp(token, "righteyeball")) {
            GetToken(false);
            sRightEyeball = token;
        }
        else if (!Q_stricmp(token, "lefteyeball")) {
            GetToken(false);
            sLeftEyeball = token;
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
        s_flexkey_t *pFlexKey = &g_studiomdl.flexkeys.emplace_back(s_flexkey_t{
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

    s_model_t *pModel = g_studiomdl.model[imodel];
    for (int i = 0; i < pModel->eyeballs.size(); ++i) {
        s_eyeball_t *pEyeball = &(pModel->eyeballs[i]);
        if (!pEyeball)
            continue;

        RightLeftType_t nRightLeftIndex = kRight;
        if (!Q_stricmp(sRightEyeball, pEyeball->name)) {
            nRightLeftIndex = kRight;
            bRightOk = true;
        }
        else if (!Q_stricmp(sLeftEyeball, pEyeball->name)) {
            nRightLeftIndex = kLeft;
            bLeftOk = true;
        }
        else {
            MdlWarning("Unknown Eyeball: %s\n", pEyeball->name.c_str());
            continue;
        }

        for (int j = 0; j < kEyelidTypeCount; ++j) {
            if (fabs(eyelidData[j].m_flTarget) > pEyeball->radius) {
                TokenError("Eyelid \"%s\" %s %.1f out of range (+-%.1f)\n", pszType, eyelidData[j].m_pszSuffix,
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
        TokenError("Could not find right eye \"%s\"\n", sRightEyeball.Get());
    }

    if (!bLeftOk) {
        TokenError("Could not find left eye \"%s\"\n", sRightEyeball.Get());
    }
}

int Option_Mouth(s_model_t *pmodel) {
    // index
    GetToken(false);
    int index = verify_atoi(token);
    if (index >= g_studiomdl.nummouths)
        g_studiomdl.nummouths = index + 1;

    // flex controller name
    GetToken(false);
    g_studiomdl.mouth[index].flexdesc = Add_Flexdesc(token);

    // bone name
    GetToken(false);
    strcpyn(g_studiomdl.mouth[index].bonename, token);

    // vector
    GetToken(false);
    g_studiomdl.mouth[index].forward[0] = verify_atof(token);
    GetToken(false);
    g_studiomdl.mouth[index].forward[1] = verify_atof(token);
    GetToken(false);
    g_studiomdl.mouth[index].forward[2] = verify_atof(token);
    return 0;
}

void Option_Flexcontroller(s_model_t *pmodel) {
    char type[256];
    float range_min = 0.0f;
    float range_max = 1.0f;

    // g_flex
    GetToken(false);
    strcpy(type, token);

    while (TokenAvailable()) {
        GetToken(false);

        if (stricmp(token, "range") == 0) {
            GetToken(false);
            range_min = verify_atof(token);

            GetToken(false);
            range_max = verify_atof(token);
        }
        else {
            if (g_studiomdl.flexcontrollers.size() >= g_studiomdl.flexcontrollers.capacity()) {
                TokenError("Too many flex controllers, max %d\n", g_studiomdl.flexcontrollers.capacity());
            }

            g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = token,
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
    g_studiomdl.flexcontrollers.clear();
    pSource->bNoAutoDMXRules = true;
}

//-----------------------------------------------------------------------------
// Purpose: throw away all the options for a specific sequence or animation
//-----------------------------------------------------------------------------

int ParseEmpty() {
    int depth = 0;

    while (1) {
        if (depth > 0) {
            if (!GetToken(true)) {
                break;
            }
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            else {
                GetToken(false);
            }
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return 1;
        }
        if (stricmp("{", token) == 0) {
            depth++;
        }
        else if (stricmp("}", token) == 0) {
            depth--;
        }

        if (depth < 0) {
            TokenError("missing {\n");
        }
    }

    return 0;
}

void Option_Flex(char *name, char *vtafile, int imodel, float pairsplit) {
    if (g_studiomdl.flexkeys.size() >= MAXSTUDIOFLEXKEYS) {
        TokenError("Too many flexes, max %d\n", MAXSTUDIOFLEXKEYS);
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
    auto &new_flexkey = g_studiomdl.flexkeys.emplace(s_flexkey_t{
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

    const auto current_count = g_studiomdl.flexkeys.size() - 1;

    while (TokenAvailable()) {
        GetToken(false);

        if (stricmp(token, "frame") == 0) {
            GetToken(false);

            new_flexkey.frame = verify_atoi(token);
        }
        else if (stricmp(token, "position") == 0) {
            GetToken(false);
            new_flexkey.target1 = verify_atof(token);
        }
        else if (stricmp(token, "split") == 0) {
            GetToken(false);
            new_flexkey.split = verify_atof(token);
        }
        else if (stricmp(token, "decay") == 0) {
            GetToken(false);
            new_flexkey.decay = verify_atof(token);
        }
        else {
            TokenError("unknown option: %s", token);
        }
    }

    if (current_count > 1) {
        if (g_studiomdl.flexkeys[current_count - 1].flexdesc == new_flexkey.flexdesc) {
            g_studiomdl.flexkeys[current_count - 1].target2 = g_studiomdl.flexkeys[current_count - 1].target1;
            g_studiomdl.flexkeys[current_count - 1].target3 = new_flexkey.target1;
            new_flexkey.target0 = g_studiomdl.flexkeys[current_count - 1].target1;
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
    printf("%s = ", g_studiomdl.flexdescs[pRule->flex].FACS.c_str());
    for (int i = 0; i < pRule->numops; i++) {
        switch (pRule->op[i].op) {
            case STUDIO_CONST:
                printf("%f ", pRule->op[i].d.value);
                break;
            case STUDIO_FETCH1:
                printf("%s ", g_studiomdl.flexcontrollers[pRule->op[i].d.index].name.c_str());
                break;
            case STUDIO_FETCH2:
                printf("[%d] ", pRule->op[i].d.index);
                break;
            case STUDIO_ADD:
                printf("+ ");
                break;
            case STUDIO_SUB:
                printf("- ");
                break;
            case STUDIO_MUL:
                printf("* ");
                break;
            case STUDIO_DIV:
                printf("/ ");
                break;
            case STUDIO_NEG:
                printf("neg ");
                break;
            case STUDIO_MAX:
                printf("max ");
                break;
            case STUDIO_MIN:
                printf("min ");
                break;
            case STUDIO_COMMA:
                printf(", ");
                break; // error
            case STUDIO_OPEN:
                printf("( ");
                break; // error
            case STUDIO_CLOSE:
                printf(") ");
                break; // error
            case STUDIO_2WAY_0:
                printf("2WAY_0 ");
                break;
            case STUDIO_2WAY_1:
                printf("2WAY_1 ");
                break;
            case STUDIO_NWAY:
                printf("NWAY ");
                break;
            case STUDIO_COMBO:
                printf("COMBO ");
                break;
            case STUDIO_DOMINATE:
                printf("DOMINATE ");
                break;
            case STUDIO_DME_LOWER_EYELID:
                printf("DME_LOWER_EYELID ");
                break;
            case STUDIO_DME_UPPER_EYELID:
                printf("DME_UPPER_EYELID ");
                break;
            default:
                printf("err%d ", pRule->op[i].op);
                break;
        }
    }
    printf("\n");
}


void Option_Flexrule(s_model_t * /* pmodel */, const char *name) {
    int precedence[32];
    precedence[STUDIO_CONST] = 0;
    precedence[STUDIO_FETCH1] = 0;
    precedence[STUDIO_FETCH2] = 0;
    precedence[STUDIO_ADD] = 1;
    precedence[STUDIO_SUB] = 1;
    precedence[STUDIO_MUL] = 2;
    precedence[STUDIO_DIV] = 2;
    precedence[STUDIO_NEG] = 4;
    precedence[STUDIO_EXP] = 3;
    precedence[STUDIO_OPEN] = 0; // only used in token parsing
    precedence[STUDIO_CLOSE] = 0;
    precedence[STUDIO_COMMA] = 0;
    precedence[STUDIO_MAX] = 5;
    precedence[STUDIO_MIN] = 5;

    s_flexop_t stream[MAX_OPS] = {}; // operators have no operand: d must not carry stack garbage into the MDL
    int i = 0;
    s_flexop_t stack[MAX_OPS] = {};
    int j = 0;
    int k = 0;

    if (g_studiomdl.flexrules.size() + 1 > g_studiomdl.flexrules.capacity()) {
        TokenError("Too many flex rules (max %d)\n", g_studiomdl.flexrules.capacity());
    }
    auto &pRule = g_studiomdl.flexrules.emplace_back();

    int flexdesc;
    for (flexdesc = 0; flexdesc < g_studiomdl.flexdescs.size(); flexdesc++) {
        if (stricmp(name, g_studiomdl.flexdescs[flexdesc].FACS) == 0) {
            break;
        }
    }

    if (flexdesc >= g_studiomdl.flexdescs.size()) {
        TokenError("Rule for unknown flex %s\n", name);
    }

    pRule.flex = flexdesc;
    pRule.numops = 0;

    // =
    GetToken(false);

    // parse all the tokens
    bool linecontinue = false;
    while (linecontinue || TokenAvailable()) {
        GetExprToken(linecontinue);

        linecontinue = false;

        if (token[0] == '\\') {
            if (!GetToken(false) || token[0] != '\\') {
                TokenError("unknown expression token '\\%s\n", token);
            }
            linecontinue = true;
        }
        else if (token[0] == '(') {
            stream[i++].op = STUDIO_OPEN;
        }
        else if (token[0] == ')') {
            stream[i++].op = STUDIO_CLOSE;
        }
        else if (token[0] == '+') {
            stream[i++].op = STUDIO_ADD;
        }
        else if (token[0] == '-') {
            stream[i].op = STUDIO_SUB;
            if (i > 0) {
                switch (stream[i - 1].op) {
                    case STUDIO_OPEN:
                    case STUDIO_ADD:
                    case STUDIO_SUB:
                    case STUDIO_MUL:
                    case STUDIO_DIV:
                    case STUDIO_COMMA:
                        // it's a unary if it's preceded by a "(+-*/,"?
                        stream[i].op = STUDIO_NEG;
                        break;
                }
            }
            i++;
        }
        else if (token[0] == '*') {
            stream[i++].op = STUDIO_MUL;
        }
        else if (token[0] == '/') {
            stream[i++].op = STUDIO_DIV;
        }
        else if (V_isdigit(token[0])) {
            stream[i].op = STUDIO_CONST;
            stream[i++].d.value = verify_atof(token);
        }
        else if (token[0] == ',') {
            stream[i++].op = STUDIO_COMMA;
        }
        else if (stricmp(token, "max") == 0) {
            stream[i++].op = STUDIO_MAX;
        }
        else if (stricmp(token, "min") == 0) {
            stream[i++].op = STUDIO_MIN;
        }
        else {
            if (token[0] == '%') {
                GetExprToken(false);

                for (k = 0; k < g_studiomdl.flexdescs.size(); k++) {
                    if (stricmp(token, g_studiomdl.flexdescs[k].FACS) == 0) {
                        stream[i].op = STUDIO_FETCH2;
                        stream[i++].d.index = k;
                        break;
                    }
                }
                if (k >= g_studiomdl.flexdescs.size()) {
                    TokenError("unknown flex %s\n", token);
                }
            }
            else {
                for (k = 0; k < g_studiomdl.flexcontrollers.size(); k++) {
                    if (stricmp(token, g_studiomdl.flexcontrollers[k].name.c_str()) == 0) {
                        stream[i].op = STUDIO_FETCH1;
                        stream[i++].d.index = k;
                        break;
                    }
                }
                if (k >= g_studiomdl.flexcontrollers.size()) {
                    TokenError("unknown controller %s\n", token);
                }
            }
        }
    }

    if (i > MAX_OPS) {
        TokenError("expression %s too complicated\n", g_studiomdl.flexdescs[pRule.flex].FACS.c_str());
    }

    if (0) {
        printf("%s = ", g_studiomdl.flexdescs[pRule.flex].FACS.c_str());
        for (k = 0; k < i; k++) {
            switch (stream[k].op) {
                case STUDIO_CONST:
                    printf("%f ", stream[k].d.value);
                    break;
                case STUDIO_FETCH1:
                    printf("%s ", g_studiomdl.flexcontrollers[stream[k].d.index].name.c_str());
                    break;
                case STUDIO_FETCH2:
                    printf("[%d] ", stream[k].d.index);
                    break;
                case STUDIO_ADD:
                    printf("+ ");
                    break;
                case STUDIO_SUB:
                    printf("- ");
                    break;
                case STUDIO_MUL:
                    printf("* ");
                    break;
                case STUDIO_DIV:
                    printf("/ ");
                    break;
                case STUDIO_NEG:
                    printf("neg ");
                    break;
                case STUDIO_MAX:
                    printf("max ");
                    break;
                case STUDIO_MIN:
                    printf("min ");
                    break;
                case STUDIO_COMMA:
                    printf(", ");
                    break; // error
                case STUDIO_OPEN:
                    printf("( ");
                    break; // error
                case STUDIO_CLOSE:
                    printf(") ");
                    break; // error
                default:
                    printf("err%d ", stream[k].op);
                    break;
            }
        }
        printf("\n");
        // exit(1);
    }

    j = 0;
    for (k = 0; k < i; k++) {
        if (j >= MAX_OPS) {
            TokenError("expression %s too complicated\n", g_studiomdl.flexdescs[pRule.flex].FACS.c_str());
        }
        switch (stream[k].op) {
            case STUDIO_CONST:
            case STUDIO_FETCH1:
            case STUDIO_FETCH2:
                pRule.op[pRule.numops++] = stream[k];
                break;
            case STUDIO_OPEN:
                stack[j++] = stream[k];
                break;
            case STUDIO_CLOSE:
                // pop all operators off of the stack until an open paren
                while (j > 0 && stack[j - 1].op != STUDIO_OPEN) {
                    pRule.op[pRule.numops++] = stack[j - 1];
                    j--;
                }
                if (j == 0) {
                    TokenError("unmatched closed parentheses\n");
                }
                if (j > 0)
                    j--;
                break;
            case STUDIO_COMMA:
                // pop all operators off of the stack until an open paren
                while (j > 0 && stack[j - 1].op != STUDIO_OPEN) {
                    pRule.op[pRule.numops++] = stack[j - 1];
                    j--;
                }
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
            case STUDIO_ADD:
            case STUDIO_SUB:
            case STUDIO_MUL:
            case STUDIO_DIV:
                // pop all operators off of the stack that have equal or higher precedence
                while (j > 0 && precedence[stream[k].op] <= precedence[stack[j - 1].op]) {
                    pRule.op[pRule.numops++] = stack[j - 1];
                    j--;
                }
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
            case STUDIO_NEG:
                if (stream[k + 1].op == STUDIO_CONST) {
                    // change sign of constant, skip op
                    stream[k + 1].d.value = -stream[k + 1].d.value;
                }
                else {
                    // push operator onto the stack
                    stack[j++] = stream[k];
                }
                break;
            case STUDIO_MAX:
            case STUDIO_MIN:
                // push operator onto the stack
                stack[j++] = stream[k];
                break;
        }
        if (pRule.numops >= MAX_OPS)
            TokenError("expression for \"%s\" too complicated\n", g_studiomdl.flexdescs[pRule.flex].FACS.c_str());
    }
    // pop all operators off of the stack
    while (j > 0) {
        pRule.op[pRule.numops++] = stack[j - 1];
        j--;
        if (pRule.numops >= MAX_OPS)
            TokenError("expression for \"%s\" too complicated\n", g_studiomdl.flexdescs[pRule.flex].FACS.c_str());
    }

    // reprocess the operands, eating commas for all functions
    int numCommas = 0;
    j = 0;
    for (k = 0; k < pRule.numops; k++) {
        switch (pRule.op[k].op) {
            case STUDIO_MAX:
            case STUDIO_MIN:
                if (pRule.op[j - 1].op != STUDIO_COMMA) {
                    TokenError("missing comma\n");
                }
                // eat the comma operator
                numCommas--;
                pRule.op[j - 1] = pRule.op[k];
                break;
            case STUDIO_COMMA:
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
        TokenError("too many comma's\n");
    }

    if (pRule.numops > MAX_OPS) {
        TokenError("expression %s too complicated\n", g_studiomdl.flexdescs[pRule.flex].FACS.c_str());
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
        case FLEXCONTROLLER_REMAP_PASSTHRU:
            // Easy As!
            pRule->op[pRule->numops].op = STUDIO_FETCH1;
            pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
            pRule->numops++;
            break;

        case FLEXCONTROLLER_REMAP_EYELID:
            if (remapLocalIndex == 0) {
                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value =
                        remap.m_EyesUpDownFlexController >= 0 ? remap.m_EyesUpDownFlexController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = remap.m_BlinkController >= 0 ? remap.m_BlinkController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = globalFlexControllerIndex; // CloseLid
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_DME_LOWER_EYELID;
                pRule->op[pRule->numops].d.index = remap.m_MultiIndex; // CloseLidV
                pRule->numops++;
            }
            else {
                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value =
                        remap.m_EyesUpDownFlexController >= 0 ? remap.m_EyesUpDownFlexController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = remap.m_BlinkController >= 0 ? remap.m_BlinkController : -1;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = globalFlexControllerIndex; // CloseLid
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_DME_UPPER_EYELID;
                pRule->op[pRule->numops].d.index = remap.m_MultiIndex; // CloseLidV
                pRule->numops++;
            }
            break;

        case FLEXCONTROLLER_REMAP_2WAY:
            // A little trickier... local index 0 is on the left, local index 1 is on the right
            // Left Equivalent RemapVal( -1.0, 0.0, 0.0, 1.0 )
            // Right Equivalent RemapVal( 0.0, 1.0, 0.0, 1.0 )
            if (remapLocalIndex == 0) {
                pRule->op[pRule->numops].op = STUDIO_2WAY_0;
                pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
                pRule->numops++;
            }
            else {
                pRule->op[pRule->numops].op = STUDIO_2WAY_1;
                pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
                pRule->numops++;
            }
            break;

        case FLEXCONTROLLER_REMAP_NWAY: {
            int nRemapCount = remap.m_RawControls.size();
            float flStep = (nRemapCount > 2) ? 2.0f / (nRemapCount - 1) : 0.0f;

            if (remapLocalIndex == 0) {
                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = -11.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = -10.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = -1.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = -1.0f + flStep;
                pRule->numops++;
            }
            else if (remapLocalIndex == nRemapCount - 1) {
                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = 1.0f - flStep;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = 1.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = 10.0f;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = 11.0f;
                pRule->numops++;
            }
            else {
                float flPeak = remapLocalIndex * flStep - 1.0f;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = flPeak - flStep;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = flPeak;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = flPeak;
                pRule->numops++;

                pRule->op[pRule->numops].op = STUDIO_CONST;
                pRule->op[pRule->numops].d.value = flPeak + flStep;
                pRule->numops++;
            }

            pRule->op[pRule->numops].op = STUDIO_CONST;
            pRule->op[pRule->numops].d.value = remap.m_MultiIndex;
            pRule->numops++;

            pRule->op[pRule->numops].op = STUDIO_NWAY;
            pRule->op[pRule->numops].d.index = globalFlexControllerIndex;
            pRule->numops++;
        }
        break;
        default:
            Assert(0);
            // This is an error condition
            pRule->op[pRule->numops].op = STUDIO_CONST;
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
    if (g_studiomdl.flexrules.size() >= g_studiomdl.flexrules.capacity())
        MdlError("Line %d: Too many flex rules, max %d",
                 g_StudioMdlContext.iLinecount, g_studiomdl.flexrules.capacity());

    s_flexrule_t &pRule = g_studiomdl.flexrules.emplace_back(s_flexrule_t{
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
            pRule.op[pRule.numops].op = STUDIO_COMBO;
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

            pRule.op[pRule.numops].op = STUDIO_DOMINATE;
            pRule.op[pRule.numops].d.index = nFactorCount;
            pRule.numops++;
        }
    }
}


void AddBodyFlexRules(s_source_t *pSource) {
    const int nRemapCount = pSource->m_FlexControllerRemaps.Count();
    for (int i = 0; i < nRemapCount; ++i) {
        s_flexcontrollerremap_t &remap = pSource->m_FlexControllerRemaps[i];
        if (remap.m_RemapType == FLEXCONTROLLER_REMAP_EYELID && !remap.m_EyesUpDownFlexName.IsEmpty()) {
            for (int j = 0; j < g_studiomdl.flexcontrollers.size(); ++j) {
                if (!Q_strcmp(g_studiomdl.flexcontrollers[j].name.c_str(), remap.m_EyesUpDownFlexName.Get())) {
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
        s_flexkey_t &flexKey = g_studiomdl.flexkeys[pSource->m_nKeyStartIndex + rule.m_nFlex];
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

void Option_Eyelid(int imodel) {
    char type[256];
    char vtafile[256];

    // type
    GetToken(false);
    strcpyn(type, token);

    // source
    GetToken(false);
    strcpyn(vtafile, token);

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

    basedesc = g_studiomdl.flexdescs.size();
    g_studiomdl.flexdescs.emplace_back(type);

    while (TokenAvailable()) {
        GetToken(false);

        char localdesc[256];
        strcpy(localdesc, type);
        strcat(localdesc, "_");
        strcat(localdesc, token);

        if (stricmp(token, "lowerer") == 0) {
            GetToken(false);
            lowererframe = verify_atoi(token);
            GetToken(false);
            lowerertarget = verify_atof(token);
            lowererdesc = g_studiomdl.flexdescs.size();
            g_studiomdl.flexdescs.emplace_back(localdesc);
        }
        else if (stricmp(token, "neutral") == 0) {
            GetToken(false);
            neutralframe = verify_atoi(token);
            GetToken(false);
            neutraltarget = verify_atof(token);
            neutraldesc = g_studiomdl.flexdescs.size();
            g_studiomdl.flexdescs.emplace_back(localdesc);
        }
        else if (stricmp(token, "raiser") == 0) {
            GetToken(false);
            raiserframe = verify_atoi(token);
            GetToken(false);
            raisertarget = verify_atof(token);
            raiserdesc = g_studiomdl.flexdescs.size();
            g_studiomdl.flexdescs.emplace_back(localdesc);
        }
        else if (stricmp(token, "split") == 0) {
            GetToken(false);
            split = verify_atof(token);
        }
        else if (stricmp(token, "eyeball") == 0) {
            GetToken(false);
            strcpy(szEyeball, token);
        }
        else {
            TokenError("unknown option: %s", token);
        }
    }

    s_source_t *pSource = Load_Source(vtafile, "vta");
    auto &lower_flexkey = g_studiomdl.flexkeys.emplace_back(s_flexkey_t{
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
    g_studiomdl.flexkeys.emplace_back(s_flexkey_t{
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

    g_studiomdl.flexkeys.emplace_back(s_flexkey_t{
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

    s_model_t *pmodel = g_studiomdl.model[imodel];
    for (int i = 0; i < pmodel->eyeballs.size(); i++) {
        s_eyeball_t *peyeball = &(pmodel->eyeballs[i]);

        if (szEyeball[0] != '\0') {
            if (stricmp(peyeball->name, szEyeball) != 0)
                continue;
        }

        if (fabs(lowerertarget) > peyeball->radius) {
            TokenError("Eyelid \"%s\" lowerer out of range (+-%.1f)\n", type, peyeball->radius);
        }
        if (fabs(neutraltarget) > peyeball->radius) {
            TokenError("Eyelid \"%s\" neutral out of range (+-%.1f)\n", type, peyeball->radius);
        }
        if (fabs(raisertarget) > peyeball->radius) {
            TokenError("Eyelid \"%s\" raiser  out of range (+-%.1f)\n", type, peyeball->radius);
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

void Cmd_Model() {
    g_studiomdl.model[g_studiomdl.nummodels] = (s_model_t *) calloc(1, sizeof(s_model_t));

    // name
    if (!GetToken(false)) {
        MdlError("Error: missing <name>.\n");
        return;
    }
    strcpyn(g_studiomdl.model[g_studiomdl.nummodels]->name, token);

    // fake g_studiomdl.bodypart stuff
    g_studiomdl.bodypart[g_studiomdl.numbodyparts].base = 1;
    if (g_studiomdl.numbodyparts != 0) {
        g_studiomdl.bodypart[g_studiomdl.numbodyparts].base =
                g_studiomdl.bodypart[g_studiomdl.numbodyparts - 1].base * g_studiomdl.bodypart[
                    g_studiomdl.numbodyparts - 1].nummodels;
    }
    strcpyn(g_studiomdl.bodypart[g_studiomdl.numbodyparts].name, token);

    g_studiomdl.bodypart[g_studiomdl.numbodyparts].pmodel[g_studiomdl.bodypart[g_studiomdl.numbodyparts].nummodels] =
            g_studiomdl.model[g_studiomdl.nummodels];
    g_studiomdl.bodypart[g_studiomdl.numbodyparts].nummodels = 1;
    g_studiomdl.numbodyparts++;

    Option_Studio(g_studiomdl.model[g_studiomdl.nummodels]);

    if (g_studiomdl.model[g_studiomdl.nummodels]->source) {
        // Body command should add any flex commands in the source loaded
        AddBodyFlexData(g_studiomdl.model[g_studiomdl.nummodels]->source, g_studiomdl.nummodels);
        AddBodyAttachments(g_studiomdl.model[g_studiomdl.nummodels]->source);
    }

    int depth = 0;
    // vtafile is set by "flexfile" and used by the flex/defaultflex lines that follow it: keep it across tokens
    char FAC[256]{}, vtafile[256]{};
    while (1) {
        if (depth > 0) {
            if (!GetToken(true))
                break;
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            else {
                GetToken(false);
            }
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return;
        }
        if (!Q_stricmp("{", token)) {
            depth++;
        }
        else if (!Q_stricmp("}", token)) {
            depth--;
        }
        else if (!Q_stricmp("eyeball", token)) {
            Option_Eyeball(g_studiomdl.model[g_studiomdl.nummodels]);
        }
        else if (!Q_stricmp("eyelid", token)) {
            Option_Eyelid(g_studiomdl.nummodels);
        }
        else if (!Q_stricmp("dmxeyelid", token)) {
            Option_DmxEyelid(g_studiomdl.nummodels);
        }
        else if (!V_stricmp("vcafile", token)) {
            // vertex cache animation file
            GetToken(false); // file
            Option_VertexCacheAnimationFile(token, g_studiomdl.nummodels);
        }
        else if (!Q_stricmp("flex", token)) {
            // g_flex
            GetToken(false);
            strcpy(FAC, token);
            if (depth == 0) {
                // file
                GetToken(false);
                strcpy(vtafile, token);
            }
            Option_Flex(FAC, vtafile, g_studiomdl.nummodels, 0.0);
            // FIXME: this needs to point to a model used, not loaded!!!
        }
        else if (!Q_stricmp("flexpair", token)) {
            // g_flex
            GetToken(false);
            strcpy(FAC, token);

            GetToken(false);
            float split = atof(token);

            if (depth == 0) {
                // file
                GetToken(false);
                strcpy(vtafile, token);
            }
            Option_Flex(FAC, vtafile, g_studiomdl.nummodels, split);
            // FIXME: this needs to point to a model used, not loaded!!!
        }
        else if (!Q_stricmp("defaultflex", token)) {
            if (depth == 0) {
                // file
                GetToken(false);
                strcpy(vtafile, token);
            }

            // g_flex
            // FIXME: this needs to point to a model used, not loaded!!!
            Option_Flex("default", vtafile, g_studiomdl.nummodels, 0.0);
            g_studiomdl.defaultflexkey = &g_studiomdl.flexkeys.back();
        }
        else if (!Q_stricmp("flexfile", token)) {
            // file
            GetToken(false);
            strcpy(vtafile, token);
        }
        else if (!Q_stricmp("localvar", token)) {
            while (TokenAvailable()) {
                GetToken(false);
                Add_Flexdesc(token);
            }
        }
        else if (!Q_stricmp("mouth", token)) {
            Option_Mouth(g_studiomdl.model[g_studiomdl.nummodels]);
        }
        else if (!Q_stricmp("flexcontroller", token)) {
            Option_Flexcontroller(g_studiomdl.model[g_studiomdl.nummodels]);
        }
        else if (token[0] == '%') {
            Option_Flexrule(g_studiomdl.model[g_studiomdl.nummodels], &token[1]);
        }
        else if (!Q_stricmp("attachment", token)) {
            // 	Option_Attachment( g_studiomdl.model[g_studiomdl.nummodels] );
        }
        else if (!Q_stricmp(token, "spherenormals")) {
            Option_Spherenormals(g_studiomdl.model[g_studiomdl.nummodels]->source);
        }
        else if (!Q_stricmp(token, "noautodmxrules")) {
            Option_NoAutoDMXRules(g_studiomdl.model[g_studiomdl.nummodels]->source);
        }
        else {
            TokenError("unknown model option \"%s\"\n", token);
        }

        if (depth < 0) {
            TokenError("missing {\n");
        }
    };

    if (!g_studiomdl.model[g_studiomdl.nummodels]->source->bNoAutoDMXRules) {
        // Actually connect up the expressions between the Dme Flex Controllers & Flex Descriptors
        // In case there was data added by some other eyeball command (like eyelid)
        AddBodyFlexRules(g_studiomdl.model[g_studiomdl.nummodels]->source);
    }

    g_studiomdl.nummodels++;
}

void Cmd_FakeVTA() {
    int depth = 0;

    GetToken(false);

    auto psource = (s_source_t *) calloc(1, sizeof(s_source_t));
    g_studiomdl.sources.emplace_back(psource);
    strcpyn(psource->filename, token);

    while (1) {
        if (depth > 0) {
            if (!GetToken(true)) {
                break;
            }
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            else {
                GetToken(false);
            }
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return;
        }
        if (stricmp("{", token) == 0) {
            depth++;
        }
        else if (stricmp("}", token) == 0) {
            depth--;
        }
        else if (stricmp("appendvta", token) == 0) {
            char filename[256];
            // file
            GetToken(false);
            strcpy(filename, token);

            GetToken(false);
            int frame = verify_atoi(token);

            AppendVTAtoOBJ(psource, filename, frame);
        }
    }
}


int lookupControl(char *string) {
    if (stricmp(string, "X") == 0) return STUDIO_X;
    if (stricmp(string, "Y") == 0) return STUDIO_Y;
    if (stricmp(string, "Z") == 0) return STUDIO_Z;
    if (stricmp(string, "XR") == 0) return STUDIO_XR;
    if (stricmp(string, "YR") == 0) return STUDIO_YR;
    if (stricmp(string, "ZR") == 0) return STUDIO_ZR;

    if (stricmp(string, "LX") == 0) return STUDIO_LX;
    if (stricmp(string, "LY") == 0) return STUDIO_LY;
    if (stricmp(string, "LZ") == 0) return STUDIO_LZ;
    if (stricmp(string, "LXR") == 0) return STUDIO_LXR;
    if (stricmp(string, "LYR") == 0) return STUDIO_LYR;
    if (stricmp(string, "LZR") == 0) return STUDIO_LZR;

    if (stricmp(string, "LM") == 0) return STUDIO_LINEAR;
    if (stricmp(string, "LQ") == 0) return STUDIO_QUADRATIC_MOTION;

    return -1;
}


void Cmd_IKChain() {
    if (!GetToken(false))
        return;

    int i;
    for (i = 0; i < g_studiomdl.numikchains; i++) {
        if (stricmp(token, g_studiomdl.ikchain[i].name) == 0) {
            break;
        }
    }
    if (i < g_studiomdl.numikchains) {
        if (!g_StudioMdlContext.quiet) {
            printf("duplicate ikchain \"%s\" ignored\n", token);
        }
        while (TokenAvailable()) {
            GetToken(false);
        }
        return;
    }

    strcpyn(g_studiomdl.ikchain[g_studiomdl.numikchains].name, token);

    GetToken(false);
    strcpyn(g_studiomdl.ikchain[g_studiomdl.numikchains].bonename, token);

    g_studiomdl.ikchain[g_studiomdl.numikchains].axis = STUDIO_Z;
    g_studiomdl.ikchain[g_studiomdl.numikchains].value = 0.0;
    g_studiomdl.ikchain[g_studiomdl.numikchains].height = 18.0;
    g_studiomdl.ikchain[g_studiomdl.numikchains].floor = 0.0;
    g_studiomdl.ikchain[g_studiomdl.numikchains].radius = 0.0;

    while (TokenAvailable()) {
        GetToken(false);

        if (lookupControl(token) != -1) {
            g_studiomdl.ikchain[g_studiomdl.numikchains].axis = lookupControl(token);
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].value = verify_atof(token);
        }
        else if (stricmp("height", token) == 0) {
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].height = verify_atof(token);
        }
        else if (stricmp("pad", token) == 0) {
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].radius = verify_atof(token) / 2.0;
        }
        else if (stricmp("floor", token) == 0) {
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].floor = verify_atof(token);
        }
        else if (stricmp("knee", token) == 0) {
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].link[0].kneeDir.x = verify_atof(token);
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].link[0].kneeDir.y = verify_atof(token);
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].link[0].kneeDir.z = verify_atof(token);
        }
        else if (stricmp("center", token) == 0) {
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].center.x = verify_atof(token);
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].center.y = verify_atof(token);
            GetToken(false);
            g_studiomdl.ikchain[g_studiomdl.numikchains].center.z = verify_atof(token);
        }
    }
    g_studiomdl.numikchains++;
}

void Cmd_IKAutoplayLock() {
    GetToken(false);
    strcpyn(g_studiomdl.ikautoplaylock[g_studiomdl.numikautoplaylocks].name, token);

    GetToken(false);
    g_studiomdl.ikautoplaylock[g_studiomdl.numikautoplaylocks].flPosWeight = verify_atof(token);

    GetToken(false);
    g_studiomdl.ikautoplaylock[g_studiomdl.numikautoplaylocks].flLocalQWeight = verify_atof(token);

    g_studiomdl.numikautoplaylocks++;
}

void Cmd_Root() {
    if (GetToken(false)) {
        strcpyn(g_studiomdl.rootname, token);
    }
}

void Cmd_Controller() {
    if (!GetToken(false)) {
        MdlError("Missing controller <number>");
        return;
    }
    s_bonecontroller_t controller;

    if (!stricmp("mouth", token)) {
        controller.inputfield = 4;
    }
    else {
        controller.inputfield = verify_atoi(token);
    }
    if (!GetToken(false)) {
        MdlError("Missing controller <bone_name>");
        return;
    }
    strcpyn(controller.name, token);
    if (!GetToken(false)) {
        MdlError("Missing controller <axis>");
        return;
    }
    const auto axis = lookupControl(token);
    if (axis == -1) {
        MdlWarning("unknown g_studiomdl.bonecontroller type '%s'\n", token);
        return;
    }
    controller.type = axis;
    if (!GetToken(false)) {
        MdlError("Missing controller <start>");
        return;
    }
    controller.start = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Missing controller <end>");
        return;
    }
    controller.end = verify_atof(token);

    if (controller.type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) {
        if (((int) (controller.start + 360) % 360) ==
            ((int) (controller.end + 360) % 360)) {
            controller.type |= STUDIO_RLOOP;
        }
    }
    g_studiomdl.bonecontrollers.emplace_back(controller);
}

void Cmd_ScreenAlign() {
    if (GetToken(false)) {
        Assert(g_studiomdl.numscreenalignedbones < MAXSTUDIOSRCBONES);

        strcpyn(g_studiomdl.screenalignedbone[g_studiomdl.numscreenalignedbones].name, token);
        g_studiomdl.screenalignedbone[g_studiomdl.numscreenalignedbones].flags = BONE_SCREEN_ALIGN_SPHERE;

        if (GetToken(false)) {
            if (!stricmp("sphere", token)) {
                g_studiomdl.screenalignedbone[g_studiomdl.numscreenalignedbones].flags = BONE_SCREEN_ALIGN_SPHERE;
            }
            else if (!stricmp("cylinder", token)) {
                g_studiomdl.screenalignedbone[g_studiomdl.numscreenalignedbones].flags = BONE_SCREEN_ALIGN_CYLINDER;
            }
        }

        g_studiomdl.numscreenalignedbones++;
    }
    else {
        TokenError("$screenalign: expected bone name\n");
    }
}

void Cmd_WorldAlign() {
    if (GetToken(false)) {
        Assert(g_studiomdl.numworldalignedbones < MAXSTUDIOSRCBONES);

        strcpyn(g_studiomdl.worldalignedbone[g_studiomdl.numworldalignedbones].name, token);
        g_studiomdl.worldalignedbone[g_studiomdl.numworldalignedbones].flags = BONE_WORLD_ALIGN;

        g_studiomdl.numworldalignedbones++;
    }
    else {
        TokenError("$worldalign: expected bone name\n");
    }
}

void Cmd_BBox() {
    GetToken(false);
    g_studiomdl.bbox[0][0] = verify_atof(token);

    GetToken(false);
    g_studiomdl.bbox[0][1] = verify_atof(token);

    GetToken(false);
    g_studiomdl.bbox[0][2] = verify_atof(token);

    GetToken(false);
    g_studiomdl.bbox[1][0] = verify_atof(token);

    GetToken(false);
    g_studiomdl.bbox[1][1] = verify_atof(token);

    GetToken(false);
    g_studiomdl.bbox[1][2] = verify_atof(token);

    g_studiomdl.wrotebbox = true;
}

void Cmd_BBoxOnlyVerts() {
    g_studiomdl.bboxonlyverts = true;
}

void Cmd_CBox() {
    GetToken(false);
    g_studiomdl.cbox[0][0] = verify_atof(token);

    GetToken(false);
    g_studiomdl.cbox[0][1] = verify_atof(token);

    GetToken(false);
    g_studiomdl.cbox[0][2] = verify_atof(token);

    GetToken(false);
    g_studiomdl.cbox[1][0] = verify_atof(token);

    GetToken(false);
    g_studiomdl.cbox[1][1] = verify_atof(token);

    GetToken(false);
    g_studiomdl.cbox[1][2] = verify_atof(token);

    g_studiomdl.wrotecbox = true;
}

void Cmd_Gamma() {
    GetToken(false);
    g_studiomdl.gamma = verify_atof(token);
}

void Cmd_TextureGroup() {
    if (g_StudioMdlContext.createMakefile) {
        return;
    }
    int i;
    int depth = 0;
    int index = 0;
    int group = 0;


    if (!GetToken(false))
        return;

    if (g_studiomdl.numskinref == 0)
        g_studiomdl.numskinref = g_studiomdl.textures.size();

    while (1) {
        if (!GetToken(true)) {
            break;
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return;
        }
        if (token[0] == '{') {
            depth++;
        }
        else if (token[0] == '}') {
            depth--;
            if (depth == 0)
                break;
            group++;
            index = 0;
        }
        else if (depth == 2) {
            i = UseTextureAsMaterial(LookupTexture(token));
            g_studiomdl.texturegroup[g_studiomdl.numtexturegroups][group][index] = i;
            if (group != 0)
                g_studiomdl.textures[i].parent = g_studiomdl.texturegroup[g_studiomdl.numtexturegroups][0][index];
            index++;
            g_studiomdl.numtexturereps[g_studiomdl.numtexturegroups] = index;
            g_studiomdl.numtexturelayers[g_studiomdl.numtexturegroups] = group + 1;
        }
    }

    g_studiomdl.numtexturegroups++;
}

void Cmd_Hitgroup() {
    auto &hitgroup = g_studiomdl.hitgroups.emplace_back();
    GetToken(false);
    hitgroup.group = verify_atoi(token);
    GetToken(false);
    hitgroup.name = token;
}

void Cmd_Hitbox() {
    bool autogenerated = false;
    if (g_StudioMdlContext.hitboxsets.size() == 0) {
        g_StudioMdlContext.hitboxsets.emplace_back();
        autogenerated = true;
    }

    // Last one
    s_hitboxset *set = &g_StudioMdlContext.hitboxsets[g_StudioMdlContext.hitboxsets.size() - 1];
    if (autogenerated) {
        memset(set, 0, sizeof(*set));

        // fill in name if it wasn't specified in the .qc
        strcpy(set->hitboxsetname, "default");
    }

    if (!GetToken(false)) {
        MdlError("Error: missing hitbox group number");
    }

    auto &hitbox = set->hitboxes.emplace_back(s_bbox_t{
        .group = verify_atoi(token)
    });

    // Grab the bone name:
    if (!GetToken(false)) {
        MdlError("Error: missing hitbox name");
    }
    hitbox.name =token;

    if (!GetToken(false)) {
        MdlError("Error: missing hitbox min X");
    }
    hitbox.bmin[0] = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Error: missing hitbox min Y");
    }
    hitbox.bmin[1] = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Error: missing hitbox min Z");
    }
    hitbox.bmin[2] = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Error: missing hitbox max X");
    }
    hitbox.bmax[0] = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Error: missing hitbox max Y");
    }
    hitbox.bmax[1] = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Error: missing hitbox max Z");
    }
    hitbox.bmax[2] = verify_atof(token);

    if (TokenAvailable()) {
        GetToken(false);
        hitbox.angOffsetOrientation[0] = verify_atof(token);
        GetToken(false);
        hitbox.angOffsetOrientation[1] = verify_atof(token);
        GetToken(false);
        hitbox.angOffsetOrientation[2] = verify_atof(token);
    }
    else {
        hitbox.angOffsetOrientation = QAngle(0, 0, 0);
    }

    if (TokenAvailable()) {
        GetToken(false);
        hitbox.flCapsuleRadius = verify_atof(token);
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
    if (TokenAvailable()) {
        GetToken(false);
        strcpyn(hitbox.hitboxname, token);
    }
}

void Cmd_HitboxSet() {
    // Add a new hitboxset
    g_StudioMdlContext.hitboxsets.emplace_back();
    s_hitboxset *set = &g_StudioMdlContext.hitboxsets.back();
    GetToken(false);
    memset(set, 0, sizeof(*set));
    strcpy(set->hitboxsetname, token);
}

//-----------------------------------------------------------------------------
// Assigns a default surface property to the entire model
//-----------------------------------------------------------------------------
void SetDefaultSurfaceProp(const char *pSurfaceProperty) {
    Q_strncpy(g_StudioMdlContext.pDefaultSurfaceProp, pSurfaceProperty, sizeof(g_StudioMdlContext.pDefaultSurfaceProp));
}

void Cmd_SurfaceProp() {
    GetToken(false);
    SetDefaultSurfaceProp(token);
}

//-----------------------------------------------------------------------------
// Assigns a surface property to a particular joint
//-----------------------------------------------------------------------------
void Cmd_JointSurfaceProp() {
    // Get joint name...
    GetToken(false);

    char pJointName[MAX_PATH];
    Q_strncpy(pJointName, token, sizeof(pJointName));

    // surface property name
    GetToken(false);
    AddSurfaceProp(pJointName, token);
}

//-----------------------------------------------------------------------------
// Assigns a default contents to the entire model
//-----------------------------------------------------------------------------
void Cmd_Contents() {
    int nAddFlags, nRemoveFlags;
    ParseContents(&nAddFlags, &nRemoveFlags);
    s_nDefaultContents |= nAddFlags;
    s_nDefaultContents &= ~nRemoveFlags;
}

//-----------------------------------------------------------------------------
// Assigns contents to a particular joint
//-----------------------------------------------------------------------------
void Cmd_JointContents() {
    // Get joint name...
    GetToken(false);

    // Search for the name in our list
    int i;
    for (i = s_JointContents.Count(); --i >= 0;) {
        if (!stricmp(s_JointContents[i].m_pJointName, token)) {
            break;
        }
    }

    // Add new entry if we haven't seen this name before
    if (i < 0) {
        i = s_JointContents.AddToTail();
        strcpyn(s_JointContents[i].m_pJointName, token);
    }

    int nAddFlags, nRemoveFlags;
    ParseContents(&nAddFlags, &nRemoveFlags);
    s_JointContents[i].m_nContents = CONTENTS_SOLID;
    s_JointContents[i].m_nContents |= nAddFlags;
    s_JointContents[i].m_nContents &= ~nRemoveFlags;
}

void Cmd_BoneAlwaysSetup() {
    if (g_StudioMdlContext.createMakefile)
        return;

    g_studiomdl.BoneAlwaysSetup.emplace_back();

    // bone name
    GetToken(false);
    strcpyn(g_studiomdl.BoneAlwaysSetup.back().bonename, token);
}

void Internal_Cmd_Attachment(int nAttachmentTarget = g_studiomdl.attachments.size()) {
    if (g_StudioMdlContext.createMakefile)
        return;

    // name
    if (!GetToken(false)) {
        MdlError("Missing attachment <name>");
        return;
    }

    s_attachment_t *attachment;

    if (nAttachmentTarget == g_studiomdl.attachments.size()) {
        if (g_studiomdl.attachments.full()) {
            MdlError("Too many attachments");
            return;
        }
        attachment = &g_studiomdl.attachments.emplace_back();
    }
    else {
        attachment = &g_studiomdl.attachments[nAttachmentTarget];
    }

    attachment->name = token;

    // bone name
    if (!GetToken(false)) {
        MdlError("Missing attachment <parent bone>");
        return;
    }
    attachment->bonename = token;

    Vector tmp;

    // position
    if (!GetToken(false)) {
        MdlError("Missing attachment <X position>");
        return;
    }
    tmp.x = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Missing attachment <Y position>");
        return;
    }
    tmp.y = verify_atof(token);
    if (!GetToken(false)) {
        MdlError("Missing attachment <Z position>");
        return;
    }
    tmp.z = verify_atof(token);

    scale_vertex(tmp);
    // identity matrix
    AngleMatrix(QAngle(0, 0, 0), attachment->local);

    while (TokenAvailable()) {
        GetToken(false);

        if (stricmp(token, "absolute") == 0) {
            attachment->type |= IS_ABSOLUTE;
            AngleIMatrix(g_studiomdl.defaultrotation, attachment->local);
            // AngleIMatrix( Vector( 0, 0, 0 ), g_studiomdl.attachment[nAttachmentTarget].local );
        }
        else if (stricmp(token, "rigid") == 0) {
            attachment->type |= IS_RIGID;
        }
        else if (stricmp(token, "world_align") == 0) {
            attachment->flags |= ATTACHMENT_FLAG_WORLD_ALIGN;
        }
        else if (stricmp(token, "rotate") == 0) {
            QAngle angles;
            for (int i = 0; i < 3; ++i) {
                if (!TokenAvailable())
                    break;

                GetToken(false);
                angles[i] = verify_atof(token);
            }
            AngleMatrix(angles, attachment->local);
        }
        else if (stricmp(token, "x_and_z_axes") == 0) {
            int i;
            Vector xaxis, yaxis, zaxis;
            for (i = 0; i < 3; ++i) {
                if (!TokenAvailable())
                    break;

                GetToken(false);
                xaxis[i] = verify_atof(token);
            }
            for (i = 0; i < 3; ++i) {
                if (!TokenAvailable())
                    break;

                GetToken(false);
                zaxis[i] = verify_atof(token);
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
            TokenError("unknown attachment (%s) option: ", attachment->name.c_str(), token);
        }
    }

    attachment->local[0][3] = tmp.x;
    attachment->local[1][3] = tmp.y;
    attachment->local[2][3] = tmp.z;
}

void Cmd_RedefineAttachment() {
    // find a pre-existing attachment of the given name and re-populate its values

    if (g_StudioMdlContext.createMakefile)
        return;

    // name
    GetToken(false);

    UnGetToken();

    for (int n = 0; n < g_studiomdl.attachments.size(); n++) {
        if (!stricmp(token, g_studiomdl.attachments[n].name)) {
            Msg("Found pre-existing attachment matching name: %s\n", token);
            printf("Found pre-existing attachment matching name: %s\n", token);
            Internal_Cmd_Attachment(n);
            return;
        }
    }

    MdlError("Can't redefine attachment \"%s\" because it wasn't found.\n", token);
}

void Cmd_Attachment() {
    Internal_Cmd_Attachment(g_studiomdl.attachments.size());
}

void Cmd_Renamebone() {
    // from
    GetToken(false);
    strcpyn(g_studiomdl.renamedbone[g_studiomdl.numrenamedbones].from, token);

    // to
    GetToken(false);
    strcpyn(g_studiomdl.renamedbone[g_studiomdl.numrenamedbones].to, token);

    g_studiomdl.numrenamedbones++;
}

void Cmd_StripBonePrefix() {
    if (!g_studiomdl.szStripBonePrefix.full()) {
        GetToken(false);

        // make sure it's not a duplicate
        for (int k = 0; k < g_studiomdl.szStripBonePrefix.size(); k++) {
            if (g_studiomdl.szStripBonePrefix[k] == (const char *) token) {
                MdlWarning("Ignoring duplicate $bonestripprefix for token %s\n", token);
                return;
            }
        }

        g_studiomdl.szStripBonePrefix.emplace_back(token);
    }
    else {
        MdlError("Too many bone strip prefixes!\n");
    }
}

void Cmd_RenameBoneSubstr() {
    // from
    GetToken(false);
    strcpyn(g_studiomdl.szRenameBoneSubstr[g_studiomdl.numRenameBoneSubstr].from, token);

    // to
    GetToken(false);
    strcpyn(g_studiomdl.szRenameBoneSubstr[g_studiomdl.numRenameBoneSubstr].to, token);

    g_studiomdl.numRenameBoneSubstr++;
}

// Returns the 1-based id of the named transition node, adding it if needed
int LookupXNode(const char *name) {
    for (size_t i = 0; i < g_studiomdl.xnodenames.size(); i++) {
        if (stricmp(name, g_studiomdl.xnodenames[i].c_str()) == 0) {
            return static_cast<int>(i + 1);
        }
    }
    if (g_studiomdl.xnodenames.full()) {
        MdlError("Too many transition nodes, max %d\n", (int) g_studiomdl.xnodenames.capacity());
    }
    g_studiomdl.xnodenames.emplace_back(name);
    return static_cast<int>(g_studiomdl.xnodenames.size());
}

void Cmd_Skiptransition() {
    int nskips = 0;
    int list[10];

    while (TokenAvailable()) {
        GetToken(false);
        list[nskips++] = LookupXNode(token);
    }

    for (int i = 0; i < nskips; i++) {
        for (int j = 0; j < nskips; j++) {
            if (list[i] != list[j]) {
                g_studiomdl.xnodeskips.emplace_back(std::array{list[i], list[j]});
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
    int i;
    if (xext[0]) {
        // we know what extension is necessary. . look for it.
        Q_snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.%s",
                   g_studiomdl.cddir.back().c_str(), name, xext);
        for (i = 0; i < g_studiomdl.sources.size(); i++) {
            if (!Q_stricmp(g_StudioMdlContext.szFilename, g_studiomdl.sources[i]->filename))
                return g_studiomdl.sources[i];
        }
    }
    else {
        // we don't know what extension to use, so look for all of 'em.
        Q_snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.vrm",
                   g_studiomdl.cddir.back().c_str(), name);
        for (i = 0; i < g_studiomdl.sources.size(); i++) {
            if (!Q_stricmp(g_StudioMdlContext.szFilename, g_studiomdl.sources[i]->filename))
                return g_studiomdl.sources[i];
        }
        Q_snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.dmx",
                   g_studiomdl.cddir.back().c_str(), name);
        for (i = 0; i < g_studiomdl.sources.size(); i++) {
            if (!Q_stricmp(g_StudioMdlContext.szFilename, g_studiomdl.sources[i]->filename))
                return g_studiomdl.sources[i];
        }
        Q_snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.smd",
                   g_studiomdl.cddir.back().c_str(), name);
        for (i = 0; i < g_studiomdl.sources.size(); i++) {
            if (!Q_stricmp(g_StudioMdlContext.szFilename, g_studiomdl.sources[i]->filename))
                return g_studiomdl.sources[i];
        }

        Q_snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.xml",
                   g_studiomdl.cddir.back().c_str(), name);
        for (i = 0; i < g_studiomdl.sources.size(); i++) {
            if (!Q_stricmp(g_StudioMdlContext.szFilename, g_studiomdl.sources[i]->filename))
                return g_studiomdl.sources[i];
        }
        Q_snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.obj",
                   g_studiomdl.cddir.back().c_str(), name);
        for (i = 0; i < g_studiomdl.sources.size(); i++) {
            if (!Q_stricmp(g_StudioMdlContext.szFilename, g_studiomdl.sources[i]->filename))
                return g_studiomdl.sources[i];
        }
        /*
		sprintf (g_szFilename, "%s%s.vta", g_studiomdl.cddir.back().c_str(), name );
		for (i = 0; i < g_studiomdl.numsources; i++)
		{
			if (stricmp( g_szFilename, g_studiomdl.source[i]->filename ) == 0)
				return g_studiomdl.source[i];
		}
		*/
    }

    // Not found
    return nullptr;
}


//-----------------------------------------------------------------------------
// Parse replacemodel command, causes an LOD to use a new model
//-----------------------------------------------------------------------------

static void Cmd_ReplaceModel(LodScriptData_t &lodData) {
    int i = lodData.modelReplacements.AddToTail();
    CLodScriptReplacement_t &newReplacement = lodData.modelReplacements[i];

    // from
    GetToken(false);

    // Strip off extensions for the source...
    char *pDot = strrchr(token, '.');
    if (pDot) {
        *pDot = 0;
    }

    if (!FindCachedSource(token, "")) {
        // must have prior knowledge of the from
        TokenError("Unknown replace model '%s'\n", token);
    }

    newReplacement.SetSrcName(token);

    // to
    GetToken(false);
    newReplacement.SetDstName(token);

    // check for "reverse"
    bool reverse = false;
    if (TokenAvailable() && GetToken(false)) {
        if (stricmp("reverse", token) == 0) {
            reverse = true;
        }
        else {
            TokenError("\"%s\" unexpected\n", token);
        }
    }

    // If the LOD system tells us to replace "blank", let's forget
    // we ever read this. Have to do it here so parsing works
    if (!stricmp(newReplacement.GetSrcName(), "blank")) {
        lodData.modelReplacements.FastRemove(i);
        return;
    }

    // Load the source right here baby! That way its bones will get converted
    if (!lodData.IsStrippedFromModel()) {
        newReplacement.m_pSource = Load_Source(newReplacement.GetDstName(), "smd", reverse, false);
    }
    else if (!g_StudioMdlContext.quiet) {
        printf("Stripped lod \"%s\" @ %.1f\n", newReplacement.GetDstName(), lodData.switchValue);
    }
}

//-----------------------------------------------------------------------------
// Parse removemodel command, causes an LOD to stop using a model
//-----------------------------------------------------------------------------

static void Cmd_RemoveModel(LodScriptData_t &lodData) {
    int i = lodData.modelReplacements.AddToTail();
    CLodScriptReplacement_t &newReplacement = lodData.modelReplacements[i];

    // from
    GetToken(false);

    // Strip off extensions...
    char *pDot = strrchr(token, '.');
    if (pDot)
        *pDot = 0;

    newReplacement.SetSrcName(token);

    // to
    newReplacement.SetDstName("");

    // If the LOD system tells us to replace "blank", let's forget
    // we ever read this. Have to do it here so parsing works
    if (!stricmp(newReplacement.GetSrcName(), "blank")) {
        lodData.modelReplacements.FastRemove(i);
    }
}

//-----------------------------------------------------------------------------
// Parse replacebone command, causes a part of an LOD model to use a different bone
//-----------------------------------------------------------------------------

static void Cmd_ReplaceBone(LodScriptData_t &lodData) {
    int i = lodData.boneReplacements.AddToTail();
    CLodScriptReplacement_t &newReplacement = lodData.boneReplacements[i];

    // from
    GetToken(false);
    newReplacement.SetSrcName(token);

    // to
    GetToken(false);
    newReplacement.SetDstName(token);
}

//-----------------------------------------------------------------------------
// Parse bonetreecollapse command, causes the entire subtree to use the same bone as the node
//-----------------------------------------------------------------------------

static void Cmd_BoneTreeCollapse(LodScriptData_t &lodData) {
    int i = lodData.boneTreeCollapses.AddToTail();
    CLodScriptReplacement_t &newCollapse = lodData.boneTreeCollapses[i];

    // from
    GetToken(false);
    newCollapse.SetSrcName(token);
}

//-----------------------------------------------------------------------------
// Parse replacematerial command, causes a material to be used in place of another
//-----------------------------------------------------------------------------

static void Cmd_ReplaceMaterial(LodScriptData_t &lodData) {
    int i = lodData.materialReplacements.AddToTail();
    CLodScriptReplacement_t &newReplacement = lodData.materialReplacements[i];

    // from
    GetToken(false);
    newReplacement.SetSrcName(token);

    // to
    GetToken(false);
    newReplacement.SetDstName(token);

    if (!lodData.IsStrippedFromModel()) {
        // make sure it goes into the master list
        UseTextureAsMaterial(LookupTexture(token));
    }
}

//-----------------------------------------------------------------------------
// Parse removemesh command, causes a mesh to not be used anymore
//-----------------------------------------------------------------------------

static void Cmd_RemoveMesh(LodScriptData_t &lodData) {
    int i = lodData.meshRemovals.AddToTail();
    CLodScriptReplacement_t &newReplacement = lodData.meshRemovals[i];

    // from
    GetToken(false);
    Q_FixSlashes(token);
    newReplacement.SetSrcName(token);
}

void Cmd_LOD(const char *cmdname) {
    if (g_studiomdl.gflags & STUDIOHDR_FLAGS_HASSHADOWLOD) {
        MdlError("Model can only have one $shadowlod and it must be the last lod in the " SRC_FILE_EXT " (%d) : %s\n",
                 g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine.c_str());
    }

    int i = g_studiomdl.ScriptLODs.AddToTail();
    LodScriptData_t &newLOD = g_studiomdl.ScriptLODs[i];

    if (g_studiomdl.ScriptLODs.Count() > MAX_NUM_LODS) {
        MdlError("Too many LODs (MAX_NUM_LODS==%d)\n", (int) MAX_NUM_LODS);
    }

    // Shadow lod reserves -1 as switch value
    // which uniquely identifies a shadow lod
    newLOD.switchValue = -1.0f;

    bool isShadowCall = (!stricmp(cmdname, "$shadowlod")) ? true : false;

    if (isShadowCall) {
        if (TokenAvailable()) {
            GetToken(false);
            MdlWarning("(%d) : %s:  Ignoring switch value on %s command line\n", cmdname, g_StudioMdlContext.iLinecount,
                       g_StudioMdlContext.szLine.c_str());
        }

        // Disable facial animation by default
        newLOD.EnableFacialAnimation(false);
    }
    else {
        if (TokenAvailable()) {
            GetToken(false);
            newLOD.switchValue = verify_atof(token);
            if (newLOD.switchValue < 0.0f) {
                MdlError("Negative switch value reserved for $shadowlod (%d) : %s\n", g_StudioMdlContext.iLinecount,
                         g_StudioMdlContext.szLine.c_str());
            }
        }
        else {
            MdlError("Expected LOD switch value (%d) : %s\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
        }
    }

    GetToken(true);
    if (stricmp("{", token) != 0) {
        MdlError("\"{\" expected while processing %s (%d) : %s", cmdname, g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
    }

    // In case we are stripping all lods and it's not Lod0, strip it
    if (i && g_StudioMdlContext.stripLods)
        newLOD.StripFromModel(true);

    while (1) {
        GetToken(true);
        if (stricmp("replacemodel", token) == 0) {
            Cmd_ReplaceModel(newLOD);
        }
        else if (stricmp("removemodel", token) == 0) {
            Cmd_RemoveModel(newLOD);
        }
        else if (stricmp("replacebone", token) == 0) {
            Cmd_ReplaceBone(newLOD);
        }
        else if (stricmp("bonetreecollapse", token) == 0) {
            Cmd_BoneTreeCollapse(newLOD);
        }
        else if (stricmp("replacematerial", token) == 0) {
            Cmd_ReplaceMaterial(newLOD);
        }
        else if (stricmp("removemesh", token) == 0) {
            Cmd_RemoveMesh(newLOD);
        }
        else if (stricmp("nofacial", token) == 0) {
            newLOD.EnableFacialAnimation(false);
        }
        else if (stricmp("facial", token) == 0) {
            if (isShadowCall) {
                // facial animation has no reasonable purpose on a shadow lod
                TokenError("Facial animation is not allowed for $shadowlod\n");
            }

            newLOD.EnableFacialAnimation(true);
        }
        else if (stricmp("use_shadowlod_materials", token) == 0) {
            if (isShadowCall) {
                g_studiomdl.gflags |= STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS;
            }
        }
        else if (stricmp("}", token) == 0) {
            break;
        }
        else {
            MdlError("invalid input while processing %s (%d) : %s", cmdname, g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
        }
    }

    // If the LOD is stripped, then forget we saw it
    if (newLOD.IsStrippedFromModel()) {
        g_studiomdl.ScriptLODs.FastRemove(i);
    }
}

void Cmd_ShadowLOD() {
    if (!g_StudioMdlContext.quiet) {
        printf("Processing $shadowlod\n");
    }

    // Act like it's a regular lod entry
    Cmd_LOD("$shadowlod");

    // Mark .mdl as having shadow lod (we also check above that we have only one of these
    // and that it's the last entered lod )
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_HASSHADOWLOD;
}


//-----------------------------------------------------------------------------
// A couple commands related to translucency sorting
//-----------------------------------------------------------------------------
void Cmd_Opaque() {
    // Force Opaque has precedence
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_FORCE_OPAQUE;
    g_studiomdl.gflags &= ~STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
}

void Cmd_TranslucentTwoPass() {
    // Force Opaque has precedence
    if ((g_studiomdl.gflags & STUDIOHDR_FLAGS_FORCE_OPAQUE) == 0) {
        g_studiomdl.gflags |= STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS;
    }
}

//-----------------------------------------------------------------------------
// Indicates the model be rendered with ambient boost heuristic (first used on Alyx in Episode 1)
//-----------------------------------------------------------------------------
void Cmd_AmbientBoost() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_AMBIENT_BOOST;
}

//-----------------------------------------------------------------------------
// Indicates the model contains a quad-only Catmull-Clark subd mesh
//-----------------------------------------------------------------------------
void Cmd_SubdivisionSurface() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_SUBDIVISION_SURFACE;
}


//-----------------------------------------------------------------------------
// Indicates the model should not cast shadows (useful for first-person models as used in L4D)
//-----------------------------------------------------------------------------
void Cmd_DoNotCastShadows() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS;
}

//-----------------------------------------------------------------------------
// Indicates the model should cast texture-based shadows in vrad (NOTE: only applicable to prop_static)
//-----------------------------------------------------------------------------
void Cmd_CastTextureShadows() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS;
}


//-----------------------------------------------------------------------------
// Indicates the model should not fade out even if the level or fallback settings say to
//-----------------------------------------------------------------------------
void Cmd_NoForcedFade() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_NO_FORCED_FADE;
}


//-----------------------------------------------------------------------------
// Indicates the model should not use the bone origin when calculating bboxes, sequence boxes, etc.
//-----------------------------------------------------------------------------
void Cmd_SkipBoneInBBox() {
    g_StudioMdlContext.useBoneInBBox = false;
}


//-----------------------------------------------------------------------------
// Indicates the model will lengthen the viseme check to always include two phonemes
//-----------------------------------------------------------------------------
void Cmd_ForcePhonemeCrossfade() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE;
}

//-----------------------------------------------------------------------------
// Indicates the model should keep pre-defined bone lengths regardless of animation changes
//-----------------------------------------------------------------------------
void Cmd_LockBoneLengths() {
    g_StudioMdlContext.lockBoneLengths = true;
}

//-----------------------------------------------------------------------------
// Indicates the model should replace pre-defined bone bind poses
//-----------------------------------------------------------------------------
void Cmd_UnlockDefineBones() {
    g_StudioMdlContext.defineBonesLockedByDefault = false;
}

//-----------------------------------------------------------------------------
// Mark this model as obsolete so that it'll show the obsolete material in game.
//-----------------------------------------------------------------------------
void Cmd_Obsolete() {
    // Force Opaque has precedence
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_OBSOLETE;
}

//-----------------------------------------------------------------------------
// The bones should be moved so that they center themselves on the verts they own.
//-----------------------------------------------------------------------------
void Cmd_CenterBonesOnVerts() {
    // force centering on bones
    g_StudioMdlContext.centerBonesOnVerts = true;
}

//-----------------------------------------------------------------------------
// How far back should simple motion extract pull back from the last frame
//-----------------------------------------------------------------------------
void Cmd_MotionExtractionRollBack() {
    GetToken(false);
    g_StudioMdlContext.defaultMotionRollback = atof(token);
}

//-----------------------------------------------------------------------------
// rules for breaking up long animations into multiple sub anims
//-----------------------------------------------------------------------------
void Cmd_SectionFrames() {
    GetToken(false);
    g_StudioMdlContext.sectionFrames = atof(token);
    GetToken(false);
    g_StudioMdlContext.minSectionFrameLimit = atoi(token);
}


//-----------------------------------------------------------------------------
// world space clamping boundaries for animations
//-----------------------------------------------------------------------------
void Cmd_ClampWorldspace() {
    GetToken(false);
    g_StudioMdlContext.vecMinWorldspace[0] = verify_atof(token);

    GetToken(false);
    g_StudioMdlContext.vecMinWorldspace[1] = verify_atof(token);

    GetToken(false);
    g_StudioMdlContext.vecMinWorldspace[2] = verify_atof(token);

    GetToken(false);
    g_StudioMdlContext.vecMaxWorldspace[0] = verify_atof(token);

    GetToken(false);
    g_StudioMdlContext.vecMaxWorldspace[1] = verify_atof(token);

    GetToken(false);
    g_StudioMdlContext.vecMaxWorldspace[2] = verify_atof(token);
}

//-----------------------------------------------------------------------------
// Purpose: force a specific parent child relationship
//-----------------------------------------------------------------------------

void Cmd_ForcedHierarchy() {
    // child name
    GetToken(false);
    strcpyn(g_studiomdl.forcedhierarchy[g_studiomdl.numforcedhierarchy].childname, token);

    // parent name
    GetToken(false);
    strcpyn(g_studiomdl.forcedhierarchy[g_studiomdl.numforcedhierarchy].parentname, token);

    g_studiomdl.numforcedhierarchy++;
}


//-----------------------------------------------------------------------------
// Purpose: insert a virtual bone between a child and parent (currently unsupported)
//-----------------------------------------------------------------------------

void Cmd_InsertHierarchy() {
    // child name
    GetToken(false);
    strcpyn(g_studiomdl.forcedhierarchy[g_studiomdl.numforcedhierarchy].childname, token);

    // subparent name
    GetToken(false);
    strcpyn(g_studiomdl.forcedhierarchy[g_studiomdl.numforcedhierarchy].subparentname, token);

    // parent name
    GetToken(false);
    strcpyn(g_studiomdl.forcedhierarchy[g_studiomdl.numforcedhierarchy].parentname, token);

    g_studiomdl.numforcedhierarchy++;
}


//-----------------------------------------------------------------------------
// Purpose: rotate a specific bone
//-----------------------------------------------------------------------------

void Cmd_ForceRealign() {
    // bone name
    GetToken(false);
    strcpyn(g_studiomdl.forcedrealign[g_studiomdl.numforcedrealign].name, token);

    // skip
    GetToken(false);

    // X axis
    GetToken(false);
    g_studiomdl.forcedrealign[g_studiomdl.numforcedrealign].rot.x = DEG2RAD(verify_atof(token));

    // Y axis
    GetToken(false);
    g_studiomdl.forcedrealign[g_studiomdl.numforcedrealign].rot.y = DEG2RAD(verify_atof(token));

    // Z axis
    GetToken(false);
    g_studiomdl.forcedrealign[g_studiomdl.numforcedrealign].rot.z = DEG2RAD(verify_atof(token));

    g_studiomdl.numforcedrealign++;
}


//-----------------------------------------------------------------------------
// Purpose: specify a bone to allow > 180 but < 360 rotation (forces a calculated "mid point" to rotation)
//-----------------------------------------------------------------------------

void Cmd_LimitRotation() {
    // bone name
    GetToken(false);
    strcpyn(g_studiomdl.limitrotation[g_studiomdl.numlimitrotation].name, token);

    while (TokenAvailable()) {
        // sequence name
        GetToken(false);
        strcpyn(
            g_studiomdl.limitrotation[g_studiomdl.numlimitrotation].sequencename[g_studiomdl.limitrotation[g_studiomdl.
                numlimitrotation].numseq++], token);
    }

    g_studiomdl.numlimitrotation++;
}

//-----------------------------------------------------------------------------
// Purpose: artist controlled sanity check for expected state of the model.
// The idea is to allow artists to anticipate and prevent content errors by adding 'qc asserts'
// into commonly iterated models. This could be anything from bones that are expected (or not)
// to polycounts, material references, etc.- It's just an "Assert" for content.
//-----------------------------------------------------------------------------
void Cmd_QCAssert() {
    //get the assert type
    GetToken(false);

    //Msg( "Validating QC Assert '%s'\n", token );

    //start building assert description line
    char szAssertLine[1024] = "QC Assert: ";
    strcat(szAssertLine, token);

    bool bQueryValue = false;

    if (!Q_stricmp(token, "boneexists")) {
        // bone name
        GetToken(false);
        char szBoneName[MAXSTUDIONAME];
        strcpyn(szBoneName, token);

        strcat(szAssertLine, " ");
        strcat(szAssertLine, szBoneName);

        // src name
        GetToken(false);
        s_source_t *pSrc = Load_Source(token, "");

        strcat(szAssertLine, " ");
        strcat(szAssertLine, token);
        for (int n = 0; n < pSrc->numbones; n++) {
            if (!Q_stricmp(szBoneName, pSrc->localBone[n].name)) {
                bQueryValue = true;
                break;
            }
        }
    }
    else if (!Q_stricmp(token, "importboneexists")) {
        // bone name
        GetToken(false);
        char szBoneName[MAXSTUDIONAME];
        strcpyn(szBoneName, token);

        strcat(szAssertLine, " ");
        strcat(szAssertLine, szBoneName);

        for (int n = 0; n < g_studiomdl.numimportbones; n++) {
            if (!Q_stricmp(szBoneName, g_studiomdl.importbone[n].name)) {
                bQueryValue = true;
                break;
            }
        }
    }

    // add more possible qc asserts here...


    // is the assert value positive or negative
    GetToken(false);
    strcat(szAssertLine, " ");
    strcat(szAssertLine, token);

    bool bAssertValue = !Q_stricmp(token, "true");

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

        MdlError(szAssertLine);
    }
    else {
        strcat(szAssertLine, "[Success]\n");
    }
    printf("%s", szAssertLine);
}

//-----------------------------------------------------------------------------
// Purpose: specify bones to store, even if nothing references them
//-----------------------------------------------------------------------------

void Cmd_DefineBone() {
    // bone name
    GetToken(false);
    strcpyn(g_studiomdl.importbone[g_studiomdl.numimportbones].name, token);

    // parent name
    GetToken(false);
    strcpyn(g_studiomdl.importbone[g_studiomdl.numimportbones].parent, token);

    g_studiomdl.importbone[g_studiomdl.numimportbones].bUnlocked = !g_StudioMdlContext.defineBonesLockedByDefault;

    GetToken(false);
    if (!V_strcmp(token, "unlocked")) {
        g_studiomdl.importbone[g_studiomdl.numimportbones].bUnlocked = true;
    }
    else if (!V_strcmp(token, "locked")) {
        g_studiomdl.importbone[g_studiomdl.numimportbones].bUnlocked = false;
    }
    else {
        UnGetToken();
    }

    Vector pos;
    QAngle angles;

    // default pos
    GetToken(false);
    pos.x = verify_atof(token);
    GetToken(false);
    pos.y = verify_atof(token);
    GetToken(false);
    pos.z = verify_atof(token);
    GetToken(false);
    angles.x = verify_atof(token);
    GetToken(false);
    angles.y = verify_atof(token);
    GetToken(false);
    angles.z = verify_atof(token);
    AngleMatrix(angles, pos, g_studiomdl.importbone[g_studiomdl.numimportbones].rawLocal);

    if (TokenAvailable()) {
        g_studiomdl.importbone[g_studiomdl.numimportbones].bPreAligned = true;
        // realign pos
        GetToken(false);
        pos.x = verify_atof(token);
        GetToken(false);
        pos.y = verify_atof(token);
        GetToken(false);
        pos.z = verify_atof(token);
        GetToken(false);
        angles.x = verify_atof(token);
        GetToken(false);
        angles.y = verify_atof(token);
        GetToken(false);
        angles.z = verify_atof(token);

        AngleMatrix(angles, pos, g_studiomdl.importbone[g_studiomdl.numimportbones].srcRealign);
    }
    else {
        SetIdentityMatrix(g_studiomdl.importbone[g_studiomdl.numimportbones].srcRealign);
    }

    g_studiomdl.numimportbones++;
}

bool ParseJiggleAngleConstraint(s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JIGGLE_HAS_ANGLE_CONSTRAINT;

    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting angle value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return false;
    }

    jiggleInfo->data.angleLimit = verify_atof(token) * M_PI / 180.0f;

    return true;
}

bool ParseJiggleYawConstraint(s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JIGGLE_HAS_YAW_CONSTRAINT;

    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting minimum yaw value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return false;
    }

    jiggleInfo->data.minYaw = verify_atof(token) * M_PI / 180.0f;

    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting maximum yaw value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return false;
    }

    jiggleInfo->data.maxYaw = verify_atof(token) * M_PI / 180.0f;

    return true;
}

bool ParseJigglePitchConstraint(s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JIGGLE_HAS_PITCH_CONSTRAINT;

    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting minimum pitch value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return false;
    }

    jiggleInfo->data.minPitch = verify_atof(token) * M_PI / 180.0f;

    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting maximum pitch value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return false;
    }

    jiggleInfo->data.maxPitch = verify_atof(token) * M_PI / 180.0f;

    return true;
}

void Grab_AxisInterpBones() {
    char cmd[1024], tmp[1025];
    Vector basepos;
    s_axisinterpbone_t *pAxis = nullptr;
    s_axisinterpbone_t *pBone = &g_studiomdl.axisinterpbones[g_studiomdl.numaxisinterpbones];

    while (GetLineInput()) {
        if (IsEnd(g_StudioMdlContext.szLine)) {
            return;
        }
        int i = sscanf(g_StudioMdlContext.szLine, "%1023s \"%[^\"]\" \"%[^\"]\" \"%[^\"]\" \"%[^\"]\" %d", cmd,
                       pBone->bonename, tmp,
                       pBone->controlname, tmp, &pBone->axis);
        if (i == 6 && stricmp(cmd, "bone") == 0) {
            // printf( "\"%s\" \"%s\" \"%s\" \"%s\"\n", cmd, pBone->bonename, tmp, pBone->controlname );
            pAxis = pBone;
            pBone->axis = pBone->axis - 1; // MAX uses 1..3, engine 0..2
            g_studiomdl.numaxisinterpbones++;
            pBone = &g_studiomdl.axisinterpbones[g_studiomdl.numaxisinterpbones];
        }
        else if (stricmp(cmd, "display") == 0) {
            // skip all display info
        }
        else if (stricmp(cmd, "type") == 0) {
            // skip all type info
        }
        else if (stricmp(cmd, "basepos") == 0) {
            i = sscanf(g_StudioMdlContext.szLine, "basepos %f %f %f", &basepos.x, &basepos.y, &basepos.z);
            // skip all type info
        }
        else if (stricmp(cmd, "axis") == 0) {
            Vector pos;
            QAngle rot;
            int j;
            i = sscanf(g_StudioMdlContext.szLine, "axis %d %f %f %f %f %f %f", &j, &pos[0], &pos[1], &pos[2], &rot[2],
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
bool ParseCommonJiggle(s_jigglebone_t *jiggleInfo) {
    if (!stricmp(token, "tip_mass")) {
        if (!GetToken(false)) {
            return false;
        }

        jiggleInfo->data.tipMass = verify_atof(token);
    }
    else if (!stricmp(token, "length")) {
        if (!GetToken(false)) {
            return false;
        }

        jiggleInfo->data.length = verify_atof(token);
    }
    else if (!stricmp(token, "angle_constraint")) {
        if (ParseJiggleAngleConstraint(jiggleInfo) == false) {
            return false;
        }
    }
    else if (!stricmp(token, "yaw_constraint")) {
        if (ParseJiggleYawConstraint(jiggleInfo) == false) {
            return false;
        }
    }
    else if (!stricmp(token, "yaw_friction")) {
        if (!GetToken(false)) {
            return false;
        }

        jiggleInfo->data.yawFriction = verify_atof(token);
    }
    else if (!stricmp(token, "yaw_bounce")) {
        if (!GetToken(false)) {
            return false;
        }

        jiggleInfo->data.yawBounce = verify_atof(token);
    }
    else if (!stricmp(token, "pitch_constraint")) {
        if (ParseJigglePitchConstraint(jiggleInfo) == false) {
            return false;
        }
    }
    else if (!stricmp(token, "pitch_friction")) {
        if (!GetToken(false)) {
            return false;
        }

        jiggleInfo->data.pitchFriction = verify_atof(token);
    }
    else if (!stricmp(token, "pitch_bounce")) {
        if (!GetToken(false)) {
            return false;
        }

        jiggleInfo->data.pitchBounce = verify_atof(token);
    }
    else {
        // unknown token
        MdlError("$jigglebone: invalid syntax '%s'\n", token);
        return false;
    }

    return true;
}

//
// Parse parameters for is_rigid subsection
//
bool ParseRigidJiggle(s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= (JIGGLE_IS_RIGID | JIGGLE_HAS_LENGTH_CONSTRAINT);

    bool gotOpenBracket = false;
    while (true) {
        if (GetToken(true) == false) {
            MdlError("$jigglebone:is_rigid: parse error\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
            return false;
        }

        if (!stricmp(token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            MdlError("$jigglebone:is_rigid: missing '{'\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
            return false;
        }
        else if (!stricmp(token, "}")) {
            // definition complete
            break;
        }
        else if (ParseCommonJiggle(jiggleInfo) == false) {
            MdlError("$jigglebone:is_rigid: invalid syntax '%s'\n", token);
            return false;
        }
    }

    return true;
}


float ParseJiggleStiffness() {
    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting stiffness value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return 0.0f;
    }

    float stiffness = verify_atof(token);

    const float minStiffness = 0.0f;
    const float maxStiffness = 1000.0f;

    return clamp(stiffness, minStiffness, maxStiffness);
}

float ParseJiggleDamping() {
    if (!GetToken(false)) {
        MdlError("$jigglebone: expecting damping value\n", g_StudioMdlContext.iLinecount,
                 g_StudioMdlContext.szLine.c_str());
        return 0.0f;
    }

    float damping = verify_atof(token);

    const float minDamping = 0.0f;
    const float maxDamping = 10.0f;

    return clamp(damping, minDamping, maxDamping);
}

//
// Parse parameters for has_base_spring subsection
//
bool ParseBaseSpringJiggle(s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= JIGGLE_HAS_BASE_SPRING;

    bool gotOpenBracket = false;
    while (true) {
        if (GetToken(true) == false) {
            MdlError("$jigglebone:is_rigid: parse error\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
            return false;
        }

        if (!stricmp(token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            MdlError("$jigglebone:is_rigid: missing '{'\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
            return false;
        }
        else if (!stricmp(token, "}")) {
            // definition complete
            break;
        }
        else if (!stricmp(token, "stiffness")) {
            jiggleInfo->data.baseStiffness = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "damping")) {
            jiggleInfo->data.baseDamping = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "left_constraint")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMinLeft = verify_atof(token);

            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMaxLeft = verify_atof(token);
        }
        else if (!stricmp(token, "left_friction")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseLeftFriction = verify_atof(token);
        }
        else if (!stricmp(token, "up_constraint")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMinUp = verify_atof(token);

            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMaxUp = verify_atof(token);
        }
        else if (!stricmp(token, "up_friction")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseUpFriction = verify_atof(token);
        }
        else if (!stricmp(token, "forward_constraint")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMinForward = verify_atof(token);

            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMaxForward = verify_atof(token);
        }
        else if (!stricmp(token, "forward_friction")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseForwardFriction = verify_atof(token);
        }
        else if (!stricmp(token, "base_mass")) {
            if (!GetToken(false)) {
                return false;
            }

            jiggleInfo->data.baseMass = verify_atof(token);
        }
        else if (ParseCommonJiggle(jiggleInfo) == false) {
            MdlError("$jigglebone:has_base_spring: invalid syntax '%s'\n", token);
            return false;
        }
    }

    return true;
}


//
// Parse parameters for is_flexible subsection
//
bool ParseFlexibleJiggle(s_jigglebone_t *jiggleInfo) {
    jiggleInfo->data.flags |= (JIGGLE_IS_FLEXIBLE | JIGGLE_HAS_LENGTH_CONSTRAINT);

    bool gotOpenBracket = false;
    while (true) {
        if (GetToken(true) == false) {
            MdlError("$jigglebone:is_flexible: parse error\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
            return false;
        }

        if (!stricmp(token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            MdlError("$jigglebone:is_flexible: missing '{'\n", g_StudioMdlContext.iLinecount,
                     g_StudioMdlContext.szLine.c_str());
            return false;
        }
        else if (!stricmp(token, "}")) {
            // definition complete
            break;
        }
        else if (!stricmp(token, "yaw_stiffness")) {
            jiggleInfo->data.yawStiffness = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "yaw_damping")) {
            jiggleInfo->data.yawDamping = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "pitch_stiffness")) {
            jiggleInfo->data.pitchStiffness = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "pitch_damping")) {
            jiggleInfo->data.pitchDamping = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "along_stiffness")) {
            jiggleInfo->data.alongStiffness = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "along_damping")) {
            jiggleInfo->data.alongDamping = ParseJiggleStiffness();
        }
        else if (!stricmp(token, "allow_length_flex")) {
            jiggleInfo->data.flags &= ~JIGGLE_HAS_LENGTH_CONSTRAINT;
        }
        else if (ParseCommonJiggle(jiggleInfo) == false) {
            MdlError("$jigglebone:is_flexible: invalid syntax '%s'\n", token);
            return false;
        }
    }

    return true;
}

//
// Parse $jigglebone parameters
//
void Cmd_JiggleBone() {
    struct s_jigglebone_t *jiggleInfo = &g_studiomdl.jigglebones[g_studiomdl.numjigglebones];

    // bone name
    GetToken(false);
    strcpyn(jiggleInfo->bonename, token);

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
        if (GetToken(true) == false) {
            MdlError("$jigglebone: parse error\n", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine.c_str());
            return;
        }

        if (!stricmp(token, "{")) {
            gotOpenBracket = true;
        }
        else if (!gotOpenBracket) {
            MdlError("$jigglebone: missing '{'\n", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine.c_str());
            return;
        }
        else if (!stricmp(token, "}")) {
            // definition complete
            break;
        }
        else if (!stricmp(token, "is_flexible")) {
            if (ParseFlexibleJiggle(jiggleInfo) == false) {
                return;
            }
        }
        else if (!stricmp(token, "is_rigid")) {
            if (ParseRigidJiggle(jiggleInfo) == false) {
                return;
            }
        }
        else if (!stricmp(token, "has_base_spring")) {
            if (ParseBaseSpringJiggle(jiggleInfo) == false) {
                return;
            }
        }
        else {
            MdlError("$jigglebone: invalid syntax '%s'\n", token);
            return;
        }
    }

    if (!g_StudioMdlContext.quiet)
        Msg("Marking bone %s as a jiggle bone\n", jiggleInfo->bonename);

    g_studiomdl.numjigglebones++;
}


//-----------------------------------------------------------------------------
// Purpose: specify bones to store, even if nothing references them
//-----------------------------------------------------------------------------

void Cmd_IncludeModel() {
    GetToken(false);
    g_studiomdl.includemodels.emplace_back(StudioName::from_format("models/%s", token));
}

void Cmd_CD() {
    if (g_studiomdl.cdset)
        MdlError("Two $cd in one model");
    g_studiomdl.cdset = true;
    if (!GetToken(false)) {
        MdlError("Missing <path> argument for $cd");
        return;
    }
    g_studiomdl.cddir.clear();
    g_studiomdl.cddir.emplace().format("%s/", token);
}

void Cmd_ContentRootRelative() {
    g_StudioMdlContext.bContentRootRelative = true;
}

void Cmd_CDMaterials() {
    while (TokenAvailable()) {
        GetToken(false);

        char szPath[512];
        Q_strncpy(szPath, token, sizeof(szPath));

        int len = strlen(szPath);
        if (len > 0 && szPath[len - 1] != '/' && szPath[len - 1] != '\\') {
            Q_strncat(szPath, "/", sizeof(szPath), COPY_ALL_CHARACTERS);
        }

        Q_FixSlashes(szPath);
        g_studiomdl.cdtextures.emplace_back(strdup(szPath));
    }
}

void Cmd_Pushd() {
    GetToken(false);

    const auto &parent_dir = g_studiomdl.cddir.back();
    auto &new_dir = g_studiomdl.cddir.emplace();
    new_dir.append_format("%s%s/", parent_dir.c_str(), token);
}

void Cmd_Popd() {
    if (g_studiomdl.cddir.size() > 1)
        g_studiomdl.cddir.pop_back();
}


void Cmd_ExternalTextures() {
    MdlWarning("ignoring $externaltextures, obsolete...");
}

void Cmd_ClipToTextures() {
    g_studiomdl.clip_texcoords = 1;
}

void Cmd_CollapseBones() {
    g_StudioMdlContext.collapse_bones = true;
}

void Cmd_SkinnedLODs() {
    g_studiomdl.bSkinnedLODs = true;
}

void Cmd_CollapseBonesAggressive() {
    g_StudioMdlContext.collapse_bones = true;
    g_StudioMdlContext.collapse_bones_aggressive = true;
}

void Cmd_AlwaysCollapse() {
    g_StudioMdlContext.collapse_bones = true;
    GetToken(false);
    g_studiomdl.collapse.AddToTail(strdup(token));
}

void Cmd_CalcTransitions() {
    g_StudioMdlContext.multistageGraph = true;
}

void ProcessStaticProp() {
    g_studiomdl.staticprop = true;
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_STATIC_PROP;
}

void Cmd_StaticProp() {
    ProcessStaticProp();
}

void Cmd_ZBrush() {
    g_StudioMdlContext.ZBrush = true;
}

void Cmd_RealignBones() {
    g_studiomdl.realignbones = true;
}

void Cmd_BaseLOD() {
    Cmd_LOD("$lod");
}

//-----------------------------------------------------------------------------
// Key value block
//-----------------------------------------------------------------------------
static void AppendKeyValueText(std::vector<char> *pKeyValue, const char *pString) {
    int nLen = strlen(pString);
    for (int i = 0; i < nLen; ++i) { pKeyValue->emplace_back(pString[i]); }
}

int KeyValueTextSize(std::vector<char> *pKeyValue) {
    return pKeyValue->size();
}

const char *KeyValueText(std::vector<char> *pKeyValue) {
    return pKeyValue->data();
}

void Option_KeyValues(std::vector<char> *pKeyValue);

//-----------------------------------------------------------------------------
// Key value block!
//-----------------------------------------------------------------------------
void Option_KeyValues(std::vector<char> *pKeyValue) {
    // Simply read in the block between { }s as text
    // and plop it out unchanged into the .mdl file.
    // Make sure to respect the fact that we may have nested {}s
    int nLevel = 1;

    if (!GetToken(true))
        return;

    if (token[0] != '{')
        return;


    while (GetToken(true)) {
        if (!stricmp(token, "}")) {
            nLevel--;
            if (nLevel <= 0)
                break;
            AppendKeyValueText(pKeyValue, " }\n");
        }
        else if (!stricmp(token, "{")) {
            AppendKeyValueText(pKeyValue, "{\n");
            nLevel++;
        }
        else {
            // tokens inside braces are quoted
            if (nLevel > 1) {
                AppendKeyValueText(pKeyValue, "\"");
                AppendKeyValueText(pKeyValue, token);
                AppendKeyValueText(pKeyValue, "\" ");
            }
            else {
                AppendKeyValueText(pKeyValue, token);
                AppendKeyValueText(pKeyValue, " ");
            }
        }
    }

    if (nLevel >= 1) {
        TokenError("Keyvalue block missing matching braces.\n");
    }
}

void Cmd_KeyValues() {
    Option_KeyValues(&g_StudioMdlContext.KeyValueText);
}

void Cmd_ConstDirectionalLight() {
    g_studiomdl.gflags |= STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT;

    GetToken(false);
    g_studiomdl.constdirectionalightdot = (uint8_t) (verify_atof(token) * 255.0f);
}

void Cmd_MinLOD() {
    GetToken(false);
    g_StudioMdlContext.minLod = atoi(token);

    // "minlod" rules over "allowrootlods"
    if (g_StudioMdlContext.numAllowedRootLODs > 0 && g_StudioMdlContext.numAllowedRootLODs < g_StudioMdlContext.
        minLod) {
        MdlWarning("$minlod %d overrides $allowrootlods %d, proceeding with $allowrootlods %d.\n",
                   g_StudioMdlContext.minLod,
                   g_StudioMdlContext.numAllowedRootLODs, g_StudioMdlContext.minLod);
        g_StudioMdlContext.numAllowedRootLODs = g_StudioMdlContext.minLod;
    }
}

void Cmd_AllowRootLODs() {
    GetToken(false);
    g_StudioMdlContext.numAllowedRootLODs = atoi(token);

    // Root LOD restriction has to obey "minlod" request
    if (g_StudioMdlContext.numAllowedRootLODs > 0 && g_StudioMdlContext.numAllowedRootLODs < g_StudioMdlContext.
        minLod) {
        MdlWarning("$allowrootlods %d is conflicting with $minlod %d, proceeding with $allowrootlods %d.\n",
                   g_StudioMdlContext.numAllowedRootLODs, g_StudioMdlContext.minLod, g_StudioMdlContext.minLod);
        g_StudioMdlContext.numAllowedRootLODs = g_StudioMdlContext.minLod;
    }
}

void Cmd_BoneSaveFrame() {
    s_bonesaveframe_t tmp;

    // bone name
    GetToken(false);
    strcpyn(tmp.name, token);

    tmp.bSavePos = false;
    tmp.bSaveRot = false;
    tmp.bSaveRot64 = false;
    while (TokenAvailable()) {
        GetToken(false);
        if (stricmp("position", token) == 0) {
            tmp.bSavePos = true;
        }
        else if (stricmp("rotation", token) == 0) {
            tmp.bSaveRot = true;
        }
        else if (stricmp("rotation64", token) == 0) {
            tmp.bSaveRot64 = true;
        }
        else {
            MdlError("unknown option \"%s\" on $bonesaveframe : %s\n", token, tmp.name);
        }
    }

    g_studiomdl.bonesaveframe.AddToTail(tmp);
}


static bool s_bFlexClothBorderJoints = false;
QAngle s_angClothPrerotate(0, 0, 0);


void Cmd_SetDefaultFadeInTime() {
    if (!GetToken(false))
        return;

    g_StudioMdlContext.defaultFadeInTime = verify_atof(token);
}

void Cmd_SetDefaultFadeOutTime() {
    if (!GetToken(false))
        return;

    g_StudioMdlContext.defaultFadeOutTime = verify_atof(token);
}

void Cmd_LCaseAllSequences() {
    g_StudioMdlContext.lCaseAllSequences = true;
}

void Cmd_AllowActivityName() {
    if (!GetToken(false))
        return;

    g_StudioMdlContext.AllowedActivityNames.emplace_back(token);
}

void Cmd_CollisionPrecision() {
    if (!GetToken(false))
        return;

    g_StudioMdlContext.CollisionPrecision = verify_atof(token);
}

void Cmd_ErrorOnSeqRemapFail() {
    g_StudioMdlContext.errorOnSeqRemapFail = true;
}


void Cmd_SetModelIntentionallyHasZeroSequences() {
    g_StudioMdlContext.modelIntentionallyHasZeroSequences = true;
}


void Cmd_BoneMerge() {
    if (g_StudioMdlContext.createMakefile)
        return;

    g_studiomdl.BoneMerge.emplace_back();

    // bone name
    GetToken(false);
    strcpyn(g_studiomdl.BoneMerge.back().bonename, token);
}

//-----------------------------------------------------------------------------
// Purpose: create named list of boneweights
//-----------------------------------------------------------------------------
void Option_Weightlist(s_weightlist_t *pweightlist) {
    int depth = 0;
    int i;

    pweightlist->numbones = 0;

    while (1) {
        if (depth > 0) {
            if (!GetToken(true)) {
                break;
            }
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            else {
                GetToken(false);
            }
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return;
        }
        if (stricmp("{", token) == 0) {
            depth++;
        }
        else if (stricmp("}", token) == 0) {
            depth--;
        }
        else if (stricmp("posweight", token) == 0) {
            i = pweightlist->numbones - 1;
            if (i < 0) {
                MdlError("Error with specifing bone Position weight \'%s:%s\'\n", pweightlist->name,
                         pweightlist->bonename[i]);
            }
            GetToken(false);
            pweightlist->boneposweight[i] = verify_atof(token);
            if (pweightlist->boneweight[i] == 0 && pweightlist->boneposweight[i] > 0) {
                MdlError("Non-zero Position weight with zero Rotation weight not allowed \'%s:%s %f %f\'\n",
                         pweightlist->name, pweightlist->bonename[i], pweightlist->boneweight[i],
                         pweightlist->boneposweight[i]);
            }
        }
        else {
            i = pweightlist->numbones++;
            if (i >= MAXWEIGHTSPERLIST) {
                TokenError("Too many bones (%d) in weightlist '%s'\n", i, pweightlist->name);
            }
            pweightlist->bonename[i] = strdup(token);
            GetToken(false);
            pweightlist->boneweight[i] = verify_atof(token);
            pweightlist->boneposweight[i] = pweightlist->boneweight[i];
        }

        if (depth < 0) {
            TokenError("missing {\n");
        }
    };
}

void Cmd_Weightlist() {
    int i;

    if (!GetToken(false))
        return;

    if (g_studiomdl.numweightlist >= MAXWEIGHTLISTS) {
        TokenError("Too many weightlist commands (%d)\n", MAXWEIGHTLISTS);
    }

    for (i = 1; i < g_studiomdl.numweightlist; i++) {
        if (stricmp(g_studiomdl.weightlist[i].name, token) == 0) {
            TokenError("Duplicate weightlist '%s'\n", token);
        }
    }

    strcpyn(g_studiomdl.weightlist[i].name, token);

    Option_Weightlist(&g_studiomdl.weightlist[g_studiomdl.numweightlist]);

    g_studiomdl.numweightlist++;
}

void Cmd_DefaultWeightlist() {
    Option_Weightlist(&g_studiomdl.weightlist[0]);
}

/*
=================
Cmd_Origin
=================
*/
void Cmd_Origin() {
    GetToken(false);
    g_studiomdl.defaultadjust.x = verify_atof(token);

    GetToken(false);
    g_studiomdl.defaultadjust.y = verify_atof(token);

    GetToken(false);
    g_studiomdl.defaultadjust.z = verify_atof(token);

    if (TokenAvailable()) {
        GetToken(false);
        g_studiomdl.defaultrotation.z = DEG2RAD(verify_atof(token) + 90);
    }
}

//-----------------------------------------------------------------------------
// Purpose: Set the default root rotation so that the Y axis is up instead of the Z axis (for Maya)
//-----------------------------------------------------------------------------
void ProcessUpAxis(const RadianEuler &angles) {
    g_studiomdl.defaultrotation = angles;
}


//-----------------------------------------------------------------------------
// Purpose: Set the default root rotation so that the Y axis is up instead of the Z axis (for Maya)
//-----------------------------------------------------------------------------
void Cmd_UpAxis() {
    // We want to create a rotation that rotates from the art space
    // (specified by the up direction) to a z up space
    // Note: x, -x, -y are untested
    RadianEuler angles(0.0f, 0.0f, M_PI / 2.0f);
    GetToken(false);
    if (!Q_stricmp(token, "x")) {
        // rotate 90 degrees around y to move x into z
        angles.x = 0.0f;
        angles.y = M_PI / 2.0f;
    }
    else if (!Q_stricmp(token, "-x")) {
        // untested
        angles.x = 0.0f;
        angles.y = -M_PI / 2.0f;
    }
    else if (!Q_stricmp(token, "y")) {
        // rotate 90 degrees around x to move y into z
        angles.x = M_PI / 2.0f;
        angles.y = 0.0f;
    }
    else if (!Q_stricmp(token, "-y")) {
        // untested
        angles.x = -M_PI / 2.0f;
        angles.y = 0.0f;
    }
    else if (!Q_stricmp(token, "z")) {
        // there's still a built in 90 degree Z rotation :(
        angles.x = 0.0f;
        angles.y = 0.0f;
    }
    else if (!Q_stricmp(token, "-z")) {
        // there's still a built in 90 degree Z rotation :(
        angles.x = 0.0f;
        angles.y = 0.0f;
    }
    else {
        TokenError("unknown $upaxis option: \"%s\"\n", token);
        return;
    }

    ProcessUpAxis(angles);
}


void Cmd_ScaleUp() {
    GetToken(false);
    g_studiomdl.defaultscale = verify_atof(token);

    g_studiomdl.currentscale = g_studiomdl.defaultscale;
}

//-----------------------------------------------------------------------------
// Purpose: Sets how what size chunks to cut the animations into
//-----------------------------------------------------------------------------
void Cmd_AnimBlockSize() {
    GetToken(false);
    g_studiomdl.animblocksize = verify_atoi(token);
    if (g_studiomdl.animblocksize < 1024) {
        g_studiomdl.animblocksize *= 1024;
    }
    while (TokenAvailable()) {
        GetToken(false);
        if (!Q_stricmp(token, "nostall")) {
            g_StudioMdlContext.noAnimblockStall = true;
        }
        else if (!Q_stricmp(token, "highres")) {
            g_StudioMdlContext.animblockHighRes = true;
            g_StudioMdlContext.animblockLowRes = false;
        }
        else if (!Q_stricmp(token, "lowres")) {
            g_StudioMdlContext.animblockLowRes = true;
            g_StudioMdlContext.animblockHighRes = false;
        }
        else if (!Q_stricmp(token, "numframes")) {
            GetToken(false);
            g_StudioMdlContext.maxZeroFrames = clamp(atoi(token), 1, 4);
        }
        else if (!Q_stricmp(token, "cachehighres")) {
            g_StudioMdlContext.zeroFramesHighres = true;
        }
        else if (!Q_stricmp(token, "posdelta")) {
            GetToken(false);
            g_StudioMdlContext.minZeroFramePosDelta = atof(token);
        }
        else {
            MdlError("unknown option \"%s\" on $animblocksize command\n");
        }
    }
}

void Cmd_AppendSource() {
    if (!GetToken(false))
        return;

    s_source_t *pOrigSource = g_studiomdl.model[0]->source;
    s_source_t *pAppendSource = Load_Source(token, "", false, false,
                                            false
                                            /* don't use cached lookup, since this might be a dup of the starting src */);

    matrix3x4_t matTemp;
    matTemp.SetToIdentity();
    matTemp.ScaleUpper3x3Matrix(g_studiomdl.currentscale);

    if (TokenAvailable()) {
        GetToken(false);

        if (!V_strncmp(token, "offset", 6)) {
            Vector vecOffsetPosition;
            vecOffsetPosition.Init();
            QAngle angOffsetAngle;
            angOffsetAngle.Init();
            float flScale = 1;

            int nCount = sscanf(token, "offset pos[ %f %f %f ] angle[ %f %f %f ] scale[ %f ]",
                                &vecOffsetPosition.x, &vecOffsetPosition.y, &vecOffsetPosition.z,
                                &angOffsetAngle.x, &angOffsetAngle.y, &angOffsetAngle.z,
                                &flScale);

            if (nCount == 7) {
                AngleMatrix(angOffsetAngle, vecOffsetPosition, matTemp);
                matTemp.ScaleUpper3x3Matrix(flScale * (1.0f / g_studiomdl.currentscale));
            }
            else {
                MdlError("Malformed offset parameters to $appendsource.");
                return;
            }
        }
        else {
            UnGetToken();
        }
    }

    AddSrcToSrc(pOrigSource, pAppendSource, matTemp);
}

void Cmd_FlexOptimize() {
    // $flexoptimize auto|on|off [min static verts]
    GetToken(false);
    if (!ParseFlexOptimizeMode(token, g_StudioMdlContext.flexOptimizeMode)) {
        TokenError("$flexoptimize expects auto, on or off, got \"%s\"\n", token);
    }
    if (TokenAvailable()) {
        GetToken(false);
        g_StudioMdlContext.flexOptimizeMinStatic = verify_atoi(token);
    }
}

void Cmd_maxVerts() {
    // first limit
    GetToken(false);
    g_StudioMdlContext.maxVertexLimit = clamp(atoi(token), 1024, MAXSTUDIOVERTS);
    g_StudioMdlContext.maxVertexClamp = MIN(g_StudioMdlContext.maxVertexLimit, MAXSTUDIOVERTS / 2);

    if (TokenAvailable()) {
        // actual target limit
        GetToken(false);
        g_StudioMdlContext.maxVertexClamp = clamp(atoi(token), 1024, MAXSTUDIOVERTS);
    }
}

s_sequence_t *LookupSequence(const char *name) {
    int i;
    for (i = 0; i < g_studiomdl.sequence.Count(); ++i) {
        if (!Q_stricmp(g_studiomdl.sequence[i].name, name))
            return &g_studiomdl.sequence[i];
    }
    return nullptr;
}


s_animation_t *LookupAnimation(const char *name, int nFallbackRecursionDepth) {
    int i;
    for (i = 0; i < g_studiomdl.animations.size(); i++) {
        if (!Q_stricmp(g_studiomdl.animations[i]->name, name))
            return g_studiomdl.animations[i];
    }

    s_sequence_t *pseq = LookupSequence(name);

    // Used to just return pseq->panim[0][0] but pseq->panim is
    // a CUtlVectorAuto which expands the array on access as necessary
    // but seems to fill it with random data on expansion, so prevent
    // that here because we're doing a lookup to see if something
    // already exists
    if (pseq && pseq->panim.Count() > 0) {
        CUtlVectorAuto<s_animation_t *> &animList = pseq->panim[0];
        if (animList.Count() > 0)
            return animList[0];
    }

    // check optional fallbacks or reserved name syntax

    if (nFallbackRecursionDepth == 0 && !V_strcmp(name, "this")) {
        return LookupAnimation(g_StudioMdlContext.szInCurrentSeqName, 1);
    }

    return nullptr;
}

s_animation_t *LookupAnimation(const char *name) {
    return LookupAnimation(name, 0);
}


int Option_AnimTag(s_sequence_t *psequence) {
    if (psequence->animtags.full()) {
        TokenError("too many animtags\n");
    }

    if (!GetToken(false)) {
        TokenError("expected animtag name\n");
    }

    auto &animtag = psequence->animtags.emplace_back(s_animtag_t{
        .tagname = token
    });


    if (!GetToken(false)) {
        TokenError("expected animtag cycle\n");
    }
    animtag.cycle = verify_atof(token);


    return 0;
}

int Option_Event(s_sequence_t *psequence) {
    if (psequence->events.full()) {
        TokenError("too many events\n");
    }

    if (!GetToken(false)) {
        TokenError("expected event name\n");
        return 1;
    }

    auto &event = psequence->events.emplace_back(s_event_t{
        .eventname = token
    });

    if (!GetToken(false)) {
        TokenError("expected event frame\n");
        return 1;
    }
    event.frame = verify_atoi(token);

    // option token
    if (TokenAvailable()) {
        GetToken(false);
        if (token[0] == '}') // opps, hit the end
            return 1;
        // found an option
        event.options = token;
    }

    return 0;
}


void Option_IKRule(s_ikrule_t *pRule) {
    // chain
    GetToken(false);

    int i;
    for (i = 0; i < g_studiomdl.numikchains; i++) {
        if (stricmp(token, g_studiomdl.ikchain[i].name) == 0) {
            break;
        }
    }
    if (i >= g_studiomdl.numikchains) {
        TokenError("unknown chain \"%s\" in ikrule\n", token);
    }
    pRule->chain = i;
    // default slot
    pRule->slot = i;

    // type
    GetToken(false);

    if (stricmp(token, "autosteps") == 0) {
        GetToken(false);
        pRule->end = verify_atoi(token);

        GetToken(false);
        strcpyn(pRule->bonename, token);

        pRule->type = IK_GROUND;

        pRule->height = g_studiomdl.ikchain[pRule->chain].height;
        pRule->floor = g_studiomdl.ikchain[pRule->chain].floor;
        pRule->radius = g_studiomdl.ikchain[pRule->chain].radius;

        pRule->start = -2;
        pRule->peak = -1;
        pRule->tail = -1;
    }
    else if (stricmp(token, "touch") == 0) {
        pRule->type = IK_SELF;

        // bone
        GetToken(false);
        strcpyn(pRule->bonename, token);
    }
    else if (stricmp(token, "footstep") == 0) {
        pRule->type = IK_GROUND;

        pRule->height = g_studiomdl.ikchain[pRule->chain].height;
        pRule->floor = g_studiomdl.ikchain[pRule->chain].floor;
        pRule->radius = g_studiomdl.ikchain[pRule->chain].radius;
    }
    else if (stricmp(token, "attachment") == 0) {
        pRule->type = IK_ATTACHMENT;

        // name of attachment
        GetToken(false);
        strcpyn(pRule->attachment, token);
    }
    else if (stricmp(token, "release") == 0) {
        pRule->type = IK_RELEASE;
    }
    else if (stricmp(token, "unlatch") == 0) {
        pRule->type = IK_UNLATCH;
    }

    pRule->contact = -1;

    while (TokenAvailable()) {
        GetToken(false);
        if (stricmp(token, "height") == 0) {
            GetToken(false);
            pRule->height = verify_atof(token);
        }
        else if (stricmp(token, "target") == 0) {
            // slot
            GetToken(false);
            pRule->slot = verify_atoi(token);
        }
        else if (stricmp(token, "range") == 0) {
            // ramp
            GetToken(false);
            if (token[0] == '.')
                pRule->start = -1;
            else
                pRule->start = verify_atoi(token);

            GetToken(false);
            if (token[0] == '.')
                pRule->peak = -1;
            else
                pRule->peak = verify_atoi(token);

            GetToken(false);
            if (token[0] == '.')
                pRule->tail = -1;
            else
                pRule->tail = verify_atoi(token);

            GetToken(false);
            if (token[0] == '.')
                pRule->end = -1;
            else
                pRule->end = verify_atoi(token);
        }
        else if (stricmp(token, "floor") == 0) {
            GetToken(false);
            pRule->floor = verify_atof(token);
        }
        else if (stricmp(token, "pad") == 0) {
            GetToken(false);
            pRule->radius = verify_atof(token) / 2.0f;
        }
        else if (stricmp(token, "radius") == 0) {
            GetToken(false);
            pRule->radius = verify_atof(token);
        }
        else if (stricmp(token, "contact") == 0) {
            GetToken(false);
            pRule->contact = verify_atoi(token);
        }
        else if (stricmp(token, "usesequence") == 0) {
            pRule->usesequence = true;
            pRule->usesource = false;
        }
        else if (stricmp(token, "usesource") == 0) {
            pRule->usesequence = false;
            pRule->usesource = true;
        }
        else if (stricmp(token, "fakeorigin") == 0) {
            GetToken(false);
            pRule->pos.x = verify_atof(token);
            GetToken(false);
            pRule->pos.y = verify_atof(token);
            GetToken(false);
            pRule->pos.z = verify_atof(token);

            pRule->bone = -1;
        }
        else if (stricmp(token, "fakerotate") == 0) {
            QAngle ang;

            GetToken(false);
            ang.x = verify_atof(token);
            GetToken(false);
            ang.y = verify_atof(token);
            GetToken(false);
            ang.z = verify_atof(token);

            AngleQuaternion(ang, pRule->q);

            pRule->bone = -1;
        }
        else if (stricmp(token, "bone") == 0) {
            strcpy(pRule->bonename, token);
        }
        else {
            UnGetToken();
            return;
        }
    }
}


float verify_atof_with_null(const char *token) {
    if (strcmp(token, "..") == 0)
        return -1;

    if (token[0] != '-' && token[0] != '.' && (token[0] < '0' || token[0] > '9')) {
        TokenError("expecting float, got \"%s\"\n", token);
    }
    return atof(token);
}


//-----------------------------------------------------------------------------
// Purpose: parse order dependant s_animcmd_t token for $animations
//-----------------------------------------------------------------------------
int ParseCmdlistToken(s_cmdlist_t::AnimCmdVector &cmds) {
    if (cmds.size() >= MAXSTUDIOCMDS) {
        return false;
    }

    s_animcmd_t &cmd = cmds.emplace_back();
    if (stricmp("fixuploop", token) == 0) {
        cmd.cmd = CMD_FIXUP;

        GetToken(false);
        cmd.u.fixuploop.start = verify_atoi(token);
        GetToken(false);
        cmd.u.fixuploop.end = verify_atoi(token);
    }
    else if (strnicmp("weightlist", token, 6) == 0) {
        GetToken(false);

        int i;
        for (i = 1; i < g_studiomdl.numweightlist; i++) {
            if (stricmp(g_studiomdl.weightlist[i].name, token) == 0) {
                break;
            }
        }
        if (i == g_studiomdl.numweightlist) {
            TokenError("unknown weightlist '%s\'\n", token);
        }
        cmd.cmd = CMD_WEIGHTS;
        cmd.u.weightlist.index = i;
    }
    else if (stricmp("subtract", token) == 0) {
        cmd.cmd = CMD_SUBTRACT;

        GetToken(false);

        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown subtract animation '%s\'\n", token);
        }

        cmd.u.subtract.ref = extanim;

        GetToken(false);
        cmd.u.subtract.frame = verify_atoi(token);

        cmd.u.subtract.flags |= STUDIO_POST;
    }
    else if (stricmp("presubtract", token) == 0) // FIXME: rename this to something better
    {
        cmd.cmd = CMD_SUBTRACT;

        GetToken(false);

        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown presubtract animation '%s\'\n", token);
        }

        cmd.u.subtract.ref = extanim;

        GetToken(false);
        cmd.u.subtract.frame = verify_atoi(token);
    }
    else if (stricmp("alignto", token) == 0) {
        cmd.cmd = CMD_AO;

        cmd.u.ao.pBonename = nullptr;

        GetToken(false);
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown alignto animation '%s\'\n", token);
        }

        cmd.u.ao.ref = extanim;
        cmd.u.ao.motiontype = STUDIO_X | STUDIO_Y;
        cmd.u.ao.srcframe = 0;
        cmd.u.ao.destframe = 0;
    }
    else if (stricmp("align", token) == 0) {
        cmd.cmd = CMD_AO;

        cmd.u.ao.pBonename = nullptr;

        GetToken(false);
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown align animation '%s\'\n", token);
        }

        cmd.u.ao.ref = extanim;

        // motion type to match
        cmd.u.ao.motiontype = 0;
        GetToken(false);
        int ctrl;
        while ((ctrl = lookupControl(token)) != -1) {
            cmd.u.ao.motiontype |= ctrl;
            GetToken(false);
        }
        if (cmd.u.ao.motiontype == 0) {
            TokenError("missing controls on align\n");
        }

        // frame of reference animation to match
        cmd.u.ao.srcframe = verify_atoi(token);

        // against what frame of the current animation
        GetToken(false);
        cmd.u.ao.destframe = verify_atoi(token);
    }
    else if (stricmp("alignboneto", token) == 0) {
        cmd.cmd = CMD_AO;

        GetToken(false);
        cmd.u.ao.pBonename = strdup(token);

        GetToken(false);
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown alignboneto animation '%s\'\n", token);
        }

        cmd.u.ao.ref = extanim;
        cmd.u.ao.motiontype = STUDIO_X | STUDIO_Y;
        cmd.u.ao.srcframe = 0;
        cmd.u.ao.destframe = 0;
    }
    else if (stricmp("alignbone", token) == 0) {
        cmd.cmd = CMD_AO;

        GetToken(false);
        cmd.u.ao.pBonename = strdup(token);

        GetToken(false);
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown alignboneto animation '%s\'\n", token);
        }

        cmd.u.ao.ref = extanim;

        // motion type to match
        cmd.u.ao.motiontype = 0;
        GetToken(false);
        int ctrl;
        while ((ctrl = lookupControl(token)) != -1) {
            cmd.u.ao.motiontype |= ctrl;
            GetToken(false);
        }
        if (cmd.u.ao.motiontype == 0) {
            TokenError("missing controls on align\n");
        }

        // frame of reference animation to match
        cmd.u.ao.srcframe = verify_atoi(token);

        // against what frame of the current animation
        GetToken(false);
        cmd.u.ao.destframe = verify_atoi(token);
    }
    else if (stricmp("match", token) == 0) {
        cmd.cmd = CMD_MATCH;

        GetToken(false);

        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown match animation '%s\'\n", token);
        }

        cmd.u.match.ref = extanim;
    }
    else if (stricmp("matchblend", token) == 0) {
        cmd.cmd = CMD_MATCHBLEND;

        GetToken(false);

        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            MdlError("unknown match animation '%s\'\n", token);
        }

        cmd.u.match.ref = extanim;

        // frame of reference animation to match
        GetToken(false);
        cmd.u.match.srcframe = verify_atoi(token);

        // against what frame of the current animation
        GetToken(false);
        cmd.u.match.destframe = verify_atoi(token);

        // backup and starting match in here
        GetToken(false);
        cmd.u.match.destpre = verify_atoi(token);

        // continue blending match till here
        GetToken(false);
        cmd.u.match.destpost = verify_atoi(token);
    }
    else if (stricmp("worldspaceblend", token) == 0) {
        cmd.cmd = CMD_WORLDSPACEBLEND;

        GetToken(false);

        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown worldspaceblend animation '%s\'\n", token);
        }

        cmd.u.world.ref = extanim;
        cmd.u.world.startframe = 0;
        cmd.u.world.loops = false;
    }
    else if (stricmp("worldspaceblendloop", token) == 0) {
        cmd.cmd = CMD_WORLDSPACEBLEND;

        GetToken(false);

        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown worldspaceblend animation '%s\'\n", token);
        }

        cmd.u.world.ref = extanim;

        GetToken(false);
        cmd.u.world.startframe = atoi(token);

        cmd.u.world.loops = true;
    }
    else if (stricmp("rotateto", token) == 0) {
        cmd.cmd = CMD_ANGLE;

        GetToken(false);
        cmd.u.angle.angle = verify_atof(token);
    }
    else if (stricmp("ikrule", token) == 0) {
        cmd.cmd = CMD_IKRULE;

        cmd.u.ikrule.pRule = (s_ikrule_t *) calloc(1, sizeof(s_ikrule_t));

        Option_IKRule(cmd.u.ikrule.pRule);
    }
    else if (stricmp("ikfixup", token) == 0) {
        cmd.cmd = CMD_IKFIXUP;

        cmd.u.ikfixup.pRule = (s_ikrule_t *) calloc(1, sizeof(s_ikrule_t));

        Option_IKRule(cmd.u.ikrule.pRule);
    }
    else if (stricmp("walkframe", token) == 0) {
        cmd.cmd = CMD_MOTION;

        // frame
        GetToken(false);
        cmd.u.motion.iEndFrame = verify_atoi(token);

        // motion type to match
        cmd.u.motion.motiontype = 0;
        while (TokenAvailable()) {
            GetToken(false);
            int ctrl = lookupControl(token);
            if (ctrl != -1) {
                cmd.u.motion.motiontype |= ctrl;
            }
            else {
                UnGetToken();
                break;
            }
        }

        /*
		GetToken( false ); // X
		cmd.u.motion.x = verify_atof( token );

		GetToken( false ); // Y
		cmd.u.motion.y = verify_atof( token );

		GetToken( false ); // A
		cmd.u.motion.zr = verify_atof( token );
		*/
    }
    else if (stricmp("walkalignto", token) == 0) {
        cmd.cmd = CMD_REFMOTION;

        GetToken(false);
        cmd.u.motion.iEndFrame = verify_atoi(token);

        cmd.u.motion.iSrcFrame = cmd.u.motion.iEndFrame;

        GetToken(false); // reference animation
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown alignto animation '%s\'\n", token);
        }
        cmd.u.motion.pRefAnim = extanim;

        cmd.u.motion.iRefFrame = 0;

        // motion type to match
        cmd.u.motion.motiontype = 0;
        while (TokenAvailable()) {
            GetToken(false);
            int ctrl = lookupControl(token);
            if (ctrl != -1) {
                cmd.u.motion.motiontype |= ctrl;
            }
            else {
                UnGetToken();
                break;
            }
        }


        /*
		GetToken( false ); // X
		cmd.u.motion.x = verify_atof( token );

		GetToken( false ); // Y
		cmd.u.motion.y = verify_atof( token );

		GetToken( false ); // A
		cmd.u.motion.zr = verify_atof( token );
		*/
    }
    else if (stricmp("walkalign", token) == 0) {
        cmd.cmd = CMD_REFMOTION;

        // end frame to apply motion over
        GetToken(false);
        cmd.u.motion.iEndFrame = verify_atoi(token);

        // reference animation
        GetToken(false);
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown alignto animation '%s\'\n", token);
        }
        cmd.u.motion.pRefAnim = extanim;

        // motion type to match
        cmd.u.motion.motiontype = 0;
        while (TokenAvailable()) {
            GetToken(false);
            int ctrl = lookupControl(token);
            if (ctrl != -1) {
                cmd.u.motion.motiontype |= ctrl;
            }
            else {
                break;
            }
        }
        if (cmd.u.motion.motiontype == 0) {
            TokenError("missing controls on walkalign\n");
        }

        // frame of reference animation to match
        cmd.u.motion.iRefFrame = verify_atoi(token);

        // against what frame of the current animation
        GetToken(false);
        cmd.u.motion.iSrcFrame = verify_atoi(token);
    }
    else if (stricmp("derivative", token) == 0) {
        cmd.cmd = CMD_DERIVATIVE;

        // get scale
        GetToken(false);
        cmd.u.derivative.scale = verify_atof(token);
    }
    else if (stricmp("noanimation", token) == 0) {
        cmd.cmd = CMD_NOANIMATION;
    }
    else if (stricmp("noanim_keepduration", token) == 0) {
        cmd.cmd = CMD_NOANIM_KEEPDURATION;
    }
    else if (stricmp("lineardelta", token) == 0) {
        cmd.cmd = CMD_LINEARDELTA;
        cmd.u.linear.flags |= STUDIO_AL_POST;
    }
    else if (stricmp("splinedelta", token) == 0) {
        cmd.cmd = CMD_LINEARDELTA;
        cmd.u.linear.flags |= STUDIO_AL_POST;
        cmd.u.linear.flags |= STUDIO_AL_SPLINE;
    }
    else if (stricmp("compress", token) == 0) {
        cmd.cmd = CMD_COMPRESS;

        // get frames to skip
        GetToken(false);
        cmd.u.compress.frames = verify_atoi(token);
    }
    else if (stricmp("numframes", token) == 0) {
        cmd.cmd = CMD_NUMFRAMES;

        // get frames to force
        GetToken(false);
        cmd.u.compress.frames = verify_atoi(token);
    }
    else if (stricmp("counterrotate", token) == 0) {
        cmd.cmd = CMD_COUNTERROTATE;

        // get bone name
        GetToken(false);
        cmd.u.counterrotate.pBonename = strdup(token);
    }
    else if (stricmp("counterrotateto", token) == 0) {
        cmd.cmd = CMD_COUNTERROTATE;

        cmd.u.counterrotate.bHasTarget = true;

        // get pitch
        GetToken(false);
        cmd.u.counterrotate.targetAngle[0] = verify_atof(token);

        // get yaw
        GetToken(false);
        cmd.u.counterrotate.targetAngle[1] = verify_atof(token);

        // get roll
        GetToken(false);
        cmd.u.counterrotate.targetAngle[2] = verify_atof(token);

        // get bone name
        GetToken(false);
        cmd.u.counterrotate.pBonename = strdup(token);
    }
    else if (stricmp("localhierarchy", token) == 0) {
        cmd.cmd = CMD_LOCALHIERARCHY;

        // get bone name
        GetToken(false);
        cmd.u.localhierarchy.pBonename = strdup(token);

        // get parent name
        GetToken(false);
        cmd.u.localhierarchy.pParentname = strdup(token);

        cmd.u.localhierarchy.start = -1;
        cmd.u.localhierarchy.peak = -1;
        cmd.u.localhierarchy.tail = -1;
        cmd.u.localhierarchy.end = -1;

        if (TokenAvailable()) {
            GetToken(false);
            if (stricmp(token, "range") == 0) {
                //
                GetToken(false);
                cmd.u.localhierarchy.start = verify_atof_with_null(token);

                //
                GetToken(false);
                cmd.u.localhierarchy.peak = verify_atof_with_null(token);

                //
                GetToken(false);
                cmd.u.localhierarchy.tail = verify_atof_with_null(token);

                //
                GetToken(false);
                cmd.u.localhierarchy.end = verify_atof_with_null(token);
            }
            else {
                UnGetToken();
            }
        }
    }
    else if (stricmp("forceboneposrot", token) == 0) {
        cmd.cmd = CMD_FORCEBONEPOSROT;

        // get bone name
        GetToken(false);
        cmd.u.forceboneposrot.pBonename = strdup(token);

        cmd.u.forceboneposrot.bDoPos = false;
        cmd.u.forceboneposrot.bDoRot = false;

        cmd.u.forceboneposrot.pos[0] = 0;
        cmd.u.forceboneposrot.pos[1] = 0;
        cmd.u.forceboneposrot.pos[2] = 0;

        cmd.u.forceboneposrot.rot[0] = 0;
        cmd.u.forceboneposrot.rot[1] = 0;
        cmd.u.forceboneposrot.rot[2] = 0;

        if (TokenAvailable()) {
            GetToken(false);
            if (stricmp(token, "pos") == 0) {
                cmd.u.forceboneposrot.bDoPos = true;

                GetToken(false);
                cmd.u.forceboneposrot.pos[0] = verify_atof_with_null(token);

                GetToken(false);
                cmd.u.forceboneposrot.pos[1] = verify_atof_with_null(token);

                GetToken(false);
                cmd.u.forceboneposrot.pos[2] = verify_atof_with_null(token);
            }
            else {
                UnGetToken();
            }

            if (TokenAvailable()) {
                GetToken(false);
                if (stricmp(token, "rot") == 0) {
                    cmd.u.forceboneposrot.bDoRot = true;

                    GetToken(false);
                    cmd.u.forceboneposrot.rot[0] = verify_atof_with_null(token);

                    GetToken(false);
                    cmd.u.forceboneposrot.rot[1] = verify_atof_with_null(token);

                    GetToken(false);
                    cmd.u.forceboneposrot.rot[2] = verify_atof_with_null(token);

                    cmd.u.forceboneposrot.bRotIsLocal = false;
                    if (TokenAvailable()) {
                        GetToken(false);
                        if (stricmp(token, "local") == 0) {
                            cmd.u.forceboneposrot.bRotIsLocal = true;
                        }
                        else {
                            UnGetToken();
                        }
                    }
                }
                else {
                    UnGetToken();
                }
            }
        }
    }
    else if (stricmp("bonedriver", token) == 0) {
        cmd.cmd = CMD_BONEDRIVER;

        cmd.u.bonedriver.iAxis = 0;
        cmd.u.bonedriver.value = 1.0f;
        cmd.u.bonedriver.all = true;

        // get bone name
        GetToken(false);
        cmd.u.bonedriver.pBonename = strdup(token);

        if (TokenAvailable()) {
            GetToken(false);
            if (stricmp(token, "axis") == 0) {
                GetToken(false);
                if (stricmp(token, "x") == 0) {
                    cmd.u.bonedriver.iAxis = 0;
                }
                else if (stricmp(token, "y") == 0) {
                    cmd.u.bonedriver.iAxis = 1;
                }
                else if (stricmp(token, "z") == 0) {
                    cmd.u.bonedriver.iAxis = 2;
                }
                else {
                    TokenError("Unknown bonedriver axis.\n");
                }
            }
            else {
                UnGetToken();
            }
        }

        if (TokenAvailable()) {
            GetToken(false);
            if (stricmp(token, "value") == 0) {
                GetToken(false);
                cmd.u.bonedriver.value = verify_atof_with_null(token);
            }
            else {
                UnGetToken();
            }
        }

        if (TokenAvailable()) {
            GetToken(false);
            if (stricmp(token, "range") == 0) {
                cmd.u.bonedriver.all = false;

                GetToken(false);
                cmd.u.bonedriver.start = verify_atoi(token);

                GetToken(false);
                cmd.u.bonedriver.peak = verify_atoi(token);

                GetToken(false);
                cmd.u.bonedriver.tail = verify_atoi(token);

                GetToken(false);
                cmd.u.bonedriver.end = verify_atoi(token);
            }
            else {
                UnGetToken();
            }
        }
    }
    else if (stricmp("reverse", token) == 0) {
        cmd.cmd = CMD_REVERSE;
    }
    else if (stricmp("appendanim", token) == 0) {
        cmd.cmd = CMD_APPENDANIM;

        GetToken(false); // reference animation
        s_animation_t *extanim = LookupAnimation(token);
        if (extanim == nullptr) {
            TokenError("unknown appendanim '%s\'\n", token);
        }
        cmd.u.appendanim.ref = extanim;
    }
    else {
        return false;
    }
    return true;
}

void Cmd_Cmdlist() {
    int depth = 0;

    // name
    GetToken(false);
    auto &cmd = g_studiomdl.cmdlists.emplace_back(s_cmdlist_t{
        .name = token,
    });

    while (1) {
        if (depth > 0) {
            if (!GetToken(true)) {
                break;
            }
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            GetToken(false);
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return;
        }
        if (stricmp("{", token) == 0) {
            depth++;
        }
        else if (stricmp("}", token) == 0) {
            depth--;
        }
        else if (ParseCmdlistToken(cmd.cmds)) {
        }
        else {
            TokenError("unknown command: %s\n", token);
        }

        if (depth < 0) {
            TokenError("missing {\n");
        }
    };
}

//-----------------------------------------------------------------------------
// Purpose: parse order independant s_animation_t token for $animations
//-----------------------------------------------------------------------------
bool ParseAnimationToken(s_animation_t *panim) {
    if (!Q_stricmp("if", token)) {
        // fixme: add expression evaluation
        GetToken(false);
        if (atoi(token) == 0 && stricmp(token, "true") != 0) {
            GetToken(true);
            if (token[0] == '{') {
                int depth = 1;
                while (TokenAvailable() && depth > 0) {
                    GetToken(true);
                    if (stricmp("{", token) == 0) {
                        depth++;
                    }
                    else if (stricmp("}", token) == 0) {
                        depth--;
                    }
                }
            }
        }
        return true;
    }

    if (!Q_stricmp("fps", token)) {
        GetToken(false);
        panim->fps = verify_atof(token);
        if (panim->fps <= 0.0f) {
            TokenError("ParseAnimationToken:  fps (%f from '%s') <= 0.0\n", panim->fps, token);
        }
        return true;
    }

    if (!Q_stricmp("origin", token)) {
        GetToken(false);
        panim->adjust.x = verify_atof(token);

        GetToken(false);
        panim->adjust.y = verify_atof(token);

        GetToken(false);
        panim->adjust.z = verify_atof(token);
        return true;
    }

    if (!Q_stricmp("rotate", token)) {
        GetToken(false);
        // FIXME: broken for Maya
        panim->rotation.z = DEG2RAD(verify_atof(token) + 90);
        return true;
    }

    if (!Q_stricmp("angles", token)) {
        GetToken(false);
        panim->rotation.x = DEG2RAD(verify_atof(token));
        GetToken(false);
        panim->rotation.y = DEG2RAD(verify_atof(token));
        GetToken(false);
        panim->rotation.z = DEG2RAD(verify_atof(token) + 90.0f);
        return true;
    }

    if (!Q_stricmp("scale", token)) {
        GetToken(false);
        panim->scale = verify_atof(token);
        return true;
    }

    if (!Q_strnicmp("loop", token, 4)) {
        panim->flags |= STUDIO_LOOPING;
        return true;
    }

    if (!Q_stricmp("noforceloop", token)) {
        panim->flags |= STUDIO_NOFORCELOOP;
        return true;
    }

    if (!Q_strcmp("startloop", token)) {
        GetToken(false);
        panim->looprestart = verify_atoi(token);
        if (panim->looprestartpercent != 0) {
            MdlError("Can't specify startloop for animation %s, percentstartloop already specified.",
                     panim->name.c_str());
        }
        panim->flags |= STUDIO_LOOPING;
        return true;
    }

    if (!Q_strcmp("percentstartloop", token)) {
        GetToken(false);
        panim->looprestartpercent = verify_atof(token);
        if (panim->looprestart != 0) {
            MdlError("Can't specify percentstartloop for animation %s, looprestart already specified.",
                     panim->name.c_str());
        }
        panim->flags |= STUDIO_LOOPING;
        return true;
    }

    if (!Q_stricmp("fudgeloop", token)) {
        panim->fudgeloop = true;
        panim->flags |= STUDIO_LOOPING;
        return true;
    }

    if (!Q_strnicmp("snap", token, 4)) {
        panim->flags |= STUDIO_SNAP;
        return true;
    }

    if (!Q_strnicmp("frame", token, 5) || !Q_strnicmp("framestart", token, 10)) {
        // framestart assumes the animation's end frame is ok to use no matter what it is. This is better than finding 'frame 9 10000' in qc script
        bool bUseDefaultEndFrame = (!Q_strnicmp("framestart", token, 10));

        GetToken(false);
        panim->startframe = verify_atoi(token);

        if (!bUseDefaultEndFrame) {
            GetToken(false);
            panim->endframe = verify_atoi(token);
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

        if (!g_StudioMdlContext.createMakefile && panim->endframe < panim->startframe) {
            TokenError("end frame before start frame in %s", panim->name.c_str());
        }

        panim->numframes = panim->endframe - panim->startframe + 1;

        return true;
    }

    if (!Q_stricmp("blockname", token)) {
        GetToken(false);
        s_sourceanim_t *pSourceAnim = FindSourceAnim(panim->source, token);

        // NOTE: This always affects the first source anim read in
        if (pSourceAnim) {
            panim->startframe = pSourceAnim->startframe;
            panim->endframe = pSourceAnim->endframe;

            if (!g_StudioMdlContext.createMakefile && panim->endframe < panim->startframe) {
                TokenError("end frame before start frame in %s", panim->name.c_str());
            }

            panim->numframes = panim->endframe - panim->startframe + 1;
            Q_strncpy(panim->animationname, token, sizeof(panim->animationname));
        }
        else {
            MdlError("Requested unknown animation block name %s\n", token);
        }
        return true;
    }

    if (!Q_stricmp("post", token)) {
        panim->flags |= STUDIO_POST;
        return true;
    }

    if (!Q_stricmp("noautoik", token)) {
        panim->noAutoIK = true;
        return true;
    }

    if (!Q_stricmp("autoik", token)) {
        panim->noAutoIK = false;
        return true;
    }

    if (ParseCmdlistToken(panim->cmds))
        return true;

    if (!Q_stricmp("cmdlist", token)) {
        GetToken(false); // A

        int i;
        for (i = 0; i < g_studiomdl.cmdlists.size(); i++) {
            if (stricmp(g_studiomdl.cmdlists[i].name, token) == 0) {
                break;
            }
        }
        if (i == g_studiomdl.cmdlists.size())
            TokenError("unknown cmdlist %s\n", token);

        for (int j = 0; j < g_studiomdl.cmdlists[i].cmds.size(); j++) {
            if (panim->cmds.size() >= MAXSTUDIOCMDS) {
                TokenError("Too many cmds in %s\n", panim->name.c_str());
            }
            panim->cmds.emplace_back(g_studiomdl.cmdlists[i].cmds[j]);
        }
        return true;
    }

    if (!Q_stricmp("motionrollback", token)) {
        GetToken(false);
        panim->motionrollback = atof(token);
        return true;
    }

    if (!Q_stricmp("noanimblock", token)) {
        panim->disableAnimblocks = true;
        return true;
    }

    if (!Q_stricmp("noanimblockstall", token)) {
        panim->isFirstSectionLocal = true;
        return true;
    }

    if (!Q_stricmp("nostallframes", token)) {
        GetToken(false);
        panim->numNostallFrames = atof(token);
        return true;
    }

    if (lookupControl(token) != -1) {
        panim->motiontype |= lookupControl(token);
        return true;
    }

    return false;
}


//-----------------------------------------------------------------------------
// Purpose: wrapper for parsing $animation tokens
//-----------------------------------------------------------------------------

int ParseAnimation(s_animation_t *panim, bool isAppend) {
    int depth = 0;

    while (1) {
        if (depth > 0) {
            if (!GetToken(true)) {
                break;
            }
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            else {
                GetToken(false);
            }
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return 1;
        }
        if (stricmp("{", token) == 0) {
            depth++;
        }
        else if (stricmp("}", token) == 0) {
            depth--;
        }
        else if (ParseAnimationToken(panim)) {
        }
        else {
            TokenError("Unknown animation option\'%s\'\n", token);
        }

        if (depth < 0) {
            TokenError("missing {\n");
        }
    };

    return 0;
}


//-----------------------------------------------------------------------------
// Purpose: allocate an entry for $animation
//-----------------------------------------------------------------------------
void Cmd_Animation() {
    // name
    GetToken(false);

    s_animation_t *panim = LookupAnimation(token);

    if (panim != nullptr) {
        if (!panim->isOverride) {
            TokenError("Duplicate animation name \"%s\"\n", token);
        }
        else {
            panim->doesOverride = true;
            ParseEmpty();
            return;
        }
    }

    // allocate animation entry
    panim = g_studiomdl.animations.emplace_back(static_cast<s_animation_t *>(calloc(1, sizeof(s_animation_t))));

    panim->index = g_studiomdl.animations.size() - 1;
    panim->name = token;

    // filename
    GetToken(false);
    strcpyn(panim->filename, token);

    panim->source = Load_Source(panim->filename, "");
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
    VectorCopy(g_studiomdl.defaultadjust, panim->adjust);
    panim->rotation = g_studiomdl.defaultrotation;
    panim->scale = 1.0f;
    panim->fps = 30.0;
    panim->motionrollback = g_StudioMdlContext.defaultMotionRollback;

    ParseAnimation(panim, false);

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
            TokenError("Duplicate sequence name \"%s\"\n", pSequenceName);
        }
        else {
            panim->doesOverride = true;
            return nullptr;
        }
    }

    if (g_studiomdl.sequence.Count() >= MAXSTUDIOSEQUENCES) {
        TokenError("Too many sequences (%d max)\n", MAXSTUDIOSEQUENCES);
    }

    s_sequence_t *pseq = &g_studiomdl.sequence[g_studiomdl.sequence.AddToTail()];
    memset(pseq, 0, sizeof(s_sequence_t));

    // initialize sequence
    Q_strncpy(pseq->name, pSequenceName, sizeof(pseq->name));

    pseq->actweight = 0;
    pseq->activityname[0] = '\0';
    pseq->activity = -1; // -1 is the default for 'no activity'

    pseq->paramindex[0] = -1;
    pseq->paramindex[1] = -1;

    pseq->groupsize[0] = 0;
    pseq->groupsize[1] = 0;

    pseq->fadeintime = g_StudioMdlContext.defaultFadeInTime;
    pseq->fadeouttime = g_StudioMdlContext.defaultFadeOutTime;
    return pseq;
}


int Option_Activity(s_sequence_t *psequence) {
    qboolean found;

    found = false;

    GetToken(false);
    strcpy(psequence->activityname, token);

    if (g_StudioMdlContext.AllowedActivityNames.size()) {
        if (std::find(g_StudioMdlContext.AllowedActivityNames.begin(), g_StudioMdlContext.AllowedActivityNames.end(),
                      token) ==
            g_StudioMdlContext.AllowedActivityNames.end()) {
            MdlError("Unknown sequence activity \"%s\" in \"%s\".", token, psequence->name.c_str());
        }
    }

    GetToken(false);
    psequence->actweight = verify_atoi(token);

    if (psequence->actweight == 0) {
        TokenError("Activity %s has a zero weight (weights must be integers > 0)\n", psequence->activityname.c_str());
    }

    return 0;
}


int Option_ActivityModifier(s_sequence_t *psequence) {
    GetToken(false);

    if (token[0] == '{') {
        while (TokenAvailable()) {
            GetToken(true);
            if (stricmp("}", token) == 0)
                break;

            strlwr(token);
            strcpyn(psequence->activitymodifier[psequence->numactivitymodifiers++].name, token);
        }
    }
    else {
        strlwr(token);
        strcpyn(psequence->activitymodifier[psequence->numactivitymodifiers++].name, token);
    }

    return 0;
}

//-----------------------------------------------------------------------------
// Purpose: create a virtual $animation command from a $sequence reference
//-----------------------------------------------------------------------------
s_animation_t *ProcessImpliedAnimation(s_sequence_t *psequence, const char *filename) {
    // allocate animation entry
    s_animation_t *panim = g_studiomdl.animations.emplace_back((s_animation_t *) calloc(1, sizeof(s_animation_t)));
    panim->index = g_studiomdl.animations.size() - 1;

    panim->isImplied = true;

    panim->startframe = 0;
    panim->endframe = MAXSTUDIOANIMFRAMES - 1;

    strcpy(panim->name, "@");
    strcat(panim->name, psequence->name);
    strcpyn(panim->filename, filename);

    VectorCopy(g_studiomdl.defaultadjust, panim->adjust);
    panim->scale = 1.0f;
    panim->rotation = g_studiomdl.defaultrotation;
    panim->fps = 30;
    panim->motionrollback = g_StudioMdlContext.defaultMotionRollback;

    //panim->source = Load_Source( panim->filename, "smd" );
    panim->source = Load_Source(panim->filename, "");
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

    if (!g_StudioMdlContext.createMakefile && panim->endframe < panim->startframe) {
        TokenError("end frame before start frame in %s", panim->name.c_str());
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
            TokenError("Too many cmds in %s\n", pdest->name.c_str());
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
        TokenError("no animations found\n");
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
                TokenError("non-square (%d) number of blends without \"blendwidth\" set\n", numblends);
            }
        }
    }
    else {
        pseq->groupsize[1] = numblends / pseq->groupsize[0];

        if (pseq->groupsize[0] * pseq->groupsize[1] != numblends) {
            TokenError("missing animation blends. Expected %d, found %d\n",
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
int ParseSequence(s_sequence_t *pseq, bool isAppend) {
    g_StudioMdlContext.szInCurrentSeqName = pseq->name;

    int depth = 0;
    s_animation_t *animations[64];
    int i, j, n;
    int numblends = 0;

    if (isAppend) {
        animations[0] = pseq->panim[0][0];
    }

    while (1) {
        if (depth > 0) {
            if (!GetToken(true)) {
                break;
            }
        }
        else {
            if (!TokenAvailable()) {
                break;
            }
            else {
                GetToken(false);
            }
        }

        if (endofscript) {
            if (depth != 0) {
                TokenError("missing }\n");
            }
            return 1;
        }
        if (stricmp("{", token) == 0) {
            depth++;
        }
        else if (stricmp("}", token) == 0) {
            depth--;
        }
        /*
    else if (stricmp("deform", token ) == 0)
    {
        Option_Deform( pseq );
    }
    */
        else if (stricmp("animtag", token) == 0) {
            depth -= Option_AnimTag(pseq);
        }
        else if (stricmp("event", token) == 0) {
            depth -= Option_Event(pseq);
        }
        else if (stricmp("activity", token) == 0) {
            Option_Activity(pseq);
        }
        else if ((stricmp("activitymodifier", token) == 0) || (stricmp("actmod", token) == 0)) {
            Option_ActivityModifier(pseq);
        }
        else if (strnicmp(token, "ACT_", 4) == 0) {
            UnGetToken();
            Option_Activity(pseq);
        }
        else if (stricmp("snap", token) == 0) {
            pseq->flags |= STUDIO_SNAP;
        }
        else if (stricmp("blendwidth", token) == 0) {
            GetToken(false);
            pseq->groupsize[0] = verify_atoi(token);
        }
        else if (stricmp("blend", token) == 0) {
            i = 0;
            if (pseq->paramindex[0] != -1) {
                i = 1;
            }

            GetToken(false);
            j = LookupPoseParameter(token);
            pseq->paramindex[i] = j;
            pseq->paramattachment[i] = -1;
            GetToken(false);
            pseq->paramstart[i] = verify_atof(token);
            GetToken(false);
            pseq->paramend[i] = verify_atof(token);

            g_studiomdl.pose[j].min = std::min(g_studiomdl.pose[j].min, pseq->paramstart[i]);
            g_studiomdl.pose[j].min = std::min(g_studiomdl.pose[j].min, pseq->paramend[i]);
            g_studiomdl.pose[j].max = std::max(g_studiomdl.pose[j].max, pseq->paramstart[i]);
            g_studiomdl.pose[j].max = std::max(g_studiomdl.pose[j].max, pseq->paramend[i]);
        }
        else if (stricmp("calcblend", token) == 0) {
            i = 0;
            if (pseq->paramindex[0] != -1) {
                i = 1;
            }

            GetToken(false);
            j = LookupPoseParameter(token);
            pseq->paramindex[i] = j;

            GetToken(false);
            pseq->paramattachment[i] = LookupAttachment(token);
            if (pseq->paramattachment[i] == -1) {
                TokenError("Unknown calcblend attachment \"%s\"\n", token);
            }

            GetToken(false);
            pseq->paramcontrol[i] = lookupControl(token);
        }
        else if (stricmp("blendref", token) == 0) {
            GetToken(false);
            pseq->paramanim = LookupAnimation(token);
            if (pseq->paramanim == nullptr) {
                TokenError("Unknown blendref animation \"%s\"\n", token);
            }
        }
        else if (stricmp("blendcomp", token) == 0) {
            GetToken(false);
            pseq->paramcompanim = LookupAnimation(token);
            if (pseq->paramcompanim == nullptr) {
                TokenError("Unknown blendcomp animation \"%s\"\n", token);
            }
        }
        else if (stricmp("blendcenter", token) == 0) {
            GetToken(false);
            pseq->paramcenter = LookupAnimation(token);
            if (pseq->paramcenter == nullptr) {
                TokenError("Unknown blendcenter animation \"%s\"\n", token);
            }
        }
        else if (stricmp("node", token) == 0) {
            GetToken(false);
            pseq->entrynode = pseq->exitnode = LookupXNode(token);
        }
        else if (stricmp("transition", token) == 0) {
            GetToken(false);
            pseq->entrynode = LookupXNode(token);
            GetToken(false);
            pseq->exitnode = LookupXNode(token);
        }
        else if (stricmp("rtransition", token) == 0) {
            GetToken(false);
            pseq->entrynode = LookupXNode(token);
            GetToken(false);
            pseq->exitnode = LookupXNode(token);
            pseq->nodeflags |= 1;
        }
        else if (stricmp("exitphase", token) == 0) {
            GetToken(false);
            pseq->exitphase = verify_atof(token);
        }
        else if (stricmp("delta", token) == 0) {
            pseq->flags |= STUDIO_DELTA;
            pseq->flags |= STUDIO_POST;
        }
        else if (stricmp("worldspace", token) == 0) {
            pseq->flags |= STUDIO_WORLD;
            pseq->flags |= STUDIO_POST;
        }
        else if (stricmp("worldrelative", token) == 0) {
            pseq->flags |= STUDIO_WORLD_AND_RELATIVE;
            pseq->flags |= STUDIO_POST;
        }
        else if (stricmp("rootdriver", token) == 0) {
            pseq->flags |= STUDIO_ROOTXFORM;

            // get bone name
            GetToken(false);

            strcpyn(pseq->rootDriverBoneName, token);
        }
        else if (stricmp("post", token) == 0) // remove
        {
            pseq->flags |= STUDIO_POST;
        }
        else if (stricmp("predelta", token) == 0) {
            pseq->flags |= STUDIO_DELTA;
        }
        else if (stricmp("autoplay", token) == 0) {
            pseq->flags |= STUDIO_AUTOPLAY;
        }
        else if (stricmp("fadein", token) == 0) {
            GetToken(false);
            pseq->fadeintime = verify_atof(token);
        }
        else if (stricmp("fadeout", token) == 0) {
            GetToken(false);
            pseq->fadeouttime = verify_atof(token);
        }
        else if (stricmp("realtime", token) == 0) {
            pseq->flags |= STUDIO_REALTIME;
        }
        else if (stricmp("posecycle", token) == 0) {
            pseq->flags |= STUDIO_CYCLEPOSE;

            GetToken(false);
            pseq->cycleposeindex = LookupPoseParameter(token);
        }
        else if (stricmp("hidden", token) == 0) {
            pseq->flags |= STUDIO_HIDDEN;
        }
        else if (stricmp("addlayer", token) == 0) {
            GetToken(false);
            strcpyn(pseq->autolayer[pseq->numautolayers].name, token);

            while (TokenAvailable()) {
                GetToken(false);
                if (stricmp("local", token) == 0) {
                    pseq->autolayer[pseq->numautolayers].flags |= STUDIO_AL_LOCAL;
                    pseq->flags |= STUDIO_LOCAL;
                }
                else {
                    UnGetToken();
                    break;
                }
            }

            pseq->numautolayers++;
        }
        else if (stricmp("iklock", token) == 0) {
            GetToken(false);
            strcpyn(pseq->iklock[pseq->numiklocks].name, token);

            GetToken(false);
            pseq->iklock[pseq->numiklocks].flPosWeight = verify_atof(token);

            GetToken(false);
            pseq->iklock[pseq->numiklocks].flLocalQWeight = verify_atof(token);

            pseq->numiklocks++;
        }
        else if (stricmp("keyvalues", token) == 0) {
            Option_KeyValues(&pseq->KeyValue);
        }
        else if (stricmp("blendlayer", token) == 0) {
            pseq->autolayer[pseq->numautolayers].flags = 0;

            GetToken(false);
            strcpyn(pseq->autolayer[pseq->numautolayers].name, token);

            GetToken(false);
            pseq->autolayer[pseq->numautolayers].start = verify_atof(token);

            GetToken(false);
            pseq->autolayer[pseq->numautolayers].peak = verify_atof(token);

            GetToken(false);
            pseq->autolayer[pseq->numautolayers].tail = verify_atof(token);

            GetToken(false);
            pseq->autolayer[pseq->numautolayers].end = verify_atof(token);

            while (TokenAvailable()) {
                GetToken(false);
                if (stricmp("xfade", token) == 0) {
                    pseq->autolayer[pseq->numautolayers].flags |= STUDIO_AL_XFADE;
                }
                else if (stricmp("spline", token) == 0) {
                    pseq->autolayer[pseq->numautolayers].flags |= STUDIO_AL_SPLINE;
                }
                else if (stricmp("noblend", token) == 0) {
                    pseq->autolayer[pseq->numautolayers].flags |= STUDIO_AL_NOBLEND;
                }
                else if (stricmp("poseparameter", token) == 0) {
                    pseq->autolayer[pseq->numautolayers].flags |= STUDIO_AL_POSE;
                    GetToken(false);
                    pseq->autolayer[pseq->numautolayers].pose = LookupPoseParameter(token);
                }
                else if (stricmp("local", token) == 0) {
                    pseq->autolayer[pseq->numautolayers].flags |= STUDIO_AL_LOCAL;
                    pseq->flags |= STUDIO_LOCAL;
                }
                else {
                    UnGetToken();
                    break;
                }
            }

            pseq->numautolayers++;
        }
        else if ((numblends || isAppend) && ParseAnimationToken(animations[0])) {
        }
        else if (!isAppend) {
            // assume it's an animation reference
            // first look up an existing animation
            for (n = 0; n < g_studiomdl.animations.size(); n++) {
                if (stricmp(token, g_studiomdl.animations[n]->name) == 0) {
                    animations[numblends++] = g_studiomdl.animations[n];
                    break;
                }
            }

            if (n >= g_studiomdl.animations.size()) {
                // assume it's an implied animation
                animations[numblends++] = ProcessImpliedAnimation(pseq, token);
            }
            // hack to allow animation commands to refer to same sequence
            if (numblends == 1) {
                pseq->panim[0][0] = animations[0];
            }
        }
        else {
            TokenError("unknown command \"%s\"\n", token);
        }

        if (depth < 0) {
            TokenError("missing {\n");
        }
    }

    ProcessSequence(pseq, numblends, animations, isAppend);
    return 0;
}

//-----------------------------------------------------------------------------
// Process the sequence command
//-----------------------------------------------------------------------------
void Cmd_Sequence() {
    if (!GetToken(false))
        return;

    if (g_StudioMdlContext.lCaseAllSequences)
        strlwr(token);

    // Find existing sequences
    const char *pSequenceName = token;
    s_animation_t *panim = LookupAnimation(pSequenceName);
    if (panim != nullptr && panim->isOverride) {
        ParseEmpty();
    }

    s_sequence_t *pseq = ProcessCmdSequence(pSequenceName);
    if (pseq) {
        ParseSequence(pseq, false);
    }
}

//-----------------------------------------------------------------------------
// Purpose: append commands to either a sequence or an animation
//-----------------------------------------------------------------------------
void Cmd_Append() {
    GetToken(false);


    s_sequence_t *pseq = LookupSequence(token);

    if (pseq) {
        ParseSequence(pseq, true);
        return;
    }
    else {
        s_animation_t *panim = LookupAnimation(token);

        if (panim) {
            ParseAnimation(panim, true);
            return;
        }
    }
    TokenError("unknown append animation %s\n", token);
}

void Cmd_Prepend() {
    GetToken(false);

    s_sequence_t *pseq = LookupSequence(token);
    int count = 0;
    s_animation_t *panim = nullptr;
    int iRet = false;

    if (pseq) {
        panim = pseq->panim[0][0];
        count = panim->cmds.size();
        iRet = ParseSequence(pseq, true);
    }
    else {
        panim = LookupAnimation(token);
        if (panim) {
            count = panim->cmds.size();
            iRet = ParseAnimation(panim, true);
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
    TokenError("unknown prepend animation \"%s\"\n", token);
}

void Cmd_Continue() {
    GetToken(false);

    s_sequence_t *pseq = LookupSequence(token);

    if (pseq) {
        GetToken(true);
        UnGetToken();
        if (token[0] != '$')
            ParseSequence(pseq, true);
        return;
    }
    else {
        s_animation_t *panim = LookupAnimation(token);

        if (panim) {
            GetToken(true);
            UnGetToken();
            if (token[0] != '$')
                ParseAnimation(panim, true);
            return;
        }
    }
    TokenError("unknown continue animation %s\n", token);
}

//-----------------------------------------------------------------------------
// Purpose: foward declare an empty sequence
//-----------------------------------------------------------------------------
void Cmd_DeclareSequence() {
    if (g_studiomdl.sequence.Count() >= MAXSTUDIOSEQUENCES) {
        TokenError("Too many sequences (%d max)\n", MAXSTUDIOSEQUENCES);
    }

    s_sequence_t *pseq = &g_studiomdl.sequence[g_studiomdl.sequence.AddToTail()];
    memset(pseq, 0, sizeof(s_sequence_t));
    pseq->flags = STUDIO_OVERRIDE;

    // initialize sequence
    GetToken(false);
    strcpyn(pseq->name, token);
}


//-----------------------------------------------------------------------------
// Purpose: foward declare an empty sequence
//-----------------------------------------------------------------------------
void Cmd_DeclareAnimation() {
    if (g_studiomdl.animations.size() >= g_studiomdl.animations.capacity()) {
        TokenError("Too many animations (%d max)\n", MAXSTUDIOANIMS);
    }

    // allocate animation entry
    s_animation_t *panim = g_studiomdl.animations.emplace_back((s_animation_t *) calloc(1, sizeof(s_animation_t)));
    panim->index = g_studiomdl.animations.size() - 1;
    panim->flags = STUDIO_OVERRIDE;

    // initialize animation
    GetToken(false);
    strcpyn(panim->name, token);
}

bool Grab_AimAtBones() {
    s_aimatbone_t *pAimAtBone(&g_studiomdl.aimatbones[g_studiomdl.numaimatbones]);

    // Already know it's <aimconstraint> in the first string, otherwise wouldn't be here
    if (sscanf(g_StudioMdlContext.szLine, "%*s %127s %127s %127s", pAimAtBone->bonename, pAimAtBone->parentname,
               pAimAtBone->aimname) ==
        3) {
        g_studiomdl.numaimatbones++;

        char cmd[1024];
        Vector vector;

        while (GetLineInput()) {
            g_StudioMdlContext.iLinecount++;

            if (IsEnd(g_StudioMdlContext.szLine)) {
                return false;
            }

            if (sscanf(g_StudioMdlContext.szLine, "%1024s %f %f %f", cmd, &vector[0], &vector[1], &vector[2]) != 4) {
                // Allow blank lines to be skipped without error
                bool allSpace(true);
                for (const char *pC(g_StudioMdlContext.szLine); *pC != '\0' && pC < (g_StudioMdlContext.szLine + 4096);
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
    s_quatinterpbone_t *pBone = &g_studiomdl.quatinterpbones[g_studiomdl.numquatinterpbones];

    while (GetLineInput()) {
        g_StudioMdlContext.iLinecount++;
        if (IsEnd(g_StudioMdlContext.szLine)) {
            return;
        }

        int i = sscanf(g_StudioMdlContext.szLine, "%s %s %s %s %s", cmd, pBone->bonename, pBone->parentname,
                       pBone->controlparentname,
                       pBone->controlname);

        while (i == 4 && stricmp(cmd, "<aimconstraint>") == 0) {
            // If Grab_AimAtBones() returns false, there file is at EOF
            if (!Grab_AimAtBones()) {
                return;
            }

            // Grab_AimAtBones will read input into g_StudioMdlContext.szLine same as here until it gets a line it doesn't understand, at which point
            // it will exit leaving that line in g_StudioMdlContext.szLine, so check for the end and scan the current buffer again and continue on with
            // the normal QuatInterpBones process

            i = sscanf(g_StudioMdlContext.szLine, "%s %s %s %s %s", cmd, pBone->bonename, pBone->parentname,
                       pBone->controlparentname,
                       pBone->controlname);
        }

        if (i == 5 && stricmp(cmd, "<helper>") == 0) {
            // printf( "\"%s\" \"%s\" \"%s\" \"%s\"\n", cmd, pBone->bonename, tmp, pBone->controlname );
            pAxis = pBone;
            g_studiomdl.numquatinterpbones++;
            pBone = &g_studiomdl.quatinterpbones[g_studiomdl.numquatinterpbones];
        }
        else if (i > 0) {
            // There was a bug before which could cause the same command to be parsed twice
            // because if the sscanf above completely fails, it will return 0 and not
            // change the contents of cmd, so i should be greater than 0 in order for
            // any of these checks to be valid... Still kind of buggy as these checks
            // do case insensitive stricmp but then the sscanf does case sensitive
            // matching afterwards... Should probably change those to
            // sscanf( g_StudioMdlContext.szLine, "%*s %f ... ) etc...

            if (stricmp(cmd, "<display>") == 0) {
                // skip all display info
                Vector size;
                float distance;

                i = sscanf(g_StudioMdlContext.szLine, "<display> %f %f %f %f",
                           &size[0], &size[1], &size[2],
                           &distance);

                if (i == 4) {
                    pAxis->percentage = distance / 100.0;
                    pAxis->size = size;
                }
                else {
                    MdlError("Line %d: Unable to parse procedual <display> bone: %s", g_StudioMdlContext.iLinecount,
                             g_StudioMdlContext.szLine.c_str());
                }
            }
            else if (stricmp(cmd, "<basepos>") == 0) {
                i = sscanf(g_StudioMdlContext.szLine, "<basepos> %f %f %f", &basepos.x, &basepos.y, &basepos.z);
                // skip all type info
            }
            else if (stricmp(cmd, "<rotateaxis>") == 0) {
                i = sscanf(g_StudioMdlContext.szLine, "%*s %f %f %f", &rotateaxis.x, &rotateaxis.y, &rotateaxis.z);
                rotateaxis.x = DEG2RAD(rotateaxis.x);
                rotateaxis.y = DEG2RAD(rotateaxis.y);
                rotateaxis.z = DEG2RAD(rotateaxis.z);
            }
            else if (stricmp(cmd, "<jointorient>") == 0) {
                i = sscanf(g_StudioMdlContext.szLine, "%*s %f %f %f", &jointorient.x, &jointorient.y, &jointorient.z);
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
                i = sscanf(g_StudioMdlContext.szLine, "<trigger> %f %f %f %f %f %f %f %f %f %f",
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
                    MdlError("Line %d: Unable to parse procedual <trigger> bone: %s", g_StudioMdlContext.iLinecount,
                             g_StudioMdlContext.szLine.c_str());
                }
            }
            else {
                MdlError("Line %d: Unable to parse procedual bone data: %s", g_StudioMdlContext.iLinecount,
                         g_StudioMdlContext.szLine.c_str());
            }
        }
        else {
            // Allow blank lines to be skipped without error
            bool allSpace(true);
            for (const char *pC(g_StudioMdlContext.szLine); *pC != '\0' && pC < (g_StudioMdlContext.szLine + 4096); ++
                 pC) {
                if (!V_isspace(*pC)) {
                    allSpace = false;
                    break;
                }
            }

            if (!allSpace) {
                MdlError("Line %d: Unable to parse procedual bone data: %s", g_StudioMdlContext.iLinecount,
                         g_StudioMdlContext.szLine.c_str());
            }
        }
    }
}

void Load_ProceduralBones() {
    char filename[256];
    char cmd[1024];
    int option;

    GetToken(false);
    strcpy(filename, token);

    if (!OpenGlobalFile(filename)) {
        Error("unknown $procedural file \"%s\"\n", filename);
        return;
    }

    g_StudioMdlContext.iLinecount = 0;

    char ext[32];
    Q_ExtractFileExtension(filename, ext, sizeof(ext));

    if (stricmp(ext, "vrd") == 0) {
        Grab_QuatInterpBones();
    }
    else {
        while (GetLineInput()) {
            g_StudioMdlContext.iLinecount++;
            const int numRead = sscanf(g_StudioMdlContext.szLine, "%s", cmd, &option);

            // No Command Was Parsed, Blank Line Usually
            if ((numRead == EOF) || (numRead == 0))
                continue;

            if (stricmp(cmd, "version") == 0) {
                if (option != 1) {
                    MdlError("bad version\n");
                }
            }
            else if (stricmp(cmd, "proceduralbones") == 0) {
                Grab_AxisInterpBones();
            }
        }
    }
    fclose(g_StudioMdlContext.fpInput);
}

void Cmd_CollisionModel()
{
    DoCollisionModel( false );
}

void Cmd_CollisionJoints()
{
    DoCollisionModel( true );
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

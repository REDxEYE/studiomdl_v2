//===== Copyright � 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//


//
// studiomdl.c: generates a studio .mdl file from a .qc script
// models/<scriptname>.mdl.
//


#pragma warning( disable : 4244 )
#pragma warning( disable : 4237 )
#pragma warning( disable : 4305 )

#include <Windows.h>

#undef GetCurrentDirectory

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <cmath>
#include <direct.h>
#include <vector>
#include "istudiorender.h"
#include "common/filesystem_tools.h"
#include "tier2/fileutils.h"
#include "common/cmdlib.h"
#include "common/scriplib.h"

#define EXTERN

#include "studiomdl/studiomdl.h"
#include "studiomdl/collisionmodel.h"
#include "studiomdl/optimize.h"
#include "studiomdl/studiobyteswap.h"
#include "bspflags.h"
#include "bitvec.h"
#include "appframework/AppFramework.h"
#include "datamodel/idatamodel.h"
#include "tier3/tier3.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmeanimationset.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmeflexrules.h"
#include "dmserializers/idmserializers.h"
#include "mdllib/mdllib.h"
#include "studiomdl/perfstats.h"
#include "worldsize.h"
#include "tier1/keyvalues.h"
#include "studiomdl/compileclothproxy.h"
#include "movieobjects/dmemodel.h"

#include "studiomdl_commands.h"
#include "studiomdl_errors.h"



#ifdef WIN32
#undef strdup
#define strdup _strdup
#define strlwr _strlwr
#endif

bool g_parseable_completion_output = false;
bool g_collapse_bones_message = false;
bool g_collapse_bones = false;
bool g_collapse_bones_aggressive = false;
bool g_quiet = false;
bool g_bPreferFbx = false;
bool g_bCheckLengths = false;
bool g_bPrintBones = false;
bool g_bPerf = false;
bool g_bDumpGraph = false;
bool g_bMultistageGraph = false;
bool g_verbose = true;
bool g_bCreateMakefile = false;
bool g_bHasModelName = false;
bool g_bZBrush = false;
bool g_bVerifyOnly = false;
bool g_bUseBoneInBBox = true;
bool g_bLockBoneLengths = false;
bool g_bDefineBonesLockedByDefault = true;
int g_minLod = 0;
bool g_bFastBuild = false;
int g_numAllowedRootLODs = 0;
bool g_bNoWarnings = false;
int g_maxWarnings = -1;
bool g_bX360 = false;
bool g_bBuildPreview = false;
bool g_bPreserveTriangleOrder = false;
bool g_bCenterBonesOnVerts = false;
bool g_bDumpMaterials = false;
bool g_bStripLods = false;
bool g_bMakeVsi = false;
float g_flDefaultMotionRollback = 0.3f;
int g_minSectionFrameLimit = 30;
int g_sectionFrames = 30;
bool g_bNoAnimblockStall = false;
float g_flPreloadTime = 1.0f;
bool g_bAnimblockHighRes = false;
bool g_bAnimblockLowRes = false;
int g_nMaxZeroFrames = 3; // clamped from 1..4
bool g_bZeroFramesHighres = false;
float g_flMinZeroFramePosDelta = 2.0f;
extern int g_maxVertexLimit; // nasty wireframe limit
extern int g_maxVertexClamp; // nasty wireframe limit

bool g_bLCaseAllSequences = false;

bool g_bErrorOnSeqRemapFail = false;

bool g_bModelIntentionallyHasZeroSequences = false;

float g_flDefaultFadeInTime = 0.2f;
float g_flDefaultFadeOutTime = 0.2f;

float g_flCollisionPrecision = 0;

static CMdlLoggingListener s_MdlLoggingListener;


char g_path[1024];
Vector g_vecMinWorldspace{MIN_COORD_INTEGER, MIN_COORD_INTEGER, MIN_COORD_INTEGER};
Vector g_vecMaxWorldspace{MAX_COORD_INTEGER, MAX_COORD_INTEGER, MAX_COORD_INTEGER};
DmElementHandle_t g_hDmeBoneFlexDriverList = DMELEMENT_HANDLE_INVALID;

extern std::vector<CUtlString> g_AllowedActivityNames;

enum RunMode {
    RUN_MODE_BUILD,
    RUN_MODE_STRIP_MODEL,
    RUN_MODE_STRIP_VHV
} g_eRunMode = RUN_MODE_BUILD;

extern bool g_bContentRootRelative;

int g_numtexcoords[MAXSTUDIOTEXCOORDS];
CUtlVectorAuto<Vector2D> g_texcoord[MAXSTUDIOTEXCOORDS];

std::vector<s_hitboxset> g_hitboxsets;
std::vector<char> g_KeyValueText;
std::vector<s_flexcontrollerremap_t> g_FlexControllerRemap;


extern const char *g_szInCurrentSeqName;


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
void AddBodyFlexData(s_source_t *pSource, int imodel);

void AddBodyAttachments(s_source_t *pSource);

void AddBodyFlexRules(s_source_t *pSource);

void Option_Flexrule(s_model_t * /* pmodel */, const char *name);

//-----------------------------------------------------------------------------
//  Stuff for writing a makefile to build models incrementally.
//-----------------------------------------------------------------------------
std::vector<CUtlSymbol> m_CreateMakefileDependencies;

void CreateMakefile_AddDependency(const char *pFileName) {
    if (!g_bCreateMakefile) {
        return;
    }

    CUtlSymbol sym(pFileName);
    int i;
    for (i = 0; i < m_CreateMakefileDependencies.size(); i++) {
        if (m_CreateMakefileDependencies[i] == sym) {
            return;
        }
    }
    m_CreateMakefileDependencies.emplace_back(sym);
}

void
StudioMdl_ScriptLoadedCallback(const char *pFilenameLoaded, const char *pIncludedFromFileName, int nIncludeLineNumber) {
//    printf("Script loaded callback: %s",pFilenameLoaded);
}

void CreateMakefile_OutputMakefile(void) {
    if (!g_bHasModelName) {
        MdlError("Can't write makefile since a target mdl hasn't been specified!");
    }
    FILE *fp = fopen("makefile.tmp", "a");
    if (!fp) {
        MdlError("can't open makefile.tmp!\n");
    }
    char mdlname[MAX_PATH];
    strcpy(mdlname, gamedir);
//	if( *g_pPlatformName )
//	{
//		strcat( mdlname, "platform_" );
//		strcat( mdlname, g_pPlatformName );
//		strcat( mdlname, "/" );
//	}
    strcat(mdlname, "models/");
    strcat(mdlname, g_outname);
    Q_StripExtension(mdlname, mdlname, sizeof(mdlname));
    strcat(mdlname, ".mdl");
    Q_FixSlashes(mdlname);

    fprintf(fp, "%s:", mdlname);
    int i;
    for (i = 0; i < m_CreateMakefileDependencies.size(); i++) {
        fprintf(fp, " %s", m_CreateMakefileDependencies[i].String());
    }
    fprintf(fp, "\n");
    char mkdirpath[MAX_PATH];
    strcpy(mkdirpath, mdlname);
    Q_StripFilename(mkdirpath);
    fprintf(fp, "\tmkdir \"%s\"\n", mkdirpath);
    fprintf(fp, "\t%s -quiet %s\n\n", CommandLine()->GetParm(0), g_fullpath);
    fclose(fp);
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------


//#endif

/*
=================
=================
*/

int verify_atoi(const char *token) {
    for (int i = 0; i < strlen(token); i++) {
        if (token[i] != '-' && (token[i] < '0' || token[i] > '9'))
            TokenError("expecting integer, got \"%s\"\n", token);
    }
    return atoi(token);
}

float verify_atof(const char *token) {
    for (int i = 0; i < strlen(token); i++) {
        if (token[i] != '-' && token[i] != '.' && (token[i] < '0' || token[i] > '9'))
            TokenError("expecting float, got \"%s\"\n", token);
    }
    return atof(token);
}


//-----------------------------------------------------------------------------
// Read global input into common string
//-----------------------------------------------------------------------------

bool GetLineInput(void) {
    while (fgets(g_szLine, sizeof(g_szLine), g_fpInput) != NULL) {
        g_iLinecount++;
        // skip comments
        if (g_szLine[0] == '/' && g_szLine[1] == '/')
            continue;

        return true;
    }
    return false;
}


int LookupPoseParameter(const char *name) {
    int i;
    for (i = 0; i < g_numposeparameters; i++) {
        if (!stricmp(name, g_pose[i].name)) {
            return i;
        }
    }
    strcpyn(g_pose[i].name, name);
    g_numposeparameters = i + 1;

    if (g_numposeparameters > MAXSTUDIOPOSEPARAM) {
        TokenError("too many pose parameters (max %d)\n", MAXSTUDIOPOSEPARAM);
    }

    return i;
}


//-----------------------------------------------------------------------------
// Stuff for writing a makefile to build models incrementally.
//-----------------------------------------------------------------------------
s_sourceanim_t *FindSourceAnim(s_source_t *pSource, const char *pAnimName) {
    int nCount = pSource->m_Animations.Count();
    for (int i = 0; i < nCount; ++i) {
        s_sourceanim_t *pAnim = &pSource->m_Animations[i];
        if (!Q_stricmp(pAnimName, pAnim->animationname))
            return pAnim;
    }
    return nullptr;
}

const s_sourceanim_t *FindSourceAnim(const s_source_t *pSource, const char *pAnimName) {
    if (!pAnimName[0])
        return nullptr;

    int nCount = pSource->m_Animations.Count();
    for (int i = 0; i < nCount; ++i) {
        const s_sourceanim_t *pAnim = &pSource->m_Animations[i];
        if (!Q_stricmp(pAnimName, pAnim->animationname))
            return pAnim;
    }
    return nullptr;
}

s_sourceanim_t *FindOrAddSourceAnim(s_source_t *pSource, const char *pAnimName) {
    if (!pAnimName[0])
        return nullptr;

    int nCount = pSource->m_Animations.Count();
    for (int i = 0; i < nCount; ++i) {
        s_sourceanim_t *pAnim = &pSource->m_Animations[i];
        if (!Q_stricmp(pAnimName, pAnim->animationname))
            return pAnim;
    }

    int nIndex = pSource->m_Animations.AddToTail();
    s_sourceanim_t *pAnim = &pSource->m_Animations[nIndex];
    memset(pAnim, 0, sizeof(s_sourceanim_t));
    Q_strncpy(pAnim->animationname, pAnimName, sizeof(pAnim->animationname));
    return pAnim;
}

int LookupTexture(const char *pTextureName, bool bRelativePath) {
    char pTextureNoExt[MAX_PATH];
    char pTextureBase[MAX_PATH];
    char pTextureBase2[MAX_PATH];
    Q_StripExtension(pTextureName, pTextureNoExt, sizeof(pTextureNoExt));
    Q_FileBase(pTextureName, pTextureBase, sizeof(pTextureBase));

    int nFlags = bRelativePath ? RELATIVE_TEXTURE_PATH_SPECIFIED : 0;
    int i;
    for (i = 0; i < g_numtextures; i++) {
        if (g_texture[i].flags == nFlags) {
            if (!Q_stricmp(pTextureNoExt, g_texture[i].name))
                return i;
            continue;
        }

        // Comparing relative vs non-relative
        if (bRelativePath) {
            if (!Q_stricmp(pTextureBase, g_texture[i].name))
                return i;
            continue;
        }

        // Comparing non-relative vs relative
        Q_FileBase(g_texture[i].name, pTextureBase2, sizeof(pTextureBase2));
        if (!Q_stricmp(pTextureNoExt, pTextureBase2))
            return i;
    }

    if (i >= MAXSTUDIOSKINS) {
        MdlError("Too many materials used, max %d\n", (int) MAXSTUDIOSKINS);
    }

    Q_strncpy(g_texture[i].name, pTextureNoExt, sizeof(g_texture[i].name));
    g_texture[i].material = -1;
    g_texture[i].flags = nFlags;
    g_numtextures++;
    return i;
}


int UseTextureAsMaterial(int textureindex) {
    if (g_texture[textureindex].material == -1) {
        if (g_bDumpMaterials) {
            printf("material %d %d %s\n", textureindex, g_nummaterials, g_texture[textureindex].name);
        }
        g_material[g_nummaterials] = textureindex;
        g_texture[textureindex].material = g_nummaterials++;
    }

    return g_texture[textureindex].material;
}

int MaterialToTexture(int material) {
    int i;
    for (i = 0; i < g_numtextures; i++) {
        if (g_texture[i].material == material) {
            return i;
        }
    }
    return -1;
}

//Wrong name for the use of it.
void scale_vertex(Vector &org) {
    org *= g_currentscale;
}


void SetSkinValues() {
    int i, j;
    int index;

    // Check all textures to see if we have relative paths specified
    for (i = 0; i < g_numtextures; i++) {
        if (g_texture[i].flags & RELATIVE_TEXTURE_PATH_SPECIFIED) {
            // Add an empty path to prepend if anything specifies a relative path
            cdtextures[numcdtextures] = 0;
            ++numcdtextures;
            break;
        }
    }

    if (numcdtextures == 0) {
        char szName[256];

        // strip down till it finds "models"
        strcpyn(szName, g_fullpath);
        while (szName[0] != '\0' && strnicmp("models", szName, 6) != 0) {
            strcpy(&szName[0], &szName[1]);
        }
        if (szName[0] != '\0') {
            Q_StripFilename(szName);
            strcat(szName, "/");
        } else {
//			if( *g_pPlatformName )
//			{
//				strcat( szName, "platform_" );
//				strcat( szName, g_pPlatformName );
//				strcat( szName, "/" );
//			}
            strcpy(szName, "models/");
            strcat(szName, g_outname);
            Q_StripExtension(szName, szName, sizeof(szName));
            strcat(szName, "/");
        }
        cdtextures[0] = strdup(szName);
        numcdtextures = 1;
    }

    for (i = 0; i < g_numtextures; i++) {
        char szName[256];
        Q_StripExtension(g_texture[i].name, szName, sizeof(szName));
        Q_strncpy(g_texture[i].name, szName, sizeof(g_texture[i].name));
    }

    // build texture groups
    for (i = 0; i < MAXSTUDIOSKINS; i++) {
        for (j = 0; j < MAXSTUDIOSKINS; j++) {
            g_skinref[i][j] = j;
        }
    }
    index = 0;
    for (i = 0; i < g_numtexturelayers[0]; i++) {
        for (j = 0; j < g_numtexturereps[0]; j++) {
            g_skinref[i][g_texturegroup[0][0][j]] = g_texturegroup[0][i][j];
        }
    }

    if (i != 0) {
        g_numskinfamilies = i;
    } else {
        g_numskinfamilies = 1;
    }
    g_numskinref = g_numtextures;

    // printf ("width: %i  height: %i\n",width, height);
    /*
	printf ("adjusted width: %i height: %i  top : %i  left: %i\n",
			pmesh->skinwidth, pmesh->skinheight, pmesh->skintop, pmesh->skinleft );
	*/
}


char g_szFilename[1024];
FILE *g_fpInput;
char g_szLine[4096];
int g_iLinecount;


void Build_Reference(s_source_t *pSource, const char *pAnimName) {
    int i, parent;
    Vector angle;

    s_sourceanim_t *pReferenceAnim = FindSourceAnim(pSource, pAnimName);
    for (i = 0; i < pSource->numbones; i++) {
        matrix3x4_t m;
        if (pReferenceAnim) {
            AngleMatrix(pReferenceAnim->rawanim[0][i].rot, m);
            m[0][3] = pReferenceAnim->rawanim[0][i].pos[0];
            m[1][3] = pReferenceAnim->rawanim[0][i].pos[1];
            m[2][3] = pReferenceAnim->rawanim[0][i].pos[2];
        } else {
            SetIdentityMatrix(m);
        }

        parent = pSource->localBone[i].parent;
        if (parent == -1) {
            // scale the done pos.
            // calc rotational matrices
            MatrixCopy(m, pSource->boneToPose[i]);
        } else {
            // calc compound rotational matrices
            // FIXME : Hey, it's orthogical so inv(A) == transpose(A)
            Assert(parent < i);
            ConcatTransforms(pSource->boneToPose[parent], m, pSource->boneToPose[i]);
        }
        // printf("%3d %f %f %f\n", i, psource->bonefixup[i].worldorg[0], psource->bonefixup[i].worldorg[1], psource->bonefixup[i].worldorg[2] );
        /*
		AngleMatrix( angle, m );
		printf("%8.4f %8.4f %8.4f\n", m[0][0], m[1][0], m[2][0] );
		printf("%8.4f %8.4f %8.4f\n", m[0][1], m[1][1], m[2][1] );
		printf("%8.4f %8.4f %8.4f\n", m[0][2], m[1][2], m[2][2] );
		*/
    }
}


int Grab_Nodes(std::array<s_node_t, MAXSTUDIOSRCBONES> &pnodes) {
    int index;
    char name[1024];
    int parent;
    int numbones = 0;

    for (index = 0; index < MAXSTUDIOSRCBONES; index++) {
        pnodes[index].parent = -1;
    }

    while (GetLineInput()) {
        if (sscanf(g_szLine, "%d \"%[^\"]\" %d", &index, name, &parent) == 3) {
            // check for duplicated bones
            /*
			if (strlen(pnodes[index].name) != 0)
			{
				MdlError( "bone \"%s\" exists more than once\n", name );
			}
			*/

            strcpyn(pnodes[index].name, name);
            pnodes[index].parent = parent;
            if (index > numbones) {
                numbones = index;
            }
        } else {
            return numbones + 1;
        }
    }
    MdlError("Unexpected EOF at line %d\n", g_iLinecount);
    return 0;
}


void clip_rotations(RadianEuler &rot) {
    int j;
    // clip everything to : -M_PI <= x < M_PI

    for (j = 0; j < 3; j++) {
        while (rot[j] >= M_PI)
            rot[j] -= M_PI * 2;
        while (rot[j] < -M_PI)
            rot[j] += M_PI * 2;
    }
}


void clip_rotations(Vector &rot) {
    int j;
    // clip everything to : -180 <= x < 180

    for (j = 0; j < 3; j++) {
        while (rot[j] >= 180)
            rot[j] -= 180 * 2;
        while (rot[j] < -180)
            rot[j] += 180 * 2;
    }
}


//-----------------------------------------------------------------------------
// Comparison operator for s_attachment_t
//-----------------------------------------------------------------------------
bool s_attachment_t::operator==(const s_attachment_t &rhs) const {
    if (Q_strcmp(name, rhs.name))
        return false;

    if (Q_stricmp(bonename, rhs.bonename) ||
        bone != rhs.bone ||
        type != rhs.type ||
        flags != rhs.flags ||
        Q_memcmp(local.Base(), rhs.local.Base(), sizeof(local))) {
        RadianEuler iEuler, jEuler;
        Vector iPos, jPos;
        MatrixAngles(local, iEuler, iPos);
        MatrixAngles(rhs.local, jEuler, jPos);
        MdlWarning(
                "Attachments with the same name but different parameters found\n"
                "  %s: ParentBone: %s Type: %d Flags: 0x%08x P: %6.2f %6.2f %6.2f R: %6.2f %6.2f %6.2f\n"
                "  %s: ParentBone: %s Type: %d Flags: 0x%08x P: %6.2f %6.2f %6.2f R: %6.2f %6.2f %6.2f\n",
                name, bonename, type, flags,
                iPos.x, iPos.y, iPos.z, RAD2DEG(iEuler.x), RAD2DEG(iEuler.y), RAD2DEG(iEuler.z),
                rhs.name, rhs.bonename, rhs.type, rhs.flags,
                jPos.x, jPos.y, jPos.z, RAD2DEG(jEuler.x), RAD2DEG(jEuler.y), RAD2DEG(jEuler.z));

        return false;
    }

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool s_constraintbonetarget_t::operator==(const s_constraintbonetarget_t &rhs) const {
    if (V_strcmp(m_szBoneName, rhs.m_szBoneName))
        return false;

    if (m_flWeight != rhs.m_flWeight ||
        !VectorsAreEqual(m_vOffset, rhs.m_vOffset) ||
        !QuaternionsAreEqual(m_qOffset, rhs.m_qOffset, 1.0e-4)) {
        const RadianEuler e(m_qOffset);
        const RadianEuler eRhs(rhs.m_qOffset);
        MdlWarning(
                "Constraint bones with same target but different target parameters found\n"
                " Target %s: W: %6.2f VO: %6.2f %6.2f %6.2f RO: %6.2f %6.2f %6.2f\n"
                " Target %s: W: %6.2f VO: %6.2f %6.2f %6.2f RO: %6.2f %6.2f %6.2f\n",
                m_szBoneName, m_flWeight, m_vOffset.x, m_vOffset.y, m_vOffset.z, RAD2DEG(e.x), RAD2DEG(e.y),
                RAD2DEG(e.z),
                rhs.m_szBoneName, rhs.m_flWeight, rhs.m_vOffset.x, rhs.m_vOffset.y, rhs.m_vOffset.z, RAD2DEG(eRhs.x),
                RAD2DEG(eRhs.y), RAD2DEG(eRhs.z));
    }

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool s_constraintboneslave_t::operator==(const s_constraintboneslave_t &rhs) const {
    if (V_strcmp(m_szBoneName, rhs.m_szBoneName))
        return false;

    if (!VectorsAreEqual(m_vBaseTranslate, rhs.m_vBaseTranslate) ||
        !QuaternionsAreEqual(m_qBaseRotation, rhs.m_qBaseRotation, 1.0e-4)) {
        const RadianEuler e(m_qBaseRotation);
        const RadianEuler eRhs(rhs.m_qBaseRotation);
        MdlWarning(
                "Constraint bones with same target but different slave parameters found\n"
                " Target %s: VO: %6.2f %6.2f %6.2f RO: %6.2f %6.2f %6.2f\n"
                " Target %s: VO: %6.2f %6.2f %6.2f RO: %6.2f %6.2f %6.2f\n",
                m_szBoneName, m_vBaseTranslate.x, m_vBaseTranslate.y, m_vBaseTranslate.z, RAD2DEG(e.x), RAD2DEG(e.y),
                RAD2DEG(e.z),
                rhs.m_szBoneName, rhs.m_vBaseTranslate.x, rhs.m_vBaseTranslate.y, rhs.m_vBaseTranslate.z,
                RAD2DEG(eRhs.x), RAD2DEG(eRhs.y), RAD2DEG(eRhs.z));
    }

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CConstraintBoneBase::operator==(const CConstraintBoneBase &rhs) const {
    if (m_slave != rhs.m_slave)
        return false;

    if (m_targets.Count() != rhs.m_targets.Count())
        return false;

    for (int i = 0; i < m_targets.Count(); ++i) {
        if (m_targets[i] != rhs.m_targets[i])
            return false;
    }

    {
        // TODO: Add a static type field
        const CAimConstraint *pAimThis = dynamic_cast< const CAimConstraint * >( this );
        if (pAimThis) {
            if (!dynamic_cast< const CAimConstraint * >( &rhs ))
                return false;
        }
    }

    return true;
}





//-----------------------------------------------------------------------------
// Post-processes a source (used when loading preprocessed files)
//-----------------------------------------------------------------------------
void PostProcessSource(s_source_t *pSource, int imodel) {
    if (pSource) {
        AddBodyFlexData(pSource, imodel);
        AddBodyAttachments(pSource);
        AddBodyFlexRules(pSource);
    }
}



/*
===============
===============
*/

void Grab_Animation(s_source_t *pSource, const char *pAnimName) {
    Vector pos;
    RadianEuler rot;
    char cmd[1024];
    int index;
    int t = -99999999;
    int size;

    s_sourceanim_t *pAnim = FindOrAddSourceAnim(pSource, pAnimName);
    pAnim->startframe = -1;

    size = pSource->numbones * sizeof(s_bone_t);

    while (GetLineInput()) {
        if (sscanf(g_szLine, "%d %f %f %f %f %f %f", &index, &pos[0], &pos[1], &pos[2], &rot[0], &rot[1], &rot[2]) ==
            7) {
            if (pAnim->startframe < 0) {
                MdlError("Missing frame start(%d) : %s", g_iLinecount, g_szLine);
            }

            scale_vertex(pos);
            VectorCopy(pos, pAnim->rawanim[t][index].pos);
            VectorCopy(rot, pAnim->rawanim[t][index].rot);

            clip_rotations(rot); // !!!
            continue;
        }

        if (sscanf(g_szLine, "%1023s %d", cmd, &index) == 0) {
            MdlError("MdlError(%d) : %s", g_iLinecount, g_szLine);
            continue;
        }

        if (!Q_stricmp(cmd, "time")) {
            t = index;
            if (pAnim->startframe == -1) {
                pAnim->startframe = t;
            }
            if (t < pAnim->startframe) {
                MdlError("Frame MdlError(%d) : %s", g_iLinecount, g_szLine);
            }
            if (t > pAnim->endframe) {
                pAnim->endframe = t;
            }
            t -= pAnim->startframe;

            if (t >= pAnim->rawanim.Count()) {
                s_bone_t *ptr = NULL;
                pAnim->rawanim.AddMultipleToTail(t - pAnim->rawanim.Count() + 1, &ptr);
            }

            if (pAnim->rawanim[t] != NULL) {
                continue;
            }

            pAnim->rawanim[t] = (s_bone_t *) calloc(1, size);

            // duplicate previous frames keys
            if (t > 0 && pAnim->rawanim[t - 1]) {
                for (int j = 0; j < pSource->numbones; j++) {
                    VectorCopy(pAnim->rawanim[t - 1][j].pos, pAnim->rawanim[t][j].pos);
                    VectorCopy(pAnim->rawanim[t - 1][j].rot, pAnim->rawanim[t][j].rot);
                }
            }
            continue;
        }

        if (!Q_stricmp(cmd, "end")) {
            pAnim->numframes = pAnim->endframe - pAnim->startframe + 1;

            for (t = 0; t < pAnim->numframes; t++) {
                if (pAnim->rawanim[t] == NULL) {
                    MdlError("%s is missing frame %d\n", pSource->filename, t + pAnim->startframe);
                }
            }

            Build_Reference(pSource, pAnimName);
            return;
        }

        MdlError("MdlError(%d) : %s", g_iLinecount, g_szLine);
    }

    MdlError("unexpected EOF: %s\n", pSource->filename);
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static void FlipFacing(s_source_t *pSrc) {
    unsigned short tmp;

    int i, j;
    for (i = 0; i < pSrc->nummeshes; i++) {
        s_mesh_t *pMesh = &pSrc->mesh[i];
        for (j = 0; j < pMesh->numfaces; j++) {
            s_face_t &f = pSrc->face[pMesh->faceoffset + j];
            tmp = f.b;
            f.b = f.c;
            f.c = tmp;
        }
    }
}


// Processes source comment line and extracts information about the data file
void ProcessSourceComment(s_source_t *psource, const char *pCommentString) {
}


//-----------------------------------------------------------------------------
// Clamp meshes into N vertex sizes so as to not overrun
//-----------------------------------------------------------------------------
// TODO: It may be better to go ahead and create a new "source", since there's other limits besides just vertices per mesh, such as total verts per model.
class CClampedSource {
public:
    CClampedSource() : m_nummeshes(0) {};

    void Init(int numvertices) {
        m_nOrigMap.EnsureCount(numvertices);
        for (int v = 0; v < numvertices; v++) {
            m_nOrigMap[v] = -1;
        }
        for (int m = 0; m < MAXSTUDIOSKINS; m++) {
            m_mesh[m].numvertices = 0;
            m_mesh[m].vertexoffset = 0;
            m_mesh[m].numfaces = 0;
            m_mesh[m].faceoffset = 0;
        }
    };

    // per material mesh
    int m_nummeshes;
    int m_meshindex[MAXSTUDIOSKINS];    // mesh to skin index
    s_mesh_t m_mesh[MAXSTUDIOSKINS];

    // vertices defined in "local" space (not remapped to global bones)
    CUtlVector<int> m_nOrigMap; // maps the original index to the new index
    CUtlVector<s_vertexinfo_t> m_vertex;
    CUtlVector<s_face_t> m_face;
    CUtlVector<s_sourceanim_t> m_Animations;

    int AddNewVert(s_source_t *pOrigSource, int nVert, int nSrcMesh, int nDstMesh, int nPreOffset = 0);

    void AddAnimations(const s_source_t *pOrigSource);

    void DestroyAnimations(s_source_t *pNewSource);

    void Copy(s_source_t *pOrigSource);

    void CopyFlexKeys(const s_source_t *pOrigSource, s_source_t *pNewSource, int imodel);
};

int CClampedSource::AddNewVert(s_source_t *pOrigSource, int nVert, int nSrcMesh, int nDstMesh, int nPreOffset) {
    nVert += (nPreOffset + pOrigSource->mesh[nSrcMesh].vertexoffset);

    if (m_nOrigMap[nVert] == -1) {
        m_nOrigMap[nVert] = m_vertex.AddToTail(pOrigSource->vertex[nVert - nPreOffset]);

        if (m_mesh[nDstMesh].numvertices == 0) {
            m_mesh[nDstMesh].vertexoffset = m_nOrigMap[nVert];
        }
        m_mesh[nDstMesh].numvertices++;
    }

    return m_nOrigMap[nVert] - m_mesh[nDstMesh].vertexoffset;
}

void CClampedSource::AddAnimations(const s_source_t *pOrigSource) {
    // invert the vertex mapping (maps the new index to the original index)
    CUtlVector<int> nReverseMap;
    int numvertices = m_vertex.Count();
    nReverseMap.AddMultipleToTail(numvertices);
    if (numvertices > 0) {
        memset(nReverseMap.Base(), 0xffffffff, numvertices * sizeof(int));
        for (int i = 0; i < pOrigSource->numvertices; i++) {
            if (m_nOrigMap[i] != -1) {
                Assert(nReverseMap[m_nOrigMap[i]] == -1);
                nReverseMap[m_nOrigMap[i]] = i;
            }
        }
        for (int i = 0; i < numvertices; i++) {
            Assert(nReverseMap[i] != -1);
        }
    }

    // copy animations
    int nAnimations = pOrigSource->m_Animations.Count();
    for (int nAnim = 0; nAnim < nAnimations; nAnim++) {
        const s_sourceanim_t &srcAnim = pOrigSource->m_Animations[nAnim];
        if (srcAnim.vanim_mapcount || srcAnim.vanim_map || srcAnim.vanim_flag) {
            Warning("Cannot split SMD model with vertex animations... discarding animation\n");
            Assert(0);
            continue;
        }

        s_sourceanim_t &dstAnim = m_Animations[m_Animations.AddToTail()];
        memset(&dstAnim, 0, sizeof(dstAnim));

        // bone anims can be copied as-is
        memcpy(dstAnim.animationname, srcAnim.animationname, sizeof(dstAnim.animationname));
        dstAnim.numframes = srcAnim.numframes;
        dstAnim.startframe = srcAnim.startframe;
        dstAnim.endframe = srcAnim.endframe;
        dstAnim.rawanim.RemoveAll();
        dstAnim.rawanim.AddMultipleToTail(srcAnim.rawanim.Count());
        for (int i = 0; i < srcAnim.rawanim.Count(); i++) {
            dstAnim.rawanim[i] = new s_bone_t[pOrigSource->numbones];
            memcpy(dstAnim.rawanim.Element(i), srcAnim.rawanim.Element(i), pOrigSource->numbones * sizeof(s_bone_t));
        }

        // vertex animations need remapping
        dstAnim.newStyleVertexAnimations = srcAnim.newStyleVertexAnimations;

        if (!srcAnim.newStyleVertexAnimations)
            return;

        for (int i = 0; i < MAXSTUDIOANIMFRAMES; i++) {
            // Count the number of verts which apply to this sub-model...
            for (int j = 0; j < srcAnim.numvanims[i]; j++) {
                int nMappedVert = m_nOrigMap[srcAnim.vanim[i][j].vertex];
                if (nMappedVert != -1)
                    dstAnim.numvanims[i]++;
            }
            // ...and just copy those verts:
            if (dstAnim.numvanims[i]) {
                dstAnim.vanim[i] = new s_vertanim_t[dstAnim.numvanims[i]];
                int nvanim = 0;
                for (int j = 0; j < srcAnim.numvanims[i]; j++) {
                    int nMappedVert = m_nOrigMap[srcAnim.vanim[i][j].vertex];
                    if (nMappedVert != -1) {
                        memcpy(&dstAnim.vanim[i][nvanim], &srcAnim.vanim[i][j], sizeof(s_vertanim_t));
                        dstAnim.vanim[i][nvanim].vertex = nMappedVert;
                        nvanim++;
                    }
                }
            }
        }
    }
}

void CClampedSource::Copy(s_source_t *pNewSource) {
    // copy over new meshes
    pNewSource->numfaces = m_face.Count();
    pNewSource->face = (s_face_t *) calloc(pNewSource->numfaces, sizeof(s_face_t));
    for (int i = 0; i < pNewSource->numfaces; i++) {
        pNewSource->face[i] = m_face[i];
    }

    pNewSource->numvertices = m_vertex.Count();
    pNewSource->vertex = (s_vertexinfo_t *) calloc(pNewSource->numvertices, sizeof(s_vertexinfo_t));
    for (int i = 0; i < pNewSource->numvertices; i++) {
        pNewSource->vertex[i] = m_vertex[i];
    }

    pNewSource->nummeshes = m_nummeshes;
    for (int i = 0; i < MAXSTUDIOSKINS - 1; i++) {
        pNewSource->mesh[i] = m_mesh[i];
        pNewSource->meshindex[i] = m_meshindex[i];
    }

    // copy over new animations (just copy the structs, pointers and all)
    int nAnimations = m_Animations.Count();
    pNewSource->m_Animations.RemoveAll(); // NOTE: this leaks, but we just dont care
    pNewSource->m_Animations.SetCount(nAnimations);
    for (int i = 0; i < nAnimations; i++) {
        memcpy(&pNewSource->m_Animations[i], &m_Animations[i], sizeof(s_sourceanim_t));
        // Clear the source structure so its embedded CUtlVectorAuto thinks it's empty upon destruction:
        memset(&m_Animations[i], 0, sizeof(s_sourceanim_t));
    }
    m_Animations.RemoveAll();
}

void CClampedSource::CopyFlexKeys(const s_source_t *pOrigSource, s_source_t *pNewSource, int imodel) {
    // TODO: this produces many useless flex keys in HLMV, and it can fail if 'numSubmodels*numFlexKeys' exceeds the supported maximum
    //       (this would happen for sure if a character's face got cut up, for example), so:
    //  - in CClampedSource::AddAnimations, we can detect flex animations which do not apply to a submodel and cull them
    //  - we would need to build up a mapping table from pre-culled indices to post-culled indices (and vice versa), so that in here we can copy just those
    //    elements of m_FlexKeys/m_CombinationControls/m_CombinationRules/m_FlexControllerRemaps which were not culled (these arrays are all parallel)
    //  - the copied m_CombinationRules values would need to be remapped using the mapping table
    //  - if a flex key should be culled but it is referred to (via m_CombinationRules) by a non-culled flex key, then we can't cull it
    // If characters are the only failure cases, a simpler alternative may be to just split models into flexed/unflexed parts (given only the faces are flexed)

    if (pOrigSource == pNewSource)
        return;

// TODO: need to change g_defaultflexkey so it works with this (set a flag on the default flexkey (error if the user sets two defaults), duplicate the flag with that flexkey in here, update RemapVertexAnimations to use the flag)

    pNewSource->m_FlexKeys.SetCount(pOrigSource->m_FlexKeys.Count());
    for (int i = 0; i < pOrigSource->m_FlexKeys.Count(); i++) {
        pNewSource->m_FlexKeys[i] = pOrigSource->m_FlexKeys[i];
        pNewSource->m_FlexKeys[i].source = pNewSource;
    }
    pNewSource->m_CombinationControls.SetCount(pOrigSource->m_CombinationControls.Count());
    for (int i = 0; i < pOrigSource->m_CombinationControls.Count(); i++) {
        pNewSource->m_CombinationControls[i] = pOrigSource->m_CombinationControls[i];
    }
    pNewSource->m_CombinationRules.SetCount(pOrigSource->m_CombinationRules.Count());
    for (int i = 0; i < pOrigSource->m_CombinationRules.Count(); i++) {
        pNewSource->m_CombinationRules[i] = pOrigSource->m_CombinationRules[i];
    }
    pNewSource->m_FlexControllerRemaps.SetCount(pOrigSource->m_FlexControllerRemaps.Count());
    for (int i = 0; i < pOrigSource->m_FlexControllerRemaps.Count(); i++) {
        pNewSource->m_FlexControllerRemaps[i] = pOrigSource->m_FlexControllerRemaps[i];
    }

    // Emulate post-processing of flex data done by Cmd_Bodygroup, via PostProcessSource:
    //   Calling AddBodyFlexData will update:
    //     - g_flexkey, g_numflexkeys, g_flexcontroller, g_numflexcontrollers, g_FlexControllerRemap
    //     - pNewSource->( m_nKeyStartIndex, m_rawIndexToRemapSourceIndex, m_rawIndexToRemapLocalIndex, m_leftRemapIndexToGlobalFlexControllIndex, m_rightRemapIndexToGlobalFlexControllIndex )
    //   Calling AddBodyFlexRules will update:
    //     - pSource->m_FlexControllerRemaps
    //     - g_flexrule, g_numflexrules
    //   NOTE: we dont call AddBodyAttachments, since we're not duplicating those
    AddBodyFlexData(pNewSource, imodel);
    AddBodyFlexRules(pNewSource);
}

void ApplyOffsetToSrcVerts(s_source_t *pModel, matrix3x4_t matOffset) {
    if (MatrixIsIdentity(matOffset))
        return;

    for (int v = 0; v < pModel->numvertices; v++) {
        VectorTransform(pModel->vertex[v].position, matOffset, pModel->vertex[v].position);
        VectorRotate(pModel->vertex[v].normal, matOffset, pModel->vertex[v].normal);
        VectorRotate(pModel->vertex[v].tangentS.AsVector3D(), matOffset, pModel->vertex[v].tangentS.AsVector3D());
    }
}

void AddSrcToSrc(s_source_t *pOrigSource, s_source_t *pAppendSource, matrix3x4_t matOffset) {
    // steps are:
    // only A exists
    // make a new source C
    // append A to C
    // append B to C
    // replace A with C

    CClampedSource newSource;

    newSource.Init(pOrigSource->numvertices + pAppendSource->numvertices);

    bool bDone[MAXSTUDIOSKINS];
    for (int m = 0; m < MAXSTUDIOSKINS; m++) {
        newSource.m_meshindex[m] = 0;
        bDone[m] = false;
    }

    for (int m = 0; m < MAXSTUDIOSKINS; m++) {
        int nSrcMeshIndex = pOrigSource->meshindex[m];
        s_mesh_t *pOrigMesh = &pOrigSource->mesh[nSrcMeshIndex];

        if (pOrigMesh->numvertices == 0 || bDone[nSrcMeshIndex])
            continue;
        bDone[nSrcMeshIndex] = true;

        // copy all origmesh faces into newsource
        for (int f = pOrigMesh->faceoffset; f < pOrigMesh->faceoffset + pOrigMesh->numfaces; f++) {
            s_face_t face;
            face.a = newSource.AddNewVert(pOrigSource, pOrigSource->face[f].a, nSrcMeshIndex, 0);
            face.b = newSource.AddNewVert(pOrigSource, pOrigSource->face[f].b, nSrcMeshIndex, 0);
            face.c = newSource.AddNewVert(pOrigSource, pOrigSource->face[f].c, nSrcMeshIndex, 0);
            if (pOrigSource->face[f].d != 0)
                face.d = newSource.AddNewVert(pOrigSource, pOrigSource->face[f].d, nSrcMeshIndex, 0);
            else
                face.d = 0;

            //if ( newSource.m_mesh[0].numfaces == 0 )
            //{
            //	newSource.m_mesh[0].faceoffset = newSource.m_face.Count();
            //}

            newSource.m_face.AddToTail(face);
            newSource.m_mesh[0].numfaces++;
        }
    }

    // just use src anim - we don't really care because it's for static props
    newSource.AddAnimations(pOrigSource);
    newSource.m_nummeshes = 1;//pOrigSource->nummeshes;// + pAppendSource->nummeshes;

    // apply offset to appended vertices
    ApplyOffsetToSrcVerts(pAppendSource, matOffset); // no-op if matOffset is identity

    // append the vertices to the new source

    for (int m = 0; m < MAXSTUDIOSKINS; m++) {
        bDone[m] = false;
    }

    for (int m = 0; m < pAppendSource->nummeshes; m++) {
        int nCurMesh = pAppendSource->meshindex[m];
        s_mesh_t *pAppendMesh = &pAppendSource->mesh[nCurMesh];

        if (bDone[nCurMesh])
            continue;

        bDone[nCurMesh] = true;

        //if ( newSource.m_mesh[nDestMesh].numvertices + 4 > g_maxVertexLimit )
        //	nDestMesh++;

        if (pAppendMesh && pAppendMesh->numvertices > 0) {
            for (int f = pAppendMesh->faceoffset; f < pAppendMesh->faceoffset + pAppendMesh->numfaces; f++) {
                s_face_t face;
                face.a = newSource.AddNewVert(pAppendSource, pAppendSource->face[f].a, nCurMesh, 0,
                                              pOrigSource->numvertices);
                face.b = newSource.AddNewVert(pAppendSource, pAppendSource->face[f].b, nCurMesh, 0,
                                              pOrigSource->numvertices);
                face.c = newSource.AddNewVert(pAppendSource, pAppendSource->face[f].c, nCurMesh, 0,
                                              pOrigSource->numvertices);
                if (pAppendSource->face[f].d != 0)
                    face.d = newSource.AddNewVert(pAppendSource, pAppendSource->face[f].d, nCurMesh, 0,
                                                  pOrigSource->numvertices);
                else
                    face.d = 0;

                newSource.m_face.AddToTail(face);
                newSource.m_mesh[0].numfaces++;
            }
        }
    }

    free(pOrigSource->face);
    free(pOrigSource->vertex);
    newSource.Copy(pOrigSource);
}

void AddSrcToSrc(s_source_t *pOrigSource, s_source_t *pAppendSource) {
    matrix3x4_t matNoop;
    matNoop.SetToIdentity();
    AddSrcToSrc(pOrigSource, pAppendSource, matNoop);
}




void ClampMaxVerticesPerModel(s_source_t *pOrigSource) {
    // check for overage
    if (pOrigSource->numvertices < g_maxVertexLimit)
        return;

    MdlWarning("model has too many verts, cutting into multiple models\n", pOrigSource->numvertices);

    CUtlVector<CClampedSource> newSource;

    int ns = newSource.AddToTail();
    newSource[ns].Init(pOrigSource->numvertices);

    for (int m = 0; m < pOrigSource->nummeshes; m++) {
        s_mesh_t *pOrigMesh = &pOrigSource->mesh[m];

        for (int f = pOrigMesh->faceoffset; f < pOrigMesh->faceoffset + pOrigMesh->numfaces; f++) {
            // make sure all the total for all the meshes in the model don't go over limit
            int nVertsInFace = (pOrigSource->face[f].d == 0) ? 3 : 4;
            if ((newSource[ns].m_vertex.Count() + nVertsInFace) > g_maxVertexClamp) {
                // go to the next model
                ns = newSource.AddToTail();
                newSource[ns].Init(pOrigSource->numvertices);
            }

            // build face
            s_face_t face;
            face.a = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].a, m, m);
            face.b = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].b, m, m);
            face.c = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].c, m, m);
            if (pOrigSource->face[f].d != 0)
                face.d = newSource[ns].AddNewVert(pOrigSource, pOrigSource->face[f].d, m, m);
            else
                face.d = 0;

            if (newSource[ns].m_mesh[m].numfaces == 0) {
                newSource[ns].m_mesh[m].faceoffset = newSource[ns].m_face.Count();
            }
            newSource[ns].m_face.AddToTail(face);
            newSource[ns].m_mesh[m].numfaces++;
        }
    }

    // Split animations into the new sub-models
    for (int n = 0; n < newSource.Count(); n++) {
        newSource[n].AddAnimations(pOrigSource);
        newSource[n].m_nummeshes = pOrigSource->nummeshes;
    }

    // copy over new meshes and animations back into initial source
    free(pOrigSource->face);
    free(pOrigSource->vertex);
    newSource[0].Copy(pOrigSource);

    for (int n = 1; n < newSource.Count(); n++) {
        // create a new internal "source"
        s_source_t *pSource = (s_source_t *) calloc(1, sizeof(s_source_t));
        g_source[g_numsources++] = pSource;

        // copy all the members, in order
        memcpy(&(pSource->filename[0]), &(pOrigSource->filename[0]), sizeof(pSource->filename));

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


        // copy mesh data
        for (int i = 0; i < pSource->nummeshes; i++) {
            pSource->texmap[i] = pOrigSource->texmap[i];
            pSource->meshindex[i] = pOrigSource->meshindex[i];
        }

        // copy settings
        pSource->adjust = pOrigSource->adjust;
        pSource->scale = pOrigSource->scale;
        pSource->rotation = pOrigSource->rotation;
        pSource->bNoAutoDMXRules = pOrigSource->bNoAutoDMXRules;

        // allocate a model
        s_model_t *pModel = (s_model_t *) calloc(1, sizeof(s_model_t));
        pModel->source = pSource;
        sprintf(pModel->name, "%s%d", "clamped", n);
        int imodel = g_nummodels++;
        g_model[imodel] = pModel;

        // make it a new bodypart
        g_bodypart[g_numbodyparts].nummodels = 1;
        g_bodypart[g_numbodyparts].base =
                g_bodypart[g_numbodyparts - 1].base * g_bodypart[g_numbodyparts - 1].nummodels;
        sprintf(g_bodypart[g_numbodyparts].name, "%s%d", "clamped", n);
        g_bodypart[g_numbodyparts].pmodel[0] = pModel;
        g_numbodyparts++;

        // finally, copy flex keys
        newSource[n].CopyFlexKeys(pOrigSource, pSource, imodel);

        // NOTE: we leave attachments on the first sub-model, we don't want to duplicate those
    }
}


//-----------------------------------------------------------------------------
// Purpose: insert a virtual bone between a child and parent (currently unsupported)
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Loads an animation/model source
//-----------------------------------------------------------------------------
s_source_t *Load_Source(const char *name, const char *ext, bool reverse, bool isActiveModel, bool bUseCache) {
    if (g_numsources >= MAXSTUDIOSEQUENCES) {
        TokenError("Load_Source( %s ) - overflowed g_numsources.", name);
    }

    Assert(name);
    int namelen = Q_strlen(name) + 1;
    char *pTempName = (char *) stackalloc(namelen);
    char xext[32];
    int result = false;

    strcpy(pTempName, name);
    Q_ExtractFileExtension(pTempName, xext, sizeof(xext));

    if (xext[0] == '\0') {
        Q_strncpy(xext, ext, sizeof(xext));
    } else {
        Q_StripExtension(pTempName, pTempName, namelen);
    }

    s_source_t *pSource = NULL;

    if (bUseCache) {
        pSource = FindCachedSource(pTempName, xext);
        if (pSource) {
            if (isActiveModel) {
                pSource->isActiveModel = true;
            }
            return pSource;
        }
    }

    // NOTE: The load proc can potentially add other sources (for the MPP format)
    // So we have to deal with setting everything up in this source prior to
    // calling the load func, and we cannot reference g_source anywhere below
    pSource = (s_source_t *) calloc(1, sizeof(s_source_t));
    g_source[g_numsources++] = pSource;
    if (isActiveModel) {
        pSource->isActiveModel = true;
    }

    // copy over default settings of when the model was loaded
    // (since there's no actual animation for some of the systems)
    VectorCopy(g_defaultadjust, pSource->adjust);
    pSource->scale = 1.0f;
    pSource->rotation = g_defaultrotation;
    typedef int (*load_proc)(s_source_t *);
#ifdef FBX_SUPPORT
    std::array<std::pair<const char *, load_proc>, 9> supported_formats = {
             std::make_pair("fbx", Load_FBX),
             std::make_pair("vrm", Load_VRM),
             std::make_pair("dmx", Load_DMX),
             std::make_pair("mpp", Load_DMX),
             std::make_pair("smd", Load_SMD),
             std::make_pair("sma", Load_SMD),
             std::make_pair("phys", Load_SMD),
             std::make_pair("vta", Load_VTA),
             std::make_pair("obj", Load_OBJ),
             std::make_pair("xml", Load_DMX),
             std::make_pair("fbx", Load_FBX)
     };
#else
    std::array<std::pair<const char *, load_proc>, 9> supported_formats = {
            std::make_pair("vrm", Load_VRM),
            std::make_pair("dmx", Load_DMX),
            std::make_pair("mpp", Load_DMX),
            std::make_pair("smd", Load_SMD),
            std::make_pair("sma", Load_SMD),
            std::make_pair("phys", Load_SMD),
            std::make_pair("vta", Load_VTA),
            std::make_pair("obj", Load_OBJ),
            std::make_pair("xml", Load_DMX)
    };
#endif
    for (int fmt_id = (g_bPreferFbx ? 0 : 1); fmt_id < supported_formats.size(); ++fmt_id) {
        if ((!result && xext[0] == '\0') || std::strcmp(xext, supported_formats[fmt_id].first) == 0) {
            std::snprintf(g_szFilename, sizeof(g_szFilename), "%s%s.%s", cddir[numdirs], pTempName,
                          supported_formats[fmt_id].first);
            std::strncpy(pSource->filename, g_szFilename, sizeof(pSource->filename));
            result = (supported_formats[fmt_id].second)(pSource);
        }
    }

    if (!g_bCreateMakefile && !result) {
        if (xext[0] == '\0') {
            TokenError("could not load file '%s%s'\n", cddir[numdirs], pTempName);
        } else {
            TokenError("could not load file '%s%s.%s'\n", cddir[numdirs], pTempName, xext);
        }
    }

    if (pSource->numbones == 0) {
        TokenError("missing all bones in file '%s'\n", pSource->filename);
    }

    if (reverse) {
        FlipFacing(pSource);
    }

    return pSource;
}


//-----------------------------------------------------------------------------
// Purpose: create named order dependant s_animcmd_t blocks, used as replicated token list for $animations
//-----------------------------------------------------------------------------

int ParseAnimation(s_animation_t *panim, bool isAppend);

int ParseEmpty(void);



int ParseSequence(s_sequence_t *pseq, bool isAppend);




int Add_Flexdesc(const char *name) {
    int flexdesc;
    for (flexdesc = 0; flexdesc < g_numflexdesc; flexdesc++) {
        if (stricmp(name, g_flexdesc[flexdesc].FACS) == 0) {
            break;
        }
    }

    if (flexdesc >= MAXSTUDIOFLEXDESC) {
        TokenError("Too many flex types, max %d\n", MAXSTUDIOFLEXDESC);
    }

    if (flexdesc == g_numflexdesc) {
        strcpyn(g_flexdesc[flexdesc].FACS, name);

        g_numflexdesc++;
    }
    return flexdesc;
}





//-----------------------------------------------------------------------------
// Adds combination data to the source
//-----------------------------------------------------------------------------
int FindSourceFlexKey(s_source_t *pSource, const char *pName) {
    int nCount = pSource->m_FlexKeys.Count();
    for (int i = 0; i < nCount; ++i) {
        if (!Q_stricmp(pSource->m_FlexKeys[i].animationname, pName))
            return i;
    }
    return -1;
}


//-----------------------------------------------------------------------------
// Adds flexkey data to a particular source
//-----------------------------------------------------------------------------
void AddFlexKey(s_source_t *pSource, CDmeCombinationOperator *pComboOp, const char *pFlexKeyName) {
    // See if the delta state is already accounted for
    if (FindSourceFlexKey(pSource, pFlexKeyName) >= 0)
        return;

    int i = pSource->m_FlexKeys.AddToTail();

    s_flexkey_t &key = pSource->m_FlexKeys[i];
    memset(&key, 0, sizeof(key));

    key.target0 = 0.0f;
    key.target1 = 1.0f;
    key.target2 = 10.0f;
    key.target3 = 11.0f;
    key.decay = 1.0f;
    key.source = pSource;

    Q_strncpy(key.animationname, pFlexKeyName, sizeof(key.animationname));
    key.flexpair = pComboOp->IsDeltaStateStereo(pFlexKeyName);    // Signal used by AddBodyFlexData
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void FindOrAddFlexController(
        const char *pszFlexControllerName,
        const char *pszFlexControllerType = "default",
        float flMin = 0.0f,
        float flMax = 1.0f) {
    for (int i = 0; i < g_numflexcontrollers; ++i) {
        if (!V_strcmp(g_flexcontroller[i].name, pszFlexControllerName)) {
            if (V_strcmp(g_flexcontroller[i].type, pszFlexControllerType) ||
                g_flexcontroller[i].min != flMin ||
                g_flexcontroller[i].max != flMax) {
                MdlWarning("Flex Controller %s Defined Twice With Different Params: %s, %f %f vs %s, %f %f\n",
                           pszFlexControllerName,
                           pszFlexControllerType, flMin, flMax,
                           g_flexcontroller[i].type, g_flexcontroller[i].min, g_flexcontroller[i].max);
            }

            return;
        }
    }

    strcpyn(g_flexcontroller[g_numflexcontrollers].name, pszFlexControllerName);
    strcpyn(g_flexcontroller[g_numflexcontrollers].type, pszFlexControllerType);
    g_flexcontroller[g_numflexcontrollers].min = flMin;
    g_flexcontroller[g_numflexcontrollers].max = flMax;
    g_numflexcontrollers++;
}


//-----------------------------------------------------------------------------
// In scriplib.cpp
// Called to parse from a memory buffer on the script stack
//-----------------------------------------------------------------------------
void PushMemoryScript(char *pszBuffer, const int nSize);

bool PopMemoryScript();

//-----------------------------------------------------------------------------
// Adds combination data to the source
//-----------------------------------------------------------------------------
void AddCombination(s_source_t *pSource, CDmeCombinationOperator *pCombination) {
    CDmrElementArray<CDmElement> targets = pCombination->GetAttribute("targets");

    // See if all targets of the DmeCombinationOperator are DmeFlexRules
    // If so implement controllers & flexes from flex rules, if not do old
    // behavior
    bool bFlexRules = true;
    for (int i = 0; i < targets.Count(); ++i) {
        if (!CastElement<CDmeFlexRules>(targets[i])) {
            bFlexRules = false;
            break;
        }
    }

    if (bFlexRules) {
        // Add a controller for each control in the combintion operator
        CDmAttribute *pControlsAttr = pCombination->GetAttribute("controls");
        if (pControlsAttr) {
            CDmrElementArrayConst<CDmElement> controlsAttr(pControlsAttr);
            for (int i = 0; i < controlsAttr.Count(); ++i) {
                CDmElement *pControlElement = controlsAttr[i];
                if (!pControlElement)
                    continue;

                float flMin = 0.0f;
                float flMax = 1.0f;

                flMin = pControlElement->GetValue("flexMin", flMin);
                flMax = pControlElement->GetValue("flexMax", flMax);

                FindOrAddFlexController(pControlElement->GetName(), "default", flMin, flMax);
            }
        }

        CUtlString sOldToken = token;
        CUtlString sTmpBuf;

        for (int i = 0; i < targets.Count(); ++i) {
            CDmeFlexRules *pDmeFlexRules = CastElement<CDmeFlexRules>(targets[i]);
            if (!pDmeFlexRules)
                continue;

            for (int i = 0; i < pDmeFlexRules->GetRuleCount(); ++i) {
                CDmeFlexRuleBase *pDmeFlexRule = pDmeFlexRules->GetRule(i);
                if (!pDmeFlexRule)
                    continue;

                sTmpBuf = "= ";

                bool bFlexRule = true;

                if (CastElement<CDmeFlexRulePassThrough>(pDmeFlexRule)) {
                    sTmpBuf += pDmeFlexRule->GetName();
                } else if (CastElement<CDmeFlexRuleExpression>(pDmeFlexRule)) {
                    CDmeFlexRuleExpression *pDmeFlexRuleExpression = CastElement<CDmeFlexRuleExpression>(pDmeFlexRule);

                    sTmpBuf += pDmeFlexRuleExpression->GetExpression();
                } else if (CastElement<CDmeFlexRuleLocalVar>(pDmeFlexRule)) {
                    bFlexRule = false;
                } else {
                    MdlWarning("Unknown DmeDeltaRule: %s Of Type: %s\n", pDmeFlexRule->GetName(),
                               pDmeFlexRule->GetTypeString());
                    continue;
                }

                PushMemoryScript(sTmpBuf.Get(), sTmpBuf.Length());

                Add_Flexdesc(pDmeFlexRule->GetName());

                if (bFlexRule) {
                    Option_Flexrule(NULL, pDmeFlexRule->GetName());
                }

                PopMemoryScript();
            }
        }

        V_strncpy(token, sOldToken.Get(), ARRAYSIZE(token));
        UnGetToken();

        return;
    }

    // Define the remapped controls
    int nControlCount = pCombination->GetRawControlCount();
    for (int i = 0; i < nControlCount; ++i) {
        int m = pSource->m_CombinationControls.AddToTail();
        s_combinationcontrol_t &control = pSource->m_CombinationControls[m];
        Q_strncpy(control.name, pCombination->GetRawControlName(i), sizeof(control.name));
    }

    // Define the combination + domination rules
    int nTargetCount = pCombination->GetOperationTargetCount();
    for (int i = 0; i < nTargetCount; ++i) {
        int nOpCount = pCombination->GetOperationCount(i);
        for (int j = 0; j < nOpCount; ++j) {
            CDmElement *pDeltaState = pCombination->GetOperationDeltaState(i, j);
            if (!pDeltaState)
                continue;

            int nFlex = FindSourceFlexKey(pSource, pDeltaState->GetName());
            if (nFlex < 0)
                continue;

            int k = pSource->m_CombinationRules.AddToTail();
            s_combinationrule_t &rule = pSource->m_CombinationRules[k];
            rule.m_nFlex = nFlex;
            rule.m_Combination = pCombination->GetOperationControls(i, j);
            int nDominatorCount = pCombination->GetOperationDominatorCount(i, j);
            for (int l = 0; l < nDominatorCount; ++l) {
                int m = rule.m_Dominators.AddToTail();
                rule.m_Dominators[m] = pCombination->GetOperationDominator(i, j, l);
            }
        }
    }

    // Define the remapping controls
    nControlCount = pCombination->GetControlCount();
    for (int i = 0; i < nControlCount; ++i) {
        int k = pSource->m_FlexControllerRemaps.AddToTail();
        s_flexcontrollerremap_t &remap = pSource->m_FlexControllerRemaps[k];
        remap.m_Name = pCombination->GetControlName(i);
        remap.m_bIsStereo = pCombination->IsStereoControl(i);
        remap.m_Index = -1;            // Don't know this right now
        remap.m_LeftIndex = -1;        // Don't know this right now
        remap.m_RightIndex = -1;    // Don't know this right now
        remap.m_MultiIndex = -1;    // Don't know this right now
        remap.m_EyesUpDownFlexController = -1;
        remap.m_BlinkController = -1;

        int nRemapCount = pCombination->GetRawControlCount(i);
        if (pCombination->IsEyelidControl(i)) {
            remap.m_RemapType = FLEXCONTROLLER_REMAP_EYELID;

            // Save the eyes_updown flex for later
            const char *pEyesUpDownFlexName = pCombination->GetEyesUpDownFlexName(i);
            remap.m_EyesUpDownFlexName = pEyesUpDownFlexName ? pEyesUpDownFlexName : "eyes_updown";
        } else {
            switch (nRemapCount) {
                case 0:
                case 1:
                    remap.m_RemapType = FLEXCONTROLLER_REMAP_PASSTHRU;
                    break;
                case 2:
                    remap.m_RemapType = FLEXCONTROLLER_REMAP_2WAY;
                    break;
                default:
                    remap.m_RemapType = FLEXCONTROLLER_REMAP_NWAY;
                    break;
            }
        }

        Assert(nRemapCount != 0);
        for (int j = 0; j < nRemapCount; ++j) {
            const char *pRemapName = pCombination->GetRawControlName(i, j);
            remap.m_RawControls.emplace_back(pRemapName);
        }
    }
}



//-----------------------------------------------------------------------------
// Assigns a default surface property to the entire model
//-----------------------------------------------------------------------------
struct SurfacePropName_t {
    char m_pJointName[128];
    char m_pSurfaceProp[128];
};

char s_pDefaultSurfaceProp[128] = {"default"};
static CUtlVector<SurfacePropName_t> s_JointSurfaceProp;


//-----------------------------------------------------------------------------
// Adds a joint surface property
//-----------------------------------------------------------------------------
void AddSurfaceProp(const char *pBoneName, const char *pSurfaceProperty) {
    // Search for the name in our list
    int i;
    for (i = s_JointSurfaceProp.Count(); --i >= 0;) {
        if (!Q_stricmp(s_JointSurfaceProp[i].m_pJointName, pBoneName))
            break;
    }

    // Add new entry if we haven't seen this name before
    if (i < 0) {
        i = s_JointSurfaceProp.AddToTail();
        Q_strncpy(s_JointSurfaceProp[i].m_pJointName, pBoneName, sizeof(s_JointSurfaceProp[i].m_pJointName));
    }

    Q_strncpy(s_JointSurfaceProp[i].m_pSurfaceProp, pSurfaceProperty, sizeof(s_JointSurfaceProp[i].m_pSurfaceProp));
}

//-----------------------------------------------------------------------------
// Returns the default surface prop name
//-----------------------------------------------------------------------------
char *GetDefaultSurfaceProp() {
    return s_pDefaultSurfaceProp;
}


//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
char *FindSurfaceProp(const char *pJointName) {
    for (int i = s_JointSurfaceProp.Count(); --i >= 0;) {
        if (!Q_stricmp(s_JointSurfaceProp[i].m_pJointName, pJointName))
            return s_JointSurfaceProp[i].m_pSurfaceProp;
    }

    return 0;
}


//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
char *GetSurfaceProp(const char *pJointName) {
    while (pJointName) {
        // First try to find this joint
        char *pSurfaceProp = FindSurfaceProp(pJointName);
        if (pSurfaceProp)
            return pSurfaceProp;

        // If we can't find the joint, then find it's parent...
        if (!g_numbones)
            return s_pDefaultSurfaceProp;

        int i = findGlobalBone(pJointName);

        if ((i >= 0) && (g_bonetable[i].parent >= 0)) {
            pJointName = g_bonetable[g_bonetable[i].parent].name;
        } else {
            pJointName = 0;
        }
    }

    // No match, return the default one
    return s_pDefaultSurfaceProp;
}


//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
void ConsistencyCheckSurfaceProp() {
    for (int i = s_JointSurfaceProp.Count(); --i >= 0;) {
        int j = findGlobalBone(s_JointSurfaceProp[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint surface property for joint\n"
                       "    \"%s\" which either doesn't exist or was optimized out.\n",
                       s_JointSurfaceProp[i].m_pJointName);
        }
    }
}


//-----------------------------------------------------------------------------
// Assigns a default contents to the entire model
//-----------------------------------------------------------------------------
int s_nDefaultContents = CONTENTS_SOLID;
CUtlVector<ContentsName_t> s_JointContents;




//-----------------------------------------------------------------------------
// Returns the default contents
//-----------------------------------------------------------------------------
int GetDefaultContents() {
    return s_nDefaultContents;
}

//-----------------------------------------------------------------------------
// Returns contents for a given joint
//-----------------------------------------------------------------------------
static int FindContents(const char *pJointName) {
    for (int i = s_JointContents.Count(); --i >= 0;) {
        if (!stricmp(s_JointContents[i].m_pJointName, pJointName)) {
            return s_JointContents[i].m_nContents;
        }
    }

    return -1;
}


//-----------------------------------------------------------------------------
// Returns contents for a given joint
//-----------------------------------------------------------------------------
int GetContents(const char *pJointName) {
    while (pJointName) {
        // First try to find this joint
        int nContents = FindContents(pJointName);
        if (nContents != -1)
            return nContents;

        // If we can't find the joint, then find it's parent...
        if (!g_numbones)
            return s_nDefaultContents;

        int i = findGlobalBone(pJointName);

        if ((i >= 0) && (g_bonetable[i].parent >= 0)) {
            pJointName = g_bonetable[g_bonetable[i].parent].name;
        } else {
            pJointName = 0;
        }
    }

    // No match, return the default one
    return s_nDefaultContents;
}


//-----------------------------------------------------------------------------
// Checks specified contents
//-----------------------------------------------------------------------------
void ConsistencyCheckContents() {
    for (int i = s_JointContents.Count(); --i >= 0;) {
        int j = findGlobalBone(s_JointContents[i].m_pJointName);

        if (j < 0) {
            MdlWarning("You specified a joint contents for joint\n"
                       "    \"%s\" which either doesn't exist or was optimized out.\n",
                       s_JointSurfaceProp[i].m_pJointName);
        }
    }
}




int LookupAttachment(const char *name) {
    int i;
    for (i = 0; i < g_numattachments; i++) {
        if (stricmp(g_attachment[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}


void Grab_Vertexanimation(s_source_t *psource, const char *pAnimName) {
    char cmd[1024];
    int index;
    Vector pos;
    Vector normal;
    int t = -1;
    int count = 0;
    static s_vertanim_t tmpvanim[MAXSTUDIOVERTS * 4];

    s_sourceanim_t *pAnim = FindSourceAnim(psource, pAnimName);
    if (!pAnim) {
        MdlError("Unknown animation %s(%d) : %s\n", pAnimName, g_iLinecount, g_szLine);
    }

    while (GetLineInput()) {
        if (sscanf(g_szLine, "%d %f %f %f %f %f %f", &index, &pos[0], &pos[1], &pos[2], &normal[0], &normal[1],
                   &normal[2]) == 7) {
            if (pAnim->startframe < 0) {
                MdlError("Missing frame start(%d) : %s", g_iLinecount, g_szLine);
            }

            if (t < 0) {
                MdlError("VTA Frame Sync (%d) : %s", g_iLinecount, g_szLine);
            }

            tmpvanim[count].vertex = index;
            VectorCopy(pos, tmpvanim[count].pos);
            VectorCopy(normal, tmpvanim[count].normal);
            count++;

            if (index >= psource->numvertices) {
                psource->numvertices = index + 1;
            }
        } else {
            // flush data

            if (count) {
                pAnim->numvanims[t] = count;

                pAnim->vanim[t] = (s_vertanim_t *) calloc(count, sizeof(s_vertanim_t));

                memcpy(pAnim->vanim[t], tmpvanim, count * sizeof(s_vertanim_t));
            } else if (t > 0) {
                pAnim->numvanims[t] = 0;
            }

            // next command
            if (sscanf(g_szLine, "%1023s %d", cmd, &index)) {
                if (stricmp(cmd, "time") == 0) {
                    t = index;
                    count = 0;

                    if (t < pAnim->startframe) {
                        MdlError("Frame MdlError(%d) : %s", g_iLinecount, g_szLine);
                    }
                    if (t > pAnim->endframe) {
                        MdlError("Frame MdlError(%d) : %s", g_iLinecount, g_szLine);
                    }

                    t -= pAnim->startframe;
                } else if (!Q_stricmp(cmd, "end")) {
                    pAnim->numframes = pAnim->endframe - pAnim->startframe + 1;
                    return;
                } else {
                    MdlError("MdlError(%d) : %s", g_iLinecount, g_szLine);
                }
            } else {
                MdlError("MdlError(%d) : %s", g_iLinecount, g_szLine);
            }
        }
    }
    MdlError("unexpected EOF: %s\n", psource->filename);
}

bool GetGlobalFilePath(const char *pSrc, char *pFullPath, int nMaxLen) {
    char pFileName[1024];
    Q_strncpy(pFileName, ExpandPath((char *) pSrc), sizeof(pFileName));

    // This is kinda gross. . . doing the same work in cmdlib on SafeOpenRead.
    int nPathLength;
    if (CmdLib_HasBasePath(pFileName, nPathLength)) {
        char tmp[1024];
        int i;
        int nNumBasePaths = CmdLib_GetNumBasePaths();
        for (i = 0; i < nNumBasePaths; i++) {
            strcpy(tmp, CmdLib_GetBasePath(i));
            strcat(tmp, pFileName + nPathLength);

            struct _stat buf;
            int rt = _stat(tmp, &buf);
            if (rt != -1 && (buf.st_size > 0) && ((buf.st_mode & _S_IFDIR) == 0)) {
                Q_strncpy(pFullPath, tmp, nMaxLen);
                return true;
            }
        }
        return false;
    }

    struct _stat buf;
    int rt = _stat(pFileName, &buf);
    if (rt != -1 && (buf.st_size > 0) && ((buf.st_mode & _S_IFDIR) == 0)) {
        Q_strncpy(pFullPath, pFileName, nMaxLen);
        return true;
    }
    return false;
}


int OpenGlobalFile(char *src) {
    int time1;
    char filename[1024];

    strcpy(filename, ExpandPath(src));

    // if the file doesn't exist it might be a relative content dir path
    if (g_bContentRootRelative && !g_pFullFileSystem->FileExists(filename))
        g_pFullFileSystem->RelativePathToFullPath(src, "CONTENT", filename, sizeof(filename));

    int pathLength;
    int numBasePaths = CmdLib_GetNumBasePaths();
    // This is kinda gross. . . doing the same work in cmdlib on SafeOpenRead.
    if (CmdLib_HasBasePath(filename, pathLength)) {
        char tmp[1024];
        int i;
        for (i = 0; i < numBasePaths; i++) {
            strcpy(tmp, CmdLib_GetBasePath(i));
            strcat(tmp, filename + pathLength);
            if (g_bCreateMakefile) {
                CreateMakefile_AddDependency(tmp);
                return 0;
            }

            time1 = FileTime(tmp);
            if (time1 != -1) {
                if ((g_fpInput = fopen(tmp, "r")) == 0) {
                    MdlWarning("reader: could not open file '%s'\n", src);
                    return 0;
                } else {
                    return 1;
                }
            }
        }
        return 0;
    } else {
        time1 = FileTime(filename);
        if (time1 == -1)
            return 0;

        if (g_bCreateMakefile) {
            CreateMakefile_AddDependency(filename);
            return 0;
        }
        if ((g_fpInput = fopen(filename, "r")) == 0) {
            MdlWarning("reader: could not open file '%s'\n", src);
            return 0;
        }

        return 1;
    }
}


int Load_VTA(s_source_t *psource) {
    char cmd[1024];
    int option;

    if (!OpenGlobalFile(psource->filename))
        return 0;

    if (!g_quiet)
        printf("VTA MODEL %s\n", psource->filename);

    g_iLinecount = 0;
    while (GetLineInput()) {
        g_iLinecount++;

        const int numRead = sscanf(g_szLine, "%s %d", cmd, &option);

        // No Command Was Parsed, Blank Line Usually
        if ((numRead == EOF) || (numRead == 0))
            continue;

        if (stricmp(cmd, "version") == 0) {
            if (option != 1) {
                MdlError("bad version\n");
            }
        } else if (stricmp(cmd, "nodes") == 0) {
            psource->numbones = Grab_Nodes(psource->localBone);
        } else if (stricmp(cmd, "skeleton") == 0) {
            Grab_Animation(psource, "VertexAnimation");
        } else if (stricmp(cmd, "vertexanimation") == 0) {
            Grab_Vertexanimation(psource, "VertexAnimation");
        } else {
            MdlWarning("unknown studio command \"%s\" in vta file: \"%s\" line: %d\n", cmd, psource->filename,
                       g_iLinecount - 1);
        }
    }
    fclose(g_fpInput);

    return 1;
}

/*
===============
ParseScript
===============
*/
void ParseScript(const char *pExt) {
    while (1) {
        GetToken(true);
        if (endofscript)
            return;

        // Check all the commands we know about.
        int i;
        for (i = 0; i < g_nMDLCommandCount; i++) {
            if (Q_stricmp(g_Commands[i].m_pName, token))
                continue;

            g_Commands[i].m_pCmd();
            break;
        }
        if (i == g_nMDLCommandCount) {
            if (true) {
                if (!g_bCreateMakefile) {
                    TokenError("bad command %s\n", token);
                }
            }
        }
    }
}


//-----------------------------------------------------------------------------
// For preprocessed files, all data lies in the g_fullpath.
// The DMX loader will take care of it.
//-----------------------------------------------------------------------------
bool ParsePreprocessedFile(const char *pFullPath) {
    char pFullPathBuf[MAX_PATH];
    Q_strcpy(pFullPathBuf, pFullPath);
    Q_FixSlashes(pFullPathBuf);

    if (!LoadPreprocessedFile(pFullPathBuf, 1.0f))
        return false;

    if (!g_bHasModelName) {
        // The output name can be set via a "mdlPath" attribute on the root
        // node of the preprocessed filename.  If it wasn't set then derive it
        // from the input filename

        // The output name is directly derived from the input name
        // NOTE: We use directory names when using preprocessed files
        // Fix up passed pathname to use correct path separators otherwise
        // functions below will fail
        char pOutputBuf[MAX_PATH], pTemp[MAX_PATH], pOutputBuf2[MAX_PATH], pRelativeBuf[MAX_PATH];
        char *pOutputName = pOutputBuf;
        ComputeModFilename(pFullPathBuf, pTemp, sizeof(pTemp));
        Q_ExtractFilePath(pTemp, pOutputBuf, sizeof(pOutputBuf));
        Q_StripTrailingSlash(pOutputBuf);
        if (!Q_stricmp(Q_UnqualifiedFileName(pOutputBuf), "preprocess") ||
            !Q_stricmp(Q_UnqualifiedFileName(pOutputBuf), ".preprocess")) {
            Q_ExtractFilePath(pOutputBuf, pOutputBuf2, sizeof(pOutputBuf2));
            Q_StripTrailingSlash(pOutputBuf2);
            pOutputName = pOutputBuf2;
        }

        int nBufLen = sizeof(pOutputBuf);
        if (Q_IsAbsolutePath(pOutputName)) {
            if (!g_pFullFileSystem->FullPathToRelativePathEx(pOutputName, "GAME", pRelativeBuf, sizeof(pRelativeBuf))) {
                MdlError("Full path %s is not associated with the current mod!\n", pOutputName);
                return false;
            }
            Q_FixSlashes(pRelativeBuf);
            if (Q_strnicmp(pRelativeBuf, "models\\", 7)) {
                MdlError("Full path %s is not under the 'models' directory\n", pOutputName);
                return false;
            }
            pOutputName = pRelativeBuf + 7;
            nBufLen -= 7;
        }
        Q_SetExtension(pOutputName, "mdl", nBufLen);
        ProcessModelName(pOutputName);
    }

    return true;
}


// Used by the CheckSurfaceProps.py script.
// They specify the .mdl file and it prints out all the surface props that the model uses.
bool HandlePrintSurfaceProps(int &returnValue) {
    const char *pFilename = CommandLine()->ParmValue("-PrintSurfaceProps", (const char *) NULL);
    if (pFilename) {
        CUtlVector<char> buf;

        FILE *fp = fopen(pFilename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            buf.SetSize(ftell(fp));
            fseek(fp, 0, SEEK_SET);
            fread(buf.Base(), 1, buf.Count(), fp);

            fclose(fp);

            studiohdr_t *pHdr = (studiohdr_t *) buf.Base();

            Studio_ConvertStudioHdrToNewVersion(pHdr);

            if (pHdr->version == STUDIO_VERSION) {
                for (int i = 0; i < pHdr->numbones; i++) {
                    const mstudiobone_t *pBone = pHdr->pBone(i);
                    printf("%s\n", pBone->pszSurfaceProp());
                }

                returnValue = 0;
            } else {
                printf("-PrintSurfaceProps: '%s' is wrong version (%d should be %d).\n",
                       pFilename, pHdr->version, STUDIO_VERSION);
                returnValue = 1;
            }
        } else {
            printf("-PrintSurfaceProps: can't open '%s'\n", pFilename);
            returnValue = 1;
        }

        return true;
    } else {
        return false;
    }
}

// Used by the modelstats.pl script.
// They specify the .mdl file and it prints out perf info.
bool HandleMdlReport(int &returnValue) {
    const char *pFilename = CommandLine()->ParmValue("-mdlreport", (const char *) NULL);
    if (pFilename) {
        CUtlVector<char> buf;

        FILE *fp = fopen(pFilename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            buf.SetSize(ftell(fp));
            fseek(fp, 0, SEEK_SET);
            fread(buf.Base(), 1, buf.Count(), fp);

            fclose(fp);

            studiohdr_t *pHdr = (studiohdr_t *) buf.Base();

            Studio_ConvertStudioHdrToNewVersion(pHdr);

            if (pHdr->version == STUDIO_VERSION) {
                int flags = SPEWPERFSTATS_SHOWPERF;
                if (CommandLine()->CheckParm("-mdlreportspreadsheet", nullptr)) {
                    flags |= SPEWPERFSTATS_SPREADSHEET;
                }
                SpewPerfStats(pHdr, pFilename, flags);

                returnValue = 0;
            } else {
                printf("-mdlreport: '%s' is wrong version (%d should be %d).\n",
                       pFilename, pHdr->version, STUDIO_VERSION);
                returnValue = 1;
            }
        } else {
            printf("-mdlreport: can't open '%s'\n", pFilename);
            returnValue = 1;
        }

        return true;
    } else {
        return false;
    }
}


// Debugging function that enumerate all a models bones to stdout.
static void SpewBones() {
    MdlWarning("g_numbones %i\n", g_numbones);

    for (int i = g_numbones; --i >= 0;) {
        printf("%s\n", g_bonetable[i].name);
    }
}

void UsageAndExit() {
    MdlError("Bad or missing options\n"
             #ifdef MDLCOMPILE
             "usage: mdlcompile [options] <file.mc>\n"
             #else
             "usage: studiomdl [options] <file.qc>\n"
             #endif
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
             "[-n] - tag bad normals\n"
             "[-perf] report perf info upon compiling model\n"
             "[-printbones]\n"
             "[-printgraph]\n"
             "[-quiet] - operate silently\n"
             "[-r] - tag reversed\n"
             "[-t <texture>]\n"
             "[-x360] - generate xbox360 output\n"
             "[-nox360] - disable xbox360 output(default)\n"
             "[-fastbuild] - write a single vertex windings file\n"
             "[-nowarnings] - disable warnings\n"
             "[-dumpmaterials] - dump out material names\n"
             "[-mdlreport] model.mdl - report perf info\n"
             "[-mdlreportspreadsheet] - report perf info as a comma-delimited spreadsheet\n"
             "[-striplods] - use only lod0\n"
             "[-overridedefinebones] - equivalent to specifying $unlockdefinebones in " SRC_FILE_EXT " file\n"
             "[-stripmodel] - process binary model files and strip extra lod data\n"
             "[-stripvhv] - strip hardware verts to match the stripped model\n"
             "[-vsi] - generate stripping information .vsi file - can be used on .mdl files too\n"
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
             "[-nop4]\n"
    );
}

LONG __stdcall VExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    MdlExceptionFilter(ExceptionInfo->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER; // (never gets here anyway)
}

#ifndef _DEBUG

#endif
/*
==============
main
==============
*/


//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CStudioMDLApp : public CDefaultAppSystemGroup<CSteamAppSystemGroup> {
    typedef CDefaultAppSystemGroup<CSteamAppSystemGroup> BaseClass;

public:
    // Methods of IApplication
    virtual bool Create();

    virtual bool PreInit();

    virtual int Main();

    virtual void PostShutdown();

    virtual void Destroy();

private:
    int Main_StripModel();

    int Main_StripVhv();

    int Main_MakeVsi();

private:
    bool ParseArguments();
};

static bool
CStudioMDLApp_SuggestGameInfoDirFn(CFSSteamSetupInfo const *pFsSteamSetupInfo, char *pchPathBuffer, int nBufferLength,
                                   bool *pbBubbleDirectories) {
    const char *pProcessFileName = NULL;
    int nParmCount = CommandLine()->ParmCount();
    if (nParmCount > 1) {
        pProcessFileName = CommandLine()->GetParm(nParmCount - 1);
    }

    if (pProcessFileName) {
        Q_MakeAbsolutePath(pchPathBuffer, nBufferLength, pProcessFileName);

        if (pbBubbleDirectories)
            *pbBubbleDirectories = true;

        return true;
    }

    return false;
}

int main(int argc, char **argv) {
    SetSuggestGameInfoDirFn(CStudioMDLApp_SuggestGameInfoDirFn);

    CStudioMDLApp s_ApplicationObject;
    CSteamApplication s_SteamApplicationObject(&s_ApplicationObject);
    return AppMain(argc, argv, &s_SteamApplicationObject);
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
#ifndef _DEBUG
#endif

    if (CommandLine()->ParmCount() == 1) {
        UsageAndExit();
        return false;
    }

    int nReturnValue;
    if (HandlePrintSurfaceProps(nReturnValue))
        return false;

    if (!ParseArguments())
        return false;


    AddSystem(g_pDataModel, VDATAMODEL_INTERFACE_VERSION);
    AddSystem(g_pDmElementFramework, VDMELEMENTFRAMEWORK_VERSION);
    AddSystem(g_pDmSerializers, DMSERIALIZERS_INTERFACE_VERSION);

    // Add in the locally-defined studio data cache
    AppModule_t studioDataCacheModule = LoadModule(Sys_GetFactoryThis());
    AddSystem(studioDataCacheModule, STUDIO_DATA_CACHE_INTERFACE_VERSION);

    return true;
}

void CStudioMDLApp::Destroy() {
    LoggingSystem_PopLoggingState();
}

bool CStudioMDLApp::PreInit() {
    CreateInterfaceFn factory = GetFactory();
    ConnectTier1Libraries(&factory, 1);
    ConnectTier2Libraries(&factory, 1);
    ConnectTier3Libraries(&factory, 1);

    if (!g_pFullFileSystem || !g_pDataModel /*|| !g_pMaterialSystem || !g_pStudioRender*/ ) {
        Warning("StudioMDL is missing a required interface!\n");
        return false;
    }

    if (!SetupSearchPaths(g_path, false, true))
        return false;

    // NOTE: This is necessary to get the cmdlib filesystem stuff to work.
    g_pFileSystem = g_pFullFileSystem;

    // NOTE: This is stuff copied out of cmdlib necessary to get
    // the tools in cmdlib working
    FileSystem_SetupStandardDirectories(g_path, GetGameInfoPath());
    return true;
}


void CStudioMDLApp::PostShutdown() {
    DisconnectTier3Libraries();
    DisconnectTier2Libraries();
    DisconnectTier1Libraries();
}


//-----------------------------------------------------------------------------
// Method which parses arguments
//-----------------------------------------------------------------------------
bool CStudioMDLApp::ParseArguments() {
    g_currentscale = g_defaultscale = 1.0;
    g_defaultrotation = RadianEuler(0, 0, M_PI / 2);

    // skip weightlist 0
    g_numweightlist = 1;

    eyeposition = Vector(0, 0, 0);
    gflags = 0;
    numrep = 0;

    normal_blend = cos(DEG2RAD(2.0));

    g_gamma = 2.2;

    g_staticprop = false;
    g_centerstaticprop = false;

    g_realignbones = false;
    g_constdirectionalightdot = 0;

    g_bDumpGLViewFiles = false;
    g_quiet = false;

    g_illumpositionattachment = 0;
    g_flMaxEyeDeflection = 0.0f;

    g_collapse_bones_message = false;

    int argc = CommandLine()->ParmCount();
    int i;
    for (i = 1; i < argc - 1; i++) {
        const char *pArgv = CommandLine()->GetParm(i);
        if (pArgv[0] != '-')
            continue;

        if (!Q_stricmp(pArgv, "-collapsereport")) {
            g_collapse_bones_message = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-parsecompletion")) {
            // reliably prints output we can parse for automatically
            g_parseable_completion_output = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-allowdebug")) {
            // Ignore, used by interface system to catch debug builds checked into release tree
            continue;
        }

        if (!Q_stricmp(pArgv, "-mdlreport")) {
            // Will reparse later, ignore rest of arguments.
            return true;
        }

        if (!Q_stricmp(pArgv, "-mdlreportspreadsheet")) {
            // Will reparse later, ignore for now.
            continue;
        }

        if (!Q_stricmp(pArgv, "-overridedefinebones")) {
            g_bDefineBonesLockedByDefault = false;
            continue;
        }

        if (!Q_stricmp(pArgv, "-striplods")) {
            g_bStripLods = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-stripmodel")) {
            g_eRunMode = RUN_MODE_STRIP_MODEL;
            continue;
        }

        if (!Q_stricmp(pArgv, "-stripvhv")) {
            g_eRunMode = RUN_MODE_STRIP_VHV;
            continue;
        }

        if (!Q_stricmp(pArgv, "-vsi")) {
            g_bMakeVsi = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-quiet")) {
            g_quiet = true;
            g_verbose = false;
            continue;
        }

        if (!Q_stricmp(pArgv, "-verbose")) {
            g_quiet = false;
            g_verbose = true;
            continue;
        }


        if (!Q_stricmp(pArgv, "-checklengths")) {
            g_bCheckLengths = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-printbones")) {
            g_bPrintBones = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-perf")) {
            g_bPerf = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-printgraph")) {
            g_bDumpGraph = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-definebones")) {
            g_definebones = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-makefile")) {
            g_bCreateMakefile = true;
            g_quiet = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-verify")) {
            g_bVerifyOnly = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-minlod")) {
            g_minLod = atoi(CommandLine()->GetParm(++i));
            continue;
        }

        if (!Q_stricmp(pArgv, "-fastbuild")) {
            g_bFastBuild = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-x360")) {
            StudioByteSwap::ActivateByteSwapping(true); // Set target to big endian
            g_bX360 = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-nox360")) {
            g_bX360 = false;
            continue;
        }

        if (!Q_stricmp(pArgv, "-nowarnings")) {
            g_bNoWarnings = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-maxwarnings")) {
            g_maxWarnings = atoi(CommandLine()->GetParm(++i));
            continue;
        }

        if (!Q_stricmp(pArgv, "-preview")) {
            g_bBuildPreview = true;
            continue;
        }

        if (!Q_stricmp(pArgv, "-dumpmaterials")) {
            g_bDumpMaterials = true;
            continue;
        }

        if (pArgv[1] && pArgv[2] == '\0') {
            switch (pArgv[1]) {
                case 't':
                    i++;
                    strcpy(defaulttexture[numrep], pArgv);
                    if (i < argc - 2 && CommandLine()->GetParm(i + 1)[0] != '-') {
                        i++;
                        strcpy(sourcetexture[numrep], pArgv);
                        printf("Replacing %s with %s\n", sourcetexture[numrep], defaulttexture[numrep]);
                    }
                    printf("Using default texture: %s\n", defaulttexture);
                    numrep++;
                    break;
                case 'a':
                    i++;
                    normal_blend = cos(DEG2RAD(verify_atof(pArgv)));
                    break;
                case 'h':
                    dump_hboxes = 1;
                    break;
                case 'i':
                    ignore_warnings = 1;
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

    const char *pArgv = CommandLine()->GetParm(i);
    Q_strncpy(g_path, pArgv, sizeof(g_path));
    if (Q_IsAbsolutePath(g_path)) {
        // Set the working directory to be the path of the qc file
        // so the relative-file fopen code works
        char pQCDir[MAX_PATH];
        Q_ExtractFilePath(g_path, pQCDir, sizeof(pQCDir));
        _chdir(pQCDir);
    }
    Q_StripExtension(pArgv, g_outname, sizeof(g_outname));
    return true;
}


//-----------------------------------------------------------------------------
// Purpose: search through the "GamePath" key and create a mirrored version in the content path searches
//-----------------------------------------------------------------------------

void AddContentPaths() {
    // look for the "content" in the path to the initial QC file
    char *match = "content\\";
    char *sp = strstr(qdir, match);
    if (!sp)
        return;

    // copy off everything before and including "content"
    char pre[1024];
    strncpy(pre, qdir, sp - qdir + strlen(match));
    pre[sp - qdir + strlen(match)] = '\0';
    sp = sp + strlen(match);

    // copy off everything following the word after "content"
    char post[1024];
    sp = strstr(sp + 1, "\\");
    strcpy(post, sp);

    // get a copy of the game search paths
    char paths[1024];
    g_pFullFileSystem->GetSearchPath("GAME", false, paths, sizeof(paths));
    if (!g_quiet)
        printf("all paths:%s\n", paths);

    // pull out the game names and insert them into a content path string
    sp = strstr(paths, "game\\");
    while (sp) {
        char temp[1024];
        sp = sp + 5;
        char *sz = strstr(sp, "\\");
        if (!sz)
            return;

        strcpy(temp, pre);
        strncat(temp, sp, sz - sp);
        strcat(temp, post);
        sp = sz;
        sp = strstr(sp, "game\\");
        CmdLib_AddBasePath(temp);
        if (!g_quiet)
            printf("content:%s\n", temp);
    }
}

//////////////////////////////////////////////////////////////////////////
// Purpose: parses the game info file to retrieve relevant settings
//////////////////////////////////////////////////////////////////////////
struct GameInfo_t g_gameinfo;

void ParseGameInfo() {
    bool bParsed = false;

    GameInfo_t gameinfoDefault;
    gameinfoDefault.bSupportsXBox360 = false;
    gameinfoDefault.bSupportsDX8 = true;

    KeyValues *pKeyValues = new KeyValues("gameinfo.txt");
    if (pKeyValues != nullptr) {
        if (g_pFileSystem && pKeyValues->LoadFromFile(g_pFileSystem, "gameinfo.txt")) {
            g_gameinfo.bSupportsXBox360 = !!pKeyValues->GetInt("SupportsXBox360",
                                                               (int) gameinfoDefault.bSupportsXBox360);
            g_gameinfo.bSupportsDX8 = !!pKeyValues->GetInt("SupportsDX8", (int) gameinfoDefault.bSupportsDX8);
            bParsed = true;
        }
        pKeyValues->deleteThis();
    }

    if (!bParsed) {
        g_gameinfo = gameinfoDefault;
    }
}


//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
int CStudioMDLApp::Main() {
    g_numverts = g_numnormals = g_numfaces = 0;
    for (int &g_numtexcoord: g_numtexcoords) {
        g_numtexcoord = 0;
    }

    // Set the named changelist
#ifdef MDLCOMPILE
    //	g_p4factory->SetDummyMode( true );	// Don't use perforce with mdlcompile
#else
//	g_p4factory->SetOpenFileChangeList( "StudioMDL Auto Checkout" );
#endif

    // This bit of hackery allows us to access files on the harddrive
    g_pFullFileSystem->AddSearchPath("", "LOCAL", PATH_ADD_TO_HEAD);

//	g_pMaterialSystem->ModInit();
//	MaterialSystem_Config_t config;
//	g_pMaterialSystem->OverrideConfig( config, false );

    int nReturnValue;
    if (HandleMdlReport(nReturnValue))
        return false;

    // Don't bother with undo here
    g_pDataModel->SetUndoEnabled(false);

    // look for the "content\hl2x" string in the qdir and add what should be the correct path as an alternate
    // FIXME: add these to an envvar if folks are using complicated directory mappings instead of defaults
    char *match = "content\\hl2x\\";
    char *sp = strstr(qdir, match);
    if (sp) {
        char temp[1024];
        strncpy(temp, qdir, sp - qdir + strlen(match));
        temp[sp - qdir + strlen(match)] = '\0';
        CmdLib_AddBasePath(temp);
        strcat(temp, "..\\..\\..\\..\\main\\content\\hl2\\");
        CmdLib_AddBasePath(temp);
    }

    AddContentPaths();

    ParseGameInfo();

    if (!g_quiet) {
        printf("qdir:    \"%s\"\n", qdir);
        printf("gamedir: \"%s\"\n", gamedir);
        printf("g_path:  \"%s\"\n", g_path);
    }

    switch (g_eRunMode) {
        case RUN_MODE_STRIP_MODEL:
            return Main_StripModel();

        case RUN_MODE_STRIP_VHV:
            return Main_StripVhv();

        case RUN_MODE_BUILD:
        default:
            break;
    }

    const char *pExt = Q_GetFileExtension(g_path);

    // Look for the presence of a .mdl file (only -vsi is currently supported for .mdl files)
    if (pExt && !Q_stricmp(pExt, "mdl")) {
        if (g_bMakeVsi)
            return Main_MakeVsi();

        printf("ERROR: " SRC_FILE_EXT " or .dmx file should be specified to build.\n");
        return 1;
    }

    if (!g_quiet)
        printf("Building binary model files...\n");

    bool bLoadingPreprocessedFile = false;
#ifdef MDLCOMPILE
                                                                                                                            if ( pExt && !Q_stricmp( pExt, "mpp" ) )
	{
		bLoadingPreprocessedFile = true;
		// Handle relative path names because g_path is appended onto qdir which is the
		// absolute path to the file minus the filename
		Q_FileBase( g_path, g_path, sizeof( g_path ) );
		Q_DefaultExtension( g_path, "mpp" , sizeof( g_path ) );
	}
	if ( !pExt && !bLoadingPreprocessedFile )
	{
#endif
    Q_FileBase(g_path, g_path, sizeof(g_path));
    Q_DefaultExtension(g_path, SRC_FILE_EXT, sizeof(g_path));
    if (!pExt) {
        pExt = SRC_FILE_EXT;
    }
#ifdef MDLCOMPILE
    }
#endif

    if (!g_quiet) {
        printf("Working on \"%s\"\n", g_path);
    }

    // Set up script loading callback, discarding default callback
    (void) SetScriptLoadedCallback(StudioMdl_ScriptLoadedCallback);

    // load the script
    if (!bLoadingPreprocessedFile) {
        LoadScriptFile(g_path);
    }

    strcpy(g_fullpath, g_path);
    strcpy(g_fullpath, ExpandPath(g_fullpath));
    strcpy(g_fullpath, ExpandArg(g_fullpath));

    // default to having one entry in the LOD list that doesn't do anything so
    // that we don't have to do any special cases for the first LOD.
    g_ScriptLODs.Purge();
    g_ScriptLODs.AddToTail(); // add an empty one
    g_ScriptLODs[0].switchValue = 0.0f;

    //
    // parse it
    //
    ClearModel();

//	strcpy( g_pPlatformName, "" );
    if (bLoadingPreprocessedFile) {
        if (!ParsePreprocessedFile(g_fullpath)) {
            MdlError("Invalid MPP File: %s\n", g_path);
            return 1;
        }
    } else {
        ParseScript(pExt);
    }

    if (!g_bCreateMakefile) {
        int nCount = g_numsources;
        for (int i = 0; i < nCount; i++) {
            if (g_source[i]->isActiveModel) {
                ClampMaxVerticesPerModel(g_source[i]);
            }
        }

        SetSkinValues();

        SimplifyModel();

        ConsistencyCheckSurfaceProp();
        ConsistencyCheckContents();
        CollisionModel_Build();
        // ValidateSharedAnimationGroups();

        WriteModelFiles();
    }

    if (g_bCreateMakefile) {
        CreateMakefile_OutputMakefile();
    } else if (g_bMakeVsi) {
        Q_snprintf(g_path, ARRAYSIZE(g_path), "%smodels/%s", gamedir, g_outname);
        Main_MakeVsi();
    }

    if (!g_quiet) {
        printf("\nCompleted \"%s\"\n", g_path);
    }

    if (g_parseable_completion_output) {
        printf("\nRESULT: SUCCESS\n");
    }

    g_pDataModel->UnloadFile(DMFILEID_INVALID);

    return 0;
}


//
// WriteFileToDisk
//	Equivalent to g_pFullFileSystem->WriteFile( pFileName, pPath, buf ), but works
//	for relative paths.
//
bool WriteFileToDisk(const char *pFileName, const char *pPath, CUtlBuffer &buf) {
    // For some reason calling full filesystem will write into hl2 root dir
    // return g_pFullFileSystem->WriteFile( pFileName, pPath, buf );

    FILE *f = fopen(pFileName, "wb");
    if (!f)
        return false;

    fwrite(buf.Base(), 1, buf.TellPut(), f);
    fclose(f);
    return true;
}

//
// WriteBufferToFile
//	Helper to concatenate file base and extension.
//
bool WriteBufferToFile(CUtlBuffer &buf, const char *szFilebase, const char *szExt) {
    char szFilename[1024];
    Q_snprintf(szFilename, ARRAYSIZE(szFilename), "%s%s", szFilebase, szExt);
    return WriteFileToDisk(szFilename, nullptr, buf);
}


//
// LoadBufferFromFile
//	Loads the buffer from file, return true on success, false otherwise.
//  If bError is true prints an error upon failure.
//
bool LoadBufferFromFile(CUtlBuffer &buffer, const char *szFilebase, const char *szExt, bool bError = true) {
    char szFilename[1024];
    Q_snprintf(szFilename, ARRAYSIZE(szFilename), "%s%s", szFilebase, szExt);

    if (g_pFullFileSystem->ReadFile(szFilename, nullptr, buffer))
        return true;

    if (bError)
        MdlError("Failed to open '%s'!\n", szFilename);

    return false;
}


bool Load3ModelBuffers(CUtlBuffer &bufMDL, CUtlBuffer &bufVVD, CUtlBuffer &bufVTX, const char *szFilebase) {
    // Load up the mdl file
    if (!LoadBufferFromFile(bufMDL, szFilebase, ".mdl"))
        return false;

    // Load up the vvd file
    if (!LoadBufferFromFile(bufVVD, szFilebase, ".vvd"))
        return false;

    // Load up the dx90.vtx file
    if (!LoadBufferFromFile(bufVTX, szFilebase, ".dx90.vtx"))
        return false;

    return true;
}


//////////////////////////////////////////////////////////////////////////
//
// Studiomdl hooks to call the stripping routines:
//	Main_StripVhv
//	Main_StripModel
//
//////////////////////////////////////////////////////////////////////////

int CStudioMDLApp::Main_StripVhv() {
    if (!g_quiet) {
        printf("Stripping vhv data...\n");
    }

    if (!mdllib) {
        printf("ERROR: mdllib is not available!\n");
        return 1;
    }

    Q_StripExtension(g_path, g_path, sizeof(g_path));
    char *pExt = g_path + strlen(g_path);
    *pExt = 0;

    //
    // ====== Load files
    //

    // Load up the vhv file
    CUtlBuffer bufVHV;
    if (!LoadBufferFromFile(bufVHV, g_path, ".vhv"))
        return 1;

    // Load up the info.strip file
    CUtlBuffer bufRemapping;
    if (!LoadBufferFromFile(bufRemapping, g_path, ".info.strip", false) &&
        !LoadBufferFromFile(bufRemapping, g_path, ".vsi"))
        return 1;

    //
    // ====== Process file contents
    //

    bool bResult = false;
    {
        LoggingSystem_SetChannelSpewLevelByName("ModelLib", LS_MESSAGE);

        IMdlStripInfo *pMdlStripInfo = NULL;

        if (mdllib->CreateNewStripInfo(&pMdlStripInfo)) {
            pMdlStripInfo->UnSerialize(bufRemapping);
            bResult = pMdlStripInfo->StripHardwareVertsBuffer(bufVHV);
        }

        if (pMdlStripInfo)
            pMdlStripInfo->DeleteThis();
    }

    if (!bResult) {
        printf("ERROR: stripping failed!\n");
        return 1;
    }

    //
    // ====== Save out processed data
    //

    // Save vhv
    if (!WriteBufferToFile(bufVHV, g_path, ".vhv.strip")) {
        printf("ERROR: Failed to save '%s'!\n", g_path);
        return 1;
    }

    return 0;
}

int CStudioMDLApp::Main_MakeVsi() {
    if (!mdllib) {
        printf("ERROR: mdllib is not available!\n");
        return 1;
    }

    Q_StripExtension(g_path, g_path, sizeof(g_path));
    char *pExt = g_path + strlen(g_path);
    *pExt = 0;

    // Load up the files
    CUtlBuffer bufMDL;
    CUtlBuffer bufVVD;
    CUtlBuffer bufVTX;
    if (!Load3ModelBuffers(bufMDL, bufVVD, bufVTX, g_path))
        return 1;

    //
    // ====== Process file contents
    //

    CUtlBuffer bufMappingTable;
    bool bResult = false;
    {
        if (!g_quiet) {
            printf("---------------------\n");
            printf("Generating .vsi stripping information...\n");

            LoggingSystem_SetChannelSpewLevelByName("ModelLib", LS_MESSAGE);
        }

        IMdlStripInfo *pMdlStripInfo = NULL;

        bResult =
                mdllib->StripModelBuffers(bufMDL, bufVVD, bufVTX, &pMdlStripInfo) &&
                pMdlStripInfo->Serialize(bufMappingTable);

        if (pMdlStripInfo)
            pMdlStripInfo->DeleteThis();
    }

    if (!bResult) {
        printf("ERROR: stripping failed!\n");
        return 1;
    }

    //
    // ====== Save out processed data
    //

    // Save remapping data using "P4 edit -> save -> P4 add"  approach
    sprintf(pExt, ".vsi");
//	CP4AutoEditAddFile _auto_edit_vsi( g_path );

    if (!WriteFileToDisk(g_path, nullptr, bufMappingTable)) {
        printf("ERROR: Failed to save '%s'!\n", g_path);
        return 1;
    } else if (!g_quiet) {
        printf("Generated .vsi stripping information.\n");
    }

    return 0;
}

int CStudioMDLApp::Main_StripModel() {
    if (!g_quiet) {
        printf("Stripping binary model files...\n");
    }

    if (!mdllib) {
        printf("ERROR: mdllib is not available!\n");
        return 1;
    }

    Q_FileBase(g_path, g_path, sizeof(g_path));
    char *pExt = g_path + strlen(g_path);
    *pExt = 0;

    // Load up the files
    CUtlBuffer bufMDL;
    CUtlBuffer bufVVD;
    CUtlBuffer bufVTX;
    if (!Load3ModelBuffers(bufMDL, bufVVD, bufVTX, g_path))
        return 1;

    //
    // ====== Process file contents
    //

    CUtlBuffer bufMappingTable;
    bool bResult = false;
    {
        LoggingSystem_SetChannelSpewLevelByName("ModelLib", LS_MESSAGE);

        IMdlStripInfo *pMdlStripInfo = NULL;

        bResult =
                mdllib->StripModelBuffers(bufMDL, bufVVD, bufVTX, &pMdlStripInfo) &&
                pMdlStripInfo->Serialize(bufMappingTable);

        if (pMdlStripInfo)
            pMdlStripInfo->DeleteThis();
    }

    if (!bResult) {
        printf("ERROR: stripping failed!\n");
        return 1;
    }

    //
    // ====== Save out processed data
    //

    // Save mdl
    sprintf(pExt, ".mdl.strip");
    if (!WriteFileToDisk(g_path, nullptr, bufMDL)) {
        printf("ERROR: Failed to save '%s'!\n", g_path);
        return 1;
    }

    // Save vvd
    sprintf(pExt, ".vvd.strip");
    if (!WriteFileToDisk(g_path, nullptr, bufVVD)) {
        printf("ERROR: Failed to save '%s'!\n", g_path);
        return 1;
    }

    // Save vtx
    sprintf(pExt, ".vtx.strip");
    if (!WriteFileToDisk(g_path, nullptr, bufVTX)) {
        printf("ERROR: Failed to save '%s'!\n", g_path);
        return 1;
    }

    // Save remapping data
    sprintf(pExt, ".info.strip");
    if (!WriteFileToDisk(g_path, nullptr, bufMappingTable)) {
        printf("ERROR: Failed to save '%s'!\n", g_path);
        return 1;
    }

    return 0;
}

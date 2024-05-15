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
#include <vector>
#include "common/filesystem_tools.h"
#include "common/cmdlib.h"
#include "common/scriplib.h"

#define EXTERN

#include "studiomdl/studiomdl.h"
#include "bspflags.h"
#include "bitvec.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmeanimationset.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmeflexrules.h"
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


StudioMdlContext g_StudioMdlContext;

//-----------------------------------------------------------------------------
//  Stuff for writing a makefile to build models incrementally.
//-----------------------------------------------------------------------------
void CreateMakefile_AddDependency(const char *pFileName) {
    if (!g_StudioMdlContext.createMakefile) {
        return;
    }

    CUtlSymbol sym(pFileName);
    int i;
    for (i = 0; i < g_StudioMdlContext.CreateMakefileDependencies.size(); i++) {
        if (g_StudioMdlContext.CreateMakefileDependencies[i] == sym) {
            return;
        }
    }
    g_StudioMdlContext.CreateMakefileDependencies.emplace_back(sym);
}

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
bool GetLineInput() {
    while (fgets(g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine), g_StudioMdlContext.fpInput) != NULL) {
        g_StudioMdlContext.iLinecount++;
        // skip comments
        if (g_StudioMdlContext.szLine[0] == '/' && g_StudioMdlContext.szLine[1] == '/')
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
        if (g_StudioMdlContext.dumpMaterials) {
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
        if (sscanf(g_StudioMdlContext.szLine, "%d \"%[^\"]\" %d", &index, name, &parent) == 3) {
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
    MdlError("Unexpected EOF at line %d\n", g_StudioMdlContext.iLinecount);
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

void CClampedSource::Init(int numvertices) {
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
        if (sscanf(g_StudioMdlContext.szLine, "%d %f %f %f %f %f %f", &index, &pos[0], &pos[1], &pos[2], &rot[0], &rot[1], &rot[2]) ==
            7) {
            if (pAnim->startframe < 0) {
                MdlError("Missing frame start(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }

            scale_vertex(pos);
            VectorCopy(pos, pAnim->rawanim[t][index].pos);
            VectorCopy(rot, pAnim->rawanim[t][index].rot);

            clip_rotations(rot); // !!!
            continue;
        }

        if (sscanf(g_StudioMdlContext.szLine, "%1023s %d", cmd, &index) == 0) {
            MdlError("MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            continue;
        }

        if (!Q_stricmp(cmd, "time")) {
            t = index;
            if (pAnim->startframe == -1) {
                pAnim->startframe = t;
            }
            if (t < pAnim->startframe) {
                MdlError("Frame MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
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

        MdlError("MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
    }

    MdlError("unexpected EOF: %s\n", pSource->filename);
}

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
    for (int fmt_id = 0; fmt_id < supported_formats.size(); ++fmt_id) {
        if ((!result && xext[0] == '\0') || std::strcmp(xext, supported_formats[fmt_id].first) == 0) {
            std::snprintf(g_StudioMdlContext.szFilename, sizeof(g_StudioMdlContext.szFilename), "%s%s.%s", cddir[numdirs], pTempName,
                          supported_formats[fmt_id].first);
            std::strncpy(pSource->filename, g_StudioMdlContext.szFilename, sizeof(pSource->filename));
            result = (supported_formats[fmt_id].second)(pSource);
        }
    }

    if (!g_StudioMdlContext.createMakefile && !result) {
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
// Adds a joint surface property
//-----------------------------------------------------------------------------
void AddSurfaceProp(const char *pBoneName, const char *pSurfaceProperty) {
    // Search for the name in our list
    int i;
    for (i = g_StudioMdlContext.JointSurfaceProp.Count(); --i >= 0;) {
        if (!Q_stricmp(g_StudioMdlContext.JointSurfaceProp[i].m_pJointName, pBoneName))
            break;
    }

    // Add new entry if we haven't seen this name before
    if (i < 0) {
        i = g_StudioMdlContext.JointSurfaceProp.AddToTail();
        Q_strncpy(g_StudioMdlContext.JointSurfaceProp[i].m_pJointName, pBoneName, sizeof(g_StudioMdlContext.JointSurfaceProp[i].m_pJointName));
    }

    Q_strncpy(g_StudioMdlContext.JointSurfaceProp[i].m_pSurfaceProp, pSurfaceProperty, sizeof(g_StudioMdlContext.JointSurfaceProp[i].m_pSurfaceProp));
}

//-----------------------------------------------------------------------------
// Returns the default surface prop name
//-----------------------------------------------------------------------------
char *GetDefaultSurfaceProp() {
    return g_StudioMdlContext.pDefaultSurfaceProp;
}

//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
char *FindSurfaceProp(const char *pJointName) {
    for (int i = g_StudioMdlContext.JointSurfaceProp.Count(); --i >= 0;) {
        if (!Q_stricmp(g_StudioMdlContext.JointSurfaceProp[i].m_pJointName, pJointName))
            return g_StudioMdlContext.JointSurfaceProp[i].m_pSurfaceProp;
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
        if (!g_StudioMdlContext.numbones)
            return g_StudioMdlContext.pDefaultSurfaceProp;

        int i = findGlobalBone(pJointName);

        if ((i >= 0) && (g_bonetable[i].parent >= 0)) {
            pJointName = g_bonetable[g_bonetable[i].parent].name;
        } else {
            pJointName = 0;
        }
    }

    // No match, return the default one
    return g_StudioMdlContext.pDefaultSurfaceProp;
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
        if (!g_StudioMdlContext.numbones)
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
        MdlError("Unknown animation %s(%d) : %s\n", pAnimName, g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
    }

    while (GetLineInput()) {
        if (sscanf(g_StudioMdlContext.szLine, "%d %f %f %f %f %f %f", &index, &pos[0], &pos[1], &pos[2], &normal[0], &normal[1],
                   &normal[2]) == 7) {
            if (pAnim->startframe < 0) {
                MdlError("Missing frame start(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
            }

            if (t < 0) {
                MdlError("VTA Frame Sync (%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
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
            if (sscanf(g_StudioMdlContext.szLine, "%1023s %d", cmd, &index)) {
                if (stricmp(cmd, "time") == 0) {
                    t = index;
                    count = 0;

                    if (t < pAnim->startframe) {
                        MdlError("Frame MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
                    }
                    if (t > pAnim->endframe) {
                        MdlError("Frame MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
                    }

                    t -= pAnim->startframe;
                } else if (!Q_stricmp(cmd, "end")) {
                    pAnim->numframes = pAnim->endframe - pAnim->startframe + 1;
                    return;
                } else {
                    MdlError("MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
                }
            } else {
                MdlError("MdlError(%d) : %s", g_StudioMdlContext.iLinecount, g_StudioMdlContext.szLine);
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
    if (g_StudioMdlContext.bContentRootRelative && !g_pFullFileSystem->FileExists(filename))
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
            if (g_StudioMdlContext.createMakefile) {
                CreateMakefile_AddDependency(tmp);
                return 0;
            }

            time1 = FileTime(tmp);
            if (time1 != -1) {
                if ((g_StudioMdlContext.fpInput = fopen(tmp, "r")) == 0) {
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

        if (g_StudioMdlContext.createMakefile) {
            CreateMakefile_AddDependency(filename);
            return 0;
        }
        if ((g_StudioMdlContext.fpInput = fopen(filename, "r")) == 0) {
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

    if (!g_StudioMdlContext.quiet)
        printf("VTA MODEL %s\n", psource->filename);

    g_StudioMdlContext.iLinecount = 0;
    while (GetLineInput()) {
        g_StudioMdlContext.iLinecount++;

        const int numRead = sscanf(g_StudioMdlContext.szLine, "%s %d", cmd, &option);

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
                       g_StudioMdlContext.iLinecount - 1);
        }
    }
    fclose(g_StudioMdlContext.fpInput);

    return 1;
}
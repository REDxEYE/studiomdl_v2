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

#include "studiomdl/platform_compat.h"

#undef GetCurrentDirectory

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "common/cmdlib.h"
#include "common/scriplib.h"

#include "studiomdl/studiomdl.h"
#include "bspflags.h"
#include "bitvec.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmeflexrules.h"

#include "studiomdl_commands.h"
#include "interfaces/interfaces.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"
#include "studiomdl/flexoptimize.h"
#include "studiomdl/studiomdl_helpers.hpp"


#ifdef WIN32
#undef strdup
#define strdup _strdup
#endif

//-----------------------------------------------------------------------------
//  Stuff for writing a makefile to build models incrementally.
//-----------------------------------------------------------------------------
void CreateMakefile_AddDependency(StudioMdlState &ctx, const char *pFileName) {
    if (!ctx.options.createMakefile) {
        return;
    }

    auto &dependencies = ctx.paths.CreateMakefileDependencies;
    if (std::ranges::find(dependencies, std::string_view(pFileName)) == dependencies.end()) {
        dependencies.emplace_back(pFileName);
    }
}

//-----------------------------------------------------------------------------
// Read global input into common string
//-----------------------------------------------------------------------------
bool GetLineInput(StudioMdlState &ctx) {
    while (ctx.input.inputFile->Gets(ctx.input.szLine, sizeof(ctx.input.szLine)) != nullptr) {
        ctx.input.iLinecount++;
        // skip comments
        if (ctx.input.szLine[0] == '/' && ctx.input.szLine[1] == '/')
            continue;

        return true;
    }
    return false;
}

int LookupPoseParameter(StudioMdlState &ctx, const char *name) {
    int i;
    for (i = 0; i < std::ssize(ctx.anim.pose); i++) {
        if (vstd::iequals(name, ctx.anim.pose[i].name)) {
            return i;
        }
    }

    if (i >= MAXSTUDIOPOSEPARAM) {
        MdlError("too many pose parameters (max {})\n", MAXSTUDIOPOSEPARAM);
    }
    strcpyn(ctx.anim.pose.emplace_back().name, name);

    return i;
}

//-----------------------------------------------------------------------------
// Stuff for writing a makefile to build models incrementally.
//-----------------------------------------------------------------------------
s_sourceanim_t *FindSourceAnim(s_source_t *source, const std::string_view animName) {
    for (auto& anim: source->m_Animations) {
        if (vstd::iequals(animName, anim.animationname))
            return &anim;
    }
    return nullptr;
}

const s_sourceanim_t *FindSourceAnim(const s_source_t *source, const std::string_view animName) {
    if (!animName[0])
        return nullptr;

    for (auto& anim: source->m_Animations) {
        if (vstd::iequals(animName, anim.animationname))
            return &anim;
    }
    return nullptr;
}

s_sourceanim_t *FindOrAddSourceAnim(s_source_t *source, const char *animName) {
    if (!animName[0])
        return nullptr;

    for (auto& anim: source->m_Animations) {
        if (vstd::iequals(animName, anim.animationname))
            return &anim;
    }

    s_sourceanim_t *anim = &source->m_Animations.emplace_back();
    anim->animationname = animName;
    return anim;
}

size_t LookupTexture(StudioMdlState &ctx, const std::string_view textureName, const bool relativePath) {
    StudioPath textureNoExt = textureName;
    vstd::paths::strip_extension(textureNoExt);
    const StudioPath textureBase = vstd::paths::file_base(textureName);

    const int flags = relativePath ? RELATIVE_TEXTURE_PATH_SPECIFIED : 0;
    for (size_t i = 0; i < ctx.materials.textures.size(); i++) {
        if (ctx.materials.textures[i].flags == flags) {
            if (vstd::iequals(textureNoExt, ctx.materials.textures[i].name))
                return i;
            continue;
        }

        // Comparing relative vs non-relative
        if (relativePath) {
            if (vstd::iequals(textureBase, ctx.materials.textures[i].name))
                return i;
            continue;
        }

        // Comparing non-relative vs relative
        if (vstd::iequals(textureNoExt, vstd::paths::file_base(ctx.materials.textures[i].name.view())))
            return i;
    }

    if (ctx.materials.textures.size() >= ctx.materials.textures.capacity()) {
        MdlError("Too many materials used, max {}\n", ctx.materials.textures.capacity());
    }

    auto& newTexture = ctx.materials.textures.emplace();
    newTexture.name = textureNoExt;
    newTexture.material = -1;
    newTexture.flags = flags;
    return ctx.materials.textures.size() - 1;
}

size_t UseTextureAsMaterial(StudioMdlState &ctx, size_t textureIndex) {
    if (ctx.materials.textures[textureIndex].material == -1) {
        if (ctx.options.dumpMaterials) {
            MdlPrint("material {} {} {}\n", textureIndex, ctx.materials.materials.size(), ctx.materials.textures[textureIndex].name);
        }
        ctx.materials.materials.emplace_back(textureIndex);
        ctx.materials.textures[textureIndex].material = vstd::as_signed_checked<int>(ctx.materials.materials.size() - 1);
    }

    return ctx.materials.textures[textureIndex].material;
}

int32_t MaterialToTexture(StudioMdlState &ctx, const size_t material) {
    for (int i = 0; i < ctx.materials.textures.size(); i++) {
        if (ctx.materials.textures[i].material == material) {
            return i;
        }
    }
    return -1;
}

//Wrong name for the use of it.
void scale_vertex(StudioMdlState &ctx, Vector &org) {
    org *= ctx.script.currentscale;
}

void Build_Reference(s_source_t *source, const std::string_view animName) {
    s_sourceanim_t *pReferenceAnim = FindSourceAnim(source, animName);
    for (int i = 0; i < source->numbones; i++) {
        matrix3x4_t m;
        if (pReferenceAnim) {
            AngleMatrix(pReferenceAnim->rawanim[0][i].rot, m);
            m[0][3] = pReferenceAnim->rawanim[0][i].pos[0];
            m[1][3] = pReferenceAnim->rawanim[0][i].pos[1];
            m[2][3] = pReferenceAnim->rawanim[0][i].pos[2];
        } else {
            SetIdentityMatrix(m);
        }

        const int parent = source->localBone[i].parent;
        if (parent == -1) {
            // scale the done pos.
            // calc rotational matrices
            MatrixCopy(m, source->boneToPose[i]);
        } else {
            // calc compound rotational matrices
            // FIXME : Hey, it's orthogical so inv(A) == transpose(A)
            Assert(parent < i);
            ConcatTransforms(source->boneToPose[parent], m, source->boneToPose[i]);
        }
    }
}

void InitSourceNodes(std::array<s_node_t, MAXSTUDIOSRCBONES> &pnodes) {
    for (auto &node: pnodes) {
        node.parent = -1;
    }
}

void SetSourceNode(std::array<s_node_t, MAXSTUDIOSRCBONES> &pnodes, const int index, const std::string_view name,
                   const int parent) {
    pnodes[index].name = name;
    pnodes[index].parent = parent;
}

int Grab_Nodes(StudioMdlState &ctx, std::array<s_node_t, MAXSTUDIOSRCBONES> &pnodes) {
    vstd::FunctionTimingRecorder _("SMD");
    int index;
    char name[1024];
    int parent;
    int numbones = 0;

    InitSourceNodes(pnodes);

    while (GetLineInput(ctx)) {
        if (sscanf(ctx.input.szLine, "%d \"%[^\"]\" %d", &index, name, &parent) == 3) {
            // check for duplicated bones
            /*
			if (strlen(pnodes[index].name) != 0)
			{
				MdlError( "bone \"%s\" exists more than once\n", name );
			}
			*/

            SetSourceNode(pnodes, index, name, parent);
            if (index > numbones) {
                numbones = index;
            }
        } else {
            return numbones + 1;
        }
    }
    MdlError("Unexpected EOF at line {}\n", ctx.input.iLinecount);
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

    if (!vstd::iequals(bonename, rhs.bonename) ||
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
                "  {}: ParentBone: {} Type: {} Flags: 0x{:08x} P: {:6.2f} {:6.2f} {:6.2f} R: {:6.2f} {:6.2f} {:6.2f}\n"
                "  {}: ParentBone: {} Type: {} Flags: 0x{:08x} P: {:6.2f} {:6.2f} {:6.2f} R: {:6.2f} {:6.2f} {:6.2f}\n",
                name, bonename, std::to_underlying(type), std::to_underlying(flags),
                iPos.x, iPos.y, iPos.z, RAD2DEG(iEuler.x), RAD2DEG(iEuler.y), RAD2DEG(iEuler.z),
                rhs.name, rhs.bonename, std::to_underlying(rhs.type), std::to_underlying(rhs.flags),
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
                " Target {}: W: {:6.2f} VO: {:6.2f} {:6.2f} {:6.2f} RO: {:6.2f} {:6.2f} {:6.2f}\n"
                " Target {}: W: {:6.2f} VO: {:6.2f} {:6.2f} {:6.2f} RO: {:6.2f} {:6.2f} {:6.2f}\n",
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
                " Target {}: VO: {:6.2f} {:6.2f} {:6.2f} RO: {:6.2f} {:6.2f} {:6.2f}\n"
                " Target {}: VO: {:6.2f} {:6.2f} {:6.2f} RO: {:6.2f} {:6.2f} {:6.2f}\n",
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

    if (m_targets.size() != rhs.m_targets.size())
        return false;

    for (size_t i = 0; i < m_targets.size(); ++i) {
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
    nVert += (nPreOffset + pOrigSource->meshes[nSrcMesh].vertexoffset);

    if (m_nOrigMap[nVert] == -1) {
        m_nOrigMap[nVert] = std::ssize(m_vertex);
        m_vertex.push_back(pOrigSource->vertex[nVert - nPreOffset]);

        if (m_meshes[nDstMesh].numvertices == 0) {
            m_meshes[nDstMesh].vertexoffset = m_nOrigMap[nVert];
        }
        m_meshes[nDstMesh].numvertices++;
    }

    return m_nOrigMap[nVert] - m_meshes[nDstMesh].vertexoffset;
}

void CClampedSource::AddAnimations(CompileArena &arena, const s_source_t *pOrigSource) {
    // invert the vertex mapping (maps the new index to the original index)
    const int numvertices = std::ssize(m_vertex);
    std::vector<int> nReverseMap(numvertices, -1);
    if (numvertices > 0) {
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
    const int nAnimations = std::ssize(pOrigSource->m_Animations);
    for (int nAnim = 0; nAnim < nAnimations; nAnim++) {
        const s_sourceanim_t &srcAnim = pOrigSource->m_Animations[nAnim];
        if (srcAnim.vanim_mapcount || srcAnim.vanim_map || srcAnim.vanim_flag) {
            Warning("Cannot split SMD model with vertex animations... discarding animation\n");
            Assert(0);
            continue;
        }

        s_sourceanim_t &dstAnim = m_Animations.emplace_back();

        // bone anims can be copied as-is
        memcpy(dstAnim.animationname, srcAnim.animationname, sizeof(dstAnim.animationname));
        dstAnim.numframes = srcAnim.numframes;
        dstAnim.startframe = srcAnim.startframe;
        dstAnim.endframe = srcAnim.endframe;
        dstAnim.rawanim.clear();
        dstAnim.rawanim.resize(srcAnim.rawanim.size());
        for (size_t i = 0; i < srcAnim.rawanim.size(); i++) {
            dstAnim.rawanim[i] = arena.CreateArray<s_bone_t>(pOrigSource->numbones);
            memcpy(dstAnim.rawanim[i], srcAnim.rawanim[i], pOrigSource->numbones * sizeof(s_bone_t));
        }

        // vertex animations need remapping
        dstAnim.newStyleVertexAnimations = srcAnim.newStyleVertexAnimations;

        if (!srcAnim.newStyleVertexAnimations)
            continue;

        for (int i = 0; i < MAXSTUDIOANIMFRAMES; i++) {
            // Count the number of verts which apply to this sub-model...
            for (int j = 0; j < srcAnim.numvanims[i]; j++) {
                int nMappedVert = m_nOrigMap[srcAnim.vanim[i][j].vertex];
                if (nMappedVert != -1)
                    dstAnim.numvanims[i]++;
            }
            // ...and just copy those verts:
            if (dstAnim.numvanims[i]) {
                dstAnim.vanim[i] = arena.CreateArray<s_vertanim_t>(dstAnim.numvanims[i]);
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

void CClampedSource::Copy(CompileArena &arena, s_source_t *pDstSource) {
    // copy over new meshes
    pDstSource->numfaces = std::ssize(m_face);
    pDstSource->face = arena.CreateArray<s_face_t>(pDstSource->numfaces);
    for (int i = 0; i < pDstSource->numfaces; i++) {
        pDstSource->face[i] = m_face[i];
    }

    pDstSource->numvertices = std::ssize(m_vertex);
    pDstSource->vertex = arena.CreateArray<s_vertexinfo_t>(pDstSource->numvertices);
    for (int i = 0; i < pDstSource->numvertices; i++) {
        pDstSource->vertex[i] = m_vertex[i];
    }

    // the piece's meshes, in the original source's mesh order
    pDstSource->meshes = m_meshes;

    // move over the new animations (the source's old animations leak, but we just dont care)
    pDstSource->m_Animations = std::move(m_Animations);
    m_Animations.clear();
}

void CClampedSource::CopyFlexKeys(StudioMdlState &ctx, const s_source_t *pOrigSource, s_source_t *newSource, int imodel) {
    // Only the flex keys are duplicated for the new piece (see below): flex controllers, controller remaps and flex
    // rules are global, they are added once for the original source and refer to flexdescs, which are shared.
    // A key is only needed for the pieces whose vertices it actually moves, so keys without any vertex animation in
    // this piece are culled. This keeps numSubmodels*numFlexKeys well below MAXSTUDIOFLEXKEYS.

    if (pOrigSource == newSource)
        return;

// TODO: need to change g_defaultflexkey so it works with this (set a flag on the default flexkey (error if the user sets two defaults), duplicate the flag with that flexkey in here, update RemapVertexAnimations to use the flag)

    newSource->m_FlexKeys.clear();
    for (const auto & origKey : pOrigSource->m_FlexKeys) {
        // AddAnimations() already remapped the vertex animations to this piece (Copy() moved them into pNewSource)
        const s_sourceanim_t *pAnim = FindSourceAnim(newSource, origKey.animationname.view());
        if (pAnim && pAnim->newStyleVertexAnimations && pAnim->numvanims[origKey.frame] == 0)
            continue;

        s_flexkey_t &key = newSource->m_FlexKeys.emplace_back(origKey);
        key.source = newSource;
    }

    // Register the flex keys of the new piece (g_flexkey, flexdescs, m_nKeyStartIndex) but NOT its controllers,
    // remaps or rules: AddBodyFlexData(pNewSource, imodel, false)
    //   NOTE: we dont call AddBodyAttachments, since we're not duplicating those
    AddBodyFlexData(ctx, newSource, imodel, false);
}

void CClampedSource::Init(int numvertices, const s_source_t *pMeshLayout) {
    m_meshes.clear();
    if (pMeshLayout) {
        for (const s_mesh_t &mesh: pMeshLayout->meshes) {
            s_mesh_t &empty = m_meshes.emplace_back();
            empty.material = mesh.material;
        }
    }
    if (std::ssize(m_nOrigMap) < numvertices)
        m_nOrigMap.resize(numvertices);
    for (int v = 0; v < numvertices; v++) {
        m_nOrigMap[v] = -1;
    }
}

//-----------------------------------------------------------------------------
// Splits a source with more vertices than the engine allows into pieces: the first keeps the source, every other
// becomes a model of its own bodypart
//-----------------------------------------------------------------------------
void ClampMaxVerticesPerModel(StudioMdlState &ctx, s_source_t* pOrigSource) {
    // check for overage
    if (pOrigSource->numvertices < ctx.options.maxVertexLimit)
        return;

    MdlWarning("model has too many verts, cutting into multiple models\n");

    std::vector<CClampedSource> newSource;

    int ns = 0;
    newSource.emplace_back().Init(pOrigSource->numvertices, pOrigSource);

    // Flex optimization: only meshes with flexes count against the engine's flex cache, so faces a flex can move are
    // put into the first pieces and the static faces into pieces of their own, which then carry no flex data.
    // Without it there is one pass over all faces, in source order.
    std::vector<bool> moving;
    bool bSortByFlex = FlexOptimizeIsActive(ctx) && !pOrigSource->m_FlexKeys.empty();
    if (bSortByFlex)
        moving = FlexOptimizeMovingVertices(ctx, pOrigSource);

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
        if (pass == 1 && !newSource[ns].m_face.empty()) {
            // static faces never share a piece with moving ones; this also keeps the faces of every mesh of a piece
            // contiguous (each pass walks the meshes in order)
            ns = std::ssize(newSource);
            newSource.emplace_back().Init(pOrigSource->numvertices, pOrigSource);
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
                if ((std::ssize(newSource[ns].m_vertex) + nVertsInFace) > ctx.options.maxVertexClamp) {
                    // go to the next model
                    ns = std::ssize(newSource);
                    newSource.emplace_back().Init(pOrigSource->numvertices, pOrigSource);
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
                    dstMesh.faceoffset = std::ssize(newSource[ns].m_face);
                }
                newSource[ns].m_face.push_back(face);
                dstMesh.numfaces++;
            }
        }
    }

    // Split animations into the new sub-models
    for (size_t n = 0; n < newSource.size(); n++) {
        newSource[n].AddAnimations(ctx.arena, pOrigSource);
    }

    // Name of what was split, so the pieces can be told apart: the model using the source, or the source file
    char szOrigName[MAXSTUDIONAME];
    Q_FileBase(pOrigSource->filename.c_str(), szOrigName, sizeof(szOrigName));
    for (int i = 0; i < std::ssize(ctx.body.model); i++) {
        if (ctx.body.model[i]->source == pOrigSource && ctx.body.model[i]->name[0]) {
            Q_FileBase(ctx.body.model[i]->name, szOrigName, sizeof(szOrigName));
            break;
        }
    }

    // copy over new meshes and animations back into initial source (the old arrays stay in the arena)
    newSource[0].Copy(ctx.arena, pOrigSource);

    for (size_t n = 1; n < newSource.size(); n++) {
        // create a new internal "source"
        s_source_t* pSource = ctx.arena.Create<s_source_t>();
        ctx.anim.sources.emplace_back(pSource);

        // copy all the members, in order
        pSource->filename = pOrigSource->filename;

        // copy over the faces/vertices/animations
        newSource[n].Copy(ctx.arena, pSource);

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
        s_model_t* pModel = ctx.arena.Create<s_model_t>();
        pModel->source = pSource;
        pModel->name.format("{}_cmodel{}", szOrigName, n);
        pModel->filename = std::string(szOrigName) + "_cmesh" + std::to_string(n);
        const int imodel = ctx.body.AddModel(pModel);

        // make it a new bodypart
        s_bodypart_t &bodypart = ctx.body.AddBodyPart();
        bodypart.name.format("{}_clamped{}", szOrigName, n);
        bodypart.pmodel.push_back(pModel);

        // finally, copy flex keys
        newSource[n].CopyFlexKeys(ctx, pOrigSource, pSource, imodel);

        // NOTE: we leave attachments on the first sub-model, we don't want to duplicate those
    }
}

//-----------------------------------------------------------------------------
// Post-processes a source (used when loading preprocessed files)
//-----------------------------------------------------------------------------
void PostProcessSource(StudioMdlState &ctx, s_source_t *pSource, int imodel) {
    if (pSource) {
        AddBodyFlexData(ctx, pSource, imodel);
        AddBodyAttachments(ctx, pSource);
        AddBodyFlexRules(ctx, pSource);
    }
}

bool SetSourceAnimTime(StudioMdlState &ctx, const s_source_t *pSource, s_sourceanim_t *pAnim, const int time, int &relativeFrame) {
    int t = time;
    if (pAnim->startframe == -1) {
        pAnim->startframe = t;
    }
    if (t < pAnim->startframe) {
        return false;
    }
    if (t > pAnim->endframe) {
        pAnim->endframe = t;
    }
    t -= pAnim->startframe;
    relativeFrame = t;

    if (t >= std::ssize(pAnim->rawanim)) {
        pAnim->rawanim.resize(t + 1);
    }

    if (pAnim->rawanim[t] != nullptr) {
        return true;
    }

    pAnim->rawanim[t] = ctx.arena.CreateArray<s_bone_t>(pSource->numbones);

    // duplicate previous frames keys
    if (t > 0 && pAnim->rawanim[t - 1]) {
        for (int j = 0; j < pSource->numbones; j++) {
            VectorCopy(pAnim->rawanim[t - 1][j].pos, pAnim->rawanim[t][j].pos);
            VectorCopy(pAnim->rawanim[t - 1][j].rot, pAnim->rawanim[t][j].rot);
        }
    }
    return true;
}

void SetSourceAnimBone(StudioMdlState &ctx, s_sourceanim_t *pAnim, const int relativeFrame, const int bone, Vector pos,
                       const RadianEuler &rot) {
    scale_vertex(ctx, pos);
    VectorCopy(pos, pAnim->rawanim[relativeFrame][bone].pos);
    VectorCopy(rot, pAnim->rawanim[relativeFrame][bone].rot);
}

int FinishSourceAnim(const s_source_t *pSource, s_sourceanim_t *pAnim, const std::string_view animName) {
    pAnim->numframes = pAnim->endframe - pAnim->startframe + 1;

    for (int t = 0; t < pAnim->numframes; t++) {
        if (pAnim->rawanim[t] == nullptr) {
            return t + pAnim->startframe;
        }
    }

    Build_Reference(const_cast<s_source_t *>(pSource), animName);
    return -1;
}

void Grab_Animation(StudioMdlState &ctx, s_source_t *pSource, const char *pAnimName) {
    vstd::FunctionTimingRecorder _("SMD");
    Vector pos;
    RadianEuler rot;
    char cmd[1024];
    int index;
    int t = -99999999;

    s_sourceanim_t *pAnim = FindOrAddSourceAnim(pSource, pAnimName);
    pAnim->startframe = -1;

    while (GetLineInput(ctx)) {
        if (sscanf(ctx.input.szLine.c_str(), "%d %f %f %f %f %f %f", &index, &pos[0], &pos[1], &pos[2], &rot[0], &rot[1], &rot[2]) ==
            7) {
            if (pAnim->startframe < 0) {
                MdlError("Missing frame start({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
            }

            SetSourceAnimBone(ctx, pAnim, t, index, pos, rot);

            clip_rotations(rot); // !!!
            continue;
        }

        if (sscanf(ctx.input.szLine.c_str(), "%1023s %d", cmd, &index) == 0) {
            MdlError("MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
            continue;
        }

        if (!Q_stricmp(cmd, "time")) {
            if (!SetSourceAnimTime(ctx, pSource, pAnim, index, t)) {
                MdlError("Frame MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
            }
            continue;
        }

        if (!Q_stricmp(cmd, "end")) {
            const int missing = FinishSourceAnim(pSource, pAnim, pAnimName);
            if (missing >= 0) {
                MdlError("{} is missing frame {}\n", pSource->filename, missing);
            }
            return;
        }

        MdlError("MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
    }

    MdlError("unexpected EOF: {}\n", pSource->filename);
}

void FlipFacing(s_source_t *pSrc) {
    unsigned short tmp;

    int i, j;
    for (i = 0; i < pSrc->meshes.size(); i++) {
        s_mesh_t *pMesh = &pSrc->meshes[i];
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

void AddSrcToSrc(StudioMdlState &ctx, s_source_t *pOrigSource, s_source_t *pAppendSource, matrix3x4_t matOffset) {
    // steps are:
    // only A exists
    // make a new source C
    // append A to C
    // append B to C
    // replace A with C
    vstd::FunctionTimingRecorder _("API");
    CClampedSource newSource;

    // everything goes into one mesh, with material 0
    newSource.Init(pOrigSource->numvertices + pAppendSource->numvertices, nullptr);
    newSource.m_meshes.emplace_back().material = 0;

    for (int nSrcMeshIndex = 0; nSrcMeshIndex < pOrigSource->meshes.size(); nSrcMeshIndex++) {
        s_mesh_t *pOrigMesh = &pOrigSource->meshes[nSrcMeshIndex];

        if (pOrigMesh->numvertices == 0)
            continue;

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

            newSource.m_face.push_back(face);
            newSource.m_meshes[0].numfaces++;
        }
    }

    // just use src anim - we don't really care because it's for static props
    newSource.AddAnimations(ctx.arena, pOrigSource);

    // apply offset to appended vertices
    ApplyOffsetToSrcVerts(pAppendSource, matOffset); // no-op if matOffset is identity

    // append the vertices to the new source

    for (int nCurMesh = 0; nCurMesh < pAppendSource->meshes.size(); nCurMesh++) {
        s_mesh_t *pAppendMesh = &pAppendSource->meshes[nCurMesh];

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

                newSource.m_face.push_back(face);
                newSource.m_meshes[0].numfaces++;
            }
        }
    }

    // the old arrays stay in the arena until the compile state goes
    newSource.Copy(ctx.arena, pOrigSource);
}

void AddSrcToSrc(StudioMdlState &ctx, s_source_t *pOrigSource, s_source_t *pAppendSource) {
    matrix3x4_t matNoop;
    matNoop.SetToIdentity();
    AddSrcToSrc(ctx, pOrigSource, pAppendSource, matNoop);
}

//-----------------------------------------------------------------------------
// Loads an animation/model source
//-----------------------------------------------------------------------------
s_source_t *Load_Source(StudioMdlState &ctx, const std::string_view name, const char *ext, bool reverse, bool isActiveModel, bool bUseCache) {
    vstd::FunctionTimingRecorder _("Parse");
    if (ctx.anim.sources.size() >= ctx.anim.sources.capacity()) {
        MdlError("Load_Source( {} ) - overflowed g_studiomdl_global_state.numsources.", name);
    }

    Assert(name);
    int result = false;

    // UTF-8 name relative to the current $cd/$pushd directory; the extension (or ext) picks the loader
    std::string tempName(name);
    std::string xext(vstd::paths::extension(tempName));
    vstd::paths::fix_slashes(tempName);
    if (xext.empty()) {
        xext = ext;
    } else {
        vstd::paths::strip_extension(tempName);
    }

    s_source_t *pSource = nullptr;

    if (bUseCache) {
        pSource = FindCachedSource(ctx, tempName.c_str(), xext.c_str());
        if (pSource) {
            if (isActiveModel) {
                pSource->isActiveModel = true;
            }
            return pSource;
        }
    }

    // NOTE: The load proc can potentially add other sources (for the MPP format)
    // So we have to deal with setting everything up in this source prior to
    // calling the load func, and we cannot reference ctx.anim.sources.back() anywhere below
    pSource = ctx.arena.Create<s_source_t>();
    ctx.anim.sources.emplace_back(pSource);
    if (isActiveModel) {
        pSource->isActiveModel = true;
    }

    // copy over default settings of when the model was loaded
    // (since there's no actual animation for some of the systems)
    VectorCopy(ctx.script.defaultadjust, pSource->adjust);
    pSource->scale = 1.0f;
    pSource->rotation = ctx.script.defaultrotation;

    const std::array supported_formats = {
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
        if ((!result && xext.empty()) || xext == supported_formats[fmt_id].first) {
            ctx.input.szFilename = ctx.paths.cddir.back() + tempName + "." + supported_formats[fmt_id].first;
            pSource->filename = ctx.input.szFilename;
            result = (supported_formats[fmt_id].second)(ctx, pSource);
        }
    }

    if (!ctx.options.createMakefile && !result) {
        if (xext.empty()) {
            MdlError("could not load file '{}{}'\n", ctx.paths.cddir.back(), tempName);
        } else {
            MdlError("could not load file '{}{}.{}'\n", ctx.paths.cddir.back(), tempName, xext);
        }
    }

    // (with -makefile a missing source isn't fatal: it can be the output of another makefile rule)
    if (result && pSource->numbones == 0) {
        MdlError("missing all bones in file '{}'\n", pSource->filename);
    }

    if (reverse) {
        FlipFacing(pSource);
    }

    return pSource;
}

int Add_Flexdesc(StudioMdlState &ctx, const char *name) {
    int flexdesc;
    for (flexdesc = 0; flexdesc < ctx.flex.flexdescs.size(); flexdesc++) {
        if (vstd::iequals(name, ctx.flex.flexdescs[flexdesc].FACS)) {
            break;
        }
    }

    if (flexdesc >= ctx.flex.flexdescs.capacity()) {
        MdlError("Too many flex types, max {}\n", MAXSTUDIOFLEXDESC);
    }

    if (flexdesc == ctx.flex.flexdescs.size()) {
        ctx.flex.flexdescs.emplace_back(name);
    }
    return flexdesc;
}

//-----------------------------------------------------------------------------
// Adds combination data to the source
//-----------------------------------------------------------------------------
int FindSourceFlexKey(s_source_t *pSource, const char *pName) {
    const int nCount = std::ssize(pSource->m_FlexKeys);
    for (int i = 0; i < nCount; ++i) {
        if (vstd::iequals(pSource->m_FlexKeys[i].animationname, pName))
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

    s_flexkey_t &key = pSource->m_FlexKeys.emplace_back();

    key.target0 = 0.0f;
    key.target1 = 1.0f;
    key.target2 = 10.0f;
    key.target3 = 11.0f;
    key.decay = 1.0f;
    key.source = pSource;

    Q_strncpy(key.animationname, pFlexKeyName, sizeof(key.animationname));
    key.flexpair = pComboOp->IsDeltaStateStereo(pFlexKeyName);    // Signal used by AddBodyFlexData
}

static void FindOrAddFlexController(
        StudioMdlState &ctx,
        const char *pszFlexControllerName,
        const char *pszFlexControllerType = "default",
        float flMin = 0.0f,
        float flMax = 1.0f) {
    for (int i = 0; i < ctx.flex.flexcontrollers.size(); ++i) {
        if (!V_strcmp(ctx.flex.flexcontrollers[i].name, pszFlexControllerName)) {
            if (V_strcmp(ctx.flex.flexcontrollers[i].type, pszFlexControllerType) ||
                ctx.flex.flexcontrollers[i].min != flMin ||
                ctx.flex.flexcontrollers[i].max != flMax) {
                MdlWarning("Flex Controller {} Defined Twice With Different Params: {}, {:f} {:f} vs {}, {:f} {:f}\n",
                           pszFlexControllerName,
                           pszFlexControllerType, flMin, flMax,
                           ctx.flex.flexcontrollers[i].type, ctx.flex.flexcontrollers[i].min, ctx.flex.flexcontrollers[i].max);
            }

            return;
        }
    }
    ctx.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
        .name = pszFlexControllerName,
        .type = pszFlexControllerType,
        .min = flMin,
        .max = flMax,
    });
}

//-----------------------------------------------------------------------------
// Adds combination data to the source
//-----------------------------------------------------------------------------
void AddCombination(StudioMdlState &ctx, s_source_t *pSource, CDmeCombinationOperator *pCombination) {
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

                FindOrAddFlexController(ctx, pControlElement->GetName(), "default", flMin, flMax);
            }
        }

        // each rule is parsed by Option_Flexrule from its own buffer, independent of the QC being parsed
        std::string sTmpBuf{};
        ScriptTokenizer ruleTokenizer(ctx.paths.qdir, &ctx.paths.content);
        InstallMdlErrorHandler(ruleTokenizer);

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
                    MdlWarning("Unknown DmeDeltaRule: {} Of Type: {}\n", pDmeFlexRule->GetName(),
                               pDmeFlexRule->GetTypeString());
                    continue;
                }

                ruleTokenizer.LoadMemory(sTmpBuf);

                Add_Flexdesc(ctx, pDmeFlexRule->GetName());

                if (bFlexRule) {
                    Option_Flexrule(ctx, ruleTokenizer, nullptr, pDmeFlexRule->GetName());
                }

                ruleTokenizer.Close();
            }
        }
        return;
    }

    // Define the remapped controls
    int nControlCount = pCombination->GetRawControlCount();
    for (int i = 0; i < nControlCount; ++i) {
        s_combinationcontrol_t &control = pSource->m_CombinationControls.emplace_back();
        control.name = pCombination->GetRawControlName(i);
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

            s_combinationrule_t &rule = pSource->m_CombinationRules.emplace_back();
            rule.m_nFlex = nFlex;
            const std::vector<int> &controls = pCombination->GetOperationControls(i, j);
            rule.m_Combination.assign(controls.begin(), controls.end());
            int nDominatorCount = pCombination->GetOperationDominatorCount(i, j);
            for (int l = 0; l < nDominatorCount; ++l) {
                const std::vector<int> &dominator = pCombination->GetOperationDominator(i, j, l);
                rule.m_Dominators.emplace_back(dominator.begin(), dominator.end());
            }
        }
    }

    // Define the remapping controls
    nControlCount = pCombination->GetControlCount();
    for (int i = 0; i < nControlCount; ++i) {
        s_flexcontrollerremap_t &remap = pSource->m_FlexControllerRemaps.emplace_back();
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
            remap.m_RemapType = Eyelid;

            // Save the eyes_updown flex for later
            const char *pEyesUpDownFlexName = pCombination->GetEyesUpDownFlexName(i);
            remap.m_EyesUpDownFlexName = pEyesUpDownFlexName ? pEyesUpDownFlexName : "eyes_updown";
        } else {
            switch (nRemapCount) {
                case 0:
                case 1:
                    remap.m_RemapType = Passthru;
                    break;
                case 2:
                    remap.m_RemapType = _2way;
                    break;
                default:
                    remap.m_RemapType = Nway;
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
void AddSurfaceProp(StudioMdlState &ctx, const char *pBoneName, const char *pSurfaceProperty) {
    // Search for the name in our list
    int i;
    for (i = std::ssize(ctx.materials.JointSurfaceProp); --i >= 0;) {
        if (vstd::iequals(ctx.materials.JointSurfaceProp[i].m_pJointName, pBoneName))
            break;
    }

    // Add new entry if we haven't seen this name before
    if (i < 0) {
        ctx.materials.JointSurfaceProp.emplace_back();
        i = std::ssize(ctx.materials.JointSurfaceProp) - 1;
        Q_strncpy(ctx.materials.JointSurfaceProp[i].m_pJointName, pBoneName, sizeof(ctx.materials.JointSurfaceProp[i].m_pJointName));
    }

    Q_strncpy(ctx.materials.JointSurfaceProp[i].m_pSurfaceProp, pSurfaceProperty, sizeof(ctx.materials.JointSurfaceProp[i].m_pSurfaceProp));
}

//-----------------------------------------------------------------------------
// Returns the default surface prop name
//-----------------------------------------------------------------------------
char *GetDefaultSurfaceProp(StudioMdlState &ctx) {
    return ctx.materials.pDefaultSurfaceProp;
}

//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
char *FindSurfaceProp(StudioMdlState &ctx, const char *pJointName) {
    for (int i = std::ssize(ctx.materials.JointSurfaceProp); --i >= 0;) {
        if (vstd::iequals(ctx.materials.JointSurfaceProp[i].m_pJointName, pJointName))
            return ctx.materials.JointSurfaceProp[i].m_pSurfaceProp;
    }
    return nullptr;
}

//-----------------------------------------------------------------------------
// Returns surface property for a given joint
//-----------------------------------------------------------------------------
char *GetSurfaceProp(StudioMdlState &ctx, const char *pJointName) {
    while (pJointName) {
        // First try to find this joint
        char *pSurfaceProp = FindSurfaceProp(ctx, pJointName);
        if (pSurfaceProp)
            return pSurfaceProp;

        // If we can't find the joint, then find it's parent...
        if (!std::ssize(ctx.bones.bonetable))
            return ctx.materials.pDefaultSurfaceProp;

        int i = findGlobalBone(ctx, pJointName);

        if ((i >= 0) && (ctx.bones.bonetable[i].parent >= 0)) {
            pJointName = ctx.bones.bonetable[ctx.bones.bonetable[i].parent].name;
        } else {
            pJointName = nullptr;
        }
    }

    // No match, return the default one
    return ctx.materials.pDefaultSurfaceProp;
}


//-----------------------------------------------------------------------------
// Returns the default contents
//-----------------------------------------------------------------------------
ContentType GetDefaultContents(StudioMdlState &ctx) {
    return ctx.core.defaultContents;
}

//-----------------------------------------------------------------------------
// Returns contents for a given joint
//-----------------------------------------------------------------------------
static ContentType FindContents(StudioMdlState &ctx, const char *pJointName) {
    for (int i = std::ssize(ctx.core.jointContents); --i >= 0;) {
        if (vstd::iequals(ctx.core.jointContents[i].m_pJointName, pJointName)) {
            return ctx.core.jointContents[i].m_nContents;
        }
    }
    return ContentType::MISSING;
}

//-----------------------------------------------------------------------------
// Returns contents for a given joint
//-----------------------------------------------------------------------------
ContentType GetContents(StudioMdlState &ctx, const char *pJointName) {
    while (pJointName) {
        // First try to find this joint
        ContentType nContents = FindContents(ctx, pJointName);
        if (nContents != ContentType::MISSING)
            return nContents;

        // If we can't find the joint, then find it's parent...
        if (!std::ssize(ctx.bones.bonetable))
            return ctx.core.defaultContents;

        int i = findGlobalBone(ctx, pJointName);

        if ((i >= 0) && (ctx.bones.bonetable[i].parent >= 0)) {
            pJointName = ctx.bones.bonetable[ctx.bones.bonetable[i].parent].name;
        } else {
            pJointName = nullptr;
        }
    }

    // No match, return the default one
    return ctx.core.defaultContents;
}

int LookupAttachment(StudioMdlState &ctx, const char *name) {
    int i;
    for (i = 0; i < ctx.bones.attachments.size(); i++) {
        if (vstd::iequals(ctx.bones.attachments[i].name, name)) {
            return i;
        }
    }
    return -1;
}

int SortAndBalanceBones(uint32_t count, const uint32_t maxCount, std::span<int> bones, std::span<float> weights) {
    int i;

    // collapse duplicate bone weights
    for (i = 0; i < count - 1; i++) {
        int j;
        for (j = i + 1; j < count; j++) {
            if (bones[i] == bones[j]) {
                weights[i] += weights[j];
                weights[j] = 0.0;
            }
        }
    }

    // do sleazy bubble sort
    int bShouldSort;
    do {
        bShouldSort = false;
        for (i = 0; i < count - 1; i++) {
            if (weights[i + 1] > weights[i]) {
                int j = bones[i + 1];
                bones[i + 1] = bones[i];
                bones[i] = j;
                float w = weights[i + 1];
                weights[i + 1] = weights[i];
                weights[i] = w;
                bShouldSort = true;
            }
        }
    } while (bShouldSort);

    // Small influences are kept on purpose: Valve's studiomdl dropped weights below 0.05 (mdlcompile: 0.0001) here.
    // Zero weights still go (fixed-size joint lists pad with them, merged duplicates leave them): an influence of 0
    // in numbones marks its bone as used by vertices, so the bone survives in the model for nothing.
    while (count > 1 && weights[count - 1] <= 0.0f) {
        count--;
    }

    // clip to the top iMaxCount bones
    if (count > maxCount) {
        count = maxCount;
    }

    float t = 0;
    for (i = 0; i < count; i++) {
        t += weights[i];
    }

    if (t <= 0.0) {
        // missing weights?, go ahead and evenly share?
        // FIXME: shouldn't this error out?
        t = 1.0f / count;

        for (i = 0; i < count; i++) {
            weights[i] = t;
        }
    }
    else {
        // scale to sum to 1.0
        t = 1.0 / t;

        for (i = 0; i < count; i++) {
            weights[i] = weights[i] * t;
        }
    }

    return count;
}

void MakeSourceBoneWeights(int count, const std::span<const int> bones, const std::span<const float> weights,
                           s_sourceboneweight_t &out) {
    out = {};
    count = std::min({count, static_cast<int>(bones.size()), static_cast<int>(weights.size())});
    if (count <= static_cast<int>(MAXSTUDIOBONEWEIGHTS))
        return;
    std::vector<int> sortedBones(bones.begin(), bones.begin() + count);
    std::vector<float> sortedWeights(weights.begin(), weights.begin() + count);
    const int kept = SortAndBalanceBones(count, MAXSTUDIOSOURCEBONEWEIGHTS, sortedBones, sortedWeights);
    if (kept <= static_cast<int>(MAXSTUDIOBONEWEIGHTS))
        return; // duplicates or zero weights: what is left fits the model as it is
    out.numbones = kept;
    for (int i = 0; i < kept; ++i) {
        out.bone[i] = sortedBones[i];
        out.weight[i] = sortedWeights[i];
    }
}

void Grab_Vertexanimation(StudioMdlState &ctx, s_source_t *psource, const char *pAnimName) {
    vstd::FunctionTimingRecorder _("SMD");
    char cmd[1024];
    int index;
    Vector pos;
    Vector normal;
    int t = -1;
    int count = 0;
    std::vector<s_vertanim_t> tmpvanim; // the current frame's vertex animations, [0, count)

    s_sourceanim_t *pAnim = FindSourceAnim(psource, pAnimName);
    if (!pAnim) {
        MdlError("Unknown animation {}({}) : {}\n", pAnimName, ctx.input.iLinecount, ctx.input.szLine);
    }

    while (GetLineInput(ctx)) {
        if (sscanf(ctx.input.szLine, "%d %f %f %f %f %f %f", &index, &pos[0], &pos[1], &pos[2], &normal[0], &normal[1],
                   &normal[2]) == 7) {
            if (pAnim->startframe < 0) {
                MdlError("Missing frame start({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
            }

            if (t < 0) {
                MdlError("VTA Frame Sync ({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
            }

            if (count >= std::ssize(tmpvanim)) {
                tmpvanim.resize(count + 1);
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

                pAnim->vanim[t] = ctx.arena.CreateArray<s_vertanim_t>(count);

                memcpy(pAnim->vanim[t], tmpvanim.data(), count * sizeof(s_vertanim_t));
            } else if (t > 0) {
                pAnim->numvanims[t] = 0;
            }

            // next command
            if (sscanf(ctx.input.szLine, "%1023s %d", cmd, &index)) {
                if (stricmp(cmd, "time") == 0) {
                    t = index;
                    count = 0;

                    if (t < pAnim->startframe) {
                        MdlError("Frame MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
                    }
                    if (t > pAnim->endframe) {
                        MdlError("Frame MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
                    }

                    t -= pAnim->startframe;
                } else if (!Q_stricmp(cmd, "end")) {
                    pAnim->numframes = pAnim->endframe - pAnim->startframe + 1;
                    return;
                } else {
                    MdlError("MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
                }
            } else {
                MdlError("MdlError({}) : {}", ctx.input.iLinecount, ctx.input.szLine);
            }
        }
    }
    MdlError("unexpected EOF: {}\n", psource->filename);
}

// Where a source file can be: below every content base path when src is below one of them, else src itself
static std::vector<std::string> SourceFileCandidates(StudioMdlState &ctx, const std::string &fileName) {
    if (auto result = CmdLib_HasBasePath(ctx.paths.content, fileName)) {
        auto [_, remainder] = *result;
        std::vector<std::string> candidates;
        for (const std::string &basePath: ctx.paths.content.basePaths) {
            candidates.push_back(vstd::paths::join(basePath, remainder));
        }
        return candidates;
    }
    return {fileName};
}

// -makefile: the dependency to record for a source when none of its candidates exists: the first one below a content
// base path (it may be generated by another rule), otherwise none
static void AddMissingSourceDependency(StudioMdlState &ctx, const std::vector<std::string> &candidates,
                                       const std::string &fileName) {
    if (ctx.options.createMakefile && !candidates.empty() && CmdLib_HasBasePath(ctx.paths.content, fileName)) {
        CreateMakefile_AddDependency(ctx, candidates.front().c_str());
    }
}

bool GetGlobalFilePath(StudioMdlState &ctx, std::string_view src, std::string &fullPath) {
    const std::string fileName = ExpandPath(ctx.paths.qdir, src);
    const std::vector<std::string> candidates = SourceFileCandidates(ctx, fileName);
    for (const std::string &candidate: candidates) {
        FileStat_t stat{};
        if (FS_Stat(candidate.c_str(), stat) && !stat.bDirectory && stat.nSize > 0) {
            CreateMakefile_AddDependency(ctx, candidate.c_str()); // -makefile only
            fullPath = candidate;
            return true;
        }
    }
    AddMissingSourceDependency(ctx, candidates, fileName);
    return false;
}

// Opens the source as ctx.input.inputFile. With -makefile the file is also recorded as a dependency, and still
// loaded: parsing the rest of the QC needs its contents.
bool OpenGlobalFile(StudioMdlState &ctx, std::string_view src) {
    std::string filename = ExpandPath(ctx.paths.qdir, src);

    // if the file doesn't exist it might be a relative content dir path
    if (ctx.paths.contentRootRelative && !g_pFullFileSystem->FileExists(filename.c_str())) {
        char contentPath[4096] = {};
        g_pFullFileSystem->RelativePathToFullPath(std::string(src).c_str(), IBaseFileSystem::PathID::Content, contentPath, sizeof(contentPath));
        filename = contentPath;
    }

    const std::vector<std::string> candidates = SourceFileCandidates(ctx, filename);
    for (const std::string &candidate: candidates) {
        FileStat_t stat{};
        if (!FS_Stat(candidate.c_str(), stat)) {
            continue;
        }
        CreateMakefile_AddDependency(ctx, candidate.c_str()); // -makefile only

        ctx.input.inputFile.reset(CStdFile::Open(candidate.c_str(), "r", nullptr));
        if (!ctx.input.inputFile) {
            MdlWarning("reader: could not open file '{}'\n", std::string(src));
            return false;
        }
        return true;
    }
    AddMissingSourceDependency(ctx, candidates, filename);
    return false;
}

// -makefile: appends the make rule of this model (its sources as dependencies) to makefile.tmp
void CreateMakefile_OutputMakefile(StudioMdlState &ctx) {
    if (!ctx.paths.hasModelName) {
        MdlError("Can't write makefile since a target mdl hasn't been specified!");
    }
    FILE* fp = fopen("makefile.tmp", "a");
    if (!fp) {
        MdlError("can't open makefile.tmp!\n");
    }
    const std::string mdlname = OutputModelPath(ctx, ".mdl");

    std::print(fp, "{}:", mdlname);
    for (const std::string &dependency: ctx.paths.CreateMakefileDependencies) {
        std::print(fp, " {}", dependency);
    }
    std::print(fp, "\n");
    std::print(fp, "\tmkdir \"{}\"\n", std::string(vstd::paths::parent(mdlname)));
    std::print(fp, "\t{} -quiet {}\n\n", ctx.paths.CreateMakefileCommand, ctx.paths.fullpath);
    fclose(fp);
}

int Load_VTA(StudioMdlState &ctx, s_source_t *psource) {
    char cmd[1024];
    int option;

    if (!OpenGlobalFile(ctx, psource->filename))
        return 0;

    if (!ctx.options.quiet)
        MdlPrint("VTA MODEL {}\n", psource->filename);

    ctx.input.iLinecount = 0;
    while (GetLineInput(ctx)) {
        ctx.input.iLinecount++;

        const int numRead = sscanf(ctx.input.szLine, "%s %d", cmd, &option);

        // No Command Was Parsed, Blank Line Usually
        if ((numRead == EOF) || (numRead == 0))
            continue;

        if (stricmp(cmd, "version") == 0) {
            if (option != 1) {
                MdlError("bad version\n");
            }
        } else if (stricmp(cmd, "nodes") == 0) {
            psource->numbones = Grab_Nodes(ctx, psource->localBone);
        } else if (stricmp(cmd, "skeleton") == 0) {
            Grab_Animation(ctx, psource, "VertexAnimation");
        } else if (stricmp(cmd, "vertexanimation") == 0) {
            Grab_Vertexanimation(ctx, psource, "VertexAnimation");
        } else {
            MdlWarning("unknown studio command \"{}\" in vta file: \"{}\" line: {}\n", cmd, psource->filename,
                       ctx.input.iLinecount - 1);
        }
    }
    ctx.input.inputFile.reset();

    return 1;
}
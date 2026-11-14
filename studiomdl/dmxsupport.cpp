//===== Copyright � 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: Loads mesh data from dmx files
//
// $NoKeywords: $
//
//===========================================================================//


// Valve includes
#include "dmxsupport.hpp"

#include "movieobjects/dmeattributereference.h"
#include "movieobjects/dmeconnectionoperator.h"
#include "movieobjects/dmemodel.h"
#include "movieobjects/dmemesh.h"
#include "movieobjects/dmefaceset.h"
#include "movieobjects/dmematerial.h"
#include "movieobjects/dmeclip.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmeattachment.h"
#include "movieobjects/dmeanimationlist.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmerigconstraintoperators.h"
#include "mdlobjects/dmeeyeball.h"
#include "mdlobjects/dmeeyelid.h"
#include "mdlobjects/dmejigglebone.h"
#include "mdlobjects/dmemouth.h"
#include "movieobjects/dmerig.h"
#include "movieobjects/dmerighandle.h"
#include "datamodel/dmelementfactoryhelper.h"

// DmeRig / DmeRigHandle dags appear in rigged DMX hierarchies and as constraint targets; nothing casts to
// them, so their factories must be pulled out of the static library explicitly to keep them typed dags
USING_ELEMENT_FACTORY( DmeRig );
USING_ELEMENT_FACTORY( DmeRigHandle );


// Local includes
#include "studiomdl_commands.h"
#include "studiomdl/studiomdl.h"
#include "tier1/fmtstr.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl/weight_fit.hpp"

#include <optional>

#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"


struct MeshInfo;
//-----------------------------------------------------------------------------
// The current model being loaded...
//-----------------------------------------------------------------------------
s_model_t *g_pCurrentModel = nullptr;


void UnifyIndices(s_source_t *psource);


//-----------------------------------------------------------------------------
// Mapping of bone transforms
//-----------------------------------------------------------------------------
struct BoneTransformMap_t {
    // Number of bones
    int m_nBoneCount;

    // The order in which transforms appear in this list specifies their bone indices
    CDmeTransform *m_ppTransforms[MAXSTUDIOSRCBONES];

    // m_pnDmeModelToMdl[bone index in DmeModel] == bone index in studiomdl
    int m_pnDmeModelToMdl[MAXSTUDIOSRCBONES];

    // m_pnMdlToDmeModel[bone index in studiomdl] == bone index in DmeModel
    int m_pnMdlToDmeModel[MAXSTUDIOSRCBONES];
};


//-----------------------------------------------------------------------------
// Index into an s_node_t array for the default root node
//-----------------------------------------------------------------------------
static int s_nDefaultRootNode;


//-----------------------------------------------------------------------------
// Balance/speed data
//-----------------------------------------------------------------------------
static CUtlVector<float> s_Balance;
static CUtlVector<float> s_Speed;


//-----------------------------------------------------------------------------
// List of unique vertices
//-----------------------------------------------------------------------------
struct VertIndices_t {
    int v;
    int n;
    int t[MAXSTUDIOTEXCOORDS];
    int balance;
    int speed;
};

static CUtlVector<VertIndices_t> s_UniqueVertices; // A list of the unique vertices in the mesh

// Given the non-unique vertex index, return the unique vertex index
// The indices are absolute indices into s_UniqueVertices
// But as both arrays contain information for all meshes in the DMX
// The proper offset for the desired mesh must be added to the lookup
// into the map but the value returned has the offset already built in
static CUtlVector<int> s_UniqueVerticesMap;


//-----------------------------------------------------------------------------
// Delta state intermediate data [used for positions, normals, etc.]
//-----------------------------------------------------------------------------
struct DeltaIndex_t {
    DeltaIndex_t() : m_nPositionIndex(-1), m_nNormalIndex(-1), m_nNextDelta(-1), m_nWrinkleIndex(-1),
                     m_bInList(false) {
    }

    int m_nPositionIndex; // Index into DeltaState_t::m_PositionDeltas
    int m_nNormalIndex; // Index into DeltaState_t::m_NormalDeltas
    int m_nWrinkleIndex; // Index into DeltaState_t::m_WrinkleDeltas
    int m_nNextDelta; // Index into DeltaState_t::m_DeltaIndices;
    bool m_bInList;
};

struct DeltaState_t {
    DeltaState_t() : m_nDeltaCount(0), m_nFirstDelta(-1) {
    }

    CUtlString m_Name;
    CUtlVector<Vector> m_PositionDeltas;
    CUtlVector<Vector> m_NormalDeltas;
    CUtlVector<float> m_WrinkleDeltas;
    CUtlVector<DeltaIndex_t> m_DeltaIndices;
    int m_nDeltaCount;
    int m_nFirstDelta;
};


// NOTE: This is a temporary which loses its state once Load_DMX is exited.
static CUtlVector<DeltaState_t> s_DeltaStates;


// Finds or adds delta states. These pointers are invalidated by calling FindOrAddDeltaState again
//-----------------------------------------------------------------------------
static DeltaState_t *FindOrAddDeltaState(const char *pDeltaStateName, const int nBaseStateVertexCount) {
    int nCount = s_DeltaStates.Count();
    for (int i = 0; i < nCount; ++i) {
        if (!Q_stricmp(s_DeltaStates[i].m_Name, pDeltaStateName)) {
            MdlWarning("Unsupported duplicate delta state named \"{}\" in DMX file\n", pDeltaStateName);

            s_DeltaStates[i].m_DeltaIndices.EnsureCount(nBaseStateVertexCount);
            return &s_DeltaStates[i];
        }
    }

    int j = s_DeltaStates.AddToTail();
    s_DeltaStates[j].m_Name = pDeltaStateName;
    s_DeltaStates[j].m_DeltaIndices.SetCount(nBaseStateVertexCount);
    return &s_DeltaStates[j];
}


struct VertexLookup_t {
    int v, n, t;
    int index;
};

static bool VertexLookup_CompareFunc(VertexLookup_t const &a, VertexLookup_t const &b) {
    return ((a.v == b.v) && (a.n == b.n) && (a.t == b.t));
}

static unsigned int VertexLookup_KeyFunc(VertexLookup_t const &a) {
    return Hash12(&a);
}

//-----------------------------------------------------------------------------
// Loads the vertices from the model
//-----------------------------------------------------------------------------
static bool DefineUniqueVertices(CDmeVertexData *pBindState) {
    const CUtlVector<int> &positionIndices = pBindState->GetVertexIndexData(CDmeVertexData::FIELD_POSITION);
    const CUtlVector<int> &normalIndices = pBindState->GetVertexIndexData(CDmeVertexData::FIELD_NORMAL);
    const CUtlVector<int> &texcoordIndices = pBindState->GetVertexIndexData(CDmeVertexData::FIELD_TEXCOORD);
    const CUtlVector<int> &balanceIndices = pBindState->GetVertexIndexData(CDmeVertexData::FIELD_BALANCE);
    const CUtlVector<int> &speedIndices = pBindState->GetVertexIndexData(CDmeVertexData::FIELD_MORPH_SPEED);

    int nPositionCount = positionIndices.Count();
    int nNormalCount = normalIndices.Count();
    int nTexcoordCount = texcoordIndices.Count();
    int nBalanceCount = balanceIndices.Count();
    int nSpeedCount = speedIndices.Count();

    int nExtraTexcoordCount[MAXSTUDIOTEXCOORDS - 1];
    const CUtlVector<int> *extraTexcoordIndices[MAXSTUDIOTEXCOORDS - 1];

    for (int i = 1; i < MAXSTUDIOTEXCOORDS; ++i) {
        FieldIndex_t nExtra = pBindState->FindFieldIndex(CFmtStr("texcoord$%d", i).Get());
        if (nExtra != -1) {
            extraTexcoordIndices[i - 1] = &pBindState->GetVertexIndexData(nExtra);
            nExtraTexcoordCount[i - 1] = extraTexcoordIndices[i - 1]->Count();
            if (nPositionCount != nExtraTexcoordCount[i - 1]) {
                MdlError(
                    "Encountered a mesh with invalid geometry (different number of indices for various data fields)\n");
                return false;
            }
        }
        else {
            extraTexcoordIndices[i - 1] = nullptr;
            nExtraTexcoordCount[i - 1] = 0;
        }
    }

    if (nNormalCount && nPositionCount != nNormalCount) {
        MdlError("Encountered a mesh with invalid geometry (different number of indices for various data fields)\n");
        return false;
    }
    if (nTexcoordCount && nPositionCount != nTexcoordCount) {
        MdlError("Encountered a mesh with invalid geometry (different number of indices for various data fields)\n");
        return false;
    }
    if (nBalanceCount && nPositionCount != nBalanceCount) {
        MdlError("Encountered a mesh with invalid geometry (different number of indices for various data fields)\n");
        return false;
    }
    if (nSpeedCount && nPositionCount != nSpeedCount) {
        MdlError("Encountered a mesh with invalid geometry (different number of indices for various data fields)\n");
        return false;
    }

    // Make a hash table to speed up this de-duplication process:
    CUtlHash<VertexLookup_t> vertexLookupHash(nPositionCount, 0, 0, VertexLookup_CompareFunc, VertexLookup_KeyFunc);

    // Only add unique vertices to the list as in UnifyIndices
    for (int i = 0; i < nPositionCount; ++i) {
        VertIndices_t vert;
        vert.v = static_cast<int>(g_studiomdl.input.vertex.size()) + positionIndices[i];
        vert.n = (nNormalCount > 0) ? static_cast<int>(g_studiomdl.input.normal.size()) + normalIndices[i] : -1;
        vert.t[0] = (nTexcoordCount > 0) ? static_cast<int>(g_studiomdl.input.texcoord[0].size()) + texcoordIndices[i] : -1;
        vert.balance = s_Balance.Count() + ((nBalanceCount > 0) ? balanceIndices[i] : 0);
        vert.speed = s_Speed.Count() + ((nSpeedCount > 0) ? speedIndices[i] : 0);
        for (int j = 1; j < MAXSTUDIOTEXCOORDS; ++j) {
            vert.t[j] = (nExtraTexcoordCount[j - 1] > 0)
                            ? static_cast<int>(g_studiomdl.input.texcoord[j].size()) + extraTexcoordIndices[j - 1]->Element(i)
                            : -1;
        }

        VertexLookup_t vertexLookup = {vert.v, vert.n, vert.t[0], -1};
        UtlHashHandle_t vertexHandle = vertexLookupHash.Find(vertexLookup);
        if (vertexHandle == vertexLookupHash.InvalidHandle()) {
            // Unique
            int k = s_UniqueVertices.AddToTail();
            s_UniqueVertices[k] = vert;
            s_UniqueVerticesMap.AddToTail(k);
            vertexLookup.index = k;
            vertexLookupHash.Insert(vertexLookup);
        }
        else {
            // Not unique
            VertexLookup_t &equivalentVertex = vertexLookupHash.Element(vertexHandle);
            s_UniqueVerticesMap.AddToTail(equivalentVertex.index);
        }
    }

    return true;
}


//-----------------------------------------------------------------------------
// Loads the vertices from the model
//-----------------------------------------------------------------------------
static bool
LoadVertices(CDmeDag *pDmeDag, CDmeVertexData *pBindState, const matrix3x4_t &mat, const float flScale, int nBoneAssign,
             int *pBoneRemap, s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("DMX");
    // nBoneAssign is only used if the mesh has no skinning information
    // It's the DMX bone index, but it might be < 0, in which case
    // no bone has been defined yet.  There are two options, use the
    // default root bone which is always defined and at the origin
    // or try and use the DmeDag's bone that was created when the mesh was
    // loaded
    if (nBoneAssign < 0) {
        nBoneAssign = s_nDefaultRootNode;
    }
    else {
        nBoneAssign = pBoneRemap[nBoneAssign];
        if (nBoneAssign < 0) {
            nBoneAssign = s_nDefaultRootNode;
        }
    }

    // Used by the morphing system to set up delta states
    DefineUniqueVertices(pBindState);

    matrix3x4_t normalMat;
    MatrixInverseTranspose(mat, normalMat);

    const CUtlVector<Vector> &positions = pBindState->GetPositionData();
    const CUtlVector<Vector> &normals = pBindState->GetNormalData();
    const CUtlVector<Vector2D> &texcoords = pBindState->GetTextureCoordData();
    const CUtlVector<float> &balances = pBindState->GetBalanceData();
    const CUtlVector<float> &speeds = pBindState->GetMorphSpeedData();

    int positionCount = positions.Count();

    const int jointCount = pBindState->HasSkinningData() ? pBindState->JointCount() : 0;

    if (jointCount <= 0 && nBoneAssign == s_nDefaultRootNode && pDmeDag) {
        // Use the bone created for the DmeDag node of the DmeMesh
        for (int i = 0; i < pSource->numbones; ++i) {
            if (!Q_strcmp(pDmeDag->GetName(), pSource->localBone[i].name)) {
                nBoneAssign = i;
                break;
            }
        }
    }

    std::array<bool, MAXSTUDIOBONES> warnmap{};
    // Any number of joints per vertex is accepted; SortAndBalanceBones keeps the MAXSTUDIOBONEWEIGHTS largest
    std::vector<float> weightBuf(jointCount);
    std::vector<int> indexBuf(jointCount);

    // Meshes with more influences per vertex than fit: measures what trimming loses, and with -fitweights refits
    // the kept weights instead of just rescaling them
    const bool bFitWeights = g_studiomdl.options.fitWeights;
    std::optional<CWeightFitter> fitter;
    std::vector<float> fullWeights;
    std::vector<int> fullBones;
    struct TrimmedVertex {
        int vertex;
        s_boneweight_t boneweight;
    };
    std::vector<TrimmedVertex> trimmedVertices;
    if (jointCount > static_cast<int>(MAXSTUDIOBONEWEIGHTS)) {
        fitter.emplace(*pSource, bFitWeights, g_studiomdl.options.fitWeightsFitPoses,
                       g_studiomdl.options.fitWeightsTestPoses);
        fullWeights.resize(jointCount);
        fullBones.resize(jointCount);
    }

    // Copy positions + bone info
    for (int i = 0; i < positionCount; ++i) {
        const int vertexIndex = static_cast<int>(g_studiomdl.input.vertex.size());
        Vector &vertex = g_studiomdl.input.vertex.emplace_back();
        s_boneweight_t &boneWeight = g_studiomdl.input.bone.emplace_back();
        // NOTE: The transform transforms the positions into the bind space
        VectorTransform(positions[i], mat, vertex);
        vertex *= flScale;
        if (jointCount == 0) {
            boneWeight.numbones = 1;
            boneWeight.bone[0] = nBoneAssign;
            boneWeight.weight[0] = 1.0;
        }
        else {
            const float *pJointWeights = pBindState->GetJointWeightData(i);
            const int *pJointIndices = pBindState->GetJointIndexData(i);

            memcpy(weightBuf.data(), pJointWeights, jointCount * sizeof(float));
            memcpy(indexBuf.data(), pJointIndices, jointCount * sizeof(int));

            int boneCount;
            if (fitter) {
                // The full influence list on source bones, so joints mapped to the same bone collapse
                for (int j = 0; j < jointCount; ++j) {
                    const int nMapped = pBoneRemap[indexBuf[j]];
                    // without -fitweights the default path below reports unmapped joints
                    if (bFitWeights && nMapped < 0 && weightBuf[j] > 0 && indexBuf[j] < MAXSTUDIOBONES &&
                        !warnmap[indexBuf[j]]) {
                        MdlWarning("DmeMesh[{}] Verts Assigned To DmeModel.jointList[{}] Which Isn't Mapped To The Dag Hierarchy\n",
                            pDmeDag->GetName(), indexBuf[j]);
                        warnmap[indexBuf[j]] = true;
                    }
                    fullBones[j] = nMapped < 0 ? nBoneAssign : nMapped;
                    fullWeights[j] = weightBuf[j];
                }

                // sorted, nothing dropped yet, summing to 1
                int nUsed = SortAndBalanceBones(jointCount, jointCount, fullBones, fullWeights);
                while (nUsed > 0 && fullWeights[nUsed - 1] <= 0) {
                    --nUsed;
                }
                if (nUsed > static_cast<int>(MAXSTUDIOBONEWEIGHTS)) {
                    if (bFitWeights) {
                        // the plain trim, in case the fit turns out not to be better on this mesh
                        TrimmedVertex &trimmed = trimmedVertices.emplace_back();
                        trimmed.vertex = vertexIndex;
                        trimmed.boneweight.numbones = MAXSTUDIOBONEWEIGHTS;
                        const float flKeptSum = fullWeights[0] + fullWeights[1] + fullWeights[2];
                        for (int j = 0; j < static_cast<int>(MAXSTUDIOBONEWEIGHTS); ++j) {
                            trimmed.boneweight.bone[j] = fullBones[j];
                            trimmed.boneweight.weight[j] = fullWeights[j] / flKeptSum;
                        }
                    }
                    fitter->Reduce(vertex, fullBones, fullWeights, nUsed);
                }

                if (bFitWeights) {
                    // sorts the kept ones again; influences past nUsed have zero weight
                    boneCount = SortAndBalanceBones(MAXSTUDIOBONEWEIGHTS, MAXSTUDIOBONEWEIGHTS, fullBones,
                                                    fullWeights);

                    boneWeight.numbones = boneCount;
                    for (int j = 0; j < boneCount; ++j) {
                        boneWeight.bone[j] = fullBones[j];
                        boneWeight.weight[j] = fullWeights[j];
                    }
                    continue;
                }
            }

            boneCount = SortAndBalanceBones(jointCount, MAXSTUDIOBONEWEIGHTS, indexBuf, weightBuf);
            int nBoneIndex = -1;

            boneWeight.numbones = boneCount;
            for (int j = 0; j < boneCount; ++j) {
                nBoneIndex = pBoneRemap[indexBuf[j]];
                if (nBoneIndex < 0) {
                    if (indexBuf[j] < MAXSTUDIOBONES && !warnmap[indexBuf[j]]) {
                        MdlWarning("DmeMesh[{}] Verts Assigned To DmeModel.jointList[{}] Which Isn't Mapped To The Dag Hierarchy\n",
                            pDmeDag->GetName(), indexBuf[j]);
                        warnmap[indexBuf[j]] = true;
                    }
                    boneWeight.bone[j] = nBoneAssign;
                }
                else {
                    boneWeight.bone[j] = nBoneIndex;
                }
                boneWeight.weight[j] = weightBuf[j];
            }
        }
    }
    if (fitter) {
        if (bFitWeights && !fitter->FitIsBetter()) {
            // not worth it on this mesh: back to the plain trim
            for (const TrimmedVertex &trimmed: trimmedVertices) {
                g_studiomdl.input.bone[trimmed.vertex] = trimmed.boneweight;
            }
        }
        if (!g_studiomdl.options.quiet) {
            fitter->Report(pDmeDag ? pDmeDag->GetName() : pSource->filename.c_str());
        }
    }

    // Copy normals
    Vector vNormal;
    positionCount = normals.Count();
    for (int i = 0; i < positionCount; ++i) {
        VectorCopy(normals[i], vNormal);
        VectorNormalize(vNormal);
        if (fabs(VectorLength(vNormal) - 1.0f) > 0.01) {
            MdlWarning("Non-Unit Length Normal [{}] < {:8.6f} {:8.6f} {:8.6f} >\n", i, vNormal.x, vNormal.y, vNormal.z);
        }
        VectorRotate(vNormal, normalMat, g_studiomdl.input.normal.emplace_back());
    }

    // Copy texcoords
    positionCount = texcoords.Count();
    bool bFlipVCoordinate = pBindState->IsVCoordinateFlipped();
    for (int i = 0; i < positionCount; ++i) {
        g_studiomdl.input.texcoord[0].emplace_back(texcoords[i].x,
                                                   bFlipVCoordinate ? 1.0f - texcoords[i].y : texcoords[i].y);
    }

    // Check for additional texcoords
    for (int i = 1; i < MAXSTUDIOTEXCOORDS; ++i) {
        g_studiomdl.input.texcoord[i].clear();
        const FieldIndex_t fieldIndex = pBindState->FindFieldIndex(CFmtStr("texcoord$%d", i).Get());
        if (fieldIndex >= 0) {
            CDmrArrayConst<Vector2D> vertexData = pBindState->GetVertexData(fieldIndex);
            const CUtlVector<Vector2D> &extraTexcoords = vertexData.Get();
            positionCount = extraTexcoords.Count();
            for (int j = 0; j < positionCount; ++j) {
                g_studiomdl.input.texcoord[i].emplace_back(extraTexcoords[j].x,
                                                           bFlipVCoordinate ? 1.0f - extraTexcoords[j].y
                                                                            : extraTexcoords[j].y);
            }
        }
        else {
            break;
        }
    }

    // In the event of no speed or balance map, use the same value of 1 for all vertices
    if (balances.Count()) {
        s_Balance.AddMultipleToTail(balances.Count(), balances.Base());
    }
    else {
        s_Balance.AddToTail(1.0f);
    }

    if (speeds.Count()) {
        s_Speed.AddMultipleToTail(speeds.Count(), speeds.Base());
    }
    else {
        s_Speed.AddToTail(1.0f);
    }

    return true;
}


//-----------------------------------------------------------------------------
// Hook delta into delta list
//-----------------------------------------------------------------------------
static void AddToDeltaList(DeltaState_t *pDeltaStateData, const int nUniqueVertex) {
    DeltaIndex_t &index = pDeltaStateData->m_DeltaIndices[nUniqueVertex];
    if (!index.m_bInList) {
        index.m_nNextDelta = pDeltaStateData->m_nFirstDelta;
        pDeltaStateData->m_nFirstDelta = nUniqueVertex;
        pDeltaStateData->m_nDeltaCount++;
        index.m_bInList = true;
    }
}


//-----------------------------------------------------------------------------
// Loads the vertices from the delta state
//-----------------------------------------------------------------------------
static bool LoadDeltaState(
    CDmeVertexDeltaData *pDeltaState,
    CDmeVertexData *pBindState,
    const matrix3x4_t &mat,
    const float flScale,
    const int nStartingUniqueVertex,
    const int nStartingUniqueVertexMap) {
    vstd::FunctionTimingRecorder _("DMX");
    DeltaState_t *pDeltaStateData = FindOrAddDeltaState(pDeltaState->GetName(),
                                                        nStartingUniqueVertex + pBindState->VertexCount());

    matrix3x4_t normalMat;
    MatrixInverseTranspose(mat, normalMat);

    const CUtlVector<Vector> &positions = pDeltaState->GetPositionData();
    const CUtlVector<int> &positionIndices = pDeltaState->GetVertexIndexData(CDmeVertexDataBase::FIELD_POSITION);
    const CUtlVector<Vector> &normals = pDeltaState->GetNormalData();
    const CUtlVector<int> &normalIndices = pDeltaState->GetVertexIndexData(CDmeVertexDataBase::FIELD_NORMAL);
    const CUtlVector<float> &wrinkle = pDeltaState->GetWrinkleData();
    const CUtlVector<int> &wrinkleIndices = pDeltaState->GetVertexIndexData(CDmeVertexDataBase::FIELD_WRINKLE);

    if (positions.Count() != positionIndices.Count()) {
        MdlError("DeltaState {} contains a different number of positions + position indices!\n",
                 pDeltaState->GetName());
        return false;
    }

    if (normals.Count() != normalIndices.Count()) {
        MdlError("DeltaState {} contains a different number of normals + normal indices!\n", pDeltaState->GetName());
        return false;
    }

    if (wrinkle.Count() != wrinkleIndices.Count()) {
        MdlError("DeltaState {} contains a different number of wrinkles + wrinkle indices!\n", pDeltaState->GetName());
        return false;
    }

    // Copy position delta
    int nCount = positions.Count();
    for (int i = 0; i < nCount; ++i) {
        Vector vecDelta;

        // NOTE NOTE!!: This is VectorRotate, *not* VectorTransform. This is because
        // we're transforming a delta, which is basically a direction vector. To
        // move it into the new space, we must rotate it
        VectorRotate(positions[i], mat, vecDelta);
        vecDelta *= flScale;

        int nPositionIndex = pDeltaStateData->m_PositionDeltas.AddToTail(vecDelta);

        // Indices
        const CUtlVector<int> &baseVerts = pBindState->FindVertexIndicesFromDataIndex(CDmeVertexData::FIELD_POSITION,
            positionIndices[i]);
        int nBaseVertCount = baseVerts.Count();
        for (int k = 0; k < nBaseVertCount; ++k) {
            int nUniqueVertexIndex = s_UniqueVerticesMap[nStartingUniqueVertexMap + baseVerts[k]];
            AddToDeltaList(pDeltaStateData, nUniqueVertexIndex);
            DeltaIndex_t &index = pDeltaStateData->m_DeltaIndices[nUniqueVertexIndex];
            index.m_nPositionIndex = nPositionIndex;
        }
    }

    // Copy normals
    nCount = normals.Count();
    for (int i = 0; i < nCount; ++i) {
        Vector vecDelta;
        VectorRotate(normals[i], normalMat, vecDelta);
        int nNormalIndex = pDeltaStateData->m_NormalDeltas.AddToTail(vecDelta);

        // Indices
        const CUtlVector<int> &baseVerts = pBindState->FindVertexIndicesFromDataIndex(CDmeVertexData::FIELD_NORMAL,
            normalIndices[i]);
        int nBaseVertCount = baseVerts.Count();
        for (int k = 0; k < nBaseVertCount; ++k) {
            int nUniqueVertexIndex = s_UniqueVerticesMap[nStartingUniqueVertexMap + baseVerts[k]];
            AddToDeltaList(pDeltaStateData, nUniqueVertexIndex);
            DeltaIndex_t &index = pDeltaStateData->m_DeltaIndices[nUniqueVertexIndex];
            index.m_nNormalIndex = nNormalIndex;
        }
    }

    // Copy wrinkle
    nCount = wrinkle.Count();
    for (int i = 0; i < nCount; ++i) {
        int nWrinkleIndex = pDeltaStateData->m_WrinkleDeltas.AddToTail(wrinkle[i]);

        // Indices
        const CUtlVector<int> &baseVerts = pBindState->FindVertexIndicesFromDataIndex(CDmeVertexData::FIELD_WRINKLE,
            wrinkleIndices[i]);
        int nBaseVertCount = baseVerts.Count();
        for (int k = 0; k < nBaseVertCount; ++k) {
            int nUniqueVertexIndex = s_UniqueVerticesMap[nStartingUniqueVertexMap + baseVerts[k]];
            AddToDeltaList(pDeltaStateData, nUniqueVertexIndex);
            DeltaIndex_t &index = pDeltaStateData->m_DeltaIndices[nUniqueVertexIndex];
            index.m_nWrinkleIndex = nWrinkleIndex;
        }
    }
    return true;
}


static int GetExtraTexcoordIndex(CDmeVertexData *pVertexData, const int nVertexIndex, const int nChannel) {
    FieldIndex_t nFieldIndex = pVertexData->FindFieldIndex(CFmtStr("texcoord$%d", nChannel).Get());
    if (nFieldIndex < 0)
        return -1;

    CDmrArrayConst<int> indices(pVertexData->GetIndexData(nFieldIndex));
    return indices[nVertexIndex];
}

// JasonM TODO: unify ParseQuadFaceData() and ParseFaceData()

struct MeshInfo {
    bool hasPositions;
    bool hasNormals;
    bool hasTexCoords;
    vstd::FixedVector<std::pair<uint32_t, FieldIndex_t>, MAXSTUDIOTEXCOORDS> activeUvFieldIndices;
};


//-----------------------------------------------------------------------------
// Reads the quad face data from the DMX data
//-----------------------------------------------------------------------------
static void ParseQuadFaceData(CDmeVertexData *pVertexData, const int material,
                              int *pIndices, const int vi, const int ni, int *ti,
                              MeshInfo &meshInfo) {
    vstd::FunctionTimingRecorder _("DMX");
    auto &face = g_studiomdl.input.faces.emplace_back();
    face.material = material;
    if (meshInfo.hasPositions) {
        const int p0 = pVertexData->GetPositionIndex(pIndices[0]);
        const int p1 = pVertexData->GetPositionIndex(pIndices[3]);
        const int p2 = pVertexData->GetPositionIndex(pIndices[2]);
        const int p3 = pVertexData->GetPositionIndex(pIndices[1]);
        face.a = p0 >= 0 ? vi + p0 : 0;
        face.b = p1 >= 0 ? vi + p1 : 0;
        face.c = p2 >= 0 ? vi + p2 : 0;
        face.d = p3 >= 0 ? vi + p3 : 0;
    }
    else {
        face.a = face.b = face.c = face.d = 0;
    }
    if (meshInfo.hasNormals) {
        const int n0 = pVertexData->GetNormalIndex(pIndices[0]);
        const int n1 = pVertexData->GetNormalIndex(pIndices[3]);
        const int n2 = pVertexData->GetNormalIndex(pIndices[2]);
        const int n3 = pVertexData->GetNormalIndex(pIndices[1]);

        face.na = n0 >= 0 ? ni + n0 : 0;
        face.nb = n1 >= 0 ? ni + n1 : 0;
        face.nc = n2 >= 0 ? ni + n2 : 0;
        face.nd = n3 >= 0 ? ni + n3 : 0;
    }
    else {
        face.na = face.nb = face.nc = face.nd = 0;
    }

    if (meshInfo.hasTexCoords) {
        const int t0 = pVertexData->GetTexCoordIndex(pIndices[0]);
        const int t1 = pVertexData->GetTexCoordIndex(pIndices[3]);
        const int t2 = pVertexData->GetTexCoordIndex(pIndices[2]);
        const int t3 = pVertexData->GetTexCoordIndex(pIndices[1]);
        face.ta[0] = t0 >= 0 ? ti[0] + t0 : 0;
        face.tb[0] = t1 >= 0 ? ti[0] + t1 : 0;
        face.tc[0] = t2 >= 0 ? ti[0] + t2 : 0;
        face.td[0] = t3 >= 0 ? ti[0] + t3 : 0;
    }
    else {
        face.ta[0] = face.tb[0] = face.tc[0] = face.td[0] = 0;
    }
    Assert(
        face.a <= g_studiomdl.input.vertex.size() &&
        face.b <= g_studiomdl.input.vertex.size() &&
        face.c <= g_studiomdl.input.vertex.size() &&
        face.d <= g_studiomdl.input.vertex.size());
    Assert(
        face.na <= g_studiomdl.input.normal.size() &&
        face.nb <= g_studiomdl.input.normal.size() &&
        face.nc <= g_studiomdl.input.normal.size() &&
        face.nd <= g_studiomdl.input.normal.size());
    Assert(
        face.ta[0] <= g_studiomdl.input.texcoord[0].size() &&
        face.tb[0] <= g_studiomdl.input.texcoord[0].size() &&
        face.tc[0] <= g_studiomdl.input.texcoord[0].size() &&
        face.td[0] <= g_studiomdl.input.texcoord[0].size());

    for (auto [layerIndex, layerFieldIndex]: meshInfo.activeUvFieldIndices) {
        CDmrArrayConst<int> indices(pVertexData->GetIndexData(layerFieldIndex));
        face.ta[layerIndex] = ti[layerIndex] + indices[pIndices[0]];
        face.tb[layerIndex] = ti[layerIndex] + indices[pIndices[3]];
        face.tc[layerIndex] = ti[layerIndex] + indices[pIndices[2]];
        face.td[layerIndex] = ti[layerIndex] + indices[pIndices[1]];
        Assert(
            face.ta[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size() &&
            face.tb[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size() &&
            face.tc[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size() &&
            face.td[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size()
        );
    }
}


//-----------------------------------------------------------------------------
// Reads the face data from the DMX data
//-----------------------------------------------------------------------------
static void ParseFaceData(CDmeVertexData *pVertexData, const int material,
                          const int v1, const int v2, const int v3,
                          const int vi, const int ni, int *ti,
                          MeshInfo &meshInfo) {
    vstd::FunctionTimingRecorder _("DMX");
    s_tmpface_t &face = g_studiomdl.input.faces.emplace_back();
    face.material = material;
    if (meshInfo.hasPositions) {
        const int p0 = pVertexData->GetPositionIndex(v1);
        const int p1 = pVertexData->GetPositionIndex(v2);
        const int p2 = pVertexData->GetPositionIndex(v3);
        face.a = p0 >= 0 ? vi + p0 : 0;
        face.b = p1 >= 0 ? vi + p1 : 0;
        face.c = p2 >= 0 ? vi + p2 : 0;
    }
    else {
        face.a = face.b = face.c = 0;
    }
    if (meshInfo.hasNormals) {
        const int n0 = pVertexData->GetNormalIndex(v1);
        const int n1 = pVertexData->GetNormalIndex(v2);
        const int n2 = pVertexData->GetNormalIndex(v3);
        face.na = n0 >= 0 ? ni + n0 : 0;
        face.nb = n1 >= 0 ? ni + n1 : 0;
        face.nc = n2 >= 0 ? ni + n2 : 0;
    }
    else {
        face.na = face.nb = face.nc = 0;
    }
    if (meshInfo.hasTexCoords) {
        const int t0 = pVertexData->GetTexCoordIndex(v1);
        const int t1 = pVertexData->GetTexCoordIndex(v2);
        const int t2 = pVertexData->GetTexCoordIndex(v3);
        face.ta[0] = t0 >= 0 ? ti[0] + t0 : 0;
        face.tb[0] = t1 >= 0 ? ti[0] + t1 : 0;
        face.tc[0] = t2 >= 0 ? ti[0] + t2 : 0;
    }
    else {
        face.ta[0] = face.tb[0] = face.tc[0] = 0;
    }

    Assert(
        face.a <= g_studiomdl.input.vertex.size() &&
        face.b <= g_studiomdl.input.vertex.size() &&
        face.c <= g_studiomdl.input.vertex.size()
    );
    Assert(
        face.na <= g_studiomdl.input.normal.size() &&
        face.nb <= g_studiomdl.input.normal.size() &&
        face.nc <= g_studiomdl.input.normal.size()
    );
    Assert(
        face.ta[0] <= g_studiomdl.input.texcoord[0].size() &&
        face.tb[0] <= g_studiomdl.input.texcoord[0].size() &&
        face.tc[0] <= g_studiomdl.input.texcoord[0].size()
    );
    for (auto [layerIndex, layerFieldIndex]: meshInfo.activeUvFieldIndices) {
        CDmrArrayConst<int> indices(pVertexData->GetIndexData(layerFieldIndex));
        const int ot1 = indices[v1];
        const int ot2 = indices[v2];
        const int ot3 = indices[v3];
        face.ta[layerIndex] = ot1 >= 0 ? ti[layerIndex] + ot1 : 0;
        face.tb[layerIndex] = ot2 >= 0 ? ti[layerIndex] + ot2 : 0;
        face.tc[layerIndex] = ot3 >= 0 ? ti[layerIndex] + ot3 : 0;
        Assert(
            face.ta[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size() &&
            face.tb[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size() &&
            face.tc[layerIndex] <= g_studiomdl.input.texcoord[layerIndex].size()
        );
    }
}


//-----------------------------------------------------------------------------
// Reads the mesh data from the DMX data
//-----------------------------------------------------------------------------
static bool
LoadMesh(CDmeDag *pDmeDag, CDmeMesh *pMesh, CDmeVertexData *pBindState, const matrix3x4_t &mat, const float flScale,
         const int nBoneAssign, int *pBoneRemap, s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("DMX");
    pMesh->CollapseRedundantNormals(g_studiomdl.options.normal_blend);

    // Load the vertices
    int nStartingVertex = static_cast<int>(g_studiomdl.input.vertex.size());
    int nStartingNormal = static_cast<int>(g_studiomdl.input.normal.size());
    int nStartingUniqueCount = s_UniqueVertices.Count();
    int nStartingUniqueMapCount = s_UniqueVerticesMap.Count();
    int nStartingTexCoord[MAXSTUDIOTEXCOORDS];
    for (int i = 0; i < MAXSTUDIOTEXCOORDS; ++i) {
        nStartingTexCoord[i] = static_cast<int>(g_studiomdl.input.texcoord[i].size());
    }

    // This defines s_UniqueVertices & s_UniqueVerticesMap
    LoadVertices(pDmeDag, pBindState, mat, flScale, nBoneAssign, pBoneRemap, pSource);

    // Load the deltas
    int nDeltaStateCount = pMesh->DeltaStateCount();
    for (int i = 0; i < nDeltaStateCount; ++i) {
        CDmeVertexDeltaData *pDeltaState = pMesh->GetDeltaState(i);
        if (!LoadDeltaState(pDeltaState, pBindState, mat, flScale, nStartingUniqueCount, nStartingUniqueMapCount))
            return false;
    }

    // load the base triangles
    int texture;
    int material;
    char pTextureName[MAX_PATH];

    int nFaceSetCount = pMesh->FaceSetCount();
    for (int i = 0; i < nFaceSetCount; ++i) {
        CDmeFaceSet *pFaceSet = pMesh->GetFaceSet(i);
        CDmeMaterial *pMaterial = pFaceSet->GetMaterial();

        // Get the material name
        Q_strncpy(pTextureName, pMaterial->GetMaterialName(), sizeof(pTextureName));

        // funky texture overrides (specified with the -t command-line argument)
        for (int j = 0; j < std::ssize(g_studiomdl.materials.defaulttextures); j++) {
            if (g_studiomdl.materials.sourcetextures[j][0] == '\0') {
                Q_strncpy(pTextureName, g_studiomdl.materials.defaulttextures[j], sizeof(pTextureName));
                break;
            }
            if (vstd::iequals(pTextureName, g_studiomdl.materials.sourcetextures[j])) {
                Q_strncpy(pTextureName, g_studiomdl.materials.defaulttextures[j], sizeof(pTextureName));
                break;
            }
        }

        // skip all faces with the null texture on them.
        char pPathNoExt[MAX_PATH]{};
        Q_StripExtension(pTextureName, pPathNoExt, sizeof(pPathNoExt));
        if (!Q_stricmp(pPathNoExt, "null"))
            continue;

        texture = LookupTexture(pTextureName, true);
        pSource->texmap[texture] = texture; // hack, make it 1:1
        material = UseTextureAsMaterial(texture);

        // Is this a quad-only subd?
        bool bQuadSubd = (g_studiomdl.header.gflags & STUDIOHDR_FLAGS_SUBDIVISION_SURFACE) != 0;
        MeshInfo meshInfo;
        meshInfo.hasPositions = pBindState->FindFieldIndex(CDmeVertexDataBase::FIELD_POSITION) >= 0;
        meshInfo.hasTexCoords = pBindState->FindFieldIndex(CDmeVertexDataBase::FIELD_TEXCOORD) >= 0;
        meshInfo.hasNormals = pBindState->FindFieldIndex(CDmeVertexDataBase::FIELD_NORMAL) >= 0;

        for (uint32_t nChannel = 0; nChannel < MAXSTUDIOTEXCOORDS; ++nChannel) {
            const FieldIndex_t fieldIndex = pBindState->FindFieldIndex(CFmtStr("texcoord$%d", nChannel).Get());
            if (fieldIndex < 0)
                continue;
            meshInfo.activeUvFieldIndices.emplace_back(nChannel, fieldIndex);
        }

        // prepare indices
        const uint32_t indexCount = pFaceSet->NumIndices();
        const auto indicesArray = pFaceSet->GetIndices();
        const uint32_t breakCount = std::count_if(indicesArray, indicesArray + indexCount,
                                                  [](const int32_t index) { return index != -1; });
        if (indexCount % 4 == 0 && (indexCount / 4 * 3) == breakCount) {
            std::array<int, 3> triangleStorage{};
            for (uint32_t faceStartVertex = 0; faceStartVertex < indexCount; faceStartVertex += 4) {
                pMesh->ComputeTriangulatedIndices(pBindState, pFaceSet, faceStartVertex, triangleStorage.data());
                ParseFaceData(pBindState, material,
                              triangleStorage[0], triangleStorage[2], triangleStorage[1],
                              nStartingVertex, nStartingNormal, nStartingTexCoord,
                              meshInfo);
            }
        }
        else {
            uint32_t firstIndex = 0;
            std::array<int, 32> triangleStorage{};
            while (firstIndex < indexCount) {
                const uint32_t vertexCount = pFaceSet->GetNextPolygonVertexCount(firstIndex);

                // Quad subd face?
                if (bQuadSubd && (vertexCount == 4)) {
                    int quadIndices[4];

                    for (int j = 0; j < 4; j++) {
                        quadIndices[j] = pFaceSet->GetIndex(firstIndex + j);
                    }

                    ParseQuadFaceData(pBindState, material, quadIndices, nStartingVertex, nStartingNormal,
                                      nStartingTexCoord, meshInfo);

                    firstIndex += 5; // -1 in list between face indices, so jump over 5 elements, not 4
                }
                else {
                    if (vertexCount >= 3) {
                        const uint32_t nOutCount = (vertexCount - 2) * 3;
                        if (nOutCount > triangleStorage.size()) {
                            MdlError("N-gon is too big, too many vertices: {} out of {}, increase triangleStorage size",
                                     nOutCount, triangleStorage.size());
                        }

                        pMesh->ComputeTriangulatedIndices(pBindState, pFaceSet, firstIndex, triangleStorage.data());
                        for (int ii = 0; ii < nOutCount; ii += 3) {
                            ParseFaceData(pBindState, material, triangleStorage[ii], triangleStorage[ii + 2],
                                          triangleStorage[ii + 1],
                                          nStartingVertex, nStartingNormal, nStartingTexCoord, meshInfo);
                        }
                    }

                    // -1 in list between face indices, so jump over nVertexCount + 1 elements, not nVertexCount elements
                    firstIndex += vertexCount + 1;
                }
            }
        }
    }

    return true;
}


//-----------------------------------------------------------------------------
// Method used to add mesh data
//-----------------------------------------------------------------------------
struct LoadMeshInfo_t {
    s_source_t *m_pSource;
    CDmeModel *m_pModel;
    float m_flScale;
    int *m_pBoneRemap;
    matrix3x4_t m_pBindPose[MAXSTUDIOSRCBONES];
};

static bool
LoadMeshes(const LoadMeshInfo_t &info, CDmeDag *pDag, const matrix3x4_t &parentToBindPose, int nBoneAssign) {
    // We want to create an aggregate matrix transforming from this dag to its closest
    // parent which actually is an animated joint. This is done so we can autoskin
    // meshes to their closest parents if they have not been skinned.
    matrix3x4_t dagToBindPose;
    int nFoundIndex = info.m_pModel->GetJointIndex(pDag);
    if (nFoundIndex >= 0 /* && ( pDag == info.m_pModel || CastElement< CDmeJoint >( pDag ) ) */) {
        nBoneAssign = nFoundIndex;
    }

    if (nFoundIndex >= 0) {
        ConcatTransforms(parentToBindPose, info.m_pBindPose[nFoundIndex], dagToBindPose);
    }
    else {
        // NOTE: This isn't particularly kosher; we're using the current pose instead of the bind pose
        // because there's no transform in the bind pose
        matrix3x4_t dagToParent;
        pDag->GetTransform()->GetTransform(dagToParent);
        ConcatTransforms(parentToBindPose, dagToParent, dagToBindPose);
    }

    CDmeMesh *pMesh = CastElement<CDmeMesh>(pDag->GetShape());
    if (pMesh) {
        CDmeVertexData *pBindState = pMesh->FindBaseState("bind");
        if (!pBindState)
            return false;

        if (!LoadMesh(pDag, pMesh, pBindState, dagToBindPose, info.m_flScale, nBoneAssign, info.m_pBoneRemap,
                      info.m_pSource))
            return false;
    }

    int nCount = pDag->GetChildCount();
    for (int i = 0; i < nCount; ++i) {
        CDmeDag *pChild = pDag->GetChild(i);
        if (!LoadMeshes(info, pChild, dagToBindPose, nBoneAssign))
            return false;
    }

    return true;
}


//-----------------------------------------------------------------------------
// Method used to add mesh data
//-----------------------------------------------------------------------------
static bool LoadMeshes(CDmeModel *pModel, const float flScale, int *pBoneRemap, s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("DMX");
    matrix3x4_t mat;
    SetIdentityMatrix(mat);

    LoadMeshInfo_t info;
    info.m_pModel = pModel;
    info.m_flScale = flScale;
    info.m_pBoneRemap = pBoneRemap;
    info.m_pSource = pSource;

    CDmeTransformList *pBindPose = pModel->FindBaseState("bind");
    int nCount = pBindPose ? pBindPose->GetTransformCount() : pModel->GetJointCount();
    for (int i = 0; i < nCount; ++i) {
        CDmeTransform *pTransform = pBindPose ? pBindPose->GetTransform(i) : pModel->GetJointTransform(i);

        matrix3x4_t jointTransform;
        pTransform->GetTransform(info.m_pBindPose[i]);
    }

    int nChildCount = pModel->GetChildCount();
    for (int i = 0; i < nChildCount; ++i) {
        CDmeDag *pChild = pModel->GetChild(i);
        if (!LoadMeshes(info, pChild, mat, -1))
            return false;
    }

    return true;
}


//-----------------------------------------------------------------------------
// Builds s_vertanim_ts
//-----------------------------------------------------------------------------
static void BuildVertexAnimations(s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("DMX");
    int nCount = s_DeltaStates.Count();
    if (nCount == 0)
        return;

    Assert(s_Speed.Count() > 0);
    Assert(s_Balance.Count() > 0);

    const auto &entries = g_studiomdl.input.v_listdata;
    Assert(s_UniqueVertices.Count() == static_cast<int>(entries.size()));
    s_vertanim_t *pVertAnim = (s_vertanim_t *) malloc(entries.size() * sizeof(s_vertanim_t));
    for (int i = 0; i < nCount; ++i) {
        DeltaState_t &state = s_DeltaStates[i];

        s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim(pSource, state.m_Name);
        pSourceAnim->numframes = 1;
        pSourceAnim->startframe = 0;
        pSourceAnim->endframe = 0;
        pSourceAnim->newStyleVertexAnimations = true;

        // Traverse the linked list of unique vertex indices j that has a delta
        uint32_t nVertAnimCount = 0;
        for (int32_t j = state.m_nFirstDelta; j >= 0; j = state.m_DeltaIndices[j].m_nNextDelta) {
            // The Delta Indices array is a parallel array to s_UniqueVertices
            // j is used to index into both
            DeltaIndex_t &delta = state.m_DeltaIndices[j];
            Assert(delta.m_nPositionIndex >= 0 || delta.m_nNormalIndex >= 0 || delta.m_nWrinkleIndex >= 0);

            VertIndices_t &uniqueVert = s_UniqueVertices[j];

            const int nFirst = uniqueVert.v < static_cast<int>(g_studiomdl.input.v_list.size())
                                   ? g_studiomdl.input.v_list[uniqueVert.v]
                                   : -1;
            for (int nEntry = nFirst; nEntry >= 0; nEntry = entries[nEntry].next) {
                const v_unify_t &entry = entries[nEntry];
                if (entry.n != uniqueVert.n || entry.t[0] != uniqueVert.t[0])
                    continue;

                s_vertanim_t &vertanim = pVertAnim[nVertAnimCount++];
                vertanim.vertex = nEntry;
                vertanim.speed = s_Speed[s_UniqueVertices[j].speed];
                vertanim.side = s_Balance[s_UniqueVertices[j].balance];
                if (delta.m_nPositionIndex >= 0) {
                    vertanim.pos = state.m_PositionDeltas[delta.m_nPositionIndex];
                }
                else {
                    vertanim.pos = vec3_origin;
                }
                if (delta.m_nNormalIndex >= 0) {
                    vertanim.normal = state.m_NormalDeltas[delta.m_nNormalIndex];
                }
                else {
                    vertanim.normal = vec3_origin;
                }

                if (delta.m_nWrinkleIndex >= 0) {
                    vertanim.wrinkle = state.m_WrinkleDeltas[delta.m_nWrinkleIndex];
                }
                else {
                    vertanim.wrinkle = 0.0f;
                }
            }
        }
        pSourceAnim->numvanims[0] = nVertAnimCount;
        pSourceAnim->vanim[0] = (s_vertanim_t *) calloc(nVertAnimCount, sizeof(s_vertanim_t));
        memcpy(pSourceAnim->vanim[0], pVertAnim, nVertAnimCount * sizeof(s_vertanim_t));
    }
    free(pVertAnim);
}


//-----------------------------------------------------------------------------
// Handles DmeJiggleBones
//-----------------------------------------------------------------------------
static void HandleDmeJiggleBone(const CDmeDag *pDmeDag) {
    const auto *pDmeJiggleBone = CastElementConst<CDmeJiggleBone>(pDmeDag);
    if (!pDmeJiggleBone)
        return;

    const char *pName = pDmeJiggleBone->GetName();
    for (int i = 0; i < std::ssize(g_studiomdl.bones.jigglebones); ++i) {
        if (vstd::iequals(g_studiomdl.bones.jigglebones[i].bonename, pName)) {
            MdlWarning("2000: Jiggle Bone: {} already defined, ignoring additional declarations\n", pName);
            return;
        }
    }

    s_jigglebone_t *pJiggleBone = &g_studiomdl.bones.jigglebones.emplace_back();

    Q_strncpy(pJiggleBone->bonename, pName, sizeof(pJiggleBone->bonename));

    // default values
    memset(&pJiggleBone->data, 0, sizeof(mstudiojigglebone_t));
    pJiggleBone->data.length = 10.0f;
    pJiggleBone->data.yawStiffness = 100.0f;
    pJiggleBone->data.pitchStiffness = 100.0f;
    pJiggleBone->data.alongStiffness = 100.0f;
    pJiggleBone->data.baseStiffness = 100.0f;
    pJiggleBone->data.baseMinUp = -100.0f;
    pJiggleBone->data.baseMaxUp = 100.0f;
    pJiggleBone->data.baseMinLeft = -100.0f;
    pJiggleBone->data.baseMaxLeft = 100.0f;
    pJiggleBone->data.baseMinForward = -100.0f;
    pJiggleBone->data.baseMaxForward = 100.0f;

    // Common Parameters
    pJiggleBone->data.length = pDmeJiggleBone->m_flLength;
    pJiggleBone->data.tipMass = pDmeJiggleBone->m_flTipMass;
    pJiggleBone->data.flags |= (pDmeJiggleBone->m_bLengthConstrained
                                    ? JiggleBoneFlags::HasLengthConstraint
                                    : JiggleBoneFlags::None);
    pJiggleBone->data.angleLimit = vstd::deg2rad<float>(pDmeJiggleBone->m_flAngleLimit);

    pJiggleBone->data.flags |= (pDmeJiggleBone->m_bYawConstrained
                                    ? JiggleBoneFlags::HasYawConstraint
                                    : JiggleBoneFlags::None);
    pJiggleBone->data.minYaw = vstd::deg2rad<float>(pDmeJiggleBone->m_flYawMin);
    pJiggleBone->data.maxYaw = vstd::deg2rad<float>(pDmeJiggleBone->m_flYawMax);
    pJiggleBone->data.yawFriction = pDmeJiggleBone->m_flYawFriction;
    pJiggleBone->data.yawBounce = pDmeJiggleBone->m_flYawBounce;

    pJiggleBone->data.flags |= (pDmeJiggleBone->m_bAngleConstrained
                                    ? JiggleBoneFlags::HasAngleConstraint
                                    : JiggleBoneFlags::None);

    pJiggleBone->data.minPitch = vstd::deg2rad<float>(pDmeJiggleBone->m_flPitchMin);
    pJiggleBone->data.maxPitch = vstd::deg2rad<float>(pDmeJiggleBone->m_flPitchMax);
    pJiggleBone->data.pitchFriction = pDmeJiggleBone->m_flPitchFriction;
    pJiggleBone->data.pitchBounce = pDmeJiggleBone->m_flPitchBounce;

    if (pDmeJiggleBone->m_bFlexible) {
        if (pDmeJiggleBone->m_bRigid) {
            MdlWarning("2001: Jiggle Bone {}: Both flexible and rigid set, ignoring rigid\n", pName);
        }

        pJiggleBone->data.flags |= (JiggleBoneFlags::IsFlexible);
        pJiggleBone->data.flags |= (pDmeJiggleBone->m_bPitchConstrained
                                        ? JiggleBoneFlags::HasPitchConstraint
                                        : JiggleBoneFlags::None);

        // flexible parameters - I think damping should be clamped [0, 10] but code
        // in studiomdl looks incorrect and clamps 0, 1000.0f
        pJiggleBone->data.yawStiffness = clamp(pDmeJiggleBone->m_flYawStiffness.Get(), 0.0f, 1000.0f);
        pJiggleBone->data.yawDamping = clamp(pDmeJiggleBone->m_flYawDamping.Get(), 0.0f, 10.0f);

        pJiggleBone->data.pitchStiffness = clamp(pDmeJiggleBone->m_flPitchStiffness.Get(), 0.0f, 1000.0f);
        pJiggleBone->data.pitchDamping = clamp(pDmeJiggleBone->m_flPitchDamping.Get(), 0.0f, 10.0f);

        pJiggleBone->data.alongStiffness = clamp(pDmeJiggleBone->m_flAlongStiffness.Get(), 0.0f, 1000.0f);
        pJiggleBone->data.alongDamping = clamp(pDmeJiggleBone->m_flAlongDamping.Get(), 0.0f, 10.0f);
    }
    else if (pDmeJiggleBone->m_bRigid) {
        pJiggleBone->data.flags |= (JiggleBoneFlags::IsFlexible | JiggleBoneFlags::HasLengthConstraint);
    }
    else {
        // TODO: Is neither rigid or flexible an error?
    }

    if (pDmeJiggleBone->m_bBaseSpring) {
        // flexible parameters - I think damping should be clamped [0, 10] but code
        // in studiomdl looks incorrect and clamps 0, 1000.0f
        pJiggleBone->data.baseMass = pDmeJiggleBone->m_flBaseMass;
        pJiggleBone->data.baseStiffness = clamp(pDmeJiggleBone->m_flBaseStiffness.Get(), 0.0f, 1000.0f);
        pJiggleBone->data.baseDamping = clamp(pDmeJiggleBone->m_flBaseStiffness.Get(), 0.0f, 10.0f);

        pJiggleBone->data.baseMinLeft = pDmeJiggleBone->m_flBaseYawMin;
        pJiggleBone->data.baseMaxLeft = pDmeJiggleBone->m_flBaseYawMax;
        pJiggleBone->data.baseLeftFriction = pDmeJiggleBone->m_flBaseYawFriction;

        pJiggleBone->data.baseMinUp = pDmeJiggleBone->m_flBasePitchMin;
        pJiggleBone->data.baseMaxUp = pDmeJiggleBone->m_flBasePitchMax;
        pJiggleBone->data.baseUpFriction = pDmeJiggleBone->m_flBasePitchFriction;

        pJiggleBone->data.baseMinForward = pDmeJiggleBone->m_flBaseAlongMin;
        pJiggleBone->data.baseMaxForward = pDmeJiggleBone->m_flBaseAlongMax;
        pJiggleBone->data.baseForwardFriction = pDmeJiggleBone->m_flBaseAlongFriction;
    }
}


//-----------------------------------------------------------------------------
// Loads the skeletal hierarchy from the game model, returns bone count
//-----------------------------------------------------------------------------
static bool
AddDagJoint(CDmeModel *pModel, CDmeDag *pDag, std::array<s_node_t,MAXSTUDIOSRCBONES> &pNodes, const int nParentIndex,
            BoneTransformMap_t &boneMap) {
    CDmeTransform *pDmeTransform = pDag->GetTransform();

    if (!pDmeTransform)
        return true;

    // Need room for one implicit bone added
    if (boneMap.m_nBoneCount >= (MAXSTUDIOSRCBONES - 1)) {
        MdlWarning("Ignoring Bone {} and children, too many bones [max can be {}]!\n", pDag->GetName(),
                   MAXSTUDIOSRCBONES - 1);
        return false;
    }

    const int nJointIndex = boneMap.m_nBoneCount++;

    boneMap.m_ppTransforms[nJointIndex] = pDmeTransform;

    if (pModel) {
        const int nFoundIndex = pModel->GetJointIndex(pDag);
        if (nFoundIndex >= 0) {
            boneMap.m_pnDmeModelToMdl[nFoundIndex] = nJointIndex;
            boneMap.m_pnMdlToDmeModel[nJointIndex] = nFoundIndex;
        }
        else {
            MdlWarning("Joint {} doesn't appear in DmeModel[{}].jointList\n", pDag->GetName(), pModel->GetName());
        }
    }

    HandleDmeJiggleBone(pDag);

    Q_strncpy(pNodes[nJointIndex].name, pDag->GetName(), sizeof(pNodes[nJointIndex].name));
    pNodes[nJointIndex].parent = nParentIndex;

    // Now deal with children
    for (int i = 0; i < pDag->GetChildCount(); ++i) {
        CDmeDag *pChild = pDag->GetChild(i);
        if (!pChild)
            continue;

        if (!AddDagJoint(pModel, pChild, pNodes, nJointIndex, boneMap))
            return false;
    }

    return true;
}


//-----------------------------------------------------------------------------
// Main entry point for loading the skeleton
//-----------------------------------------------------------------------------
static int LoadSkeleton(CDmeDag *pRoot, CDmeModel *pModel, std::array<s_node_t,MAXSTUDIOSRCBONES> &pNodes,
                        BoneTransformMap_t &boneMap) {
    vstd::FunctionTimingRecorder _("DMX");
    // Initialize bone indices
    boneMap.m_nBoneCount = 0;
    for (int i = 0; i < MAXSTUDIOSRCBONES; ++i) {
        pNodes[i].name[0] = 0;
        pNodes[i].parent = -1;
        boneMap.m_pnDmeModelToMdl[i] = -1;
        boneMap.m_pnMdlToDmeModel[i] = -1;
        boneMap.m_ppTransforms[i] = nullptr;
    }

    // Don't create joints for the the root dag ever.. just deal with the children
    for (int i = 0; i < pRoot->GetChildCount(); ++i) {
        CDmeDag *pChild = pRoot->GetChild(i);
        if (!pChild)
            continue;

        if (!AddDagJoint(pModel, pChild, pNodes, -1, boneMap))
            return 0;
    }

    // Add a default identity bone used for autoskinning if no joints are specified
    s_nDefaultRootNode = boneMap.m_nBoneCount;
    Q_strncpy(pNodes[s_nDefaultRootNode].name, "defaultRoot", sizeof(pNodes[s_nDefaultRootNode].name));
    pNodes[s_nDefaultRootNode].parent = -1;

    // +1 for the default identity bone just added
    return boneMap.m_nBoneCount + 1;
}


//-----------------------------------------------------------------------------
// Loads the attachments found in the file
//-----------------------------------------------------------------------------
static void LoadAttachments(CDmeDag *pRoot, CDmeDag *pDag, s_source_t *pSource, const bool bStaticProp) {
    CDmeAttachment *pAttachment = CastElement<CDmeAttachment>(pDag->GetShape());
    if (pAttachment && (pDag != pRoot)) {
        int i = pSource->m_Attachments.AddToTail();
        s_attachment_t &attachment = pSource->m_Attachments[i];
        memset(&attachment, 0, sizeof(s_attachment_t));
        Q_strncpy(attachment.name, pAttachment->GetName(), sizeof(attachment.name));
        Q_strncpy(attachment.bonename, pDag->GetName(), sizeof(attachment.bonename));
        SetIdentityMatrix(attachment.local);

        if (bStaticProp) {
            // Static prop will remove all bones so put the attachment transform
            // on the attachment rather than the bone. Also ignore all attachment
            // flags
            pDag->GetAbsTransform(attachment.local);
        }
        else {
            if (pAttachment->m_bIsRigid) {
                attachment.type |= AttachmentType::Rigid;
            }
            if (pAttachment->m_bIsWorldAligned) {
                attachment.flags |= AttachmentFlags::WorldAlign;
            }
        }
    }

    // Don't create joints for the the root dag ever.. just deal with the children
    int nChildCount = pDag->GetChildCount();
    for (int i = 0; i < nChildCount; ++i) {
        CDmeDag *pChild = pDag->GetChild(i);
        if (!pChild)
            continue;

        LoadAttachments(pRoot, pChild, pSource, bStaticProp);
    }
}


//-----------------------------------------------------------------------------
// Loads the bind pose
//-----------------------------------------------------------------------------
static void LoadBindPose(CDmeModel *pModel, const float flScale, const BoneTransformMap_t &boneMap,
                         s_source_t *pSource) {
    s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim(pSource, "BindPose");
    pSourceAnim->startframe = 0;
    pSourceAnim->endframe = 0;
    pSourceAnim->numframes = 1;

    // Default all transforms to identity
    pSourceAnim->rawanim[0] = (s_bone_t *) calloc(pSource->numbones, sizeof(s_bone_t));
    for (int i = 0; i < pSource->numbones; ++i) {
        pSourceAnim->rawanim[0][i].pos.Init();
        pSourceAnim->rawanim[0][i].rot.Init();
    }

    {
        matrix3x4_t jointTransform;

        CDmeTransformList *pBindPose = pModel->FindBaseState("bind");
        for (int nMdlBoneIndex = 0; nMdlBoneIndex < boneMap.m_nBoneCount; ++nMdlBoneIndex) {
            CDmeTransform *pDmeTransform = nullptr;

            const int nDmeModelBoneIndex = boneMap.m_pnMdlToDmeModel[nMdlBoneIndex];
            if (nDmeModelBoneIndex < 0) {
                // No bind pose stored for the specified joint, use the current
                // position of the joint from the skeleton instead
                pDmeTransform = boneMap.m_ppTransforms[nMdlBoneIndex];
            }
            else {
                pDmeTransform = pBindPose
                                    ? pBindPose->GetTransform(nDmeModelBoneIndex)
                                    : pModel->GetJointTransform(
                                        nDmeModelBoneIndex);
            }

            if (pDmeTransform) {
                pDmeTransform->GetTransform(jointTransform);

                s_bone_t &mdlBone = pSourceAnim->rawanim[0][nMdlBoneIndex];
                MatrixAngles(jointTransform, mdlBone.rot, mdlBone.pos);
                mdlBone.pos *= flScale;
            }
            else {
                MdlWarning("Cannot find DmeTransform for MDL Bone {}\n", nMdlBoneIndex);
            }
        }
    }

    Build_Reference(pSource, "BindPose");
}


//-----------------------------------------------------------------------------
// Does a search through connection operators for dependent DmeOperators
//-----------------------------------------------------------------------------
static void GetDependentOperators(CUtlVector<IDmeOperator *> &operatorList, CDmeOperator *pDmeOperator) {
    if (!pDmeOperator || !CastElement<CDmeOperator>(pDmeOperator))
        return;

    for (int i = 0; i < operatorList.Count(); ++i) {
        CDmeOperator *pTmpDmeOperator = CastElement<CDmeOperator>(
            reinterpret_cast<CDmeOperator *>(operatorList[i]));
        if (pTmpDmeOperator && pTmpDmeOperator == pDmeOperator)
            return;
    }

    operatorList.AddToTail(pDmeOperator);

    CUtlVector<CDmAttribute *> outAttrList;
    pDmeOperator->GetOutputAttributes(outAttrList);
    for (int i = 0; i < outAttrList.Count(); ++i) {
        CDmElement *pDmElement = outAttrList[i]->GetOwner();
        if (!pDmElement)
            continue;

        if (pDmElement == pDmeOperator) {
            CUtlVector<CDmElement *> reList0;
            FindReferringElements(reList0, pDmElement, g_pDataModel->GetSymbol("element"));
            for (int j = 0; j < reList0.Count(); ++j) {
                CDmeAttributeReference *pRe0 = CastElement<CDmeAttributeReference>(reList0[j]);
                if (!pRe0)
                    continue;

                CUtlVector<CDmElement *> reList1;
                FindReferringElements(reList1, pRe0, g_pDataModel->GetSymbol("input"));
                for (int k = 0; k < reList1.Count(); ++k) {
                    CDmeConnectionOperator *pRe1 = CastElement<CDmeConnectionOperator>(reList1[k]);
                    if (!pRe1)
                        continue;

                    GetDependentOperators(operatorList, pRe1);
                }
            }
        }
        else {
            GetDependentOperators(operatorList, CastElement<CDmeOperator>(pDmElement));
        }
    }
}


//-----------------------------------------------------------------------------
// Main entry point for loading DMX files
//-----------------------------------------------------------------------------
static void PrepareChannels(
    CUtlVector<IDmeOperator *> &operatorList,
    CDmeChannelsClip *pAnimation) {
    int nChannelsCount = pAnimation->m_Channels.Count();
    for (int i = 0; i < nChannelsCount; ++i) {
        pAnimation->m_Channels[i]->SetMode(CM_PLAY);
        GetDependentOperators(operatorList, pAnimation->m_Channels[i]);
    }
}


//-----------------------------------------------------------------------------
// Update channels so they are in position for the next frame
//-----------------------------------------------------------------------------
static void UpdateChannels(CUtlVector<IDmeOperator *> &operators, CDmeChannelsClip *pAnimation,
                           const DmeTime_t clipTime) {
    int nChannelsCount = pAnimation->m_Channels.Count();
    DmeTime_t channelTime = pAnimation->ToChildMediaTime(clipTime);
    for (int i = 0; i < nChannelsCount; ++i) {
        pAnimation->m_Channels[i]->SetCurrentTime(channelTime);
    }

    // Recompute the position of the joints
    g_pDmElementFramework->SetOperators(operators);
    g_pDmElementFramework->Operate(true);
    g_pDmElementFramework->BeginEdit();
}


//-----------------------------------------------------------------------------
// Initialize the pose for this frame
//-----------------------------------------------------------------------------
static void ComputeFramePose(s_sourceanim_t *pSourceAnim, const int nFrame, const float flScale,
                             BoneTransformMap_t &boneMap) {
    // LoadSkeleton appends the identity "defaultRoot" bone after the joints (numbones == m_nBoneCount + 1): the frame
    // needs a slot for it too, calloc leaves it at the identity
    pSourceAnim->rawanim[nFrame] = (s_bone_t *) calloc(boneMap.m_nBoneCount + 1, sizeof(s_bone_t));

    for (int i = 0; i < boneMap.m_nBoneCount; ++i) {
        matrix3x4_t jointTransform;
        boneMap.m_ppTransforms[i]->GetTransform(jointTransform);
        MatrixAngles(jointTransform, pSourceAnim->rawanim[nFrame][i].rot, pSourceAnim->rawanim[nFrame][i].pos);
        pSourceAnim->rawanim[nFrame][i].pos *= flScale;
    }
}


//-----------------------------------------------------------------------------
// Main entry point for loading animations
//-----------------------------------------------------------------------------
void
LoadAnimations(s_source_t *pSource, CDmeAnimationList *pAnimationList, const float flScale,
               BoneTransformMap_t &boneMap) {
    int nAnimationCount = pAnimationList->GetAnimationCount();
    for (int i = 0; i < nAnimationCount; ++i) {
        CDmeChannelsClip *pAnimation = pAnimationList->GetAnimation(i);

        if (!Q_stricmp(pAnimationList->GetName(), "BindPose")) {
            MdlError("Error: Cannot use \"BindPose\" as an animation name!\n");
            break;
        }

        s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim(pSource, pAnimation->GetName());
        DmeTime_t nStartTime = pAnimation->GetStartTime();
        DmeTime_t nEndTime = pAnimation->GetEndTime();
        int nFrameRateVal = pAnimation->GetValue<int>("frameRate");
        if (nFrameRateVal <= 0) {
            nFrameRateVal = 30;
        }
        DmeFramerate_t nFrameRate = nFrameRateVal;
        pSourceAnim->startframe = nStartTime.CurrentFrame(nFrameRate);
        pSourceAnim->endframe = nEndTime.CurrentFrame(nFrameRate);
        pSourceAnim->numframes = pSourceAnim->endframe - pSourceAnim->startframe + 1;
        CUtlVector<IDmeOperator *> operatorList;
        PrepareChannels(operatorList, pAnimation);
        float flOOFrameRate = 1.0f / (float) nFrameRateVal;
        int nFrame = 0;
        while (nFrame < pSourceAnim->numframes) {
            int nSecond = nFrame / nFrameRateVal;
            int nFraction = nFrame - nSecond * nFrameRateVal;
            DmeTime_t t = nStartTime + DmeTime_t(nSecond * 10000) + DmeTime_t((float) nFraction * flOOFrameRate);
            UpdateChannels(operatorList, pAnimation, t);
            ComputeFramePose(pSourceAnim, nFrame, flScale, boneMap);
            ++nFrame;
        }
    }
}


//-----------------------------------------------------------------------------
// Loads the skeletal hierarchy from the game model, returns bone count
//-----------------------------------------------------------------------------
static void AddFlexKeys(CDmeDag *pRoot, CDmeDag *pDag, CDmeCombinationOperator *pComboOp, s_source_t *pSource) {
    CDmeMesh *pMesh = CastElement<CDmeMesh>(pDag->GetShape());
    if (pMesh && (pDag != pRoot)) {
        int nDeltaStateCount = pMesh->DeltaStateCount();
        for (int i = 0; i < nDeltaStateCount; ++i) {
            CDmeVertexDeltaData *pDeltaState = pMesh->GetDeltaState(i);
            AddFlexKey(pSource, pComboOp, pDeltaState->GetName());
        }
    }

    // Don't create joints for the the root dag ever.. just deal with the children
    int nChildCount = pDag->GetChildCount();
    for (int i = 0; i < nChildCount; ++i) {
        CDmeDag *pChild = pDag->GetChild(i);
        if (!pChild)
            continue;

        AddFlexKeys(pRoot, pChild, pComboOp, pSource);
    }
}

//-----------------------------------------------------------------------------
// Loads all auxilliary model info:
//
//	*	Determine original source files used to generate
//		the current DMX file and schedule them for processing.
//-----------------------------------------------------------------------------
void LoadModelInfo(CDmElement *pRoot, char const *pFullPath) {
    return;
    // Determine original source files and schedule them for processing
    if (CDmElement *pMakeFile = pRoot->GetValueElement<CDmElement>("makefile")) {
        if (CDmAttribute *pSources = pMakeFile->GetAttribute("sources")) {
            CDmrElementArray<CDmElement> arrSources(pSources);
            for (int kk = 0; kk < arrSources.Count(); ++kk) {
                if (CDmElement *pModelSource = arrSources.Element(kk)) {
                    if (char const *szName = pModelSource->GetName()) {
                        std::print("* {}{:c}{}\n", pFullPath, CORRECT_PATH_SEPARATOR, szName);
                    }
                }
            }
        }
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool LoadTwistConstraint(CDmElement *pDmElement) {
    CDmeRigTwistConstraintOperator *pDmeTwist = CastElement<CDmeRigTwistConstraintOperator>(pDmElement);
    if (!pDmeTwist)
        return false;

    if (std::ssize(g_studiomdl.bones.twistbones) == MAXSTUDIOBONES)
        return false;

    CDmeDag *pDmeDagParent = pDmeTwist->GetParentTarget();
    CDmeDag *pDmeDagChild = pDmeTwist->GetChildTarget();

    if (!pDmeDagParent || !pDmeDagChild)
        return false;

    CTwistBone &twistBone = g_studiomdl.bones.twistbones.emplace_back();

    twistBone.m_bInverse = pDmeTwist->GetInverse();
    twistBone.m_vUpVector = pDmeTwist->GetUpAxis();
    V_strncpy(twistBone.m_szParentBoneName, pDmeDagParent->GetName(), ARRAYSIZE(twistBone.m_szParentBoneName));
    twistBone.m_qBaseRotation = twistBone.m_bInverse
                                    ? pDmeTwist->GetParentBindRotation()
                                    : pDmeTwist->GetChildBindRotation();
    V_strncpy(twistBone.m_szChildBoneName, pDmeDagChild->GetName(), ARRAYSIZE(twistBone.m_szChildBoneName));

    for (int i = 0; i < pDmeTwist->SlaveCount(); ++i) {
        CDmeDag *pDmeTwistDag = pDmeTwist->GetSlaveDag(i);
        if (!pDmeTwistDag)
            continue;

        const char *pszTwistTargetName = pDmeTwistDag->GetName();
        if (!pszTwistTargetName || *pszTwistTargetName == '\0')
            continue;

        bool bFound = false;

        // Check to see if this target is already the target of a twist bone
        for (int j = 0; !bFound && j < std::ssize(g_studiomdl.bones.twistbones); ++j) {
            const CTwistBone &tmpTwistBone = g_studiomdl.bones.twistbones[j];
            for (int k = 0; k < std::ssize(tmpTwistBone.m_twistBoneTargets); ++k) {
                if (vstd::iequals(pszTwistTargetName, tmpTwistBone.m_twistBoneTargets[k].m_szBoneName)) {
                    bFound = true;
                    break;
                }
            }
        }

        // A Twist bone is already driving this bone, don't make another
        if (bFound)
            continue;

        s_constraintbonetarget_t &twistBoneTarget = twistBone.m_twistBoneTargets.emplace_back();
        V_strncpy(twistBoneTarget.m_szBoneName, pDmeTwistDag->GetName(), ARRAYSIZE(twistBoneTarget.m_szBoneName));
        twistBoneTarget.m_nBone = -1;
        twistBoneTarget.m_flWeight = pDmeTwist->GetSlaveWeight(i);
        twistBoneTarget.m_vOffset = pDmeTwistDag->GetTransform()->GetPosition();
        twistBoneTarget.m_qOffset = pDmeTwist->GetSlaveBindOrientation(i);
    }

    // No targets, twist constraint is useless
    if (std::ssize(twistBone.m_twistBoneTargets) <= 0) {
        g_studiomdl.bones.twistbones.pop_back();
        return true;
    }

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool LoadBaseConstraintParams(CConstraintBoneBase *pConstraintBone, CDmeRigBaseConstraintOperator *pDmeBaseConstraint) {
    const CDmaElementArray<CDmeConstraintTarget> &constraintTargets = pDmeBaseConstraint->GetTargets();

    if (constraintTargets.Count() <= 0)
        return false;

    const Quaternion qRot = Quaternion(g_studiomdl.script.defaultrotation);

    for (int i = 0; i < constraintTargets.Count(); ++i) {
        CDmeConstraintTarget *pDmeConstraintTarget = constraintTargets[i];
        if (!pDmeConstraintTarget)
            continue;

        CDmeDag *pDmeTargetDag = pDmeConstraintTarget->GetDag();
        if (!pDmeTargetDag)
            continue;

        // Load targets
        s_constraintbonetarget_t &target = pConstraintBone->m_targets[pConstraintBone->m_targets.AddToTail()];
        V_strncpy(target.m_szBoneName, pDmeTargetDag->GetName(), ARRAYSIZE(target.m_szBoneName));
        target.m_nBone = -1;
        target.m_flWeight = pDmeConstraintTarget->GetWeight();
        target.m_qOffset = pDmeConstraintTarget->GetOrientationOffset();

        if (pDmeBaseConstraint->IsA(CDmeRigPointConstraintOperator::GetStaticTypeSymbol())) {
            // Target offsets are in world space for Point constraint
            VectorRotate(pDmeConstraintTarget->GetPositionOfffset(), qRot, target.m_vOffset);
        }
        else {
            target.m_vOffset = pDmeConstraintTarget->GetPositionOfffset();
        }

        //		QuaternionMult( pDmeConstraintTarget->GetOrientationOffset(), qRot, target.m_qOffset );
    }

    if (pConstraintBone->m_targets.Count() <= 0)
        return false;

    const CDmeConstraintSlave *pDmeConstraintSlave = pDmeBaseConstraint->GetConstraintSlave();
    if (!pDmeConstraintSlave)
        return false;

    const CDmeDag *pDmeSlaveDag = pDmeBaseConstraint->GetSlave();
    if (!pDmeSlaveDag)
        return false;

    s_constraintboneslave_t &slave = pConstraintBone->m_slave;

    V_strncpy(slave.m_szBoneName, pDmeSlaveDag->GetName(), ARRAYSIZE(slave.m_szBoneName));
    slave.m_nBone = -1;
    slave.m_vBaseTranslate = pDmeConstraintSlave->GetBasePosition();
    slave.m_qBaseRotation = pDmeConstraintSlave->GetBaseOrientation();

    return true;
}


//-----------------------------------------------------------------------------
// Most constraints don't have any specialized constraint parameters
//-----------------------------------------------------------------------------
template<class S, class T>
static bool LoadSpecializedConstraintParams(S *pDmeConstraint, T *pConstraint) {
    return true;
}


//-----------------------------------------------------------------------------
// Load specialized Aim Constraint parameters
//-----------------------------------------------------------------------------
template<>
bool LoadSpecializedConstraintParams(CDmeRigAimConstraintOperator *pDmeConstraint, CAimConstraint *pConstraint) {
    const Quaternion qRot = Quaternion(g_studiomdl.script.defaultrotation);

    // Set Aim Constraint Parameters
    pConstraint->m_qAimOffset = pDmeConstraint->GetValue<Quaternion>("aimOffset");

    // Up vectors are specified in world space
    VectorRotate(pDmeConstraint->GetValue<Vector>("upVector"), qRot, pConstraint->m_vUpVector);

    CDmElement *pDmeUpSpaceTarget = pDmeConstraint->GetValueElement<CDmElement>("upSpaceTarget");
    if (pDmeUpSpaceTarget) {
        V_strncpy(pConstraint->m_szUpSpaceTargetBone, pDmeUpSpaceTarget->GetName(),
                  ARRAYSIZE(pConstraint->m_szUpSpaceTargetBone));
    }
    pConstraint->m_nUpSpaceTargetBone = -1;

    pConstraint->m_nUpType = pDmeConstraint->GetValue<int>("upType");

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static bool FindDuplicateConstraint(const CConstraintBoneBase *pConstraintA) {
    for (int i = 0; i < std::ssize(g_studiomdl.bones.constraintBones); ++i) {
        const CConstraintBoneBase *pConstraintB = g_studiomdl.bones.constraintBones[i];

        if (*pConstraintA == *pConstraintB)
            return true;
    }

    return false;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class S, class T>
static bool LoadConstraint(CDmElement *pDmElement) {
    if (std::ssize(g_studiomdl.bones.constraintBones) == MAXSTUDIOBONES) {
        MdlError("Too Many Constraint Bones, Max: {}\n", MAXSTUDIOBONES);
        return false;
    }

    S *pDmeConstraint = CastElement<S>(pDmElement);
    if (!pDmeConstraint)
        return false;

    T *pConstraint = new T();
    if (!pConstraint)
        return false;

    if (!LoadBaseConstraintParams(pConstraint, pDmeConstraint)) {
        delete pConstraint;
        return false;
    }

    if (!LoadSpecializedConstraintParams(pDmeConstraint, pConstraint)) {
        delete pConstraint;
        return false;
    }

    if (FindDuplicateConstraint(pConstraint)) {
        // Delete this
        delete pConstraint;
    }
    else {
        g_studiomdl.bones.constraintBones.push_back(pConstraint);
    }

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void LoadConstraints(CDmElement *pDmeRoot) {
    if (!pDmeRoot)
        return;

    CDmAttribute *pDmeConstraintsAttr = pDmeRoot->GetAttribute("constraints", AT_ELEMENT_ARRAY);
    if (!pDmeConstraintsAttr)
        return;

    CDmrElementArray<CDmElement> constraints(pDmeConstraintsAttr);
    for (int i = 0; i < constraints.Count(); ++i) {
        if (LoadTwistConstraint(constraints[i]))
            continue;

        if (LoadConstraint<CDmeRigPointConstraintOperator, CPointConstraint>(constraints[i]))
            continue;

        if (LoadConstraint<CDmeRigOrientConstraintOperator, COrientConstraint>(constraints[i]))
            continue;

        if (LoadConstraint<CDmeRigAimConstraintOperator, CAimConstraint>(constraints[i]))
            continue;

        if (LoadConstraint<CDmeRigParentConstraintOperator, CParentConstraint>(constraints[i]))
            continue;

        Error("TODO: Support Constraint: %s\n", constraints[i]->GetName());
    }
}


//-----------------------------------------------------------------------------
// Load model and skeleton
//-----------------------------------------------------------------------------
bool LoadModelAndSkeleton(s_source_t *pSource, BoneTransformMap_t &boneMap, CDmeDag *pSkeleton, CDmeModel *pModel,
                          CDmeCombinationOperator *pCombinationOperator, const bool bStaticProp) {
    vstd::FunctionTimingRecorder _("DMX");
    s_DeltaStates.RemoveAll();
    s_Balance.RemoveAll();
    s_Speed.RemoveAll();
    s_UniqueVertices.RemoveAll();
    s_UniqueVerticesMap.RemoveAll();

    if (!pSkeleton)
        return false;

    // BoneRemap[bone index in file] == bone index in studiomdl
    pSource->numbones = LoadSkeleton(pSkeleton, pModel, pSource->localBone, boneMap);
    if (pSource->numbones == 0)
        return false;

    g_studiomdl.input.faces.clear();
    if (pModel) {
        if (pCombinationOperator) {
            pCombinationOperator->GenerateWrinkleDeltas(false);
        }
        LoadBindPose(pModel, g_studiomdl.script.currentscale, boneMap, pSource);
        if (!LoadMeshes(pModel, g_studiomdl.script.currentscale, boneMap.m_pnDmeModelToMdl, pSource))
            return false;

        UnifyIndices(pSource);
        BuildVertexAnimations(pSource);
        BuildIndividualMeshes(pSource);
    }

    if (g_studiomdl.input.faces.empty() && pSource->numbones == 1 && !V_strcmp(
            pSource->localBone[0].name, "defaultRoot")) {
        MdlError("Error - dmx has no contents: {}\n", pSource->filename);
    }

    if (pCombinationOperator) {
        AddFlexKeys(pModel, pModel, pCombinationOperator, pSource);
        AddCombination(pSource, pCombinationOperator);
    }

    LoadAttachments(pSkeleton, pSkeleton, pSource, bStaticProp);

    return true;
}


//-----------------------------------------------------------------------------
// Given the s_model_t pointer, finds the index of it in g_studiomdl.body.model
// Returns -1 if it cannot be found
//-----------------------------------------------------------------------------
static int FindModelIndex(s_model_t *pModel) {
    if (pModel) {
        for (int i = 0; i < std::ssize(g_studiomdl.body.model); ++i) {
            if (g_studiomdl.body.model[i] == pModel)
                return i;
        }

        MdlWarning("Cannot Find s_model_t: \"{}\" in g_studiomdl.body.model\n", pModel->name);
    }

    return -1;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static bool AddFlexKey(
    s_source_t *pSource, s_model_t *pModel,
    const char *pszAnimationName,
    const int nFlexDesc,
    const int nFlexPair,
    const EyelidType_t nEyelidType,
    const float flLowererHeight,
    const float flNeutralHeight,
    const float flRaiserHeight,
    const float flSplit = 0.0f,
    const float flDecay = 1.0f,
    const int nFrame = 0) // DMX frame is always 0
{
    if (g_studiomdl.flex.flexkeys.size() >= g_studiomdl.flex.flexkeys.capacity()) {
        MdlError("Too Many Flex Keys, Cannot Add {}\n", "TODO: FlexKeyName");
        return false;
    }

    const int nModelIndex = FindModelIndex(pModel);

    s_flexkey_t *pFlexKey = &g_studiomdl.flex.flexkeys.emplace_back(s_flexkey_t{
        .flexdesc = nFlexDesc,
        .flexpair = nFlexPair,
        .source = pSource,
        .imodel = nModelIndex,
        .frame = nFrame,
        .split = flSplit,
        .decay = flDecay,
    });
    V_strncpy(pFlexKey->animationname, pszAnimationName, sizeof(pFlexKey->animationname));

    switch (nEyelidType) {
        case kLowerer:
            pFlexKey->target0 = -11;
            pFlexKey->target1 = -10;
            pFlexKey->target2 = flLowererHeight;
            pFlexKey->target3 = flNeutralHeight;
            break;
        case kNeutral:
            pFlexKey->target0 = flLowererHeight;
            pFlexKey->target1 = flNeutralHeight;
            pFlexKey->target2 = flNeutralHeight;
            pFlexKey->target3 = flRaiserHeight;
            break;
        case kRaiser:
            pFlexKey->target0 = flNeutralHeight;
            pFlexKey->target1 = flRaiserHeight;
            pFlexKey->target2 = 10;
            pFlexKey->target3 = 11;
            break;
    }

    // Check for a duplicate
    for (int i = 0; i < g_studiomdl.flex.flexkeys.size(); ++i) {
        const s_flexkey_t *pTmpFlexKey = &(g_studiomdl.flex.flexkeys[i]);

        if (vstd::iequals(pFlexKey->animationname, pTmpFlexKey->animationname) &&
            pFlexKey->flexdesc == pTmpFlexKey->flexdesc &&
            pFlexKey->flexpair == pTmpFlexKey->flexpair &&
            pFlexKey->frame == pTmpFlexKey->frame &&
            pFlexKey->target0 == pTmpFlexKey->target0 &&
            pFlexKey->target1 == pTmpFlexKey->target1 &&
            pFlexKey->target2 == pTmpFlexKey->target2 &&
            pFlexKey->target3 == pTmpFlexKey->target3) {
            // Duplicate, get rid of it
            V_memset(pFlexKey, 0, sizeof(s_flexkey_t));
            g_studiomdl.flex.flexkeys.pop_back();
            return false;
        }
    }

    // Not a duplicate, keep it
    return true;
}


//-----------------------------------------------------------------------------
// In studiomdl.cpp
//-----------------------------------------------------------------------------
const s_sourceanim_t *GetNewStyleSourceVertexAnim(s_source_t *pSource, const char *pszVertexAnimName);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static bool LoadEyelid(s_model_t *pModel, CDmeEyelid *pDmeEyelid) {
    if (!pModel || !pDmeEyelid)
        return false;

    s_source_t *pSource = pModel->source;
    if (!pSource)
        return false;

    const bool bUpper = pDmeEyelid->m_bUpper.Get();

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

    eyelidData[kLowerer].m_pSourceAnim = GetNewStyleSourceVertexAnim(pSource, pDmeEyelid->m_sLowererFlex.Get());
    eyelidData[kLowerer].m_flTarget = pDmeEyelid->m_flLowererHeight.Get();
    eyelidData[kNeutral].m_pSourceAnim = GetNewStyleSourceVertexAnim(pSource, pDmeEyelid->m_sNeutralFlex.Get());
    eyelidData[kNeutral].m_flTarget = pDmeEyelid->m_flNeutralHeight.Get();
    eyelidData[kRaiser].m_pSourceAnim = GetNewStyleSourceVertexAnim(pSource, pDmeEyelid->m_sRaiserFlex.Get());
    eyelidData[kRaiser].m_flTarget = pDmeEyelid->m_flRaiserHeight.Get();

    // Add a flexdesc for <type>_right & <type>_left
    // Where <type> is "upper" or "lower"
    int nRightLeftBaseDesc[kRightLeftTypeCount] = {-1, -1};

    const char *sRightBaseDesc = bUpper ? "upper_right" : "lower_right";
    nRightLeftBaseDesc[kRight] = Add_Flexdesc(sRightBaseDesc);

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        CUtlString sRightLocalDesc = sRightBaseDesc;
        sRightLocalDesc += "_";
        sRightLocalDesc += eyelidData[i].m_pszSuffix;
        eyelidData[i].m_nFlexDesc[kRight] = Add_Flexdesc(sRightLocalDesc.Get());
    }

    const char *sLeftBaseDesc = bUpper ? "upper_left" : "lower_left";
    nRightLeftBaseDesc[kLeft] = Add_Flexdesc(sLeftBaseDesc);

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        CUtlString sLeftLocalDesc = sLeftBaseDesc;
        sLeftLocalDesc += "_";
        sLeftLocalDesc += eyelidData[i].m_pszSuffix;
        eyelidData[i].m_nFlexDesc[kLeft] = Add_Flexdesc(sLeftLocalDesc.Get());
    }

    for (int i = 0; i < kEyelidTypeCount; ++i) {
        if (!AddFlexKey(pSource, pModel,
                        eyelidData[i].m_pSourceAnim->animationname,
                        nRightLeftBaseDesc[kLeft],
                        nRightLeftBaseDesc[kRight],
                        EyelidType_t(i),
                        eyelidData[kLowerer].m_flTarget,
                        eyelidData[kNeutral].m_flTarget,
                        eyelidData[kRaiser].m_flTarget)) {
            // Flex Keys Already Added - Eyelid is a duplicate
            return false;
        }
    }

    bool bRightOk = false;
    bool bLeftOk = false;

    for (int i = 0; i < pModel->eyeballs.size(); ++i) {
        s_eyeball_t *pEyeball = &(pModel->eyeballs[i]);
        if (!pEyeball)
            continue;

        RightLeftType_t nRightLeftIndex = kRight;
        if (vstd::iequals(pDmeEyelid->m_sRightEyeballName.Get(), pEyeball->name)) {
            nRightLeftIndex = kRight;
            bRightOk = true;
        }
        else if (vstd::iequals(pDmeEyelid->m_sLeftEyeballName, pEyeball->name)) {
            nRightLeftIndex = kLeft;
            bLeftOk = true;
        }
        else {
            MdlWarning("Unknown Eyeball: {}\n", pEyeball->name);
            continue;
        }

        for (int j = 0; j < kEyelidTypeCount; ++j) {
            if (fabs(eyelidData[j].m_flTarget) > pEyeball->radius) {
                MdlError("Eyelid \"{}\" {} {:.1f} out of range (+-{:.1f})\n", bUpper ? "upper" : "lower",
                         eyelidData[j].m_pszSuffix, eyelidData[j].m_flTarget, pEyeball->radius);
            }
        }

        if (bUpper) {
            pEyeball->upperlidflexdesc = nRightLeftBaseDesc[nRightLeftIndex];
            for (int j = 0; j < kEyelidTypeCount; ++j) {
                pEyeball->upperflexdesc[j] = eyelidData[j].m_nFlexDesc[nRightLeftIndex];
                pEyeball->uppertarget[j] = eyelidData[j].m_flTarget;
            }
        }
        else {
            pEyeball->lowerlidflexdesc = nRightLeftBaseDesc[nRightLeftIndex];
            for (int j = 0; j < kEyelidTypeCount; ++j) {
                pEyeball->lowerflexdesc[j] = eyelidData[j].m_nFlexDesc[nRightLeftIndex];
                pEyeball->lowertarget[j] = eyelidData[j].m_flTarget;
            }
        }
    }

    if (!bRightOk) {
        MdlError("Could not find right eye \"{}\"\n", pDmeEyelid->m_sRightEyeballName.Get());
        return false;
    }

    if (!bLeftOk) {
        MdlError("Could not find left eye \"{}\"\n", pDmeEyelid->m_sLeftEyeballName.Get());
        return false;
    }

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static bool LoadMouth(CDmeMouth *pDmeMouth) {
    if (!pDmeMouth)
        return false;

    const int nMouthIndex = pDmeMouth->m_nMouthNumber.Get();

    // Check if mouth is already defined... not sure why people need to specify a mouth number
    // in the QC though.

    if (std::ssize(g_studiomdl.flex.mouth) > nMouthIndex)
        return false;

    s_mouth_t *pMouth = &g_studiomdl.flex.mouth[nMouthIndex]; // grows the mouth list
    pMouth->flexdesc = Add_Flexdesc(pDmeMouth->m_sFlexControllerName.Get());
    V_strncpy(pMouth->bonename, pDmeMouth->m_sBoneName, sizeof(pMouth->bonename));
    pMouth->forward = pDmeMouth->m_vForward.Get(); // TODO: Adjust Y Up?

    return false;
}

//-----------------------------------------------------------------------------
// Loads Eyeballs
//-----------------------------------------------------------------------------
void LoadEyeballs(s_source_t *pSource, s_model_t *pModel, CDmrElementArray<CDmElement> &elementArray) {
    if (!pSource || !pModel || elementArray.Count() <= 0)
        return;

    Assert(pModel->source == NULL || pModel->source == pSource);

    matrix3x4_t mDefRot;
    AngleMatrix(g_studiomdl.script.defaultrotation, mDefRot);
    Vector vTmp;

    for (int i = 0; i < elementArray.Count(); ++i) {
        CDmeEyeball *pDmeEyeball = CastElement<CDmeEyeball>(elementArray.Element(i));
        if (!pDmeEyeball)
            continue;

        if (pModel->eyeballs.full()) {
            MdlWarning("1100: Max number of eyeballs reached for model {}, ignoring eyeball {}\n", pModel->name,
                       pDmeEyeball->GetName());
            continue;
        }

        int nFoundBoneIndex = -1;
        for (int nSearchBoneIndex = 0; nSearchBoneIndex < pSource->numbones; ++nSearchBoneIndex) {
            if (vstd::iequals(pSource->localBone[nSearchBoneIndex].name, pDmeEyeball->m_sParentBoneName.Get())) {
                nFoundBoneIndex = nSearchBoneIndex;
                break;
            }
        }

        if (nFoundBoneIndex < 0) {
            MdlWarning("1101: Couldn't find bone {} on model {}, ignoring eyeball {}\n",
                       pDmeEyeball->m_sParentBoneName.Get(), pModel->name, pDmeEyeball->GetName());
            continue;
        }

        const char *pszMaterialName = pDmeEyeball->m_sMaterialName.Get();
        const bool bRelative = (strchr(pszMaterialName, '/') || strchr(pszMaterialName, '\\')) ? true : false;

        const int nSearchMeshMatIndex = UseTextureAsMaterial(
            LookupTexture(pDmeEyeball->m_sMaterialName.Get(), bRelative));
        const int nFoundMeshMatIndex = FindSourceMeshIndex(pSource, nSearchMeshMatIndex); // mesh slot

        if (nFoundMeshMatIndex < 0) {
            MdlWarning("1102: Couldn't find eyeball material {} on model {}, ignoring eyeball {}\n",
                       pDmeEyeball->m_sMaterialName.Get(), pModel->name, pDmeEyeball->GetName());
            continue;
        }

        s_eyeball_t eyeball{
            .name = pDmeEyeball->GetName(),
            .bone = nFoundBoneIndex,
            .zoffset = tan(vstd::deg2rad(pDmeEyeball->m_flYawAngle.Get())),
            .radius = pDmeEyeball->m_flRadius.Get(),
            .mesh = nFoundMeshMatIndex,
            .iris_scale = 1.0f / pDmeEyeball->m_flIrisScale,
        };

        // translate eyeball into bone space
        VectorITransform(pDmeEyeball->m_vPosition.Get(), pSource->boneToPose[eyeball.bone], eyeball.org);

        VectorIRotate(Vector(0, 0, 1), mDefRot, vTmp);
        VectorIRotate(vTmp, pSource->boneToPose[eyeball.bone], eyeball.up);

        VectorIRotate(Vector(1, 0, 0), mDefRot, vTmp);
        VectorIRotate(vTmp, pSource->boneToPose[eyeball.bone], eyeball.forward);

        // Not applicable
        eyeball.upperlidflexdesc = -1;
        eyeball.lowerlidflexdesc = -1;

        bool bOk = true;

        // Check for a duplicate eyeball
        for (int j = 0; j < pModel->eyeballs.size(); ++j) {
            s_eyeball_t *pTmp = &(pModel->eyeballs[j]);
            if (vstd::iequals(eyeball.name, pTmp->name)) {
                // TODO: Check and warn about duplicate eyeballs with mis-matched parameters
                bOk = false;
                break;
            }
        }

        if (bOk) {
            // Keep eyeball
            pModel->eyeballs.emplace_back(eyeball);
        }
    }

    // Make the standard flex controllers for eyes if required
    static const char *szEyesFlexControllers[] = {
        "eyes_updown",
        "eyes_rightleft"
    };

    for (int nNewFlexIndex = 0; nNewFlexIndex < ARRAYSIZE(szEyesFlexControllers); ++nNewFlexIndex) {
        bool bHasEyeFlexController = false;

        for (int nFlexIndex = 0; nFlexIndex < g_studiomdl.flex.flexcontrollers.size(); ++nFlexIndex) {
            if (!Q_strcmp(szEyesFlexControllers[nNewFlexIndex], g_studiomdl.flex.flexcontrollers[nFlexIndex].name)) {
                bHasEyeFlexController = true;
                break;
            }
        }

        // The flex controller range for eyes_updown & eyes_rightleft is default [-45, 45] because it's clamped by eyesMaxDeflection
        // and changing it based on max deflection would cause animatin changes since flex controller values are normalized [0, 1]
        // [-45, 45 ] gives a maxium range that's useful

        if (!bHasEyeFlexController) {
            if (g_studiomdl.flex.flexcontrollers.size() >= g_studiomdl.flex.flexcontrollers.capacity()) {
                MdlWarning("1103: Couldn't make eyes flexcontroller {}, too many flex controllers defined\n",
                           szEyesFlexControllers[nNewFlexIndex]);
                continue;
            }
            g_studiomdl.flex.flexcontrollers.emplace_back(s_flexcontroller_t{
                .name = szEyesFlexControllers[nNewFlexIndex],
                .type = "eyes",
                .min = -45.0f,
                .max = 45.0f,
            });
        }
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void LoadQcModelElements(s_source_t *pSource, s_model_t *pModel, CDmeModel *pDmeModel) {
    if (!pModel || !pDmeModel)
        return;

    CDmAttribute *pQcModelElementsAttr = pDmeModel->GetAttribute("qcModelElements", AT_ELEMENT_ARRAY);
    if (!pQcModelElementsAttr)
        return;

    CDmrElementArray<CDmElement> qcModelElements(pQcModelElementsAttr);

    LoadEyeballs(pSource, pModel, qcModelElements);

    for (int i = 0; i < qcModelElements.Count(); ++i) {
        LoadEyelid(pModel, CastElement<CDmeEyelid>(qcModelElements.Element(i)));
    }

    for (int i = 0; i < qcModelElements.Count(); ++i) {
        LoadMouth(CastElement<CDmeMouth>(qcModelElements.Element(i)));
    }
}


//-----------------------------------------------------------------------------
// Main entry point for loading DMX files
//-----------------------------------------------------------------------------
int Load_DMX(s_source_t *pSource) {
    vstd::FunctionTimingRecorder _("Parse");
    DmFileId_t fileId;

    // use the full search tree, including mod hierarchy to find the file
    std::string fullPath;
    if (!GetGlobalFilePath(pSource->filename, fullPath))
        return 0;
    // When reading, keep the CRLF; this will make ReadFile read it in binary format
    // and also append a couple 0s to the end of the buffer.
    CDmElement *pRoot;
    if (g_pDataModel->RestoreFromFile(fullPath.c_str(), nullptr, nullptr, &pRoot) == DMFILEID_INVALID)
        return 0;

    if (!g_studiomdl.options.quiet) {
        Msg("DMX Model %s\n", fullPath.c_str());
    }

    // Load model info
    LoadModelInfo(pRoot, fullPath.c_str());

    // Load constraints
    LoadConstraints(pRoot);

    // Extract out the skeleton
    // BoneRemap[bone index in file] == bone index in studiomdl
    CDmeDag *pSkeleton = pRoot->GetValueElement<CDmeDag>("skeleton");
    CDmeModel *pModel = pRoot->GetValueElement<CDmeModel>("model");
    CDmeCombinationOperator *pCombinationOperator = pRoot->GetValueElement<CDmeCombinationOperator>(
        "combinationOperator");
    BoneTransformMap_t boneMap;
    if (!LoadModelAndSkeleton(pSource, boneMap, pSkeleton, pModel, pCombinationOperator, false))
        goto dmxError;
    LoadQcModelElements(pSource, g_pCurrentModel, pModel);

    if (CDmeAnimationList *pAnimationList = pRoot->GetValueElement<CDmeAnimationList>("animationList")) {
        LoadAnimations(pSource, pAnimationList, g_studiomdl.script.currentscale, boneMap);
    }

    fileId = pRoot->GetFileId();
    g_pDataModel->RemoveFileId(fileId);
    return 1;

dmxError:
    fileId = pRoot->GetFileId();
    g_pDataModel->RemoveFileId(fileId);
    return 0;
}


//-----------------------------------------------------------------------------
// Main entry point for loading FBX files
//-----------------------------------------------------------------------------
#if FBX_SUPPORT
int Load_FBX(s_source_t *pSource) {
    // use the full search tree, including mod hierarchy to find the file
    std::string fullPath;
    if (!GetGlobalFilePath(pSource->filename, fullPath))
        return 0;
    const char *pFullPath = fullPath.c_str();

    CDmFbxSerializer dmFbxSerializer;
    dmFbxSerializer.m_eOptUpAxis = CDmeAxisSystem::AS_AXIS_Y;
    dmFbxSerializer.m_eOptForwardParity = CDmeAxisSystem::AS_PARITY_ODD;
    CDmElement *pRoot = dmFbxSerializer.ReadFBX(pFullPath);
    if (!pRoot)
        return 0;

    if (!g_quiet) {
        Msg("FBX Model %s\n", pFullPath);
    }

    // Load model info
    LoadModelInfo(pRoot, pFullPath);

    // Load constraints
    LoadConstraints(pRoot);

    // Extract out the skeleton
    // BoneRemap[bone index in file] == bone index in studiomdl
    CDmeDag *pSkeleton = pRoot->GetValueElement<CDmeDag>("skeleton");
    CDmeModel *pModel = pRoot->GetValueElement<CDmeModel>("model");
    CDmeCombinationOperator *pCombinationOperator = pRoot->GetValueElement<CDmeCombinationOperator>(
        "combinationOperator");
    BoneTransformMap_t boneMap;
    int nReturn = 0;
    if (LoadModelAndSkeleton(pSource, boneMap, pSkeleton, pModel, pCombinationOperator, false)) {
        LoadQcModelElements(pSource, g_pCurrentModel, pModel);

        CDmeAnimationList *pAnimationList = pRoot->GetValueElement<CDmeAnimationList>("animationList");
        if (pAnimationList) {
            LoadAnimations(pSource, pAnimationList, g_studiomdl.script.currentscale, boneMap);
        }

        if (CommandLine()->FindParm("-debugfbx2dmx"))
            g_pDataModel->SaveToFile(CUtlString(pFullPath).StripExtension() + ".fbx2dmx.dmx", NULL, "keyvalues2",
                                     "model", pRoot);
        nReturn = 1; // loaded ok
    }
    g_pDataModel->UnloadFile/*RemoveFileId?*/(pRoot->GetFileId());

    return nReturn;
}
#endif

//-----------------------------------------------------------------------------
// Declare it so we can call it, defined in studiomdl.cpp
//-----------------------------------------------------------------------------
extern void ProcessModelName(const char *pMdlName);

//===== Copyright � 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: Loads mesh data from dmx files
//
// $NoKeywords: $
//
//===========================================================================//


// Valve includes

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


// Local includes
#include "studiomdl/studiomdl.h"
#include "tier1/fmtstr.h"
#include "mdlobjects/dmebonemask.h"
#include "mdlobjects/dmebonemasklist.h"
#include "mdlobjects/dmeposeparameterlist.h"
#include "studiomdl/studiomdl_global_state.hpp"


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
static DeltaState_t *FindOrAddDeltaState(const char *pDeltaStateName, int nBaseStateVertexCount) {
    int nCount = s_DeltaStates.Count();
    for (int i = 0; i < nCount; ++i) {
        if (!Q_stricmp(s_DeltaStates[i].m_Name, pDeltaStateName)) {
            MdlWarning("Unsupported duplicate delta state named \"%s\" in DMX file\n", pDeltaStateName);

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
        vert.v = g_StudioMdlContext.numverts + positionIndices[i];
        vert.n = (nNormalCount > 0) ? g_StudioMdlContext.numnormals + normalIndices[i] : -1;
        vert.t[0] = (nTexcoordCount > 0) ? g_StudioMdlContext.numtexcoords[0] + texcoordIndices[i] : -1;
        vert.balance = s_Balance.Count() + ((nBalanceCount > 0) ? balanceIndices[i] : 0);
        vert.speed = s_Speed.Count() + ((nSpeedCount > 0) ? speedIndices[i] : 0);
        for (int j = 1; j < MAXSTUDIOTEXCOORDS; ++j) {
            vert.t[j] = (nExtraTexcoordCount[j - 1] > 0)
                            ? g_StudioMdlContext.numtexcoords[j] + extraTexcoordIndices[j - 1]->Element(i)
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
LoadVertices(CDmeDag *pDmeDag, CDmeVertexData *pBindState, const matrix3x4_t &mat, float flScale, int nBoneAssign,
             int *pBoneRemap, s_source_t *pSource) {
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

    int nCount = positions.Count();

    int nJointCount = pBindState->HasSkinningData() ? pBindState->JointCount() : 0;
    if (nJointCount > MAXSTUDIOBONEWEIGHTS) {
        MdlError("Too many bone influences per vertex!\n");
        return false;
    }

    if (nJointCount <= 0 && nBoneAssign == s_nDefaultRootNode && pDmeDag) {
        // Use the bone created for the DmeDag node of the DmeMesh
        for (int i = 0; i < pSource->numbones; ++i) {
            if (!Q_strcmp(pDmeDag->GetName(), pSource->localBone[i].name)) {
                nBoneAssign = i;
                break;
            }
        }
    }

    bool pbWarnmap[MAXSTUDIOBONES];
    Q_memset(pbWarnmap, 0, MAXSTUDIOBONES * sizeof(bool));

    float *pWeightBuf = (float *) malloc(nJointCount * sizeof(float));
    int *pIndexBuf = (int *) malloc(nJointCount * sizeof(int));

    // Copy positions + bone info
    for (int i = 0; i < nCount; ++i) {
        // NOTE: The transform transforms the positions into the bind space
        VectorTransform(positions[i], mat, g_StudioMdlContext.vertex[g_StudioMdlContext.numverts]);
        g_StudioMdlContext.vertex[g_StudioMdlContext.numverts] *= flScale;
        if (nJointCount == 0) {
            g_StudioMdlContext.bone[g_StudioMdlContext.numverts].numbones = 1;
            g_StudioMdlContext.bone[g_StudioMdlContext.numverts].bone[0] = nBoneAssign;
            g_StudioMdlContext.bone[g_StudioMdlContext.numverts].weight[0] = 1.0;
        }
        else {
            const float *pJointWeights = pBindState->GetJointWeightData(i);
            const int *pJointIndices = pBindState->GetJointIndexData(i);

            memcpy(pWeightBuf, pJointWeights, nJointCount * sizeof(float));
            memcpy(pIndexBuf, pJointIndices, nJointCount * sizeof(int));

            int nBoneCount = SortAndBalanceBones(nJointCount, MAXSTUDIOBONEWEIGHTS, pIndexBuf, pWeightBuf);
            int nBoneIndex = -1;

            g_StudioMdlContext.bone[g_StudioMdlContext.numverts].numbones = nBoneCount;
            for (int j = 0; j < nBoneCount; ++j) {
                nBoneIndex = pBoneRemap[pIndexBuf[j]];
                if (nBoneIndex < 0) {
                    if (pIndexBuf[j] < MAXSTUDIOBONES && !pbWarnmap[pIndexBuf[j]]) {
                        MdlWarning(
                            "DmeMesh[%s] Verts Assigned To DmeModel.jointList[%d] Which Isn't Mapped To The Dag Hierarchy\n",
                            pDmeDag->GetName(), pIndexBuf[j]);
                        pbWarnmap[pIndexBuf[j]] = true;
                    }
                    g_StudioMdlContext.bone[g_StudioMdlContext.numverts].bone[j] = nBoneAssign;
                }
                else {
                    g_StudioMdlContext.bone[g_StudioMdlContext.numverts].bone[j] = nBoneIndex;
                }
                g_StudioMdlContext.bone[g_StudioMdlContext.numverts].weight[j] = pWeightBuf[j];
            }
        }
        ++g_StudioMdlContext.numverts;
    }

    free(pWeightBuf);
    free(pIndexBuf);

    // Copy normals
    Vector vNormal;
    nCount = normals.Count();
    for (int i = 0; i < nCount; ++i) {
        VectorCopy(normals[i], vNormal);
        VectorNormalize(vNormal);
        if (fabs(VectorLength(vNormal) - 1.0f) > 0.01) {
            MdlWarning("Non-Unit Length Normal [%d] < %8.6f %8.6f %8.6f >\n", i, vNormal.x, vNormal.y, vNormal.z);
        }
        VectorRotate(vNormal, normalMat, g_StudioMdlContext.normal[g_StudioMdlContext.numnormals]);
        ++g_StudioMdlContext.numnormals;
    }

    // Copy texcoords
    nCount = texcoords.Count();
    bool bFlipVCoordinate = pBindState->IsVCoordinateFlipped();
    for (int i = 0; i < nCount; ++i) {
        g_StudioMdlContext.texcoord[0][g_StudioMdlContext.numtexcoords[0]].x = texcoords[i].x;
        g_StudioMdlContext.texcoord[0][g_StudioMdlContext.numtexcoords[0]].y = bFlipVCoordinate
                                                                                   ? 1.0f - texcoords[i].y
                                                                                   : texcoords[i].y;
        ++g_StudioMdlContext.numtexcoords[0];
    }

    // Check for additional texcoords
    for (int i = 1; i < MAXSTUDIOTEXCOORDS; ++i) {
        g_StudioMdlContext.numtexcoords[i] = 0;
        FieldIndex_t nFieldIndex = pBindState->FindFieldIndex(CFmtStr("texcoord$%d", i).Get());
        if (nFieldIndex > -1) {
            CDmrArrayConst<Vector2D> vertexData = pBindState->GetVertexData(nFieldIndex);
            const CUtlVector<Vector2D> &extraTexcoords = vertexData.Get();
            nCount = extraTexcoords.Count();
            for (int j = 0; j < nCount; ++j) {
                g_StudioMdlContext.texcoord[i][g_StudioMdlContext.numtexcoords[i]].x = extraTexcoords[j].x;
                g_StudioMdlContext.texcoord[i][g_StudioMdlContext.numtexcoords[i]].y = bFlipVCoordinate
                    ? 1.0f - extraTexcoords[j].y
                    : extraTexcoords[j].y;
                ++g_StudioMdlContext.numtexcoords[i];
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
static void AddToDeltaList(DeltaState_t *pDeltaStateData, int nUniqueVertex) {
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
    float flScale,
    int nStartingUniqueVertex,
    int nStartingUniqueVertexMap) {
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
        MdlError("DeltaState %s contains a different number of positions + position indices!\n",
                 pDeltaState->GetName());
        return false;
    }

    if (normals.Count() != normalIndices.Count()) {
        MdlError("DeltaState %s contains a different number of normals + normal indices!\n", pDeltaState->GetName());
        return false;
    }

    if (wrinkle.Count() != wrinkleIndices.Count()) {
        MdlError("DeltaState %s contains a different number of wrinkles + wrinkle indices!\n", pDeltaState->GetName());
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


static int GetExtraTexcoordIndex(CDmeVertexData *pVertexData, int nVertexIndex, int nChannel) {
    FieldIndex_t nFieldIndex = pVertexData->FindFieldIndex(CFmtStr("texcoord$%d", nChannel).Get());
    if (nFieldIndex < 0)
        return -1;

    CDmrArrayConst<int> indices(pVertexData->GetIndexData(nFieldIndex));
    return indices[nVertexIndex];
}

// JasonM TODO: unify ParseQuadFaceData() and ParseFaceData()

//-----------------------------------------------------------------------------
// Reads the quad face data from the DMX data
//-----------------------------------------------------------------------------
static void ParseQuadFaceData(CDmeVertexData *pVertexData, int material, int *pIndices, int vi, int ni, int *ti) {
    s_tmpface_t f;
    f.material = material;

    int p, n, t;
    p = pVertexData->GetPositionIndex(pIndices[0]);
    n = pVertexData->GetNormalIndex(pIndices[0]);
    t = pVertexData->GetTexCoordIndex(pIndices[0]);
    f.a = (p >= 0) ? vi + p : 0;
    f.na = (n >= 0) ? ni + n : 0;
    f.ta[0] = (t >= 0) ? ti[0] + t : 0;
    p = pVertexData->GetPositionIndex(pIndices[3]);
    n = pVertexData->GetNormalIndex(pIndices[3]);
    t = pVertexData->GetTexCoordIndex(pIndices[3]);
    f.b = (p >= 0) ? vi + p : 0;
    f.nb = (n >= 0) ? ni + n : 0;
    f.tb[0] = (t >= 0) ? ti[0] + t : 0;
    p = pVertexData->GetPositionIndex(pIndices[2]);
    n = pVertexData->GetNormalIndex(pIndices[2]);
    t = pVertexData->GetTexCoordIndex(pIndices[2]);
    f.c = (p >= 0) ? vi + p : 0;
    f.nc = (n >= 0) ? ni + n : 0;
    f.tc[0] = (t >= 0) ? ti[0] + t : 0;
    p = pVertexData->GetPositionIndex(pIndices[1]);
    n = pVertexData->GetNormalIndex(pIndices[1]);
    t = pVertexData->GetTexCoordIndex(pIndices[1]);
    f.d = (p >= 0) ? vi + p : 0;
    f.nd = (n >= 0) ? ni + n : 0;
    f.td[0] = (t >= 0) ? ti[0] + t : 0;

    Assert(f.a <= (uint32_t) g_StudioMdlContext.numverts && f.b <= (uint32_t) g_StudioMdlContext.numverts &&
        f.c <= (uint32_t) g_StudioMdlContext.numverts && f.d <= (uint32_t) g_StudioMdlContext.numverts);
    Assert(f.na <= (uint32_t) g_StudioMdlContext.numnormals && f.nb <= (uint32_t) g_StudioMdlContext.numnormals &&
        f.nc <= (uint32_t) g_StudioMdlContext.numnormals && f.nd <= (uint32_t) g_StudioMdlContext.numnormals);
    Assert(
        f.ta[0] <= (uint32_t) g_StudioMdlContext.numtexcoords[0] && f.tb[0] <= (uint32_t) g_StudioMdlContext.
        numtexcoords[0] &&
        f.tc[0] <= (uint32_t) g_StudioMdlContext.numtexcoords[0] && f.td[0] <= (uint32_t) g_StudioMdlContext.
        numtexcoords[0]);

    for (int i = 1; i < (MAXSTUDIOTEXCOORDS); ++i) {
        t = GetExtraTexcoordIndex(pVertexData, pIndices[0], i);
        f.ta[i] = (t >= 0) ? ti[i] + t : 0;
        t = GetExtraTexcoordIndex(pVertexData, pIndices[3], i);
        f.tb[i] = (t >= 0) ? ti[i] + t : 0;
        t = GetExtraTexcoordIndex(pVertexData, pIndices[2], i);
        f.tc[i] = (t >= 0) ? ti[i] + t : 0;
        t = GetExtraTexcoordIndex(pVertexData, pIndices[1], i);
        f.td[i] = (t >= 0) ? ti[i] + t : 0;
        Assert(
            f.ta[i] <= (uint32_t) g_StudioMdlContext.numtexcoords[i] && f.tb[i] <= (uint32_t) g_StudioMdlContext.
            numtexcoords[i] &&
            f.tc[i] <= (uint32_t) g_StudioMdlContext.numtexcoords[i] && f.td[i] <= (uint32_t) g_StudioMdlContext.
            numtexcoords[i]);
    }

    int i = g_StudioMdlContext.numfaces++;
    g_StudioMdlContext.face[i] = f;
}


//-----------------------------------------------------------------------------
// Reads the face data from the DMX data
//-----------------------------------------------------------------------------
static void ParseFaceData(CDmeVertexData *pVertexData, int material, int v1, int v2, int v3, int vi, int ni, int *ti) {
    s_tmpface_t f;
    f.material = material;

    int p, n, t;
    p = pVertexData->GetPositionIndex(v1);
    n = pVertexData->GetNormalIndex(v1);
    t = pVertexData->GetTexCoordIndex(v1);
    f.a = (p >= 0) ? vi + p : 0;
    f.na = (n >= 0) ? ni + n : 0;
    f.ta[0] = (t >= 0) ? ti[0] + t : 0;
    p = pVertexData->GetPositionIndex(v2);
    n = pVertexData->GetNormalIndex(v2);
    t = pVertexData->GetTexCoordIndex(v2);
    f.b = (p >= 0) ? vi + p : 0;
    f.nb = (n >= 0) ? ni + n : 0;
    f.tb[0] = (t >= 0) ? ti[0] + t : 0;
    p = pVertexData->GetPositionIndex(v3);
    n = pVertexData->GetNormalIndex(v3);
    t = pVertexData->GetTexCoordIndex(v3);
    f.c = (p >= 0) ? vi + p : 0;
    f.nc = (n >= 0) ? ni + n : 0;
    f.tc[0] = (t >= 0) ? ti[0] + t : 0;

    Assert(
        f.a <= (uint32_t) g_StudioMdlContext.numverts && f.b <= (uint32_t) g_StudioMdlContext.numverts && f.c <= (
            uint32_t) g_StudioMdlContext.numverts);
    Assert(f.na <= (uint32_t) g_StudioMdlContext.numnormals && f.nb <= (uint32_t) g_StudioMdlContext.numnormals &&
        f.nc <= (uint32_t) g_StudioMdlContext.numnormals);
    Assert(
        f.ta[0] <= (uint32_t) g_StudioMdlContext.numtexcoords[0] && f.tb[0] <= (uint32_t) g_StudioMdlContext.
        numtexcoords[0] &&
        f.tc[0] <= (uint32_t) g_StudioMdlContext.numtexcoords[0]);

    for (int i = 1; i < (MAXSTUDIOTEXCOORDS); ++i) {
        t = GetExtraTexcoordIndex(pVertexData, v1, i);
        f.ta[i] = (t >= 0) ? ti[i] + t : 0;
        t = GetExtraTexcoordIndex(pVertexData, v2, i);
        f.tb[i] = (t >= 0) ? ti[i] + t : 0;
        t = GetExtraTexcoordIndex(pVertexData, v3, i);
        f.tc[i] = (t >= 0) ? ti[i] + t : 0;
        Assert(
            f.ta[i] <= (uint32_t) g_StudioMdlContext.numtexcoords[i] && f.tb[i] <= (uint32_t) g_StudioMdlContext.
            numtexcoords[i] &&
            f.tc[i] <= (uint32_t) g_StudioMdlContext.numtexcoords[i]);
    }

    int i = g_StudioMdlContext.numfaces++;
    g_StudioMdlContext.face[i] = f;
}


//-----------------------------------------------------------------------------
// Reads the mesh data from the DMX data
//-----------------------------------------------------------------------------
static bool
LoadMesh(CDmeDag *pDmeDag, CDmeMesh *pMesh, CDmeVertexData *pBindState, const matrix3x4_t &mat, float flScale,
         int nBoneAssign, int *pBoneRemap, s_source_t *pSource) {
    pMesh->CollapseRedundantNormals(g_studiomdl.normal_blend);

    // Load the vertices
    int nStartingVertex = g_StudioMdlContext.numverts;
    int nStartingNormal = g_StudioMdlContext.numnormals;
    int nStartingUniqueCount = s_UniqueVertices.Count();
    int nStartingUniqueMapCount = s_UniqueVerticesMap.Count();
    int nStartingTexCoord[MAXSTUDIOTEXCOORDS];
    for (int i = 0; i < MAXSTUDIOTEXCOORDS; ++i) {
        nStartingTexCoord[i] = g_StudioMdlContext.numtexcoords[i];
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
        for (int j = 0; j < g_studiomdl.numrep; j++) {
            if (g_studiomdl.sourcetextures[j][0] == '\0') {
                Q_strncpy(pTextureName, g_studiomdl.defaulttextures[j], sizeof(pTextureName));
                break;
            }
            if (Q_stricmp(pTextureName, g_studiomdl.sourcetextures[j]) == 0) {
                Q_strncpy(pTextureName, g_studiomdl.defaulttextures[j], sizeof(pTextureName));
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
        bool bQuadSubd = (g_studiomdl.gflags & STUDIOHDR_FLAGS_SUBDIVISION_SURFACE) != 0;

        // prepare indices
        int nFirstIndex = 0;
        int nIndexCount = pFaceSet->NumIndices();
        while (nFirstIndex < nIndexCount) {
            int nVertexCount = pFaceSet->GetNextPolygonVertexCount(nFirstIndex);

            // Quad subd face?
            if (bQuadSubd && (nVertexCount == 4)) {
                int quadIndices[4];

                for (int j = 0; j < 4; j++) {
                    quadIndices[j] = pFaceSet->GetIndex(nFirstIndex + j);
                }

                ParseQuadFaceData(pBindState, material, quadIndices, nStartingVertex, nStartingNormal,
                                  nStartingTexCoord);

                nFirstIndex += 5; // -1 in list between face indices, so jump over 5 elements, not 4
            }
            else {
                if (nVertexCount >= 3) {
                    int nOutCount = (nVertexCount - 2) * 3;
                    int *pIndices = (int *) malloc(nOutCount * sizeof(int));
                    pMesh->ComputeTriangulatedIndices(pBindState, pFaceSet, nFirstIndex, pIndices, nOutCount);
                    for (int ii = 0; ii < nOutCount; ii += 3) {
                        ParseFaceData(pBindState, material, pIndices[ii], pIndices[ii + 2], pIndices[ii + 1],
                                      nStartingVertex, nStartingNormal, nStartingTexCoord);
                    }
                    free(pIndices);
                }

                nFirstIndex += nVertexCount +
                        1; // -1 in list between face indices, so jump over nVertexCount + 1 elements, not nVertexCount elements
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
static bool LoadMeshes(CDmeModel *pModel, float flScale, int *pBoneRemap, s_source_t *pSource) {
    FunctionTimingRecorder _("DMX");
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
    FunctionTimingRecorder _("DMX");
    int nCount = s_DeltaStates.Count();
    if (nCount == 0)
        return;

    Assert(s_Speed.Count() > 0);
    Assert(s_Balance.Count() > 0);

    Assert(s_UniqueVertices.Count() == g_numvlist);
    s_vertanim_t *pVertAnim = (s_vertanim_t *) malloc(g_studiomdl.numvlist * sizeof(s_vertanim_t));
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

            const v_unify_t *pList = g_studiomdl.v_list[uniqueVert.v];
            for (; pList; pList = pList->next) {
                if (pList->n != uniqueVert.n || pList->t[0] != uniqueVert.t[0])
                    continue;

                s_vertanim_t &vertanim = pVertAnim[nVertAnimCount++];
                vertanim.vertex = pList - g_studiomdl.v_listdata;
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
    const CDmeJiggleBone *pDmeJiggleBone = CastElementConst<CDmeJiggleBone>(pDmeDag);
    if (!pDmeJiggleBone)
        return;

    const char *pName = pDmeJiggleBone->GetName();
    for (int i = 0; i < g_studiomdl.numjigglebones; ++i) {
        if (!Q_stricmp(pName, g_studiomdl.jigglebones[i].bonename)) {
            MdlWarning("2000: Jiggle Bone: %s already defined, ignoring additional declarations\n", pName);
            return;
        }
    }

    struct s_jigglebone_t *pJiggleBone = &g_studiomdl.jigglebones[g_studiomdl.numjigglebones];
    ++g_studiomdl.numjigglebones;

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
    pJiggleBone->data.flags |= (pDmeJiggleBone->m_bLengthConstrained ? JIGGLE_HAS_LENGTH_CONSTRAINT : 0);
    pJiggleBone->data.angleLimit = DEG2RAD(pDmeJiggleBone->m_flAngleLimit);

    pJiggleBone->data.flags |= (pDmeJiggleBone->m_bYawConstrained ? JIGGLE_HAS_YAW_CONSTRAINT : 0);
    pJiggleBone->data.minYaw = DEG2RAD(pDmeJiggleBone->m_flYawMin);
    pJiggleBone->data.maxYaw = DEG2RAD(pDmeJiggleBone->m_flYawMax);
    pJiggleBone->data.yawFriction = pDmeJiggleBone->m_flYawFriction;
    pJiggleBone->data.yawBounce = pDmeJiggleBone->m_flYawBounce;

    pJiggleBone->data.flags |= (pDmeJiggleBone->m_bAngleConstrained ? JIGGLE_HAS_ANGLE_CONSTRAINT : 0);

    pJiggleBone->data.minPitch = DEG2RAD(pDmeJiggleBone->m_flPitchMin);
    pJiggleBone->data.maxPitch = DEG2RAD(pDmeJiggleBone->m_flPitchMax);
    pJiggleBone->data.pitchFriction = pDmeJiggleBone->m_flPitchFriction;
    pJiggleBone->data.pitchBounce = pDmeJiggleBone->m_flPitchBounce;

    if (pDmeJiggleBone->m_bFlexible) {
        if (pDmeJiggleBone->m_bRigid) {
            MdlWarning("2001: Jiggle Bone %s: Both flexible and rigid set, ignoring rigid\n", pName);
        }

        pJiggleBone->data.flags |= (JIGGLE_IS_FLEXIBLE);
        pJiggleBone->data.flags |= (pDmeJiggleBone->m_bPitchConstrained ? JIGGLE_HAS_PITCH_CONSTRAINT : 0);

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
        pJiggleBone->data.flags |= (JIGGLE_IS_FLEXIBLE | JIGGLE_HAS_LENGTH_CONSTRAINT);
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
AddDagJoint(CDmeModel *pModel, CDmeDag *pDag, std::array<s_node_t,MAXSTUDIOSRCBONES> &pNodes, int nParentIndex,
            BoneTransformMap_t &boneMap) {
    CDmeTransform *pDmeTransform = pDag->GetTransform();

    if (!pDmeTransform)
        return true;

    // Need room for one implicit bone added
    if (boneMap.m_nBoneCount >= (MAXSTUDIOSRCBONES - 1)) {
        MdlWarning("Ignoring Bone %s and children, too many bones [max can be %d]!\n", pDag->GetName(),
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
            MdlWarning("Joint %s doesn't appear in DmeModel[%s].jointList\n", pDag->GetName(), pModel->GetName());
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
    FunctionTimingRecorder _("DMX");
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
static void LoadAttachments(CDmeDag *pRoot, CDmeDag *pDag, s_source_t *pSource, bool bStaticProp) {
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
                attachment.type |= IS_RIGID;
            }
            if (pAttachment->m_bIsWorldAligned) {
                attachment.flags |= ATTACHMENT_FLAG_WORLD_ALIGN;
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
static void LoadBindPose(CDmeModel *pModel, float flScale, const BoneTransformMap_t &boneMap, s_source_t *pSource) {
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
                MdlWarning("Cannot find DmeTransform for MDL Bone %d\n", nMdlBoneIndex);
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
static void UpdateChannels(CUtlVector<IDmeOperator *> &operators, CDmeChannelsClip *pAnimation, DmeTime_t clipTime) {
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
static void ComputeFramePose(s_sourceanim_t *pSourceAnim, int nFrame, float flScale, BoneTransformMap_t &boneMap) {
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
static void
LoadAnimations(s_source_t *pSource, CDmeAnimationList *pAnimationList, float flScale, BoneTransformMap_t &boneMap) {
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
                        printf("* %s%c%s\n", pFullPath, CORRECT_PATH_SEPARATOR, szName);
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

    if (g_studiomdl.twistbones.Count() == MAXSTUDIOBONES)
        return false;

    CDmeDag *pDmeDagParent = pDmeTwist->GetParentTarget();
    CDmeDag *pDmeDagChild = pDmeTwist->GetChildTarget();

    if (!pDmeDagParent || !pDmeDagChild)
        return false;

    CTwistBone &twistBone = g_studiomdl.twistbones[g_studiomdl.twistbones.AddToTail()];

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
        for (int j = 0; !bFound && j < g_studiomdl.twistbones.Count(); ++j) {
            const CTwistBone &tmpTwistBone = g_studiomdl.twistbones[j];
            for (int k = 0; k < tmpTwistBone.m_twistBoneTargets.Count(); ++k) {
                if (!Q_stricmp(pszTwistTargetName, tmpTwistBone.m_twistBoneTargets[k].m_szBoneName)) {
                    bFound = true;
                    break;
                }
            }
        }

        // A Twist bone is already driving this bone, don't make another
        if (bFound)
            continue;

        s_constraintbonetarget_t &twistBoneTarget = twistBone.m_twistBoneTargets[twistBone.m_twistBoneTargets.
            AddToTail()];
        V_strncpy(twistBoneTarget.m_szBoneName, pDmeTwistDag->GetName(), ARRAYSIZE(twistBoneTarget.m_szBoneName));
        twistBoneTarget.m_nBone = -1;
        twistBoneTarget.m_flWeight = pDmeTwist->GetSlaveWeight(i);
        twistBoneTarget.m_vOffset = pDmeTwistDag->GetTransform()->GetPosition();
        twistBoneTarget.m_qOffset = pDmeTwist->GetSlaveBindOrientation(i);
    }

    // No targets, twist constraint is useless
    if (twistBone.m_twistBoneTargets.Count() <= 0) {
        g_studiomdl.twistbones.RemoveMultipleFromTail(1);
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

    const Quaternion qRot = Quaternion(g_studiomdl.defaultrotation);

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
    const Quaternion qRot = Quaternion(g_studiomdl.defaultrotation);

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
    for (int i = 0; i < g_studiomdl.constraintBones.Count(); ++i) {
        const CConstraintBoneBase *pConstraintB = g_studiomdl.constraintBones[i];

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
    if (g_studiomdl.constraintBones.Count() == MAXSTUDIOBONES) {
        MdlError("Too Many Constraint Bones, Max: %d\n", MAXSTUDIOBONES);
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
        g_studiomdl.constraintBones.AddToTail(pConstraint);
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
static bool
LoadModelAndSkeleton(s_source_t *pSource, BoneTransformMap_t &boneMap, CDmeDag *pSkeleton, CDmeModel *pModel,
                     CDmeCombinationOperator *pCombinationOperator, bool bStaticProp) {
    FunctionTimingRecorder _("DMX");
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

    g_StudioMdlContext.numfaces = 0;
    if (pModel) {
        if (pCombinationOperator) {
            pCombinationOperator->GenerateWrinkleDeltas(false);
        }
        LoadBindPose(pModel, g_studiomdl.currentscale, boneMap, pSource);
        if (!LoadMeshes(pModel, g_studiomdl.currentscale, boneMap.m_pnDmeModelToMdl, pSource))
            return false;

        UnifyIndices(pSource);
        BuildVertexAnimations(pSource);
        BuildIndividualMeshes(pSource);
    }

    if (g_StudioMdlContext.numfaces == 0 && pSource->numbones == 1 && !V_strcmp(
            pSource->localBone[0].name, "defaultRoot")) {
        MdlError("Error - dmx has no contents: %s\n", pSource->filename.c_str());
    }

    if (pCombinationOperator) {
        AddFlexKeys(pModel, pModel, pCombinationOperator, pSource);
        AddCombination(pSource, pCombinationOperator);
    }

    LoadAttachments(pSkeleton, pSkeleton, pSource, bStaticProp);

    return true;
}


//-----------------------------------------------------------------------------
// Given the s_model_t pointer, finds the index of it in g_studiomdl.model
// Returns -1 if it cannot be found
//-----------------------------------------------------------------------------
static int FindModelIndex(s_model_t *pModel) {
    if (pModel) {
        for (int i = 0; i < g_studiomdl.nummodels; ++i) {
            if (g_studiomdl.model[i] == pModel)
                return i;
        }

        MdlWarning("Cannot Find s_model_t: \"%s\" in g_studiomdl.model\n", pModel->name.c_str());
    }

    return -1;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static bool AddFlexKey(
    s_source_t *pSource, s_model_t *pModel,
    const char *pszAnimationName,
    int nFlexDesc,
    int nFlexPair,
    EyelidType_t nEyelidType,
    float flLowererHeight,
    float flNeutralHeight,
    float flRaiserHeight,
    float flSplit = 0.0f,
    float flDecay = 1.0f,
    int nFrame = 0) // DMX frame is always 0
{
    if (g_studiomdl.flexkeys.size() >= g_studiomdl.flexkeys.capacity()) {
        MdlError("Too Many Flex Keys, Cannot Add %s\n", "TODO: FlexKeyName");
        return false;
    }

    const int nModelIndex = FindModelIndex(pModel);

    s_flexkey_t *pFlexKey = &g_studiomdl.flexkeys.emplace_back(s_flexkey_t{
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
    for (int i = 0; i < g_studiomdl.flexkeys.size(); ++i) {
        const s_flexkey_t *pTmpFlexKey = &(g_studiomdl.flexkeys[i]);

        if (!V_stricmp(pFlexKey->animationname, pTmpFlexKey->animationname) &&
            pFlexKey->flexdesc == pTmpFlexKey->flexdesc &&
            pFlexKey->flexpair == pTmpFlexKey->flexpair &&
            pFlexKey->frame == pTmpFlexKey->frame &&
            pFlexKey->target0 == pTmpFlexKey->target0 &&
            pFlexKey->target1 == pTmpFlexKey->target1 &&
            pFlexKey->target2 == pTmpFlexKey->target2 &&
            pFlexKey->target3 == pTmpFlexKey->target3) {
            // Duplicate, get rid of it
            V_memset(pFlexKey, 0, sizeof(s_flexkey_t));
            g_studiomdl.flexkeys.pop_back();
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
        if (!V_stricmp(pDmeEyelid->m_sRightEyeballName.Get(), pEyeball->name)) {
            nRightLeftIndex = kRight;
            bRightOk = true;
        }
        else if (!Q_stricmp(pDmeEyelid->m_sLeftEyeballName, pEyeball->name)) {
            nRightLeftIndex = kLeft;
            bLeftOk = true;
        }
        else {
            MdlWarning("Unknown Eyeball: %s\n", pEyeball->name.c_str());
            continue;
        }

        for (int j = 0; j < kEyelidTypeCount; ++j) {
            if (fabs(eyelidData[j].m_flTarget) > pEyeball->radius) {
                MdlError("Eyelid \"%s\" %s %.1f out of range (+-%.1f)\n", bUpper ? "upper" : "lower",
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
        MdlError("Could not find right eye \"%s\"\n", pDmeEyelid->m_sRightEyeballName.Get());
        return false;
    }

    if (!bLeftOk) {
        MdlError("Could not find left eye \"%s\"\n", pDmeEyelid->m_sLeftEyeballName.Get());
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

    if (g_studiomdl.nummouths > nMouthIndex)
        return false;

    g_studiomdl.nummouths = nMouthIndex + 1;

    s_mouth_t *pMouth = &(g_studiomdl.mouth[nMouthIndex]);
    pMouth->flexdesc = Add_Flexdesc(pDmeMouth->m_sFlexControllerName.Get());
    V_strncpy(pMouth->bonename, pDmeMouth->m_sBoneName, sizeof(pMouth->bonename));
    pMouth->forward = pDmeMouth->m_vForward.Get(); // TODO: Adjust Y Up?

    return false;
}

//-----------------------------------------------------------------------------
// Loads Eyeballs
//-----------------------------------------------------------------------------
static void LoadEyeballs(s_source_t *pSource, s_model_t *pModel, CDmrElementArray<CDmElement> &elementArray) {
    if (!pSource || !pModel || elementArray.Count() <= 0)
        return;

    Assert(pModel->source == NULL || pModel->source == pSource);

    matrix3x4_t mDefRot;
    AngleMatrix(g_studiomdl.defaultrotation, mDefRot);
    Vector vTmp;

    for (int i = 0; i < elementArray.Count(); ++i) {
        CDmeEyeball *pDmeEyeball = CastElement<CDmeEyeball>(elementArray.Element(i));
        if (!pDmeEyeball)
            continue;

        if (pModel->eyeballs.full()) {
            MdlWarning("1100: Max number of eyeballs reached for model %s, ignoring eyeball %s\n", pModel->name.c_str(),
                       pDmeEyeball->GetName());
            continue;
        }

        int nFoundBoneIndex = -1;
        for (int nSearchBoneIndex = 0; nSearchBoneIndex < pSource->numbones; ++nSearchBoneIndex) {
            if (!Q_stricmp(pSource->localBone[nSearchBoneIndex].name, pDmeEyeball->m_sParentBoneName.Get())) {
                nFoundBoneIndex = nSearchBoneIndex;
                break;
            }
        }

        if (nFoundBoneIndex < 0) {
            MdlWarning("1101: Couldn't find bone %s on model %s, ignoring eyeball %s\n",
                       pDmeEyeball->m_sParentBoneName.Get(), pModel->name.c_str(), pDmeEyeball->GetName());
            continue;
        }

        const char *pszMaterialName = pDmeEyeball->m_sMaterialName.Get();
        const bool bRelative = (strchr(pszMaterialName, '/') || strchr(pszMaterialName, '\\')) ? true : false;

        const int nSearchMeshMatIndex = UseTextureAsMaterial(
            LookupTexture(pDmeEyeball->m_sMaterialName.Get(), bRelative));
        int nFoundMeshMatIndex = -1;
        for (int i = 0; i < pSource->nummeshes; ++i) {
            const int nTmpMeshMatIndex = pSource->meshindex[i]; // meshes are internally stored by material index

            if (nTmpMeshMatIndex == nSearchMeshMatIndex) {
                nFoundMeshMatIndex = i;
                break;
            }
        }

        if (nFoundMeshMatIndex < 0) {
            MdlWarning("1102: Couldn't find eyeball material %s on model %s, ignoring eyeball %s\n",
                       pDmeEyeball->m_sMaterialName.Get(), pModel->name.c_str(), pDmeEyeball->GetName());
            continue;
        }

        s_eyeball_t eyeball{
            .name = pDmeEyeball->GetName(),
            .bone = nFoundBoneIndex,
            .zoffset = tan(DEG2RAD(pDmeEyeball->m_flYawAngle.Get())),
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
            if (!V_stricmp(eyeball.name, pTmp->name)) {
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

        for (int nFlexIndex = 0; nFlexIndex < g_studiomdl.flexcontrollers.size(); ++nFlexIndex) {
            if (!Q_strcmp(szEyesFlexControllers[nNewFlexIndex], g_studiomdl.flexcontrollers[nFlexIndex].name)) {
                bHasEyeFlexController = true;
                break;
            }
        }

        // The flex controller range for eyes_updown & eyes_rightleft is default [-45, 45] because it's clamped by eyesMaxDeflection
        // and changing it based on max deflection would cause animatin changes since flex controller values are normalized [0, 1]
        // [-45, 45 ] gives a maxium range that's useful

        if (!bHasEyeFlexController) {
            if (g_studiomdl.flexcontrollers.size() >= g_studiomdl.flexcontrollers.capacity()) {
                MdlWarning("1103: Couldn't make eyes flexcontroller %s, too many flex controllers defined\n",
                           szEyesFlexControllers[nNewFlexIndex]);
                continue;
            }
            g_studiomdl.flexcontrollers.emplace_back(s_flexcontroller_t{
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
    FunctionTimingRecorder _("Parse");
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

    if (!g_StudioMdlContext.quiet) {
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
    extern qboolean tokenready;
    auto old_tokenread = tokenready;
    if (!LoadModelAndSkeleton(pSource, boneMap, pSkeleton, pModel, pCombinationOperator, false))
        goto dmxError;
    tokenready = old_tokenread;
    LoadQcModelElements(pSource, g_pCurrentModel, pModel);

    if (CDmeAnimationList *pAnimationList = pRoot->GetValueElement<CDmeAnimationList>("animationList")) {
        LoadAnimations(pSource, pAnimationList, g_studiomdl.currentscale, boneMap);
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
            LoadAnimations(pSource, pAnimationList, g_studiomdl.currentscale, boneMap);
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

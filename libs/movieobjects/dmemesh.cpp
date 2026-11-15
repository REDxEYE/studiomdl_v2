//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include <limits.h>

#include "movieobjects/dmemesh.h"
#include "movieobjects/dmevertexdata.h"
#include "movieobjects/dmefaceset.h"
#include "movieobjects/dmematerial.h"
#include "movieobjects/dmemodel.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmeselection.h"
#include "movieobjects/dmmeshcomp.h"
#include "tier1/keyvalues.h"

#include "datamodel/dmelementfactoryhelper.h"
#include "studio.h"
#include "tier1_v2/function_timing.hpp"



//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CDmeMeshRenderInfo::CDmeMeshRenderInfo(CDmeVertexData *pBaseState) : m_PositionIndices(
                                                                         pBaseState->GetVertexIndexData(
                                                                             CDmeVertexData::FIELD_POSITION)),
                                                                     m_PositionData(pBaseState->GetPositionData()),
                                                                     m_NormalIndices(
                                                                         pBaseState->GetVertexIndexData(
                                                                             CDmeVertexData::FIELD_NORMAL)),
                                                                     m_NormalData(pBaseState->GetNormalData()),
                                                                     m_TangentIndices(
                                                                         pBaseState->GetVertexIndexData(
                                                                             CDmeVertexData::FIELD_TANGENT)),
                                                                     m_TangentData(pBaseState->GetTangentData()) {
    m_pBaseState = pBaseState;
    m_bHasPositionData = m_PositionIndices.Count() > 0;
    m_bHasNormalData = m_NormalIndices.Count() > 0;
    m_bHasTangentData = m_TangentIndices.Count() > 0;
    m_nJointCount = pBaseState->JointCount();
    m_bHasSkinningData = pBaseState->HasSkinningData() && m_nJointCount > 0;
}





//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeMesh, CDmeMesh);


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeMesh::OnConstruction() {
    m_BindBaseState.Init(this, "bindState");
    m_CurrentBaseState.Init(this, "currentState");
    m_BaseStates.Init(this, "baseStates", FATTRIB_MUSTCOPY);
    m_DeltaStates.Init(this, "deltaStates", FATTRIB_MUSTCOPY | FATTRIB_HAS_CALLBACK);
    m_FaceSets.Init(this, "faceSets", FATTRIB_MUSTCOPY);
    m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL].Init(this, "deltaStateWeights");
    m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED].Init(this, "deltaStateWeightsLagged");
}

void CDmeMesh::OnDestruction() {
    m_hwFaceSets.RemoveAll();

    DeleteAttributeVarElementArray(m_BaseStates);
    DeleteAttributeVarElementArray(m_DeltaStates);
    DeleteAttributeVarElementArray(m_FaceSets);
}


//-----------------------------------------------------------------------------
// resolve internal data from changed attributes
//-----------------------------------------------------------------------------
void CDmeMesh::OnAttributeChanged(CDmAttribute *pAttribute) {
    BaseClass::OnAttributeChanged(pAttribute);

    if (pAttribute == m_DeltaStates.GetAttribute()) {
        int nDeltaStateCount = m_DeltaStates.Count();
        for (int i = 0; i < MESH_DELTA_WEIGHT_TYPE_COUNT; ++i) {
            // Make sure we have the correct number of weights
            int nWeightCount = m_DeltaStateWeights[i].Count();
            if (nWeightCount < nDeltaStateCount) {
                for (int j = nWeightCount; j < nDeltaStateCount; ++j) {
                    m_DeltaStateWeights[i].AddToTail(Vector2D(0.0f, 0.0f));
                }
            }
            else if (nDeltaStateCount > nWeightCount) {
                m_DeltaStateWeights[i].RemoveMultiple(nWeightCount, nWeightCount - nDeltaStateCount);
            }
        }
    }
}


//-----------------------------------------------------------------------------
// Adds deltas into a delta mesh
//-----------------------------------------------------------------------------
template<class T>
bool CDmeMesh::AddVertexDelta(
    CDmeVertexData *pBaseState,
    void *pVertexData, int nStride, CDmeVertexDataBase::StandardFields_t fieldId, int nIndex, bool bDoLag) {
    CDmeVertexDeltaData *pDeltaState = GetDeltaState(nIndex);

    if (!pBaseState || !pDeltaState)
        return false;

    const FieldIndex_t nBaseFieldIndex = pBaseState->FindFieldIndex(
        fieldId == CDmeVertexData::FIELD_WRINKLE ? CDmeVertexData::FIELD_TEXCOORD : fieldId);
    const FieldIndex_t nDeltaFieldIndex = pDeltaState->FindFieldIndex(fieldId);
    if (nBaseFieldIndex < 0 || nDeltaFieldIndex < 0)
        return false;

    const CDmrArray<int> indices = pDeltaState->GetIndexData(nDeltaFieldIndex);
    const CDmrArray<T> delta = pDeltaState->GetVertexData(nDeltaFieldIndex);
    const int nDeltaCount = indices.Count();

    const float flWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL][nIndex].x;

    const FieldIndex_t nSpeedFieldIndex = pBaseState->FindFieldIndex(CDmeVertexData::FIELD_MORPH_SPEED);

    const int nVertexCount = pBaseState->VertexCount();

    if (!bDoLag || nSpeedFieldIndex < 0) {
        for (int j = 0; j < nDeltaCount; ++j) {
            int nDataIndex = indices.Get(j);
            if (nDataIndex < 0 || nDataIndex >= nVertexCount) {
                Assert(nDataIndex >= 0 && nDataIndex < nVertexCount);
                continue;
            }

            T *pDeltaData = (T *) ((char *) pVertexData + nStride * nDataIndex);
            *pDeltaData += delta.Get(j) * flWeight;
        }

        return true;
    }

    const float flLaggedWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED][nIndex].x;

    const CDmrArrayConst<int> speedIndices = pBaseState->GetIndexData(nSpeedFieldIndex);
    const CDmrArrayConst<float> speedDelta = pBaseState->GetVertexData(nSpeedFieldIndex);
    for (int j = 0; j < nDeltaCount; ++j) {
        int nDataIndex = indices.Get(j);
        const CUtlVector<int> &list = pBaseState->FindVertexIndicesFromDataIndex(nBaseFieldIndex, nDataIndex);
        Assert(list.Count() > 0);
        // FIXME: Average everything in the list.. shouldn't be necessary though
        float flSpeed = speedDelta.Get(speedIndices.Get(list[0]));
        float flActualWeight = Lerp(flSpeed, flLaggedWeight, flWeight);

        T *pDeltaData = (T *) ((char *) pVertexData + nStride * nDataIndex);
        *pDeltaData += delta.Get(j) * flActualWeight;
    }
    return true;
}



template<class T>
bool CDmeMesh::AddStereoVertexDelta(
    CDmeVertexData *pBaseState,
    void *pVertexData, int nStride, CDmeVertexDataBase::StandardFields_t fieldId, int nIndex, bool bDoLag) {
    CDmeVertexDeltaData *pDeltaState = GetDeltaState(nIndex);
    if (!pBaseState || !pDeltaState)
        return false;

    const FieldIndex_t nBaseFieldIndex = pBaseState->FindFieldIndex(
        fieldId == CDmeVertexData::FIELD_WRINKLE ? CDmeVertexData::FIELD_TEXCOORD : fieldId);
    const FieldIndex_t nDeltaFieldIndex = pDeltaState->FindFieldIndex(fieldId);
    if (nBaseFieldIndex < 0 || nDeltaFieldIndex < 0)
        return false;

    float flLeftWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL][nIndex].x;
    float flRightWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL][nIndex].y;

    const CDmrArray<int> indices = pDeltaState->GetIndexData(nDeltaFieldIndex);
    const CDmrArray<T> delta = pDeltaState->GetVertexData(nDeltaFieldIndex);
    const CUtlVector<int> &balanceIndices = pBaseState->GetVertexIndexData(CDmeVertexData::FIELD_BALANCE);
    const CUtlVector<float> &balanceDelta = pBaseState->GetBalanceData();
    const int nDeltaCount = indices.Count();

    const FieldIndex_t nSpeedFieldIndex = pBaseState->FindFieldIndex(CDmeVertexData::FIELD_MORPH_SPEED);

    if (!bDoLag || nSpeedFieldIndex < 0) {
        for (int j = 0; j < nDeltaCount; ++j) {
            int nDataIndex = indices.Get(j);
            const CUtlVector<int> &list = pBaseState->FindVertexIndicesFromDataIndex(nBaseFieldIndex, nDataIndex);
            Assert(list.Count() > 0);
            // FIXME: Average everything in the list.. shouldn't be necessary though
            float flRightAmount = balanceDelta[balanceIndices[list[0]]];
            float flWeight = Lerp(flRightAmount, flLeftWeight, flRightWeight);

            T *pDeltaData = (T *) ((char *) pVertexData + nStride * nDataIndex);
            *pDeltaData += delta.Get(j) * flWeight;
        }

        return true;
    }

    float flLeftWeightLagged = m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED][nIndex].x;
    float flRightWeightLagged = m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED][nIndex].y;

    const CDmrArray<int> pSpeedIndices = pBaseState->GetIndexData(nSpeedFieldIndex);
    const CDmrArray<float> pSpeedDelta = pBaseState->GetVertexData(nSpeedFieldIndex);
    for (int j = 0; j < nDeltaCount; ++j) {
        int nDataIndex = indices.Get(j);
        const CUtlVector<int> &list = pBaseState->FindVertexIndicesFromDataIndex(nBaseFieldIndex, nDataIndex);
        Assert(list.Count() > 0);
        // FIXME: Average everything in the list.. shouldn't be necessary though
        float flRightAmount = balanceDelta[balanceIndices[list[0]]];
        float flWeight = Lerp(flRightAmount, flLeftWeight, flRightWeight);
        float flLaggedWeight = Lerp(flRightAmount, flLeftWeightLagged, flRightWeightLagged);
        float flSpeed = pSpeedDelta.Get(pSpeedIndices.Get(list[0]));
        float flActualWeight = Lerp(flSpeed, flLaggedWeight, flWeight);

        T *pDeltaData = (T *) ((char *) pVertexData + nStride * nDataIndex);
        *pDeltaData += delta.Get(j) * flActualWeight;
    }
    return true;
}





//-----------------------------------------------------------------------------
// Face sets
//-----------------------------------------------------------------------------
int CDmeMesh::FaceSetCount() const {
    return m_FaceSets.Count();
}

CDmeFaceSet *CDmeMesh::GetFaceSet(int faceSetIndex) {
    return m_FaceSets[faceSetIndex];
}


//-----------------------------------------------------------------------------
// Find a base state by name
//-----------------------------------------------------------------------------
CDmeVertexData *CDmeMesh::FindBaseState(const char *pStateName) const {
    const int nBaseStateCount = BaseStateCount();
    for (int i = 0; i < nBaseStateCount; ++i) {
        CDmeVertexData *pBaseState = GetBaseState(i);
        if (!Q_stricmp(pStateName, pBaseState->GetName()))
            return pBaseState;
    }

    return nullptr;
}





//-----------------------------------------------------------------------------
// Selects a particular base state to be current state
//-----------------------------------------------------------------------------
CDmeVertexData *CDmeMesh::GetCurrentBaseState() {
    return m_CurrentBaseState;
}


//-----------------------------------------------------------------------------
// Selects a particular base state to be current state
//-----------------------------------------------------------------------------
const CDmeVertexData *CDmeMesh::GetCurrentBaseState() const {
    return m_CurrentBaseState;
}



//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
CDmeVertexData *CDmeMesh::GetBindBaseState() {
    if (m_BindBaseState.GetElement())
        return m_BindBaseState;

    // Backwards compatibility
    return FindBaseState("bind");
}



//-----------------------------------------------------------------------------
// Delta states
//-----------------------------------------------------------------------------
int CDmeMesh::DeltaStateCount() const {
    return m_DeltaStates.Count();
}


//-----------------------------------------------------------------------------
// Returns the delta
//-----------------------------------------------------------------------------
CDmeVertexDeltaData *CDmeMesh::GetDeltaState(int nDeltaIndex) const {
    if (nDeltaIndex < 0 || nDeltaIndex >= m_DeltaStates.Count())
        return nullptr;

    return m_DeltaStates[nDeltaIndex];
}


//-----------------------------------------------------------------------------
// Finds a delta state by name.  If it isn't found, return NULL
//-----------------------------------------------------------------------------
CDmeVertexDeltaData *CDmeMesh::FindDeltaState(const char *pDeltaName, bool bSortDeltaName /* = true */) const {
    return GetDeltaState(FindDeltaStateIndex(pDeltaName, bSortDeltaName));
}

CDmeVertexDeltaData* CDmeMesh::FindDeltaState(const std::string_view deltaName, bool bSortDeltaName) const {
    return GetDeltaState(FindDeltaStateIndex(deltaName, bSortDeltaName));
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int SortDeltaNameFunc(const void *a, const void *b) {
    return Q_strcmp(*( const char ** )( a ), *( const char ** )( b ));
}

static int SortDeltaNameFunc(const std::string_view &a, const std::string_view &b) {
    return a.compare(b);
}


//-----------------------------------------------------------------------------
// If the name doe
//-----------------------------------------------------------------------------
static const char *SortDeltaName(CUtlString &sDeltaName, const char *pszInDeltaName) {
    if (!pszInDeltaName || !strchr(pszInDeltaName, '_'))
        return pszInDeltaName;

    CUtlVector<char *, CUtlMemory<char *, int> > nameComponents;
    V_SplitString(pszInDeltaName, "\\", nameComponents);

    if (nameComponents.Count() > 0) {
        CUtlVector<char *> deltaNames;
        for (int i = 0; i < nameComponents.Count(); ++i) {
            deltaNames.AddToTail(nameComponents[i]);
        }

        qsort(deltaNames.Base(), deltaNames.Count(), sizeof(char *), SortDeltaNameFunc);

        sDeltaName.Clear();

        for (int i = 0; i < deltaNames.Count(); ++i) {
            if (V_strlen(deltaNames[i]) <= 0)
                continue;

            if (sDeltaName.Length() > 0) {
                sDeltaName += "_";
            }

            sDeltaName += deltaNames[i];
        }
    }

    nameComponents.PurgeAndDeleteElements();

    return sDeltaName.Get();
}
static std::string_view SortDeltaName(std::string &sDeltaName, const std::string_view pszInDeltaName) {
    if (!pszInDeltaName.contains('_'))
        return pszInDeltaName;

    std::vector<std::string_view > nameComponents = vstd::split(pszInDeltaName, "\\");

    if (!nameComponents.empty()) {
        std::vector<std::string_view> deltaNames;
        for (auto & nameComponent : nameComponents) {
            deltaNames.emplace_back(nameComponent);
        }
        std::ranges::sort(deltaNames);

        sDeltaName.clear();

        for (int i = 0; i < deltaNames.size(); ++i) {
            if (deltaNames[i].empty())
                continue;

            if (!sDeltaName.empty()) {
                sDeltaName += '_';
            }

            sDeltaName += deltaNames[i];
        }
    }

    nameComponents.clear();

    return sDeltaName;
}



//-----------------------------------------------------------------------------
// Finds a delta state index by comparing names, if it can't be found
// searches for all permutations of the delta name
//-----------------------------------------------------------------------------
int CDmeMesh::FindDeltaStateIndex(const char *inDeltaName, bool bSortDeltaName /* = true */) const {
    CUtlString sDeltaName(inDeltaName);

    const int nDeltaStateCount = DeltaStateCount();
    for (int i = 0; i < nDeltaStateCount; ++i) {
        CDmeVertexDeltaData *pDeltaState = GetDeltaState(i);
        if (!V_stricmp(sDeltaName.Get(), pDeltaState->GetName()))
            return i;
    }

    if (bSortDeltaName && strchr(sDeltaName.Get(), '_')) {
        SortDeltaName(sDeltaName, inDeltaName);
    }

    for (int i = 0; i < nDeltaStateCount; ++i) {
        CDmeVertexDeltaData *pDeltaState = GetDeltaState(i);
        if (!V_stricmp(sDeltaName.Get(), pDeltaState->GetName()))
            return i;
    }

    return -1;
}
int CDmeMesh::FindDeltaStateIndex( const std::string_view inDeltaName, bool bSortDeltaName) const {
    std::string sDeltaName(inDeltaName);

    const int nDeltaStateCount = DeltaStateCount();
    for (int i = 0; i < nDeltaStateCount; ++i) {
        CDmeVertexDeltaData *pDeltaState = GetDeltaState(i);
        if (vstd::iequals(sDeltaName, pDeltaState->GetName()))
            return i;
    }
    if (bSortDeltaName && sDeltaName.contains('_')) {
        SortDeltaName(sDeltaName, inDeltaName);
    }

    for (int i = 0; i < nDeltaStateCount; ++i) {
        CDmeVertexDeltaData *pDeltaState = GetDeltaState(i);
        if (vstd::iequals(sDeltaName, pDeltaState->GetName()))
            return i;
    }

    return -1;
}



//-----------------------------------------------------------------------------
// Compute triangulated indices
//-----------------------------------------------------------------------------
void CDmeMesh::ComputeTriangulatedIndices(const CDmeVertexData *pBaseState, const CDmeFaceSet *pFaceSet,
                                          const uint32_t nFirstIndex, int *indicesOut) const {
    // FIXME: Come up with a more efficient way of computing this
    // This involves a bunch of recomputation of distances
    float flMinDistance = FLT_MAX;
    int nMinIndex = 0;
    int nVertexCount = pFaceSet->GetNextPolygonVertexCount(nFirstIndex);

    // Optimization for quads + triangles.. it's totally symmetric
    int nLoopCount = nVertexCount;
    if (nVertexCount <= 3) {
        nLoopCount = 0;
    }
    else if (nVertexCount == 4) {
        nLoopCount = 2;
    }

    for (int i = 0; i < nLoopCount; ++i) {
        float flDistance = 0.0f;
        const Vector &vecCenter = pBaseState->GetPosition(pFaceSet->GetIndex(nFirstIndex + i));
        for (int j = 2; j < nVertexCount - 1; ++j) {
            int vi = (i + j) % nVertexCount;
            const Vector &vecEdge = pBaseState->GetPosition(pFaceSet->GetIndex(nFirstIndex + vi));
            flDistance += vecEdge.DistTo(vecCenter);
        }

        if (flDistance < flMinDistance) {
            nMinIndex = i;
            flMinDistance = flDistance;
        }
    }

    // Compute the triangulation indices
    uint32_t nOutIndex = 0;
    for (uint32_t i = 1; i < nVertexCount - 1; ++i) {
        indicesOut[nOutIndex++] = pFaceSet->GetIndex(nFirstIndex + nMinIndex);
        indicesOut[nOutIndex++] = pFaceSet->GetIndex(nFirstIndex + ((nMinIndex + i) % nVertexCount));
        indicesOut[nOutIndex++] = pFaceSet->GetIndex(nFirstIndex + ((nMinIndex + i + 1) % nVertexCount));
    }
}






















//-----------------------------------------------------------------------------
// Takes an incoming Delta state, splits it's name '_' and then finds the
// control delta (a state without a '_' in its name) and adds the index
// of that control delta to the referenced array
//
// Returns true if all of the control states exist, false otherwise
//-----------------------------------------------------------------------------
bool CDmeMesh::GetControlDeltaIndices(
    CDmeVertexDeltaData *pDeltaState,
    CUtlVector<int> &controlDeltaIndices) const {
    Assert(pDeltaState);
    return GetControlDeltaIndices(pDeltaState->GetName(), controlDeltaIndices);
}


//-----------------------------------------------------------------------------
// Same as above but just uses the name of a delta
//-----------------------------------------------------------------------------
bool CDmeMesh::GetControlDeltaIndices(
    const char *pDeltaStateName,
    CUtlVector<int> &controlDeltaIndices) const {
    Assert(pDeltaStateName);
    controlDeltaIndices.RemoveAll();

    const int nDeltaStateName(Q_strlen(pDeltaStateName));
    char *pTmpBuf(reinterpret_cast<char *>(alloca(nDeltaStateName + 1)));
    Q_strncpy(pTmpBuf, pDeltaStateName, nDeltaStateName + 1);
    char *pNext;
    for (char *pCurr = pTmpBuf; pCurr; pCurr = pNext) {
        pNext = strchr(pCurr, '_');
        if (pNext) {
            *pNext = '\0';
            ++pNext;
        }

        if (Q_strlen(pCurr)) {
            const int controlDeltaIndex(FindDeltaStateIndex(pCurr));
            if (controlDeltaIndex >= 0) {
                controlDeltaIndices.AddToTail(controlDeltaIndex);
            }
            else {
                controlDeltaIndices.RemoveAll();
                return false;
            }
        }
    }

    return true;
}





//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class T_t>
int CDmeMesh::GenerateCompleteDataForDelta(
    const CDmeVertexDeltaData *pDelta,
    T_t *pFullData,
    int nFullData,
    CDmeVertexData::StandardFields_t standardField) {
    memset(pFullData, 0, nFullData * sizeof(T_t));

    const FieldIndex_t fIndex(pDelta->FindFieldIndex(standardField));
    if (fIndex >= 0) {
        CDmrArrayConst<T_t> fDataArray(pDelta->GetVertexData(fIndex));
        const CUtlVector<T_t> &fData(fDataArray.Get());
        const CUtlVector<int> &fIndexData(pDelta->GetVertexIndexData(fIndex));
        const int nIndexData(fIndexData.Count());

        Assert(nIndexData <= nFullData);

        int index;

        int i(0);

        for (int j(0); j < nIndexData; ++j) {
            index = fIndexData[j];
            while (index > i) {
                ++i;
            }

            Assert(i < nFullData);
            pFullData[i] = fData[j];
        }

        return nIndexData;
    }

    return 0;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class T_t>
void CDmeMesh::AddDelta(
    const CDmeVertexDeltaData *pDelta,
    T_t *pFullData,
    int nFullData,
    FieldIndex_t fieldIndex,
    float weight,
    const CDmeSingleIndexedComponent *pMask) {
    if (fieldIndex >= 0) {
        CDmrArrayConst<T_t> fDataArray(pDelta->GetVertexData(fieldIndex));
        const CUtlVector<T_t> &fData(fDataArray.Get());
        const CUtlVector<int> &fIndexData(pDelta->GetVertexIndexData(fieldIndex));
        const int nIndexData(fIndexData.Count());

        T_t t;

        Assert(nIndexData <= nFullData);

        int index;

        int i(0);

        if (pMask) {
            float cWeight;

            for (int j(0); j < nIndexData; ++j) {
                index = fIndexData[j];

                if (!pMask->GetWeight(index, cWeight))
                    continue;

                while (index > i) {
                    ++i;
                }

                Assert(i < nFullData);

                t = fData[j];
                t *= (weight * cWeight);
                pFullData[i] += t;
            }
        }
        else {
            for (int j(0); j < nIndexData; ++j) {
                index = fIndexData[j];
                while (index > i) {
                    ++i;
                }

                Assert(i < nFullData);
                t = fData[j];
                t *= weight;
                pFullData[i] += t;
            }
        }
    }
}

template void CDmeMesh::AddDelta<float>(const CDmeVertexDeltaData *, float *, int, FieldIndex_t, float,
                                        const CDmeSingleIndexedComponent *);

template void CDmeMesh::AddDelta<Vector2D>(const CDmeVertexDeltaData *, Vector2D *, int, FieldIndex_t, float,
                                           const CDmeSingleIndexedComponent *);

template void CDmeMesh::AddDelta<Vector>(const CDmeVertexDeltaData *, Vector *, int, FieldIndex_t, float,
                                         const CDmeSingleIndexedComponent *);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class T_t>
void CDmeMesh::AddDelta(
    const CDmeVertexDeltaData *pDelta,
    T_t *pFullData,
    int nFullData,
    CDmeVertexData::StandardFields_t standardField,
    float weight,
    const CDmeSingleIndexedComponent *pMask) {
    const FieldIndex_t fIndex(pDelta->FindFieldIndex(standardField));
    AddDelta(pDelta, pFullData, nFullData, fIndex, weight, pMask);
}

template void CDmeMesh::AddDelta<float>(const CDmeVertexDeltaData *, float *, int, CDmeVertexData::StandardFields_t,
                                        float, const CDmeSingleIndexedComponent *);

template void CDmeMesh::AddDelta<Vector2D>(const CDmeVertexDeltaData *, Vector2D *, int,
                                           CDmeVertexData::StandardFields_t, float, const CDmeSingleIndexedComponent *);

template void CDmeMesh::AddDelta<Vector>(const CDmeVertexDeltaData *, Vector *, int, CDmeVertexData::StandardFields_t,
                                         float, const CDmeSingleIndexedComponent *);


//-----------------------------------------------------------------------------
// There's no guarantee that fields are added in any order, nor that only
// standard fields exist...
//-----------------------------------------------------------------------------
template<class T_t>
void CDmeMesh::AddCorrectedDelta(
    CDmrArray<T_t> &baseDataArray,
    const CUtlVector<int> &baseIndices,
    const DeltaComputation_t &deltaComputation,
    const char *pFieldName,
    float weight,
    const CDmeSingleIndexedComponent *pMask) {
    const CUtlVector<T_t> &baseData(baseDataArray.Get());
    const int nData(baseData.Count());
    T_t *pData(reinterpret_cast<T_t *>(alloca(nData * sizeof( T_t ))));
    Q_memcpy(pData, baseData.Base(), nData * sizeof( T_t ));

    CDmeVertexDeltaData *pDelta(GetDeltaState(deltaComputation.m_nDeltaIndex));

    const int deltaFieldIndex(pDelta->FindFieldIndex(pFieldName));
    if (deltaFieldIndex < 0)
        return;

    AddDelta(pDelta, pData, nData, deltaFieldIndex, weight, pMask);

    const CUtlVector<int> &depDeltas(deltaComputation.m_DependentDeltas);
    const int nDepDeltas(depDeltas.Count());
    for (int j(0); j < nDepDeltas; ++j) {
        pDelta = GetDeltaState(depDeltas[j]);

        int depFieldIndex = pDelta->FindFieldIndex(pFieldName);
        if (depFieldIndex < 0)
            continue;

        AddDelta(pDelta, pData, nData, depFieldIndex, weight, pMask);
    }

    baseDataArray.CopyArray(pData, nData);
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class T_t>
void CDmeMesh::AddCorrectedDelta(
    CUtlVector<T_t> &baseData,
    const CUtlVector<int> &baseIndices,
    const DeltaComputation_t &deltaComputation,
    const char *pFieldName,
    float weight,
    const CDmeSingleIndexedComponent *pMask) {
    const int nData(baseData.Count());

    CDmeVertexDeltaData *pDelta(GetDeltaState(deltaComputation.m_nDeltaIndex));

    const int deltaFieldIndex(pDelta->FindFieldIndex(pFieldName));
    if (deltaFieldIndex < 0)
        return;

    AddDelta(pDelta, baseData.Base(), nData, deltaFieldIndex, weight, pMask);

    const CUtlVector<int> &depDeltas(deltaComputation.m_DependentDeltas);
    const int nDepDeltas(depDeltas.Count());
    for (int j(0); j < nDepDeltas; ++j) {
        pDelta = GetDeltaState(depDeltas[j]);

        int depFieldIndex = pDelta->FindFieldIndex(pFieldName);
        if (depFieldIndex < 0)
            continue;

        AddDelta(pDelta, baseData.Base(), nData, depFieldIndex, weight, pMask);
    }
}


//-----------------------------------------------------------------------------
// There's no guarantee that fields are added in any order, nor that only
// standard fields exist...
//-----------------------------------------------------------------------------
template<class T_t>
void CDmeMesh::AddRawDelta(
    CDmeVertexDeltaData *pDelta,
    CDmrArray<T_t> &baseDataArray,
    FieldIndex_t nDeltaFieldIndex,
    float weight,
    const CDmeSingleIndexedComponent *pMask) {
    if (!pDelta || nDeltaFieldIndex < 0)
        return;

    const CUtlVector<T_t> &baseData(baseDataArray.Get());
    const int nData(baseData.Count());
    T_t *pData(reinterpret_cast<T_t *>(alloca(nData * sizeof( T_t ))));
    Q_memcpy(pData, baseData.Base(), nData * sizeof( T_t ));

    AddDelta(pDelta, pData, nData, nDeltaFieldIndex, weight, pMask);

    baseDataArray.CopyArray(pData, nData);
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class T_t>
void CDmeMesh::AddRawDelta(
    CDmeVertexDeltaData *pDelta,
    CUtlVector<T_t> &baseData,
    FieldIndex_t nDeltaFieldIndex,
    float weight,
    const CDmeSingleIndexedComponent *pMask) {
    if (!pDelta || nDeltaFieldIndex < 0)
        return;

    const int nData(baseData.Count());

    AddDelta(pDelta, baseData.Base(), nData, nDeltaFieldIndex, weight, pMask);
}






//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeMesh::CreateDeltaFieldFromBaseField(
    CDmeVertexData::StandardFields_t nStandardFieldIndex,
    const CDmrArrayConst<float> &baseArray,
    const CDmrArrayConst<float> &bindArray,
    CDmeVertexDeltaData *pDelta) {
    const int nData(baseArray.Count());
    if (nData != bindArray.Count())
        return false;

    const float *pBaseData(baseArray.Get().Base());
    const float *pBindData(bindArray.Get().Base());

    float *pData(reinterpret_cast<float *>(nData * sizeof(float)));
    Q_memcpy(pData, pBaseData, nData * sizeof( float ));
    int *pIndices(reinterpret_cast<int *>(nData * sizeof(int)));

    float v;

    int nDeltaCount(0);
    for (int i = 0; i < nData; ++i) {
        v = pBaseData[i] - pBindData[i];

        // Kind of a magic number but it's because of 16 bit compression of the delta values
        if (fabs(v) >= (1 / 4096.0f)) {
            pData[nDeltaCount] = v;
            pIndices[nDeltaCount] = i;
            ++nDeltaCount;
        }
    }

    if (nDeltaCount <= 0)
        return true;

    FieldIndex_t fieldIndex(pDelta->CreateField(nStandardFieldIndex));
    if (fieldIndex < 0)
        return false;

    pDelta->AddVertexData(fieldIndex, nDeltaCount);
    pDelta->SetVertexData(fieldIndex, 0, nDeltaCount, AT_FLOAT, pData);
    pDelta->SetVertexIndices(fieldIndex, 0, nDeltaCount, pIndices);

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeMesh::CreateDeltaFieldFromBaseField(
    CDmeVertexData::StandardFields_t nStandardFieldIndex,
    const CDmrArrayConst<Vector2D> &baseArray,
    const CDmrArrayConst<Vector2D> &bindArray,
    CDmeVertexDeltaData *pDelta) {
    const int nData(baseArray.Count());
    if (nData != bindArray.Count())
        return false;

    const Vector2D *pBaseData(baseArray.Get().Base());
    const Vector2D *pBindData(bindArray.Get().Base());

    Vector2D *pData(reinterpret_cast<Vector2D *>(nData * sizeof(Vector2D)));
    Q_memcpy(pData, pBaseData, nData * sizeof( Vector2D ));
    int *pIndices(reinterpret_cast<int *>(nData * sizeof(int)));

    Vector2D v;

    int nDeltaCount(0);
    for (int i = 0; i < nData; ++i) {
        v = pBaseData[i] - pBindData[i];

        // Kind of a magic number but it's because of 16 bit compression of the delta values
        if (fabs(v.x) >= (1 / 4096.0f) || fabs(v.y) >= (1 / 4096.0f)) {
            pData[nDeltaCount] = v;
            pIndices[nDeltaCount] = i;
            ++nDeltaCount;
        }
    }

    if (nDeltaCount <= 0)
        return true;

    FieldIndex_t fieldIndex(pDelta->CreateField(nStandardFieldIndex));
    if (fieldIndex < 0)
        return false;

    pDelta->AddVertexData(fieldIndex, nDeltaCount);
    pDelta->SetVertexData(fieldIndex, 0, nDeltaCount, AT_VECTOR2, pData);
    pDelta->SetVertexIndices(fieldIndex, 0, nDeltaCount, pIndices);

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeMesh::CreateDeltaFieldFromBaseField(
    CDmeVertexData::StandardFields_t nStandardFieldIndex,
    const CDmrArrayConst<Vector> &baseArray,
    const CDmrArrayConst<Vector> &bindArray,
    CDmeVertexDeltaData *pDelta) {
    const int nData(baseArray.Count());
    if (nData != bindArray.Count())
        return false;

    const Vector *pBaseData(baseArray.Get().Base());
    const Vector *pBindData(bindArray.Get().Base());

    Vector *pData(reinterpret_cast<Vector *>(alloca(nData * sizeof( Vector ))));
    Q_memcpy(pData, pBaseData, nData * sizeof( Vector ));
    int *pIndices(reinterpret_cast<int *>(alloca(nData * sizeof( int ))));

    Vector v;

    int nDeltaCount(0);
    for (int i = 0; i < nData; ++i) {
        v = pBaseData[i] - pBindData[i];

        // Kind of a magic number but it's because of 16 bit compression of the delta values
        if (fabs(v.x) >= (1 / 4096.0f) || fabs(v.y) >= (1 / 4096.0f) || fabs(v.z) >= (1 / 4096.0f)) {
            pData[nDeltaCount] = v;
            pIndices[nDeltaCount] = i;
            ++nDeltaCount;
        }
    }

    if (nDeltaCount <= 0)
        return true;

    FieldIndex_t fieldIndex(pDelta->CreateField(nStandardFieldIndex));
    if (fieldIndex < 0)
        return false;

    pDelta->AddVertexData(fieldIndex, nDeltaCount);
    pDelta->SetVertexData(fieldIndex, 0, nDeltaCount, AT_VECTOR3, pData);
    pDelta->SetVertexIndices(fieldIndex, 0, nDeltaCount, pIndices);

    return true;
}




//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

class CSelectionHelper {
public:
    class CVert {
    public:
        int m_index;
        int m_count;
        float m_weight;
    };

    void AddVert(int vIndex, float weight = 1.0f);

    int AddToSelection(CDmeSingleIndexedComponent *pSelection) const;

protected:
    CUtlVector<CVert> m_verts;

    int BinarySearch(int component) const;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CSelectionHelper::AddVert(int vIndex, float weight /* = 1.0f */) {
    // Find the vertex, add it if necessary
    const int index = BinarySearch(vIndex);

    if (index == m_verts.Count()) {
        // New Add to end
        CVert &v(m_verts[m_verts.AddToTail()]);
        v.m_index = vIndex;
        v.m_count = 1;
        v.m_weight = weight;
    }
    else if (vIndex == m_verts[index].m_index) {
        // Existing, increment
        CVert &v(m_verts[index]);
        Assert(v.m_index == vIndex);
        v.m_count += 1;
        v.m_weight += weight;
    }
    else {
        // New insert before index
        CVert &v(m_verts[m_verts.InsertBefore(index)]);
        v.m_index = vIndex;
        v.m_count = 1;
        v.m_weight = weight;
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int CSelectionHelper::AddToSelection(CDmeSingleIndexedComponent *pSelection) const {
    const int nVerts = m_verts.Count();

    for (int i = 0; i < nVerts; ++i) {
        const CVert &v(m_verts[i]);
        Assert(!pSelection->HasComponent( v.m_index ));
        pSelection->AddComponent(v.m_index, v.m_weight / static_cast<float>(v.m_count));
    }

    return nVerts;
}



//-----------------------------------------------------------------------------
// Searches for the component in the sorted component list and returns the
// index if it's found or if it's not found, returns the index at which it
// should be inserted to maintain the sorted order of the component list
//-----------------------------------------------------------------------------
int CSelectionHelper::BinarySearch(int vIndex) const {
    const int nVerts(m_verts.Count());

    int left(0);
    int right(nVerts - 1);
    int mid;

    while (left <= right) {
        mid = (left + right) >> 1; // floor( ( left + right ) / 2.0 )
        if (vIndex > m_verts[mid].m_index) {
            left = mid + 1;
        }
        else if (vIndex < m_verts[mid].m_index) {
            right = mid - 1;
        }
        else {
            return mid;
        }
    }

    return left;
}




CDmeSingleIndexedComponent *CDmeMesh::FeatherSelection(
    float falloffDistance,
    Falloff_t falloffType,
    Distance_t distanceType,
    CDmeSingleIndexedComponent *pSelection,
    CDmMeshComp *pPassedMeshComp) {
    switch (falloffType) {
        case SMOOTH:
            return FeatherSelection<SMOOTH>(falloffDistance, distanceType, pSelection, pPassedMeshComp);
        case SPIKE:
            return FeatherSelection<SPIKE>(falloffDistance, distanceType, pSelection, pPassedMeshComp);
        case DOME:
            return FeatherSelection<DOME>(falloffDistance, distanceType, pSelection, pPassedMeshComp);
        default:
            return FeatherSelection<LINEAR>(falloffDistance, distanceType, pSelection, pPassedMeshComp);
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<int T>
CDmeSingleIndexedComponent *CDmeMesh::FeatherSelection(
    float fDistance, Distance_t distanceType,
    CDmeSingleIndexedComponent *pSelection, CDmMeshComp *pPassedMeshComp) {
    // TODO: Support feathering inward instead of just outward
    if (fDistance <= 0.0f || !pSelection)
        return nullptr;

    // Make a new CDmeSingleIndexedComponent to do all of the dirty work
    CDmeSingleIndexedComponent *pNewSelection = CreateElement<CDmeSingleIndexedComponent>(
        "feather", pSelection->GetFileId());
    pSelection->CopyAttributesTo(pNewSelection);

    CDmMeshComp *pMeshComp = pPassedMeshComp ? pPassedMeshComp : new CDmMeshComp(this);
    CDmeVertexData *pBase = pMeshComp->BaseState();

    if (distanceType == DIST_RELATIVE) {
        Vector vCenter;
        float flRadius;
        GetBoundingSphere(vCenter, flRadius, pBase, pSelection);
        fDistance *= flRadius;
    }

    const CUtlVector<Vector> &positions(pBase->GetPositionData());
    const int nPositions = positions.Count();

    if (!pBase)
        return nullptr;

    CUtlVector<int> sIndices;

    int insideCount = 0;

    CFalloff<T> falloff;

    do {
        insideCount = 0;
        CUtlVector<CDmMeshComp::CVert *> neighbours;
        CSelectionHelper sHelper;

        pNewSelection->GetComponents(sIndices);
        int nVertices = sIndices.Count();

        for (int i = 0; i < nVertices; ++i) {
            const int nNeighbours = pMeshComp->FindNeighbouringVerts(sIndices[i], neighbours);

            for (int j = 0; j < nNeighbours; ++j) {
                const int vIndex = neighbours[j]->PositionIndex();

                if (pNewSelection->HasComponent(vIndex))
                    continue;

                const int closestVert = ClosestSelectedVertex(vIndex, pSelection, pBase);
                if (closestVert < 0 || closestVert >= nPositions)
                    continue;

                const float vDistance = positions[vIndex].DistTo(positions[closestVert]);
                if (vDistance <= fDistance) {
                    sHelper.AddVert(vIndex, falloff(vDistance / fDistance));
                    ++insideCount;
                }
            }
        }

        sHelper.AddToSelection(pNewSelection);
    } while (insideCount > 0);

    return pNewSelection;
}




//-----------------------------------------------------------------------------
// Interpolates between two arrays of values and stores the result in a
// CDmrArray.
//
// result = ( ( 1 - weight ) * a ) + ( weight * b )
//
//-----------------------------------------------------------------------------
template<class T_t>
bool CDmeMesh::InterpMaskedData(
    CDmrArray<T_t> &aData,
    const CUtlVector<T_t> &bData,
    float weight,
    const CDmeSingleIndexedComponent *pMask) const {
    const int nDst = aData.Count();

    if (bData.Count() != nDst)
        return false;

    // The wacky way of writing these expression is because Vector4D is missing operators
    // And this probably works better because of fewer temporaries

    T_t a;
    T_t b;

    if (pMask) {
        // With a weight mask
        float vWeight;
        for (int i = 0; i < nDst; ++i) {
            if (pMask->GetWeight(i, vWeight)) {
                vWeight *= weight; // Specifically not clamping
                a = aData.Get(i);
                a *= (1.0f - vWeight);
                b = bData[i];
                b *= vWeight;
                b += a;
                aData.Set(i, b);
            }
        }
    }
    else {
        // Without a weight mask
        const float oneMinusWeight(1.0f - weight);
        for (int i = 0; i < nDst; ++i) {
            a = aData.Get(i);
            a *= oneMinusWeight;
            b = bData[i];
            b *= weight;
            b += a;
            aData.Set(i, b);
        }
    }

    return true;
}


//-----------------------------------------------------------------------------
// Interpolates between two CDmeVertexData's
//
// paData = ( ( 1 - weight ) * a ) + ( weight * b )
//-----------------------------------------------------------------------------
bool CDmeMesh::InterpMaskedData(
    CDmeVertexData *paData,
    const CDmeVertexData *pbData,
    float weight,
    const CDmeSingleIndexedComponent *pMask) const {
    if (!paData || !pbData || paData == pbData)
        return false;

    const int naField = paData->FieldCount();
    const int nbField = pbData->FieldCount();

    for (int i = 0; i < naField; ++i) {
        const CUtlString &aFieldName(paData->FieldName(i));

        for (int j = 0; j < nbField; ++j) {
            const CUtlString &bFieldName(pbData->FieldName(j));
            if (aFieldName != bFieldName)
                continue;

            const FieldIndex_t aFieldIndex(paData->FindFieldIndex(aFieldName));
            const FieldIndex_t bFieldIndex(pbData->FindFieldIndex(bFieldName));

            if (aFieldIndex < 0 || bFieldIndex < 0)
                break;

            CDmAttribute *paAttr(paData->GetVertexData(aFieldIndex));
            const CDmAttribute *pbAttr(pbData->GetVertexData(bFieldIndex));

            if (paAttr->GetType() != pbAttr->GetType())
                break;

            if (paData->GetVertexIndexData(aFieldIndex).Count() != pbData->GetVertexIndexData(bFieldIndex).Count())
                break;

            switch (paAttr->GetType()) {
                case AT_FLOAT_ARRAY:
                    InterpMaskedData(AsLvalue(CDmrArray<float>(paAttr)), CDmrArrayConst<float>(pbAttr).Get(), weight,
                                     pMask);
                    break;
                case AT_COLOR_ARRAY:
                    InterpMaskedData(AsLvalue(CDmrArray<Vector4D>(paAttr)), CDmrArrayConst<Vector4D>(pbAttr).Get(),
                                     weight, pMask);
                    break;
                case AT_VECTOR2_ARRAY:
                    InterpMaskedData(AsLvalue(CDmrArray<Vector2D>(paAttr)), CDmrArrayConst<Vector2D>(pbAttr).Get(),
                                     weight, pMask);
                    break;
                case AT_VECTOR3_ARRAY:
                    InterpMaskedData(AsLvalue(CDmrArray<Vector>(paAttr)), CDmrArrayConst<Vector>(pbAttr).Get(), weight,
                                     pMask);
                    break;
                default:
                    break;
            }
            break;
        }
    }

    return true;
}



//-----------------------------------------------------------------------------
// Returns the index of the closest selected vertex in the mesh to vIndex
// -1 on failure
//-----------------------------------------------------------------------------
int CDmeMesh::ClosestSelectedVertex(int vIndex, CDmeSingleIndexedComponent *pSelection,
                                    const CDmeVertexData *pPassedBase /* = NULL */) const {
    const CDmeVertexData *pBase = pPassedBase ? pPassedBase : GetCurrentBaseState();
    if (!pBase)
        return -1;

    const CUtlVector<Vector> &positions(pBase->GetPositionData());

    if (vIndex >= positions.Count())
        return -1;

    const Vector &p(positions[vIndex]);

    CUtlVector<int> verts;
    pSelection->GetComponents(verts);
    const int nVerts = verts.Count();

    if (nVerts <= 0)
        return -1;

    float minSqDist = p.DistToSqr(positions[verts[0]]);
    float tmpSqDist;

    int retVal = verts[0];
    for (int i = 1; i < nVerts; ++i) {
        tmpSqDist = p.DistToSqr(positions[verts[i]]);
        if (tmpSqDist < minSqDist) {
            minSqDist = tmpSqDist;
            retVal = verts[i];
        }
    }

    return retVal;
}






//-----------------------------------------------------------------------------
// TODO: Current state is insufficient as long as the current state isn't
//       created from the current delta weights
//-----------------------------------------------------------------------------
void CDmeMesh::GetBoundingSphere(
    Vector &c, float &r,
    CDmeVertexData *pPassedBase /* = NULL */, CDmeSingleIndexedComponent *pPassedSelection /* = NULL */) const {
    c.Zero();
    r = 0.0f;

    const CDmeVertexData *pBase = pPassedBase ? pPassedBase : GetCurrentBaseState();
    if (!pBase)
        return;

    const FieldIndex_t pIndex = pBase->FindFieldIndex(CDmeVertexData::FIELD_POSITION);
    if (pIndex < 0)
        return;

    const CUtlVector<Vector> &pData(pBase->GetPositionData());
    const int nPositions = pData.Count();

    if (pPassedSelection) {
        const int nSelectionCount = pPassedSelection->Count();
        int nIndex;
        float fWeight;
        for (int i = 0; i < nSelectionCount; ++i) {
            pPassedSelection->GetComponent(i, nIndex, fWeight);
            c += pData[nIndex];
        }

        c /= static_cast<float>(nSelectionCount);

        float sqDist;
        for (int i = 0; i < nSelectionCount; ++i) {
            for (int i = 0; i < nPositions; ++i) {
                sqDist = c.DistToSqr(pData[i]);
                if (sqDist > r) {
                    r = sqDist;
                }
            }
        }
    }
    else {
        for (int i = 0; i < nPositions; ++i) {
            c += pData[i];
        }

        c /= static_cast<float>(nPositions);

        float sqDist;
        for (int i = 0; i < nPositions; ++i) {
            for (int i = 0; i < nPositions; ++i) {
                sqDist = c.DistToSqr(pData[i]);
                if (sqDist > r) {
                    r = sqDist;
                }
            }
        }
    }

    r = sqrt(r);
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMesh::GetBoundingBox(Vector &min, Vector &max, CDmeVertexData *pPassedBase /* = NULL */,
                              CDmeSingleIndexedComponent *pPassedSelection /* = NULL */) const {
    min.Zero();
    max.Zero();

    const CDmeVertexData *pBase = pPassedBase ? pPassedBase : GetCurrentBaseState();
    if (!pBase)
        return;

    const FieldIndex_t pIndex = pBase->FindFieldIndex(CDmeVertexData::FIELD_POSITION);
    if (pIndex < 0)
        return;

    const CUtlVector<Vector> &pData(pBase->GetPositionData());
    const int nPositions = pData.Count();

    if (pPassedSelection) {
        const int nSelectionCount = pPassedSelection->Count();

        if (nSelectionCount > 0) {
            int nIndex;
            float fWeight;

            pPassedSelection->GetComponent(0, nIndex, fWeight);
            min = pData[nIndex];
            max = min;

            for (int i = 1; i < nSelectionCount; ++i) {
                pPassedSelection->GetComponent(i, nIndex, fWeight);

                const Vector &p = pData[nIndex];
                if (p.x < min.x) {
                    min.x = p.x;
                }
                else if (p.x > max.x) {
                    max.x = p.x;
                }

                if (p.y < min.y) {
                    min.y = p.y;
                }
                else if (p.y > max.y) {
                    max.y = p.y;
                }

                if (p.z < min.z) {
                    min.z = p.z;
                }
                else if (p.z > max.z) {
                    max.z = p.z;
                }
            }
        }
    }
    else {
        if (nPositions > 0) {
            min = pData[0];
            max = min;

            for (int i = 1; i < nPositions; ++i) {
                const Vector &p = pData[i];
                if (p.x < min.x) {
                    min.x = p.x;
                }
                else if (p.x > max.x) {
                    max.x = p.x;
                }

                if (p.y < min.y) {
                    min.y = p.y;
                }
                else if (p.y > max.y) {
                    max.y = p.y;
                }

                if (p.z < min.z) {
                    min.z = p.z;
                }
                else if (p.z > max.z) {
                    max.z = p.z;
                }
            }
        }
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<class T_t>
bool CDmeMesh::SetBaseDataToDeltas(
    CDmeVertexData *pBase,
    CDmeVertexData::StandardFields_t nStandardField, CDmrArrayConst<T_t> &srcData, CDmrArray<T_t> &dstData,
    bool bDoStereo, bool bDoLag) {
    const int nDataCount = dstData.Count();
    if (srcData.Count() != nDataCount)
        return false;

    // Create the temp buffer for the data
    T_t *pData = reinterpret_cast<T_t *>(alloca(nDataCount * sizeof( T_t )));

    // Copy the data from the src base state
    memcpy(pData, srcData.Base(), nDataCount * sizeof(T_t));

    const int nCount = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL].Count();
    Assert(
        m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL].Count() == m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED].Count());

    if (bDoStereo) {
        for (int i = 0; i < nCount; ++i) {
            float flLeftWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL][i].x;
            float flRightWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL][i].y;
            float flLeftWeightLagged = m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED][i].x;
            float flRightWeightLagged = m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED][i].y;
            if (flLeftWeight <= 0.0f && flRightWeight <= 0.0f && (
                    !bDoLag || (flLeftWeightLagged <= 0.0f && flRightWeightLagged <= 0.0f)))
                continue;

            AddStereoVertexDelta<T_t>(pBase, pData, sizeof(T_t), nStandardField, i, bDoLag);
        }
    }
    else {
        for (int i = 0; i < nCount; ++i) {
            float flWeight = m_DeltaStateWeights[MESH_DELTA_WEIGHT_NORMAL][i].x;
            float flWeightLagged = m_DeltaStateWeights[MESH_DELTA_WEIGHT_LAGGED][i].x;
            if (flWeight < 0.0f && (!bDoLag || flWeightLagged <= 0.0f))
                continue;

            AddVertexDelta<T_t>(pBase, pData, sizeof(T_t), nStandardField, i, bDoLag);
        }
    }

    dstData.SetMultiple(0, nDataCount, pData);

    return true;
}





//-----------------------------------------------------------------------------
// Cleans up delta data that is referring to normals which have been merged out
//-----------------------------------------------------------------------------
static void CollapseRedundantDeltaNormals(CDmeVertexDeltaData *pDmeDelta, const CUtlVector<int> &normalMap) {
    if (!pDmeDelta)
        return;

    FieldIndex_t nNormalFieldIndex = pDmeDelta->FindFieldIndex(CDmeVertexData::FIELD_NORMAL);
    if (nNormalFieldIndex < 0)
        return; // No normal deltas

    const CUtlVector<Vector> &oldNormalData = pDmeDelta->GetNormalData();
    const CUtlVector<int> &oldNormalIndices = pDmeDelta->GetVertexIndexData(nNormalFieldIndex);

    Assert(oldNormalData.Count() == oldNormalIndices.Count());

    CUtlVector<bool> done;
    done.SetCount(normalMap.Count());
    Q_memset(done.Base(), 0, done.Count() * sizeof( bool ));

    CUtlVector<Vector> newNormalData;
    CUtlVector<int> newNormalIndices;

    for (int i = 0; i < oldNormalIndices.Count(); ++i) {
        const int nNewIndex = normalMap[oldNormalIndices[i]];
        if (nNewIndex < 0 || done[nNewIndex])
            continue;

        done[nNewIndex] = true;
        newNormalData.AddToTail(oldNormalData[i]);
        newNormalIndices.AddToTail(nNewIndex);
    }

    pDmeDelta->RemoveAllVertexData(nNormalFieldIndex);
    nNormalFieldIndex = pDmeDelta->CreateField(CDmeVertexDeltaData::FIELD_NORMAL);
    pDmeDelta->AddVertexData(nNormalFieldIndex, newNormalData.Count());
    pDmeDelta->SetVertexData(nNormalFieldIndex, 0, newNormalData.Count(), AT_VECTOR3, newNormalData.Base());
    pDmeDelta->SetVertexIndices(nNormalFieldIndex, 0, newNormalIndices.Count(), newNormalIndices.Base());
}


//-----------------------------------------------------------------------------
// Remove redundant normals from a DMX Mesh
// Looks at all of the normals around each position vertex and merges normals
// which are numerically similar (within flNormalBlend which by default in
// studiomdl is within 2 degrees) around that vertex
//
// If this would result in more normals being created, then don't do anything
// return false.
//-----------------------------------------------------------------------------
static bool CollapseRedundantBaseNormals(CDmeVertexData *pDmeVertexData, CUtlVector<int> &normalMap,
                                         float flNormalBlend) {
    if (!pDmeVertexData)
        return false;

    FieldIndex_t nPositionFieldIndex = pDmeVertexData->FindFieldIndex(CDmeVertexData::FIELD_POSITION);
    FieldIndex_t nNormalFieldIndex = pDmeVertexData->FindFieldIndex(CDmeVertexData::FIELD_NORMAL);
    if (nPositionFieldIndex < 0 || nNormalFieldIndex < 0)
        return false;

    const CUtlVector<Vector> &oldNormalData = pDmeVertexData->GetNormalData();
    const CUtlVector<int> &oldNormalIndices = pDmeVertexData->GetVertexIndexData(nNormalFieldIndex);

    CUtlVector<Vector> newNormalData;
    CUtlVector<int> newNormalIndices;

    newNormalIndices.SetCount(oldNormalIndices.Count());
    for (int i = 0; i < newNormalIndices.Count(); ++i) {
        newNormalIndices[i] = -1;
    }

    const int nPositionDataCount = pDmeVertexData->GetPositionData().Count();
    for (int i = 0; i < nPositionDataCount; ++i) {
        int nNewNormalDataIndex = newNormalData.Count();

        const CUtlVector<int> &vertexIndices = pDmeVertexData->FindVertexIndicesFromDataIndex(
            CDmeVertexData::FIELD_POSITION, i);
        for (int j = 0; j < vertexIndices.Count(); ++j) {
            bool bUnique = true;
            const int nVertexIndex = vertexIndices[j];
            const Vector &vNormal = oldNormalData[oldNormalIndices[vertexIndices[j]]];

            for (int k = nNewNormalDataIndex; k < newNormalData.Count(); ++k) {
                if (DotProduct(vNormal, newNormalData[k]) > flNormalBlend) {
                    newNormalIndices[nVertexIndex] = k;
                    bUnique = false;
                    break;
                }
            }

            if (!bUnique)
                continue;

            newNormalIndices[nVertexIndex] = newNormalData.AddToTail(vNormal);
        }
    }

    for (int i = 0; i < newNormalIndices.Count(); ++i) {
        if (newNormalIndices[i] == -1) {
            newNormalIndices[i] = newNormalData.AddToTail(oldNormalData[oldNormalIndices[i]]);
        }
    }

    // If it's the same or more don't do anything
    if (newNormalData.Count() >= oldNormalData.Count())
        return false;

    normalMap.SetCount(oldNormalData.Count());
    for (int i = 0; i < normalMap.Count(); ++i) {
        normalMap[i] = -1;
    }

    Assert(newNormalIndices.Count() == oldNormalIndices.Count());
    for (int i = 0; i < oldNormalIndices.Count(); ++i) {
        if (normalMap[oldNormalIndices[i]] == -1) {
            normalMap[oldNormalIndices[i]] = newNormalIndices[i];
        }
        else {
            Assert(normalMap[ oldNormalIndices[i] ] == newNormalIndices[i]);
        }
    }

    pDmeVertexData->RemoveAllVertexData(nNormalFieldIndex);
    nNormalFieldIndex = pDmeVertexData->CreateField(CDmeVertexDeltaData::FIELD_NORMAL);
    pDmeVertexData->AddVertexData(nNormalFieldIndex, newNormalData.Count());
    pDmeVertexData->SetVertexData(nNormalFieldIndex, 0, newNormalData.Count(), AT_VECTOR3, newNormalData.Base());
    pDmeVertexData->SetVertexIndices(nNormalFieldIndex, 0, newNormalIndices.Count(), newNormalIndices.Base());

    return true;
}


//-----------------------------------------------------------------------------
// Collapse all normals with the same numerical value into the same normal
//-----------------------------------------------------------------------------
static bool CollapseRedundantBaseNormalsAggressive(CDmeVertexData *pDmeVertexData, float flNormalBlend) {
    if (!pDmeVertexData)
        return false;

    FieldIndex_t nNormalFieldIndex = pDmeVertexData->FindFieldIndex(CDmeVertexData::FIELD_NORMAL);
    if (nNormalFieldIndex < 0)
        return false;

    const CUtlVector<Vector> &oldNormalData = pDmeVertexData->GetNormalData();
    const CUtlVector<int> &oldNormalIndices = pDmeVertexData->GetVertexIndexData(nNormalFieldIndex);

    CUtlVector<int> normalMap;
    normalMap.SetCount(oldNormalData.Count());

    CUtlVector<Vector> newNormalData;

    for (int i = 0; i < oldNormalData.Count(); ++i) {
        bool bUnique = true;
        const Vector &vNormal = oldNormalData[i];

        for (int j = 0; j < newNormalData.Count(); ++j) {
            if (DotProduct(vNormal, newNormalData[j]) > flNormalBlend) {
                normalMap[i] = j;
                bUnique = false;
                break;
            }
        }

        if (!bUnique)
            continue;

        normalMap[i] = newNormalData.AddToTail(vNormal);
    }

    // If it's the same then don't do anything.
    if (newNormalData.Count() >= oldNormalData.Count())
        return false;

    CUtlVector<int> newNormalIndices;
    newNormalIndices.SetCount(oldNormalIndices.Count());

    for (int i = 0; i < oldNormalIndices.Count(); ++i) {
        newNormalIndices[i] = normalMap[oldNormalIndices[i]];
    }

    pDmeVertexData->RemoveAllVertexData(nNormalFieldIndex);
    nNormalFieldIndex = pDmeVertexData->CreateField(CDmeVertexDeltaData::FIELD_NORMAL);
    pDmeVertexData->AddVertexData(nNormalFieldIndex, newNormalData.Count());
    pDmeVertexData->SetVertexData(nNormalFieldIndex, 0, newNormalData.Count(), AT_VECTOR3, newNormalData.Base());
    pDmeVertexData->SetVertexIndices(nNormalFieldIndex, 0, newNormalIndices.Count(), newNormalIndices.Base());

    return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMesh::NormalizeNormals() {
    Vector vNormal;

    for (int i = 0; i < this->BaseStateCount(); ++i) {
        CDmeVertexData *pDmeVertexData = GetBaseState(i);
        if (!pDmeVertexData)
            continue;

        FieldIndex_t nNormalIndex = pDmeVertexData->FindFieldIndex(CDmeVertexData::FIELD_NORMAL);
        if (nNormalIndex < 0)
            continue;

        CDmAttribute *pDmNormalAttr = pDmeVertexData->GetVertexData(nNormalIndex);
        if (!pDmNormalAttr)
            continue;

        CDmrArray<Vector> normalData(pDmNormalAttr);
        for (int j = 0; j < normalData.Count(); ++j) {
            vNormal = normalData.Get(j);
            VectorNormalize(vNormal);
            normalData.Set(j, vNormal);
        }
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMesh::CollapseRedundantNormals(float flNormalBlend) {
    NormalizeNormals();

    CDmeVertexData *pDmeBind = GetBindBaseState();
    if (!pDmeBind)
        return;

    CUtlVector<int> normalMap;

    const int nDeltaStateCount = DeltaStateCount();
    if (nDeltaStateCount <= 0) {
        // No deltas
        if (CollapseRedundantBaseNormalsAggressive(pDmeBind, flNormalBlend)) {
            // Collapse any other states
            for (int i = 0; i < BaseStateCount(); ++i) {
                CDmeVertexData *pDmeVertexData = GetBaseState(i);
                if (!pDmeVertexData || pDmeVertexData == pDmeBind)
                    continue;

                CollapseRedundantBaseNormalsAggressive(pDmeVertexData, flNormalBlend);
            }
        }
    }
    else {
        // Collapse the base state
        if (CollapseRedundantBaseNormals(pDmeBind, normalMap, flNormalBlend)) {
            // Collapse any delta states using the baseState normal map
            for (int i = 0; i < DeltaStateCount(); ++i) {
                CDmeVertexDeltaData *pDmeDeltaData = GetDeltaState(i);
                if (!pDmeDeltaData)
                    continue;

                CollapseRedundantDeltaNormals(pDmeDeltaData, normalMap);
            }

            // Collapse any other states
            for (int i = 0; i < BaseStateCount(); ++i) {
                CDmeVertexData *pDmeVertexData = GetBaseState(i);
                if (!pDmeVertexData || pDmeVertexData == pDmeBind)
                    continue;

                CollapseRedundantBaseNormals(pDmeVertexData, normalMap, flNormalBlend);
            }
        }
    }
}

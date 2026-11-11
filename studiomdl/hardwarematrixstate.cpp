//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "studiomdl/platform_compat.h"
#include "studiomdl/hardwarematrixstate.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include "studio.h"
#include "studiomdl/studiomdl.h"

CHardwareMatrixState::CHardwareMatrixState() {
    m_LRUCounter = 0;
    m_NumMatrices = 0;
    m_savedMatrixState.clear();
}

void CHardwareMatrixState::Init(const uint32_t numHardwareMatrices) {
    m_NumMatrices = numHardwareMatrices;
    m_matrixState.clear();
    m_matrixState.resize(m_NumMatrices);
    Assert(!m_matrixState.empty());
    m_savedMatrixState.clear();
    m_savedMatrixState.resize(m_NumMatrices);
    Assert(!m_savedMatrixState.empty());
    m_LRUCounter = 0;
    m_AllocatedMatrices = 0;

    for (uint32_t i = 0; i < m_NumMatrices; i++) {
        m_matrixState[i].allocated = false;
    }
}

bool CHardwareMatrixState::AllocateMatrix(uint32_t globalMatrixID) {
    if (IsMatrixAllocated(globalMatrixID)) {
        return true;
    }

    for (int i = 0; i < m_NumMatrices; i++) {
        if (!m_matrixState[i].allocated) {
            m_matrixState[i].globalMatrixID = globalMatrixID;
            m_matrixState[i].allocated = true;
            m_matrixState[i].lastUsageID = m_LRUCounter++;
            ++m_AllocatedMatrices;
            DumpState();
            return true;
        }
    }
    DumpState();
    return false;
}

uint32_t CHardwareMatrixState::FindLocalLRUIndex() {
    uint32_t oldestLRUCounter = INT_MAX;
    uint32_t oldestID = 0;

    for (uint32_t i = 0; i < m_NumMatrices; i++) {
        if (!m_matrixState[i].allocated) {
            continue;
        }
        if (m_matrixState[i].lastUsageID < oldestLRUCounter) {
            oldestLRUCounter = m_matrixState[i].lastUsageID;
            oldestID = i;
        }
    }

    Assert(oldestLRUCounter != INT_MAX);
    return oldestID;
}

void CHardwareMatrixState::DeallocateLRU() {
    const uint32_t id = FindLocalLRUIndex();
    m_matrixState[id].allocated = false;
    --m_AllocatedMatrices;
}

void CHardwareMatrixState::DeallocateLRU(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        DeallocateLRU();
    }
}

bool CHardwareMatrixState::IsMatrixAllocated(const uint32_t globalMatrixID) const {
    for (uint32_t i = 0; i < m_NumMatrices; i++) {
        if (m_matrixState[i].globalMatrixID == globalMatrixID &&
            m_matrixState[i].allocated) {
            return true;
        }
    }
    return false;
}

void CHardwareMatrixState::DeallocateAll() {
    int i;

    DumpState();
    for (i = 0; i < m_NumMatrices; i++) {
        m_matrixState[i].allocated = false;
        m_matrixState[i].globalMatrixID = INT_MAX;
        m_matrixState[i].lastUsageID = INT_MAX;
    }
    m_AllocatedMatrices = 0;
    DumpState();
}

void CHardwareMatrixState::SaveState() {
    int i;

    for (i = 0; i < m_NumMatrices; i++) {
        m_savedMatrixState[i] = m_matrixState[i];
    }
}

void CHardwareMatrixState::RestoreState() {
    int i;

    for (i = 0; i < m_NumMatrices; i++) {
        m_matrixState[i] = m_savedMatrixState[i];
    }
}

uint32_t CHardwareMatrixState::AllocatedMatrixCount() const {
    return m_AllocatedMatrices;
}

uint32_t CHardwareMatrixState::FreeMatrixCount() const {
    return m_NumMatrices - m_AllocatedMatrices;
}

uint32_t CHardwareMatrixState::GetNthBoneGlobalID(uint32_t n) const {
    int m = 0;

    for (int i = 0; i < m_NumMatrices; i++) {
        if (m_matrixState[i].allocated) {
            if (n == m) {
                return m_matrixState[i].globalMatrixID;
            }
            m++;
        }
    }
    Assert(0);
    MdlError("GetNthBoneGlobalID() Failure\n");
    return 0;
}

void CHardwareMatrixState::DumpState() {
    int i;
    static char buf[256];

    //#ifndef _DEBUG
    return;
    //#endif

    OutputDebugString("DumpState\n:");
    for (i = 0; i < m_NumMatrices; i++) {
        if (m_matrixState[i].allocated) {
            sprintf(buf, "%d: allocated: %s lastUsageID: %d globalMatrixID: %d\n",
                    i,
                    m_matrixState[i].allocated ? "true " : "false",
                    m_matrixState[i].lastUsageID,
                    m_matrixState[i].globalMatrixID);
            OutputDebugString(buf);
        }
    }
}

uint32_t CHardwareMatrixState::FindHardwareMatrix(const uint32_t globalMatrixID) const {
    for (int i = 0; i < m_NumMatrices; i++) {
        if (m_matrixState[i].globalMatrixID == globalMatrixID) {
            return i;
        }
    }

    Assert(0);
    MdlError("barfing in FindHardwareMatrix\n");

    return 0;
}

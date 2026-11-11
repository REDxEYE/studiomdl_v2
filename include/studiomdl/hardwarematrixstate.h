//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once
#pragma once

// This emulates the hardware matrix palette and keeps up with 
// matrix usage, LRU's matrices, etc.
class CHardwareMatrixState {
public:
    CHardwareMatrixState();

    void Init(uint32_t numHardwareMatrices);

    // return false if there is no slot for this matrix.
    bool AllocateMatrix(uint32_t globalMatrixID);

    // deallocate the least recently used matrix
    void DeallocateLRU();

    void DeallocateLRU(uint32_t n);

    // return true if a matrix is allocate.
    bool IsMatrixAllocated(uint32_t globalMatrixID) const;

    // flush usage flags - signifies that none of the matrices are being used in the current strip
    // do this when starting a new strip.
    void SetAllUnused();

    void DeallocateAll();

    // save the complete state of the hardware matrices
    void SaveState();

    // restore the complete state of the hardware matrices
    void RestoreState();

    // Returns the number of free + unsed matrices
    uint32_t AllocatedMatrixCount() const;

    uint32_t FreeMatrixCount() const;

    uint32_t GetNthBoneGlobalID(uint32_t n) const;

    void DumpState();

private:
    uint32_t FindHardwareMatrix(uint32_t globalMatrixID) const;

    uint32_t FindLocalLRUIndex();

    // Increment and return LRU counter.
    struct MatrixState_t {
        bool allocated;
        uint32_t lastUsageID;
        uint32_t globalMatrixID;
    };

    uint32_t m_LRUCounter;
    uint32_t m_NumMatrices;
    uint32_t m_AllocatedMatrices;

    std::vector<MatrixState_t> m_matrixState;
    std::vector<MatrixState_t> m_savedMatrixState;
};

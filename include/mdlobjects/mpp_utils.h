//===== Copyright (c) 1996-2009, Valve Corporation, All rights reserved. =====
//
// Purpose:
//
//============================================================================


#pragma once

// Valve includes
#include "movieobjects/dmemodel.h"
#include "mdlobjects/dmeasset.h"
#include "mdlobjects/dmesequence.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"


//-----------------------------------------------------------------------------
// Ok to pass NULL
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Iterates over all CDmeSequence's in the MPP File (Not CDmeMultiSequence)
// Get() only returns NULL if IsDone() also returns true so this usage is safe
//
// for ( MppSequenceIt sIt( pDmeAssetRoot ); !sIt.IsDone(); sIt.Next() )
// {
//    sIt.Get()->SomeFunctionOnCDmeSequence()
// }
//-----------------------------------------------------------------------------
class MppSequenceIt
{
public:
	MppSequenceIt( CDmeAssetRoot *pDmeAssetRoot );
	CDmeSequence *Get() const;
	void Next();
	void Reset();

protected:
	CUtlVector< DmElementHandle_t > m_hDmeSequenceList;
	int m_nSequenceIndex;
};

// Masks for MppGetSkeletonLIst
enum MppSkeletonMask_t
{
	MPP_ANIM_SKELETON_MASK =	1 << 0,
	MPP_PHYSICS_SKELETON_MASK = 1 << 1,
	MPP_MODEL_SKELETON_MASK =	1 << 2,
	MPP_ALL_SKELETON_MASK = MPP_ANIM_SKELETON_MASK | MPP_PHYSICS_SKELETON_MASK | MPP_MODEL_SKELETON_MASK
};

// Utility to return DmElement id as name:string
std::string ComputeDmElementIdStr(const CDmElement* pDmElement);


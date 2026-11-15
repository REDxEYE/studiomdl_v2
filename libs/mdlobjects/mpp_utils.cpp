//====== Copyright (c) 1996-2009, Valve Corporation, All rights reserved. =====
//
// Purpose:
//
//=============================================================================


// Valve includes
#include "tier1/utlstack.h"
#include "movieobjects/dmeanimationlist.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmeconnectionoperator.h"
#include "movieobjects/dmelog.h"
#include "mdlobjects/dmebbox.h"
#include "mdlobjects/dmebodygrouplist.h"
#include "mdlobjects/dmebodygroup.h"
#include "mdlobjects/dmecollisionmodel.h"
#include "mdlobjects/dmeeyeball.h"
#include "mdlobjects/dmeeyeballglobals.h"
#include "mdlobjects/dmeik.h"
#include "mdlobjects/dmelodlist.h"
#include "mdlobjects/dmelod.h"
#include "mdlobjects/dmesequencelist.h"
#include "mdlobjects/dmesequence.h"
#include "mdlobjects/dmedefinebonelist.h"
#include "mdlobjects/dmedefinebone.h"
#include "mdlobjects/mpp_utils.h"

























//=============================================================================
// MppSequenceIt - MPP File Sequence Iterator
//=============================================================================
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
MppSequenceIt::MppSequenceIt( CDmeAssetRoot *pDmeAssetRoot )
: m_nSequenceIndex( -1 )
{
	if ( pDmeAssetRoot )
	{
		CDmeSequenceList *pDmeSequenceList = pDmeAssetRoot->GetValueElement< CDmeSequenceList >( "sequenceList" );
		if ( pDmeSequenceList )
		{
			CUtlVector< CDmeSequenceBase * > sortedSequenceList;
			pDmeSequenceList->GetSortedSequenceList( sortedSequenceList );

			const int nSortedSequenceCount = sortedSequenceList.size();
			m_hDmeSequenceList.EnsureCapacity( nSortedSequenceCount );
			for ( int i = 0; i < nSortedSequenceCount; ++i )
			{
				CDmeSequence *pDmeSequence = CastElement< CDmeSequence >( sortedSequenceList[i] );
				if ( !pDmeSequence )
					continue;

				m_hDmeSequenceList.AddToTail( pDmeSequence->GetHandle() );
			}
		}
	}

	Next();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
CDmeSequence *MppSequenceIt::Get() const
{
	if ( m_nSequenceIndex < 0 || m_nSequenceIndex >= m_hDmeSequenceList.size() )
		return nullptr;

	return CastElement< CDmeSequence >( g_pDataModel->GetElement( m_hDmeSequenceList[m_nSequenceIndex] ) );
}



//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void MppSequenceIt::Next()
{
	for ( int i = m_nSequenceIndex + 1; i < m_hDmeSequenceList.size(); ++i )
	{
		CDmeSequence *pDmeSequence = CastElement< CDmeSequence >( g_pDataModel->GetElement( m_hDmeSequenceList[i] ) );
		if ( pDmeSequence )
		{
			m_nSequenceIndex = i;
			return;
		}
	}

	m_nSequenceIndex = m_hDmeSequenceList.size();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void MppSequenceIt::Reset()
{
	m_nSequenceIndex = -1;
	Next();
}















//-----------------------------------------------------------------------------
// Utility to return DmElement id as name:string
//-----------------------------------------------------------------------------
CUtlString ComputeDmElementIdStr( const CDmElement *pDmElement )
{
	if ( !pDmElement )
		return CUtlString( "NULL(Unknown):\"Unknown\"" );

	CUtlString sReturn;

	sReturn = pDmElement->GetTypeString();
	sReturn += "(";
	sReturn = pDmElement->GetName();
	sReturn += "):\"";

	char pszBuf[64];
	UniqueIdToString( pDmElement->GetId(), pszBuf, ARRAYSIZE( pszBuf ) );
	sReturn += pszBuf;

	sReturn += "\"";

	return sReturn;
}
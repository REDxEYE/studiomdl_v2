//====== Copyright � 1996-2008, Valve Corporation, All rights reserved. =====//
//
// A list of DmeSequences's
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmesequence.h"
#include "mdlobjects/dmesequencelist.h"
#include "mdlobjects/dmeik.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeSequenceList, CDmeSequenceList );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSequenceList::OnConstruction()
{
	m_Sequences.Init( this, "sequences" );
	m_eIkChainList.Init( this, "ikChainList" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSequenceList::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// Returns a sorted of the sequences in the m_Sequences attribute in order of priority
// Sequences that are referred to come before the sequences that refer to them
//-----------------------------------------------------------------------------
void CDmeSequenceList::GetSortedSequenceList( CUtlVector< CDmeSequenceBase * > &sortedSequenceList ) const
{
	sortedSequenceList.RemoveAll();

	const int nSequenceCount = m_Sequences.size();

	sortedSequenceList.EnsureCapacity( nSequenceCount );

	for ( int i = 0; i < nSequenceCount; ++i )
	{
		CDmeSequenceBase *pDmeSequenceBase = m_Sequences[i];
		if ( !pDmeSequenceBase )
			continue;

		sortedSequenceList.AddToTail( pDmeSequenceBase );
	}

	qsort( sortedSequenceList.data(), sortedSequenceList.size(), sizeof( CDmeSequenceBase * ), CDmeSequenceBase::QSortFunction );
}



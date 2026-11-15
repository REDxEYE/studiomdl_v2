//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A fast stack memory allocator that uses virtual memory if available
//
//===========================================================================//

#pragma once

#if defined( _WIN32 )
#pragma once
#endif

#include "tier1/utlvector.h"

#if defined( _WIN32 )
#define MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
#endif

//-----------------------------------------------------------------------------

typedef unsigned MemoryStackMark_t;

class CMemoryStack
{
public:
	CMemoryStack();
	~CMemoryStack();

	bool Init( const char *pszAllocOwner, unsigned maxSize = 0, unsigned commitIncrement = 0, unsigned initialCommit = 0, unsigned alignment = 16 );
	void Term();

	int GetSize() const;
	int GetMaxSize() const ;
	int	GetUsed() const;
	
	void *Alloc( unsigned bytes, bool bClear = false ) RESTRICT;

	MemoryStackMark_t GetCurrentAllocPoint() const;
	void FreeToAllocPoint( MemoryStackMark_t mark, bool bDecommit = true );
	void FreeAll( bool bDecommit = true );
	
	void Access( void **ppRegion, unsigned *pBytes );

	void PrintContents() const;

	void *GetBase();
	const void *GetBase() const {  return const_cast<CMemoryStack *>(this)->GetBase(); }

	bool CommitSize( int nBytes );

	void SetAllocOwner( const char *pszAllocOwner );

private:
	bool CommitTo( uint8_t * ) RESTRICT;

	uint8_t *m_pNextAlloc; // Current alloc point (m_pNextAlloc - m_pBase == allocated bytes)
	uint8_t *m_pCommitLimit; // The current end of the committed memory. On systems without dynamic commit/decommit this is always m_pAllocLimit
	uint8_t *m_pAllocLimit; // The top of the allocated address space (m_pBase + m_maxSize)
	// Track the highest alloc limit seen.
	uint8_t *m_pHighestAllocLimit;
	uint8_t *m_pBase;
	bool m_bPhysical;
	char *m_pszAllocOwner;

	unsigned m_maxSize; // m_maxSize stores how big the stack can grow. It measures the reservation size.
	unsigned m_alignment;
#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
	unsigned m_commitIncrement;
	unsigned m_minCommit;
#endif

private:
	// Make the assignment operator and copy constructor private and unimplemented.
	CMemoryStack& operator=( const CMemoryStack& );
	CMemoryStack( const CMemoryStack& );
};

//-------------------------------------

FORCEINLINE void *CMemoryStack::Alloc( unsigned bytes, bool bClear ) RESTRICT
{
	Assert( m_pBase );

	bytes = MAX( bytes, m_alignment );
	bytes = AlignValue( bytes, m_alignment );

	void *pResult = m_pNextAlloc;
	uint8_t *pNextAlloc = m_pNextAlloc + bytes;

	if ( pNextAlloc > m_pCommitLimit )
	{
		if ( !CommitTo( pNextAlloc ) )
		{
			return nullptr;
		}
	}

	if ( bClear )
	{
		memset( pResult, 0, bytes );
	}

	m_pNextAlloc = pNextAlloc;
	m_pHighestAllocLimit = Max( m_pNextAlloc, m_pHighestAllocLimit );

	return pResult;
}

//-------------------------------------

inline bool CMemoryStack::CommitSize( int nBytes )
{
	if ( GetSize() != nBytes )
	{
		return CommitTo( m_pBase + nBytes );
	}
	return true;
}

//-------------------------------------

// How big can this memory stack grow? This is equivalent to how many
// bytes are reserved.
inline int CMemoryStack::GetMaxSize() const
{ 
	return m_maxSize;
}

//-------------------------------------

inline int CMemoryStack::GetUsed() const
{ 
	return ( m_pNextAlloc - m_pBase ); 
}

//-------------------------------------

inline void *CMemoryStack::GetBase()
{
	return m_pBase;
}

//-------------------------------------

inline MemoryStackMark_t CMemoryStack::GetCurrentAllocPoint() const
{
	return ( m_pNextAlloc - m_pBase );
}





//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#if defined( _WIN32 )
#define WIN_32_LEAN_AND_MEAN
#include <windows.h>
#define VA_COMMIT_FLAGS MEM_COMMIT
#define VA_RESERVE_FLAGS MEM_RESERVE
#endif


#include "memstack.h"
#include "utlmap.h"

void PrintStatus( void* p )
{
	CMemoryStack* pMemoryStack = (CMemoryStack*)p;

	pMemoryStack->PrintContents();
}

CMemoryStack::CMemoryStack()
 : 	m_pNextAlloc(nullptr)
	, m_pCommitLimit(nullptr)
	, m_pAllocLimit(nullptr)
	, m_pHighestAllocLimit(nullptr)
	, m_pBase(nullptr)
 	, m_maxSize( 0 )
	, m_alignment( 16 )
#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
 	, m_commitIncrement( 0 )
	, m_minCommit( 0 )
#endif
{
	m_pszAllocOwner = strdup( "CMemoryStack unattributed" );
}
	
//-------------------------------------

CMemoryStack::~CMemoryStack()
{
	if ( m_pBase )
		Term();
	free( m_pszAllocOwner );
}

//-------------------------------------

bool CMemoryStack::Init( const char *pszAllocOwner, unsigned maxSize, unsigned commitIncrement, unsigned initialCommit, unsigned alignment )
{
	Assert( !m_pBase );

	m_bPhysical = false;

	m_maxSize = maxSize;
	m_alignment = AlignValue( alignment, 4 );

	Assert( m_alignment == alignment );
	Assert( m_maxSize > 0 );

	SetAllocOwner( pszAllocOwner );

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE


	if ( commitIncrement != 0 )
	{
		m_commitIncrement = commitIncrement;
	}

	unsigned pageSize;

	SYSTEM_INFO sysInfo;
	GetSystemInfo( &sysInfo );
	Assert( !( sysInfo.dwPageSize & (sysInfo.dwPageSize-1)) );
	pageSize = sysInfo.dwPageSize;

	if ( m_commitIncrement == 0 )
	{
		m_commitIncrement = pageSize;
	}
	else
	{
		m_commitIncrement = AlignValue( m_commitIncrement, pageSize );
	}

	m_maxSize = AlignValue( m_maxSize, m_commitIncrement );
	
	Assert( m_maxSize % pageSize == 0 && m_commitIncrement % pageSize == 0 && m_commitIncrement <= m_maxSize );

	m_pBase = (unsigned char *)VirtualAlloc( NULL, m_maxSize, VA_RESERVE_FLAGS, PAGE_NOACCESS );
	if ( !m_pBase )
	{
		MemAlloc_OutOfMemory();
		return false;
	}
	m_pCommitLimit = m_pNextAlloc = m_pBase;

	if ( initialCommit )
	{
		initialCommit = AlignValue( initialCommit, m_commitIncrement );
		Assert( initialCommit <= m_maxSize );
		if ( !VirtualAlloc( m_pCommitLimit, initialCommit, VA_COMMIT_FLAGS, PAGE_READWRITE ) )
		{
			MemAlloc_OutOfMemory();
			return false;
		}
		m_minCommit = initialCommit;
		m_pCommitLimit += initialCommit;
	}

#else
	m_pBase = (uint8_t*)MemAlloc_AllocAligned( m_maxSize, alignment ? alignment : 1 );
	m_pNextAlloc = m_pBase;
	m_pCommitLimit = m_pBase + m_maxSize;
#endif

	m_pHighestAllocLimit = m_pNextAlloc;

	m_pAllocLimit = m_pBase + m_maxSize;

	return ( m_pBase != nullptr);
}

//-------------------------------------


//-------------------------------------

void CMemoryStack::Term()
{
	FreeAll();
	if ( m_pBase )
	{

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
		VirtualFree( m_pBase, 0, MEM_RELEASE );
#else
		MemAlloc_FreeAligned( m_pBase );
#endif
		m_pBase = nullptr;
		// Zero these variables to avoid getting misleading mem_dump
		// results when m_pBase is NULL.
		m_pNextAlloc = nullptr;
		m_pCommitLimit = nullptr;
		m_pHighestAllocLimit = nullptr;
		m_maxSize = 0;
	}
}

//-------------------------------------

int CMemoryStack::GetSize() const
{ 
	if ( m_bPhysical )
		return m_maxSize;

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
	return m_pCommitLimit - m_pBase; 
#else
	return m_maxSize;
#endif
}


//-------------------------------------

bool CMemoryStack::CommitTo( uint8_t *pNextAlloc ) RESTRICT
{
	if ( m_bPhysical )
	{
		return NULL;
	}

#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
	unsigned char *	pNewCommitLimit = AlignValue( pNextAlloc, m_commitIncrement );
	ptrdiff_t commitIncrement 		= pNewCommitLimit - m_pCommitLimit;
	
	if( m_pCommitLimit + commitIncrement > m_pAllocLimit )
	{
		MemAlloc_OutOfMemory();
		return false;
	}

	if ( pNewCommitLimit > m_pCommitLimit )
	{
		if ( !VirtualAlloc( m_pCommitLimit, commitIncrement, VA_COMMIT_FLAGS, PAGE_READWRITE ) )
		{
			MemAlloc_OutOfMemory();
			return false;
		}
		m_pCommitLimit = pNewCommitLimit;
	}
	else if ( pNewCommitLimit < m_pCommitLimit )
	{
		if  ( m_pNextAlloc > pNewCommitLimit )
		{
			Warning( "ATTEMPTED TO DECOMMIT OWNED MEMORY STACK SPACE\n" );
			pNewCommitLimit = AlignValue( m_pNextAlloc, m_commitIncrement );
		}

		if ( pNewCommitLimit < m_pCommitLimit )
		{
			ptrdiff_t decommitIncrement = m_pCommitLimit - pNewCommitLimit;
			VirtualFree( pNewCommitLimit, decommitIncrement, MEM_DECOMMIT );
			m_pCommitLimit = pNewCommitLimit;
		}
	}

	return true;
#else
	return false;
#endif
}

// Identify the owner of this memory stack's memory
void CMemoryStack::SetAllocOwner( const char *pszAllocOwner )
{
	if ( !pszAllocOwner || !Q_strcmp( m_pszAllocOwner, pszAllocOwner ) )
		return;
	free( m_pszAllocOwner );
	m_pszAllocOwner = strdup( pszAllocOwner );
}

//-------------------------------------

void CMemoryStack::FreeToAllocPoint( MemoryStackMark_t mark, bool bDecommit )
{
	mark = AlignValue( mark, m_alignment );
	uint8_t *pAllocPoint = m_pBase + mark;

	Assert( pAllocPoint >= m_pBase && pAllocPoint <= m_pNextAlloc );
	if ( pAllocPoint >= m_pBase && pAllocPoint <= m_pNextAlloc )
	{
		m_pNextAlloc = pAllocPoint;
#ifdef MEMSTACK_VIRTUAL_MEMORY_AVAILABLE
		if ( bDecommit && !m_bPhysical )
		{
			CommitTo( MAX( m_pNextAlloc, (m_pBase + m_minCommit) ) );
		}
#endif
	}
}

//-------------------------------------

void CMemoryStack::FreeAll( bool bDecommit )
{
	if ( m_pBase && ( m_pBase < m_pCommitLimit ) )
	{
		FreeToAllocPoint( 0, bDecommit );
	}
}

//-------------------------------------

void CMemoryStack::Access( void **ppRegion, unsigned *pBytes )
{
	*ppRegion = m_pBase;
	*pBytes = ( m_pNextAlloc - m_pBase);
}

//-------------------------------------

void CMemoryStack::PrintContents() const
{
	size_t highest = m_pHighestAllocLimit - m_pBase;
#ifdef PLATFORM_WINDOWS_PC
	MEMORY_BASIC_INFORMATION info;
	char moduleName[260];
	strcpy( moduleName, "unknown module" );
	// Because this code is statically linked into each DLL, this function and the PrintStatus
	// function will be in the DLL that constructed the CMemoryStack object. We can then
	// retrieve the DLL name to give slightly more verbose memory dumps.
	if ( VirtualQuery( (LPCVOID)&PrintStatus, &info, sizeof( info ) ) == sizeof( info ) )
	{
		GetModuleFileName( (HMODULE) info.AllocationBase, moduleName, _countof( moduleName ) );
		moduleName[ _countof( moduleName )-1 ] = 0;
	}
	Msg( "CMemoryStack %s in %s\n", m_pszAllocOwner, moduleName );
#else
	Msg( "CMemoryStack %s\n", m_pszAllocOwner );
#endif
	Msg( "    Total used memory:      %d KB\n", GetUsed() / 1024 );
	Msg( "    Total committed memory: %d KB\n", GetSize() / 1024 );
	Msg( "    Max committed memory: %u KB out of %d KB\n", (unsigned)highest / 1024, GetMaxSize() / 1024 );
}


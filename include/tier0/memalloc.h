//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Heap helpers. Plain C runtime allocation plus aligned variants; no allocator
// interface, no tracking.
//
//=============================================================================//

#pragma once

#ifdef _WIN32
#pragma once
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "tier0/platform.h"

inline void *MemAlloc_Alloc( size_t nSize )
{
	return malloc( nSize );
}

inline void MemAlloc_Free( void *pMem )
{
	free( pMem );
}

// Usable size of a block from MemAlloc_Alloc (may be larger than requested)
inline size_t MemAlloc_GetSize( void *pMem )
{
#ifdef _WIN32
	return _msize( pMem );
#else
	return malloc_usable_size( pMem );
#endif
}

inline void *MemAlloc_AllocAligned( size_t nSize, size_t nAlign )
{
#ifdef _WIN32
	return _aligned_malloc( nSize, nAlign );
#else
	// posix_memalign needs a power-of-two multiple of sizeof(void*)
	void *pMem = nullptr;
	return posix_memalign( &pMem, nAlign < sizeof( void * ) ? sizeof( void * ) : nAlign, nSize ) == 0 ? pMem : nullptr;
#endif
}

inline void MemAlloc_FreeAligned( void *pMem )
{
#ifdef _WIN32
	_aligned_free( pMem );
#else
	free( pMem );
#endif
}

inline void *MemAlloc_ReallocAligned( void *pMem, size_t nSize, size_t nAlign )
{
#ifdef _WIN32
	return _aligned_realloc( pMem, nSize, nAlign );
#else
	void *pNew = MemAlloc_AllocAligned( nSize, nAlign );
	if ( pNew && pMem )
	{
		size_t nOldSize = malloc_usable_size( pMem );
		memcpy( pNew, pMem, nSize < nOldSize ? nSize : nOldSize );
		free( pMem );
	}
	return pNew;
#endif
}

// Allocation failed: break into an attached debugger, otherwise exit.
inline void MemAlloc_OutOfMemory()
{
	if ( Plat_IsInDebugSession() )
	{
		DebuggerBreak();
	}
	else
	{
		Plat_ExitProcess( EXIT_FAILURE );
	}
}

struct aligned_tmp_t
{
	// empty base class
};

// template here to allow adding alignment at levels of hierarchy that aren't the base
template< int bytesAlignment = 16, class T = aligned_tmp_t >
class CAlignedNewDelete : public T
{
public:
	void *operator new( size_t nSize )
	{
		return MemAlloc_AllocAligned( nSize, bytesAlignment );
	}

	void operator delete( void *pData )
	{
		MemAlloc_FreeAligned( pData );
	}
};


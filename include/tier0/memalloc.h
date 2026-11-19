//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Heap helpers. Plain C runtime allocation plus aligned variants; no allocator
// interface, no tracking.
//
//=============================================================================//

#pragma once

#include <cstring>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <type_traits>
#include <cstdio>
#include <new>

#include "tier0/platform.h"

// Allocation failure is std::bad_alloc, never the end of the process (the entry points handle it).
template<typename T> requires std::is_pod_v<T>
T* allocate() {
	auto raw = malloc(sizeof(T));
	if (raw==nullptr) {
		throw std::bad_alloc();
	}
	return static_cast<T*>(raw);
}

template<typename T>
T* c_allocate() {
	auto raw = malloc(sizeof(T));
	if (raw==nullptr) {
		throw std::bad_alloc();
	}
	memset(raw, 0, sizeof(T));
	// T() (value-initialization) zeroes members without default initializers; with "new T" the compiler may treat
	// the memset above as a dead store once T's constructor is non-trivial (e.g. a std::string member)
	return ::new(raw) T();
}

template<typename T> requires std::is_pod_v<T>
T* allocate(const size_t count) {
	auto raw = malloc(sizeof(T) * count);
	if (raw==nullptr) {
		throw std::bad_alloc();
	}
	return static_cast<T*>(raw);
}

template<typename T>
T* c_allocate(const size_t count) {
	auto* raw = static_cast<char *>(malloc(sizeof(T) * count));
	if (raw==nullptr) {
		throw std::bad_alloc();
	}
	memset(raw, 0, sizeof(T) * count);
	for (int i = 0; i < count; ++i) {
		::new(raw + i * sizeof(T)) T();
	}
	return static_cast<T*>(raw);
}




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
#elif defined( __APPLE__ )
	return malloc_size( pMem );
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
		size_t nOldSize = MemAlloc_GetSize( pMem );
		memcpy( pNew, pMem, nSize < nOldSize ? nSize : nOldSize );
		free( pMem );
	}
	return pNew;
#endif
}

// Allocation failed: break into an attached debugger, then std::bad_alloc.
inline void MemAlloc_OutOfMemory()
{
	if ( Plat_IsInDebugSession() )
	{
		DebuggerBreak();
	}
	throw std::bad_alloc();
}


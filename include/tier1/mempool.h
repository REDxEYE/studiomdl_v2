//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//===========================================================================//

#pragma once



#include <string_view>

#include "tier0/memalloc.h"
#include "tier0/platform.h"
#include "tier1/utlvector.h"
#include "tier1/utlrbtree.h"

//-----------------------------------------------------------------------------
// Purpose: Optimized pool memory allocator
//-----------------------------------------------------------------------------

typedef void (*MemoryPoolReportFunc_t)( PRINTF_FORMAT_STRING char const* pMsg, ... );

class CUtlMemoryPool
{
public:
	// Ways the memory pool can grow when it needs to make a new blob.
	enum MemoryPoolGrowType_t
	{
		GROW_NONE=0,		// Don't allow new blobs.
		GROW_FAST=1,		// New blob size is numElements * (i+1)  (ie: the blocks it allocates
							// get larger and larger each time it allocates one).
		GROW_SLOW=2			// New blob size is numElements.
	};

				CUtlMemoryPool( int blockSize, int numElements, int growMode = GROW_FAST, const char *pszAllocOwner = nullptr, int nAlignment = 0 );
				~CUtlMemoryPool();

	void*		Alloc();	// Allocate the element size you specified in the constructor.
	void*		Alloc( size_t amount );
	void*		AllocZero();	// Allocate the element size you specified in the constructor, zero the memory before construction
	void*		AllocZero( size_t amount );
	void		Free(void *pMem);
	
	// Frees everything
	void		Clear();

	// Error reporting... 
	static void SetErrorReportFunc( MemoryPoolReportFunc_t func );

	// returns number of allocated blocks
	int Count() const		{ return m_BlocksAllocated; }
	int PeakCount() const	{ return m_PeakAlloc; }
	int BlockSize() const	{ return m_BlockSize; }

protected:
	class CBlob
	{
	public:
		CBlob	*m_pPrev, *m_pNext;
		int		m_NumBytes;		// Number of bytes in this blob.
		char	m_Data[1];
		char	m_Padding[3]; // to int align the struct
	};

	// Resets the pool
	void		Init();
	void		AddNewBlob();
	void		ReportLeaks();

	int			m_BlockSize;
	int			m_BlocksPerBlob;

	int			m_GrowMode;	// GROW_ enum.

	int				m_BlocksAllocated;
	int				m_PeakAlloc;
	unsigned short	m_nAlignment;
	unsigned short	m_NumBlobs;
	// Group up pointers at the end of the class to avoid padding bloat
	// FIXME: Change m_ppMemBlob into a growable array?
	void			*m_pHeadOfFreeList;
	const char *	m_pszAllocOwner;
	// CBlob could be not a multiple of 4 bytes so stuff it at the end here to keep us otherwise aligned
	CBlob			m_BlobHead;

	static MemoryPoolReportFunc_t g_ReportFunc;
};



//-----------------------------------------------------------------------------
// Wrapper macro to make an allocator that returns particular typed allocations
// and construction and destruction of objects.
//-----------------------------------------------------------------------------
template< class T >
class CClassMemoryPool : public CUtlMemoryPool
{
public:
	CClassMemoryPool(int numElements, int growMode = GROW_FAST, int nAlignment = 0 ) :
		CUtlMemoryPool( sizeof(T), numElements, growMode, nullptr, nAlignment ) {}

	T*		Alloc();
	T*		AllocZero();
	void	Free( T *pMem );

	void	Clear();
};


template< class T >
inline T* CClassMemoryPool<T>::Alloc()
{
	T *pRet;

	{
	pRet = (T*)CUtlMemoryPool::Alloc();
	}

	if ( pRet )
	{
		Construct( pRet );
	}
	return pRet;
}

template< class T >
inline T* CClassMemoryPool<T>::AllocZero()
{
	T *pRet;

	{
	pRet = (T*)CUtlMemoryPool::AllocZero();
	}

	if ( pRet )
	{
		Construct( pRet );
	}
	return pRet;
}

template< class T >
inline void CClassMemoryPool<T>::Free(T *pMem)
{
	if ( pMem )
	{
		Destruct( pMem );
	}

	CUtlMemoryPool::Free( pMem );
}

template< class T >
inline void CClassMemoryPool<T>::Clear()
{
	CUtlRBTree<void *, int> freeBlocks;
	SetDefLessFunc( freeBlocks );

	void *pCurFree = m_pHeadOfFreeList;
	while ( pCurFree != nullptr)
	{
		freeBlocks.Insert( pCurFree );
		pCurFree = *((void**)pCurFree);
	}

	for( CBlob *pCur=m_BlobHead.m_pNext; pCur != &m_BlobHead; pCur=pCur->m_pNext )
	{
		int nElements = pCur->m_NumBytes / this->m_BlockSize;
		T *p = ( T * ) AlignValue( pCur->m_Data, this->m_nAlignment );
		T *pLimit = p + nElements;
		while ( p < pLimit )
		{
			if ( freeBlocks.Find( p ) == freeBlocks.InvalidIndex() )
			{
				Destruct( p );
			}
			p++;
		}
	}

	CUtlMemoryPool::Clear();
}





//-----------------------------------------------------------------------------
// Macros that make it simple to make a class use a fixed-size allocator
// Put DECLARE_FIXEDSIZE_ALLOCATOR in the private section of a class,
// Put DEFINE_FIXEDSIZE_ALLOCATOR in the CPP file
//-----------------------------------------------------------------------------
#define DECLARE_FIXEDSIZE_ALLOCATOR( _class )									\
	public:																		\
		inline void* operator new( size_t size ) { return s_Allocator.Alloc(size); }   \
		inline void* operator new( size_t size, int nBlockUse, const std::string_view pFileName, int nLine ) { return s_Allocator.Alloc(size); }   \
		inline void  operator delete( void* p ) { s_Allocator.Free(p); }		\
		inline void  operator delete( void* p, int nBlockUse, const std::string_view pFileName, int nLine ) { s_Allocator.Free(p); }   \
	private:																		\
		static   CUtlMemoryPool   s_Allocator
    
#define DEFINE_FIXEDSIZE_ALLOCATOR( _class, _initsize, _grow )					\
	CUtlMemoryPool   _class::s_Allocator(sizeof(_class), _initsize, _grow, #_class " pool")

#define DEFINE_FIXEDSIZE_ALLOCATOR_ALIGNED( _class, _initsize, _grow, _alignment )		\
	CUtlMemoryPool   _class::s_Allocator(sizeof(_class), _initsize, _grow, #_class " pool", _alignment )

#define DECLARE_FIXEDSIZE_ALLOCATOR_MT( _class )									\
	public:																		\
	   inline void* operator new( size_t size ) { return s_Allocator.Alloc(size); }   \
	   inline void* operator new( size_t size, int nBlockUse, const std::string_view pFileName, int nLine ) { return s_Allocator.Alloc(size); }   \
	   inline void  operator delete( void* p ) { s_Allocator.Free(p); }		\
	   inline void  operator delete( void* p, int nBlockUse, const std::string_view pFileName, int nLine ) { s_Allocator.Free(p); }   \
	private:																		\
		static   CMemoryPoolMT   s_Allocator

#define DEFINE_FIXEDSIZE_ALLOCATOR_MT( _class, _initsize, _grow )					\
	CMemoryPoolMT   _class::s_Allocator(sizeof(_class), _initsize, _grow, #_class " pool")

//-----------------------------------------------------------------------------
// Macros that make it simple to make a class use a fixed-size allocator
// This version allows us to use a memory pool which is externally defined...
// Put DECLARE_FIXEDSIZE_ALLOCATOR_EXTERNAL in the private section of a class,
// Put DEFINE_FIXEDSIZE_ALLOCATOR_EXTERNAL in the CPP file
//-----------------------------------------------------------------------------

#define DECLARE_FIXEDSIZE_ALLOCATOR_EXTERNAL( _class )							\
   public:																		\
      inline void* operator new( size_t size )  { return s_pAllocator->Alloc(size); }   \
      inline void* operator new( size_t size, int nBlockUse, const std::string_view pFileName, int nLine )  { return s_pAllocator->Alloc(size); }   \
      inline void  operator delete( void* p )   { s_pAllocator->Free(p); }		\
   private:																		\
      static   CUtlMemoryPool*   s_pAllocator

#define DEFINE_FIXEDSIZE_ALLOCATOR_EXTERNAL( _class, _allocator )				\
   CUtlMemoryPool*   _class::s_pAllocator = _allocator



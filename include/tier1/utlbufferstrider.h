//====== Copyright (c) Valve Corporation, All rights reserved. =======//
#pragma once

#include "tier1/strtools.h"

//#include "tier0/memdbg_for_headers.h"

template< class T, class A >
class CUtlVector;


class CBufferStrider
{
public:
	CBufferStrider( void *pBuffer );

	template <typename T> T *Stride( int nCount = 1 );
	template <typename T> T StrideUnaligned( );
	template <typename T> void StrideUnaligned( T* pOut, int nCount = 1 );
	char* StrideString( );
	char* StrideString( const void *pEnd );
	template <typename T> T Peek( ){ return *(T*)Get(); }
	uint8_t *Get(){ return m_pBuffer; }
	void Set( void *p ){ m_pBuffer = ( uint8_t *)p; }
	void Align( uint32_t nAlignment ){ m_pBuffer = ( uint8_t* )( ( uintptr_t( m_pBuffer ) + nAlignment - 1 ) & ~uintptr_t( nAlignment - 1 ) ); }
protected:
	uint8_t *m_pBuffer;
};


template <typename T>
inline T *CBufferStrider::Stride( int nCount )
{
	uint32_t nAlignMask = VALIGNOF( T ) - 1; NOTE_UNUSED( nAlignMask );
	AssertDbg( !( uintptr_t( m_pBuffer ) & nAlignMask ) );
	T* p = ( T* ) m_pBuffer;
	m_pBuffer += sizeof( T ) * nCount;
	return p;
}

template <typename T>
inline T CBufferStrider::StrideUnaligned( )
{
	T out;
	memcpy( &out, m_pBuffer, sizeof( T ) );
	m_pBuffer += sizeof( T );
	return out;
}


template <typename T>
inline void CBufferStrider::StrideUnaligned( T* pOut, int nCount )
{
	memcpy( pOut, m_pBuffer, sizeof( T ) * nCount );
	m_pBuffer += sizeof( T ) * nCount;
}

inline char* CBufferStrider::StrideString( const void *pEnd )
{
	uint8_t *pNextItem = m_pBuffer;
	do
	{
		if ( pNextItem >= ( uint8_t* ) pEnd )
		{
			return NULL; // we steppend past end of buffer
		}
	}
	while ( *( pNextItem++ ) );
	// found end of string
	char *pString = ( char* ) m_pBuffer;
	m_pBuffer = pNextItem;
	return pString;
}


inline char* CBufferStrider::StrideString( )
{
	uint8_t *pNextItem = m_pBuffer;
	while ( *( pNextItem++ ) ) // this will crash in case of malformed (no terminating NULL) buffer
	{
		continue;
	}
	// found end of string
	char *pString = ( char* ) m_pBuffer;
	m_pBuffer = pNextItem;
	return pString;
}


inline
CBufferStrider::CBufferStrider( void *pBuffer )
: m_pBuffer( ( uint8_t* ) pBuffer )
{
}


#define MULTIBUFFER_ASSERTS

class CMultiBufferCounter
{
public:
	CMultiBufferCounter( )
		:m_nByteCount( 0 )
#ifdef MULTIBUFFER_ASSERTS
		, m_nMaxAlignment( 0 )
		, m_nRunAlignment( 0xFFFFFFFF )
#endif
	{}
	template <typename T> 
	T* operator()( T *& pDummy, uint32_t nCount )
	{
#ifdef MULTIBUFFER_ASSERTS
		const uint32_t nAlignMask = VALIGNOF( T ) - 1;
		Assert( m_nRunAlignment >= nAlignMask );
		m_nRunAlignment &= nAlignMask;
		m_nMaxAlignment |= nAlignMask;
#endif
		m_nByteCount += sizeof( T ) * nCount;
		return NULL; // we didn't allocate anything yet!
	}
	template< typename T, typename A >
	T* operator()( T*& pDummy, const CUtlVector< T, A > &arr )
	{
		( *this )( pDummy, arr.Count( ) );
		return NULL; // we didn't allocate anything yet
	}
	char *String( const char *& pDummy, const char *pSourceString )
	{
		if ( pSourceString )
		{
			( *this )( pDummy, uint32_t( V_strlen( pSourceString ) + 1 ) );
		}
		return NULL; // we didn't allocate anything yet
	}
	uint32_t GetByteCount( )const { return m_nByteCount; }
protected:
	uint32_t m_nByteCount;
#ifdef MULTIBUFFER_ASSERTS
	uint32_t m_nMaxAlignment;
	uint32_t m_nRunAlignment;
#endif
};


class CMultiBufferStrider: public CBufferStrider
{
public:
	CMultiBufferStrider( void *pBuffer ): CBufferStrider( pBuffer ){}
	template <typename T>
	T* operator() ( T*& refPtr, uint32_t nCount )
	{
		refPtr = nCount ? Stride< T >( nCount ) : NULL;
		return refPtr;
	}
	template < typename T, typename A >
	T* operator()( T *&refPtr, const CUtlVector< T, A > &arr )
	{
		if ( arr.IsEmpty( ) )
		{
			refPtr = NULL;
		}
		else
		{
			refPtr = Stride< T >( arr.Count( ) );
			V_memcpy( refPtr, arr.Base( ), sizeof( T ) * arr.Count( ) );
		}
		return refPtr;
	}

	char *String( const char *& refPtr, const char *pSourceString )
	{
		if ( pSourceString )
		{
			uint32_t nBufLen = V_strlen( pSourceString ) + 1;
			char *pOut = const_cast< char* >( ( *this )( refPtr, nBufLen ) );
			V_memcpy( pOut, pSourceString, nBufLen );
			AssertDbg( refPtr == pOut );
			return pOut;
		}
		else
		{
			return NULL; // we didn't allocate anything
		}
	}
};

//////////////////////////////////////////////////////////////////////////
// Derive from this buffer and override function Enum( T& ) that should
// enumerate all the pointers and sizes of buffers to be allocated
//
template <typename Derived>
class CMultiBufferHelper
{
public:
	CMultiBufferHelper( )
	{
		m_pMultiBuffer = NULL;
		m_nMultiBufferSize = 0;
	}
	~CMultiBufferHelper( )
	{
		if ( m_pMultiBuffer )
		{
			MemAlloc_FreeAligned( m_pMultiBuffer );
			m_pMultiBuffer = NULL;
			m_nMultiBufferSize = 0;
		}
	}
	uint8_t *GetBuffer( ) { return m_pMultiBuffer; }
	uint32_t GetBufferSize( ) { return m_nMultiBufferSize; }
	uint8_t *TakeBuffer( )
	{
		// forget the buffer
		uint8_t *pBuffer = m_pMultiBuffer;
		m_pMultiBuffer = NULL;
		m_nMultiBufferSize = 0;
		return pBuffer;
	}

protected:

	template <typename Ctx >
	void ReallocateMultiBuffer( Ctx &context )
	{
		if ( m_pMultiBuffer )
		{
			MemAlloc_FreeAligned( m_pMultiBuffer );
		}

		CMultiBufferCounter byteCounter;
		static_cast< Derived* >( this )->OnAllocateMultiBuffer( byteCounter, context );
		m_nMultiBufferSize = byteCounter.GetByteCount( );
		m_pMultiBuffer = ( uint8_t* )MemAlloc_AllocAligned( m_nMultiBufferSize, 16 );
		CMultiBufferStrider bufferStrider( m_pMultiBuffer );
		static_cast< Derived* >( this )->OnAllocateMultiBuffer( bufferStrider, context );
		Assert( m_pMultiBuffer + m_nMultiBufferSize == bufferStrider.Get( ) );
	}

protected:
	uint8_t *m_pMultiBuffer;
	uint32_t m_nMultiBufferSize;
};



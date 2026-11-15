//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once




#include "tier1/utlvector.h"
#include "tier1/utlqueue.h"


//-----------------------------------------------------------------------------
// Handles are 32 bits. Invalid handles are all 1s
//-----------------------------------------------------------------------------
typedef unsigned int UtlHandle_t;
#define UTLHANDLE_INVALID ((UtlHandle_t)~0)


//-----------------------------------------------------------------------------
// Purpose: This is a table used to allocate handles
// HandleBits specifies the max # of simultaneously allocated handles.
// An extra bit is used for the validity state
// The rest of the 32 bits are used for a serial number
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
class CUtlHandleTable
{
public:
	CUtlHandleTable();

	// Allocate, deallocate handles
	UtlHandle_t AddHandle();
	void RemoveHandle( UtlHandle_t h );

	// Set/get handle values
	void SetHandle( UtlHandle_t h, T *pData );
	T *GetHandle( UtlHandle_t h ) const;
	T *GetHandle( UtlHandle_t h, bool checkValidity ) const;

	// Is a handle valid?
	bool IsHandleValid( UtlHandle_t h ) const;

	// Iterate over handles; they may not be valid
	unsigned int GetValidHandleCount() const;
	unsigned int GetHandleCount() const;
	UtlHandle_t GetHandleFromIndex( int i ) const;
	int GetIndexFromHandle( UtlHandle_t h ) const;

	void MarkHandleInvalid( UtlHandle_t h );
	void MarkHandleValid( UtlHandle_t h );

private:
	struct HandleType_t
	{
		HandleType_t( unsigned int i, unsigned int s ) : nIndex( i ), nSerial( s )
		{
			Assert( i < ( 1 << HandleBits ) );
			Assert( s < ( 1 << ( 31 - HandleBits ) ) );
		}
		unsigned int nIndex  : HandleBits;
		unsigned int nSerial : 32 - HandleBits;
	};

	struct EntryType_t
	{
		EntryType_t() : m_nSerial( 0 ), nInvalid( 0 ), m_pData( nullptr ) {}
		unsigned int m_nSerial : 31;
		unsigned int nInvalid : 1;
		T *m_pData;
	};

	static unsigned int GetSerialNumber( UtlHandle_t handle );
	static unsigned int GetListIndex( UtlHandle_t handle );
	static UtlHandle_t CreateHandle( unsigned int nSerial, unsigned int nIndex );
	const EntryType_t *GetEntry( UtlHandle_t handle, bool checkValidity ) const;

	unsigned int m_nValidHandles;
	CUtlVector< EntryType_t > m_list;
	CUtlQueue< int > m_unused;

};


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
CUtlHandleTable<T, HandleBits>::CUtlHandleTable() : m_nValidHandles( 0 )
{
}


//-----------------------------------------------------------------------------
// Allocate, deallocate handles
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
UtlHandle_t CUtlHandleTable<T, HandleBits>::AddHandle()
{
	unsigned int nIndex = ( m_unused.Count() > 0 ) ? m_unused.RemoveAtHead() : m_list.AddToTail();

	EntryType_t &entry = m_list[ nIndex ];
	entry.nInvalid = 0;
	entry.m_pData = NULL;

	++m_nValidHandles;

	return CreateHandle( entry.m_nSerial, nIndex );
}

template< class T, int HandleBits >
void CUtlHandleTable<T, HandleBits>::RemoveHandle( UtlHandle_t handle )
{
	unsigned int nIndex = GetListIndex( handle );
	Assert( nIndex < ( unsigned int )m_list.Count() );
	if ( nIndex >= ( unsigned int )m_list.size() )
		return;

	EntryType_t &entry = m_list[ nIndex ];
	++entry.m_nSerial; // mark old serial# invalid
	if ( !entry.nInvalid )
	{
		entry.nInvalid = 1;
		--m_nValidHandles;
	}
	entry.m_pData = NULL;


	// If a handle has been used this many times, then we need to take it out of service, otherwise if the 
	//  serial # wraps around we'll possibly revalidate old handles and they'll start to point at the wrong objects.  Unlikely, but possible.
	bool bStopUsing = ( entry.m_nSerial >= ( (1 << ( 31 - HandleBits ) ) - 1 ) );
	if ( !bStopUsing )
	{
		m_unused.Insert( nIndex );
	}
}


//-----------------------------------------------------------------------------
// Set/get handle values
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
void CUtlHandleTable<T, HandleBits>::SetHandle( UtlHandle_t handle, T *pData )
{
	EntryType_t *entry = const_cast< EntryType_t* >( GetEntry( handle, false ) );
	Assert( entry );
	if ( entry == nullptr)
		return;

	// Validate the handle
	if ( entry->nInvalid )
	{
		++m_nValidHandles;
		entry->nInvalid = 0;
	}
	entry->m_pData = pData;
}

template< class T, int HandleBits >
T *CUtlHandleTable<T, HandleBits>::GetHandle( UtlHandle_t handle ) const
{
	const EntryType_t *entry = GetEntry( handle, true );
	return entry ? entry->m_pData : NULL;
}

template< class T, int HandleBits >
T *CUtlHandleTable<T, HandleBits>::GetHandle( UtlHandle_t handle, bool checkValidity ) const
{
	const EntryType_t *entry = GetEntry( handle, checkValidity );
	return entry ? entry->m_pData : NULL;
}


//-----------------------------------------------------------------------------
// Is a handle valid?
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
bool CUtlHandleTable<T, HandleBits>::IsHandleValid( UtlHandle_t handle ) const
{
	if ( handle == UTLHANDLE_INVALID )
		return false;

	unsigned int nIndex = GetListIndex( handle );
	AssertOnce( nIndex < ( unsigned int )m_list.Count() );
	if ( nIndex >= ( unsigned int )m_list.size() )
		return false;

	const EntryType_t &entry = m_list[ nIndex ];
	if ( entry.m_nSerial != GetSerialNumber( handle ) )
		return false;

	if ( 1 == entry.nInvalid )
		return false;

	return true;
}

	
//-----------------------------------------------------------------------------
// Current max handle
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
unsigned int CUtlHandleTable<T, HandleBits>::GetValidHandleCount() const
{
	return m_nValidHandles;
}

template< class T, int HandleBits >
unsigned int CUtlHandleTable<T, HandleBits>::GetHandleCount() const
{
	return m_list.size();
}

template< class T, int HandleBits >
UtlHandle_t CUtlHandleTable<T, HandleBits>::GetHandleFromIndex( int i ) const
{
	if ( m_list[i].m_pData )
		return CreateHandle( m_list[i].m_nSerial, i );
	return UTLHANDLE_INVALID;
}

template< class T, int HandleBits >
int CUtlHandleTable<T, HandleBits>::GetIndexFromHandle( UtlHandle_t h ) const
{
	if ( h == UTLHANDLE_INVALID )
		return -1;

	return GetListIndex( h );
}



//-----------------------------------------------------------------------------
// Cracking handles into indices + serial numbers
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
unsigned int CUtlHandleTable<T, HandleBits>::GetSerialNumber( UtlHandle_t handle )
{
	return ( ( HandleType_t* )&handle )->nSerial;
}

template< class T, int HandleBits >
unsigned int CUtlHandleTable<T, HandleBits>::GetListIndex( UtlHandle_t handle )
{
	return ( ( HandleType_t* )&handle )->nIndex;
}

template< class T, int HandleBits >
UtlHandle_t CUtlHandleTable<T, HandleBits>::CreateHandle( unsigned int nSerial, unsigned int nIndex )
{
	HandleType_t h( nIndex, nSerial );
	return *( UtlHandle_t* )&h;
}


//-----------------------------------------------------------------------------
// Looks up a entry by handle
//-----------------------------------------------------------------------------
template< class T, int HandleBits >
const typename CUtlHandleTable<T, HandleBits>::EntryType_t *CUtlHandleTable<T, HandleBits>::GetEntry( UtlHandle_t handle, bool checkValidity ) const
{
	if ( handle == UTLHANDLE_INVALID )
		return nullptr;

	unsigned int nIndex = GetListIndex( handle );
	Assert( nIndex < ( unsigned int )m_list.Count() );
	if ( nIndex >= ( unsigned int )m_list.size() )
		return nullptr;

	const EntryType_t &entry = m_list[ nIndex ];
	if ( entry.m_nSerial != GetSerialNumber( handle ) )
		return nullptr;

	if ( checkValidity &&
		( 1 == entry.nInvalid ) )
		return nullptr;

	return &entry;
}

template< class T, int HandleBits >
void CUtlHandleTable<T, HandleBits>::MarkHandleInvalid( UtlHandle_t handle )
{
	if ( handle == UTLHANDLE_INVALID )
		return;

	unsigned int nIndex = GetListIndex( handle );
	Assert( nIndex < ( unsigned int )m_list.Count() );
	if ( nIndex >= ( unsigned int )m_list.size() )
		return;

	EntryType_t &entry = m_list[ nIndex ];
	if ( entry.m_nSerial != GetSerialNumber( handle ) )
		return;

	if ( !entry.nInvalid )
	{
		--m_nValidHandles;
		entry.nInvalid = 1;
	}
}

template< class T, int HandleBits >
void CUtlHandleTable<T, HandleBits>::MarkHandleValid( UtlHandle_t handle )
{
	if ( handle == UTLHANDLE_INVALID )
		return;

	unsigned int nIndex = GetListIndex( handle );
	Assert( nIndex < ( unsigned int )m_list.Count() );
	if ( nIndex >= ( unsigned int )m_list.size() )
		return;

	EntryType_t &entry = m_list[ nIndex ];
	if ( entry.m_nSerial != GetSerialNumber( handle ) )
		return;

	if ( entry.nInvalid )
	{
		++m_nValidHandles;
		entry.nInvalid = 0;
	}
}









//-----------------------------------------------------------------------------
// Add this macro to a class definition to hook in handles for it! 
//-----------------------------------------------------------------------------
#define DECLARE_HANDLES( _className, _handleBitCount )						\
	public:																	\
		UtlHandle_t GetHandle()												\
		{																	\
			return m_Handle;												\
		}																	\
		static _className* GetPtrFromHandle( UtlHandle_t h )				\
		{																	\
			return m_HandleTable.GetHandle( h );							\
		}																	\
		static bool IsHandleValid( UtlHandle_t h )							\
		{																	\
			return m_HandleTable.IsHandleValid( h );						\
		}																	\
	private:																\
		UtlHandle_t m_Handle;												\
		static CUtlHandleTable< _className, _handleBitCount > m_HandleTable
																			

//-----------------------------------------------------------------------------
// Add this macro to a .cpp file  to hook in handles for it! 
//-----------------------------------------------------------------------------
#define IMPLEMENT_HANDLES( _className, _handleBitCount )					\
	CUtlHandleTable< _className, _handleBitCount > _className::m_HandleTable;


//-----------------------------------------------------------------------------
// Add these macro to the class constructor + destructor
//-----------------------------------------------------------------------------
#define CONSTRUCT_HANDLE( )						\
	m_Handle = m_HandleTable.AddHandle();		\
	m_HandleTable.SetHandle( m_Handle, this )

#define DESTRUCT_HANDLE()						\
	m_HandleTable.RemoveHandle( m_Handle );		\
	m_Handle = UTLHANDLE_INVALID





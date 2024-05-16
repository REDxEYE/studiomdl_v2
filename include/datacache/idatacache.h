//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//

#ifndef IDATACACHE_H
#define IDATACACHE_H

#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "appframework/iappsystem.h"
#include "tier3/tier3.h"

class IDataCache;

//-----------------------------------------------------------------------------
//
// Shared Data Cache API
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Support types and enums
//-----------------------------------------------------------------------------

//---------------------------------------------------------
// Unique (per section) identifier for a cache item defined by client
//---------------------------------------------------------
typedef uintp DataCacheClientID_t;


//---------------------------------------------------------
// Cache-defined handle for a cache item
//---------------------------------------------------------
FORWARD_DECLARE_HANDLE( memhandle_t );
typedef memhandle_t DataCacheHandle_t;
#define DC_INVALID_HANDLE ((DataCacheHandle_t)0)

//---------------------------------------------------------
// Cache Limits
//---------------------------------------------------------
struct DataCacheLimits_t
{
	DataCacheLimits_t( unsigned _nMaxBytes = (unsigned)-1, unsigned _nMaxItems = (unsigned)-1, unsigned _nMinBytes = 0, unsigned _nMinItems = 0 )
		: nMaxBytes(_nMaxBytes), 
		nMaxItems(_nMaxItems), 
		nMinBytes(_nMinBytes),
		nMinItems(_nMinItems)
	{
	}

	// Maximum levels permitted
	unsigned nMaxBytes;
	unsigned nMaxItems;

	// Minimum levels permitted
	unsigned nMinBytes;
	unsigned nMinItems;
};

//---------------------------------------------------------
// Cache status
//---------------------------------------------------------
struct DataCacheStatus_t
{
	// Current state of the cache
	unsigned nBytes;
	unsigned nItems;

	unsigned nBytesLocked;
	unsigned nItemsLocked;

	// Diagnostics
	unsigned nFindRequests;
	unsigned nFindHits;
};

//---------------------------------------------------------
// Cache options
//---------------------------------------------------------
enum DataCacheOptions_t
{
	DC_TRACE_ACTIVITY		= (1 << 0),
	DC_FORCE_RELOCATE		= (1 << 1),
	DC_ALWAYS_MISS			= (1 << 2),
	DC_VALIDATE				= (1 << 3),
	DC_NO_USER_FORCE_FLUSH	= (1 << 4)
};


//---------------------------------------------------------
// Cache report types
//---------------------------------------------------------
enum DataCacheReportType_t
{
	DC_SUMMARY_REPORT,
	DC_DETAIL_REPORT,
	DC_DETAIL_REPORT_LRU,
	DC_DETAIL_REPORT_VXCONSOLE,
};


//-----------------------------------------------------------------------------
// IDataCacheSection
//
// Purpose: Implements a sub-section of the global cache. Subsections are
//			areas of the cache with thier own memory constraints and common
//			management.
//-----------------------------------------------------------------------------
abstract_class IDataCacheSection
{
public:
    virtual ~IDataCacheSection() { };
	//--------------------------------------------------------

	virtual const char *GetName() = 0;

	//--------------------------------------------------------
	// Purpose: Add an item to the cache.  Purges old items if over budget, returns false if item was already in cache.
	//--------------------------------------------------------
	virtual void EnsureCapacity( unsigned nBytes, unsigned nItems = 1 ) = 0;


	//--------------------------------------------------------
	// Purpose: Add an item to the cache.  Purges old items if over budget, returns false if item was already in cache.
	//--------------------------------------------------------
	virtual bool Add( DataCacheClientID_t clientId, const void *pItemData, unsigned size, DataCacheHandle_t *pHandle ) = 0;

	//--------------------------------------------------------
	// Purpose: Finds an item in the cache, returns NULL if item is not in cache. Not a cheap operation if section not configured for fast find.
	//--------------------------------------------------------
	virtual DataCacheHandle_t Find( DataCacheClientID_t clientId ) = 0;


	//--------------------------------------------------------
	// Purpose: Lock an item in the cache, returns NULL if item is not in the cache.
	//--------------------------------------------------------
	virtual void *Lock( DataCacheHandle_t handle ) = 0;


	//--------------------------------------------------------
	// Purpose: Unlock a previous lock.
	//--------------------------------------------------------
	virtual int Unlock( DataCacheHandle_t handle ) = 0;


	//--------------------------------------------------------
	// Purpose: Get an item without locking it, returns NULL if item is not in the cache. Use with care!
	//--------------------------------------------------------
	virtual void *Get( DataCacheHandle_t handle, bool bFrameLock = false ) = 0;

	//--------------------------------------------------------
	// Purpose: "Frame locking" (not game frame). A crude way to manage locks over relatively 
	//			short periods. Does not affect normal locks/unlocks
	//--------------------------------------------------------
	virtual bool IsFrameLocking() = 0;



	//--------------------------------------------------------
	// Purpose: Empty the cache. Returns bytes released, will remove locked items if force specified
	//--------------------------------------------------------
	virtual unsigned Flush( bool bUnlockedOnly = true, bool bNotify = true ) = 0;


	//--------------------------------------------------------
	// Purpose: Dump the oldest items to free the specified amount of memory. Returns amount actually freed
	//--------------------------------------------------------
	virtual unsigned Purge( unsigned nBytes ) = 0;

	//--------------------------------------------------------
	// Purpose: Output the state of the section
	//--------------------------------------------------------
	virtual void OutputReport( DataCacheReportType_t reportType = DC_SUMMARY_REPORT ) = 0;


	//--------------------------------------------------------
	// Purpose: Access to the mutex. More explicit control during get-then-lock sequences
	// to ensure object stays valid during "then"
	//--------------------------------------------------------
	virtual void LockMutex() = 0;
	virtual void UnlockMutex() = 0;

};


//-----------------------------------------------------------------------------
// IDataCache
//
// Purpose: The global shared cache. Manages sections and overall budgets.
//
//-----------------------------------------------------------------------------
abstract_class IDataCache : public IAppSystem
{
public:
	//--------------------------------------------------------
	// Purpose: Controls cache size.
	//--------------------------------------------------------
	virtual void SetSize( int nMaxBytes ) = 0;



	//--------------------------------------------------------
	// Purpose: Remove a section from the cache
	//--------------------------------------------------------
	virtual void RemoveSection( const char *pszClientName, bool bCallFlush = true ) = 0;


	//--------------------------------------------------------
	// Purpose: Find a section of the cache
	//--------------------------------------------------------
	virtual IDataCacheSection *FindSection( const char *pszClientName ) = 0;

	//--------------------------------------------------------
	// Purpose: Dump the oldest items to free the specified amount of memory. Returns amount actually freed
	//--------------------------------------------------------
	virtual unsigned Purge( unsigned nBytes ) = 0;


	//--------------------------------------------------------
	// Purpose: Empty the cache. Returns bytes released, will remove locked items if force specified
	//--------------------------------------------------------
	virtual unsigned Flush( bool bUnlockedOnly = true, bool bNotify = true ) = 0;


	//--------------------------------------------------------
	// Purpose: Output the state of the cache
	//--------------------------------------------------------
	virtual void OutputReport( DataCacheReportType_t reportType = DC_SUMMARY_REPORT, const char *pszSection = NULL ) = 0;

};

#endif // IDataCache

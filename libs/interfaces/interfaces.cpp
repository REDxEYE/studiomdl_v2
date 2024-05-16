//===== Copyright � 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A higher level link library for general use in the game and tools.
//
//===========================================================================//

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "interfaces/interfaces.h"


//-----------------------------------------------------------------------------
// Tier 1 libraries
//-----------------------------------------------------------------------------
ICvar *cvar = 0;
ICvar *g_pCVar = 0;
IProcessUtils *g_pProcessUtils = 0;
IEventSystem *g_pEventSystem = 0;
ILocalize *g_pLocalize = 0;

// for utlsortvector.h
#ifndef _WIN32
void *g_pUtlSortVectorQSortContext = NULL;
#endif



//-----------------------------------------------------------------------------
// Tier 2 libraries
//-----------------------------------------------------------------------------
IFileSystem *g_pFullFileSystem = 0;
IMaterialSystem *materials = 0;
IMaterialSystem *g_pMaterialSystem = 0;
IVBAllocTracker *g_VBAllocTracker = 0;
IMdlLib *mdllib = 0;
IQueuedLoader *g_pQueuedLoader = 0;
IPrecacheSystem *g_pPrecacheSystem = 0;
ISceneSystem *g_pSceneSystem = 0;

//-----------------------------------------------------------------------------
// Tier3 libraries
//-----------------------------------------------------------------------------
IMeshSystem *g_pMeshSystem = 0;
IStudioRender *g_pStudioRender = 0;
IStudioRender *studiorender = 0;
IMatSystemSurface *g_pMatSystemSurface = 0;
IDataCache *g_pDataCache = 0;
IMDLCache *g_pMDLCache = 0;
IMDLCache *mdlcache = 0;


//-----------------------------------------------------------------------------
// Mapping of interface string to globals
//-----------------------------------------------------------------------------
struct InterfaceGlobals_t
{
	const char *m_pInterfaceName;
	void *m_ppGlobal;
};

static InterfaceGlobals_t g_pInterfaceGlobals[] =
{
	{ CVAR_INTERFACE_VERSION, &cvar },
	{ CVAR_INTERFACE_VERSION, &g_pCVar },
	{ EVENTSYSTEM_INTERFACE_VERSION, &g_pEventSystem },
	{ PROCESS_UTILS_INTERFACE_VERSION, &g_pProcessUtils },
	{ FILESYSTEM_INTERFACE_VERSION, &g_pFullFileSystem },
	{ MATERIAL_SYSTEM_INTERFACE_VERSION, &materials },
	{ VB_ALLOC_TRACKER_INTERFACE_VERSION, &g_VBAllocTracker },
	{ MDLLIB_INTERFACE_VERSION, &mdllib },
	{ QUEUEDLOADER_INTERFACE_VERSION, &g_pQueuedLoader },
	{ LOCALIZE_INTERFACE_VERSION, &g_pLocalize },
	{ MAT_SYSTEM_SURFACE_INTERFACE_VERSION, &g_pMatSystemSurface },
	{ DATACACHE_INTERFACE_VERSION, &g_pDataCache },
	{ MDLCACHE_INTERFACE_VERSION, &g_pMDLCache },
	{ MDLCACHE_INTERFACE_VERSION, &mdlcache },
	{ MESHSYSTEM_INTERFACE_VERSION, &g_pMeshSystem },
	{ SCENESYSTEM_INTERFACE_VERSION, &g_pSceneSystem },
};


//-----------------------------------------------------------------------------
// The # of times this DLL has been connected
//-----------------------------------------------------------------------------
static int s_nConnectionCount = 0;


//-----------------------------------------------------------------------------
// At each level of connection, we're going to keep track of which interfaces
// we filled in. When we disconnect, we'll clear those interface pointers out.
//-----------------------------------------------------------------------------
struct ConnectionRegistration_t
{
	void *m_ppGlobalStorage;
	int m_nConnectionPhase;
};

static int s_nRegistrationCount = 0;
static ConnectionRegistration_t s_pConnectionRegistration[ ARRAYSIZE(g_pInterfaceGlobals) + 1 ];

void RegisterInterface( CreateInterfaceFn factory, const char *pInterfaceName, void **ppGlobal )
{
	if ( !(*ppGlobal) )
	{
		*ppGlobal = factory( pInterfaceName, NULL );
		if ( *ppGlobal )
		{
			Assert( s_nRegistrationCount < ARRAYSIZE(s_pConnectionRegistration) );
			ConnectionRegistration_t &reg = s_pConnectionRegistration[s_nRegistrationCount++];
			reg.m_ppGlobalStorage = ppGlobal;
			reg.m_nConnectionPhase = s_nConnectionCount;
		}
	}
}

void ReconnectInterface( CreateInterfaceFn factory, const char *pInterfaceName, void **ppGlobal )
{
	*ppGlobal = factory( pInterfaceName, NULL );

	bool bFound = false;
	Assert( s_nRegistrationCount < ARRAYSIZE(s_pConnectionRegistration) );
	for ( int i = 0; i < s_nRegistrationCount; ++i )
	{
		ConnectionRegistration_t &reg = s_pConnectionRegistration[i];
		if ( reg.m_ppGlobalStorage != ppGlobal )
			continue;

		reg.m_ppGlobalStorage = ppGlobal;
		bFound = true;
	}

	if ( !bFound && *ppGlobal )
	{
		Assert( s_nRegistrationCount < ARRAYSIZE(s_pConnectionRegistration) );
		ConnectionRegistration_t &reg = s_pConnectionRegistration[s_nRegistrationCount++];
		reg.m_ppGlobalStorage = ppGlobal;
		reg.m_nConnectionPhase = s_nConnectionCount;
	}
}


//-----------------------------------------------------------------------------
// Call this to connect to all tier 1 libraries.
// It's up to the caller to check the globals it cares about to see if ones are missing
//-----------------------------------------------------------------------------
void ConnectInterfaces( CreateInterfaceFn *pFactoryList, int nFactoryCount )
{
	if ( s_nRegistrationCount < 0 )
	{
		Error( "APPSYSTEM: In ConnectInterfaces(), s_nRegistrationCount is %d!\n", s_nRegistrationCount );
	}
	else if ( s_nRegistrationCount == 0 )
	{
		for ( int i = 0; i < nFactoryCount; ++i )
		{
			for ( int j = 0; j < ARRAYSIZE( g_pInterfaceGlobals ); ++j )
			{
				RegisterInterface( pFactoryList[i], g_pInterfaceGlobals[j].m_pInterfaceName, (void**)g_pInterfaceGlobals[j].m_ppGlobal );
			}
		}
	}
	else
	{
		// This is no longer questionable: ConnectInterfaces() is expected to be called multiple times for a file that exports multiple interfaces.
		// Warning("APPSYSTEM: ConnectInterfaces() was called twice for the same DLL.\nThis is expected behavior in building reslists, but questionable otherwise.\n");
		for ( int i = 0; i < nFactoryCount; ++i )
		{
			for ( int j = 0; j < ARRAYSIZE( g_pInterfaceGlobals ); ++j )
			{
				ReconnectInterface( pFactoryList[i], g_pInterfaceGlobals[j].m_pInterfaceName, (void**)g_pInterfaceGlobals[j].m_ppGlobal );
			}
		}
	}
	++s_nConnectionCount;
}

void DisconnectInterfaces()
{
	Assert( s_nConnectionCount > 0 );
	if ( --s_nConnectionCount < 0 )
		return;

	for ( int i = 0; i < s_nRegistrationCount; ++i )
	{
		if ( s_pConnectionRegistration[i].m_nConnectionPhase != s_nConnectionCount )
			continue;

		// Disconnect!
		*(void**)(s_pConnectionRegistration[i].m_ppGlobalStorage) = 0;
	}
}


//-----------------------------------------------------------------------------
// Reloads an interface
//-----------------------------------------------------------------------------
void ReconnectInterface( CreateInterfaceFn factory, const char *pInterfaceName )
{
	for ( int i = 0; i < ARRAYSIZE( g_pInterfaceGlobals ); ++i )
	{
		if ( strcmp( g_pInterfaceGlobals[i].m_pInterfaceName, pInterfaceName ) )
			continue;		
		ReconnectInterface( factory, g_pInterfaceGlobals[i].m_pInterfaceName, (void**)g_pInterfaceGlobals[i].m_ppGlobal );
	}
}

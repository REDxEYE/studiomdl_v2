//===== Copyright � 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A higher level link library for general use in the game and tools.
//
//===========================================================================//


#pragma once

#if defined( COMPILER_MSVC )
#pragma once
#endif


//-----------------------------------------------------------------------------
// Interface creation function
//-----------------------------------------------------------------------------
typedef void* (*CreateInterfaceFn)(const char *pName, int *pReturnCode);


//-----------------------------------------------------------------------------
// Macros to declare interfaces appropriate for various tiers
//-----------------------------------------------------------------------------
#if 1 || defined( TIER1_LIBRARY ) || defined( TIER2_LIBRARY ) || defined( TIER3_LIBRARY ) || defined( TIER4_LIBRARY ) || defined( APPLICATION )
#define DECLARE_TIER1_INTERFACE( _Interface, _Global )	extern _Interface * _Global;
#else
#define DECLARE_TIER1_INTERFACE( _Interface, _Global )
#endif

#if 1 || defined( TIER2_LIBRARY ) || defined( TIER3_LIBRARY ) || defined( TIER4_LIBRARY ) || defined( APPLICATION )
#define DECLARE_TIER2_INTERFACE( _Interface, _Global )	extern _Interface * _Global;
#else
#define DECLARE_TIER2_INTERFACE( _Interface, _Global )
#endif

#if 1 || defined( TIER3_LIBRARY ) || defined( TIER4_LIBRARY ) || defined( APPLICATION )
#define DECLARE_TIER3_INTERFACE( _Interface, _Global )	extern _Interface * _Global;
#else
#define DECLARE_TIER3_INTERFACE( _Interface, _Global )
#endif


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class ICvar;
class ILocalize;

class IFileSystem;
class IMaterialSystem;
class IMdlLib;
class IDataCache;
class IMDLCache;
class IMeshSystem;



//-----------------------------------------------------------------------------
// Fills out global DLL exported interface pointers
//-----------------------------------------------------------------------------
#define CVAR_INTERFACE_VERSION					"VEngineCvar007"
DECLARE_TIER1_INTERFACE( ICvar, cvar );
DECLARE_TIER1_INTERFACE( ICvar, g_pCVar )

#define FILESYSTEM_INTERFACE_VERSION			"VFileSystem017"
DECLARE_TIER2_INTERFACE( IFileSystem, g_pFullFileSystem );

#define MATERIAL_SYSTEM_INTERFACE_VERSION		"VMaterialSystem080"
DECLARE_TIER2_INTERFACE( IMaterialSystem, materials );
DECLARE_TIER2_INTERFACE( IMaterialSystem, g_pMaterialSystem );

#define MDLLIB_INTERFACE_VERSION				"VMDLLIB001"
DECLARE_TIER2_INTERFACE( IMdlLib, mdllib );

#define MESHSYSTEM_INTERFACE_VERSION			"MeshSystem001"
DECLARE_TIER3_INTERFACE( IMeshSystem, g_pMeshSystem );

#define MDLCACHE_INTERFACE_VERSION				"MDLCache004"
DECLARE_TIER3_INTERFACE( IMDLCache, g_pMDLCache );
DECLARE_TIER3_INTERFACE( IMDLCache, mdlcache );

//-----------------------------------------------------------------------------
// Fills out global DLL exported interface pointers
//-----------------------------------------------------------------------------
void ConnectInterfaces( CreateInterfaceFn *pFactoryList, int nFactoryCount );
void DisconnectInterfaces();


//-----------------------------------------------------------------------------
// Reconnects an interface
//-----------------------------------------------------------------------------
void ReconnectInterface( CreateInterfaceFn factory, const char *pInterfaceName );




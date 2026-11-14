//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#if defined( _WIN32 )
#elif POSIX
#include <unistd.h>
#endif

#include "filesystem_tools.h"

#include "filesystem_init.h"
#include "interfaces/interfaces.h"




// ---------------------------------------------------------------------------------------------------- //
// Module interface.
// ---------------------------------------------------------------------------------------------------- //

IBaseFileSystem *g_pFileSystem = nullptr;


bool FileSystem_SetGame( const char *szModDir )
{
	g_pFullFileSystem->RemoveAllSearchPaths();
	if ( FileSystem_SetBasePaths( g_pFullFileSystem ) != FS_OK )
		return false;

	CFSSearchPathsInit fsInit;
	fsInit.m_pDirectoryName = szModDir;
	fsInit.m_pFileSystem = g_pFullFileSystem;
	return ( FileSystem_LoadSearchPaths( fsInit ) == FS_OK );
}


//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#if defined( _WIN32 )
#include <windows.h>
#include <direct.h>
#include <io.h> // _chmod
#elif POSIX
#include <unistd.h>
#endif

#include <stdio.h>
#include <sys/stat.h>
#include "tier1/strtools.h"
#include "filesystem_tools.h"
#include "tier0/icommandline.h"
#include "tier1/keyvalues.h"
#include "tier2/tier2.h"

#ifdef MPI
	#include "vmpi.h"
	#include "vmpi_tools_shared.h"
	#include "vmpi_filesystem.h"
#endif




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


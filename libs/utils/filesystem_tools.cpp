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

// ---------------------------------------------------------------------------
//
// These are the base paths that everything will be referenced relative to (textures especially)
// All of these directories include the trailing slash
//
// ---------------------------------------------------------------------------

// This is the the path of the initial source file (relative to the cwd)
char		qdir[1024]{};

// This is the base engine + mod-specific game dir (e.g. "c:\tf2\mytfmod\")
char		gamedir[1024]{};

void FileSystem_SetupStandardDirectories( const char *pFilename, const char *pGameInfoPath )
{
	// Set qdir.
	if ( !pFilename )
	{
		pFilename = ".";
	}

	Q_MakeAbsolutePath( qdir, sizeof( qdir ), pFilename, nullptr);
	Q_StripFilename( qdir );
#if !(PLATFORM_LINUX)
	Q_strlower( qdir );
#endif
	if ( qdir[0] != 0 )
	{
		Q_AppendSlash( qdir, sizeof( qdir ) );
	}

	// Set gamedir.
	Q_MakeAbsolutePath( gamedir, sizeof( gamedir ), pGameInfoPath );
	Q_AppendSlash( gamedir, sizeof( gamedir ) );
}


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


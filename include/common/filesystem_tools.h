//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#pragma once
#ifdef _WIN32
#pragma once
#endif


#include "filesystem.h"
#include "filesystem_init.h"


// This is the the path of the initial source file
extern char		qdir[1024];

// This is the base engine + mod-specific game dir (e.g. "d:\tf2\mytfmod\")
extern char		gamedir[1024];	


// ---------------------------------------------------------------------------------------- //
// Filesystem initialization.
// ---------------------------------------------------------------------------------------- //

bool				FileSystem_SetGame( const char *szModDir );

// Sets qdir and gamedir from the source file and the gameinfo.txt directory
void				FileSystem_SetupStandardDirectories( const char *pFilename, const char *pGameInfoPath );


extern IBaseFileSystem	*g_pFileSystem;



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


// ---------------------------------------------------------------------------------------- //
// Filesystem initialization.
// ---------------------------------------------------------------------------------------- //

bool				FileSystem_SetGame( const char *szModDir );


extern IBaseFileSystem	*g_pFileSystem;



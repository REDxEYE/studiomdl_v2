//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#pragma once



#include "filesystem.h"


// ---------------------------------------------------------------------------------------- //
// Filesystem initialization.
// ---------------------------------------------------------------------------------------- //

bool				FileSystem_SetGame( const char *szModDir );


extern IBaseFileSystem	*g_pFileSystem;



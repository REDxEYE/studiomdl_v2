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



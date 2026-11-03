//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//===========================================================================//


#if defined( PLATFORM_WINDOWS_PC )
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0403
#include <windows.h>
#endif



#include "platform.h"

// First include standard libraries
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

#include <stddef.h>
#ifdef PLATFORM_POSIX
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#define _MAX_PATH PATH_MAX
#endif


#include "tier0/basetypes.h"


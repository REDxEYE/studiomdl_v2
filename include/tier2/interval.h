//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "tier0/basetypes.h"


interval_t ReadInterval( const char *pString );
float RandomInterval( const interval_t &interval );


//========= Copyright � Valve Corporation, All rights reserved. ============//
#include "pch_tier0.h"
#include "dbg.h"

// Case-insensitive ASCII compare; unrolled twice per loop iteration.

#define TOLOWERC( x )  (( ( x >= 'A' ) && ( x <= 'Z' ) )?( x + 32 ) : x )

#if !defined( STATIC_LINK )
#define FDECL extern "C"
#else
#define FDECL
#endif

FDECL int V_tier0_stricmp(const char *s1, const char *s2 )
{
	// A string is always equal to itself. This optimization is
	// surprisingly valuable.
	if ( s1 == s2 )
		return 0;

	uint8_t const *pS1 = ( uint8_t const * ) s1;
	uint8_t const *pS2 = ( uint8_t const * ) s2;
	for(;;)
	{
		int c1 = *( pS1++ );
		int c2 = *( pS2++ );
		if ( c1 == c2 )
		{
			if ( !c1 ) return 0;
		}
		else
		{
			if ( ! c2 )
			{
				return c1 - c2;
			}
			c1 = TOLOWERC( c1 );
			c2 = TOLOWERC( c2 );
			if ( c1 != c2 )
			{
				return c1 - c2;
			}
		}
		c1 = *( pS1++ );
		c2 = *( pS2++ );
		if ( c1 == c2 )
		{
			if ( !c1 ) return 0;
		}
		else
		{
			if ( ! c2 )
			{
				return c1 - c2;
			}
			c1 = TOLOWERC( c1 );
			c2 = TOLOWERC( c2 );
			if ( c1 != c2 )
			{
				return c1 - c2;
			}
		}
	}
}

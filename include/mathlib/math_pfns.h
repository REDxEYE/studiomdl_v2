//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#pragma once

enum MatrixAxisType_t
{
	FORWARD_AXIS = 0,
	LEFT_AXIS = 1,
	UP_AXIS = 2,
	X_AXIS = 0,
	Y_AXIS = 1,
	Z_AXIS = 2,
	ORIGIN = 3,
	PROJECTIVE = 3,
};


#include <cfloat>
#include <cmath>

// The following are not declared as macros because they are often used in limiting situations,
// and sometimes the compiler simply refuses to inline them for some reason
// (IEEE sqrt is exact, so this equals the former sqrtss intrinsic)
FORCEINLINE float VECTORCALL FastSqrt( float x )
{
	return std::sqrt( x );
}
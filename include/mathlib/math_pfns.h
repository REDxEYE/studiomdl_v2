//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#pragma once

// YUP_ACTIVE is from Source2. It's (obviously) not supported on this branch, just including it here to help merge camera.cpp/.h and the CSM shadow code.
//#define YUP_ACTIVE 1

enum MatrixAxisType_t
{
#ifdef YUP_ACTIVE
	FORWARD_AXIS = 2,
	LEFT_AXIS = 0,
	UP_AXIS = 1,
#else
	FORWARD_AXIS = 0,
	LEFT_AXIS = 1,
	UP_AXIS = 2,
#endif

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

void FastSinCos( float x, float* s, float* c );  // any x
float FastCos( float x );



inline float FastRecip(float x) {return 1.0f / x;}

// if x is infinite, return FLT_MAX
inline float FastClampInfinity( float x )
{
	return ( x > FLT_MAX ? FLT_MAX : x );
}


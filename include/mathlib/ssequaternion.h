//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: - SIMD load/store helpers for aligned quaternions.
//
//===========================================================================//
#pragma once

#include "mathlib/ssemath.h"

//---------------------------------------------------------------------
// Load/store quaternions
//---------------------------------------------------------------------
FORCEINLINE fltx4 LoadAlignedSIMD( const QuaternionAligned & pSIMD )
{
	fltx4 retval = LoadAlignedSIMD( pSIMD.Base() );
	return retval;
}

FORCEINLINE fltx4 LoadAlignedSIMD( const QuaternionAligned * RESTRICT pSIMD )
{
	fltx4 retval = LoadAlignedSIMD( pSIMD->Base() );
	return retval;
}

FORCEINLINE void StoreAlignedSIMD( QuaternionAligned * RESTRICT pSIMD, const fltx4 & a )
{
	StoreAlignedSIMD( pSIMD->Base(), a );
}

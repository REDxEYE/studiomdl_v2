//===== Copyright 1996-2010, Valve Corporation, All rights reserved. ======//
//
// Purpose: - defines the type fltx4 - Avoid cyclic includion.
//
//===========================================================================//

#pragma once

#include <cstdint>

// The SIMD helpers (ssemath.h) are plain scalar code over the four lanes, so the same
// results come out on every target. The packed integer and float views of a register are
// separate union members because the nature of SIMD code which includes comparisons is to
// blur the relationship between packed floats and packed integer types.
//
// may_alias: matrix rows, constant masks etc. are reinterpreted as fltx4 through pointers.
#if defined( __GNUC__ ) || defined( __clang__ )
#define FLTX4_MAY_ALIAS __attribute__(( __may_alias__ ))
#else
#define FLTX4_MAY_ALIAS
#endif

union alignas( 16 ) FLTX4_MAY_ALIAS fltx4
{
	float    m128_f32[4];
	uint32_t m128_u32[4];
	int32_t  m128_i32[4];
};

typedef fltx4 i32x4;
typedef fltx4 u32x4;
typedef fltx4 bi32x4;

union alignas( 16 ) FLTX4_MAY_ALIAS shortx8
{
	int16_t m128i_i16[8];
	int32_t m128i_i32[4];
};

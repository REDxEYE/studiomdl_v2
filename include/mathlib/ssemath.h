//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: - defines SIMD "structure of arrays" classes and functions.
//
//===========================================================================//
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "mathlib/vector.h"
#include "mathlib/mathlib.h"

#include "mathlib/fltx4.h"

// The FLTX4 type is a fltx4 used as a parameter to a function (passed by const reference).
typedef const fltx4 & FLTX4;

// A 16-byte aligned int32 datastructure
// (for use when writing out fltx4's as SIGNED
// ints).
struct ALIGN16 intx4
{
	int32_t m_i32[4];

	inline int & operator[](int which) 
	{
		return m_i32[which];
	}

	inline const int & operator[](int which) const
	{
		return m_i32[which];
	}

	inline int32_t *Base() {
		return m_i32;
	}

	inline const int32_t *Base() const
	{
		return m_i32;
	}

	inline bool operator==(const intx4 &other) const
	{
		return m_i32[0] == other.m_i32[0] &&
			m_i32[1] == other.m_i32[1] &&
			m_i32[2] == other.m_i32[2] &&
			m_i32[3] == other.m_i32[3] 	;
	}
} ALIGN16_POST;


FORCEINLINE void TestVPUFlags() {}


// useful constants in SIMD packed float format:
// (note: some of these aren't stored on the 360, 
// but are manufactured directly in one or two 
// instructions, saving a load and possible L2
// miss.)

extern const fltx4 Four_Zeros;									// 0 0 0 0
extern const fltx4 Four_Ones;									// 1 1 1 1
extern const fltx4 Four_Twos;									// 2 2 2 2
extern const fltx4 Four_Threes;									// 3 3 3 3
extern const fltx4 Four_Fours;									// guess.
extern const fltx4 Four_Point225s;								// .225 .225 .225 .225
extern const fltx4 Four_PointFives;								// .5 .5 .5 .5
extern const fltx4 Four_Thirds;									// 1/3
extern const fltx4 Four_TwoThirds;								// 2/3
extern const fltx4 Four_NegativeOnes;							// -1 -1 -1 -1 
extern const fltx4 Four_DegToRad;								// (float)(M_PI_F / 180.f) times four
extern const fltx4 Four_Epsilons;								// FLT_EPSILON FLT_EPSILON FLT_EPSILON FLT_EPSILON
extern const fltx4 Four_2ToThe21s;								// (1<<21)..
extern const fltx4 Four_2ToThe22s;								// (1<<22)..
extern const fltx4 Four_2ToThe23s;								// (1<<23)..
extern const fltx4 Four_2ToThe24s;								// (1<<24)..
extern const fltx4 Four_Origin;									// 0 0 0 1 (origin point, like vr0 on the PS2)
extern const fltx4 Four_FLT_MAX;								// FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX
extern const fltx4 Four_Negative_FLT_MAX;						// -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX
extern const fltx4 g_SIMD_0123;									// 0 1 2 3 as float


// coefficients for polynomial approximation of srgb conversions

// 4th order polynomial for x^(1/2.2), x in 0..1
extern const fltx4 Four_LinearToGammaCoefficients_A;		// *x^4
extern const fltx4 Four_LinearToGammaCoefficients_B;		// *x^3
extern const fltx4 Four_LinearToGammaCoefficients_C;		// *x^2
extern const fltx4 Four_LinearToGammaCoefficients_D;		// *x^1
extern const fltx4 Four_LinearToGammaCoefficients_E;		// *x^0

// 3rd order polynomial for x^2.2 x in 0..1
extern const fltx4 Four_GammaToLinearCoefficients_A;		// *x^3
extern const fltx4 Four_GammaToLinearCoefficients_B;		// *x^2
extern const fltx4 Four_GammaToLinearCoefficients_C;		// *x^1
extern const fltx4 Four_GammaToLinearCoefficients_D;		// *x^0


// external aligned integer constants
#ifndef ALIGN16_POST
#define ALIGN16_POST
#endif
extern const ALIGN16 uint32_t g_SIMD_clear_signmask[] ALIGN16_POST;			// 0x7fffffff x 4
extern const ALIGN16 uint32_t g_SIMD_signmask[] ALIGN16_POST;				// 0x80000000 x 4
extern const ALIGN16 uint32_t g_SIMD_lsbmask[] ALIGN16_POST;				// 0xfffffffe x 4
extern const ALIGN16 uint32_t g_SIMD_clear_wmask[] ALIGN16_POST;			// -1 -1 -1 0
extern const ALIGN16 uint32_t g_SIMD_ComponentMask[4][4] ALIGN16_POST;		// [0xFFFFFFFF 0 0 0], [0 0xFFFFFFFF 0 0], [0 0 0xFFFFFFFF 0], [0 0 0 0xFFFFFFFF]
extern const ALIGN16 uint32_t g_SIMD_AllOnesMask[] ALIGN16_POST;			// ~0,~0,~0,~0
extern const fltx4 g_SIMD_Identity[4];									// [1 0 0 0], [0 1 0 0], [0 0 1 0], [0 0 0 1]
extern const ALIGN16 int32_t g_SIMD_Low16BitsMask[] ALIGN16_POST;			// 0xffff x 4

// this mask is used for skipping the tail of things. If you have N elements in an array, and wish
// to mask out the tail, g_SIMD_SkipTailMask[N & 3] what you want to use for the last iteration.
extern const uint32_t ALIGN16 g_SIMD_SkipTailMask[4][4] ALIGN16_POST;



// Here's a handy function to align a pointer to the next
// sixteen byte boundary -- it'll round it up to the nearest
// multiple of 16. This is useful if you're subdividing
// big swaths of allocated memory, but in that case, remember
// to leave yourself the necessary slack in the allocation.
template<class T>
inline T *AlignPointer(void * ptr)
{
	uintptr_t temp = (uintptr_t)ptr;
	temp = ALIGN_VALUE(temp, sizeof(T));
	return (T *)temp;
}


FORCEINLINE void StoreAlignedSIMD( float * RESTRICT pSIMD, const fltx4 & a )
{
	memcpy( pSIMD, &a, sizeof( a ) );
}

FORCEINLINE void StoreAlignedSIMD( short * RESTRICT pSIMD, const shortx8 & a )
{
	memcpy( pSIMD, &a, sizeof( a ) );
}
FORCEINLINE void StoreUnalignedSIMD( float * RESTRICT pSIMD, const fltx4 & a )
{
	memcpy( pSIMD, &a, sizeof( a ) );
}

FORCEINLINE void StoreUnalignedSIMD(short* RESTRICT pSIMD, const shortx8& a)
{
	memcpy( pSIMD, &a, sizeof( a ) );
}

FORCEINLINE void StoreUnalignedFloat( float *pSingleFloat, const fltx4 & a )
{
	memcpy( pSingleFloat, &a, sizeof( float ) );
}


FORCEINLINE void StoreUnaligned3SIMD( float *pSIMD, const fltx4 & a )
{
	memcpy( pSIMD, &a, 3 * sizeof( float ) );
}


// strongly typed -- syntactic castor oil used for typechecking as we transition to SIMD
FORCEINLINE void StoreAligned3SIMD( VectorAligned * RESTRICT pSIMD, const fltx4 & a )
{
	StoreAlignedSIMD( pSIMD->Base(),a );
}

// Store the x,y,z components of the four FLTX4 parameters
// into the four consecutive Vectors:
//    pDestination[0],  pDestination[1],  pDestination[2],  pDestination[3]
// The Vectors are assumed to be unaligned.
FORCEINLINE void StoreFourUnalignedVector3SIMD( fltx4 a, fltx4 b, fltx4	c, FLTX4 d, // first three passed by copy (deliberate)
											   Vector * const pDestination )
{
	StoreUnaligned3SIMD( pDestination->Base(), a );
	StoreUnaligned3SIMD( (pDestination+1)->Base(), b );
	StoreUnaligned3SIMD( (pDestination+2)->Base(), c );
	StoreUnaligned3SIMD( (pDestination+3)->Base(), d );
}

// Store the x,y,z components of the four FLTX4 parameters
// into the four consecutive Vectors:
//    pDestination ,  pDestination + 1,  pDestination + 2,  pDestination + 3
// The Vectors are assumed to start on an ALIGNED address, that is, 
// pDestination is 16-byte aligned (thhough obviously pDestination+1 is not).
FORCEINLINE void StoreFourAlignedVector3SIMD( fltx4 a, fltx4 b, fltx4	c, FLTX4 d, // first three passed by copy (deliberate)
											 Vector * const pDestination )
{
	StoreUnaligned3SIMD( pDestination->Base(), a );
	StoreUnaligned3SIMD( (pDestination+1)->Base(), b );
	StoreUnaligned3SIMD( (pDestination+2)->Base(), c );
	StoreUnaligned3SIMD( (pDestination+3)->Base(), d );
}

FORCEINLINE fltx4 LoadAlignedSIMD( const void *pSIMD )
{
	fltx4 result;
	memcpy( &result, pSIMD, sizeof( result ) );
	return result;
}

FORCEINLINE shortx8 LoadAlignedShortSIMD( const void *pSIMD )
{
	shortx8 result;
	memcpy( &result, pSIMD, sizeof( result ) );
	return result;
}

FORCEINLINE shortx8 LoadUnalignedShortSIMD( const void *pSIMD )
{
	shortx8 result;
	memcpy( &result, pSIMD, sizeof( result ) );
	return result;
}

FORCEINLINE fltx4 AndSIMD( const fltx4 & a, const fltx4 & b )				// a & b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = a.m128_u32[i] & b.m128_u32[i];
	return result;
}

FORCEINLINE fltx4 AndNotSIMD( const fltx4 & a, const fltx4 & b )			// ~a & b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ~a.m128_u32[i] & b.m128_u32[i];
	return result;
}

FORCEINLINE fltx4 XorSIMD( const fltx4 & a, const fltx4 & b )				// a ^ b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = a.m128_u32[i] ^ b.m128_u32[i];
	return result;
}

FORCEINLINE fltx4 OrSIMD( const fltx4 & a, const fltx4 & b )				// a | b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = a.m128_u32[i] | b.m128_u32[i];
	return result;
}

// Squelch the w component of a vector to +0.0.
// Most efficient when you say a = SetWToZeroSIMD(a) (avoids a copy)
FORCEINLINE fltx4 SetWToZeroSIMD( const fltx4 & a )
{
	return AndSIMD( a, LoadAlignedSIMD( g_SIMD_clear_wmask ) );
}

// for the transitional class -- load a 3-by VectorAligned and squash its w component
FORCEINLINE fltx4 LoadAlignedSIMD( const VectorAligned & pSIMD )
{
	return SetWToZeroSIMD( LoadAlignedSIMD(pSIMD.Base()) );
}

FORCEINLINE fltx4 LoadUnalignedSIMD( const void *pSIMD )
{
	return LoadAlignedSIMD( pSIMD );
}

FORCEINLINE fltx4 LoadUnaligned3SIMD( const void *pSIMD )
{
	return LoadAlignedSIMD( pSIMD );
}

// load a single unaligned float into the x component of a SIMD word (y, z, w are zeroed)
FORCEINLINE fltx4 LoadUnalignedFloatSIMD( const float *pFlt )
{
	fltx4 result;
	memcpy( &result.m128_u32[0], pFlt, sizeof( float ) );
	result.m128_u32[1] = result.m128_u32[2] = result.m128_u32[3] = 0;
	return result;
}

/// replicate a single 32 bit integer value to all 4 components of an m128
FORCEINLINE fltx4 ReplicateIX4( int i )
{
	fltx4 result;
	for ( int lane = 0; lane < 4; lane++ )
		result.m128_u32[lane] = (uint32_t)i;
	return result;
}


FORCEINLINE fltx4 ReplicateX4( float flValue )
{
	fltx4 result;
	for ( int lane = 0; lane < 4; lane++ )
		result.m128_f32[lane] = flValue;
	return result;
}

FORCEINLINE fltx4 ReplicateX4( const float * flValue )
{
	return ReplicateX4( *flValue );
}

FORCEINLINE float SubFloat( const fltx4 & a, int idx )
{
	return a.m128_f32[ idx ];
}

FORCEINLINE float & SubFloat( fltx4 & a, int idx )
{
	return a.m128_f32[ idx ];
}

FORCEINLINE uint32_t SubFloatConvertToInt( const fltx4 & a, int idx )
{
	return (uint32_t)SubFloat(a,idx);
}

FORCEINLINE uint32_t SubInt( const fltx4 & a, int idx )
{
	return a.m128_u32[idx];
}

FORCEINLINE uint32_t & SubInt( fltx4 & a, int idx )
{
	return a.m128_u32[idx];
}

// Return one in the fastest way -- on the x360, faster even than loading.
FORCEINLINE fltx4 LoadZeroSIMD( void )
{
	return Four_Zeros;
}

// Return one in the fastest way -- on the x360, faster even than loading.
FORCEINLINE fltx4 LoadOneSIMD( void )
{
	return Four_Ones;
}

FORCEINLINE fltx4 MaskedAssign( const fltx4 & ReplacementMask, const fltx4 & NewValue, const fltx4 & OldValue )
{
	return OrSIMD(
		AndSIMD( ReplacementMask, NewValue ),
		AndNotSIMD( ReplacementMask, OldValue ) );
}

// result = { a[i0], a[i1], a[i2], a[i3] }, bits copied unchanged
FORCEINLINE fltx4 ShuffleLanesSIMD( const fltx4 & a, int i0, int i1, int i2, int i3 )
{
	fltx4 result;
	result.m128_u32[0] = a.m128_u32[i0];
	result.m128_u32[1] = a.m128_u32[i1];
	result.m128_u32[2] = a.m128_u32[i2];
	result.m128_u32[3] = a.m128_u32[i3];
	return result;
}

FORCEINLINE fltx4 SplatXSIMD( fltx4 const & a )
{
	return ShuffleLanesSIMD( a, 0, 0, 0, 0 );
}

FORCEINLINE fltx4 SplatYSIMD( fltx4 const &a )
{
	return ShuffleLanesSIMD( a, 1, 1, 1, 1 );
}

FORCEINLINE fltx4 SplatZSIMD( fltx4 const &a )
{
	return ShuffleLanesSIMD( a, 2, 2, 2, 2 );
}

FORCEINLINE fltx4 SplatWSIMD( fltx4 const &a )
{
	return ShuffleLanesSIMD( a, 3, 3, 3, 3 );
}

FORCEINLINE fltx4 ShuffleXXYY( const fltx4 &a )
{
	return ShuffleLanesSIMD( a, 0, 0, 1, 1 );
}

FORCEINLINE fltx4 ShuffleXYXY( const fltx4 &a )
{
	return ShuffleLanesSIMD( a, 0, 1, 0, 1 );
}

FORCEINLINE fltx4 ShuffleZZWW( const fltx4 &a )
{
	return ShuffleLanesSIMD( a, 2, 2, 3, 3 );
}




FORCEINLINE fltx4 SetXSIMD( const fltx4& a, const fltx4& x )
{
	fltx4 result = MaskedAssign( LoadAlignedSIMD( g_SIMD_ComponentMask[0] ), x, a );
	return result;
}

FORCEINLINE fltx4 SetYSIMD( const fltx4& a, const fltx4& y )
{
	fltx4 result = MaskedAssign( LoadAlignedSIMD( g_SIMD_ComponentMask[1] ), y, a );
	return result;
}

FORCEINLINE fltx4 SetZSIMD( const fltx4& a, const fltx4& z )
{
	fltx4 result = MaskedAssign( LoadAlignedSIMD( g_SIMD_ComponentMask[2] ), z, a );
	return result;
}

FORCEINLINE fltx4 SetWSIMD( const fltx4& a, const fltx4& w )
{
	fltx4 result = MaskedAssign( LoadAlignedSIMD( g_SIMD_ComponentMask[3] ), w, a );
	return result;
}

FORCEINLINE fltx4 SetComponentSIMD( const fltx4& a, int nComponent, float flValue )
{
	fltx4 val = ReplicateX4( flValue );
	fltx4 result = MaskedAssign( LoadAlignedSIMD( g_SIMD_ComponentMask[nComponent] ), val, a );
	return result;
}

// a b c d -> b c d a
FORCEINLINE fltx4 RotateLeft( const fltx4 & a )
{
	return ShuffleLanesSIMD( a, 1, 2, 3, 0 );
}

// a b c d -> c d a b
FORCEINLINE fltx4 RotateLeft2( const fltx4 & a )
{
	return ShuffleLanesSIMD( a, 2, 3, 0, 1 );
}

// a b c d -> d a b c
FORCEINLINE fltx4 RotateRight( const fltx4 & a )
{
	return ShuffleLanesSIMD( a, 3, 0, 1, 2 );
}

// a b c d -> c d a b
FORCEINLINE fltx4 RotateRight2( const fltx4 & a )
{
	return ShuffleLanesSIMD( a, 2, 3, 0, 1 );
}

FORCEINLINE fltx4 AddSIMD( const fltx4 & a, const fltx4 & b )				// a+b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = a.m128_f32[i] + b.m128_f32[i];
	return result;
}

FORCEINLINE fltx4 SubSIMD( const fltx4 & a, const fltx4 & b )				// a-b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = a.m128_f32[i] - b.m128_f32[i];
	return result;
}

FORCEINLINE fltx4 MulSIMD( const fltx4 & a, const fltx4 & b )				// a*b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = a.m128_f32[i] * b.m128_f32[i];
	return result;
}

FORCEINLINE fltx4 DivSIMD( const fltx4 & a, const fltx4 & b )				// a/b
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = a.m128_f32[i] / b.m128_f32[i];
	return result;
}

// exact division; the SSE version multiplied by a hardware reciprocal estimate
FORCEINLINE fltx4 DivEstSIMD( const fltx4 & a, const fltx4 & b )			// a/b
{
	return DivSIMD( a, b );
}

FORCEINLINE fltx4 MaddSIMD( const fltx4 & a, const fltx4 & b, const fltx4 & c )				// a*b + c
{
	return AddSIMD( MulSIMD(a,b), c );
}

FORCEINLINE fltx4 MsubSIMD( const fltx4 & a, const fltx4 & b, const fltx4 & c )				// c - a*b
{
	return SubSIMD( c, MulSIMD(a,b) );
};

FORCEINLINE fltx4 Dot3SIMD( const fltx4 &a, const fltx4 &b )
{
	fltx4 m = MulSIMD( a, b );
	return AddSIMD( AddSIMD( SplatXSIMD(m), SplatYSIMD(m) ), SplatZSIMD(m) );
}

FORCEINLINE fltx4 Dot4SIMD( const fltx4 &a, const fltx4 &b )
{
	// 4 instructions, serial, order of addition varies so individual elements my differ in the LSB on some CPUs
	fltx4 fl4Product = MulSIMD( a, b );
	fltx4 fl4YXWZ = ShuffleLanesSIMD( fl4Product, 1, 0, 3, 2 );
	fltx4 fl4UUVV = AddSIMD( fl4Product, fl4YXWZ ); // U = X+Y; V = Z+W
	fltx4 fl4VVUU = RotateLeft2( fl4UUVV );
	return AddSIMD( fl4UUVV, fl4VVUU );
}

//TODO: implement as four-way Taylor series (see xbox implementation)
FORCEINLINE fltx4 SinSIMD( const fltx4 &radians )
{
	fltx4 result;
	SubFloat( result, 0 ) = sin( SubFloat( radians, 0 ) );
	SubFloat( result, 1 ) = sin( SubFloat( radians, 1 ) );
	SubFloat( result, 2 ) = sin( SubFloat( radians, 2 ) );
	SubFloat( result, 3 ) = sin( SubFloat( radians, 3 ) );
	return result;
}

FORCEINLINE void SinCos3SIMD( fltx4 &sine, fltx4 &cosine, const fltx4 &radians )
{
	// FIXME: Make a fast SSE version
	SinCos( SubFloat( radians, 0 ), &SubFloat( sine, 0 ), &SubFloat( cosine, 0 ) );
	SinCos( SubFloat( radians, 1 ), &SubFloat( sine, 1 ), &SubFloat( cosine, 1 ) );
	SinCos( SubFloat( radians, 2 ), &SubFloat( sine, 2 ), &SubFloat( cosine, 2 ) );
}

FORCEINLINE void SinCosSIMD( fltx4 &sine, fltx4 &cosine, const fltx4 &radians )				// a*b + c
{
	// FIXME: Make a fast SSE version
	SinCos( SubFloat( radians, 0 ), &SubFloat( sine, 0 ), &SubFloat( cosine, 0 ) );
	SinCos( SubFloat( radians, 1 ), &SubFloat( sine, 1 ), &SubFloat( cosine, 1 ) );
	SinCos( SubFloat( radians, 2 ), &SubFloat( sine, 2 ), &SubFloat( cosine, 2 ) );
	SinCos( SubFloat( radians, 3 ), &SubFloat( sine, 3 ), &SubFloat( cosine, 3 ) );
}

//TODO: implement as four-way Taylor series (see xbox implementation)
FORCEINLINE fltx4 ArcSinSIMD( const fltx4 &sine )
{
	// FIXME: Make a fast SSE version
	fltx4 result;
	SubFloat( result, 0 ) = asin( SubFloat( sine, 0 ) );
	SubFloat( result, 1 ) = asin( SubFloat( sine, 1 ) );
	SubFloat( result, 2 ) = asin( SubFloat( sine, 2 ) );
	SubFloat( result, 3 ) = asin( SubFloat( sine, 3 ) );
	return result;
}

FORCEINLINE fltx4 ArcCosSIMD( const fltx4 &cs )
{
	fltx4 result;
	SubFloat( result, 0 ) = acos( SubFloat( cs, 0 ) );
	SubFloat( result, 1 ) = acos( SubFloat( cs, 1 ) );
	SubFloat( result, 2 ) = acos( SubFloat( cs, 2 ) );
	SubFloat( result, 3 ) = acos( SubFloat( cs, 3 ) );
	return result;
}

// tan^1(a/b) .. ie, pass sin in as a and cos in as b
FORCEINLINE fltx4 ArcTan2SIMD( const fltx4 &a, const fltx4 &b )
{
	fltx4 result;
	SubFloat( result, 0 ) = atan2( SubFloat( a, 0 ), SubFloat( b, 0 ) );
	SubFloat( result, 1 ) = atan2( SubFloat( a, 1 ), SubFloat( b, 1 ) );
	SubFloat( result, 2 ) = atan2( SubFloat( a, 2 ), SubFloat( b, 2 ) );
	SubFloat( result, 3 ) = atan2( SubFloat( a, 3 ), SubFloat( b, 3 ) );
	return result;
}

FORCEINLINE fltx4 NegSIMD(const fltx4 &a) // negate: -a
{
	return SubSIMD(LoadZeroSIMD(),a);
}

FORCEINLINE int TestSignSIMD( const fltx4 & a )								// mask of which floats have the high bit set
{
	return (int)( ( a.m128_u32[0] >> 31 ) | ( ( a.m128_u32[1] >> 31 ) << 1 ) | ( ( a.m128_u32[2] >> 31 ) << 2 ) | ( ( a.m128_u32[3] >> 31 ) << 3 ) );
}

FORCEINLINE bool IsAnyNegative( const fltx4 & a )							// (a.x < 0) || (a.y < 0) || (a.z < 0) || (a.w < 0)
{
	return (0 != TestSignSIMD( a ));
}

FORCEINLINE bool IsAnyTrue( const fltx4 & a )
{
	return (0 != TestSignSIMD( a ));
}

FORCEINLINE fltx4 CmpEqSIMD( const fltx4 & a, const fltx4 & b )				// (a==b) ? ~0:0
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] == b.m128_f32[i] ) ? 0xFFFFFFFFu : 0u;
	return result;
}

FORCEINLINE fltx4 CmpGtSIMD( const fltx4 & a, const fltx4 & b )				// (a>b) ? ~0:0
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] > b.m128_f32[i] ) ? 0xFFFFFFFFu : 0u;
	return result;
}

FORCEINLINE fltx4 CmpGeSIMD( const fltx4 & a, const fltx4 & b )				// (a>=b) ? ~0:0
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] >= b.m128_f32[i] ) ? 0xFFFFFFFFu : 0u;
	return result;
}

FORCEINLINE fltx4 CmpLtSIMD( const fltx4 & a, const fltx4 & b )				// (a<b) ? ~0:0
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] < b.m128_f32[i] ) ? 0xFFFFFFFFu : 0u;
	return result;
}

FORCEINLINE fltx4 CmpLeSIMD( const fltx4 & a, const fltx4 & b )				// (a<=b) ? ~0:0
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] <= b.m128_f32[i] ) ? 0xFFFFFFFFu : 0u;
	return result;
}

// for branching when a.xyzw > b.xyzw
FORCEINLINE bool IsAllGreaterThan( const fltx4 &a, const fltx4 &b )
{
	return	TestSignSIMD( CmpLeSIMD( a, b ) ) == 0;
}

// for branching when a.xyzw >= b.xyzw
FORCEINLINE bool IsAllGreaterThanOrEq( const fltx4 &a, const fltx4 &b )
{
	return	TestSignSIMD( CmpLtSIMD( a, b ) ) == 0;
}

// For branching if all a.xyzw == b.xyzw
FORCEINLINE bool IsAllEqual( const fltx4 & a, const fltx4 & b )
{
	return	TestSignSIMD( CmpEqSIMD( a, b ) ) == 0xf;
}

FORCEINLINE fltx4 CmpInBoundsSIMD( const fltx4 & a, const fltx4 & b )		// (a <= b && a >= -b) ? ~0 : 0
{
	return AndSIMD( CmpLeSIMD(a,b), CmpGeSIMD(a, NegSIMD(b)) );
}

// NaN / equal operands: the second operand b is returned (same as SSE minps / maxps)
FORCEINLINE fltx4 MinSIMD( const fltx4 & a, const fltx4 & b )				// min(a,b)
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] < b.m128_f32[i] ) ? a.m128_u32[i] : b.m128_u32[i];
	return result;
}

FORCEINLINE fltx4 MaxSIMD( const fltx4 & a, const fltx4 & b )				// max(a,b)
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_u32[i] = ( a.m128_f32[i] > b.m128_f32[i] ) ? a.m128_u32[i] : b.m128_u32[i];
	return result;
}



// SSE lacks rounding operations. 
// Really.
// You can emulate them by setting the rounding mode for the 
// whole processor and then converting to int, and then back again.
// But every time you set the rounding mode, you clear out the
// entire pipeline. So, I can't do them per operation. You
// have to do it once, before the loop that would call these.
// Round towards positive infinity
FORCEINLINE fltx4 CeilSIMD( const fltx4 &a )
{
	fltx4 retVal;
	SubFloat( retVal, 0 ) = ceil( SubFloat( a, 0 ) );
	SubFloat( retVal, 1 ) = ceil( SubFloat( a, 1 ) );
	SubFloat( retVal, 2 ) = ceil( SubFloat( a, 2 ) );
	SubFloat( retVal, 3 ) = ceil( SubFloat( a, 3 ) );
	return retVal;

}

fltx4 AbsSIMD( const fltx4 & x );		// To make it more coherent with the whole API (the whole SIMD API is postfixed with SIMD except a couple of methods. Well...)
fltx4 fabs( const fltx4 & x );

// Round towards negative infinity
// This is the implementation that was here before; it assumes
// you are in round-to-floor mode, which I guess is usually the
// case for us vis-a-vis SSE. It's totally unnecessary on 
// VMX, which has a native floor op.
FORCEINLINE fltx4 FloorSIMD( const fltx4 &val )
{
	fltx4 fl4Abs = fabs( val );
	fltx4 ival = SubSIMD( AddSIMD( fl4Abs, Four_2ToThe23s ), Four_2ToThe23s );
	ival = MaskedAssign( CmpGtSIMD( ival, fl4Abs ), SubSIMD( ival, Four_Ones ), ival );
	return XorSIMD( ival, XorSIMD( val, fl4Abs ) );			// restore sign bits
}



FORCEINLINE bool IsAnyZeros( const fltx4 & a )								// any floats are zero?
{
	return TestSignSIMD( CmpEqSIMD( a, Four_Zeros ) ) != 0;
}

inline bool IsAllZeros( const fltx4 & var )
{
	return TestSignSIMD( CmpEqSIMD( var, Four_Zeros ) ) == 0xF;
}

FORCEINLINE fltx4 SqrtSIMD( const fltx4 & a )						// sqrt(a)
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = std::sqrt( a.m128_f32[i] );
	return result;
}

FORCEINLINE fltx4 SqrtEstSIMD( const fltx4 & a )					// sqrt(a), exact (was already sqrtps)
{
	return SqrtSIMD( a );
}

// exact 1/sqrt(a); the SSE version was the ~12 bit rsqrtps estimate
FORCEINLINE fltx4 ReciprocalSqrtEstSIMD( const fltx4 & a )			// 1/sqrt(a)
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = 1.0f / std::sqrt( a.m128_f32[i] );
	return result;
}

FORCEINLINE fltx4 ReciprocalSqrtEstSaturateSIMD( const fltx4 & a )
{
	fltx4 zero_mask = CmpEqSIMD( a, Four_Zeros );
	fltx4 ret = OrSIMD( a, AndSIMD( Four_Epsilons, zero_mask ) );
	ret = ReciprocalSqrtEstSIMD( ret );
	return ret;
}

// exact 1/sqrt(a); the SSE version was rsqrtps plus one newton iteration
FORCEINLINE fltx4 ReciprocalSqrtSIMD( const fltx4 & a )				// 1/sqrt(a)
{
	return ReciprocalSqrtEstSIMD( a );
}

// exact 1/a; the SSE version was the ~12 bit rcpps estimate
FORCEINLINE fltx4 ReciprocalEstSIMD( const fltx4 & a )				// 1/a
{
	fltx4 result;
	for ( int i = 0; i < 4; i++ )
		result.m128_f32[i] = 1.0f / a.m128_f32[i];
	return result;
}

/// 1/x for all 4 values, more or less
/// 1/0 will result in a big but NOT infinite result
FORCEINLINE fltx4 ReciprocalEstSaturateSIMD( const fltx4 & a )
{
	fltx4 zero_mask = CmpEqSIMD( a, Four_Zeros );
	fltx4 ret = OrSIMD( a, AndSIMD( Four_Epsilons, zero_mask ) );
	ret = ReciprocalEstSIMD( ret );
	return ret;
}

/// 1/x for all 4 values. Exact; the SSE version was rcpps plus one newton iteration.
/// No error checking!
FORCEINLINE fltx4 ReciprocalSIMD( const fltx4 & a )					// 1/a
{
	return ReciprocalEstSIMD( a );
}

/// 1/x for all 4 values.
/// 1/0 will result in a big but NOT infinite result
FORCEINLINE fltx4 ReciprocalSaturateSIMD( const fltx4 & a )
{
	fltx4 zero_mask = CmpEqSIMD( a, Four_Zeros );
	fltx4 ret = OrSIMD( a, AndSIMD( Four_Epsilons, zero_mask ) );
	ret = ReciprocalSIMD( ret );
	return ret;
}

// CHRISG: is it worth doing integer bitfiddling for this?
// 2^x for all values (the antilog)
FORCEINLINE fltx4 ExpSIMD( const fltx4 &toPower )
{
	fltx4 retval;
	SubFloat( retval, 0 ) = powf( 2, SubFloat(toPower, 0) );
	SubFloat( retval, 1 ) = powf( 2, SubFloat(toPower, 1) );
	SubFloat( retval, 2 ) = powf( 2, SubFloat(toPower, 2) );
	SubFloat( retval, 3 ) = powf( 2, SubFloat(toPower, 3) );

	return retval;
}

// Clamps the components of a vector to a specified minimum and maximum range.
FORCEINLINE fltx4 ClampVectorSIMD( FLTX4 in, FLTX4 min, FLTX4 max)
{
	return MaxSIMD( min, MinSIMD( max, in ) );
}

FORCEINLINE void TransposeSIMD( fltx4 & x, fltx4 & y, fltx4 & z, fltx4 & w)
{
	const fltx4 rows[4] = { x, y, z, w };
	fltx4 * const outs[4] = { &x, &y, &z, &w };
	for ( int r = 0; r < 4; r++ )
		for ( int c = 0; c < 4; c++ )
			outs[r]->m128_u32[c] = rows[c].m128_u32[r];
}

FORCEINLINE fltx4 FindLowestSIMD3( const fltx4 &a )
{
	// a is [x,y,z,G] (where G is garbage)
	// rotate left by one 
	fltx4 compareOne = RotateLeft( a );
	// compareOne is [y,z,G,x]
	fltx4 retval = MinSIMD( a, compareOne );
	// retVal is [min(x,y), ... ]
	compareOne = RotateLeft2( a );
	// compareOne is [z, G, x, y]
	retval = MinSIMD( retval, compareOne );
	// retVal = [ min(min(x,y),z)..]
	// splat the x component out to the whole vector and return
	return SplatXSIMD( retval );
	
}

FORCEINLINE fltx4 FindHighestSIMD3( const fltx4 &a )
{
	// a is [x,y,z,G] (where G is garbage)
	// rotate left by one 
	fltx4 compareOne = RotateLeft( a );
	// compareOne is [y,z,G,x]
	fltx4 retval = MaxSIMD( a, compareOne );
	// retVal is [max(x,y), ... ]
	compareOne = RotateLeft2( a );
	// compareOne is [z, G, x, y]
	retval = MaxSIMD( retval, compareOne );
	// retVal = [ max(max(x,y),z)..]
	// splat the x component out to the whole vector and return
	return SplatXSIMD( retval );
	
}


inline bool IsVector3LessThan(const fltx4 &v1, const fltx4 &v2 )
{
	bi32x4 isOut = CmpLtSIMD( v1, v2 );
	return IsAnyNegative( isOut );
}

inline bool IsVector4LessThan(const fltx4 &v1, const fltx4 &v2 )
{
	bi32x4 isOut = CmpLtSIMD( v1, v2 );
	return IsAnyNegative( isOut );
}


// ------------------------------------
// INTEGER SIMD OPERATIONS.
// ------------------------------------


// Load 4 aligned words into a SIMD register
FORCEINLINE i32x4 LoadAlignedIntSIMD( const void * RESTRICT pSIMD)
{
	return LoadAlignedSIMD( pSIMD );
}

// Load 4 unaligned words into a SIMD register
FORCEINLINE i32x4 LoadUnalignedIntSIMD( const void * RESTRICT pSIMD)
{
	return LoadAlignedSIMD( pSIMD );
}

// save into four words, 16-byte aligned
FORCEINLINE void StoreAlignedIntSIMD( int32_t * RESTRICT pSIMD, const fltx4 & a )
{
	memcpy( pSIMD, &a, sizeof( a ) );
}

FORCEINLINE void StoreAlignedIntSIMD( intx4 &pSIMD, const fltx4 & a )
{
	memcpy( pSIMD.Base(), &a, sizeof( a ) );
}

FORCEINLINE void StoreUnalignedIntSIMD( int32_t * RESTRICT pSIMD, const fltx4 & a )
{
	memcpy( pSIMD, &a, sizeof( a ) );
}

// a={ a.x, a.z, b.x, b.z }
// combine two fltx4s by throwing away every other field.
FORCEINLINE fltx4 CompressSIMD( fltx4 const & a, fltx4 const &b )
{
	fltx4 result;
	result.m128_u32[0] = a.m128_u32[0];
	result.m128_u32[1] = a.m128_u32[2];
	result.m128_u32[2] = b.m128_u32[0];
	result.m128_u32[3] = b.m128_u32[2];
	return result;
}

// Load four consecutive uint16's, and turn them into floating point numbers.
FORCEINLINE fltx4 LoadAndConvertUint16SIMD( const uint16_t *pInts )
{
	fltx4 retval;
	SubFloat( retval, 0 ) = pInts[0];
	SubFloat( retval, 1 ) = pInts[1];
	SubFloat( retval, 2 ) = pInts[2];
	SubFloat( retval, 3 ) = pInts[3];
	return retval;
}


// a={ a.x, b.x, c.x, d.x }
// combine 4 fltx4s by throwing away 3/4s of the fields
FORCEINLINE fltx4 Compress4SIMD( fltx4 const a, fltx4 const &b, fltx4 const &c, fltx4 const &d )
{
	fltx4 result;
	result.m128_u32[0] = a.m128_u32[0];
	result.m128_u32[1] = b.m128_u32[0];
	result.m128_u32[2] = c.m128_u32[0];
	result.m128_u32[3] = d.m128_u32[0];
	return result;
}

// outa={a.x, a.x, a.y, a.y}, outb = a.z, a.z, a.w, a.w }
FORCEINLINE void ExpandSIMD( fltx4 const &a, fltx4 &fl4OutA, fltx4 &fl4OutB )
{
	fl4OutA = ShuffleLanesSIMD( a, 0, 0, 1, 1 );
	fl4OutB = ShuffleLanesSIMD( a, 2, 2, 3, 3 );

}


// construct a fltx4 from four different scalars, which are assumed to be neither aligned nor contiguous
FORCEINLINE fltx4 LoadGatherSIMD( const float &x, const float &y, const float &z, const float &w )
{
	fltx4 result;
	memcpy( &result.m128_u32[0], &x, sizeof( float ) );
	memcpy( &result.m128_u32[1], &y, sizeof( float ) );
	memcpy( &result.m128_u32[2], &z, sizeof( float ) );
	memcpy( &result.m128_u32[3], &w, sizeof( float ) );
	return result;
}

// CHRISG: the conversion functions all seem to operate on m64's only...
// how do we make them work here?

// Take a fltx4 containing fixed-point uints and 
// return them as single precision floats. No
// fixed point conversion is done.
FORCEINLINE fltx4 UnsignedIntConvertToFltSIMD( const u32x4 &vSrcA )
{
	fltx4 retval;
	SubFloat( retval, 0 ) = ( (float) SubInt( vSrcA, 0 ) );
	SubFloat( retval, 1 ) = ( (float) SubInt( vSrcA, 1 ) );
	SubFloat( retval, 2 ) = ( (float) SubInt( vSrcA, 2 ) );
	SubFloat( retval, 3 ) = ( (float) SubInt( vSrcA, 3 ) );
	return retval;
}

// Take a fltx4 containing fixed-point sints and 
// return them as single precision floats. No 
// fixed point conversion is done.
FORCEINLINE fltx4 SignedIntConvertToFltSIMD( const i32x4 &vSrcA )
{
	fltx4 retval;
	for ( int i = 0; i < 4; i++ )
		retval.m128_f32[i] = (float)vSrcA.m128_i32[i];
	return retval;
}

FORCEINLINE fltx4 SignedIntConvertToFltSIMD( const shortx8 &vSrcA )
{
	fltx4 retval;
	for ( int i = 0; i < 4; i++ )
		retval.m128_f32[i] = (float)vSrcA.m128i_i32[i];
	return retval;
}

/*
  works on fltx4's as if they are four uints.
  the first parameter contains the words to be shifted,
  the second contains the amount to shift by AS INTS

  for i = 0 to 3
  shift = vSrcB_i*32:(i*32)+4
  vReturned_i*32:(i*32)+31 = vSrcA_i*32:(i*32)+31 << shift
*/
FORCEINLINE i32x4 IntShiftLeftWordSIMD(const i32x4 &vSrcA, const i32x4 &vSrcB)
{
	i32x4 retval;
	SubInt(retval, 0) = SubInt(vSrcA, 0) << SubInt(vSrcB, 0);
	SubInt(retval, 1) = SubInt(vSrcA, 1) << SubInt(vSrcB, 1);
	SubInt(retval, 2) = SubInt(vSrcA, 2) << SubInt(vSrcB, 2);
	SubInt(retval, 3) = SubInt(vSrcA, 3) << SubInt(vSrcB, 3);


	return retval;
}


// Fixed-point conversion and save as SIGNED INTS.
// pDest->x = Int (vSrc.x)
// Truncates toward zero; out-of-range values and NaN give INT_MIN (same as SSE cvttps2dq).
FORCEINLINE void ConvertStoreAsIntsSIMD(intx4 * RESTRICT pDest, const fltx4 &vSrc)
{
	for ( int i = 0; i < 4; i++ )
	{
		float f = SubFloat( vSrc, i );
		(*pDest)[i] = ( f >= -2147483648.0f && f < 2147483648.0f ) ? (int)f : INT32_MIN;
	}
}




// a={a.y, a.z, a.w, b.x } b={b.y, b.z, b.w, b.x }
FORCEINLINE void RotateLeftDoubleSIMD( fltx4 &a, fltx4 &b )
{
	a = SetWSIMD( RotateLeft( a ), SplatXSIMD( b ) );
	b = RotateLeft( b );
}


// Some convenience operator overloads, which are just aliasing the functions above.
// Componentwise add
FORCEINLINE fltx4 operator+( const fltx4& a, const fltx4& b )
{
	return AddSIMD( a, b );
}

// Componentwise subtract
FORCEINLINE fltx4 operator-( const fltx4& a, const fltx4& b )
{
	return SubSIMD( a, b );
}

// Componentwise multiply
FORCEINLINE fltx4 operator*( const fltx4& a, const fltx4& b )
{
	return MulSIMD( a, b );
}

// No divide. You need to think carefully about whether you want a reciprocal
// or a reciprocal estimate.

// bitwise and
FORCEINLINE fltx4 operator&( const fltx4& a, const fltx4& b )
{
	return AndSIMD( a ,b );
}

// bitwise or
FORCEINLINE fltx4 operator|( const fltx4& a, const fltx4& b )
{
	return OrSIMD( a, b );
}

// bitwise xor
FORCEINLINE fltx4 operator^( const fltx4& a, const fltx4& b )
{
	return XorSIMD( a, b );
}

// unary negate
FORCEINLINE fltx4 operator-( const fltx4& a )
{
	return NegSIMD( a );
}

FORCEINLINE fltx4 & operator+=( fltx4 &a, FLTX4 b )
{
	a = AddSIMD( a, b );
	return a;
}

FORCEINLINE fltx4 & operator-=( fltx4 &a, FLTX4 b )
{
	a = SubSIMD( a, b );
	return a;
}

FORCEINLINE fltx4 & operator*=( fltx4 &a, FLTX4 b )
{
	a = MulSIMD( a, b );
	return a;
}

/// calculate the absolute value of a packed single
inline fltx4 fabs( const fltx4 & x )
{
	return AndSIMD( x, LoadAlignedSIMD( g_SIMD_clear_signmask ) );
}

// Convenience version
inline fltx4 AbsSIMD( const fltx4 & x )
{
	return fabs( x );
}

/// negate all four components of a SIMD packed single
inline fltx4 fnegate( const fltx4 & x )
{
	return XorSIMD( x, LoadAlignedSIMD( g_SIMD_signmask ) );
}

///  (x<1)?x^(1/2.2):1. Use a 4th order polynomial to approximate x^(1/2.2) over 0..1
inline fltx4 LinearToGammaSIMD( fltx4 x )
{
	// y = -3.7295x4 + 8.9635x3 - 7.7397x2 + 3.443x + 0.048
	x = MaxSIMD( MinSIMD( Four_Ones, x ), Four_Zeros );
	return AddSIMD( Four_LinearToGammaCoefficients_E,
					MulSIMD( x, AddSIMD( Four_LinearToGammaCoefficients_D, 
										 MulSIMD( x, AddSIMD( Four_LinearToGammaCoefficients_C,
															  MulSIMD( x, AddSIMD( Four_LinearToGammaCoefficients_B,
																				   MulSIMD( x, Four_LinearToGammaCoefficients_A ) ) ) ) ) ) ) );
}


inline fltx4 GammaToLinearSIMD( fltx4 x )
{
	x = MaxSIMD( x, Four_Zeros );
	x = AddSIMD( Four_GammaToLinearCoefficients_D,
					MulSIMD( x, AddSIMD( Four_GammaToLinearCoefficients_C, 
										 MulSIMD( x, AddSIMD( Four_GammaToLinearCoefficients_B,
															  MulSIMD( x, Four_GammaToLinearCoefficients_A ) ) ) ) ) );
	return MinSIMD( x, Four_Ones );
}

/// ( x > 1 ) ? x : x^2.2
inline fltx4 GammaToLinearExtendedSIMD( fltx4 x )
{
	x = MaxSIMD( x, Four_Zeros );
	fltx4 fl4Ret = AddSIMD( Four_GammaToLinearCoefficients_D,
					MulSIMD( x, AddSIMD( Four_GammaToLinearCoefficients_C, 
										 MulSIMD( x, AddSIMD( Four_GammaToLinearCoefficients_B,
															  MulSIMD( x, Four_GammaToLinearCoefficients_A ) ) ) ) ) );
	return MaskedAssign( CmpGeSIMD( x, Four_Ones ), x, fl4Ret );
}


FORCEINLINE fltx4 LerpSIMD ( const fltx4 &percent, const fltx4 &a, const fltx4 &b)
{
	return AddSIMD( a, MulSIMD( SubSIMD( b, a ), percent ) );
}

FORCEINLINE fltx4 RemapValClampedSIMD(const fltx4 &val, const fltx4 &a, const fltx4 &b, const fltx4 &c, const fltx4 &d) // Remap val from clamped range between a and b to new range between c and d
{
	fltx4 range = MaskedAssign( CmpEqSIMD( a, b ), Four_Ones, SubSIMD( b, a ) ); //make sure range > 0
	fltx4 cVal = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, DivSIMD( SubSIMD( val, a ), range ) ) ); //saturate
	return LerpSIMD( cVal, c, d );
}

// SIMD versions of mathlib simplespline functions
// hermite basis function for smooth interpolation
// Similar to Gain() above, but very cheap to call
// value should be between 0 & 1 inclusive
inline fltx4 SimpleSpline( const fltx4 & value )
{
	// Arranged to avoid a data dependency between these two MULs:
	fltx4 valueDoubled = MulSIMD( value, Four_Twos );
	fltx4 valueSquared = MulSIMD( value, value );

	// Nice little ease-in, ease-out spline-like curve
	return SubSIMD(
		MulSIMD( Four_Threes,  valueSquared ),
		MulSIMD( valueDoubled, valueSquared ) );
}

// remaps a value in [startInterval, startInterval+rangeInterval] from linear to
// spline using SimpleSpline
inline fltx4 SimpleSplineRemapValWithDeltas( const fltx4 & val,
											 const fltx4 & A, const fltx4 & BMinusA,
											 const fltx4 & OneOverBMinusA, const fltx4 & C, 
											 const fltx4 & DMinusC )
{
// 	if ( A == B )
// 		return val >= B ? D : C;
	fltx4 cVal = MulSIMD( SubSIMD( val, A), OneOverBMinusA );
	return AddSIMD( C, MulSIMD( DMinusC, SimpleSpline( cVal ) ) );
}

inline fltx4 SimpleSplineRemapValWithDeltasClamped( const fltx4 & val,
													const fltx4 & A, const fltx4 & BMinusA,
													const fltx4 & OneOverBMinusA, const fltx4 & C, 
													const fltx4 & DMinusC )
{
// 	if ( A == B )
// 		return val >= B ? D : C;
	fltx4 cVal = MulSIMD( SubSIMD( val, A), OneOverBMinusA );
	cVal = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, cVal ) );
	return AddSIMD( C, MulSIMD( DMinusC, SimpleSpline( cVal ) ) );
}

FORCEINLINE fltx4 FracSIMD( const fltx4 &val )
{
	fltx4 fl4Abs = fabs( val );
	fltx4 ival = SubSIMD( AddSIMD( fl4Abs, Four_2ToThe23s ), Four_2ToThe23s );
	ival = MaskedAssign( CmpGtSIMD( ival, fl4Abs ), SubSIMD( ival, Four_Ones ), ival );
	return XorSIMD( SubSIMD( fl4Abs, ival ), XorSIMD( val, fl4Abs ) );			// restore sign bits
}

FORCEINLINE fltx4 Mod2SIMD( const fltx4 &val )
{
	fltx4 fl4Abs = fabs( val );
	fltx4 ival = SubSIMD( AndSIMD( LoadAlignedSIMD( (float *) g_SIMD_lsbmask ), AddSIMD( fl4Abs, Four_2ToThe23s ) ), Four_2ToThe23s );
	ival = MaskedAssign( CmpGtSIMD( ival, fl4Abs ), SubSIMD( ival, Four_Twos ), ival );
	return XorSIMD( SubSIMD( fl4Abs, ival ), XorSIMD( val, fl4Abs ) );			// restore sign bits
}

FORCEINLINE fltx4 Mod2SIMDPositiveInput( const fltx4 &val )
{
	fltx4 ival = SubSIMD( AndSIMD( LoadAlignedSIMD( g_SIMD_lsbmask ), AddSIMD( val, Four_2ToThe23s ) ), Four_2ToThe23s );
	ival = MaskedAssign( CmpGtSIMD( ival, val ), SubSIMD( ival, Four_Twos ), ival );
	return SubSIMD( val, ival );
}


// approximate sin of an angle, with -1..1 representing the whole sin wave period instead of -pi..pi.
// no range reduction is done - for values outside of 0..1 you won't like the results
FORCEINLINE fltx4 _SinEst01SIMD( const fltx4 &val )
{
	// really rough approximation - x*(4-x*4) - a parabola. s(0) = 0, s(.5) = 1, s(1)=0, smooth in-between.
	// sufficient for simple oscillation.
	return MulSIMD( val, SubSIMD( Four_Fours, MulSIMD( val, Four_Fours ) ) );
}

FORCEINLINE fltx4 _Sin01SIMD( const fltx4 &val )
{
	// not a bad approximation : parabola always over-estimates. Squared parabola always
	// underestimates. So lets blend between them:  goodsin = badsin + .225*( badsin^2-badsin)
	fltx4 fl4BadEst = MulSIMD( val, SubSIMD( Four_Fours, MulSIMD( val, Four_Fours ) ) );
	return AddSIMD( MulSIMD( Four_Point225s, SubSIMD( MulSIMD( fl4BadEst, fl4BadEst ), fl4BadEst ) ), fl4BadEst );
}

// full range useable implementations
FORCEINLINE fltx4 SinEst01SIMD( const fltx4 &val )
{
	fltx4 fl4Abs = fabs( val );
	fltx4 fl4Reduced2 = Mod2SIMDPositiveInput( fl4Abs );
	bi32x4 fl4OddMask = CmpGeSIMD( fl4Reduced2, Four_Ones );
	fltx4 fl4val = SubSIMD( fl4Reduced2, AndSIMD( Four_Ones, fl4OddMask ) );
	fltx4 fl4Sin = _SinEst01SIMD( fl4val );
	fl4Sin = XorSIMD( fl4Sin, AndSIMD( LoadAlignedSIMD( g_SIMD_signmask ), XorSIMD( val, fl4OddMask ) ) );
	return fl4Sin;

}

FORCEINLINE fltx4 Sin01SIMD( const fltx4 &val )
{
	fltx4 fl4Abs = fabs( val );
	fltx4 fl4Reduced2 = Mod2SIMDPositiveInput( fl4Abs );
	bi32x4 fl4OddMask = CmpGeSIMD( fl4Reduced2, Four_Ones );
	fltx4 fl4val = SubSIMD( fl4Reduced2, AndSIMD( Four_Ones, fl4OddMask ) );
	fltx4 fl4Sin = _Sin01SIMD( fl4val );
	fl4Sin = XorSIMD( fl4Sin, AndSIMD( LoadAlignedSIMD( g_SIMD_signmask ), XorSIMD( val, fl4OddMask ) ) );
	return fl4Sin;

}

FORCEINLINE fltx4 NatExpSIMD( const fltx4 &val )			// why is ExpSimd( x ) defined to be 2^x?
{
	// need to write this. just stub with normal float implementation for now
	fltx4 fl4Result;
	SubFloat( fl4Result, 0 ) = exp( SubFloat( val, 0 ) );
	SubFloat( fl4Result, 1 ) = exp( SubFloat( val, 1 ) );
	SubFloat( fl4Result, 2 ) = exp( SubFloat( val, 2 ) );
	SubFloat( fl4Result, 3 ) = exp( SubFloat( val, 3 ) );
	return fl4Result;
}

// Schlick style Bias approximation see graphics gems 4 : bias(t,a)= t/( (1/a-2)*(1-t)+1)
 
FORCEINLINE fltx4 PreCalcBiasParameter( const fltx4 &bias_parameter )
{
	// convert perlin-style-bias parameter to the value right for the approximation
	return SubSIMD( ReciprocalSIMD( bias_parameter ), Four_Twos );
}

FORCEINLINE fltx4 BiasSIMD( const fltx4 &val, const fltx4 &precalc_param )
{
	// similar to bias function except pass precalced bias value from calling PreCalcBiasParameter.

	//!!speed!! use reciprocal est?
	//!!speed!! could save one op by precalcing _2_ values
	return DivSIMD( val, AddSIMD( MulSIMD( precalc_param, SubSIMD( Four_Ones, val ) ), Four_Ones ) );
}

//-----------------------------------------------------------------------------
// Box/plane test 
// NOTE: The w component of emins + emaxs must be 1 for this to work
//-----------------------------------------------------------------------------


FORCEINLINE int BoxOnPlaneSideSIMD( const fltx4& emins, const fltx4& emaxs, const cplane_t *p, float tolerance = 0.f )
{
	fltx4 corners[2];
	fltx4 normal = LoadUnalignedSIMD( p->normal.Base() );
	fltx4 dist = ReplicateX4( -p->dist );
	normal = SetWSIMD( normal, dist );
	fltx4 t4 = ReplicateX4( tolerance );
	fltx4 negt4 = ReplicateX4( -tolerance );
	bi32x4 cmp = CmpGeSIMD( normal, Four_Zeros );
	corners[0] = MaskedAssign( cmp, emaxs, emins );
	corners[1] = MaskedAssign( cmp, emins, emaxs );
	fltx4 dot1 = Dot4SIMD( normal, corners[0] );
	fltx4 dot2 = Dot4SIMD( normal, corners[1] );
	cmp = CmpGeSIMD( dot1, t4 );
	bi32x4 cmp2 = CmpGtSIMD( negt4, dot2 );
	fltx4 result = MaskedAssign( cmp, Four_Ones, Four_Zeros );
	fltx4 result2 = MaskedAssign( cmp2, Four_Twos, Four_Zeros );
	result = AddSIMD( result, result2 );
	intx4 sides;
	ConvertStoreAsIntsSIMD( &sides, result );
	return sides[0];
}


// These are not optimized right now for some platforms. We should be able to shuffle the values in some platforms.
// As the methods are hard-coded we can actually avoid loading memory to do the transfer.
// We should be able to create all versions.
FORCEINLINE fltx4 SetWFromXSIMD( const fltx4 & a, const fltx4 & x )
{
	fltx4 value = SplatXSIMD( x );
	return SetWSIMD( a, value );
}

FORCEINLINE fltx4 SetWFromYSIMD( const fltx4 & a, const fltx4 & y )
{
	fltx4 value = SplatYSIMD( y );
	return SetWSIMD( a, value );
}

FORCEINLINE fltx4 SetWFromZSIMD( const fltx4 & a, const fltx4 & z )
{
	fltx4 value = SplatZSIMD( z );
	return SetWSIMD( a, value );
}

FORCEINLINE fltx4 CrossProductSIMD( const fltx4 &A, const fltx4 &B )
{
	fltx4 CrossVal;
	SubFloat( CrossVal, 0 ) = SubFloat( A, 1 )*SubFloat( B, 2 ) - SubFloat( A, 2 )*SubFloat( B, 1 );
	SubFloat( CrossVal, 1 ) = SubFloat( A, 2 )*SubFloat( B, 0 ) - SubFloat( A, 0 )*SubFloat( B, 2 );
	SubFloat( CrossVal, 2 ) = SubFloat( A, 0 )*SubFloat( B, 1 ) - SubFloat( A, 1 )*SubFloat( B, 0 );
	SubFloat( CrossVal, 3 ) = 0;
	return CrossVal;
}

inline const fltx4 Length3SIMD(const fltx4 vec)
{
	fltx4 scLengthSqr = Dot3SIMD(vec,vec);
	bi32x4 isSignificant = CmpGtSIMD(scLengthSqr, Four_Epsilons);
	fltx4 scLengthInv = ReciprocalSqrtSIMD(scLengthSqr);
	return AndSIMD(isSignificant, MulSIMD(scLengthInv, scLengthSqr));
}

inline const fltx4 Normalized3SIMD (const fltx4 vec)
{
	fltx4 scLengthSqr = Dot3SIMD(vec,vec);
	bi32x4 isSignificant = CmpGtSIMD(scLengthSqr, Four_Epsilons);
	fltx4 scLengthInv = ReciprocalSqrtSIMD(scLengthSqr);
	return AndSIMD(isSignificant, MulSIMD(vec, scLengthInv));
}



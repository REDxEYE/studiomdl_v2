//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//


#include "mathlib/ssemath.h"
#include "mathlib/compressed_vector.h"

// NOTE: This has to be the last file included!


const fltx4 Four_PointFives={0.5,0.5,0.5,0.5};
const fltx4 Four_Zeros={0.0,0.0,0.0,0.0};
const fltx4 Four_Ones={1.0,1.0,1.0,1.0};
const fltx4 Four_Twos={2.0,2.0,2.0,2.0};
const fltx4 Four_Threes={3.0,3.0,3.0,3.0};
const fltx4 Four_Fours={4.0,4.0,4.0,4.0};
const fltx4 Four_Origin={0,0,0,1};
const fltx4 Four_NegativeOnes={-1,-1,-1,-1};

const fltx4 Four_2ToThe21s={ (float) (1<<21), (float) (1<<21), (float) (1<<21), (float)(1<<21) };
const fltx4 Four_2ToThe22s={ (float) (1<<22), (float) (1<<22), (float) (1<<22), (float)(1<<22) };
const fltx4 Four_2ToThe23s={ (float) (1<<23), (float) (1<<23), (float) (1<<23), (float)(1<<23) };
const fltx4 Four_2ToThe24s={ (float) (1<<24), (float) (1<<24), (float) (1<<24), (float)(1<<24) };
const fltx4 Four_Thirds={ 0.33333333, 0.33333333, 0.33333333, 0.33333333 };
const fltx4 Four_TwoThirds={ 0.66666666, 0.66666666, 0.66666666, 0.66666666 };
const fltx4 Four_Point225s={ .225, .225, .225, .225 };
const fltx4 Four_Epsilons={FLT_EPSILON,FLT_EPSILON,FLT_EPSILON,FLT_EPSILON};
const fltx4 Four_DegToRad= { ((float)(M_PI_F / 180.f)), ((float)(M_PI_F / 180.f)), ((float)(M_PI_F / 180.f)), ((float)(M_PI_F / 180.f))};

const fltx4 Four_FLT_MAX={FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX};
const fltx4 Four_Negative_FLT_MAX={-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX};
const fltx4 g_SIMD_0123 = { 0., 1., 2., 3. };

const fltx4 Four_LinearToGammaCoefficients_A = { -3.7295, -3.7295, -3.7295, -3.7295 };
const fltx4 Four_LinearToGammaCoefficients_B = { 8.9635,  8.9635,  8.9635,  8.9635 };
const fltx4 Four_LinearToGammaCoefficients_C = { -7.7397,  -7.7397,  -7.7397,  -7.7397 };
const fltx4 Four_LinearToGammaCoefficients_D = {3.443, 3.443, 3.443, 3.443 };
const fltx4 Four_LinearToGammaCoefficients_E = { 0.048, 0.048, 0.048, 0.048 };

const fltx4 Four_GammaToLinearCoefficients_A = { .1731, .1731, .1731, .1731 };
const fltx4 Four_GammaToLinearCoefficients_B = { .8717, .8717, .8717, .8717 };
const fltx4 Four_GammaToLinearCoefficients_C = { -.0452, -.0452, -.0452, -.0452 };
const fltx4 Four_GammaToLinearCoefficients_D = { .0012, .0012, .0012, .0012 };

const uint32_t ALIGN16 g_SIMD_clear_signmask[4] ALIGN16_POST = {0x7fffffff,0x7fffffff,0x7fffffff,0x7fffffff};
const uint32_t ALIGN16 g_SIMD_signmask[4] ALIGN16_POST = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
const uint32_t ALIGN16 g_SIMD_lsbmask[4] ALIGN16_POST = { 0xfffffffe, 0xfffffffe, 0xfffffffe, 0xfffffffe };
const uint32_t ALIGN16 g_SIMD_clear_wmask[4] ALIGN16_POST = { 0xffffffff, 0xffffffff, 0xffffffff, 0 };
const uint32_t ALIGN16 g_SIMD_AllOnesMask[4] ALIGN16_POST = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff }; // ~0,~0,~0,~0
const int32_t ALIGN16 g_SIMD_Low16BitsMask[4] ALIGN16_POST = { 0xffff, 0xffff, 0xffff, 0xffff }; // 0xffff x 4


const uint32_t ALIGN16 g_SIMD_ComponentMask[4][4] ALIGN16_POST =
{
	{ 0xFFFFFFFF, 0, 0, 0 }, { 0, 0xFFFFFFFF, 0, 0 }, { 0, 0, 0xFFFFFFFF, 0 }, { 0, 0, 0, 0xFFFFFFFF }
};

const fltx4 g_SIMD_Identity[4] =
{
	{ 1.0, 0, 0, 0 }, { 0, 1.0, 0, 0 }, { 0, 0, 1.0, 0 }, { 0, 0, 0, 1.0 }
};

const uint32_t ALIGN16 g_SIMD_SkipTailMask[4][4] ALIGN16_POST =
{
	{ 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff },
	{ 0xffffffff, 0x00000000, 0x00000000, 0x00000000 },
	{ 0xffffffff, 0xffffffff, 0x00000000, 0x00000000 },
	{ 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000 },
};





	// FUNCTIONS
	// NOTE: WHY YOU **DO NOT** WANT TO PUT FUNCTIONS HERE
// Generally speaking, you want to make sure SIMD math functions
// are inlined, because that gives the compiler much more latitude
// in instruction scheduling. It's not that the overhead of calling
// the function is particularly great; rather, many of the SIMD 
// opcodes have long latencies, and if you have a sequence of 
// several dependent ones inside a function call, the latencies 
// stack up to create a big penalty. If the function is inlined,
// the compiler can interleave its operations with ones from the
// caller to better hide those latencies. Finally, on the 360,
// putting parameters or return values on the stack, and then 
// reading them back within the next forty cycles, is a very 
// severe penalty. So, as much as possible, you want to leave your
// data on the registers.

// That said, there are certain occasions where it is appropriate
// to call into functions -- particularly for very large blocks
// of code that will spill most of the registers anyway. Unless your
// function is more than one screen long, yours is probably not one
// of those occasions.



// Transform many (horizontal) points in-place by a 3x4 matrix,
// here already loaded onto three fltx4 registers but not transposed. 
// The points must be stored as 16-byte aligned. They are points
// and not vectors because we assume the w-component to be 1. 




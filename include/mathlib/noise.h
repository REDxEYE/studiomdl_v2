//========= Copyright � 1996-2006, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#pragma once

#include "mathlib/vector.h"



// The following code is the c-ification of Ken Perlin's new noise algorithm
// "JAVA REFERENCE IMPLEMENTATION OF IMPROVED NOISE - COPYRIGHT 2002 KEN PERLIN"
// as available here: http://mrl.nyu.edu/~perlin/noise/
// it generates a single octave of noise in the -1..1 range
// this should at some point probably replace SparseConvolutionNoise - jd
float ImprovedPerlinNoise( Vector const &pnt );


//====== Copyright � 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#ifndef AMALGAMATEDTEXTURE_VARS_H
#define AMALGAMATEDTEXTURE_VARS_H
#ifdef _WIN32
#pragma once
#endif

#include "filesystem.h"
#include "tier0/dbg.h"


#define MAX_IMAGES_PER_FRAME 4

enum PackingMode_t
{
	PCKM_INVALID,
	PCKM_FLAT,			// Default mode - every frame consumes entire RGBA space
	PCKM_RGB_A,			// Some sequences consume RGB space and some Alpha space
};


enum SeqMode_t
{
	SQM_RGBA,		// Sequence occupies entire RGBA space
	SQM_RGB,		// Sequence occupies only RGB space
	SQM_ALPHA,		// Sequence occupies only Alpha space
	SQM_ALPHA_INVALID,
};


#endif // AMALGAMATEDTEXTURE_VARS_H
//===== Copyright � 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A higher level link library for general use in the game and tools.
//
//===========================================================================//


#pragma once

#if defined( _WIN32 )
#pragma once
#endif

#include <cstdint>

#include "interfaces/interfaces.h"


//-----------------------------------------------------------------------------
// Call this to get the file system set up to stdio for utilities, etc:
//-----------------------------------------------------------------------------
void InitDefaultFileSystem(void);
void ShutdownDefaultFileSystem(void);




//-----------------------------------------------------------------------------
// Distance fade information
//-----------------------------------------------------------------------------
enum FadeMode_t
{
	// These map directly to cpu_level, and g_aFadeData contains settings for each given cpu_level (see videocfg.h CPULevel_t).
	// The exception is 'FADE_MODE_LEVEL', which refers to level-specific values in the map entity.
	FADE_MODE_NONE = 0,
	FADE_MODE_LOW,
	FADE_MODE_MED,
	FADE_MODE_HIGH,
	FADE_MODE_360,
	FADE_MODE_PS3,
	FADE_MODE_LEVEL,

	FADE_MODE_COUNT,
};

struct FadeData_t
{
	float	m_flPixelMin;		// Size (height in pixels) above which objects start to fade in
	float	m_flPixelMax;		// Size (height in pixels) above which objects are fully faded in
	float	m_flWidth;			// Reference screen res w.r.t which the above pixel values were chosen
	float	m_flFadeDistScale;	// Scale factor applied before entity distance-based fade is calculated
};

// see tier2.cpp for data!
extern FadeData_t g_aFadeData[FADE_MODE_COUNT];


//-----------------------------------------------------------------------------
// Used by the resource system for fast resource frame counter
//-----------------------------------------------------------------------------
extern uint32_t g_nResourceFrameCount;




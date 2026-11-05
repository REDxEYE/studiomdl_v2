//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme version of a hitbox
//
//===========================================================================//

#pragma once

#ifdef _WIN32
#pragma once
#endif


// Valve includes
#include "mdlobjects/dmebbox.h"


//-----------------------------------------------------------------------------
// A class representing an attachment point
//-----------------------------------------------------------------------------
class CDmeHitbox : public CDmeBBox
{
	DEFINE_ELEMENT( CDmeHitbox, CDmeBBox );

public:
	CDmaString m_sSurfaceProperty;
	CDmaVar< int > m_nGroupId;
	CDmaString m_sBoneName;
	CDmaColor m_cRenderColor;	// used for visualization

};



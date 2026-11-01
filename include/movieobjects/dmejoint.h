//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme version of a joint of a skeletal model (gets compiled into a MDL)
//
//===========================================================================//

#pragma once

#ifdef _WIN32
#pragma once
#endif


// Valve includes
#include "movieobjects/dmedag.h"


// Forward declarations
class CDmeDrawSettings;


//-----------------------------------------------------------------------------
// A class representing a skeletal model
//-----------------------------------------------------------------------------
class CDmeJoint : public CDmeDag
{
	DEFINE_ELEMENT( CDmeJoint, CDmeDag );
};



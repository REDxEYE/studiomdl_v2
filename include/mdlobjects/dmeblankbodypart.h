//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =====//
//
// A blank body part
//
//===========================================================================//

#pragma once


#ifdef _WIN32
#pragma once
#endif


#include "mdlobjects/dmebodypart.h"


//-----------------------------------------------------------------------------
// A blank body part
//-----------------------------------------------------------------------------
class CDmeBlankBodyPart : public CDmeBodyPart
{
	DEFINE_ELEMENT( CDmeBlankBodyPart, CDmeBodyPart );
};



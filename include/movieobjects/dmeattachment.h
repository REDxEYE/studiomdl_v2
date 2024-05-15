//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme version of a model attachment point
//
//===========================================================================//

#ifndef DMEATTACHMENT_H
#define DMEATTACHMENT_H

#ifdef _WIN32
#pragma once
#endif


// Valve includes
#include "movieobjects/dmeshape.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/materialsystemutil.h"


//-----------------------------------------------------------------------------
// Forward Declarations
//-----------------------------------------------------------------------------
class CDmeDrawSettings;


//-----------------------------------------------------------------------------
// A class representing an attachment point
//-----------------------------------------------------------------------------
class CDmeAttachment : public CDmeShape
{
	DEFINE_ELEMENT( CDmeAttachment, CDmeShape );

public:
	CDmaVar< bool > m_bIsRigid;	// Does the attachment animate?
	CDmaVar< bool > m_bIsWorldAligned;	// Is the attachment world-aligned?

};


#endif // DMEATTACHMENT_H
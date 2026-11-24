//===== Copyright (c) 1996-2009, Valve Corporation, All rights reserved. ====
//
// Element group
//
//===========================================================================


#pragma once

// Valve includes
#include "datamodel/dmelement.h"
#include "datamodel/dmattributevar.h"


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CDmeElementGroup : public CDmElement
{
	DEFINE_ELEMENT( CDmeElementGroup, CDmElement );

public:

	CDmaElementArray< CDmeElementGroup > m_eElementList;

};



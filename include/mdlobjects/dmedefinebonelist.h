//====== Copyright © 1996-2008, Valve Corporation, All rights reserved. =====//
//
// A list of DmeSequences's
//
//===========================================================================//


#pragma once

#include "datamodel/dmattributevar.h"
#include "mdlobjects/dmemdllist.h"


//-----------------------------------------------------------------------------
// Forward Declarations
//-----------------------------------------------------------------------------
class CDmeDefineBone;


//-----------------------------------------------------------------------------
// A class representing a list of sequences
//-----------------------------------------------------------------------------
class CDmeDefineBoneList : public CDmeMdlList
{
	DEFINE_ELEMENT( CDmeDefineBoneList, CDmeMdlList );

public:
    CDmAttribute *GetListAttr() override { return m_DefineBones.GetAttribute(); }

	CDmaElementArray< CDmeDefineBone > m_DefineBones;

};



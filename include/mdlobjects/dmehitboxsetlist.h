//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme version of a hitbox set
//
//===========================================================================//

#pragma once


#ifdef _WIN32
#pragma once
#endif


#include "datamodel/dmattributevar.h"
#include "mdlobjects/dmemdllist.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeHitboxSet;


//-----------------------------------------------------------------------------
// A class representing an attachment point
//-----------------------------------------------------------------------------
class CDmeHitboxSetList : public CDmeMdlList
{
	DEFINE_ELEMENT( CDmeHitboxSetList, CDmeMdlList );

public:
	virtual CDmAttribute *GetListAttr() { return m_HitboxSetList.GetAttribute(); }

	CDmaElementArray< CDmeHitboxSet > m_HitboxSetList;

};



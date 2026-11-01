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
class CDmeHitbox;


//-----------------------------------------------------------------------------
// A class representing an attachment point
//-----------------------------------------------------------------------------
class CDmeHitboxSet : public CDmeMdlList
{
	DEFINE_ELEMENT( CDmeHitboxSet, CDmeMdlList );

public:
	virtual CDmAttribute *GetListAttr() { return m_HitboxList.GetAttribute(); }

	CDmaElementArray< CDmeHitbox > m_HitboxList;	

};



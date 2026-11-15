//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Snapshot of 
//
//===========================================================================//

#pragma once

#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "movieobjects/dmeclip.h" // GetAnimation() below converts to CDmeChannelsClip *


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeChannelsClip;


//-----------------------------------------------------------------------------
// A class representing a list of animations
//-----------------------------------------------------------------------------
class CDmeAnimationList : public CDmElement
{
	DEFINE_ELEMENT( CDmeAnimationList, CDmElement );

public:
	int GetAnimationCount() const;
	CDmeChannelsClip *GetAnimation( int nIndex );

private:
	CDmaElementArray<CDmeChannelsClip> m_Animations;
};


//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
inline int CDmeAnimationList::GetAnimationCount() const
{
	return m_Animations.size();
}

inline CDmeChannelsClip *CDmeAnimationList::GetAnimation( int nIndex )
{
	return m_Animations[nIndex];
}



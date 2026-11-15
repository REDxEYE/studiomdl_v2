//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// An element that contains a list of animations
//
//=============================================================================
#include "movieobjects/dmeanimationlist.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmeclip.h"




//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeAnimationList, CDmeAnimationList );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeAnimationList::OnConstruction()
{
	m_Animations.Init( this, "animations" );
}

void CDmeAnimationList::OnDestruction()
{
}





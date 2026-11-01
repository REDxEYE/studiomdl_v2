//===== Copyright (c) 1996-2009, Valve Corporation, All rights reserved. ====
//
// Dme Asset
//
//===========================================================================


// Valve includes
#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmeelementgroup.h"





//-----------------------------------------------------------------------------
// DmeIkChain
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeElementGroup, CDmeElementGroup );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeElementGroup::OnConstruction()
{
	m_eElementList.Init( this, "elementList" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeElementGroup::OnDestruction()
{
}
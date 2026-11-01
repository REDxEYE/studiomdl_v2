//====== Copyright � 1996-2008, Valve Corporation, All rights reserved. =====//
//
// DmeMaterialGroup
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmematerialgroup.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeMaterialGroup, CDmeMaterialGroup );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMaterialGroup::OnConstruction()
{
	m_MaterialList.Init( this, "materialList" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMaterialGroup::OnDestruction()
{
}

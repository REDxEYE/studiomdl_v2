//====== Copyright � 1996-2008, Valve Corporation, All rights reserved. =====//
//
// A list of DmeBoneWeight elements, replacing QC's $WeightList
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmebonemask.h"
#include "mdlobjects/dmebonemasklist.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeBoneMaskList, CDmeBoneMaskList );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBoneMaskList::OnConstruction()
{
	m_eDefaultBoneMask.Init( this, "defaultBoneMask" );
	m_BoneMaskList.Init( this, "boneMaskList" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBoneMaskList::OnDestruction()
{
}
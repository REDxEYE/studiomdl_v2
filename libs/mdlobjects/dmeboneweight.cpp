//====== Copyright � 1996-2008, Valve Corporation, All rights reserved. =====//
//
// Dme version of a bone weight as in QC $WeightList
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmeboneweight.h"


// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeBoneWeight, CDmeBoneWeight );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBoneWeight::OnConstruction()
{
	m_flWeight.Init( this, "weight" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBoneWeight::OnDestruction()
{
}
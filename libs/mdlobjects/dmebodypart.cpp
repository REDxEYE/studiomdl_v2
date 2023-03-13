//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme body part base class
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmebodypart.h"


// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeBodyPart, CDmeBodyPart );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBodyPart::OnConstruction()
{
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBodyPart::OnDestruction()
{
}
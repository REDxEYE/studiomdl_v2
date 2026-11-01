//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme blank/empty body part base class
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmeblankbodypart.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeBlankBodyPart, CDmeBlankBodyPart );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBlankBodyPart::OnConstruction()
{
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBlankBodyPart::OnDestruction()
{
}
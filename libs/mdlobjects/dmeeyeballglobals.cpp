//====== Copyright � 1996-2009, Valve Corporation, All rights reserved. =====//
//
// A list of DmeEyeballs's
//
//===========================================================================//


// Valve includes
#include "mdlobjects/dmeeyeballglobals.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmedag.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeEyeballGlobals, CDmeEyeballGlobals );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeEyeballGlobals::OnConstruction()
{
	m_vEyePosition.Init( this, "eyePosition" );
	m_flMaxEyeDeflection.InitAndSet( this, "maximumEyeDeflection", 90.0f );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeEyeballGlobals::OnDestruction()
{
}
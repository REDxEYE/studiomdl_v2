//============ Copyright (c) Valve Corporation, All rights reserved. ==========
//
//=============================================================================



// Valve includes
#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmemouth.h"
#include "movieobjects/dmedag.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeMouth, CDmeMouth );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMouth::OnConstruction()
{
	m_nMouthNumber.Init( this, "mouthNumber" );
	m_sFlexControllerName.Init( this, "flexControllerName" );
	m_sBoneName.Init( this, "boneName" );
	m_vForward.Init( this, "forward" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMouth::OnDestruction()
{
}

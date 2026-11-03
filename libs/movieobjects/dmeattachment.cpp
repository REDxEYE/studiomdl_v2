//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Dme version of a model attachment point
//
//=============================================================================
#include "movieobjects/dmeattachment.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "tier1/keyvalues.h"




//-----------------------------------------------------------------------------
// Statics
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeAttachment, CDmeAttachment );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeAttachment::OnConstruction()
{
	m_bIsRigid.Init( this, "isRigid" );
	m_bIsWorldAligned.Init( this, "isWorldAligned" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeAttachment::OnDestruction()
{
}

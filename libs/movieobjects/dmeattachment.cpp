//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Dme version of a model attachment point
//
//=============================================================================
#include "movieobjects/dmeattachment.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "materialsystem/imesh.h"
#include "tier1/keyvalues.h"

// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


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

	if ( !g_pMaterialSystem )
		return;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeAttachment::OnDestruction()
{
}

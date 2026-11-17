//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmetimeframe.h"
#include "datamodel/dmelementfactoryhelper.h"




//-----------------------------------------------------------------------------
// Class factory 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeTimeFrame, CDmeTimeFrame );


//-----------------------------------------------------------------------------
// Constructor, destructor 
//-----------------------------------------------------------------------------
void CDmeTimeFrame::OnConstruction()
{
	m_Start   .InitAndSet( this, "start", DMETIME_ZERO, FATTRIB_HAS_CALLBACK );
	m_Duration.InitAndSet( this, "duration", DMETIME_ZERO, FATTRIB_HAS_CALLBACK );
	m_Offset  .InitAndSet( this, "offset", DMETIME_ZERO);
	m_Scale   .InitAndSet( this, "scale",    1.0f );
}

void CDmeTimeFrame::OnDestruction()
{
}


void CDmeTimeFrame::OnAttributeChanged( CDmAttribute *pAttribute )
{
	BaseClass::OnAttributeChanged( pAttribute );

	// notify parent clip that the time has changed
	if ( pAttribute == m_Start.GetAttribute() || pAttribute == m_Duration.GetAttribute() )
	{
		InvokeOnAttributeChangedOnReferrers( GetHandle(), pAttribute );
	}
}


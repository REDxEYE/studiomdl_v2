//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmeshape.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmedag.h"




//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeShape, CDmeShape );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeShape::OnConstruction()
{
	m_visible.InitAndSet( this, "visible", true );
}

void CDmeShape::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// The default bounding sphere is empty at the origin
//-----------------------------------------------------------------------------
void CDmeShape::GetBoundingSphere( Vector &c, float &r ) const
{
	c.Zero();
	r = 0.0f;
}




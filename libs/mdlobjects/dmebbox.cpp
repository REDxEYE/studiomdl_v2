//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme version of an axis aligned bounding box
//
//===========================================================================//


// Valve includes
#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmebbox.h"
#include "mathlib/mathlib.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeBBox, CDmeBBox );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeBBox::OnConstruction()
{
	Vector vMin;
	Vector vMax;

	ClearBounds( vMin, vMax );

	m_vMinBounds.InitAndSet( this, "minBounds", vMin );
	m_vMaxBounds.InitAndSet( this, "maxBounds", vMax );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBBox::OnDestruction()
{
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeBBox::Clear()
{
	Vector vMin;
	Vector vMax;

	ClearBounds( vMin, vMax );

	m_vMinBounds = vMin;
	m_vMaxBounds = vMax;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeBBox::Empty() const
{
	const Vector &vMin = m_vMinBounds.Get();
	const Vector &vMax = m_vMaxBounds.Get();

	return vMin.x > vMax.x || vMin.y > vMax.y || vMin.z > vMax.z;
}






//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeBBox::Intersects( const CDmeBBox &bbox ) const
{
	return QuickBoxIntersectTest( m_vMinBounds, m_vMaxBounds, bbox.m_vMinBounds, bbox.m_vMaxBounds );
}






//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
const Vector &CDmeBBox::Min() const
{
	return m_vMinBounds.Get();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
const Vector &CDmeBBox::Max() const
{
	return m_vMaxBounds.Get();
}

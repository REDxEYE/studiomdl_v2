//============ Copyright (c) Valve Corporation, All rights reserved. ==========
//
//=============================================================================


#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmemodel.h"


#include "movieobjects/dmeaxissystem.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeAxisSystem, CDmeAxisSystem );






//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeAxisSystem::OnConstruction()
{
	// Initialize to Maya Y Up
	m_nUpAxis.InitAndSet( this, "upAxis", AS_AXIS_Y );
	m_nForwardParity.InitAndSet( this, "forwardParity", AS_PARITY_ODD );
	m_nCoordSys.InitAndSet( this, "coordSys", AS_RIGHT_HANDED );

	Assert( IsValid() );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeAxisSystem::OnDestruction()
{
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeAxisSystem::Init( Axis_t eUpAxis, ForwardParity_t eForwardParity, CoordSys_t eCoordSys /*= AS_RIGHT_HANDED */ )
{
	if ( !IsValid( eUpAxis, eForwardParity, eCoordSys ) )
	{
		AssertMsg( false, "Invalid Initialization of CDmeAxisSystem" );
		return false;
	}

	m_nUpAxis = eUpAxis;
	m_nForwardParity = eForwardParity;
	m_nCoordSys = eCoordSys;

	return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeAxisSystem::Init( PredefinedAxisSystem ePredefinedAxisSystem )
{
	Axis_t eUpAxis = static_cast< Axis_t >( m_nUpAxis.Get() );
	ForwardParity_t eForwardParity = static_cast< ForwardParity_t >( m_nForwardParity.Get() );
	CoordSys_t eCoordSys = static_cast< CoordSys_t >( m_nCoordSys.Get() );

	if ( !GetPredefinedAxisSystem( eUpAxis, eForwardParity, eCoordSys, ePredefinedAxisSystem ) )
		return false;

	return Init( eUpAxis, eForwardParity, eCoordSys );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeAxisSystem::IsValid() const
{
	return IsValid(
		static_cast< Axis_t >( m_nUpAxis.Get() ),
		static_cast< ForwardParity_t >( m_nForwardParity.Get() ),
		static_cast< CoordSys_t >( m_nCoordSys.Get() ) );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeAxisSystem::IsValid( Axis_t nUpAxis, ForwardParity_t eForwardParity, CoordSys_t eCoordSys )
{
	if ( nUpAxis == 0 || nUpAxis < AS_AXIS_NZ || nUpAxis > AS_AXIS_Z )
		return false;

	if ( eForwardParity == 0 || eForwardParity < AS_PARITY_NODD || eForwardParity > AS_PARITY_ODD )
		return false;

	if ( eCoordSys < AS_RIGHT_HANDED || eCoordSys > AS_LEFT_HANDED )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeAxisSystem::GetPredefinedAxisSystem(
	Axis_t &eUpAxis,
	ForwardParity_t &eForwardParity,
	CoordSys_t &eCoordSys,
	PredefinedAxisSystem ePredefinedAxisSystem )
{
	if ( ePredefinedAxisSystem < AS_VALVE_ENGINE || ePredefinedAxisSystem > AS_3DSMAX )
		return false;

	static int predefinedAxisSystemList[][3] = {
		{  AS_AXIS_Z,	 AS_PARITY_EVEN,	AS_RIGHT_HANDED },	// AS_VALVE_ENGINE	+Z, +X
		{  AS_AXIS_Z,	-AS_PARITY_ODD,		AS_RIGHT_HANDED },	// AS_SMD			+Z, -Y
		{  AS_AXIS_Y,	 AS_PARITY_ODD,		AS_RIGHT_HANDED },	// AS_MAYA_YUP		+Y, +Z
		{  AS_AXIS_Z,	-AS_PARITY_ODD,		AS_RIGHT_HANDED },	// AS_MAYA_ZUP		+Z, -Y
		{  AS_AXIS_Y,	 AS_PARITY_ODD,		AS_RIGHT_HANDED },	// AS_MODO			+Y, +Z
		{  AS_AXIS_Z,	-AS_PARITY_ODD,		AS_RIGHT_HANDED }	// AS_3DSMAX		+Z, -Y
	};

	COMPILE_TIME_ASSERT( AS_VALVE_ENGINE == 0 );
	COMPILE_TIME_ASSERT( AS_SMD == 1 );
	COMPILE_TIME_ASSERT( AS_MAYA_YUP == 2 );
	COMPILE_TIME_ASSERT( AS_MAYA_ZUP == 3 );
	COMPILE_TIME_ASSERT( AS_MODO_YUP == 4 );
	COMPILE_TIME_ASSERT( AS_3DSMAX == 5 );
	COMPILE_TIME_ASSERT( AS_3DSMAX + 1 == ARRAYSIZE( predefinedAxisSystemList ) );

	eUpAxis = static_cast< Axis_t >( predefinedAxisSystemList[ePredefinedAxisSystem][0] );
	eForwardParity = static_cast< ForwardParity_t >( predefinedAxisSystemList[ePredefinedAxisSystem][1] );
	eCoordSys = static_cast< CoordSys_t >( predefinedAxisSystemList[ePredefinedAxisSystem][2] );

	Assert( IsValid( eUpAxis, eForwardParity, eCoordSys ) );

	return true;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeAxisSystem::IsEqual( PredefinedAxisSystem ePredefinedAxisSystem ) const
{
	Axis_t eAUpAxis;
	ForwardParity_t eAForwardParity;
	CoordSys_t eACoordSys;

	if ( !GetPredefinedAxisSystem( eAUpAxis, eAForwardParity, eACoordSys, ePredefinedAxisSystem ) )
		return false;

	const Axis_t eBUpAxis = GetUpAxis();
	const ForwardParity_t eBForwardParity = GetForwardParity();
	const CoordSys_t eBCoordSys = GetCoordSys();

	return ( eBUpAxis == eAUpAxis ) && ( eBForwardParity == eAForwardParity ) && ( eBCoordSys == eACoordSys );

}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
CDmeAxisSystem::Axis_t CDmeAxisSystem::GetUpAxis() const
{
	return static_cast< Axis_t >( m_nUpAxis.Get() );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
CDmeAxisSystem::ForwardParity_t CDmeAxisSystem::GetForwardParity() const
{
	return static_cast< ForwardParity_t >( m_nForwardParity.Get() );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
CDmeAxisSystem::CoordSys_t CDmeAxisSystem::GetCoordSys() const
{
	return static_cast< CoordSys_t >( m_nCoordSys.Get() );
}












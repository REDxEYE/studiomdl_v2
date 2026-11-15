//============ Copyright (c) Valve Corporation, All rights reserved. ==========
//
// This is patterned after FbxAxisSystem
//
//=============================================================================


#pragma once


#include "datamodel/dmelement.h"


//=============================================================================
//
// CDmeAxisSystem, an unambiguous definition of the axis system.  Previously
// the terms Y Up and Z Up got thrown around a lot but they do not unambiguously
// specify a coordinate system.
//
// To define an axis system both the up axis and the parity of the axis system
// need to be specified.  The parity is not an independent variable, it depends
// on the value of the up axis specified.  Whichever axis is specified as the
// up axis leaves two remaining axes and even implies the first remaining
// axis, in alphabetical order, is the Forward axis.
//
// The Valve engine coordinate system has traditionally been +Z Up and
// +X as the forward axis.  By specifying +Z as the up axis that leaves
// X & Y, since X is desired, that's even parity.  It would be defined
// in Axis, Parity as:
//
// pDmeAxisSystem->Init( CDmeAxisSystem::AS_AXIS_Z, CDmeAxisSystem::AS_PARITY_EVEN );
//
// By default, Maya's coordinate system is +Y Up with +Z forward. Taking
// +Y as up leaves X & Z, since Z is 2nd, that's odd parity.  i.e.
//
// pDmeAxisSystem->Init( CDmeAxisSystem::AS_AXIS_Y, CDmeAxisSystem::AS_PARITY_ODD );
//
//=============================================================================
class CDmeAxisSystem : public CDmElement
{
	DEFINE_ELEMENT( CDmeAxisSystem, CDmElement );

public:
	enum Axis_t
	{
		AS_AXIS_NZ = -3,
		AS_AXIS_NY = -2,
		AS_AXIS_NX = -1,
		AS_AXIS_X = 1,
		AS_AXIS_Y = 2,
		AS_AXIS_Z = 3
	};

	enum ForwardParity_t
	{
		AS_PARITY_NODD = -2,
		AS_PARITY_NEVEN = -1,
		AS_PARITY_EVEN = 1,
		AS_PARITY_ODD = 2
	};

	enum CoordSys_t
	{
		AS_RIGHT_HANDED = 0,
		AS_LEFT_HANDED = 1
	};

	enum PredefinedAxisSystem
	{
		AS_INVALID = -1,	// Invalid
		AS_VALVE_ENGINE,	// Up: +Z, Forward: +X
		AS_SMD,				// Up: +Z, Forward: -Y
		AS_MAYA_YUP,		// Up: +Y, Forward: +Z
		AS_MAYA_ZUP,		// Up: +Z, Forward: -Y
		AS_MODO_YUP,		// Up: +Y, Forward: +Z
		AS_3DSMAX			// Up: +Z, Forward: -Y
	};

	bool Init( Axis_t eUpAxis, ForwardParity_t eForwardParity, CoordSys_t eCoordSys = AS_RIGHT_HANDED );
	bool Init( PredefinedAxisSystem nPredefinedAxisSystem );
	
	// Returns true if the specified eUpAxis, eForward and eCoordSys are all valid values together
	static bool IsValid( Axis_t eUpAxis, ForwardParity_t eForwardAxis, CoordSys_t eCoordSys = AS_RIGHT_HANDED );
	bool IsValid() const;

	// Returns the upAxis, forwardParity & coordSys for the specified predefined axisSystem
	static bool GetPredefinedAxisSystem( Axis_t &eUpAxis, ForwardParity_t &eForwardParity, CoordSys_t &eCoordSys, PredefinedAxisSystem ePredefinedAxisSystem );

	// Specifies whether this CDmeAxisSystem is the same as the predefined axis system specified
	bool IsEqual( PredefinedAxisSystem ePredefinedAxisSystem ) const;

	// Returns one of [ AS_AXIS_NX, AS_AXIS_NY, AS_AXIS_NZ, AS_AXIS_X, AS_AXIS_Y, AS_AXIS_Z ] from m_nUpAxis
	Axis_t GetUpAxis() const;

	// Returns one of [ AS_PARITY_NEVEN, AS_PARITY_NODD, AS_PARITY_EVEN, AS_PARITY_ODD ] from m_nForwardParity
	ForwardParity_t GetForwardParity() const;

	// Returns one of [ AS_LEFT_HANDED, AS_RIGHT_HANDED ] from m_nCoordSys
	CoordSys_t GetCoordSys() const;

protected:

	CDmaVar< int > m_nUpAxis;			// [ +/- AS_AXIS_X, +/- AS_AXIS_Y, +/- AS_AXIS_Z ]
	CDmaVar< int > m_nForwardParity;	// [ +/- AS_PARITY_EVEN, +/- AS_PARITY_ODD ]
	CDmaVar< int > m_nCoordSys;			// [ AS_RIGHT_HANDED, AS_LEFT_HANDED ]

};



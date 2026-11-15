//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Dme version of a skeletal model (gets compiled into a MDL)
//
//===========================================================================//

#pragma once



#include "tier1/utlstack.h"
#include "movieobjects/dmejoint.h"
#include "movieobjects/dmetransformlist.h"
#include "movieobjects/dmeaxissystem.h"


class CDmeMesh;

//-----------------------------------------------------------------------------
// A class representing a skeletal model
//-----------------------------------------------------------------------------
class CDmeModel : public CDmeDag
{
	DEFINE_ELEMENT( CDmeModel, CDmeDag );

public:
	// Add joint

	// Returns the number of joint transforms we know about
	int GetJointCount() const;

	// Returns the DmeDag for the specified joint index

	// Returns the DmeTransform for the specified joint index
	CDmeTransform *GetJointTransform( int nIndex );

	// Determines joint index of a given a joint
	int GetJointIndex( CDmeDag *pJoint ) const;

	// Finds a base state by name, returns NULL if not found
	CDmeTransformList *FindBaseState( const char *pBaseStateName );

	// NOTE: See comment for m_upAxis attribute below

	// Returns true if the DmeModel is Z Up.
	bool IsZUp() const;

	// Sets the DmeAxisSystem, no reorientation is done, see ConvertToAxisSystem

	// Sets the DmeAxisSystem and reorients all of the data under the DmeModel
	// as necessary to change from the current DmeAxisSystem to the specified DmeAxisSystem
	// Currently private, will be made public soon, upAxis is still authoritative

	// TransformScene
	void TransformScene( const Vector &vScale = Vector( 1.0f, 1.0f, 1.0f ), const Vector &vTranslate = vec3_origin, const DegreeEuler &eRotation = DegreeEuler( 0.0f, 0.0f, 0.0f ), float flEps = 1.0e-4 );

private:

	// Removes all children from this joint, moving shapes to be 

	// Helper functions for ReorientToEngineSpace() and ReorientToDCCToolSpace()

protected:
	// The order in which the joint transform names appear in this list
	// indicates the joint index for each dag
	CDmaElementArray< CDmeDag > m_JointList;

	// Stores a list of base poses for all the joint transforms
	CDmaElementArray<CDmeTransformList> m_BaseStates;

	// Stores the up axis of the model
	// One of Y or Z
	//
	// NOTE: Y up is Maya's typical coordinate system.  Setting the up axis to Z
	//       won't quite give you the engine's coordinate system (even though it
	//       has an Up Axis of Z as well).  The difference is the forward axis.
	//
	//       A DmeModel with m_UpAxis = Y gives the data in a coordinate system
	//       that is Maya's typical coordinate system.  Y Up, Z Foward, X Right.
	//
	//       A DmeModel with m_UpAxis = Z gives the data in a coordinate system
	//       that is compatible with typical SMD data.  This is data that can
	//       be compiled in studiomdl without any special options.
	//
	//       Neither Y up or Z up is engine space.  Z up is closer but still
	//       requires another 90 degree rotation to align things.  This can be
	//       changed in the future if required but Z up was added for legacy
	//       SMD compatibility.
	//
	//       In this table, missing entries mean it's not required or isn't
	//       supported
	//
	// m_UpAxis Desc          Up      Forward Right   studiomdl
	// -------- ------------- ------- ------- ------- ---------
	//    Y     Maya Y Up        Y       Z       X    $upaxis Y
	//    Z     Maya Z Up/XSI    Z      -Y       X
	//          Engine           Z       X       Y    $origin 0 0 0 0
	//-----------------------------------------------------------------------------
	CDmaString m_UpAxis;
	
	CDmaElement< CDmeAxisSystem > m_eAxisSystem;

	CDmaElement< CDmeModel > m_eModel;
	CDmaElement< CDmeModel > m_eSkeleton;

private:
	enum SetupBoneRetval_t
	{
		NO_SKIN_DATA = 0,
		TOO_MANY_BONES,
		BONES_SET_UP
	};

	// Sets up the render state for the model

	// Stack of DmeModels currently being rendered. Used to set up render state

	friend class CDmeMesh;
	friend bool SetMeshFromSkeleton( CDmeMesh *pDmeMesh );	// dmx edit function - to do skin weights

};



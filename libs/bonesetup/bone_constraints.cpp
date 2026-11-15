//============ Copyright (c) Valve Corporation, All rights reserved. ============
//
// Routines used for various prodcedural bones but meant to be called from
// datamodel or maya as well
//
// In a separate source file so linking bonesetup.lib doesn't get more than
// needed
//
//===============================================================================


// Valve includes
#include "mathlib/mathlib.h"
#include "mathlib/vector.h"
#include "tier1/strtools.h"
#include "studiomdl/bone_setup.h"
#include "bone_constraints.h"
#include "bone_accessor.h"
#include "studio.h"


//=============================================================================
//=============================================================================
//
// CConstraintBones
//
//=============================================================================
//=============================================================================






//-----------------------------------------------------------------------------
// Calculate the orientation needed to make a transform where the y 
// vector of the transform matches the forward vector and the z vector matches
// the up reference vector as closely as possible. The x vector will be in the 
// plane defined by using the forward vector as the normal. 
//-----------------------------------------------------------------------------
void CConstraintBones::ComputeAimConstraintAimAt(
	Quaternion &qAim,
	const Vector &vForward,
	const Vector &vReferenceUp )
{ 
	Vector vFwd = vForward;
	vFwd.NormalizeInPlace();
	const float flRatio = DotProduct( vFwd, vReferenceUp );
	Vector vUp = vReferenceUp - ( vFwd * flRatio );
	vUp.NormalizeInPlace();

	Vector vRight = vFwd.Cross( vUp );
	vRight.NormalizeInPlace();

	const Vector &vX = vRight;
	const Vector &vY = vFwd;
	const Vector &vZ = vUp; 

	const float flTr = vX.x + vY.y + vZ.z; 
	qAim.Init( vY.z - vZ.y , vZ.x - vX.z, vX.y - vY.x, flTr + 1.0f ); 
	const float flRadius = qAim[0] * qAim[0] + qAim[1] * qAim[1] + qAim[2] * qAim[2] + qAim[3] * qAim[3];
	if ( flRadius > FLT_EPSILON )
	{
		QuaternionNormalize( qAim ); 
	}
	else
	{
		matrix3x4_t mRot;
		MatrixSetColumn( vX, 0, mRot );
		MatrixSetColumn( vY, 1, mRot );
		MatrixSetColumn( vZ, 2, mRot );
		MatrixQuaternion( mRot, qAim );
	}
}


//-----------------------------------------------------------------------------
// Given the various parameters, computes the local vForward & vReferenceUp
// and calls ComputeAimConstraintAimAt
//-----------------------------------------------------------------------------
void CConstraintBones::ComputeAimConstraint(
	Quaternion &qAim,
	const Vector &vTargetWorldPos,
	const matrix3x4_t &mParentToWorld,
	const Vector &vUp,
	const Vector &vSlaveLocalPos,
	const matrix3x4_t *pmUpToWorld,
	AimConstraintUpType_t eUpType )
{
	matrix3x4_t mWorldToParent;
	MatrixInvert( mParentToWorld, mWorldToParent );

	// If the up vector is in world space, convert it into local space
	Vector vWorldUp;

	ComputeWorldUpVector( &vWorldUp, mParentToWorld, vUp, vSlaveLocalPos, pmUpToWorld, eUpType );

	Vector vLocalUp;
	VectorRotate( vWorldUp, mWorldToParent, vLocalUp );

	// Convert the target's world space position into the local space of the slave.
	Vector vTargetLocalPos;
	VectorTransform( vTargetWorldPos, mWorldToParent, vTargetLocalPos );

	// Compute the local space forward vector
	Vector vLocalForward = vTargetLocalPos - vSlaveLocalPos;
	vLocalForward.NormalizeInPlace();

	// Compute the orientation 
	CConstraintBones::ComputeAimConstraintAimAt( qAim, vLocalForward, vLocalUp );	
	RadianEuler e(qAim);
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CConstraintBones::ComputeWorldUpVector(
	Vector *pvWorldUp,
	const matrix3x4_t & mParentToWorld,
	const Vector &vUp,
	const Vector &vSlaveLocalPos,
	const matrix3x4_t *pmUpToWorld,
	AimConstraintUpType_t eUpType )
{
	switch ( eUpType )
	{
	case AC_UP_TYPE_VECTOR:
		VectorCopy( vUp, *pvWorldUp );
		break;
	case AC_UP_TYPE_OBJECT:
		if ( pmUpToWorld )
		{
			Vector vUpObjectWorldPos;
			MatrixPosition( *pmUpToWorld, vUpObjectWorldPos );
			Vector vSlaveWorldPos;
			VectorTransform( vSlaveLocalPos, mParentToWorld, vSlaveWorldPos );
			VectorSubtract( vUpObjectWorldPos, vSlaveWorldPos, *pvWorldUp );
			VectorNormalize( *pvWorldUp );
		}
		else
		{
			VectorCopy( vUp, *pvWorldUp );
		}
		break;
	case AC_UP_TYPE_PARENT_ROTATION:
		VectorRotate( vUp, mParentToWorld, *pvWorldUp );
		break;
	default:
	case AC_UP_TYPE_OBJECT_ROTATION:
		if ( pmUpToWorld )
		{
			VectorRotate( vUp, *pmUpToWorld, *pvWorldUp );
		}
		else
		{
			VectorCopy( vUp, *pvWorldUp );
		}
		break;
	}
}


//=============================================================================
//=============================================================================
//
// CStudioConstraintBones
//
//=============================================================================
//=============================================================================



//=============================================================================
// CStudioConstraintBones : Studio Interface Functions
//=============================================================================










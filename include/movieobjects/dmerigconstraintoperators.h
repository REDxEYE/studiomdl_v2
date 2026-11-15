//====== Copyright © 1996-2008, Valve Corporation, All rights reserved. =======
//
// Purpose:  Declaration of the constraint operator classes. The constraint 
// operators control the position and / or orientation of a specified slave dag
// node based on the position and orientation of a set of weighted target dag 
// nodes. The relationship between the target dag node and the slave is
// determined by the type of the constraint. The Operate() function of each 
// constraint class is responsible for enforcing its specific relationship.
//
//=============================================================================

#pragma once


#include "movieobjects/dmedag.h"
#include "movieobjects/dmeoperator.h"

// Forward declarations
class CDmAttribute;
class CDmeChannel;
class CDmeRigBaseConstraintOperator;

//-----------------------------------------------------------------------------
// Different rig operator modes (directions)
//-----------------------------------------------------------------------------
enum RigOperatorMode_t
{
	RM_FORWARD = 0, // Data flows from handles to drive slaves
	RM_REVERSE,		// Slave transforms drive handle positions
};

//-----------------------------------------------------------------------------
// Constraint attribute attachment type, specifies which channels the 
// constraint will apply to.
//-----------------------------------------------------------------------------
enum EAddType
{
	AA_TYPE_UNKNOWN				= 0,
	AA_TYPE_POSITION			= (1<<0),
	AA_TYPE_ORIENTATION			= (1<<1),
	AA_TYPE_ALL = AA_TYPE_POSITION | AA_TYPE_ORIENTATION,
};


//-----------------------------------------------------------------------------
// General constraint types
//-----------------------------------------------------------------------------
enum EConstraintType
{
	CT_UNKNOWN = 0,
	CT_POINT,				// Point constraint, slave position matches target position
	CT_ORIENT,				// Orient constraint, slave orientation matches target orientation
	CT_AIM,					// Aim constraint, slave orientation is adjusted to look point toward target position
	CT_IK,					// Inverse kinematics constraint, position and orientation are adjusted to reach a target
	CT_PARENT,				// Parent constraint, slave position and orientation are updated to behave as children of the target
	CT_ROTATION,			// Rotation constraint, slave orientation is set to the orientation generated from a set of rotations
	CT_TWIST,				// Twist constraint, takes a percentage of the rotation around the axis between a child & parent bone
	NUM_CONSTRAINT_TYPES,
};


//-----------------------------------------------------------------------------
// CDmeConstraintTarget: Specifies a single target of a constraint and the 
// weight of that target in the final result, as well as a position and 
// orientation. 
//-----------------------------------------------------------------------------
class CDmeConstraintTarget : public CDmElement
{
	DEFINE_ELEMENT( CDmeConstraintTarget, CDmElement );
	friend class CDmeRigBaseConstraintOperator;

public:

	// Clear the position and rotation offset
	void ClearOffset();

	// Get the dag node that is the source of the target
	CDmeDag *GetDag() { return m_Handle; }

	// Get the weight value for the target
	float GetWeight() { return m_flWeight; }

	// Get the position offset of the target
	const Vector &GetPositionOfffset()	{ return m_vecOffset; }

	// Get the orientation offset of the target
	const Quaternion &GetOrientationOffset() { return m_qOffset; }

	// Get the weight attribute of the target
	CDmAttribute *GetWeightAttribute() { return m_flWeight.GetAttribute(); }

private:

	CDmaElement< CDmeDag >	m_Handle;
	CDmaVar< float >		m_flWeight;
	CDmaVar< Vector >		m_vecOffset;
	CDmaVar< Quaternion >	m_qOffset;
};


//-----------------------------------------------------------------------------
// CDmeConstraintSlave: A dag node which is being controlled by a constraint,
// also stores the original position and orientation of the dag which will be
// used if no weight is active.
//-----------------------------------------------------------------------------
class CDmeConstraintSlave : public CDmElement
{
	DEFINE_ELEMENT( CDmeConstraintSlave, CDmElement );
	friend class CDmeRigBaseConstraintOperator;

public:

	// Get the transform of the dag assigned to the slave
	CDmeTransform *GetTransform()							{ return m_Dag.GetElement() ? m_Dag->GetTransform() : nullptr; }

	// Set the dag node targeted by the slave
	void SetDag( CDmeDag *pDag )							{ m_Dag = pDag; }

	// Get the dag node targeted by the slave.
	CDmeDag *GetDag() const									{ return m_Dag; }
	
	// Set the local space base position of the slave
	void SetBasePosition( const Vector &positon )			{ m_BasePosition = positon; }

	// Get the local space base position of the slave
	const Vector &GetBasePosition() const					{ return m_BasePosition; }

	// Set the local space base orientation of the slave	
	void SetBaseOrientation( const Quaternion &orientation )	{ m_BaseOrientation = orientation; }

	// Get the local space base orientation of the slave	
	const Quaternion &GetBaseOrientation() const				{ return m_BaseOrientation; }

	// Get the attribute for the base position
	const CDmAttribute *GetBasePositionAttribute() const		{ return m_BasePosition.GetAttribute(); }

	// Get the attribute for the base orientation 
	const CDmAttribute *GetBaseOrientationAttribute() const		{ return m_BaseOrientation.GetAttribute(); }
	
	// Get the constraint to which the slave belongs.
	CDmeRigBaseConstraintOperator *GetConstraint() const;

	// Compute the base world space matrix 
	void GetBaseWorldTransform( matrix3x4_t &worldTransform ) const;

	// Compute the base position and orientation in world space
	void ComputeBaseWorldValues( Vector &wsPosition, Quaternion &wsOrientation ) const;

private:
	
	CDmaElement< CDmeDag >						 m_Dag;				// Dag node being controlled by the constraint
	CDmaVar< Vector >							 m_BasePosition;	// Position of the slave when no constraint target is active
	CDmaVar< Quaternion>						 m_BaseOrientation;	// Orientation of the slave when no constraint target is active
};


//-----------------------------------------------------------------------------
// CDmeRigBaseConstraintOperator: Base class from which all constraints are
// derived.
//-----------------------------------------------------------------------------
class CDmeRigBaseConstraintOperator : public CDmeOperator
{
	DEFINE_ELEMENT( CDmeRigBaseConstraintOperator, CDmeOperator );

public:

	// DERIVED CLASSES SHOULD OVERRIDE
	virtual EConstraintType GetConstraintType() const { Error( "Derived must implement" ); return CT_UNKNOWN; }
	virtual EAddType GetInputAttributeType() const { Error( "Derived must implement" ); return AA_TYPE_UNKNOWN; };
	virtual EAddType GetOutputAttributeType() const { Error( "Derived must implement" ); return AA_TYPE_UNKNOWN; };
    void Operate() override { Error( "Derived must implement" ); };

	// Determine if data has changed and the operator needs to be updated
	// FIXME:  Each op type should check extra fields
	virtual bool IsDirty() const { return true; } 
	
	// Perform any additional work which needs to be done after handles have been added
	virtual void PostHandlesAdded( bool bPreserveOffset ) {}
	
	// Determine if the the constraint has slave with the specified name
	virtual bool IsSlaveObject( char const *pchName ) const;

	// Get the attributes that the constraint reads data from, Inputs are CDmeDags (handles usually)
    void GetInputAttributes ( std::vector< CDmAttribute * > &attrs ) override;

	// Get the attributes to which the attribute writes, Outputs are CDmeDags (bones or other CDmeRigHandles usually)
    void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override;

	// Disconnect the channels driving the slave dag nodes from the dag transforms and connect them to the constraint
	virtual void DisconnectTransformChannels();

	// Reconnect the base channels of each slave directly to the dag
	virtual void ReconnectTransformChannels();

	// Find all of the operators the evaluation of the constraint operator is dependent on
    void GatherInputOperators( std::vector< CDmeOperator * > &operatorList ) override;

	// Get a pointer to the dag node the constraint is controlling
	virtual const CDmeDag *GetSlave() const;

	// Get the list of target handles used by the constraint
	const CDmaElementArray< CDmeConstraintTarget > &GetTargets() const { return m_Targets; }

	// Get the constraint slave
	const CDmeConstraintSlave* GetConstraintSlave() const { return m_Slave.GetElement(); }

protected:

	// Add the specified type of attributes from the provided transform
	static void AddAttribute( std::vector< CDmAttribute * > &attrs, enum EAddType type, class CDmeTransform *pTxForm );

	// Add the position and orientation attributes the entire ancestry of the dag node.
	static void AddAncestorAttributes( std::vector< CDmAttribute * > &attrs, CDmeDag *pDag );

	// Compute the offsets of the specified target based on the relative transforms of the target to the slave
	virtual void ComputeOffset( Vector &vOffset, Quaternion &qOffset, CDmeConstraintTarget *pTarget, bool bPreserveOffset );

	// Compute the aggregate target position from the weighted target list and return the total weight
	float ComputeTargetPosition( Vector &wsTargetPosition );

	// Compute the aggregate target orientation from the weighted target list and return the total weight
	float ComputeTargetOrientation( Quaternion &wsTargetOrientation );

	// Disconnect the channels driving the specified slave from the dag and connect them to the constraint
	void DisconnectSlaveChannels( CDmeConstraintSlave *pSlave, int attributeFlags );

	// Reconnect the transform channels associated with the specified slave directly to the dag
	void ReconnectSlaveChannels( CDmeConstraintSlave *pSlave, int attributeFlags );


protected:

	CDmaElementArray< CDmeConstraintTarget > m_Targets;	// Set of target dag nodes used to calculate position and orientation
	CDmaElement< CDmeConstraintSlave >		 m_Slave;	// Dag node who's position and/or orientation are controlled by the constraint
	CDmaVar< int >							 m_mode;	// Operating mode of the constraint, determines data flow direction

};



//-----------------------------------------------------------------------------
// CDmeRigPointConstraintOperator 
//-----------------------------------------------------------------------------
class CDmeRigPointConstraintOperator : public CDmeRigBaseConstraintOperator
{
public:

	DEFINE_ELEMENT( CDmeRigPointConstraintOperator, CDmeRigBaseConstraintOperator );

public:
    EConstraintType GetConstraintType() const override { return CT_POINT; }

    void Operate() override;

    EAddType GetInputAttributeType() const override { return (EAddType)( AA_TYPE_POSITION | AA_TYPE_ORIENTATION ); } // Requires position and orientation, orientation is needed for offset
    EAddType GetOutputAttributeType() const override { return AA_TYPE_POSITION; } // Only writes position of destination bone
};



//-----------------------------------------------------------------------------
// CDmeRigOrientConstraintOperator
//-----------------------------------------------------------------------------
class CDmeRigOrientConstraintOperator : public CDmeRigBaseConstraintOperator
{
public:

	DEFINE_ELEMENT( CDmeRigOrientConstraintOperator, CDmeRigBaseConstraintOperator );

public:
    EConstraintType GetConstraintType() const override { return CT_ORIENT; }

    void Operate() override;

    EAddType GetInputAttributeType() const override { return AA_TYPE_ORIENTATION; } // Uses just orientations of handles
    EAddType GetOutputAttributeType() const override { return AA_TYPE_ORIENTATION; } // Only writes orientation of destination bone
};


//-----------------------------------------------------------------------------
// CDmeRigAimConstraintOperator
//-----------------------------------------------------------------------------
class CDmeRigAimConstraintOperator : public CDmeRigBaseConstraintOperator
{
public:

	DEFINE_ELEMENT( CDmeRigAimConstraintOperator, CDmeRigBaseConstraintOperator );

public:
    EConstraintType GetConstraintType() const override { return CT_AIM; }

    void Operate() override;

    void GetInputAttributes( std::vector< CDmAttribute * > &attrs ) override;

    EAddType GetInputAttributeType() const override { return AA_TYPE_POSITION; } // Uses just positions of handles
    EAddType GetOutputAttributeType() const override { return AA_TYPE_ORIENTATION; } // Only writes angle of destination bone

    void PostHandlesAdded( bool bPreserveOffset ) override;

private:

	// Calculate the orientation needed to make a transform with the specified forward vector
	void AimAt( const Vector &vecForward, const Vector &referenceUp, Quaternion &q );

	// Calculate the orientation to apply to the slave to make it look at the target position.
	float CalculateOrientation( Quaternion &targetOrientation );

	CDmaVar< Quaternion >			m_AimOffset;
	CDmaVar< Vector >				m_UpVector;
	CDmaElement< const CDmeDag >	m_UpSpaceTarget;
	CDmaVar< int >					m_UpType;			// One of CConstraintBones::AimConstraintUpType_t
};


//-----------------------------------------------------------------------------
// CDmeRigRotationConstraintOperator -- An operator for controlling the 
// orientation of a dag node using a series axis angle rotations.
//-----------------------------------------------------------------------------
class CDmeRigRotationConstraintOperator : public CDmeRigBaseConstraintOperator
{

public:

	DEFINE_ELEMENT( CDmeRigRotationConstraintOperator, CDmeRigBaseConstraintOperator );

public:
    EConstraintType GetConstraintType() const override { return CT_ROTATION; }
    EAddType GetInputAttributeType() const override { return AA_TYPE_UNKNOWN; }		// Does not use target input values
    EAddType GetOutputAttributeType() const override { return AA_TYPE_ORIENTATION; } // Only writes orientation of destination dag

	// Run the operator, this calculates the quaternion orientation by concatenating the axis angle rotations.
    void Operate() override;

	// Get the list of input attributes the operation is dependent on this includes all of the axis values and rotations
    void GetInputAttributes( std::vector< CDmAttribute * > &attrs ) override;

private:

	CDmaArray< float >		m_Rotations;	// "rotation" : Rotation around the specified axis in degrees
	CDmaArray< Vector >		m_Axies;		// "axies"	  : Axis about which the rotation will occur
};


//-----------------------------------------------------------------------------
// CDmeRigParentConstraintOperator
//-----------------------------------------------------------------------------
class CDmeRigParentConstraintOperator : public CDmeRigBaseConstraintOperator
{
public:

	DEFINE_ELEMENT( CDmeRigParentConstraintOperator, CDmeRigBaseConstraintOperator );

public:
    EConstraintType GetConstraintType() const override { return CT_PARENT; }

    void Operate() override;

    EAddType GetInputAttributeType() const override { return (EAddType)( AA_TYPE_POSITION | AA_TYPE_ORIENTATION); } // Uses both position and orientations of handles
    EAddType GetOutputAttributeType() const override { return (EAddType)( AA_TYPE_POSITION | AA_TYPE_ORIENTATION); } // Writes full transform of destination object

private:

	// Compute the aggregate target position and orientation from the weighted target list and return the total weight
	float ComputeTargetPositionOrientation( Vector &wsTargetPos, Quaternion &wsTargetOrientation );

	// Compute the offsets of the specified target based on the relative transforms of the target to the slave
    void ComputeOffset( Vector &vOffset, Quaternion &qOffset, CDmeConstraintTarget *pTarget, bool bPreserveOffset ) override;

};


//-----------------------------------------------------------------------------
// CDmeRigIKConstraintOperator
//-----------------------------------------------------------------------------
class CDmeRigIKConstraintOperator : public CDmeRigBaseConstraintOperator
{
public:

	DEFINE_ELEMENT( CDmeRigIKConstraintOperator, CDmeRigBaseConstraintOperator );

public:
    EConstraintType GetConstraintType() const override { return CT_IK; }

    void Operate() override;

    EAddType GetInputAttributeType() const override { return AA_TYPE_POSITION; } // Uses just positions of handles
    EAddType GetOutputAttributeType() const override { Assert( 0 ); return AA_TYPE_UNKNOWN; } // Only writes angle of destination bone

	// Overridden
    void GetInputAttributes( std::vector< CDmAttribute * > &attrs ) override;

    void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override;

	// Find all of the channels relevant to all of the target handles 
    void GatherInputOperators( std::vector< CDmeOperator * > &operatorList ) override;

	// Disconnect the channels driving the slave dag nodes from the dag transforms and connect them to the constraint
    void DisconnectTransformChannels() override;

	// Reconnect the base channels of each slave directly to the dag
    void ReconnectTransformChannels() override;

	// Validates that the start and end effector are set up and that there is only one intermediate bone between them forming a 2 bone ik chain.
	bool Setup( bool bPreserveOffset );

	// Get a pointer to the dag node the constraint is controlling
    const CDmeDag *GetSlave() const override;

	// Determine if the the constraint has slave with the specified name, for the ik constraint this is always false.
    bool IsSlaveObject( char const *pchName ) const override;

private:

	// Compute the offsets of the specified target based on the relative transforms of the target to the slave
    void ComputeOffset( Vector &vOffset, Quaternion &qOffset, CDmeConstraintTarget *pTarget, bool bPreserveOffset ) override;

	// Calculate the orientation needed to make a transform with the specified forward vector
	void AimAt( const Vector &vecForward, const Vector &referenceUp, Quaternion &q );

	// Perform the 2 bone ik solve to get target orientation for the start and mid bones
	float CalculateOrientations( Quaternion &startBoneOrient, Quaternion &midBoneOrient, const Quaternion &startOffset, const Quaternion &midOffset );

public:

	CDmaVar< Quaternion >				m_StartOffsetRotation;
	CDmaVar< Quaternion >				m_MidOffsetRotation;
	CDmaVar< Vector >					m_PoleVector;
	CDmaElement< CDmeDag >				m_PoleVectorTarget;
	CDmaElement< CDmeConstraintSlave >	m_StartJoint;
	CDmaElement< CDmeConstraintSlave >	m_MidJoint;
	CDmaElement< CDmeConstraintSlave >	m_EndJoint;

};


//-----------------------------------------------------------------------------
// CDmeRigTwistSlave
//-----------------------------------------------------------------------------
class CDmeRigTwistSlave : public CDmeConstraintSlave
{
	DEFINE_ELEMENT( CDmeRigTwistSlave, CDmeConstraintSlave );

public:
	// Sets the weight for this slave
	void SetWeight( float flWeight ) { m_flWeight = flWeight; }

	// Gets the weight for this slave
	float GetWeight() const { return m_flWeight; }

protected:
	CDmaVar< float > m_flWeight;
};


//-----------------------------------------------------------------------------
// CDmeRigTwistConstraintOperator 
//-----------------------------------------------------------------------------
class CDmeRigTwistConstraintOperator : public CDmeRigBaseConstraintOperator
{
public:

	DEFINE_ELEMENT( CDmeRigTwistConstraintOperator, CDmeRigBaseConstraintOperator );

public:
	// Returns the value of the m_bInverse attribute
	bool GetInverse() const { return m_bInverse; }

	// Sets the value of the m_bInverse attribute
	void SetInverse( bool bInverse ) { m_bInverse = bInverse; }

	// Returns the value of the m_vUpAxis attribute
	const Vector &GetUpAxis() const { return m_vUpAxis; }

	// Sets the value of the m_bInverse attribute
	void SetUpAxis( const Vector &vUpAxis ) { m_vUpAxis = vUpAxis; }

	// Get the Target dag that is the parent joint
	CDmeDag *GetParentTarget() const;

	// Get the Target dag that is the child joint
	CDmeDag *GetChildTarget() const;

	// Remove all slaves
	void ClearSlaves();

	// Get slave count
	int SlaveCount() const { return m_eSlaves.size(); }

	// Get slave count
	CDmeDag *GetSlaveDag( int i ) const;

	// Get slave count
	float GetSlaveWeight( int i ) const;

	// Get parent bind rotation
	const Quaternion &GetParentBindRotation() const { return m_qParentBindRotation; }

	// Get child bind rotation
	const Quaternion &GetChildBindRotation() const { return m_qChildBindRotation; }

	// Get slave bind rotation
	const Quaternion &GetSlaveBindOrientation( int i ) const;

	// From CDmeRigBaseConstraintOperator
    EConstraintType GetConstraintType() const override { return CT_TWIST; }

    void Operate() override;

    EAddType GetInputAttributeType() const override { return (EAddType)( AA_TYPE_POSITION | AA_TYPE_ORIENTATION ); } // Requires position and orientation
    EAddType GetOutputAttributeType() const override { return AA_TYPE_ORIENTATION; } // Only writes orientation of destination bone

	// Overridden
    void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override;

	// Get a pointer to the dag node the constraint is controlling
    const CDmeDag *GetSlave() const override;

	// Determine if the the constraint has slave with the specified name, for the ik constraint this is always false.
    bool IsSlaveObject( char const *pchName ) const override;

private:
	// Compute the offsets of the specified target based on the relative transforms of the target to the slave
    void ComputeOffset( Vector &vOffset, Quaternion &qOffset, CDmeConstraintTarget *pTarget, bool bPreserveOffset ) override;

	// Disconnect the channels driving the slave dag nodes from the dag transforms and connect them to the constraint
    void DisconnectTransformChannels() override;

	// Reconnect the base channels of each slave directly to the dag
    void ReconnectTransformChannels() override;
	CDmaVar< bool > m_bInverse;
	CDmaVar< Vector > m_vUpAxis;
	CDmaArray< float > m_flWeights;
	CDmaElementArray< CDmeRigTwistSlave > m_eSlaves;
	CDmaVar< Quaternion > m_qParentBindRotation;
	CDmaVar< Quaternion > m_qChildBindRotation;
};



//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Dme version of a skeletal model (gets compiled into a MDL)
//
//=============================================================================
#include "movieobjects/dmemodel.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "studio.h"
#include "movieobjects/dmemesh.h"
#include "movieobjects/dmechannel.h"




//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeModel, CDmeModel );


//-----------------------------------------------------------------------------
// Stack of DmeModels currently being rendered. Used to set up render state
//-----------------------------------------------------------------------------
static CUtlVector< matrix3x4_t > s_PoseToWorld;


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeModel::OnConstruction()
{
	m_JointList.Init( this, "jointList" );
	m_BaseStates.Init( this, "baseStates" );
	m_UpAxis.InitAndSet( this, "upAxis", "Y" );
	m_eAxisSystem.InitAndCreate( this, "axisSystem" );	// Defaults to Y up
}

void CDmeModel::OnDestruction()
{
}




//-----------------------------------------------------------------------------
// Returns the number of joint transforms we know about
//-----------------------------------------------------------------------------
size_t CDmeModel::GetJointCount() const
{
	return m_JointList.size();
}


//-----------------------------------------------------------------------------
// Determines joint transform index	given a joint transform
//-----------------------------------------------------------------------------
int CDmeModel::GetJointIndex(const CDmeDag *pJoint ) const
{
	const auto nJointCount = m_JointList.size();
	for ( int i = 0; i < nJointCount; ++i )
	{
		if ( pJoint == m_JointList[ i ] )
		{
			return i;
		}
	}

	return -1;
}




//-----------------------------------------------------------------------------
// Determines joint transform index	given a joint name
//-----------------------------------------------------------------------------
CDmeTransform *CDmeModel::GetJointTransform(size_t index) const {
	return m_JointList[ index ]->GetTransform();
}



//-----------------------------------------------------------------------------
// Finds a base state by name, returns NULL if not found
//-----------------------------------------------------------------------------
CDmeTransformList *CDmeModel::FindBaseState( const char *pBaseStateName )
{
	int nCount = m_BaseStates.size();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( !V_stricmp( m_BaseStates[i]->GetName(), pBaseStateName ) )
			return m_BaseStates[i];
	}
	return nullptr;
}







//-----------------------------------------------------------------------------
// Returns true if the data under the DmeModel is AS_VALVE_ENGINE_SPACE ( Up: Z, Fwd: X, Left: Y )
// Returns false otherwise, NOTE: false doesn't imply any other axis system, examine m_eAxisSystem to see
//-----------------------------------------------------------------------------
bool CDmeModel::IsZUp() const
{
	Assert( m_eAxisSystem.GetElement() );

	if ( m_eAxisSystem.GetElement()->IsEqual( CDmeAxisSystem::AS_VALVE_ENGINE ) )
		return true;

	if ( m_eAxisSystem.GetElement()->IsEqual( CDmeAxisSystem::AS_MAYA_YUP ) )
		return false;

	AssertMsg( false, "CDmeModel::IsZUp() called, but axis system is neither AS_VALVE_ENGINE or AS_MAYA_YUP, cannot represent via true/false, ambiguous state" );

	return false;
}






















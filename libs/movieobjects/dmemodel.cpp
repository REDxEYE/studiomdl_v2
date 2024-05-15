//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Dme version of a skeletal model (gets compiled into a MDL)
//
//=============================================================================
#include "movieobjects/dmemodel.h"
#include "movieobjects_interfaces.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "datacache/imdlcache.h"
#include "tier2/tier2.h"
#include "studio.h"
#include "movieobjects/dmemesh.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmelog.h"

// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


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
// Add joint
//-----------------------------------------------------------------------------
int CDmeModel::AddJoint( CDmeDag *pJoint )
{
	const int nIndex = GetJointIndex( pJoint );
	if ( nIndex >= 0 )
		return nIndex;

	return m_JointList.AddToTail( pJoint );
}


//-----------------------------------------------------------------------------
// Add joint
//-----------------------------------------------------------------------------
CDmeJoint *CDmeModel::AddJoint( const char *pJointName, CDmeDag *pParent )
{
	CDmeJoint *pJoint = CreateElement< CDmeJoint >( pJointName, GetFileId() );
	CDmeTransform *pTransform = pJoint->GetTransform();
	pTransform->SetName( pJointName );

	if ( !pParent )
	{
		pParent = this;
	}
	pParent->AddChild( pJoint );
	m_JointList.AddToTail( pJoint );

	return pJoint;
}


//-----------------------------------------------------------------------------
// Returns the number of joint transforms we know about
//-----------------------------------------------------------------------------
int CDmeModel::GetJointCount() const
{
	return m_JointList.Count();
}


//-----------------------------------------------------------------------------
// Determines joint transform index	given a joint transform
//-----------------------------------------------------------------------------
int CDmeModel::GetJointIndex( CDmeDag *pJoint ) const
{
	const int nJointCount = m_JointList.Count();
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
int CDmeModel::GetJointIndex( const char *pJointName ) const
{
	const int nJointCount = m_JointList.Count();
	for ( int i = 0; i < nJointCount; ++i )
	{
		if ( !V_strcmp( pJointName, m_JointList[ i ]->GetName() ) )
		{
			return i;
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Determines joint transform index	given a joint name hash
//-----------------------------------------------------------------------------
int CDmeModel::GetJointIndex( CUtlStringToken nJointNameHash ) const
{
	const int nJointCount = m_JointList.Count();
	for ( int i = 0; i < nJointCount; ++i )
	{
		if ( MakeStringToken( m_JointList[ i ]->GetName() ) == nJointNameHash )
		{
			return i;
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Returns the DmeDag for the specified joint index
//-----------------------------------------------------------------------------
CDmeDag *CDmeModel::GetJoint( int nIndex )
{
	return m_JointList[ nIndex ];
}


const CDmeDag *CDmeModel::GetJoint( int nIndex ) const
{
	return m_JointList[ nIndex ];
}


//-----------------------------------------------------------------------------
// Determines joint transform index	given a joint name
//-----------------------------------------------------------------------------
CDmeTransform *CDmeModel::GetJointTransform( int nIndex )
{
	return m_JointList[ nIndex ]->GetTransform();
}


const CDmeTransform *CDmeModel::GetJointTransform( int nIndex ) const
{
	return m_JointList[ nIndex ]->GetTransform();
}


//-----------------------------------------------------------------------------
// Finds a base state by name, returns NULL if not found
//-----------------------------------------------------------------------------
CDmeTransformList *CDmeModel::FindBaseState( const char *pBaseStateName )
{
	int nCount = m_BaseStates.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( !V_stricmp( m_BaseStates[i]->GetName(), pBaseStateName ) )
			return m_BaseStates[i];
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Captures the current joint transforms into a base state
//-----------------------------------------------------------------------------
void CDmeModel::CaptureJointsToBaseState( const char *pBaseStateName )
{
	CDmeTransformList *pTransformList = FindBaseState( pBaseStateName );
	if ( !pTransformList )
	{
		pTransformList = CreateElement<CDmeTransformList>( pBaseStateName, GetFileId() );
		m_BaseStates.AddToTail( pTransformList );
	}

	// Make the transform list have the correct number of elements
	const int nJointCount = m_JointList.Count();
	const int nCurrentCount = pTransformList->GetTransformCount();
	if ( nJointCount > nCurrentCount )
	{
		for ( int i = nCurrentCount; i < nJointCount; ++i )
		{
			CDmeTransform *pTransform = CreateElement<CDmeTransform>( m_JointList[i]->GetName(), pTransformList->GetFileId() );
			pTransformList->m_Transforms.AddToTail( pTransform );
		}
	}
	else if ( nJointCount < nCurrentCount )
	{
		pTransformList->m_Transforms.RemoveMultiple( nJointCount, nCurrentCount - nJointCount );
	}

	// Copy the state over
	for ( int i = 0; i < nJointCount; ++i )
	{
		matrix3x4_t mat;

		CDmeDag *pDmeDag = m_JointList[i];
		if ( !pDmeDag )
		{
			char szTmpBuf0[ 38 ];
			UniqueIdToString( GetId(), szTmpBuf0, ARRAYSIZE( szTmpBuf0 ) );
			Warning( "DmeModel::CaptureJointsToBaseState( %s ): DmeModel %s[%s].jointList[ %d ] is NULL\n", pBaseStateName, GetName(), szTmpBuf0, i );
			continue;
		}

		CDmeTransform *pDmeTransform = pDmeDag->GetTransform();
		if ( !pDmeTransform )
		{
			char szTmpBuf0[ 38 ];
			UniqueIdToString( GetId(), szTmpBuf0, ARRAYSIZE( szTmpBuf0 ) );
			char szTmpBuf1[ 38 ];
			UniqueIdToString( pDmeDag->GetId(), szTmpBuf1, ARRAYSIZE( szTmpBuf1 ) );
			Warning( "DmeModel::CaptureJointsToBaseState( %s ) - DmeModel %s[%s].jointList[ %d ].transform ( %s[%s].transform ) is NULL\n", pBaseStateName, GetName(), szTmpBuf0, i, pDmeDag->GetName(), szTmpBuf1 );
			continue;
		}

		pDmeTransform->GetTransform( mat );
		pTransformList->SetTransform( i, mat );
	}
}


//-----------------------------------------------------------------------------
// Sets the joint transforms to the values in the specified base state, if it exists
//-----------------------------------------------------------------------------
void CDmeModel::PushBaseStateToJoints( const char *pBaseStateName )
{
	CDmeTransformList *pTransformList = FindBaseState( pBaseStateName );
	if ( !pTransformList )
		return;

	for ( int ii = 0; ii < pTransformList->GetTransformCount(); ++ii )
	{
		CDmeTransform *pSrcDmeTransform = pTransformList->GetTransform( ii );
		CDmeDag *pDmeJoint = GetJoint( ii );

		if ( !pSrcDmeTransform || !pDmeJoint )
			continue;

		CDmeTransform *pDstDmeTransform = pDmeJoint->GetTransform();
		if ( !pDstDmeTransform )
			continue;

		pDstDmeTransform->SetPosition( pSrcDmeTransform->GetPosition() );
		pDstDmeTransform->SetOrientation( pSrcDmeTransform->GetOrientation() );
	}
}


//-----------------------------------------------------------------------------
// Loads up joint transforms for this model
//-----------------------------------------------------------------------------
void CDmeModel::LoadJointTransform( CDmeDag *pJoint, CDmeTransformList *pBindPose, const matrix3x4_t &parentToWorld, const matrix3x4_t &parentToBindPose, bool bSetHardwareState )
{
	// Determines joint transform index; no index, no traversing lower in the hierarchy
	const int nJointIndex = GetJointIndex( pJoint );
	if ( nJointIndex < 0 )
		return;

	// FIXME: Sucky search here necessary to find bone matrix index
	matrix3x4_t	jointToWorld, jointToParent;
	pJoint->GetTransform()->GetTransform( jointToParent );
	ConcatTransforms( parentToWorld, jointToParent, jointToWorld );

	matrix3x4_t bindJointToParent, bindPoseToJoint, bindPoseToWorld, jointToBindPose;
	if ( pBindPose )
	{
		if ( nJointIndex >= pBindPose->GetTransformCount() )
		{
			Warning( "Model is in an invalid state! There are different numbers of bones in the bind pose and joint transform list!\n" );
			return;
		}
		pBindPose->GetTransform( nJointIndex )->GetTransform( bindJointToParent );
	}
	else
	{
		MatrixCopy( jointToParent, bindJointToParent );
	}
	ConcatTransforms( parentToBindPose, bindJointToParent, jointToBindPose );

	MatrixInvert( jointToBindPose, bindPoseToJoint );
	ConcatTransforms( jointToWorld, bindPoseToJoint, bindPoseToWorld );

	MatrixCopy( bindPoseToWorld, s_PoseToWorld[ nJointIndex ] );

	int nChildCount = pJoint->GetChildCount();
	for ( int i = 0; i < nChildCount; ++i )
	{
		CDmeDag *pChildJoint = pJoint->GetChild(i);
		if ( !pChildJoint )
			continue;

		LoadJointTransform( pChildJoint, pBindPose, jointToWorld, jointToBindPose, bSetHardwareState );
	}
}



//-----------------------------------------------------------------------------
// Set if Z is the up axis of the model
//-----------------------------------------------------------------------------
void CDmeModel::ZUp( bool bZUp )
{
	if ( bZUp )
	{
		// Z up is technically ambiguous, but only Maya created SMD Z Up files, everything else assumes Z up is Valve engine Z up
		SetAxisSystem( CDmeAxisSystem::AS_VALVE_ENGINE );
	}
	else
	{
		SetAxisSystem( CDmeAxisSystem::AS_MAYA_YUP );
	}
}

//-----------------------------------------------------------------------------
// Returns the matrix that moves DmeModel data to engine space
//-----------------------------------------------------------------------------
void CDmeModel::GetModelToEngineMat( matrix3x4_t &modelToEngineMat )
{
	CDmeDag::DmeToEngineMatrix( modelToEngineMat, IsZUp() );
}


//-----------------------------------------------------------------------------
// Returns the matrix that moves engine data to DmeModel space
//-----------------------------------------------------------------------------
void CDmeModel::GetEngineToModelMat( matrix3x4_t &engineToModelMat )
{
	CDmeDag::EngineToDmeMatrix( engineToModelMat, IsZUp() );
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


//-----------------------------------------------------------------------------
// Replace all instances of a material with a different material
//-----------------------------------------------------------------------------
void CDmeModel::ReplaceMaterial( CDmeDag *pDag, const char *pOldMaterialName, const char *pNewMaterialName )
{
	if ( !pDag )
		return;

	CDmeMesh *pMesh = CastElement< CDmeMesh >( pDag->GetShape() );
	if ( pMesh )
	{
		pMesh->ReplaceMaterial( pOldMaterialName, pNewMaterialName );
	}

	int nCount = pDag->GetChildCount();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmeDag	*pChild = pDag->GetChild( i );
		ReplaceMaterial( pChild, pOldMaterialName, pNewMaterialName );
	}
}

void CDmeModel::ReplaceMaterial( const char *pOldMaterialName, const char *pNewMaterialName )
{
	ReplaceMaterial( this, pOldMaterialName, pNewMaterialName );
}


//-----------------------------------------------------------------------------
// Gets the joint with the specified name
//-----------------------------------------------------------------------------
CDmeDag *CDmeModel::GetJoint( const char *pJointName )
{
	CDmeDag *pJoint = NULL;

	const int nJointCount = m_JointList.Count();
	for ( int i = 0; i < nJointCount; ++i )
	{
		pJoint = m_JointList[ i ];
		if ( !pJoint )
			continue;

		if ( !V_stricmp( pJointName, pJoint->GetName() ) )
			return pJoint;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeModel::ReskinMeshes( const int *pJointTransformIndexRemap )
{
	ReskinMeshes( this, pJointTransformIndexRemap );
}


//-----------------------------------------------------------------------------
// Reskin meshes based on bone collapse
//-----------------------------------------------------------------------------
void CDmeModel::ReskinMeshes( CDmeDag *pDag, const int *pJointTransformIndexRemap )
{
	if ( !pDag )
		return;

	CDmeMesh *pMesh = CastElement< CDmeMesh >( pDag->GetShape() );
	if ( pMesh )
	{
		pMesh->Reskin( pJointTransformIndexRemap );
	}

	int nCount = pDag->GetChildCount();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmeDag	*pChild = pDag->GetChild( i );
		ReskinMeshes( pChild, pJointTransformIndexRemap );
	}
}


//-----------------------------------------------------------------------------
// Remove joints
//-----------------------------------------------------------------------------
void CDmeModel::RemoveJoints( int nNewJointCount, const int *pInvJointRemap )
{
	const int nOldJointTransformCount = m_JointList.Count();

	const int nSizeInBytes = nNewJointCount * sizeof(CDmeTransform*);
	CDmeDag **ppJoints = (CDmeDag**)stackalloc( nSizeInBytes );
	for ( int i = 0; i < nNewJointCount; ++i )
	{
		ppJoints[i] = m_JointList[ pInvJointRemap[i] ];
	}

	m_JointList.RemoveAll();
	for ( int i = 0; i < nNewJointCount; ++i )
	{
		m_JointList.AddToTail( ppJoints[i] );
	}

	CUtlVectorFixedGrowable< CDmeTransform*, 256 > transforms;

	const int nCount = m_BaseStates.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmeTransformList *pList = m_BaseStates[i];
		int nTransformCount = pList->GetTransformCount();
		transforms.SetCount( nTransformCount );
		int nStateJointCount = nNewJointCount;
		for ( int j = 0; j < nNewJointCount; ++j )
		{
			transforms[j] = pList->GetTransform( pInvJointRemap[j] );
		}
		for ( int j = nOldJointTransformCount; j < nTransformCount; ++j )
		{
			transforms[ nStateJointCount++ ] = pList->GetTransform( j );
		}

		pList->m_Transforms.RemoveAll();
		for ( int j = 0; j < nStateJointCount; ++j )
		{
			pList->m_Transforms.AddToTail( transforms[ j ] );
		}
	}
}


//-----------------------------------------------------------------------------
// Updates all base states by adding missing joints
//-----------------------------------------------------------------------------
void CDmeModel::UpdateBaseStates()
{
	const int nJointCount = m_JointList.Count();

	for ( int i = 0; i < m_BaseStates.Count(); ++i )
	{
		CDmeTransformList *pTransformList = m_BaseStates[i];
		if ( !pTransformList )
			continue;

		// Make the transform list have the correct number of elements
		const int nCurrentCount = pTransformList->GetTransformCount();

		if ( nJointCount < nCurrentCount )
		{
			pTransformList->m_Transforms.RemoveMultiple( nJointCount, nCurrentCount - nJointCount );
			return;
		}
		else if ( nJointCount == nCurrentCount )
		{
			return;
		}

		Assert( nJointCount > nCurrentCount );

		for ( int i = nCurrentCount; i < nJointCount; ++i )
		{
			CDmeTransform *pTransform = CreateElement<CDmeTransform>( m_JointList[i]->GetName(), pTransformList->GetFileId() );
			pTransformList->m_Transforms.AddToTail( pTransform );

			matrix3x4_t mat;
			CDmeDag *pDmeDag = m_JointList[i];
			if ( !pDmeDag )
				continue;

			CDmeTransform *pDmeTransform = pDmeDag->GetTransform();
			if ( !pDmeTransform )
				continue;

			pDmeTransform->GetTransform( mat );
			pTransformList->SetTransform( i, mat );
		}
	}
}


//-----------------------------------------------------------------------------
// Removes all children from this joint, moving shapes to be 
//-----------------------------------------------------------------------------
void CDmeModel::RemoveAllChildren( CDmeDag *pDag, CDmeDag *pSubtreeRoot, const matrix3x4_t &jointToSubtreeRoot )
{
	if ( !pDag )
		return;

	CDmeTransform *pDagTransform = pDag->GetTransform();
	matrix3x4_t curToSubtreeRoot, dagToParent;
	pDagTransform->GetTransform( dagToParent );
	ConcatTransforms( jointToSubtreeRoot, dagToParent, curToSubtreeRoot );

	if ( !CastElement< CDmeJoint >( pDag ) )
	{
		pSubtreeRoot->AddChild( pDag );
		pDagTransform->SetTransform( curToSubtreeRoot );
	}

	int nCount = pDag->GetChildCount();
	for ( int i = 0; i < nCount; ++i )
	{
		RemoveAllChildren( pDag->GetChild(i), pSubtreeRoot, curToSubtreeRoot );
	}

	pDag->RemoveAllChildren();
}

void CDmeModel::RemoveAllChildren( CDmeDag *pSubtreeRoot )
{
	matrix3x4_t root;
	SetIdentityMatrix( root );

	int nCount = pSubtreeRoot->GetChildCount();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmeDag *pChild = pSubtreeRoot->GetChild(i);
		if ( !CastElement< CDmeJoint >( pChild ) )
			continue;
		RemoveAllChildren( pChild, pSubtreeRoot, root );
	}

	// Regetting the child count, as it has changed from the RemoveAllChildren calls
	nCount = pSubtreeRoot->GetChildCount();
	for ( int i = nCount; --i >= 0; )
	{
		if ( !CastElement< CDmeJoint >( pSubtreeRoot->GetChild(i) ) )
			continue;

		pSubtreeRoot->RemoveChild( i );
	}
}


//-----------------------------------------------------------------------------
// Collapses all joints below the specified joint name, reskinning any meshes
// referring to collapsed joints to use the specified joint instead
//-----------------------------------------------------------------------------
void CDmeModel::CollapseJoints( const char *pJointName )
{
	CDmeDag *pJoint = GetJoint( pJointName );
	if ( !pJoint )
		return;

	// Determine which joints must be collapsed
	CDmeDag *pParent;
	const int nCount = m_JointList.Count();
	bool *pIsCollapsed = (bool*)stackalloc( nCount * sizeof(bool) );
	for ( int i = 0; i < nCount; ++i ) 
	{
		CDmeDag *pTestJoint = m_JointList[ i ];
		if ( !pTestJoint )
			continue;

		pIsCollapsed[i] = ( pJoint->FindChild( pParent, pTestJoint ) >= 0 ); 
	}

	// Build remap indices
	int nNewJointCount = 0;
	int nCollapseJointIndex = 0;
	int* pJointRemap = (int*)stackalloc( nCount * sizeof(int) );
	int* pInvJointRemap = (int*)stackalloc( nCount * sizeof(int) );
	for ( int i = 0; i < nCount; ++i ) 
	{
		if ( pIsCollapsed[i] )
		{
			pJointRemap[i] = -1;
			continue;
		}

		if ( pJoint == m_JointList[i] )
		{
			nCollapseJointIndex = nNewJointCount;
		}
		pInvJointRemap[nNewJointCount] = i;
		pJointRemap[i] = nNewJointCount++;
	}

	for ( int i = 0; i < nCount; ++i ) 
	{
		if ( pJointRemap[i] < 0 )
		{
			pJointRemap[i] = nCollapseJointIndex;
		}
	}

	// Reskin meshes
	ReskinMeshes( this, pJointRemap );

	// Fixup joint transform + base state lists
	RemoveJoints( nNewJointCount, pInvJointRemap );

	// Remove all children from this node
	RemoveAllChildren( pJoint );

	stackfree( pIsCollapsed );
	stackfree( pJointRemap );
	stackfree( pInvJointRemap );
}



//-----------------------------------------------------------------------------
// Returns the matrix & quaternion to reorient
//-----------------------------------------------------------------------------
void CDmeModel::GetReorientData( matrix3x4_t &m, Quaternion &q, bool bMakeZUp )
{
	// YUP_ACTIVE: FIXME
	if ( bMakeZUp )
	{
		static const matrix3x4_t mYtoZ( 
			0.0f,  0.0f,  1.0f, 0.0f,
			1.0f,  0.0f,  0.0f, 0.0f,
			0.0f,  1.0f,  0.0f, 0.0f );

		m = mYtoZ;

		matrix3x4a_t mA;
		CDmeAxisSystem::GetConversionMatrix( mA, CDmeAxisSystem::AS_MAYA_YUP, CDmeAxisSystem::AS_VALVE_ENGINE );
		Assert( MatricesAreEqual( mYtoZ, mA ) );
	}
	else
	{
		static const matrix3x4_t mZtoY( 
			0.0f,  1.0f,  0.0f, 0.0f,
			0.0f,  0.0f,  1.0f, 0.0f,
			1.0f,  0.0f,  0.0f, 0.0f );
			
		m = mZtoY;

		matrix3x4a_t mA;
		CDmeAxisSystem::GetConversionMatrix( mA, CDmeAxisSystem::AS_VALVE_ENGINE, CDmeAxisSystem::AS_MAYA_YUP );
		Assert( MatricesAreEqual( mZtoY, mA, 1.0e-4 ) );
	}

	MatrixQuaternion( m, q );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeModel::ReorientDmeAnimation( CDmeDag *pDmeDag, const matrix3x4_t &mOrient, const Quaternion &qOrient )
{
	if ( !pDmeDag )
		return;

	CUtlVector< CDmeChannel * > dmeChannelList;

	if ( !FindReferringElements( dmeChannelList, pDmeDag->GetTransform(), g_pDataModel->GetSymbol( "toElement" ) ) || dmeChannelList.Count() < 0 )
		return;

	const int nDmeChannelCount = dmeChannelList.Count();
	for ( int i = 0; i < nDmeChannelCount; ++i )
	{
		CDmeChannel *pDmeChannel = dmeChannelList[ i ];
		if ( !pDmeChannel )
			continue;

		CDmeLog *pDmeLog = pDmeChannel->GetLog();
		if ( !pDmeLog )
			continue;

		const int nLogLayerCount = pDmeLog->GetNumLayers();
		for ( int j = 0; j < nLogLayerCount; ++j )
		{
			CDmeLogLayer *pDmeLogLayer = pDmeLog->GetLayer( j );

			CDmeVector3LogLayer *pDmeVector3LogLayer = CastElement< CDmeVector3LogLayer >( pDmeLogLayer );
			if ( pDmeVector3LogLayer  )
			{
				RotatePositionLog( pDmeVector3LogLayer, mOrient );
				continue;
			}

			CDmeQuaternionLogLayer *pDmeQuaternionLogLayer = CastElement< CDmeQuaternionLogLayer >( pDmeLogLayer );
			if ( pDmeQuaternionLogLayer  )
			{
				RotateOrientationLog( pDmeQuaternionLogLayer, mOrient, true );
				continue;
			}
		}
	}
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeModel::ReorientDmeTransform( CDmeTransform *pDmeTransform, const matrix3x4_t &mOrient, const Quaternion &qOrient )
{
	if ( !pDmeTransform )
		return;

	Vector vTmp;
	VectorRotate( pDmeTransform->GetPosition(), mOrient, vTmp );
	pDmeTransform->SetPosition( vTmp );

	Quaternion qTmp;
	QuaternionMult( qOrient, pDmeTransform->GetOrientation(), qTmp );
	pDmeTransform->SetOrientation( qTmp );
}

//-----------------------------------------------------------------------------
// Changes the orientation of the vertices and normals of a CDmeMesh using
// the given transform.
//-----------------------------------------------------------------------------
void CDmeModel::ReorientDmeMesh( CDmeMesh *pDmeMesh, matrix3x4_t absMat )
{
	CDisableUndoScopeGuard();

	CDmeVertexData *pDmeVertexData = pDmeMesh->GetBindBaseState();

	FieldIndex_t nFieldPosIndex = pDmeVertexData->FindFieldIndex( CDmeVertexDataBase::FIELD_POSITION );
	FieldIndex_t nFieldNormIndex = pDmeVertexData->FindFieldIndex( CDmeVertexDataBase::FIELD_NORMAL );
	if ( nFieldPosIndex >= 0 && nFieldNormIndex > 0 )
	{
		CDmAttribute *pDmPositionAttr = pDmeVertexData->GetVertexData( nFieldPosIndex );
		CDmAttribute *pDmNormalAttr = pDmeVertexData->GetVertexData( nFieldNormIndex );

		CDmrArray< Vector > positions( pDmPositionAttr );
		CDmrArray< Vector > normals( pDmNormalAttr );

		CUtlVector<Vector> newPositions;

		matrix3x4_t normalMat;
		MatrixInverseTranspose( absMat, normalMat );

		for ( int i = 0; i < positions.Count(); ++i )
		{
			Vector newPosition;

			VectorTransform( positions[i], absMat, newPosition );
			positions.Set( i, newPosition );
		}

		for ( int i = 0; i < normals.Count(); ++i )
		{
			Vector normalizedNormal;
			Vector newNormal;

			VectorCopy( normals[i], normalizedNormal );
			VectorNormalize( normalizedNormal );
			VectorRotate( normalizedNormal, normalMat, newNormal );
			normals.Set( i, newNormal );
		}

	}
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeModel::ReorientDmeModelChildren( CDmeModel *pDmeModel, const matrix3x4_t &mOrient, const Quaternion &qOrient )
{
	if ( !pDmeModel )
		return;

	CDmeTransformList *pDmeTransformList = pDmeModel->FindBaseState( "bind" );
	const int nTransformCount = pDmeTransformList ? pDmeTransformList->GetTransformCount() : 0;

	const int nChildCount = pDmeModel->GetChildCount();
	for ( int i = 0; i < nChildCount; ++i )
	{
		CDmeDag *pDmeDag = pDmeModel->GetChild( i );
		if ( !pDmeDag )
			continue;

		ReorientDmeTransform( pDmeDag->GetTransform(), mOrient, qOrient );

		if ( pDmeTransformList )
		{
			int nJointIndex = pDmeModel->GetJointIndex( pDmeDag );
			if ( nJointIndex >= 0 && nJointIndex < nTransformCount )
			{
				ReorientDmeTransform( pDmeTransformList->GetTransform( nJointIndex ), mOrient, qOrient );
			}
		}

		ReorientDmeAnimation( pDmeDag, mOrient, qOrient );
	}
}


//-----------------------------------------------------------------------------
// Changes the orientation of a given CDmeDag's mesh data and those of any 
// children the CDmeDag has.
//-----------------------------------------------------------------------------
void CDmeModel::ReorientChildDmeMeshes_R( CDmeDag *pDmeDag )
{
	// Reorient the Dag's shape
	CDmeMesh *pDmeMesh = CastElement< CDmeMesh >( pDmeDag->GetShape() );

	if ( pDmeMesh ) 
	{
		matrix3x4_t absMat;
		pDmeDag->GetAbsTransform( absMat );

		ReorientDmeMesh( pDmeMesh, absMat );
	}

	// Recurse all child CDmeDags and 
	const int nChildCount = pDmeDag->GetChildCount();
	for ( int i = 0; i < nChildCount; ++i )
	{
		ReorientChildDmeMeshes_R( pDmeDag->GetChild( i ) );
	}
}


//-----------------------------------------------------------------------------
// Walks through the immediate children of this DmeModel and bakes the world
// space transform of any DmeMesh into its vertices and then sets the transform
// of that mesh to the identity.  Currently only called by hammer import and
// hammer <-> modo in modo via ReorientToEngineSpace & ReorientToDCCToolSpace
// dmeutils to convert DmeMesh to CMesh uses ReorientToValveEngineSpace
// which does not call FreezeChildMeshes.  FreezeChildMeshes is also not a
// complete implementation as it doesn't take into account deltas on meshes
//-----------------------------------------------------------------------------
void CDmeModel::FreezeChildMeshes()
{
	const int nChildCount = GetChildCount();
	for ( int i = 0; i < nChildCount; ++i )
	{
		CDmeDag *pDmeDag = GetChild( i );
		if ( !pDmeDag )
			continue;

		ReorientChildDmeMeshes_R( pDmeDag );

		// Zero out the rotation since ReorientChildDmeMeshes_R bakes it into the mesh
		CDmeTransform *pTransform = pDmeDag->GetTransform();
		if ( pTransform )
		{
			Quaternion qZero;
			qZero.Init();
			pTransform->SetOrientation( qZero );
		}
	}
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeModel::SetAxisSystem( CDmeAxisSystem::PredefinedAxisSystem ePredefAxisSystem )
{
	if ( !m_eAxisSystem.GetElement() )
		return false;

	return m_eAxisSystem.GetElement()->Init( ePredefAxisSystem );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeModel::SetAxisSystem(
	CDmeAxisSystem::Axis_t eUpAxis,
	CDmeAxisSystem::ForwardParity_t eForwardParity,
	CDmeAxisSystem::CoordSys_t eCoordSys /* = CDmeAxisSystem::AS_RIGHT_HANDED */ )
{
	if ( !m_eAxisSystem.GetElement() )
		return false;

	return m_eAxisSystem.GetElement()->Init( eUpAxis, eForwardParity, eCoordSys );
}


//-----------------------------------------------------------------------------
// This expects the app has handled undo
//-----------------------------------------------------------------------------
bool CDmeModel::ConvertToAxisSystem( CDmeAxisSystem::PredefinedAxisSystem eToPredefinedAxisSystem )
{
	if ( !m_eAxisSystem.GetElement() )
		return false;

	CDmeAxisSystem::Axis_t eToUpAxis;
	CDmeAxisSystem::ForwardParity_t eToForwardParity;
	CDmeAxisSystem::CoordSys_t eToCoordSys;

	if ( !CDmeAxisSystem::GetPredefinedAxisSystem( eToUpAxis, eToForwardParity, eToCoordSys, eToPredefinedAxisSystem ) )
		return false;

	return ConvertToAxisSystem( eToUpAxis, eToForwardParity, eToCoordSys );
}


//-----------------------------------------------------------------------------
// This expects the app has handled undo
//-----------------------------------------------------------------------------
bool CDmeModel::ConvertToAxisSystem(
	CDmeAxisSystem::Axis_t eToUpAxis,
	CDmeAxisSystem::ForwardParity_t eToForwardParity,
	CDmeAxisSystem::CoordSys_t eToCoordSys /* = CDmeAxisSystem::AS_RIGHT_HANDED */ )
{
	if ( !m_eAxisSystem.GetElement() )
		return false;

	if ( !CDmeAxisSystem::IsValid( eToUpAxis, eToForwardParity, eToCoordSys ) )
		return false;

	const CDmeAxisSystem *pFromDmeAxisSystem = m_eAxisSystem.GetElement();

	Assert( pFromDmeAxisSystem->IsValid() );

	const CDmeAxisSystem::Axis_t eFromUpAxis = pFromDmeAxisSystem->GetUpAxis();
	const CDmeAxisSystem::ForwardParity_t eFromForwardParity = pFromDmeAxisSystem->GetForwardParity();
	const CDmeAxisSystem::CoordSys_t eFromCoordSys = pFromDmeAxisSystem->GetCoordSys();

	Assert( CDmeAxisSystem::IsValid( eToUpAxis, eToForwardParity, eToCoordSys ) );

	if ( eFromUpAxis == eToUpAxis &&
		 eFromForwardParity == eToForwardParity &&
		 eFromCoordSys == eToCoordSys )
	{
		// No change needed
		return true;
	}

	matrix3x4a_t mConversion = g_MatrixIdentity;
	CDmeAxisSystem::GetConversionMatrix( mConversion, eFromUpAxis, eFromForwardParity, eFromCoordSys, eToUpAxis, eToForwardParity, eToCoordSys );
	const Quaternion qConversion = MatrixQuaternion( mConversion );

	ReorientDmeModelChildren( this, mConversion, qConversion );

	m_UpAxis.Set( "axisSystem" );

	return SetAxisSystem( eToUpAxis, eToForwardParity, eToCoordSys );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeModel::GetConversionMatrix( matrix3x4a_t &mConversion, CDmeAxisSystem::PredefinedAxisSystem eToPredefinedAxisSystem )
{
	const CDmeAxisSystem *pFromDmeAxisSystem = m_eAxisSystem.GetElement();

	Assert( pFromDmeAxisSystem->IsValid() );

	const CDmeAxisSystem::Axis_t eFromUpAxis = pFromDmeAxisSystem->GetUpAxis();
	const CDmeAxisSystem::ForwardParity_t eFromForwardParity = pFromDmeAxisSystem->GetForwardParity();
	const CDmeAxisSystem::CoordSys_t eFromCoordSys = pFromDmeAxisSystem->GetCoordSys();

	CDmeAxisSystem::Axis_t eToUpAxis;
	CDmeAxisSystem::ForwardParity_t eToForwardParity;
	CDmeAxisSystem::CoordSys_t eToCoordSys;

	if ( !CDmeAxisSystem::GetPredefinedAxisSystem( eToUpAxis, eToForwardParity, eToCoordSys, eToPredefinedAxisSystem ) )
		return false;

	CDmeAxisSystem::GetConversionMatrix( mConversion, eFromUpAxis, eFromForwardParity, eFromCoordSys, eToUpAxis, eToForwardParity, eToCoordSys );

	return true;
}


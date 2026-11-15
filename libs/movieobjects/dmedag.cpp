//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmedag.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmelog.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmerigconstraintoperators.h"




static const char OVERRIDE_PARENT[] = "overrideParent";

//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeDag, CDmeDag );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CUtlStack<CDmeDag::TransformInfo_t> CDmeDag::s_TransformStack;
bool CDmeDag::s_bDrawUsingEngineCoordinates = false;
bool CDmeDag::s_bDrawZUp = false;


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeDag::OnConstruction()
{
	m_Transform.InitAndCreate( this, "transform" );
	m_Shape.Init( this, "shape" );
	m_Visible.InitAndSet( this, "visible", true, FATTRIB_HAS_CALLBACK );
	m_Children.Init( this, "children" );
	m_bDisableOverrideParent.InitAndSet( this, "disableOverride", false, FATTRIB_DONTSAVE | FATTRIB_HIDDEN );
}

void CDmeDag::OnDestruction()
{
	g_pDataModel->DestroyElement( m_Transform.GetHandle() );
}

void CDmeDag::Resolve()
{
	// Since the overrideParent attribute is added dynamically we must update 
	// its flags is it present when re-loaded since the flags are not stored.
	CDmAttribute *pAttribute = GetAttribute( OVERRIDE_PARENT );
	if ( pAttribute )
	{
		pAttribute->AddFlag( FATTRIB_NEVERCOPY );
	}
}

//-----------------------------------------------------------------------------
// Accessors
//-----------------------------------------------------------------------------
CDmeTransform *CDmeDag::GetTransform() const
{
	return m_Transform.GetElement();
}

CDmeShape *CDmeDag::GetShape()
{
	return m_Shape.GetElement();
}




int CDmeDag::GetChildCount() const
{
	return m_Children.Count();
}

CDmeDag *CDmeDag::GetChild( int i ) const
{
	if ( i < 0 || i >= m_Children.Count() )
		return nullptr;

	return m_Children.Get( i );
}







//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDag::GetLocalMatrix( matrix3x4_t &m ) const
{
	CDmeTransform *pTransform = GetTransform();
	if ( pTransform )
	{
		pTransform->GetTransform( m );
	}
	else
	{
		SetIdentityMatrix( m );
	}
}

//-----------------------------------------------------------------------------
// Get the transform matrix which converts from the space of the parent of the
// dag node to world space. If the dag node has an override parent specified
// it will be accounted for when generating the matrix.
//-----------------------------------------------------------------------------
void CDmeDag::GetParentWorldMatrix( matrix3x4_t &mParentToWorld ) const
{
	CDmeDag *pParent = GetParent();

	if ( HasOverrideParent() )
	{
		bool bOverridePos = false;
		bool bOverrideRot = false;
		const CDmeDag *pOverrideParent = GetOverrideParent( bOverridePos, bOverrideRot );

		if ( bOverridePos && bOverrideRot )
		{
			pOverrideParent->GetAbsTransform( mParentToWorld );
		}
		else
		{
			if ( pParent )
			{
				pParent->GetAbsTransform( mParentToWorld );
			}
			else
			{
				SetIdentityMatrix( mParentToWorld );
			}
			
			Quaternion absOrientation;
			Vector absPosition;

			if ( bOverridePos )
			{			
				// The desired result of a position only parent override is that no changes to the original parent in either 
				// position or rotation will have any impact on the position of the dag. Furthermore we prefer rotations of
				// the original parent to pivot around the dag node's center, not the override parent's center. To accomplish 
				// this we essentially want to compute the rotation normally, but then apply the local translation of the dag
				// to the position of the override dag in world space. In order to construct the a parent matrix that still
				// allows the local transform of the dag to be concatenated normally, we essentially do the whole transform
				// here when constructing the parent matrix and then apply the inverse of the local matrix, so we know when
				// the local matrix is concatenated later the result will be what we setup before applying the inverse of the
				// local matrix.

				CDmeTransform *pLocalTransform = GetTransform();
				if ( pLocalTransform )
				{				
					Vector localPosition = pLocalTransform->GetPosition();
					Quaternion localOrientation = pLocalTransform->GetOrientation();
					
					// Compute the orientation normally as if there is no override parent
					// by concatenating the local orientation with the parent orientation.
					Quaternion parentOrientation;
					Quaternion worldOrientation;
					MatrixQuaternion( mParentToWorld, parentOrientation );
					QuaternionMult( parentOrientation, localOrientation, worldOrientation );

					// Compute the position by simply adding the local position to the world space 
					// position of the override parent, this effectively make the local translation 
					// of the dag a world space operation so it is never effected by rotation.
					Vector overridePosition;
					pOverrideParent->GetAbsPosition( overridePosition );
					Vector worldPosition = overridePosition + localPosition;

					// Compute the final world transform we will want for the dag 
					// (i.e. this is the reuslt we want GetAbsTransform() to return)
					matrix3x4_t mWorldTransform;
					QuaternionMatrix( worldOrientation, worldPosition, mWorldTransform );
					
					// Now construct an apply the inverse of the local transform in order to get
					// an appropriate parent matrix that will yield the desired mWorldTransfom 
					// when the local transform of the dag is applied to it.
					matrix3x4_t mLocalTransform;
					matrix3x4_t mInvLocalTransform;
					QuaternionMatrix( localOrientation, localPosition, mLocalTransform );
					MatrixInvert( mLocalTransform, mInvLocalTransform );
					ConcatTransforms( mWorldTransform, mInvLocalTransform, mParentToWorld );
				}
			}
			else if ( bOverrideRot )
			{
				// Rotation only override is much simpler than position, just construct a matrix with 
				// the orientation of the override parent and the position of the original parent.
				pOverrideParent->GetAbsOrientation( absOrientation );	
				MatrixPosition( mParentToWorld, absPosition );
				QuaternionMatrix( absOrientation, absPosition, mParentToWorld );
			}
		}
	}
	else if ( pParent )
	{
		pParent->GetAbsTransform( mParentToWorld );
	}
	else
	{
		SetIdentityMatrix( mParentToWorld );
	}
}


//-----------------------------------------------------------------------------
// Get the matrix matrix for computing the position of the dag node. This is 
// the same as GetParentWorldMatrix except in the case where the dag node has
// a position only override parent. This should be used when trying to apply a
// translation to the dag and conversion from world space to the space of the
// parent of the dag is required.
//-----------------------------------------------------------------------------
void CDmeDag::GetTranslationParentWorldMatrix( matrix3x4_t &mParentToWorld )
{
	bool bOverridePos = false;
	bool bOverrideRot = false;
	const CDmeDag *pOverrideParent = GetOverrideParent( bOverridePos, bOverrideRot );

	if ( pOverrideParent && bOverridePos && !bOverrideRot )
	{
		Vector overridePosition;
		pOverrideParent->GetAbsPosition( overridePosition );
		SetIdentityMatrix( mParentToWorld );
		PositionMatrix( overridePosition, mParentToWorld );
	}
	else
	{
		GetParentWorldMatrix( mParentToWorld );
	}
}




//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDag::GetBoundingSphere( Vector &c0, float &r0, const matrix3x4_t &pMat ) const
{
	matrix3x4_t lMat;
	m_Transform.GetElement()->GetTransform( lMat );
	matrix3x4_t wMat;
	ConcatTransforms( pMat, lMat, wMat );

	c0.Zero();
	r0 = 0.0f;

	Vector vTemp;

	const CDmeShape *pShape = m_Shape.GetElement();
	if ( pShape )
	{
		pShape->GetBoundingSphere( c0, r0 );
		VectorTransform( c0, lMat, vTemp );
		c0 = vTemp;
	}

	// No scale in Dme! :)
	VectorTransform( c0, pMat, vTemp );
	c0 = vTemp;

	const int nChildren = m_Children.Count();
	if ( nChildren > 0 )
	{
		Vector c1;	// Child center
		float r1;	// Child radius

		Vector v01;	// c1 - c0
		float l01;	// |v01|

		for ( int i = 0; i < nChildren; ++i )
		{
			m_Children[ i ]->GetBoundingSphere( c1, r1, wMat );

			if ( r0 == 0.0f )
			{
				c0 = c1;
				r0 = r1;
				continue;
			}

			v01 = c1 - c0;
			l01 = v01.NormalizeInPlace();

			if ( r0 < l01 + r1 )
			{
				// Current sphere doesn't contain both spheres
				if ( r1 < l01 + r0 )
				{
					// Child sphere doesn't contain both spheres
					c0 = c0 + 0.5f * ( r1 + l01 - r0 ) * v01;
					r0 = 0.5f * ( r0 + l01 + r1 );
				}
				else
				{
					// Child sphere contains both spheres
					c0 = c1;
					r0 = r1;
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Find the channels and operators which must be evaluated in order
// to determine the state of the dag node. Note that this function returns only
// the operators targeting this dag node, it does not include those targeting 
// the parent of this node, but it does return all operators that this node
// is dependent on even if they are are connected to this node through other
// operators.
//-----------------------------------------------------------------------------
void CDmeDag::FindLocalOperators( std::vector< CDmeOperator* > &operatorList ) const
{
	// Find any operators directly targeting the transform of the dag node
	GatherOperatorsForElement( GetTransform(), operatorList );

	// Find the slave instances targeting the specified node
	for ( DmAttributeReferenceIterator_t it = g_pDataModel->FirstAttributeReferencingElement( GetHandle() );
		it != DMATTRIBUTE_REFERENCE_ITERATOR_INVALID;
		it = g_pDataModel->NextAttributeReferencingElement( it ) )
	{
		CDmAttribute *pAttr = g_pDataModel->GetAttribute( it );
		CDmElement *pElement = pAttr->GetOwner();

		if ( !g_pDataModel->GetElement( pElement->GetHandle() ) )
			continue;

		auto *pConstraintSlave = CastElement< CDmeConstraintSlave >( pElement );
		if ( pConstraintSlave )
		{			
			CDmeRigBaseConstraintOperator *pConstraint = pConstraintSlave->GetConstraint();
			if ( pConstraint )
			{
				if ( !std::ranges::contains( operatorList, pConstraint ))
				{
					pConstraint->GatherInputOperators( operatorList );
					operatorList.emplace_back( pConstraint );
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Find all of the operators on which dag node is dependent, this 
// recursively finds operators for the parents of the dag. The list of operators
// that is returned represents all of the operators which must be evaluated in
// order to determine the current state of the node. The list is returned in
// the order that the operators should be run.
//-----------------------------------------------------------------------------
void CDmeDag::FindRelevantOperators( std::vector< CDmeOperator * > &operatorList ) const
{	
	const CDmeDag *pOverrideParent = GetOverrideParent();
	if ( pOverrideParent )
	{
		pOverrideParent->FindRelevantOperators( operatorList );
	}

	CDmeDag *pParent = GetParent();
	if ( pParent )
	{
		pParent->FindRelevantOperators( operatorList );
	}

	FindLocalOperators( operatorList );
}



void CDmeDag::GetBoundingBox( Vector &min, Vector &max, const matrix3x4_t &pMat ) const
{
	matrix3x4_t lMat;
	m_Transform.GetElement()->GetTransform( lMat );
	matrix3x4_t wMat;
	ConcatTransforms( pMat, lMat, wMat );

	min.Zero();
	max.Zero();
	bool bHasBox = false;

	const CDmeShape *pShape = m_Shape.GetElement();
	if ( pShape )
	{
		Vector cmin, cmax;
		pShape->GetBoundingBox( cmin, cmax );
		if ( cmin != cmax )
		{
			Vector bmin( cmin ), bmax( cmax );
			VectorTransform( cmin, pMat, bmin );
			VectorTransform( cmax, pMat, bmax );
			for ( int kx = 0; kx < 2; ++ kx )
				for ( int ky = 0; ky < 2; ++ ky )
					for ( int kz = 0; kz < 2; ++ kz )
					{
						Vector tp(
							kx ? cmax.x : cmin.x,
							ky ? cmax.y : cmin.y,
							kz ? cmax.z : cmin.z );
						Vector vTemp;
						VectorTransform( tp, pMat, vTemp );

						bmin = bmin.Min( vTemp );
						bmax = bmax.Max( vTemp );
					}

			if ( !bHasBox )
			{
				min = bmin;
				max = bmax;
			}
			min = min.Min( bmin );
			max = max.Max( bmax );
			bHasBox = true;
		}
	}

	for ( int i = 0, nChildren = m_Children.Count(); i < nChildren; ++ i )
	{
		CDmeDag *pChild = m_Children[i];

		Vector cmin, cmax;
		pChild->GetBoundingBox( cmin, cmax, wMat );
		if ( cmin != cmax )
		{
			if ( !bHasBox )
			{
				min = cmin;
				max = cmax;
			}
			min = min.Min( cmin );
			max = max.Max( cmax );
			bHasBox = true;
		}
	}
}




void CDmeDag::GetAbsTransform( matrix3x4_t &matAbsTransform ) const
{
	matrix3x4_t parentToWorld;
	GetParentWorldMatrix( parentToWorld );

	matrix3x4_t localMatrix;
	GetLocalMatrix( localMatrix );

	ConcatTransforms( parentToWorld, localMatrix, matAbsTransform );
}


void CDmeDag::SetAbsTransform( const matrix3x4_t &matAbsTransform )
{
	CDmeTransform *pTransform = GetTransform();
	if ( pTransform == nullptr)
		return;

	matrix3x4_t parentToWorld;
	GetParentWorldMatrix( parentToWorld );

	matrix3x4_t worldToParent;
	MatrixInvert( parentToWorld, worldToParent );

	matrix3x4_t localSpace;
	ConcatTransforms( worldToParent, matAbsTransform, localSpace );

	Vector localPos;
	Quaternion localRot;
	MatrixAngles( localSpace, localRot, localPos );
	
	// The position only override depends on the local transform when calculating the parent transform,
	// meaning that simply inverting the parent to world matrix and concatenating it with the desired 
	// absolute transform does not produce the correct local matrix since changing the local matrix 
	// changes the result of GetParentWorldMatrix(). So when the position only override is enabled we 
	// must compute the rotation and position separately.
	if ( HasOverrideParent() )
	{
		matrix3x4_t mTranslationParentToWorld;
		GetTranslationParentWorldMatrix( mTranslationParentToWorld );
	
		matrix3x4_t mTranslationWorldToParent;
		MatrixInvert( mTranslationParentToWorld, mTranslationWorldToParent );

		matrix3x4_t mTranslationLocalSpace;
		ConcatTransforms( mTranslationWorldToParent, matAbsTransform, mTranslationLocalSpace );

		MatrixPosition( mTranslationLocalSpace, localPos);
	}

	pTransform->SetPosition( localPos );
	pTransform->SetOrientation( localRot );
}


CDmeDag *CDmeDag::GetParent() const
{
	const static CUtlSymbolLarge symChildren = g_pDataModel->GetSymbol( "children" );
	CDmeDag *pParent = FindReferringElement< CDmeDag >( this, symChildren, false );
	return pParent;
}



//-----------------------------------------------------------------------------
// Determine if the dag has an override parent
//-----------------------------------------------------------------------------
bool CDmeDag::HasOverrideParent() const
{
	if ( m_bDisableOverrideParent )
		return false;

	return ( GetValueElement< CDmeDag >( OVERRIDE_PARENT ) != nullptr);
}


//-----------------------------------------------------------------------------
// Get the current override parent, returns NULL if no override parent is set
//-----------------------------------------------------------------------------
const CDmeDag *CDmeDag::GetOverrideParent( bool bIgnoreEnable ) const
{
	if ( m_bDisableOverrideParent && !bIgnoreEnable )
		return nullptr;

	return GetValueElement< CDmeDag >( OVERRIDE_PARENT );
}


//-----------------------------------------------------------------------------
// Get the current override parent, returns NULL if no override parent is set
//-----------------------------------------------------------------------------
const CDmeDag *CDmeDag::GetOverrideParent( bool &bPosition, bool &bRotation, bool bIgnoreEnable ) const
{
	bPosition = false;
	bRotation = false;

	if ( m_bDisableOverrideParent && !bIgnoreEnable )
		return nullptr;

	CDmeDag *pOverrideParent = GetValueElement< CDmeDag >( OVERRIDE_PARENT );

	if ( pOverrideParent )
	{
		bPosition = GetValue< bool >( "overridePos", false );
		bRotation = GetValue< bool >( "overrideRot", false );
	}

	if ( bPosition || bRotation )
	{
		return pOverrideParent;
	}

	return nullptr;
}






void CDmeDag::GetAbsPosition( Vector &absPos ) const
{
	matrix3x4_t abs;
	GetAbsTransform( abs );
	MatrixGetColumn( abs, 3, absPos );
}

void CDmeDag::SetAbsPosition( const Vector &absPos )
{
	matrix3x4_t abs;
	GetAbsTransform( abs );
	MatrixSetColumn( absPos, 3, abs );
	SetAbsTransform( abs );
}

void CDmeDag::GetAbsOrientation( Quaternion &absOrientation ) const
{
	matrix3x4_t abs;
	GetAbsTransform( abs );
	Vector absPos;
	MatrixQuaternion( abs, absOrientation );
}


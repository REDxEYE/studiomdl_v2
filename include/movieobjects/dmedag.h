//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing a Dag (directed acyclic graph) node used for holding transforms, lights, cameras and shapes
//
//=============================================================================

#pragma once


#include "tier1/utlstack.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "movieobjects/dmeshape.h"
#include "movieobjects/dmetransform.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeTransform;
class CDmeShape;
class CDmeChannel;
class CDmeOperator;


//-----------------------------------------------------------------------------
// A class representing a camera
//-----------------------------------------------------------------------------
class CDmeDag : public CDmElement
{
	DEFINE_ELEMENT( CDmeDag, CDmElement );

public:
    void Resolve() override;

	// Accessors
	CDmeTransform *GetTransform() const;
	CDmeShape *GetShape();


	// child helpers
	int GetChildCount() const;
	CDmeDag *GetChild( int i ) const;


	// Recursively render the Dag hierarchy
	void GetBoundingSphere( Vector &center, float &radius ) const
	{
		matrix3x4_t identity;
		SetIdentityMatrix( identity );
		GetBoundingSphere( center, radius, identity );
	}
	void GetBoundingBox( Vector &min, Vector &max ) const
	{
		matrix3x4_t identity;
		SetIdentityMatrix( identity );
		GetBoundingBox( min, max, identity );
	}

	void GetLocalMatrix( matrix3x4_t &mat ) const;

	void GetAbsTransform( matrix3x4_t &matAbsTransform ) const;
	void SetAbsTransform( const matrix3x4_t &matAbsTransform );

	void GetAbsPosition( Vector &absPos ) const;
	void SetAbsPosition( const Vector &absPos );
	void GetAbsOrientation( Quaternion &absOrientation ) const;

	template <class T>
	T *FindParentDagOfType();

	// Find all nodes in the sub-tree of the dag which are of the specified type.
	template< class T >
	void FindChildrenOfType( CUtlVector< T* > &children );

	// Determine if the dag has an override parent
	bool HasOverrideParent() const;

	// Get the current override parent, returns NULL if no override parent is set
	const CDmeDag *GetOverrideParent( bool bIgnoreEnable = false ) const;
	const CDmeDag *GetOverrideParent( bool &bPosition, bool &bRotation, bool bIgnoreEnable = false ) const;

	CDmeDag *GetParent() const;

	void GetParentWorldMatrix( matrix3x4_t &mat ) const;

	void GetTranslationParentWorldMatrix( matrix3x4_t &mParentToWorld );

	static bool IsDrawZUp() { return !s_bDrawZUp; }

	// Transform from DME to engine coordinates

	// Find all of the operators on which dag node is dependent, this recursively finds operators for the parents of the dag.
	void FindRelevantOperators( std::vector< CDmeOperator * > &operatorList ) const;

	// This one only looks at the current CDmeDag
	void FindLocalOperators( std::vector< CDmeOperator * > &operatorList ) const;


protected:

	void GetBoundingSphere( Vector &center, float &radius, const matrix3x4_t &pMat ) const;
	void GetBoundingBox( Vector &min, Vector &max, const matrix3x4_t &pMat ) const;


	CDmaVar< bool >					m_Visible;
	CDmaVar< bool >					m_bDisableOverrideParent;
	CDmaElement< CDmeTransform >	m_Transform;
	CDmaElement< CDmeShape >		m_Shape;
	CDmaElementArray< CDmeDag >		m_Children;

private:
	struct TransformInfo_t
	{
		CDmeTransform *m_pTransform;
		matrix3x4_t	m_DagToWorld;
		bool m_bComputedDagToWorld;
	};
	
	static CUtlStack<TransformInfo_t> s_TransformStack;
	static bool s_bDrawUsingEngineCoordinates;
	static bool s_bDrawZUp;									// NOTE: s_bZUp doesn't mean the model is in engine coordinates, it means it's Z Up, -Y Front & X Right.  Engine is Z Up, X Front & Y Right.
};


template <class T>
inline T *CDmeDag::FindParentDagOfType()
{
	CDmeDag *parent = GetParent();
	while ( parent )
	{
		if ( CastElement< T >( parent ) )
			return static_cast< T * >( parent );

		parent = parent->GetParent();
	}

	if ( CastElement< T >( this ) )
		return static_cast< T * >( this );

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Find all dag nodes in the sub-tree of the dag which are of the
// specified type.
//-----------------------------------------------------------------------------
template< class T >
void CDmeDag::FindChildrenOfType( CUtlVector< T* > &children )
{
	int nChildren = m_Children.Count();
	for ( int iChild = 0; iChild < nChildren; ++iChild )
	{
		CDmeDag *pChild = m_Children[ iChild ];
		if ( pChild == nullptr)
			continue;

		// Add the child to the list if it is of the specified type
		T* pChildType = CastElement< T >( pChild );
		if ( pChildType )
		{
			children.AddToTail( pChildType );
		}
		
		// Recursively add the children of the child.
		pChild->FindChildrenOfType( children );
	}
}



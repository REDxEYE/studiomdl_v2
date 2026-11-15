//====== Copyright � 1996-2009, Valve Corporation, All rights reserved. =======
//
// DmeEyeball
//
//=============================================================================


// Valve includes
#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmeskinner.h"
#include "movieobjects/dmedag.h"
#include "movieobjects/dmemodel.h"
#include "mathlib/mathlib.h"




//-----------------------------------------------------------------------------
//  Class for a unit Super Ellipsoid at the origin
//-----------------------------------------------------------------------------
class CSuperEllipsoid
{
public:
	CSuperEllipsoid(
		double powerXZ = 1.0,
		double powerY = 1.0 );

protected:

	static const double s_dEpsilon;

	double m_powerXZ;
	double m_powerY;
	int m_nMaxIterations;
};


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeSkinnerVolume, CDmeSkinnerVolume );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSkinnerVolume::OnConstruction()
{
	m_mMatrix.Init( this, "matrix" );
	m_flStrength.Init( this, "strength", 1.0f );
	m_flFalloff.Init( this, "falloff", 0.0f );
	m_nFalloffType.Init( this, "falloffType", FT_LINEAR );
	m_flPowerY.InitAndSet( this, "powerY", 1.0f );
	m_flPowerXZ.InitAndSet( this, "powerXZ", 1.0f );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSkinnerVolume::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeSkinnerJoint, CDmeSkinnerJoint );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSkinnerJoint::OnConstruction()
{
	m_mBindWorldMatrix.Init( this, "bindWorldMatrix" );
	m_eVolumeList.Init( this, "volumeList" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSkinnerJoint::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeSkinner, CDmeSkinner );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSkinner::OnConstruction()
{
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeSkinner::OnDestruction()
{
}



//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CJointWeight
{
public:
	int m_nJointDataIndex;
	float m_flWeight;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CVertexWeight
{
public:
	CVertexWeight( int nWeightsPerVertex )
	: m_nWeightPerVertex( MAX( 0, nWeightsPerVertex ) )
	{
	}

	int m_nWeightPerVertex;

	CUtlVector< CJointWeight > m_weights;	// Sorted Weight List

	static int SortWeights( const CJointWeight *pLhs, const CJointWeight *pRhs );

	int Count() const {
		return MIN( m_nWeightPerVertex, m_weights.Count() );
	}

	void Reset()
	{
		m_weights.Purge();
	}

	float ComputeTotalWeight() const
	{
		float flTotalWeight = 0.0f;

		const int nWeightCount = Count();

		for ( int i = 0; i < nWeightCount; ++i )
		{
			flTotalWeight += m_weights[ i ].m_flWeight;
		}

		return flTotalWeight;
	}

	float GetWeight( int nWeightIndex ) const
	{
		if ( nWeightIndex < m_weights.Count() )
			return m_weights[ nWeightIndex ].m_flWeight;

		return 0.0f;
	}

	int GetJointIndex( int nWeightIndex ) const
	{
		if ( nWeightIndex < m_weights.Count() )
			return m_weights[ nWeightIndex ].m_nJointDataIndex;

		return 0;
	}
};




//-----------------------------------------------------------------------------
// Sort them from highest to lowest
//-----------------------------------------------------------------------------
int CVertexWeight::SortWeights( const CJointWeight *pLhs, const CJointWeight *pRhs )
{
	if ( pLhs->m_flWeight < pRhs->m_flWeight )
		return 1;

	if ( pLhs->m_flWeight > pRhs->m_flWeight )
		return -1;

	return 0;
}




//====== Copyright (c) 1996-2009, Valve Corporation, All rights reserved. =====
//
// TODO: This belongs in a lib somewhere, comes from sdktools/maya/vsSkinner
//
//=============================================================================


//-----------------------------------------------------------------------------
// Statics
//-----------------------------------------------------------------------------
const double CSuperEllipsoid::s_dEpsilon = 1.0e-6;


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
CSuperEllipsoid::CSuperEllipsoid(
	double powerXZ /* = 1.0 */,
	double powerY /* = 1.0 */ )
: m_powerXZ( powerXZ )
, m_powerY( powerY )
, m_nMaxIterations( 100 )
{
}







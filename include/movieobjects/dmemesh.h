//====== Copyright (c) 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing a mesh
//
//=============================================================================

#pragma once



#include "movieobjects/dmeshape.h"
#include "movieobjects/dmevertexdata.h"
#include "mathlib/vector.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmElement;
class CDmeFaceSet;
class CDmeVertexData;
class IMaterial;
class IMorph;
class IMesh;
class Vector;
class Vector4D;
class Color;
class CDmeDag;
class CMeshBuilder;
class CDmeCombinationOperator;
class CDmeSingleIndexedComponent;
class CDmMeshComp;



//-----------------------------------------------------------------------------
// Mesh weights
//-----------------------------------------------------------------------------
enum MeshDeltaWeightType_t
{
	MESH_DELTA_WEIGHT_FIRST = 0,

	MESH_DELTA_WEIGHT_NORMAL = 0,
	MESH_DELTA_WEIGHT_LAGGED,

	MESH_DELTA_WEIGHT_TYPE_COUNT,
};


//-----------------------------------------------------------------------------
// Mesh representation
//-----------------------------------------------------------------------------
class CDmeMesh : public CDmeShape
{
DEFINE_ELEMENT( CDmeMesh, CDmeShape );

public:
	// resolve internal data from changed attributes
void OnAttributeChanged( CDmAttribute *pAttribute ) override;

	void GetBoundingSphere( Vector &c, float &r, CDmeVertexData *pPassedBase, CDmeSingleIndexedComponent *pPassedSelection ) const;

void GetBoundingSphere( Vector &c, float &r ) const override { return GetBoundingSphere( c, r, nullptr, nullptr); }

	void GetBoundingBox( Vector &min, Vector &max, CDmeVertexData *pPassedBase /* = NULL */, CDmeSingleIndexedComponent *pPassedSelection /* = NULL */ ) const;

void GetBoundingBox( Vector &min, Vector &max ) const override { return GetBoundingBox( min, max, nullptr, nullptr); }

	// accessors
	int FaceSetCount() const;
	CDmeFaceSet *GetFaceSet( int nFaceSetIndex );

	// Base states
	int BaseStateCount() const;

	CDmeVertexData *GetBaseState( int nBaseIndex ) const;

	CDmeVertexData *FindBaseState( const char *pStateName ) const;


	// Selects a particular base state to be current state
	CDmeVertexData *GetCurrentBaseState();
	const CDmeVertexData *GetCurrentBaseState() const;

	CDmeVertexData *GetBindBaseState();

	// Compute triangulated indices
	void ComputeTriangulatedIndices(const CDmeVertexData *pBaseState, const CDmeFaceSet *pFaceSet, uint32_t nFirstIndex, int *indicesOut) const;

	// Delta states
	int DeltaStateCount() const;
	CDmeVertexDeltaData *GetDeltaState( int nDeltaIndex ) const;
	CDmeVertexDeltaData *FindDeltaState( const char *pDeltaName, bool bSortDeltaName = true ) const;
	CDmeVertexDeltaData *FindDeltaState( const std::string_view deltaName, bool bSortDeltaName = true ) const;
	int FindDeltaStateIndex( const char *pDeltaName, bool bSortDeltaName = true ) const;
	int FindDeltaStateIndex( std::string_view deltaName, bool bSortDeltaName = true ) const;

	enum SelectHalfType_t
	{
		kLeft,
		kRight
	};

	enum Falloff_t
	{
		STRAIGHT = 0,
		LINEAR = STRAIGHT,
		BELL,
		SMOOTH = BELL,
		SPIKE,
		DOME
	};

	enum Distance_t
	{
		DIST_ABSOLUTE = 0,
		DIST_RELATIVE,
		DIST_DEFAULT
	};

	CDmeSingleIndexedComponent *FeatherSelection( float falloffDistance, Falloff_t falloffType, Distance_t distanceType, CDmeSingleIndexedComponent *pSelection, CDmMeshComp *pPassedMeshComp );

	struct DeltaComputation_t 
	{
		int m_nDeltaIndex;
		int m_nDimensionality;
		CUtlVector<int> m_DependentDeltas;
	};

	// Computes adds a delta to the passed data weighted by the passed weight
	template < class T_t > void AddDelta(
		const CDmeVertexDeltaData *pDelta, T_t *pFullData, int nFullData, FieldIndex_t fieldIndex, float weight = 1.0f, const CDmeSingleIndexedComponent *pMask = nullptr);

	template < class T_t > void AddDelta(
		const CDmeVertexDeltaData *pDelta, T_t *pFullData, int nFullData, CDmeVertexData::StandardFields_t standardField, float weight = 1.0f, const CDmeSingleIndexedComponent *pMask = nullptr);

	template < class T_t >
	bool SetBaseDataToDeltas( CDmeVertexData *pBase, CDmeVertexData::StandardFields_t nStandardField, CDmrArrayConst< T_t > &srcData, CDmrArray< T_t > &dstData, bool bDoStereo, bool bDoLag );

	template < class T_t > static int GenerateCompleteDataForDelta( const CDmeVertexDeltaData *pDelta, T_t *pFullData, int nFullData, CDmeVertexData::StandardFields_t standardField );

	// Normalizes all normals
	void NormalizeNormals();

	// Collapses redundant normals in the model
	// flNormalBlend is the maximum difference in the dot product between two normals to consider them
	// to be the same normal, a value of cos( DEG2RAD( 2.0 ) ) is the default studiomdl uses, for example
	void CollapseRedundantNormals( float flNormalBlend );

	// SWIG errors on the parsing of something in the private section of DmeMesh, it isn't exposed by SWIG anyway, so have SWIG ignore it
#ifndef SWIG

private:
	friend class CDmMeshComp;

	struct FaceSet_t
	{
		FaceSet_t() : m_bBuilt(false) {}
		IMesh *m_pMesh;
		bool m_bBuilt;
	};

	struct Triangle_t
	{
		int m_nIndex[3];
		Vector m_vecTangentS;
		Vector m_vecTangentT;
	};

	struct RenderVertexDelta_t
	{
		Vector m_vecDeltaPosition;
		Vector m_vecDeltaNormal;
		Vector2D m_vecDeltaUV;
		Vector4D m_vecDeltaColor;
		float m_flDeltaWrinkle;
	};


	// Adds deltas into a delta mesh
	template< class T > bool AddVertexDelta( CDmeVertexData *pBaseState, void *pVertexData, int nStride, CDmeVertexDataBase::StandardFields_t fieldId, int nIndex, bool bDoLag );
	template< class T > bool AddStereoVertexDelta( CDmeVertexData *pBaseState, void *pVertexData, int nStride, CDmeVertexDataBase::StandardFields_t fieldId, int nIndex, bool bDoLag );

// Initializes the normal material
	// Initializes the wireframe material

	// Splits the passed delta state name on '_' and finds all of the control Delta states which make up the name
	bool GetControlDeltaIndices( CDmeVertexDeltaData *pDeltaState, CUtlVector< int > &controlDeltaIndices ) const;

	// Splits the passed delta state name on '_' and finds all of the control Delta states which make up the name
	bool GetControlDeltaIndices( const char *pDeltaStateName, CUtlVector< int > &controlDeltaIndices ) const;

	template < class T_t > void AddCorrectedDelta(
		CDmrArray< T_t > &baseDataArray,
		const CUtlVector< int > &baseIndices,
		const DeltaComputation_t &deltaComputation,
		const char *pFieldName,
		float weight = 1.0f,
		const CDmeSingleIndexedComponent *pMask = nullptr);

	template < class T_t > void AddCorrectedDelta(
		CUtlVector< T_t > &baseData,
		const CUtlVector< int > &baseIndices,
		const DeltaComputation_t &deltaComputation,
		const char *pFieldName,
		float weight = 1.0f,
		const CDmeSingleIndexedComponent *pMask = nullptr);

	template < class T_t > void AddRawDelta(
		CDmeVertexDeltaData *pDelta,
		CDmrArray< T_t > &baseDataArray,
		FieldIndex_t nDeltaFieldIndex,
		float weight = 1.0f,
		const CDmeSingleIndexedComponent *pMask = nullptr);

	template < class T_t > void AddRawDelta(
		CDmeVertexDeltaData *pDelta,
		CUtlVector< T_t > &baseData,
		FieldIndex_t nDeltaFieldIndex,
		float weight = 1.0f,
		const CDmeSingleIndexedComponent *pMask = nullptr);

	friend class CDmxEdit;

	// CFalloff functors map [0, 1] values to [0, 1] values
	template < int T >
	class CFalloff
	{
	public:
		virtual inline float operator()( float x ) { return 1 - x; }
	};

	// Feather's the selection by a specified amount, creates a new CDmeSingleIndexedComponent or NULL if error
	template < int T >
	CDmeSingleIndexedComponent *FeatherSelection( float fFalloffDistance, Distance_t distanceType, CDmeSingleIndexedComponent *pSelection, CDmMeshComp *pPassedMeshComp );

	bool CreateDeltaFieldFromBaseField( CDmeVertexData::StandardFields_t nStandardFieldIndex, const CDmrArrayConst< float > &baseArray, const CDmrArrayConst< float > &bindArray, CDmeVertexDeltaData *pDelta );
	bool CreateDeltaFieldFromBaseField( CDmeVertexData::StandardFields_t nStandardFieldIndex, const CDmrArrayConst< Vector2D > &baseArray, const CDmrArrayConst< Vector2D > &bindArray, CDmeVertexDeltaData *pDelta );
	bool CreateDeltaFieldFromBaseField( CDmeVertexData::StandardFields_t nStandardFieldIndex, const CDmrArrayConst< Vector > &baseArray, const CDmrArrayConst< Vector > &bindArray, CDmeVertexDeltaData *pDelta );

	template< class T_t > bool InterpMaskedData(
		CDmrArray< T_t > &aData,
		const CUtlVector< T_t > &bData,
		float weight,
		const CDmeSingleIndexedComponent *pMask ) const;

	// Interpolate between the current state and the specified delta by the specified percentage masked by the selection
	bool InterpMaskedData(
		CDmeVertexData *paData,
		const CDmeVertexData *pbData,
		float weight,
		const CDmeSingleIndexedComponent *pMask ) const;

	// Find the closest vertex in the specified selection to the passed vertex in the specified base state, if the passed base state is NULL is the current base state
	int ClosestSelectedVertex( int vIndex, CDmeSingleIndexedComponent *pSelection, const CDmeVertexData *pPassedBase = nullptr) const;

	CDmaElement< CDmeVertexData > m_BindBaseState;
	CDmaElement< CDmeVertexData > m_CurrentBaseState;
	CDmaElementArray< CDmeVertexData > m_BaseStates;
	CDmaElementArray< CDmeVertexDeltaData > m_DeltaStates;
	CDmaElementArray< CDmeFaceSet > m_FaceSets;

	// x is left value, y is right value. If the delta state isn't split, they are the same value
	CDmaArray<Vector2D> m_DeltaStateWeights[MESH_DELTA_WEIGHT_TYPE_COUNT];

	// Cached-off map of fields->
	CUtlVector< FaceSet_t > m_hwFaceSets;
	
	friend class CDmeMeshRenderInfo;

#endif // #ifndef SWIG
};

// Explicit specializations have to live at namespace scope (GCC/Clang do not accept them inside the class)
template<>
class CDmeMesh::CFalloff< CDmeMesh::LINEAR >
{
public:
	virtual inline float operator()( float x ) { return 1 - x; }
};

template<>
class CDmeMesh::CFalloff< CDmeMesh::SMOOTH >
{
public:
	virtual inline float operator()( float x ) {
		return ( cosf( x * M_PI ) + 1.0f ) / 2.0f;
	}
};

template<>
class CDmeMesh::CFalloff< CDmeMesh::DOME >
{
public:
	virtual inline float operator()( float x ) {
		return ( cosf( x * M_PI / 2.0 ) );
	}
};

template<>
class CDmeMesh::CFalloff< CDmeMesh::SPIKE >
{
public:
	virtual inline float operator()( float x ) {
		return ( 1.0f - cosf( ( 1.0f - x ) * M_PI / 2.0 ) );
	}
};


//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
inline int CDmeMesh::BaseStateCount() const
{
	return m_BaseStates.Count();
}


inline CDmeVertexData *CDmeMesh::GetBaseState( int nBaseIndex ) const
{
	return m_BaseStates[ nBaseIndex ];
}



//-----------------------------------------------------------------------------
// Helper class to deal with software skinning 
//-----------------------------------------------------------------------------
class CDmeMeshRenderInfo
{
public:
	CDmeMeshRenderInfo( CDmeVertexData *pBaseState );


	inline bool HasPositionData() const { return m_bHasPositionData; }
	inline bool HasNormalData() const { return m_bHasNormalData; }
	inline bool HasTangentData() const { return m_bHasTangentData; }
private:
	const CUtlVector<int>& m_PositionIndices;
	const CUtlVector<Vector>& m_PositionData;
	const CUtlVector<int>& m_NormalIndices;
	const CUtlVector<Vector>& m_NormalData;
	const CUtlVector<int>& m_TangentIndices;
	const CUtlVector<Vector4D>& m_TangentData;
	const CDmeVertexData *m_pBaseState;
	int m_nJointCount;
	bool m_bHasPositionData;
	bool m_bHasNormalData;
	bool m_bHasTangentData;
	bool m_bHasSkinningData;
};



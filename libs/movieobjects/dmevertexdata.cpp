//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "movieobjects/dmevertexdata.h"
#include <limits.h>

#include "datamodel/dmelementfactoryhelper.h"
#include <algorithm>




//-----------------------------------------------------------------------------
// Standard vertex fields
//-----------------------------------------------------------------------------
static const char *const g_pStandardFieldNames[] =
{
	"positions",
	"normals",
	"tangents",
	"textureCoordinates",
	"colors",
	"jointWeights",
	"jointIndices",
	"balance",
	"speed",
	"wrinkle",
	"weight",
	"cloth_enable",
};

static DmAttributeType_t g_pStandardFieldTypes[] =
{
	AT_VECTOR3_ARRAY,
	AT_VECTOR3_ARRAY,
	AT_VECTOR4_ARRAY,
	AT_VECTOR2_ARRAY,
	AT_COLOR_ARRAY,
	AT_FLOAT_ARRAY,
	AT_INT_ARRAY,
	AT_FLOAT_ARRAY,
	AT_FLOAT_ARRAY,
	AT_FLOAT_ARRAY,
	AT_FLOAT_ARRAY,
	AT_FLOAT_ARRAY,
};



//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeVertexDataBase, CDmeVertexDataBase );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::OnConstruction()
{
	m_nVertexCount = 0;
	memset( m_pStandardFieldIndex, 0xFF, sizeof(m_pStandardFieldIndex) );
	m_VertexFormat.Init( this, "vertexFormat" );

	m_nJointCount.Init( this, "jointCount" );
	m_bFlipVCoordinates.Init( this, "flipVCoordinates" );
}

void CDmeVertexDataBase::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// Updates info for fast lookups for well-known fields
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::UpdateStandardFieldInfo( int nFieldIndex, const char *pFieldName, DmAttributeType_t attrType )
{
	COMPILE_TIME_ASSERT( ARRAYSIZE(g_pStandardFieldNames) == STANDARD_FIELD_COUNT );
	COMPILE_TIME_ASSERT( ARRAYSIZE(g_pStandardFieldTypes) == STANDARD_FIELD_COUNT );

	for ( int i = 0; i < STANDARD_FIELD_COUNT; ++i )
	{
		if ( !V_stricmp( pFieldName, g_pStandardFieldNames[i] ) )
		{
			if ( attrType != g_pStandardFieldTypes[i] )
			{
				Warning( "Standard field {} has incorrect attribute type!\n", pFieldName );
				return;
			}
			m_pStandardFieldIndex[i] = nFieldIndex;
			break;
		}
	}

	if ( m_pStandardFieldIndex[ FIELD_CLOTH_ENABLE ] < 0 && !V_stricmp( pFieldName, "cloth_enable" ) && attrType == g_pStandardFieldTypes[ FIELD_CLOTH_ENABLE ] )
	{
		m_pStandardFieldIndex[ FIELD_CLOTH_ENABLE ] = nFieldIndex;
	}
}


//-----------------------------------------------------------------------------
// Computes information about how to find particular fields
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::ComputeFieldInfo()
{
	// Clear existing field info, 
	// but keep the old names around so field indices remain constant
	int nCurrentCount = m_FieldInfo.size();
	for ( int i = 0; i < nCurrentCount; ++i )
	{
		m_FieldInfo[i].m_pIndexData = nullptr;
		m_FieldInfo[i].m_pVertexData = nullptr;
	}

	CUtlVectorFixedGrowable< char, 256 > indicesName;

	// FIXME: Want to maintain field indices as constants for all time 
	int nFieldCount = m_VertexFormat.size();
	for ( int i = 0; i < nFieldCount; ++i )
	{
		const char *pFieldName = m_VertexFormat[i];
		int nLen = Q_strlen( pFieldName ) + 21;
		indicesName.EnsureCount( nLen );
		Q_snprintf( indicesName.data(), nLen, "%sIndices", pFieldName );

		CDmAttribute *pVerticesArray = GetAttribute( pFieldName );
		if ( !pVerticesArray || !IsArrayType( pVerticesArray->GetType() ) )
			continue;

		CDmAttribute *pIndicesArray = nullptr;
		if ( Q_stricmp( pFieldName, g_pStandardFieldNames[FIELD_JOINT_WEIGHTS] ) &&
			Q_stricmp( pFieldName, g_pStandardFieldNames[FIELD_JOINT_INDICES] ) )
		{
			pIndicesArray = GetAttribute( indicesName.data() );
			if ( !pIndicesArray || pIndicesArray->GetType() != AT_INT_ARRAY )
				continue;
		}

		FieldIndex_t nFieldIndex = FindFieldIndex( pFieldName );
		if ( nFieldIndex < 0 )
		{
			nFieldIndex = m_FieldInfo.AddToTail();
			m_FieldInfo[nFieldIndex].m_Name = pFieldName;
			m_FieldInfo[nFieldIndex].m_bInverseMapDirty = true;
			UpdateStandardFieldInfo( nFieldIndex, pFieldName, pVerticesArray->GetType() );
		}
		m_FieldInfo[nFieldIndex].m_pVertexData = pVerticesArray;
		m_FieldInfo[nFieldIndex].m_pIndexData = pIndicesArray;
	}
}


//-----------------------------------------------------------------------------
// Computes the vertex count ( min of the index buffers )
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::ComputeVertexCount()
{
	int nCount = m_FieldInfo.size();
	if ( nCount == 0 )
	{
		m_nVertexCount = 0;
		return;
	}

	m_nVertexCount = INT_MAX;
	for ( int i = 0; i < nCount; ++i )
	{
		if ( !m_FieldInfo[i].m_pIndexData )
			continue;

		CDmrGenericArray array( m_FieldInfo[i].m_pIndexData );
		int nFieldCount = array.Count();
		if ( nFieldCount < m_nVertexCount )
		{
			m_nVertexCount = nFieldCount; 
		}
	}
}


//-----------------------------------------------------------------------------
// resolve internal data from changed attributes
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::Resolve()
{
	BaseClass::Resolve();

	if ( m_VertexFormat.IsDirty() )
	{
		ComputeFieldInfo();
	}

	if ( !IsVertexDeltaData() )
	{
		ComputeVertexCount();
	}

	// Mark inverse map dirty if necessary
	int nCount = m_FieldInfo.size();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( m_FieldInfo[i].m_pIndexData && m_FieldInfo[i].m_pIndexData->IsFlagSet( FATTRIB_DIRTY ) )
		{
			m_FieldInfo[i].m_bInverseMapDirty = true;
		}
	}
}


//-----------------------------------------------------------------------------
// Returns indices into the various fields
//-----------------------------------------------------------------------------
int CDmeVertexDataBase::GetPositionIndex( int nVertexIndex ) const
{
	return GetFieldIndex( nVertexIndex, FIELD_POSITION );
}

int CDmeVertexDataBase::GetNormalIndex( int nVertexIndex ) const
{
	return GetFieldIndex( nVertexIndex, FIELD_NORMAL );
}

int CDmeVertexDataBase::GetTexCoordIndex( int nVertexIndex ) const
{
	return GetFieldIndex( nVertexIndex, FIELD_TEXCOORD );
}


//-----------------------------------------------------------------------------
// Vertex accessors
//-----------------------------------------------------------------------------
const Vector& CDmeVertexDataBase::GetPosition( int nIndex ) const
{
	Assert( IsVertexDeltaData() || nIndex < m_nVertexCount );
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[FIELD_POSITION];
	if ( nFieldIndex < 0 )
		return vec3_origin;

	CDmrArrayConst<int> indices( GetIndexData( nFieldIndex ) );
	CDmrArrayConst<Vector> vertexData( GetVertexData( nFieldIndex ) );
	return vertexData[ indices[nIndex] ];
}



static Color s_Black( 0, 0, 0, 255 );
const Color& CDmeVertexDataBase::GetColor( int nIndex ) const
{
	Assert( IsVertexDeltaData() || nIndex < m_nVertexCount );
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[FIELD_COLOR];
	if ( nFieldIndex < 0 )
		return s_Black;

	CDmrArrayConst<int> indices = GetIndexData( nFieldIndex );
	CDmrArrayConst<Color> vertexData = GetVertexData( nFieldIndex );
	return vertexData[ indices[ nIndex ] ];
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
float CDmeVertexDataBase::GetWeight( int nIndex ) const
{
	Assert( IsVertexDeltaData() || nIndex < m_nVertexCount );
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[FIELD_WEIGHT];
	if ( nFieldIndex < 0 )
		return 1.0f;

	CDmrArrayConst<int> indices = GetIndexData( nFieldIndex );
	CDmrArrayConst<float> vertexData = GetVertexData( nFieldIndex );
	return vertexData[ indices[ nIndex ] ];
}


//-----------------------------------------------------------------------------
// Adds a field to the vertex format
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::FindOrAddVertexField( const char *pFieldName )
{
	int i;
	int nFormatCount = m_VertexFormat.size();
	for ( i = 0; i < nFormatCount; ++i )
	{
		if ( !Q_stricmp( pFieldName, m_VertexFormat[i] ) )
			return;
	}
	m_VertexFormat.AddToTail( pFieldName );
}


//-----------------------------------------------------------------------------
// Returns the field index of a particular field
//-----------------------------------------------------------------------------
FieldIndex_t CDmeVertexDataBase::CreateField( const char *pFieldName, DmAttributeType_t type )
{
	Assert( Q_stricmp( pFieldName, g_pStandardFieldNames[FIELD_JOINT_WEIGHTS] ) );
	Assert( Q_stricmp( pFieldName, g_pStandardFieldNames[FIELD_JOINT_INDICES] ) );
	if ( !Q_stricmp( pFieldName, g_pStandardFieldNames[FIELD_JOINT_WEIGHTS] ) ||
		!Q_stricmp( pFieldName, g_pStandardFieldNames[FIELD_JOINT_INDICES] ) )
	{
		return -1;
	}

	AddAttribute( pFieldName, type );

	int nLen = Q_strlen( pFieldName ) + 21;
	char *pIndicesName = (char*)_alloca( nLen );
	Q_snprintf( pIndicesName, nLen, "%sIndices", pFieldName );
	AddAttribute( pIndicesName, AT_INT_ARRAY );

	FindOrAddVertexField( pFieldName );
	 
	// FIXME: Not hugely efficient, is there a better way of doing this?
	// Necessary to return a field index for the name
	ComputeFieldInfo();
	FieldIndex_t nFieldIndex = FindFieldIndex( pFieldName );
	if ( !IsVertexDeltaData() && m_nVertexCount > 0 )
	{
		CDmrArray<int> indices( GetIndexData( nFieldIndex ) );
		indices.EnsureCount( m_nVertexCount );
	}
	return nFieldIndex;
}


//-----------------------------------------------------------------------------
// Creates a field given a file ID
//-----------------------------------------------------------------------------
FieldIndex_t CDmeVertexDataBase::CreateField( StandardFields_t fieldId )
{
	return CreateField( g_pStandardFieldNames[fieldId], g_pStandardFieldTypes[fieldId] );
}



//-----------------------------------------------------------------------------
// Adds a new vertex; creates a new entry in all vertex data fields
// Returns the vertex index
//-----------------------------------------------------------------------------
int CDmeVertexDataBase::AddVertexData( FieldIndex_t nFieldIndex, int nCount )
{
	CDmrGenericArray array( m_FieldInfo[nFieldIndex].m_pVertexData );
	int nDataCount = array.Count();
	array.EnsureCount( nDataCount + nCount );

	// DmeMeshDeltaData must have the same number of vertices + indices
	if ( IsVertexDeltaData() )
	{
		CDmrArray<int> indices( GetIndexData( nFieldIndex ) );
		Assert( nDataCount == indices.Count() );
		indices.EnsureCount( nDataCount + nCount );
	}

	return nDataCount;
}


//-----------------------------------------------------------------------------
// Sets vertex data
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::SetVertexData( FieldIndex_t nFieldIndex, int nFirstVertex, int nCount, DmAttributeType_t valueType, const void *pData )
{
	CDmrGenericArray array( m_FieldInfo[nFieldIndex].m_pVertexData );
	Assert(	nFirstVertex + nCount <= array.Count() );
	array.SetMultiple( nFirstVertex, nCount, valueType, pData );
}

void CDmeVertexDataBase::SetVertexIndices( FieldIndex_t nFieldIndex, int nFirstIndex, int nCount, const int *pIndices )
{
	CDmrArray<int> array( GetIndexData( nFieldIndex ) );
	Assert(	nFirstIndex + nCount <= array.Count() );
	array.SetMultiple( nFirstIndex, nCount, pIndices );
}


//-----------------------------------------------------------------------------
// Removes all vertex data associated with a particular field
//-----------------------------------------------------------------------------
void CDmeVertexDataBase::RemoveAllVertexData( FieldIndex_t nFieldIndex )
{
	CDmrGenericArray array( m_FieldInfo[nFieldIndex].m_pVertexData );
	array.RemoveAll();
	if ( IsVertexDeltaData() )
	{
		CDmrArray<int> array( m_FieldInfo[nFieldIndex].m_pIndexData );
		array.RemoveAll();
	}
}


//-----------------------------------------------------------------------------
// Returns the field index of a particular field
//-----------------------------------------------------------------------------
FieldIndex_t CDmeVertexDataBase::FindFieldIndex( const char *pFieldName ) const
{
	int nCount = m_FieldInfo.size();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( !Q_stricmp( m_FieldInfo[i].m_Name, pFieldName ) )
			return i;
	}
	return -1;
}


//-----------------------------------------------------------------------------
// Returns well-known vertex data
//-----------------------------------------------------------------------------
static const std::vector<Vector4D> s_emptyVector4D;
static const std::vector<Vector> s_emptyVector;
static const std::vector<Vector2D> s_emptyVector2D;
static const std::vector<float> s_emptyFloat;

const std::vector<Vector> &CDmeVertexDataBase::GetPositionData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_POSITION ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyVector;

	CDmrArrayConst<Vector> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}

const std::vector<Vector> &CDmeVertexDataBase::GetNormalData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_NORMAL ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyVector;

	CDmrArrayConst<Vector> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}

const std::vector<Vector4D> &CDmeVertexDataBase::GetTangentData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_TANGENT ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyVector4D;

	CDmrArrayConst<Vector4D> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}

const std::vector<Vector2D> &CDmeVertexDataBase::GetTextureCoordData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_TEXCOORD ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyVector2D;

	CDmrArrayConst<Vector2D> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}

const float *CDmeVertexDataBase::GetJointWeightData( int nDataIndex ) const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[FIELD_JOINT_WEIGHTS];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return nullptr;

	CDmrArrayConst<float> vertexData = GetVertexData( nFieldIndex );
	return &vertexData[ nDataIndex * m_nJointCount ];
}

const int *CDmeVertexDataBase::GetJointIndexData( int nDataIndex ) const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[FIELD_JOINT_INDICES];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return nullptr;

	CDmrArrayConst<int> vertexData = GetVertexData( nFieldIndex );
	return &vertexData.at( nDataIndex * m_nJointCount );
}

const std::vector<float> &CDmeVertexDataBase::GetBalanceData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_BALANCE ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyFloat;

	CDmrArrayConst<float> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}

const std::vector<float> &CDmeVertexDataBase::GetMorphSpeedData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_MORPH_SPEED ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyFloat;

	CDmrArrayConst<float> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}

const std::vector<float> &CDmeVertexDataBase::GetWrinkleData() const
{
	FieldIndex_t nFieldIndex = m_pStandardFieldIndex[ FIELD_WRINKLE ];
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyFloat;

	CDmrArrayConst<float> vertexData = GetVertexData( nFieldIndex );
	return vertexData.Get(); 
}


//-----------------------------------------------------------------------------
// Returns well-known index data
//-----------------------------------------------------------------------------
static const std::vector<int> s_emptyIndices;
const std::vector<int> &CDmeVertexDataBase::GetVertexIndexData(FieldIndex_t nFieldIndex) const
{
	if ( nFieldIndex < 0 || nFieldIndex >= m_FieldInfo.size() )
		return s_emptyIndices;

	CDmrArrayConst<int> indexData = GetIndexData( nFieldIndex );
	return indexData.Get(); 
}

const std::vector<int> &CDmeVertexDataBase::GetVertexIndexData(StandardFields_t fieldId) const
{
	return GetVertexIndexData( m_pStandardFieldIndex[fieldId] );
}


//-----------------------------------------------------------------------------
// Returns an inverse map from vertex data index to vertex index
//-----------------------------------------------------------------------------
const CUtlVector< int > &CDmeVertexDataBase::FindVertexIndicesFromDataIndex( FieldIndex_t nFieldIndex, int nDataIndex )
{
	if ( nFieldIndex < 0 )
	{
		static const CUtlVector<int> empty;
		return empty;
	}

	FieldInfo_t &info = m_FieldInfo[nFieldIndex]; 
	if ( info.m_bInverseMapDirty )
	{
		CDmrArrayConst<int> array( info.m_pIndexData );
		CDmrGenericArray vertexArray( info.m_pVertexData );

		int nDataCount = vertexArray.Count();
		int nCount = array.size();

		// Clear out the utlvectors
		info.m_InverseMap.RemoveAll();
		info.m_InverseMap.SetCount( nDataCount );

		for ( int i = 0; i < nCount; ++i )
		{
			int nIndex = array[ i ];
			info.m_InverseMap[nIndex].AddToTail( i );
		}
		info.m_bInverseMapDirty = false;
	}

	return info.m_InverseMap[ nDataIndex ];
}

const CUtlVector< int > &CDmeVertexDataBase::FindVertexIndicesFromDataIndex( StandardFields_t fieldId, int nDataIndex )
{
	// NOTE! Wrinkles don't exist in the base state, therefore we use the index to index
	// into the TEXCOORD base state fields instead of the wrinkle fields
	if ( fieldId == FIELD_WRINKLE )
	{
		fieldId = FIELD_TEXCOORD;
	}

	return FindVertexIndicesFromDataIndex( m_pStandardFieldIndex[fieldId], nDataIndex );
}


//-----------------------------------------------------------------------------
// Do we have skinning data?
//-----------------------------------------------------------------------------
bool CDmeVertexDataBase::HasSkinningData() const
{
	if ( m_nJointCount == 0 )
		return false;
	FieldIndex_t nWeightFieldIndex = m_pStandardFieldIndex[FIELD_JOINT_WEIGHTS];
	if ( nWeightFieldIndex < 0 )
		return false;
	FieldIndex_t nIndexFieldIndex = m_pStandardFieldIndex[FIELD_JOINT_INDICES];
	if ( nIndexFieldIndex < 0 )
		return false;

	CDmrArrayConst<float> weightData = GetVertexData( nWeightFieldIndex );
	CDmrArrayConst<int> indexData = GetVertexData( nIndexFieldIndex );
	return ( weightData.size() > 0 && indexData.size() > 0 );
}



//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int CDmeVertexDataBase::FieldCount() const
{
	return m_VertexFormat.size();
}


//-----------------------------------------------------------------------------
// Returns the full fieldname (semanticname$index)
//-----------------------------------------------------------------------------
const char *CDmeVertexDataBase::FieldName( int i ) const
{
	if ( i < 0 || i >= m_VertexFormat.size() )
		return nullptr;

	return m_VertexFormat[ i ];
}




//-----------------------------------------------------------------------------
// Sort models function
//-----------------------------------------------------------------------------
struct TempVertex_t
{
	float m_flBoneWeight;
	int m_nBoneIndex;
};

inline bool TempVertexLessFunc( const TempVertex_t &left, const TempVertex_t &right )
{
	return left.m_nBoneIndex < right.m_nBoneIndex;
}

inline bool WeightLessFunc( const TempVertex_t &left, const TempVertex_t &right )
{
	return left.m_flBoneWeight > right.m_flBoneWeight;
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeVertexData, CDmeVertexData );
IMPLEMENT_ELEMENT_FACTORY( DmeVertexDeltaData, CDmeVertexDeltaData );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeVertexData::OnConstruction()
{
}

void CDmeVertexData::OnDestruction()
{
}

void CDmeVertexDeltaData::OnConstruction()
{
	m_bCorrected.InitAndSet( this, "corrected", false );
	m_bRenderVerts.InitAndSet( this, "renderVerts", false, FATTRIB_DONTSAVE );	// Runtime flag
}

void CDmeVertexDeltaData::OnDestruction()
{
}



//-----------------------------------------------------------------------------
// Computes max positional delta length
//-----------------------------------------------------------------------------
float CDmeVertexDeltaData::ComputeMaxDeflection( )
{
	float flMaxDeflection = 0.0f;

	const auto &pos = GetPositionData();
	int nCount = pos.size();
	for ( int i = 0; i < nCount; ++i )
	{
		float flDeflection = pos[i].Length();
		if ( flMaxDeflection < flDeflection )
		{
			flMaxDeflection = flDeflection;
		}
	}
	return flMaxDeflection;
}


//-----------------------------------------------------------------------------
// Computes wrinkle data from position deltas
//-----------------------------------------------------------------------------
void CDmeVertexDeltaData::GenerateWrinkleDelta(
	CDmeVertexData *pBindState,
	float flScale,
	bool bOverwrite,
	bool bUseNormalForSign /* = false */ )
{
	FieldIndex_t nPosIndex = FindFieldIndex( FIELD_POSITION );
	if ( nPosIndex < 0 )
		return;

	FieldIndex_t nBaseTexCoordIndex = pBindState->FindFieldIndex( FIELD_TEXCOORD );
	if ( nBaseTexCoordIndex < 0 )
		return;

	FieldIndex_t nNormalIndex = pBindState->FindFieldIndex( FIELD_NORMAL );
	if ( bUseNormalForSign && nNormalIndex < 0 )
		return;

	FieldIndex_t nWrinkleIndex = FindFieldIndex( FIELD_WRINKLE );
	if ( nWrinkleIndex < 0 )
	{
		nWrinkleIndex = CreateField( FIELD_WRINKLE );
	}
	else if ( !bOverwrite )
		return;

	RemoveAllVertexData( nWrinkleIndex );
	if ( flScale == 0.0f )
		return;

	const float flMaxDeflection( ComputeMaxDeflection() );
	if ( flMaxDeflection == 0.0f )
		return;

	const double scaledInverseMaxDeflection = static_cast< double >( flScale ) / static_cast< double >( flMaxDeflection );

	const auto &positionIndices = GetVertexIndexData( nPosIndex );
	const auto &pos = GetPositionData();
	const auto &baseTexCoordIndices = pBindState->GetVertexIndexData( nBaseTexCoordIndex );

	CDmrArrayConst<Vector2D> texData( pBindState->GetVertexData( nBaseTexCoordIndex ) );
	int nBaseTexCoordCount = texData.size();
	int nBufSize = ( ( nBaseTexCoordCount + 7 ) >> 3 );
	unsigned char *pUsedBits = (unsigned char*)_alloca( nBufSize );
	memset( pUsedBits, 0, nBufSize );

	const int nCount = pos.size();

	if ( bUseNormalForSign )
	{
		const auto &normalIndices = pBindState->GetVertexIndexData( nNormalIndex );
		const auto &normals = pBindState->GetNormalData();

		for ( int i = 0; i < nCount; ++i )
		{
			float flWrinkleDelta = static_cast< float >( static_cast< double >( pos[i].Length() ) * scaledInverseMaxDeflection );
			Assert( fabs( flWrinkleDelta ) <= fabs( flScale ) );
			float flNegativeWrinkleDelta = -flWrinkleDelta;

			Vector vPosDelta = pos[i];
			vPosDelta.NormalizeInPlace();

			// NOTE: This will produce bad behavior in cases where two positions share the
			// same texcoord, which shouldn't theoretically happen.
			const CUtlVector< int > &baseVerts = pBindState->FindVertexIndicesFromDataIndex( FIELD_POSITION, positionIndices[i] );
			int nBaseVertCount = baseVerts.size();
			for ( int j = 0; j < nBaseVertCount; ++j )
			{
				// See if we have a delta for this texcoord...
				int nTexCoordIndex = baseTexCoordIndices[ baseVerts[j] ];
				if ( pUsedBits[ nTexCoordIndex >> 3 ] & ( 1 << ( nTexCoordIndex & 0x7 ) ) )
					continue;

				pUsedBits[ nTexCoordIndex >> 3 ] |= 1 << ( nTexCoordIndex & 0x7 );

				Vector vNormal = normals[ normalIndices[ baseVerts[j] ] ];
				vNormal.NormalizeInPlace();

				int nDeltaIndex = AddVertexData( nWrinkleIndex, 1 );
				SetVertexIndices( nWrinkleIndex, nDeltaIndex, 1, &nTexCoordIndex );

				if ( DotProduct( vPosDelta, vNormal ) < 0 )
				{
					SetVertexData( nWrinkleIndex, nDeltaIndex, 1, AT_FLOAT, &flNegativeWrinkleDelta );
				}
				else
				{
					SetVertexData( nWrinkleIndex, nDeltaIndex, 1, AT_FLOAT, &flWrinkleDelta );
				}
			}
		}
	}
	else
	{
		for ( int i = 0; i < nCount; ++i )
		{
			float flWrinkleDelta = static_cast< float >( static_cast< double >( pos[i].Length() ) * scaledInverseMaxDeflection );
			Assert( fabs( flWrinkleDelta ) <= fabs( flScale ) );

			// NOTE: This will produce bad behavior in cases where two positions share the
			// same texcoord, which shouldn't theoretically happen.
			const CUtlVector< int > &baseVerts = pBindState->FindVertexIndicesFromDataIndex( FIELD_POSITION, positionIndices[i] );
			int nBaseVertCount = baseVerts.size();
			for ( int j = 0; j < nBaseVertCount; ++j )
			{
				// See if we have a delta for this texcoord...
				int nTexCoordIndex = baseTexCoordIndices[ baseVerts[j] ];
				if ( pUsedBits[ nTexCoordIndex >> 3 ] & ( 1 << ( nTexCoordIndex & 0x7 ) ) )
					continue;

				pUsedBits[ nTexCoordIndex >> 3 ] |= 1 << ( nTexCoordIndex & 0x7 );

				int nDeltaIndex = AddVertexData( nWrinkleIndex, 1 );
				SetVertexIndices( nWrinkleIndex, nDeltaIndex, 1, &nTexCoordIndex );
				SetVertexData( nWrinkleIndex, nDeltaIndex, 1, AT_FLOAT, &flWrinkleDelta );
			}
		}
	}
}




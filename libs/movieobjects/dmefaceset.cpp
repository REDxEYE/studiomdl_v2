//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmefaceset.h"
#include "movieobjects/dmematerial.h"
#include "datamodel/dmelementfactoryhelper.h"




//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeFaceSet, CDmeFaceSet );


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
void CDmeFaceSet::OnConstruction()
{
	m_indices.Init( this, "faces" );
	m_material.Init( this, "material" );
}

void CDmeFaceSet::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// accessors
//-----------------------------------------------------------------------------
CDmeMaterial *CDmeFaceSet::GetMaterial()
{
	return m_material.GetElement();
}



//-----------------------------------------------------------------------------
// Returns the number of triangulated indices
//-----------------------------------------------------------------------------
uint32_t CDmeFaceSet::GetNextPolygonVertexCount(const uint32_t nFirstIndex) const
{
	uint32_t nCurrIndex = nFirstIndex;
	const uint32_t nTotalCount = m_indices.size();
	while( nCurrIndex < nTotalCount )
	{
		if ( m_indices[nCurrIndex] == -1 )
			break;
		++nCurrIndex;
	}

	return nCurrIndex - nFirstIndex;
}






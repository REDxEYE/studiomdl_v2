//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing an abstract shape (ie drawable object)
//
//=============================================================================

#pragma once


#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "tier1/utlvector.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeMaterial;


//-----------------------------------------------------------------------------
// A class representing a face of a polygonal mesh
//-----------------------------------------------------------------------------
class CDmeFaceSet : public CDmElement
{
	DEFINE_ELEMENT( CDmeFaceSet, CDmElement );

public:
	// material accessors
	CDmeMaterial *GetMaterial();

	// Total number of indices in the face set including the -1 end of face designators
	uint32_t NumIndices() const;
	const int *GetIndices() const;
	int GetIndex(uint32_t i) const;

	// Returns the number of vertices in the next polygon
	uint32_t GetNextPolygonVertexCount(uint32_t nFirstIndex) const;

private:
	CDmaArray< int > m_indices;
	CDmaElement< CDmeMaterial > m_material;
};


//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
inline uint32_t CDmeFaceSet::NumIndices() const
{
	return m_indices.Count();
}

inline const int *CDmeFaceSet::GetIndices() const
{
	return m_indices.Base();
}

inline int CDmeFaceSet::GetIndex(uint32_t i) const
{
	return m_indices[i];
}



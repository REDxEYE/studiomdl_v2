//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// The morph operator class - sets morph target weights on meshes
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "movieobjects/dmeoperator.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeMesh;


//-----------------------------------------------------------------------------
// A class representing a camera
//-----------------------------------------------------------------------------
class CDmeMorphOperator : public CDmeOperator
{
	DEFINE_ELEMENT( CDmeMorphOperator, CDmeOperator );

public:
	virtual void Operate();

	// need this until we have the EditApply message queue
	void OnAttributeChanged( CDmAttribute *pAttribute );

	virtual void GetInputAttributes ( CUtlVector< CDmAttribute * > &attrs );
	virtual void GetOutputAttributes( CUtlVector< CDmAttribute * > &attrs );

	// accessors
	uint32_t NumDeltaStateWeights();
	CDmElement *GetDeltaStateWeight( uint32_t i );
	CDmeMesh *GetMesh();

protected:
	CDmaElement< CDmeMesh >    m_mesh;
	CDmaElementArray< CDmElement > m_deltaStateWeights;
	CDmaString                 m_baseStateName;
};



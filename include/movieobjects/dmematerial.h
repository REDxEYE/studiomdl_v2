//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing a material
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif


// Valve includes
#include "datamodel/dmelement.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class IMaterial;


//-----------------------------------------------------------------------------
// A class representing a material
//-----------------------------------------------------------------------------
class CDmeMaterial : public CDmElement
{
	DEFINE_ELEMENT( CDmeMaterial, CDmElement );

public:
	void SetMaterial( const char *pMaterialName );
	const char *GetMaterialName() const;

	virtual void Resolve();

private:
	CDmaString m_mtlName;
};



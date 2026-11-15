//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing a material
//
//=============================================================================

#pragma once



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
	const char *GetMaterialName() const;

    void Resolve() override;

private:
	CDmaString m_mtlName;
};



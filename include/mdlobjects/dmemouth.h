//============ Copyright (c) Valve Corporation, All rights reserved. ==========
//
//=============================================================================

#pragma once

// Valve includes
#include "datamodel/dmelement.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeDag;


//-----------------------------------------------------------------------------
// DmeEyeball
//-----------------------------------------------------------------------------
class CDmeMouth : public CDmElement
{
	DEFINE_ELEMENT( CDmeMouth, CDmElement );

public:
	CDmaVar< int > m_nMouthNumber;
	CDmaString m_sFlexControllerName;
	CDmaString m_sBoneName;
	CDmaVar< Vector > m_vForward;
};



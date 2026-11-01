//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "datamodel/dmelement.h"

class CDmePhonemeMapping : public CDmElement
{
	DEFINE_ELEMENT( CDmePhonemeMapping, CDmElement );

public:
	CDmaString    m_Preset; // "preset"  // map this item to this preset
	CDmaVar< float > m_Weight; // "weight"  // using this weight
};


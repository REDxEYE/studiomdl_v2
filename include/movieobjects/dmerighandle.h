//====== Copyright © 1996-2008, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "datamodel/dmelement.h"
#include "movieobjects/dmedag.h"
#include "movieobjects/dmeoperator.h"

class CDmeRigHandle : public CDmeDag
{
	DEFINE_ELEMENT( CDmeRigHandle, CDmeDag );

public:

};


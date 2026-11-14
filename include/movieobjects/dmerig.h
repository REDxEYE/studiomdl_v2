//====== Copyright © 1996-2009, Valve Corporation, All rights reserved. =======
//
// Purpose: Contains the declaration of the CDmeRig, a dag which groups a set
// of associated constraints and operators together.
//
// The SFM animation set bookkeeping (CDmeRigAnimSetElements, "animSetList") is
// gone; such attributes still load as untyped elements. The class stays so DmeRig
// nodes in a DMX hierarchy remain typed dags (children arrays / constraint targets).
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "movieobjects/dmedag.h"


class CDmeRig : public CDmeDag
{
	DEFINE_ELEMENT( CDmeRig, CDmeDag );
};

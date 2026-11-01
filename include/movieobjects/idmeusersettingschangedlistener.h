//============ Copyright (c) Valve Corporation, All rights reserved. ============

#pragma once

#ifdef _WIN32
#pragma once
#endif

#include "tier1/keyvalues.h"

#define NameKey "name"
#define AttributeKey "attribute"
#define RegistryPathKey "registryPath"

//-----------------------------------------------------------------------------

class IDmeUserSettingsChangedListener
{
public:
	virtual void OnUserSettingsChanged( KeyValues *message ) = 0;
};



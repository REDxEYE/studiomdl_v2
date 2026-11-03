//===== Copyright © 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Startup/shutdown of the datamodel libraries (datamodel, element framework, dmserializers).
//
//===========================================================================//

#include "tier2/tier2dm.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "dmserializers/idmserializers.h"


bool InitDataModel()
{
	// All DME element classes get installed first, so their type symbols exist before the serializers and importers
	InstallDmElementFactories();

	if ( !g_pDataModel->Init() )
		return false;

	if ( !g_pDmElementFramework->Init() )
	{
		g_pDataModel->Shutdown();
		return false;
	}

	if ( !g_pDmSerializers->Init() )
	{
		g_pDmElementFramework->Shutdown();
		g_pDataModel->Shutdown();
		return false;
	}

	return true;
}

void ShutdownDataModel()
{
	g_pDmSerializers->Shutdown();
	g_pDmElementFramework->Shutdown();
	g_pDataModel->Shutdown();
}

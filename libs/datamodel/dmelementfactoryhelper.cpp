//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: Registration of element classes with the datamodel
//
//=============================================================================

#include "datamodel/dmelementfactoryhelper.h"
#include "datamodel.h"


void InstallDmElementFactories()
{
	g_pDataModelImp->InstallElementTypes( DmElementTypeInfo::s_pFirst );
}

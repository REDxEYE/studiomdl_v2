//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmematerial.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects_interfaces.h"

#include "materialsystem/IMaterial.h"
#include "materialsystem/IMaterialSystem.h"
#include "tier2/tier2.h"
#include "datamodel/dmattributevar.h"

// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeMaterial, CDmeMaterial );


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
void CDmeMaterial::OnConstruction()
{
	m_mtlName.Init( this, "mtlName" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMaterial::OnDestruction()
{
}


//-----------------------------------------------------------------------------
// resolve
//-----------------------------------------------------------------------------
void CDmeMaterial::Resolve()
{
	BaseClass::Resolve();

}


//-----------------------------------------------------------------------------
// Sets the material
//-----------------------------------------------------------------------------
void CDmeMaterial::SetMaterial( const char *pMaterialName )
{
	m_mtlName = pMaterialName;
}


//-----------------------------------------------------------------------------
// Returns the material name
//-----------------------------------------------------------------------------
const char *CDmeMaterial::GetMaterialName() const
{
	return m_mtlName;
}
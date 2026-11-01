//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmematerial.h"
#include "datamodel/dmelementfactoryhelper.h"



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
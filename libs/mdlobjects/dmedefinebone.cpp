//====== Copyright � 1996-2008, Valve Corporation, All rights reserved. =====//
//
// Dme representation of QC: $sequence
//
//===========================================================================//


#include "datamodel/dmelementfactoryhelper.h"
#include "mdlobjects/dmedefinebone.h"
#include "mdlobjects/dmedefinebonelist.h"





//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeDefineBone, CDmeDefineBone );


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDefineBone::OnConstruction()
{
	m_Parent.Init( this, "parent" );
	m_Translation.Init( this, "translation" );
	m_Rotation.Init( this, "rotation" );
	m_RealignTranslation.Init( this, "realignTranslation" );
	m_RealignRotation.Init( this, "realignRotation" );
	m_sContentsDescription.Init( this, "contentsDescription" );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDefineBone::OnDestruction()
{
}
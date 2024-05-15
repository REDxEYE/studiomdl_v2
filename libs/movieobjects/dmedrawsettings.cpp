//======= Copyright � 1996-2006, Valve Corporation, All rights reserved. ======
//
// Purpose:
//
//=============================================================================


#include "datamodel/dmelementfactoryhelper.h"
#include "tier1/KeyValues.h"
#include "tier1/utlrbtree.h"
#include "movieobjects/dmedag.h"
#include "movieobjects/dmedrawsettings.h"


// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeDrawSettings, CDmeDrawSettings );


//-----------------------------------------------------------------------------
// Statics
//-----------------------------------------------------------------------------

CUtlRBTree< CUtlSymbolLarge > CDmeDrawSettings::s_KnownDrawableTypes;


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDrawSettings::OnConstruction()
{
	if ( s_KnownDrawableTypes.Count() == 0 )
	{
		BuildKnownDrawableTypes();
	}

	SetDefLessFunc< CUtlRBTree< CUtlSymbolLarge > >( m_NotDrawable );
	m_NotDrawable.RemoveAll();

	m_DrawType.InitAndSet( this, "drawType", static_cast< int >( DRAW_SMOOTH ) );
	m_bBackfaceCulling.InitAndSet( this, "backfaceCulling", true );
	m_bWireframeOnShaded.InitAndSet( this, "wireframeOnShaded", false );
	m_bXRay.InitAndSet( this, "xray", false );
	m_bGrayShade.InitAndSet( this, "grayShade", false );
	m_bNormals.InitAndSet( this, "normals", false );
	m_NormalLength.InitAndSet( this, "normalLength", 1.0 );
	m_Color.InitAndSet( this, "color", Color( 0, 0, 0, 1 ) );
	m_bDeltaHighlight.InitAndSet( this, "highlightDeltas", false );
	m_flHighlightSize.InitAndSet( this, "highlightSize", 1.5f );
	m_cHighlightColor.InitAndSet( this, "highlightColor", Color( 0xff, 0x14, 0x93, 0xff ) );	// Deep Pink

	m_IsAMaterialBound = false;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDrawSettings::OnDestruction()
{
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDrawSettings::Resolve()
{
}





//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CDmeDrawSettings::Drawable( CDmElement *pElement )
{
	return false;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeDrawSettings::BuildKnownDrawableTypes()
{
	SetDefLessFunc< CUtlRBTree< CUtlSymbolLarge > >( s_KnownDrawableTypes );

	s_KnownDrawableTypes.RemoveAll();

	s_KnownDrawableTypes.InsertIfNotFound( g_pDataModel->GetSymbol( "DmeMesh" ) );
	s_KnownDrawableTypes.InsertIfNotFound( g_pDataModel->GetSymbol( "DmeJoint" ) );
	s_KnownDrawableTypes.InsertIfNotFound( g_pDataModel->GetSymbol( "DmeModel" ) );
	s_KnownDrawableTypes.InsertIfNotFound( g_pDataModel->GetSymbol( "DmeAttachment" ) );

	m_NotDrawable.RemoveAll();
}


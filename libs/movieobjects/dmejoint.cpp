//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Dme version of a joint of a skeletal model (gets compiled into a MDL)
//
//=============================================================================
#include "movieobjects/dmejoint.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "tier1/keyvalues.h"

// memdbgon must be the last include file in a .cpp file!!!
// DISABLED #include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeJoint, CDmeJoint );


//-----------------------------------------------------------------------------
// Statics
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeJoint::OnConstruction()
{

}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeJoint::OnDestruction()
{
}

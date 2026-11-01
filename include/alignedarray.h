//========= Copyright c 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#pragma once

#include "mathlib/vector4d.h"
#include "tier1/utlvector.h"

// can't make it with Vector4DAligned for now, because of private copy constructor..
typedef CUtlVector<Vector4D, CUtlMemoryAligned<Vector4D, 16> > CUtlVector_Vector4DAligned;


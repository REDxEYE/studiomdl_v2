//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing an abstract shape (ie drawable object)
//
//=============================================================================

#pragma once


#include "mathlib/mathlib.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "mdlobjects/dmeik.h"
#include "datamodel/dmattributevar.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeTransform;
class CDmeDag;


//-----------------------------------------------------------------------------
// A class representing a camera
//-----------------------------------------------------------------------------
class CDmeShape : public CDmElement
{
	DEFINE_ELEMENT( CDmeShape, CDmElement );

public:
	virtual void GetBoundingSphere( Vector &c, float &r ) const;
	virtual void GetBoundingBox( Vector &min, Vector &max ) const { min.Zero(); max.Zero(); }

protected:
	CDmaVar< bool > m_visible;
};



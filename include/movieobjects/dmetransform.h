//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// A class representing a transform
//
//=============================================================================

#pragma once


#include "datamodel/dmelement.h"

static const char TRANSFORM_POSITION[] = "position";
static const char TRANSFORM_ORIENTATION[] = "orientation";

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
struct matrix3x4_t;
class CDmeDag;

//-----------------------------------------------------------------------------
// A class representing a transformation matrix
//-----------------------------------------------------------------------------
class CDmeTransform : public CDmElement
{
	DEFINE_ELEMENT( CDmeTransform, CDmElement );

public:
    void OnAttributeChanged( CDmAttribute *pAttribute ) override;

	void SetTransform( const matrix3x4_t &transform );
	void GetTransform( matrix3x4_t &transform );

	const Vector &GetPosition() const;
	void SetPosition( const Vector &vecPosition );
	const Quaternion &GetOrientation() const;
	void SetOrientation( const Quaternion &orientation );

	CDmAttribute *GetPositionAttribute();
	CDmAttribute *GetOrientationAttribute();

private:
	CDmaVar<Vector> m_Position;
	CDmaVar<Quaternion> m_Orientation;
};



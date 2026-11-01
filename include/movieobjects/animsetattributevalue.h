//====== Copyright � 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlmap.h"
#include "tier1/strtools.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmAttribute;
#include "datamodel/dmelementhandle.h"


//-----------------------------------------------------------------------------
// AnimationControlType
//-----------------------------------------------------------------------------
enum AnimationControlType_t
{
	ANIM_CONTROL_INVALID = -1,

	ANIM_CONTROL_VALUE = 0,
	ANIM_CONTROL_VALUE_RIGHT,
	ANIM_CONTROL_VALUE_LEFT,
	ANIM_CONTROL_COUNT,

	ANIM_CONTROL_TXFORM_POSITION = ANIM_CONTROL_COUNT,
	ANIM_CONTROL_TXFORM_ORIENTATION,

	ANIM_CONTROL_FULL_COUNT,
	ANIM_CONTROL_TRANSFORM_CONTROL_COUNT = ANIM_CONTROL_FULL_COUNT - ANIM_CONTROL_COUNT,
};


struct AttributeValue_t
{
	AttributeValue_t()
	{
		// Default values
		m_pValue[ANIM_CONTROL_VALUE] = 0.0f;
		m_pValue[ANIM_CONTROL_VALUE_RIGHT] = 0.0f;
		m_pValue[ANIM_CONTROL_VALUE_LEFT] = 0.0f;

		// Default values
		m_Vector.Init();
		m_Quaternion = quat_identity;
	}

	float		m_pValue[ANIM_CONTROL_COUNT];

	Vector		m_Vector;		// ANIM_CONTROL_TXFORM_POSITION
	Quaternion	m_Quaternion;	// ANIM_CONTROL_TXFORM_ORIENTATION
};

struct AnimationControlAttributes_t : public AttributeValue_t
{
	AnimationControlAttributes_t()
	{
		m_pValueAttribute[ANIM_CONTROL_VALUE] = nullptr;
		m_pValueAttribute[ANIM_CONTROL_VALUE_RIGHT] = nullptr;
		m_pValueAttribute[ANIM_CONTROL_VALUE_LEFT] = nullptr;
		m_pValueAttribute[ANIM_CONTROL_TXFORM_POSITION] = nullptr;
		m_pValueAttribute[ANIM_CONTROL_TXFORM_ORIENTATION] = nullptr;

		m_pTimesAttribute[ANIM_CONTROL_VALUE] = nullptr;
		m_pTimesAttribute[ANIM_CONTROL_VALUE_RIGHT] = nullptr;
		m_pTimesAttribute[ANIM_CONTROL_VALUE_LEFT] = nullptr;
		m_pTimesAttribute[ANIM_CONTROL_TXFORM_POSITION] = nullptr;
		m_pTimesAttribute[ANIM_CONTROL_TXFORM_ORIENTATION] = nullptr;
	}

	void Clear()
	{
		// Only works because we are a class
		Q_memset( this, 0, sizeof( *this ) );
		m_Quaternion = quat_identity;
	}

	CDmAttribute* m_pValueAttribute[ ANIM_CONTROL_FULL_COUNT ];
	CDmAttribute* m_pTimesAttribute[ ANIM_CONTROL_FULL_COUNT ];
};

typedef CUtlMap< DmElementHandle_t, AnimationControlAttributes_t, unsigned short > AttributeDict_t;

inline AttributeDict_t *CopyAttributeDict( AttributeDict_t *pSrc )
{
	if ( !pSrc )
		return nullptr;

	AttributeDict_t *pDest = new AttributeDict_t();
	pDest->AccessTree()->CopyFrom( *pSrc->AccessTree() );
	return pDest;
}


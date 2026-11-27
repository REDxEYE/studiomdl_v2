//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Physics-based "jiggle bone" simulation (ported from CS:GO public/jigglebones.h)
//
//=============================================================================//
#pragma once

#include "studio.h"
#include "tier1/utlvector.h"
#include "tier1/utllinkedlist.h"

//-----------------------------------------------------------------------------
/**
 * JiggleData is the instance-specific data for a jiggle bone
 */
struct JiggleData
{
	void Init( int initBone, float currenttime, const Vector &initBasePos, const Vector &initTipPos )
	{
		bone = initBone;

		lastUpdate = currenttime;

		basePos = initBasePos;
		baseLastPos = basePos;
		baseVel.Init();
		baseAccel.Init();

		tipPos = initTipPos;
		tipVel.Init();
		tipAccel.Init();
	}

	int bone;
	int id;

	float lastUpdate;	// caller supplied time of the last update

	Vector basePos;		// position of the base of the jiggle bone
	Vector baseLastPos;
	Vector baseVel;
	Vector baseAccel;

	Vector tipPos;		// position of the tip of the jiggle bone
	Vector tipVel;
	Vector tipAccel;
};

class CJiggleBones
{
public:
	JiggleData *GetJiggleData( int bone, float currenttime, const Vector &initBasePos, const Vector &initTipPos );
	void BuildJiggleTransformations( int boneIndex, float currenttime, const mstudiojigglebone_t *jiggleParams, const matrix3x4a_t &goalMX, matrix3x4a_t &boneMX, bool coordSystemIsFlipped );

	CUtlLinkedList< JiggleData >	m_jiggleBoneState;
};

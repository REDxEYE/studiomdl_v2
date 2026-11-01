//========== Copyright (c) Valve Corporation. All Rights Reserved. ============
#pragma once

#ifdef _WIN32
#pragma once
#endif

#include "resourcefile/resourcefile.h"

#define META( X )
//#define DECLARE_SCHEMA_DATA_CLASS( X )
#define DECLARE_SCHEMA_ENUM( X )


schema enum multiplayerBreak_t
{
	MULTIPLAYER_BREAK_SERVER,
	MULTIPLAYER_BREAK_CLIENT
};
DECLARE_SCHEMA_ENUM( multiplayerBreak_t );


// Runtime class compiled from schema class CPhysPartBreakableData (open file://src\public\mdlobjects\authphysmodel.h)
schema class VpropBreakablePartData_t
{
	TYPEMETA( MNoScatter )
	DECLARE_SCHEMA_DATA_CLASS( VpropBreakablePartData_t );

	bool m_bMotionDisabled; META( MPropertyFriendlyName = "Motion Disabled" );
	bool m_bNoShadows;		META( MPropertyFriendlyName = "Do Not Cast Shadows" );
	int32_t m_nHealth;META( MPropertyFriendlyName = "Health" );
	int32_t m_nFadeTime;META( MPropertyFriendlyName = "Fade Time" );
	int32_t m_nFadeMin;META( MPropertyFriendlyName = "Fade Min Distance" );
	int32_t m_nFadeMax;META( MPropertyFriendlyName = "Fade Max Distance" );
	float32 m_flBurstScale; META( MPropertyFriendlyName = "Burst Scale" );
	float32 m_flBurstRandomize; META( MPropertyFriendlyName = "Burst Randomize" );
	uint32_t m_nSurfaceProp;	META( MPropertyFriendlyName = "Surface Prop Hash" );
	uint32_t m_nCollisionGroupHash; META( MPropertyFriendlyName = "Collision Group Hash" );
};



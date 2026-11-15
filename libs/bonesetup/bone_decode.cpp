//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//


#include "mathlib/mathlib.h"
#include "studiomdl/bone_setup.h"
#include <cstring>



#ifdef CLIENT_DLL
	#include "posedebugger.h"
#endif

#include "bone_utils.h"



//-----------------------------------------------------------------------------
// Purpose: return a sub frame rotation for a single bone
//-----------------------------------------------------------------------------
void ExtractAnimValue( int frame, mstudioanimvalue_t *panimvalue, float scale, float &v1, float &v2 )
{
	if ( !panimvalue )
	{
		v1 = v2 = 0;
		return;
	}

	// Avoids a crash reading off the end of the data
	// There is probably a better long-term solution; Ken is going to look into it.
	if ( ( panimvalue->num.total == 1 ) && ( panimvalue->num.valid == 1 ) )
	{
		v1 = v2 = panimvalue[1].value * scale;
		return;
	}

	int k = frame;

	// find the data list that has the frame
	while (panimvalue->num.total <= k)
	{
		k -= panimvalue->num.total;
		panimvalue += panimvalue->num.valid + 1;
		if ( panimvalue->num.total == 0 )
		{
			Assert( 0 ); // running off the end of the animation stream is bad
			v1 = v2 = 0;
			return;
		}
	}
	if (panimvalue->num.valid > k)
	{
		// has valid animation data
		v1 = panimvalue[k+1].value * scale;

		if (panimvalue->num.valid > k + 1)
		{
			// has valid animation blend data
			v2 = panimvalue[k+2].value * scale;
		}
		else
		{
			if (panimvalue->num.total > k + 1)
			{
				// data repeats, no blend
				v2 = v1;
			}
			else
			{
				// pull blend from first data block in next list
				v2 = panimvalue[panimvalue->num.valid+2].value * scale;
			}
		}
	}
	else
	{
		// get last valid data block
		v1 = panimvalue[panimvalue->num.valid].value * scale;
		if (panimvalue->num.total > k + 1)
		{
			// data repeats, no blend
			v2 = v1;
		}
		else
		{
			// pull blend from first data block in next list
			v2 = panimvalue[panimvalue->num.valid + 2].value * scale;
		}
	}
}


void ExtractAnimValue( int frame, mstudioanimvalue_t *panimvalue, float scale, float &v1 )
{
	if ( !panimvalue )
	{
		v1 = 0;
		return;
	}

	int k = frame;

	while (panimvalue->num.total <= k)
	{
		k -= panimvalue->num.total;
		panimvalue += panimvalue->num.valid + 1;
		if ( panimvalue->num.total == 0 )
		{
			Assert( 0 ); // running off the end of the animation stream is bad
			v1 = 0;
			return;
		}
	}
	if (panimvalue->num.valid > k)
	{
		v1 = panimvalue[k+1].value * scale;
	}
	else
	{
		// get last valid data block
		v1 = panimvalue[panimvalue->num.valid].value * scale;
	}
}

//-----------------------------------------------------------------------------
// Purpose: return a sub frame rotation for a single bone
//-----------------------------------------------------------------------------
void CalcBoneQuaternion( int frame, float s, 
						const Quaternion &baseQuat, const RadianEuler &baseRot, const Vector &baseRotScale, 
						BoneFlags iBaseFlags, const Quaternion &baseAlignment, 
						const mstudio_rle_anim_t *panim, Quaternion &q )
{
	if (HasFlag(panim->flags, StudioAnimFlags::RawRot))
	{
		q = *(panim->pQuat48());
		Assert( q.IsValid() );
		return;
	} 
	
	if (HasFlag(panim->flags, StudioAnimFlags::RawRot2))
	{
		q = *(panim->pQuat64());
		Assert( q.IsValid() );
		return;
	}

	if ( !HasFlag(panim->flags, StudioAnimFlags::AnimRot) )
	{
		if (HasFlag(panim->flags, StudioAnimFlags::Delta))
		{
			q.Init( 0.0f, 0.0f, 0.0f, 1.0f );
		}
		else
		{
			q = baseQuat;
		}
		return;
	}

	mstudioanim_valueptr_t *pValuesPtr = panim->pRotV();

	if (s > 0.001f)
	{
		QuaternionAligned	q1, q2;
		RadianEuler			angle1, angle2;

		ExtractAnimValue( frame, pValuesPtr->pAnimvalue( 0 ), baseRotScale.x, angle1.x, angle2.x );
		ExtractAnimValue( frame, pValuesPtr->pAnimvalue( 1 ), baseRotScale.y, angle1.y, angle2.y );
		ExtractAnimValue( frame, pValuesPtr->pAnimvalue( 2 ), baseRotScale.z, angle1.z, angle2.z );

		if (!HasFlag(panim->flags, StudioAnimFlags::Delta))
		{
			angle1.x = angle1.x + baseRot.x;
			angle1.y = angle1.y + baseRot.y;
			angle1.z = angle1.z + baseRot.z;
			angle2.x = angle2.x + baseRot.x;
			angle2.y = angle2.y + baseRot.y;
			angle2.z = angle2.z + baseRot.z;
		}

		Assert( angle1.IsValid() && angle2.IsValid() );
		if (angle1.x != angle2.x || angle1.y != angle2.y || angle1.z != angle2.z)
		{
			AngleQuaternion( angle1, q1 );
			AngleQuaternion( angle2, q2 );

			QuaternionBlend( q1, q2, s, q );
		}
		else
		{
			AngleQuaternion( angle1, q );
		}
	}
	else
	{
		RadianEuler			angle;

		ExtractAnimValue( frame, pValuesPtr->pAnimvalue( 0 ), baseRotScale.x, angle.x );
		ExtractAnimValue( frame, pValuesPtr->pAnimvalue( 1 ), baseRotScale.y, angle.y );
		ExtractAnimValue( frame, pValuesPtr->pAnimvalue( 2 ), baseRotScale.z, angle.z );

		if (!HasFlag(panim->flags, StudioAnimFlags::Delta))
		{
			angle.x = angle.x + baseRot.x;
			angle.y = angle.y + baseRot.y;
			angle.z = angle.z + baseRot.z;
		}

		Assert( angle.IsValid() );
		AngleQuaternion( angle, q );
	}

	Assert( q.IsValid() );

	// align to unified bone
	if (!HasFlag(panim->flags, StudioAnimFlags::Delta) && HasFlag(iBaseFlags, BoneFlags::FixedAlignment))
	{
		QuaternionAlign( baseAlignment, q, q );
	}
}

inline void CalcBoneQuaternion( int frame, float s, 
						const mstudiobone_t *pBone,
						const mstudiolinearbone_t *pLinearBones,
						const mstudio_rle_anim_t *panim, Quaternion &q )
{
	if (pLinearBones)
	{
		CalcBoneQuaternion( frame, s, pLinearBones->quat(panim->bone), pLinearBones->rot(panim->bone), pLinearBones->rotscale(panim->bone), pLinearBones->flags(panim->bone), pLinearBones->qalignment(panim->bone), panim, q );
	}
	else
	{
		CalcBoneQuaternion( frame, s, pBone->quat, pBone->rot, pBone->rotscale, pBone->flags, pBone->qAlignment, panim, q );
	}
}



//-----------------------------------------------------------------------------
// Purpose: return a sub frame position for a single bone
//-----------------------------------------------------------------------------
void CalcBonePosition(	int frame, float s,
						const Vector &basePos, const Vector &baseBoneScale, 
						const mstudio_rle_anim_t *panim, BoneVector &pos	)
{
	if (HasFlag(panim->flags, StudioAnimFlags::RawPos))
	{
		pos = *(panim->pPos());
		Assert( pos.IsValid() );

		return;
	}
	else if (!HasFlag(panim->flags, StudioAnimFlags::AnimPos))
	{
		if (HasFlag(panim->flags, StudioAnimFlags::Delta))
		{
			pos.Init( 0.0f, 0.0f, 0.0f );
		}
		else
		{
			pos = basePos;
		}
		return;
	}

	mstudioanim_valueptr_t *pPosV = panim->pPosV();
	int					j;

	if (s > 0.001f)
	{
		float v1, v2;
		for (j = 0; j < 3; j++)
		{
			ExtractAnimValue( frame, pPosV->pAnimvalue( j ), baseBoneScale[j], v1, v2 );
			pos[j] = v1 * (1.0 - s) + v2 * s;
		}
	}
	else
	{
		for (j = 0; j < 3; j++)
		{
			ExtractAnimValue( frame, pPosV->pAnimvalue( j ), baseBoneScale[j], pos[j] );
		}
	}

	if (!HasFlag(panim->flags, StudioAnimFlags::Delta))
	{
		pos.x = pos.x + basePos.x;
		pos.y = pos.y + basePos.y;
		pos.z = pos.z + basePos.z;
	}

	Assert( pos.IsValid() );
}


inline void CalcBonePosition( int frame, float s, 
						const mstudiobone_t *pBone,
						const mstudiolinearbone_t *pLinearBones,
						const mstudio_rle_anim_t *panim, BoneVector &pos )
{
	if (pLinearBones)
	{
		CalcBonePosition( frame, s, pLinearBones->pos(panim->bone), pLinearBones->posscale(panim->bone), panim, pos );
	}
	else
	{
		CalcBonePosition( frame, s, pBone->pos, pBone->posscale, panim, pos );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CalcDecompressedAnimation( const mstudiocompressedikerror_t *pCompressed, int iFrame, float fraq, BoneVector &pos, BoneQuaternion &q )
{
	if (fraq > 0.0001f)
	{
		Vector p1, p2;
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 0 ), pCompressed->scale[0], p1.x, p2.x );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 1 ), pCompressed->scale[1], p1.y, p2.y );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 2 ), pCompressed->scale[2], p1.z, p2.z );
		pos = p1 * (1 - fraq) + p2 * fraq;

		Quaternion			q1, q2;
		RadianEuler			angle1, angle2;
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 3 ), pCompressed->scale[3], angle1.x, angle2.x );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 4 ), pCompressed->scale[4], angle1.y, angle2.y );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 5 ), pCompressed->scale[5], angle1.z, angle2.z );

		if (angle1.x != angle2.x || angle1.y != angle2.y || angle1.z != angle2.z)
		{
			AngleQuaternion( angle1, q1 );
			AngleQuaternion( angle2, q2 );
			QuaternionBlend( q1, q2, fraq, q );
		}
		else
		{
			AngleQuaternion( angle1, q );
		}
	}
	else
	{
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 0 ), pCompressed->scale[0], pos.x );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 1 ), pCompressed->scale[1], pos.y );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 2 ), pCompressed->scale[2], pos.z );

		RadianEuler			angle;
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 3 ), pCompressed->scale[3], angle.x );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 4 ), pCompressed->scale[4], angle.y );
		ExtractAnimValue( iFrame, pCompressed->pAnimvalue( 5 ), pCompressed->scale[5], angle.z );

		AngleQuaternion( angle, q );
	}
}




//-----------------------------------------------------------------------------
// Purpose: Calc Zeroframe Data
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Extract and blend two frames from a mstudio_frame_anim_t block of data
//-----------------------------------------------------------------------------

inline uint8_t *ExtractTwoFrames( StudioFrameFlags flags, float s, uint8_t *RESTRICT pFrameData, uint8_t *&pConstantData, int framelength, BoneQuaternion &q, BoneVector &pos, bool bIsDelta = false, const mstudiolinearbone_t *pLinearBones = nullptr, int bone = 0 )
{
	Quaternion q1, q2;
	// Making these aligned.  Could be VectorAligned instead, but I don't want to change the behavior of this code.
	ALIGN16 Vector p1;
	ALIGN16 Vector p2;

	if (HasFlag(flags, StudioFrameFlags::AnimRot))
	{
		q1 = *((Quaternion48 *)(pFrameData));
		q2 = *((Quaternion48 *)(pFrameData + framelength));
		QuaternionBlend( q1, q2, s, q );
		Assert( q.IsValid() );
		pFrameData += sizeof( Quaternion48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::AnimRot2))
	{
		q1 = *((Quaternion48S *)(pFrameData));
		q2 = *((Quaternion48S *)(pFrameData + framelength));
		QuaternionBlend( q1, q2, s, q );
		Assert( q.IsValid() );
		pFrameData += sizeof( Quaternion48S );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstRot))
	{
		q = *((Quaternion48 *)(pConstantData));
		Assert( q.IsValid() );
		pConstantData += sizeof( Quaternion48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstRot2))
	{
		q = *((Quaternion48S *)(pConstantData));
		Assert( q.IsValid() );
		pConstantData += sizeof( Quaternion48S );
	}
	// the non-virtual version needs initializers for no-animation
	else if (pLinearBones)
	{
		if (bIsDelta)
		{
			q.Init( 0.0f, 0.0f, 0.0f, 1.0f );
		}
		else
		{
			q = pLinearBones->quat( bone );
		}
	}
	if (HasFlag(flags, StudioFrameFlags::AnimPos))
	{
		p1 = *((Vector48 *)(pFrameData));
		p2 = *((Vector48 *)(pFrameData + framelength));
		pos = p1 * (1.0 - s) + p2 * s;
		Assert( pos.IsValid() );
		pFrameData += sizeof( Vector48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstPos))
	{
		pos = *((Vector48 *)(pConstantData));
		Assert( pos.IsValid() );
		pConstantData += sizeof( Vector48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::AnimPos2))
	{
		// pFrameData has no alignment guarantees, so using V_memcpy.
		V_memcpy( &p1, pFrameData, sizeof( p1 ) );
		V_memcpy( &p2, pFrameData + framelength, sizeof( p2 ) );
		pos = p1 * (1.0 - s) + p2 * s;
		Assert( pos.IsValid() );
		pFrameData += sizeof( Vector );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstPos2))
	{
		// pFrameData has no alignment guarantees, so using V_memcpy.
		V_memcpy( &pos, pConstantData, sizeof( pos ) );
		Assert( pos.IsValid() );
		pConstantData += sizeof( Vector );
	}
	// the non-virtual version needs initializers for no-animation
	else if (pLinearBones)
	{
		if (bIsDelta)
		{
			pos.Init( 0.0f, 0.0f, 0.0f );
		}
		else
		{
			pos = pLinearBones->pos( bone );
		}
	}
	return pFrameData;
}

//-----------------------------------------------------------------------------
// Purpose: Extract one frame from a mstudio_frame_anim_t block of data
//-----------------------------------------------------------------------------

inline uint8_t *ExtractSingleFrame( StudioFrameFlags flags, uint8_t *pFrameData, uint8_t *&pConstantData, BoneQuaternion &q, BoneVector &pos, bool bIsDelta = false, const mstudiolinearbone_t *pLinearBones = nullptr, int bone = 0 )
{
	if (HasFlag(flags, StudioFrameFlags::AnimRot))
	{
		q = *((Quaternion48 *)(pFrameData));
		Assert( q.IsValid() );
		pFrameData += sizeof( Quaternion48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::AnimRot2))
	{
		q = *((Quaternion48S *)(pFrameData));
		Assert( q.IsValid() );
		pFrameData += sizeof( Quaternion48S );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstRot))
	{
		q = *((Quaternion48 *)(pConstantData));
		Assert( q.IsValid() );
		pConstantData += sizeof( Quaternion48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstRot2))
	{
		q = *((Quaternion48S *)(pConstantData));
		Assert( q.IsValid() );
		pConstantData += sizeof( Quaternion48S );
	}
	// the non-virtual version needs initializers for no-animation
	else if (pLinearBones)
	{
		if (bIsDelta)
		{
			q.Init( 0.0f, 0.0f, 0.0f, 1.0f );
		}
		else
		{
			q = pLinearBones->quat( bone );
		}
	}
	if (HasFlag(flags, StudioFrameFlags::AnimPos))
	{
		pos = *((Vector48 *)(pFrameData));
		Assert( pos.IsValid() );
		pFrameData += sizeof( Vector48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstPos))
	{
		pos = *((Vector48 *)(pConstantData));
		Assert( pos.IsValid() );
		pConstantData += sizeof( Vector48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::AnimPos2))
	{
		// pFrameData has no guarantee on alignment, so using V_memcpy.
		V_memcpy( &pos, pFrameData, sizeof( Vector ) );
		Assert( pos.IsValid() );
		pFrameData += sizeof( Vector );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstPos2))
	{
		// pFrameData has no guarantee on alignment, so using V_memcpy.
		V_memcpy( &pos, pConstantData, sizeof( Vector ) );
		Assert( pos.IsValid() );
		pConstantData += sizeof( Vector );
	}
	// the non-virtual version needs initializers for no-animation
	else if (pLinearBones)
	{
		if (bIsDelta)
		{
			pos.Init( 0.0f, 0.0f, 0.0f );
		}
		else
		{
			pos = pLinearBones->pos( bone );
		}
	}

	return pFrameData;
}



//-----------------------------------------------------------------------------
// Purpose: Skip forward to the next bone in a mstudio_frame_anim_t block of data
//-----------------------------------------------------------------------------

inline uint8_t *SkipBoneFrame( StudioFrameFlags flags, uint8_t * RESTRICT pFrameData, uint8_t *&pConstantData )
{
	if (HasFlag(flags, StudioFrameFlags::AnimRot))
	{
		pFrameData += sizeof( Quaternion48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::AnimRot2))
	{
		pFrameData += sizeof( Quaternion48S );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstRot))
	{
		pConstantData += sizeof( Quaternion48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstRot2))
	{
		pConstantData += sizeof( Quaternion48S );
	}
	if (HasFlag(flags, StudioFrameFlags::AnimPos))
	{
		pFrameData += sizeof( Vector48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstPos))
	{
		pConstantData += sizeof( Vector48 );
	}
	else if (HasFlag(flags, StudioFrameFlags::AnimPos2))
	{
		pFrameData += sizeof( Vector );
	}
	else if (HasFlag(flags, StudioFrameFlags::ConstPos2))
	{
		pConstantData += sizeof( Vector );
	}
	return pFrameData;
}


//-----------------------------------------------------------------------------
// Purpose: Extract a single bone of animation
//-----------------------------------------------------------------------------





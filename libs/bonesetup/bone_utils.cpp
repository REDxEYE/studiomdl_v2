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
#ifdef POSIX
#define _rotl(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
#endif
#include "mathlib/ssequaternion.h"

#include "bone_utils.h"



void CBoneCache::DestroyResource()
{
	MemAlloc_FreeAligned( this );
}


CBoneCache::CBoneCache()
{
	m_size = 0;
	m_cachedBoneCount = 0;
}

void CBoneCache::Init( const bonecacheparams_t &params, unsigned int size, short *pStudioToCached, short *pCachedToStudio, int cachedBoneCount )
{
	m_cachedBoneCount = cachedBoneCount;
	m_size = size;
	m_timeValid = params.curtime;
	m_boneMask = params.boneMask;

	int studioTableSize = params.pStudioHdr->numbones() * sizeof(short);
	m_cachedToStudioOffset = studioTableSize;
	memcpy( StudioToCached(), pStudioToCached, studioTableSize );

	int cachedTableSize = cachedBoneCount * sizeof(short);
	memcpy( CachedToStudio(), pCachedToStudio, cachedTableSize );

	m_matrixOffset = AlignValue( sizeof(CBoneCache) + m_cachedToStudioOffset + cachedTableSize, 16 );

	UpdateBones( params.pBoneToWorld, params.pStudioHdr->numbones(), params.curtime );
}

void CBoneCache::UpdateBones( const matrix3x4a_t *pBoneToWorld, int numbones, float curtime )
{
	matrix3x4a_t *pBones = BoneArray();
	const short *pCachedToStudio = CachedToStudio();

	for ( int i = 0; i < m_cachedBoneCount; i++ )
	{
		int index = pCachedToStudio[i];
		//MatrixCopy( pBoneToWorld[index], pBones[i] );

		const float *pInput = pBoneToWorld[index].Base();
		float *pOutput = pBones[i].Base();

		fltx4 fl4Tmp0 = LoadAlignedSIMD( pInput );
		StoreAlignedSIMD( pOutput, fl4Tmp0 );
		fltx4 fl4Tmp1 = LoadAlignedSIMD( pInput + 4 );
		StoreAlignedSIMD( pOutput+4, fl4Tmp1 );
		fltx4 fl4Tmp2 = LoadAlignedSIMD( pInput + 8 );
		StoreAlignedSIMD( pOutput+8, fl4Tmp2 );
	}
	m_timeValid = curtime;
}

bool CBoneCache::IsValid( float curtime, float dt )
{
	if ( curtime - m_timeValid <= dt )
		return true;
	return false;
}


// private functions
matrix3x4a_t *CBoneCache::BoneArray()
{
	return (matrix3x4a_t *)( (uint8_t *)(this) + m_matrixOffset );
}

short *CBoneCache::StudioToCached()
{
	return (short *)( (char *)(this+1) );
}

short *CBoneCache::CachedToStudio()
{
	return (short *)( (char *)(this+1) + m_cachedToStudioOffset );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void BuildBoneChain(
	const CStudioHdr *pStudioHdr,
	const matrix3x4a_t &rootxform,
	const BoneVector pos[],
	const BoneQuaternion q[],
	int	iBone,
	matrix3x4a_t *pBoneToWorld )
{
	CBoneBitList boneComputed;
	BuildBoneChainPartial( pStudioHdr, rootxform, pos, q, iBone, pBoneToWorld, boneComputed, -1 );
	return;
}



//-----------------------------------------------------------------------------
// Purpose: build boneToWorld transforms for a specific bone
//-----------------------------------------------------------------------------
void BuildBoneChain(
	const CStudioHdr *pStudioHdr,
	const matrix3x4a_t &rootxform,
	const BoneVector pos[],
	const BoneQuaternion q[],
	int	iBone,
	matrix3x4a_t *pBoneToWorld,
	CBoneBitList &boneComputed )
{
	BuildBoneChainPartial( pStudioHdr, rootxform, pos, q, iBone, pBoneToWorld, boneComputed, -1 );
}


void BuildBoneChainPartial(
	const CStudioHdr *pStudioHdr,
	const matrix3x4_t &rootxform,
	const BoneVector pos[],
	const BoneQuaternion q[],
	int	iBone,
	matrix3x4_t *pBoneToWorld,
	CBoneBitList &boneComputed,
	int iRoot )
{
	if ( boneComputed.IsBoneMarked(iBone) )
		return;

	matrix3x4_t bonematrix;
	QuaternionMatrix( q[iBone], pos[iBone], bonematrix );

	int parent = pStudioHdr->boneParent( iBone );
	if (parent == -1 || iBone == iRoot)
	{
		ConcatTransforms( rootxform, bonematrix, pBoneToWorld[iBone] );
	}
	else
	{
		// evil recursive!!!
		BuildBoneChainPartial( pStudioHdr, rootxform, pos, q, parent, pBoneToWorld, boneComputed, iRoot );
		ConcatTransforms( pBoneToWorld[parent], bonematrix, pBoneToWorld[iBone]);
	}

	boneComputed.MarkBone(iBone);
}


//-----------------------------------------------------------------------------
// Purpose: qt = ( s * p ) * q
//-----------------------------------------------------------------------------
void QuaternionSM( float s, const Quaternion &p, const Quaternion &q, Quaternion &qt )
{
	Quaternion		p1, q1;

	QuaternionScale( p, s, p1 );
	QuaternionMult( p1, q, q1 );
	QuaternionNormalize( q1 );
	qt[0] = q1[0];
	qt[1] = q1[1];
	qt[2] = q1[2];
	qt[3] = q1[3];
}

#if ALLOW_SIMD_QUATERNION_MATH
FORCEINLINE fltx4 QuaternionSMSIMD( const fltx4 &s, const fltx4 &p, const fltx4 &q )
{
	fltx4 p1, q1, result;
	p1 = QuaternionScaleSIMD( p, s );
	q1 = QuaternionMultSIMD( p1, q );
	result = QuaternionNormalizeSIMD( q1 );
	return result;
}

FORCEINLINE fltx4 QuaternionSMSIMD( float s, const fltx4 &p, const fltx4 &q )
{
	return QuaternionSMSIMD( ReplicateX4(s), p, q );
}
#endif

//-----------------------------------------------------------------------------
// Purpose: qt = p * ( s * q )
//-----------------------------------------------------------------------------
void QuaternionMA( const Quaternion &p, float s, const Quaternion &q, Quaternion &qt )
{
	Quaternion p1, q1;

	QuaternionScale( q, s, q1 );
	QuaternionMult( p, q1, p1 );
	QuaternionNormalize( p1 );
	qt[0] = p1[0];
	qt[1] = p1[1];
	qt[2] = p1[2];
	qt[3] = p1[3];
}

#if ALLOW_SIMD_QUATERNION_MATH

FORCEINLINE fltx4 QuaternionMASIMD( const fltx4 &p, const fltx4 &s, const fltx4 &q )
{
	fltx4 p1, q1, result;
	q1 = QuaternionScaleSIMD( q, s );
	p1 = QuaternionMultSIMD( p, q1 );
	result = QuaternionNormalizeSIMD( p1 );
	return result;
}

FORCEINLINE fltx4 QuaternionMASIMD( const fltx4 &p, float s, const fltx4 &q )
{
	return QuaternionMASIMD(p, ReplicateX4(s), q);
}
#endif


//-----------------------------------------------------------------------------
// Purpose: qt = p + s * q
//-----------------------------------------------------------------------------
void QuaternionAccumulate( const Quaternion &p, float s, const Quaternion &q, Quaternion &qt )
{
	Quaternion q2;
	QuaternionAlign( p, q, q2 );

	qt[0] = p[0] + s * q2[0];
	qt[1] = p[1] + s * q2[1];
	qt[2] = p[2] + s * q2[2];
	qt[3] = p[3] + s * q2[3];
}

#if ALLOW_SIMD_QUATERNION_MATH
FORCEINLINE fltx4 QuaternionAccumulateSIMD( const fltx4 &p, float s, const fltx4 &q )
{
	fltx4 q2, s4, result;
	q2 = QuaternionAlignSIMD( p, q );
	s4 = ReplicateX4( s );
	result = MaddSIMD( s4, q2, p );
	return result;
}
#endif


#define PARANOID_SIMD_DOUBLECHECK 0  // set this to one to perform both SIMD and scalar bones every frame,
									 // then compare the results.
#define PARANOID_SIMD_TIMING_TEST 0 // enable to allow running many iterations of SlerpBones per frame
									// for timing purposes



#if PARANOID_SIMD_TIMING_TEST
static ConVar cl_bones_simd_timing_iter( "cl_bones_simd_timing_iter", "100", FCVAR_REPLICATED, "number of times to run SlerpBones." );
void SlerpBones(
				const CStudioHdr *pStudioHdr,
				Quaternion q1[MAXSTUDIOBONES],
				BoneVector pos1[MAXSTUDIOBONES],
				mstudioseqdesc_t &seqdesc,  // source of q2 and pos2
				int sequence,
				const BoneQuaternionAligned q2[MAXSTUDIOBONES],
				const BoneVector pos2[MAXSTUDIOBONES],
				float s,
				BoneFlags boneMask )
{
	// copy off the input arrays for safety
	int numBones =  pStudioHdr->numbones();
	BoneQuaternionAligned fake_q1[MAXSTUDIOBONES];
	BoneVector fake_pos1[MAXSTUDIOBONES];
	bool version = cl_bones_simd_timing_version.GetBool();

	// fruitlessly run as many times as specified
	for (int i = cl_bones_simd_timing_iter.GetInt() ; i > 0 ; --i )
	{
		memcpy( fake_q1, q1, numBones * sizeof(Quaternion) );
		memcpy( fake_pos1, pos1, numBones * sizeof(BoneVector) );

		if (version) // 1 = simd  0 = scalar
		{
			SlerpBonesSpeedy(pStudioHdr,
				fake_q1,
				fake_pos1,
				seqdesc,
				sequence,
				q2,
				pos2,
				s,
				boneMask
				);
		}
		else
		{

			SlerpBonesSlow(pStudioHdr,
				fake_q1,
				fake_pos1,
				seqdesc,
				sequence,
				q2,
				pos2,
				s,
				boneMask
				);
		}
	}

	// run once for real
	if (version) // 1 = simd  0 = scalar
	{
		SlerpBonesSpeedy(pStudioHdr,
			static_cast<BoneQuaternionAligned *>(q1),
			pos1,
			seqdesc,
			sequence,
			q2,
			pos2,
			s,
			boneMask
			);
	}
	else
	{

		SlerpBonesSlow(pStudioHdr,
			q1,
			pos1,
			seqdesc,
			sequence,
			q2,
			pos2,
			s,
			boneMask
			);
	}
}
#endif

template <int N>
struct GetLog2_t
{};
template<>
struct GetLog2_t<0x00100000>
{
	enum {kLog2 = 20};
};

inline void AlwaysAssert(bool condition)
{
	Assert(condition);
}


//-----------------------------------------------------------------------------
// Purpose: resolve a global pose parameter to the specific setting for this sequence
//-----------------------------------------------------------------------------
int Studio_LocalPoseParameter( const CStudioHdr *pStudioHdr, const float poseParameter[], mstudioseqdesc_t &seqdesc, int iSequence, int iLocalIndex, float &flSetting )
{
	int iPose = pStudioHdr->GetSharedPoseParameter( iSequence, seqdesc.paramindex[iLocalIndex] );

	if (iPose == -1)
	{
		flSetting = 0;
		return 0;
	}

	const mstudioposeparamdesc_t &Pose = ((CStudioHdr *)pStudioHdr)->pPoseParameter( iPose );

	float flValue = poseParameter[iPose];

	if (Pose.loop)
	{
		float wrap = (Pose.start + Pose.end) / 2.0 + Pose.loop / 2.0;
		float shift = Pose.loop - wrap;

		flValue = flValue - Pose.loop * floor((flValue + shift) / Pose.loop);
	}

	int nIndex = 0;
	if (seqdesc.posekeyindex == 0)
	{
		float flLocalStart	= ((float)seqdesc.paramstart[iLocalIndex] - Pose.start) / (Pose.end - Pose.start);
		float flLocalEnd	= ((float)seqdesc.paramend[iLocalIndex] - Pose.start) / (Pose.end - Pose.start);

		// convert into local range
		flSetting = (flValue - flLocalStart) / (flLocalEnd - flLocalStart);

		// clamp.  This shouldn't ever need to happen if it's looping.
		if (flSetting < 0)
			flSetting = 0;
		if (flSetting > 1)
			flSetting = 1;

		nIndex = 0;
		if (seqdesc.groupsize[iLocalIndex] > 2 )
		{
			// estimate index
			nIndex = (int)(flSetting * (seqdesc.groupsize[iLocalIndex] - 1));
			if (nIndex == seqdesc.groupsize[iLocalIndex] - 1)
			{
				nIndex = seqdesc.groupsize[iLocalIndex] - 2;
			}
			flSetting = flSetting * (seqdesc.groupsize[iLocalIndex] - 1) - nIndex;
		}
	}
	else
	{
		flValue = flValue * (Pose.end - Pose.start) + Pose.start;
		nIndex = 0;

		// FIXME: this needs to be 2D
		// FIXME: this shouldn't be a linear search

		while (1)
		{
			flSetting = (flValue - seqdesc.poseKey( iLocalIndex, nIndex )) / (seqdesc.poseKey( iLocalIndex, nIndex + 1 ) - seqdesc.poseKey( iLocalIndex, nIndex ));
			/*
			if (index > 0 && flSetting < 0.0)
			{
				index--;
				continue;
			}
			else
			*/
			if (nIndex < seqdesc.groupsize[iLocalIndex] - 2 && flSetting > 1.0)
			{
				nIndex++;
				continue;
			}
			break;
		}

		// clamp.
		if (flSetting < 0.0f)
			flSetting = 0.0f;
		if (flSetting > 1.0f)
			flSetting = 1.0f;
	}
	return nIndex;
}


//-----------------------------------------------------------------------------
// Purpose:  Lookup a bone controller
//-----------------------------------------------------------------------------




//-----------------------------------------------------------------------------
// Purpose: converts a ranged bone controller value into a 0..1 encoded value
// Output: 	ctlValue contains 0..1 encoding.
//			returns clamped ranged value
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: converts a 0..1 encoded bone controller value into a ranged value
// Output: 	returns ranged value
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: Calculates default values for the pose parameters
// Output: 	fills in an array
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: converts a ranged pose parameter value into a 0..1 encoded value
// Output: 	ctlValue contains 0..1 encoding.
//			returns clamped ranged value
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: converts a 0..1 encoded pose parameter value into a ranged value
// Output: 	returns ranged value
//-----------------------------------------------------------------------------





//-----------------------------------------------------------------------------
// Purpose: returns array of animations and weightings for a sequence based on current pose parameters
//-----------------------------------------------------------------------------

void Studio_SeqAnims( const CStudioHdr *pStudioHdr, mstudioseqdesc_t &seqdesc, int iSequence, const float poseParameter[], mstudioanimdesc_t *panim[4], float *weight )
{
	if (!pStudioHdr || iSequence >= pStudioHdr->GetNumSeq())
	{
		weight[0] = weight[1] = weight[2] = weight[3] = 0.0;
		return;
	}

	float s0 = 0, s1 = 0;

	int i0 = Studio_LocalPoseParameter( pStudioHdr, poseParameter, seqdesc, iSequence, 0, s0 );
	int i1 = Studio_LocalPoseParameter( pStudioHdr, poseParameter, seqdesc, iSequence, 1, s1 );

	panim[0] = &((CStudioHdr *)pStudioHdr)->pAnimdesc( ((CStudioHdr *)pStudioHdr)->iRelativeAnim( iSequence, seqdesc.anim( i0  , i1 ) ) );
	weight[0] = (1 - s0) * (1 - s1);

	panim[1] = &((CStudioHdr *)pStudioHdr)->pAnimdesc( ((CStudioHdr *)pStudioHdr)->iRelativeAnim( iSequence, seqdesc.anim( i0+1, i1 ) ) );
	weight[1] = (s0) * (1 - s1);

	panim[2] = &((CStudioHdr *)pStudioHdr)->pAnimdesc( ((CStudioHdr *)pStudioHdr)->iRelativeAnim( iSequence, seqdesc.anim( i0  , i1+1 ) ) );
	weight[2] = (1 - s0) * (s1);

	panim[3] = &((CStudioHdr *)pStudioHdr)->pAnimdesc( ((CStudioHdr *)pStudioHdr)->iRelativeAnim( iSequence, seqdesc.anim( i0+1, i1+1 ) ) );
	weight[3] = (s0) * (s1);

	Assert( weight[0] >= 0.0f && weight[1] >= 0.0f && weight[2] >= 0.0f && weight[3] >= 0.0f );
}

//-----------------------------------------------------------------------------
// Purpose: returns max frame number for a sequence
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: returns frames per second of a sequence
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: returns cycles per second of a sequence (cycles/second)
//-----------------------------------------------------------------------------

float Studio_CPS( const CStudioHdr *pStudioHdr, mstudioseqdesc_t &seqdesc, int iSequence, const float poseParameter[] )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	float t = 0;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i] > 0 && panim[i]->numframes > 1)
		{
			t += (panim[i]->fps / (panim[i]->numframes - 1)) * weight[i];
		}
	}

	// FIXME: add support for more than just start 0 and end 0 pose param layers
	for (int j = 0; j < seqdesc.numautolayers; j++)
	{
		mstudioautolayer_t *pLayer = seqdesc.pAutolayer( j );

		if (HasFlag(pLayer->flags, AutoLayerFlags::Local))
			continue;

		float layerWeight = 0;

		int iSequenceLocal = pStudioHdr->iRelativeSeq( iSequence, pLayer->iSequence );

		if ( pLayer->start == 0 && pLayer->end == 0 && HasFlag(pLayer->flags, AutoLayerFlags::Pose) )
		{
			int iPose = pStudioHdr->GetSharedPoseParameter( iSequenceLocal, pLayer->iPose );
			if (iPose == -1)
				continue;

			const mstudioposeparamdesc_t &Pose = ((CStudioHdr *)pStudioHdr)->pPoseParameter( iPose );
			float s = poseParameter[ iPose ] * (Pose.end - Pose.start) + Pose.start;

			Assert( (pLayer->tail - pLayer->peak) != 0 );

			s = clamp( (s - pLayer->peak) / (pLayer->tail - pLayer->peak), 0, 1 );

			if (HasFlag(pLayer->flags, AutoLayerFlags::Spline))
			{
				s = SimpleSpline( s );
			}

			layerWeight = seqdesc.weight(0) * s;
		}

		if ( layerWeight )
		{
			mstudioseqdesc_t &seqdescLocal = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequenceLocal );
			Studio_SeqAnims( pStudioHdr, seqdescLocal, iSequenceLocal, poseParameter, panim, weight );

			float flLocalT = 0;

			for (int i = 0; i < 4; i++)
			{
				if (weight[i] > 0 && panim[i]->numframes > 1)
				{
					flLocalT += (panim[i]->fps / (panim[i]->numframes - 1)) * weight[i];
				}
			}

			if ( flLocalT )
			{
				t = Lerp( layerWeight, t, flLocalT );
			}
		}
	}

	return t;
}

//-----------------------------------------------------------------------------
// Purpose: returns length (in seconds) of a sequence (seconds/cycle)
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle relative to the start of an animations cycle
// Output:	updated position and angle, relative to the origin
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: calculate instantaneous velocity in ips at a given point
//			in the animations cycle
// Output:	velocity vector, relative to identity orientation
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in an animation cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: finds how much of an animation to play to move given linear distance
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in a sequences cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose: calculate instantaneous velocity in ips at a given point in the sequence's cycle
// Output:	velocity vector, relative to identity orientation
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: finds how much of an sequence to play to move given linear distance
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: lookup attachment by name
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: lookup attachments by substring. Randomly return one of the matching attachments.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: lookup bone by name
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: return pointer to sequence key value buffer
//-----------------------------------------------------------------------------



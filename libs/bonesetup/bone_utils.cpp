//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//


#include "mathlib/mathlib.h"
#include "tier0/threadtools.h"
#include "studiomdl/bone_setup.h"
#include <cstring>
#ifdef POSIX
#define _rotl(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
#endif
#include "mathlib/ssequaternion.h"
#include "tier1/datamanager.h"



#ifdef CLIENT_DLL
	#include "posedebugger.h"
#endif

#include "bone_utils.h"



// -----------------------------------------------------------------
CBoneCache *CBoneCache::CreateResource( const bonecacheparams_t &params )
{
	short studioToCachedIndex[MAXSTUDIOBONES];
	short cachedToStudioIndex[MAXSTUDIOBONES];
	int cachedBoneCount = 0;
	for ( int i = 0; i < params.pStudioHdr->numbones(); i++ )
	{
		// skip bones that aren't part of the boneMask (and aren't the root bone)
		if (i != 0 && !HasFlag(params.pStudioHdr->boneFlags(i), params.boneMask))
		{
			studioToCachedIndex[i] = -1;
			continue;
		}
		studioToCachedIndex[i] = cachedBoneCount;
		cachedToStudioIndex[cachedBoneCount] = i;
		cachedBoneCount++;
	}
	int tableSizeStudio = sizeof(short) * params.pStudioHdr->numbones();
	int tableSizeCached = sizeof(short) * cachedBoneCount;
	int matrixSize = sizeof(matrix3x4_t) * cachedBoneCount;
	size_t size = AlignValue( sizeof(CBoneCache) + tableSizeStudio + tableSizeCached, 16 ) + matrixSize;

	CBoneCache *pMem = (CBoneCache *)MemAlloc_AllocAligned( size, 16 );
	Construct( pMem );
	Assert( size == ( uint32_t )size ); // make sure we're not trimming the int in 64bit
	pMem->Init( params, size, studioToCachedIndex, cachedToStudioIndex, cachedBoneCount );
	return pMem;
}

unsigned int CBoneCache::EstimatedSize( const bonecacheparams_t &params )
{
	// conservative estimate - max size
	return ( params.pStudioHdr->numbones() * (sizeof(short) + sizeof(short) + sizeof(matrix3x4_t)) + 3 ) & ~3;
}

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

matrix3x4a_t *CBoneCache::GetCachedBone( int studioIndex )
{
	int cachedIndex = StudioToCached()[studioIndex];
	if ( cachedIndex >= 0 )
	{
		return BoneArray() + cachedIndex;
	}
	return nullptr;
}

void CBoneCache::ReadCachedBones( matrix3x4a_t *pBoneToWorld )
{
	matrix3x4a_t *pBones = BoneArray();
	const short *pCachedToStudio = CachedToStudio();
	for ( int i = 0; i < m_cachedBoneCount; i++ )
	{
		//MatrixCopy( pBones[i], pBoneToWorld[pCachedToStudio[i]] );

		const float *pInput = pBones[i].Base();
		float *pOutput = pBoneToWorld[pCachedToStudio[i]].Base();
		fltx4 fl4Tmp0 = LoadAlignedSIMD( pInput );
		StoreAlignedSIMD( pOutput, fl4Tmp0 );
		fltx4 fl4Tmp1 = LoadAlignedSIMD( pInput + 4 );
		StoreAlignedSIMD( pOutput+4, fl4Tmp1 );
		fltx4 fl4Tmp2 = LoadAlignedSIMD( pInput + 8 );
		StoreAlignedSIMD( pOutput+8, fl4Tmp2 );
	}
}

void CBoneCache::ReadCachedBonePointers( matrix3x4_t **bones, int numbones )
{
	memset( bones, 0, sizeof(matrix3x4_t *) * numbones );
	matrix3x4a_t *pBones = BoneArray();
	const short *pCachedToStudio = CachedToStudio();
	for ( int i = 0; i < m_cachedBoneCount; i++ )
	{
		bones[pCachedToStudio[i]] = pBones + i;
	}
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

// Construct a singleton
static CDataManager<CBoneCache, bonecacheparams_t, CBoneCache *> g_StudioBoneCache( 128 * 1024L );


CBoneCache *Studio_GetBoneCache( memhandle_t cacheHandle, bool bLock )
{
	if ( !bLock )
	{
		return g_StudioBoneCache.GetResource_NoLock( cacheHandle );
	}
	else
	{
		return g_StudioBoneCache.LockResource( cacheHandle );
	}
}

void Studio_ReleaseBoneCache( memhandle_t cacheHandle )
{
	g_StudioBoneCache.UnlockResource( cacheHandle );
	g_StudioBoneCache.FlushToTargetSize();
}

memhandle_t Studio_CreateBoneCache( bonecacheparams_t &params )
{
	return g_StudioBoneCache.CreateResource( params );
}

void Studio_DestroyBoneCache( memhandle_t cacheHandle )
{
	g_StudioBoneCache.DestroyResource( cacheHandle );
}

void Studio_InvalidateBoneCacheIfNotMatching( memhandle_t cacheHandle, float flTimeValid )
{
	CBoneCache *pCache = g_StudioBoneCache.GetResource_NoLock( cacheHandle );
	if ( pCache && pCache->m_timeValid != flTimeValid )
	{
		pCache->m_timeValid = -1.0f;
	}
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

bool IsInList(int value, const int *pBegin, const int *pEnd)
{
	for(const int *p = pBegin; p < pEnd; ++p)
		if(*p == value)
			return true;
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Scale a set of bones.  Must be of type delta
//-----------------------------------------------------------------------------
void ScaleBones(
	const CStudioHdr *pStudioHdr,
	BoneQuaternion q1[MAXSTUDIOBONES],
	BoneVector pos1[MAXSTUDIOBONES],
	int sequence,
	float s,
	BoneFlags boneMask )
{
	int			i, j;
	Quaternion		q3;

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( sequence );

	virtualmodel_t *pVModel = pStudioHdr->GetVirtualModel();
	const virtualgroup_t *pSeqGroup = nullptr;
	if (pVModel)
	{
		pSeqGroup = pVModel->pSeqGroup( sequence );
	}

	float s2 = s;
	float s1 = 1.0 - s2;

	for (i = 0; i < pStudioHdr->numbones(); i++)
	{
		// skip unused bones
		if (!HasFlag(pStudioHdr->boneFlags(i), boneMask))
		{
			continue;
		}

		if (pSeqGroup)
		{
			j = pSeqGroup->boneMap[i];
		}
		else
		{
			j = i;
		}

		if (j >= 0 && seqdesc.weight( j ) > 0.0)
		{
			QuaternionIdentityBlend( q1[i], s1, q1[i] );
			VectorScale( pos1[i], s2, pos1[i] );
		}
	}
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

void Studio_CalcBoneToBoneTransform( const CStudioHdr *pStudioHdr, int inputBoneIndex, int outputBoneIndex, matrix3x4_t& matrixOut )
{
	const mstudiobone_t *pbone = pStudioHdr->pBone( inputBoneIndex );

	matrix3x4a_t inputToPose;
	MatrixInvert( pbone->poseToBone, inputToPose );
	ConcatTransforms( pStudioHdr->pBone( outputBoneIndex )->poseToBone, inputToPose, matrixOut );
}


//-----------------------------------------------------------------------------
// Purpose:  Lookup a bone controller
//-----------------------------------------------------------------------------



static mstudiobonecontroller_t* FindController( const CStudioHdr *pStudioHdr, int iController)
{
	// find first controller that matches the index
	for (int i = 0; i < pStudioHdr->numbonecontrollers(); i++)
	{
		if (pStudioHdr->pBonecontroller( i )->inputfield == iController)
			return pStudioHdr->pBonecontroller( i );
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose: converts a ranged bone controller value into a 0..1 encoded value
// Output: 	ctlValue contains 0..1 encoding.
//			returns clamped ranged value
//-----------------------------------------------------------------------------

float Studio_SetController( const CStudioHdr *pStudioHdr, int iController, float flValue, float &ctlValue )
{
	if (! pStudioHdr)
		return flValue;

	mstudiobonecontroller_t *pbonecontroller = FindController(pStudioHdr, iController);
	if(!pbonecontroller)
	{
		ctlValue = 0;
		return flValue;
	}

	// wrap 0..360 if it's a rotational controller
	if (HasFlag(pbonecontroller->type, StudioMotionFlags::XR | StudioMotionFlags::YR | StudioMotionFlags::ZR))
	{
		// ugly hack, invert value if end < start
		if (pbonecontroller->end < pbonecontroller->start)
			flValue = -flValue;

		// does the controller not wrap?
		if (pbonecontroller->start + 359.0 >= pbonecontroller->end)
		{
			if (flValue > ((pbonecontroller->start + pbonecontroller->end) / 2.0) + 180)
				flValue = flValue - 360;
			if (flValue < ((pbonecontroller->start + pbonecontroller->end) / 2.0) - 180)
				flValue = flValue + 360;
		}
		else
		{
			if (flValue > 360)
				flValue = flValue - (int)(flValue / 360.0) * 360.0;
			else if (flValue < 0)
				flValue = flValue + (int)((flValue / -360.0) + 1) * 360.0;
		}
	}

	ctlValue = (flValue - pbonecontroller->start) / (pbonecontroller->end - pbonecontroller->start);
	if (ctlValue < 0) ctlValue = 0;
	if (ctlValue > 1) ctlValue = 1;

	float flReturnVal = ((1.0 - ctlValue)*pbonecontroller->start + ctlValue *pbonecontroller->end);

	// ugly hack, invert value if a rotational controller and end < start
	if (HasFlag(pbonecontroller->type, StudioMotionFlags::XR | StudioMotionFlags::YR | StudioMotionFlags::ZR) &&
		pbonecontroller->end < pbonecontroller->start				)
	{
		flReturnVal *= -1;
	}

	return flReturnVal;
}


//-----------------------------------------------------------------------------
// Purpose: converts a 0..1 encoded bone controller value into a ranged value
// Output: 	returns ranged value
//-----------------------------------------------------------------------------

float Studio_GetController( const CStudioHdr *pStudioHdr, int iController, float ctlValue )
{
	if (!pStudioHdr)
		return 0.0;

	mstudiobonecontroller_t *pbonecontroller = FindController(pStudioHdr, iController);
	if(!pbonecontroller)
		return 0;

	return ctlValue * (pbonecontroller->end - pbonecontroller->start) + pbonecontroller->start;
}


//-----------------------------------------------------------------------------
// Purpose: Calculates default values for the pose parameters
// Output: 	fills in an array
//-----------------------------------------------------------------------------

void Studio_CalcDefaultPoseParameters( const CStudioHdr *pStudioHdr, float flPoseParameter[], int nCount )
{
	int nPoseCount = pStudioHdr->GetNumPoseParameters();
	int nNumParams = MIN( nCount, MAXSTUDIOPOSEPARAM );

	for ( int i = 0; i < nNumParams; ++i )
	{
		// Default to middle of the pose parameter range
		flPoseParameter[ i ] = 0.5f;
		if ( i < nPoseCount )
		{
			const mstudioposeparamdesc_t &Pose = ((CStudioHdr *)pStudioHdr)->pPoseParameter( i );

			// Want to try for a zero state.  If one doesn't exist set it to .5 by default.
			if ( Pose.start < 0.0f && Pose.end > 0.0f )
			{
				float flPoseDelta = Pose.end - Pose.start;
				flPoseParameter[i] = -Pose.start / flPoseDelta;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: converts a ranged pose parameter value into a 0..1 encoded value
// Output: 	ctlValue contains 0..1 encoding.
//			returns clamped ranged value
//-----------------------------------------------------------------------------

float Studio_SetPoseParameter( const CStudioHdr *pStudioHdr, int iParameter, float flValue, float &ctlValue )
{
	if (iParameter < 0 || iParameter >= pStudioHdr->GetNumPoseParameters())
	{
		ctlValue = 0;
		return 0;
	}

	const mstudioposeparamdesc_t &PoseParam = ((CStudioHdr *)pStudioHdr)->pPoseParameter( iParameter );

	Assert( IsFinite( flValue ) );

	if (PoseParam.loop)
	{
		float wrap = (PoseParam.start + PoseParam.end) / 2.0 + PoseParam.loop / 2.0;
		float shift = PoseParam.loop - wrap;

		flValue = flValue - PoseParam.loop * floor((flValue + shift) / PoseParam.loop);
	}

	ctlValue = (flValue - PoseParam.start) / (PoseParam.end - PoseParam.start);

	if (ctlValue < 0) ctlValue = 0;
	if (ctlValue > 1) ctlValue = 1;

	Assert( IsFinite( ctlValue ) );

	return ctlValue * (PoseParam.end - PoseParam.start) + PoseParam.start;
}


//-----------------------------------------------------------------------------
// Purpose: converts a 0..1 encoded pose parameter value into a ranged value
// Output: 	returns ranged value
//-----------------------------------------------------------------------------

float Studio_GetPoseParameter( const CStudioHdr *pStudioHdr, int iParameter, float ctlValue )
{
	if (iParameter < 0 || iParameter >= pStudioHdr->GetNumPoseParameters())
	{
		return 0;
	}

	const mstudioposeparamdesc_t &PoseParam = ((CStudioHdr *)pStudioHdr)->pPoseParameter( iParameter );

	return ctlValue * (PoseParam.end - PoseParam.start) + PoseParam.start;
}





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

int Studio_MaxFrame( const CStudioHdr *pStudioHdr, int iSequence, const float poseParameter[] )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );
	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	float maxFrame = 0;
	for (int i = 0; i < 4; i++)
	{
		if (weight[i] > 0)
		{
			maxFrame += panim[i]->numframes * weight[i];
		}
	}

	if ( maxFrame > 1 )
		maxFrame -= 1;


	// FIXME: why does the weights sometimes not exactly add it 1.0 and this sometimes rounds down?
	return (maxFrame + 0.01);
}


//-----------------------------------------------------------------------------
// Purpose: returns frames per second of a sequence
//-----------------------------------------------------------------------------

float Studio_FPS( const CStudioHdr *pStudioHdr, int iSequence, const float poseParameter[] )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );
	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	float t = 0;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i] > 0)
		{
			t += panim[i]->fps * weight[i];
		}
	}
	return t;
}


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

float Studio_Duration( const CStudioHdr *pStudioHdr, int iSequence, const float poseParameter[] )
{
	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );
	float cps = Studio_CPS( pStudioHdr, seqdesc, iSequence, poseParameter );

	if( cps == 0 )
		return 0.0f;

	return 1.0f/cps;
}


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle relative to the start of an animations cycle
// Output:	updated position and angle, relative to the origin
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------

bool Studio_AnimPosition( mstudioanimdesc_t *panim, float flCycle, Vector &vecPos, QAngle &vecAngle )
{
	float	prevframe = 0;
	vecPos.Init( );
	vecAngle.Init( );

	if (panim->nummovements == 0)
		return false;

	int iLoops = 0;
	if (flCycle > 1.0)
	{
		iLoops = (int)flCycle;
	}
	else if (flCycle < 0.0)
	{
		iLoops = (int)flCycle - 1;
	}
	flCycle = flCycle - iLoops;

	float	flFrame = flCycle * (panim->numframes - 1);

	for (int i = 0; i < panim->nummovements; i++)
	{
		mstudiomovement_t *pmove = panim->pMovement( i );

		if (pmove->endframe >= flFrame)
		{
			float f = (flFrame - prevframe) / (pmove->endframe - prevframe);

			float d = pmove->v0 * f + 0.5 * (pmove->v1 - pmove->v0) * f * f;

			vecPos = vecPos + d * pmove->vector;
			vecAngle.y = vecAngle.y * (1 - f) + pmove->angle * f;
			if (iLoops != 0)
			{
				mstudiomovement_t *pmove = panim->pMovement( panim->nummovements - 1 );
				vecPos = vecPos + iLoops * pmove->position;
				vecAngle.y = vecAngle.y + iLoops * pmove->angle;
			}
			return true;
		}
		else
		{
			prevframe = pmove->endframe;
			vecPos = pmove->position;
			vecAngle.y = pmove->angle;
		}
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: calculate instantaneous velocity in ips at a given point
//			in the animations cycle
// Output:	velocity vector, relative to identity orientation
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------

bool Studio_AnimVelocity( mstudioanimdesc_t *panim, float flCycle, Vector &vecVelocity )
{
	float	prevframe = 0;

	float	flFrame = flCycle * (panim->numframes - 1);
	flFrame = flFrame - (int)(flFrame / (panim->numframes - 1));

	for (int i = 0; i < panim->nummovements; i++)
	{
		mstudiomovement_t *pmove = panim->pMovement( i );

		if (pmove->endframe >= flFrame)
		{
			float f = (flFrame - prevframe) / (pmove->endframe - prevframe);

			float vel = pmove->v0 * (1 - f) + pmove->v1 * f;
			// scale from per block to per sec velocity
			vel = vel * panim->fps / (pmove->endframe - prevframe);

			vecVelocity = pmove->vector * vel;
			return true;
		}
		else
		{
			prevframe = pmove->endframe;
		}
	}
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in an animation cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if animation is not a movement animation
//-----------------------------------------------------------------------------

bool Studio_AnimMovement( mstudioanimdesc_t *panim, float flCycleFrom, float flCycleTo, Vector &deltaPos, QAngle &deltaAngle )
{
	if (panim->nummovements == 0)
		return false;

	Vector startPos;
	QAngle startA;
	Studio_AnimPosition( panim, flCycleFrom, startPos, startA );

	Vector endPos;
	QAngle endA;
	Studio_AnimPosition( panim, flCycleTo, endPos, endA );

	Vector tmp = endPos - startPos;
	deltaAngle.y = endA.y - startA.y;
	VectorYawRotate( tmp, -startA.y, deltaPos );

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: finds how much of an animation to play to move given linear distance
//-----------------------------------------------------------------------------

float Studio_FindAnimDistance( mstudioanimdesc_t *panim, float flDist )
{
	float	prevframe = 0;

	if (flDist <= 0)
		return 0.0;

	for (int i = 0; i < panim->nummovements; i++)
	{
		mstudiomovement_t *pmove = panim->pMovement( i );

		float flMove = (pmove->v0 + pmove->v1) * 0.5;

		if (flMove >= flDist)
		{
			float root1, root2;

			// d = V0 * t + 1/2 (V1-V0) * t^2
			if (SolveQuadratic( 0.5 * (pmove->v1 - pmove->v0), pmove->v0, -flDist, root1, root2 ))
			{
				float cpf = 1.0 / (panim->numframes - 1);  // cycles per frame

				return (prevframe + root1 * (pmove->endframe - prevframe)) * cpf;
			}
			return 0.0;
		}
		else
		{
			flDist -= flMove;
			prevframe = pmove->endframe;
		}
	}
	return 1.0;
}


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in a sequences cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------

bool Studio_SeqMovement( const CStudioHdr *pStudioHdr, int iSequence, float flCycleFrom, float flCycleTo, const float poseParameter[], Vector &deltaPos, QAngle &deltaAngles )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );

	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	deltaPos.Init( );
	deltaAngles.Init( );

	bool found = false;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			Vector localPos;
			QAngle localAngles;

			localPos.Init();
			localAngles.Init();

			if (Studio_AnimMovement( panim[i], flCycleFrom, flCycleTo, localPos, localAngles ))
			{
				found = true;
				deltaPos = deltaPos + localPos * weight[i];
				// FIXME: this makes no sense
				deltaAngles = deltaAngles + localAngles * weight[i];
			}
			else if (!HasFlag(panim[i]->flags, StudioSequenceFlags::Delta) && panim[i]->nummovements == 0 && seqdesc.weight(0) > 0.0)
			{
				found = true;
			}
		}
	}

	// FIXME: add support for more than just start 0 and end 0 pose param layers (currently no cycle handling or angular delta)
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
			Vector layerPos;
			//QAngle layerAngles;

			layerPos.Init();
			//layerAngles.Init();

			bool bLayerFound = false;

			mstudioseqdesc_t &seqdescLocal = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequenceLocal );
			Studio_SeqAnims( pStudioHdr, seqdescLocal, iSequenceLocal, poseParameter, panim, weight );

			for (int i = 0; i < 4; i++)
			{
				if (weight[i])
				{
					Vector localPos;
					QAngle localAngles;

					localPos.Init();
					//localAngles.Init();

					if ( Studio_AnimMovement( panim[i], flCycleFrom, flCycleTo, localPos, localAngles ) )
					{
						bLayerFound = true;
						layerPos = layerPos + localPos * weight[i];
						// FIXME: do angles
						//layerAngles = layerAngles + localAngles * weight[i];
					}
				}
			}

			if ( bLayerFound )
			{
				deltaPos = Lerp( layerWeight, deltaPos, layerPos );
			}
		}
	}

	return found;
}


//-----------------------------------------------------------------------------
// Purpose: calculate changes in position and angle between two points in a sequences cycle
// Output:	updated position and angle, relative to CycleFrom being at the origin
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------
float Studio_SeqMovementAndDuration( const CStudioHdr *pStudioHdr, int iSequence, float flCycleFrom, float flCycleTo, const float poseParameter[], Vector &deltaPos )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );

	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	deltaPos.Init( );

	Vector localPos;
	QAngle localAngles;

	float t = 0;
	for ( int i = 0; i < 4; i++ )
	{
		if ( weight[i] == 0.0f )
			continue;

		if ( panim[i]->numframes > 1 )
		{
			t += ( panim[i]->fps / ( panim[i]->numframes - 1 ) ) * weight[i];
		}

		if ( Studio_AnimMovement( panim[i], flCycleFrom, flCycleTo, localPos, localAngles ) )
		{
			VectorMA( deltaPos, weight[i], localPos, deltaPos );
		}
	}
	return ( t != 0.0f ) ? 1.0f / t : 0.0f;
}


//-----------------------------------------------------------------------------
// Purpose: calculate instantaneous velocity in ips at a given point in the sequence's cycle
// Output:	velocity vector, relative to identity orientation
//			returns false if sequence is not a movement sequence
//-----------------------------------------------------------------------------

bool Studio_SeqVelocity( const CStudioHdr *pStudioHdr, int iSequence, float flCycle, const float poseParameter[], Vector &vecVelocity )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );
	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	vecVelocity.Init( );

	bool found = false;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			Vector vecLocalVelocity;

			if (Studio_AnimVelocity( panim[i], flCycle, vecLocalVelocity ))
			{
				vecVelocity = vecVelocity + vecLocalVelocity * weight[i];
				found = true;
			}
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose: finds how much of an sequence to play to move given linear distance
//-----------------------------------------------------------------------------

float Studio_FindSeqDistance( const CStudioHdr *pStudioHdr, int iSequence, const float poseParameter[], float flDist )
{
	mstudioanimdesc_t *panim[4];
	float	weight[4];

	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );
	Studio_SeqAnims( pStudioHdr, seqdesc, iSequence, poseParameter, panim, weight );

	float flCycle = 0;

	for (int i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			float flLocalCycle = Studio_FindAnimDistance( panim[i], flDist );
			flCycle = flCycle + flLocalCycle * weight[i];
		}
	}
	return flCycle;
}

//-----------------------------------------------------------------------------
// Purpose: lookup attachment by name
//-----------------------------------------------------------------------------

int Studio_FindAttachment( const CStudioHdr *pStudioHdr, const char *pAttachmentName )
{
	if ( pStudioHdr && pStudioHdr->SequencesAvailable() )
	{
		// Extract the bone index from the name
		for (int i = 0; i < pStudioHdr->GetNumAttachments(); i++)
		{
			if (!stricmp(pAttachmentName,((CStudioHdr *)pStudioHdr)->pAttachment(i).pszName( )))
			{
				return i;
			}
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: lookup attachments by substring. Randomly return one of the matching attachments.
//-----------------------------------------------------------------------------

int Studio_FindRandomAttachment( const CStudioHdr *pStudioHdr, const char *pAttachmentName )
{
	if ( pStudioHdr )
	{
		// First move them all matching attachments into a list
		CUtlVector<int> matchingAttachments;

		// Extract the bone index from the name
		for (int i = 0; i < pStudioHdr->GetNumAttachments(); i++)
		{
			if ( strstr( ((CStudioHdr *)pStudioHdr)->pAttachment(i).pszName(), pAttachmentName ) )
			{
				matchingAttachments.AddToTail(i);
			}
		}

		// Then randomly return one of the attachments
		if ( matchingAttachments.Count() > 0 )
			return matchingAttachments[ RandomInt( 0, matchingAttachments.Count()-1 ) ];
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: lookup bone by name
//-----------------------------------------------------------------------------

int Studio_BoneIndexByName( const CStudioHdr *pStudioHdr, const char *pName )
{
	// binary search for the bone matching pName
	int start = 0, end = pStudioHdr->numbones()-1;
	const uint8_t *pBoneTable = pStudioHdr->GetBoneTableSortedByName();
	const mstudiobone_t *pbones = pStudioHdr->pBone( 0 );
	while (start <= end)
	{
		int mid = (start + end) >> 1;
		int cmp = Q_stricmp( pbones[pBoneTable[mid]].pszName(), pName );

		if ( cmp < 0 )
		{
			start = mid + 1;
		}
		else if ( cmp > 0 )
		{
			end = mid - 1;
		}
		else
		{
			return pBoneTable[mid];
		}
	}
	return -1;
}

const char *Studio_GetDefaultSurfaceProps( CStudioHdr *pstudiohdr )
{
	return pstudiohdr->pszSurfaceProp();
}

float Studio_GetMass( CStudioHdr *pstudiohdr )
{
	return pstudiohdr->mass();
}

//-----------------------------------------------------------------------------
// Purpose: return pointer to sequence key value buffer
//-----------------------------------------------------------------------------

const char *Studio_GetKeyValueText( const CStudioHdr *pStudioHdr, int iSequence )
{
	if (pStudioHdr && pStudioHdr->SequencesAvailable())
	{
		if (iSequence >= 0 && iSequence < pStudioHdr->GetNumSeq())
		{
			return ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence ).KeyValueText();
		}
	}
	return nullptr;
}

bool Studio_PrefetchSequence( const CStudioHdr *pStudioHdr, int iSequence )
{
	bool pendingload = false;
	mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pStudioHdr)->pSeqdesc( iSequence );
	int size0 = seqdesc.groupsize[ 0 ];
	int size1 = seqdesc.groupsize[ 1 ];
	for ( int i = 0; i < size0; ++i )
	{
		for ( int j = 0; j < size1; ++j )
		{
			mstudioanimdesc_t &animdesc = ((CStudioHdr *)pStudioHdr)->pAnimdesc( seqdesc.anim( i, j ) );
			int iFrame = 0;
			uint8_t *panim = animdesc.pAnim( &iFrame );
			if ( !panim )
			{
				pendingload = true;
			}
		}
	}

	// Everything for this sequence is resident?
	return !pendingload;
}


//-----------------------------------------------------------------------------
// Purpose: Drive a flex controller from a component of a bone
//-----------------------------------------------------------------------------
void Studio_RunBoneFlexDrivers( float *pflFlexControllerWeights, const CStudioHdr *pStudioHdr, const Vector *pvPositions, const matrix3x4_t *pBoneToWorld, const matrix3x4_t &mRootToWorld )
{
	bool bRootToWorldInvComputed = false;
	matrix3x4_t mRootToWorldInv;
	matrix3x4_t mParentInv;
	matrix3x4_t mBoneLocal;

	const int nBoneFlexDriverCount = pStudioHdr->BoneFlexDriverCount();

	for ( int i = 0; i < nBoneFlexDriverCount; ++i )
	{
		const mstudioboneflexdriver_t *pBoneFlexDriver = pStudioHdr->BoneFlexDriver( i );
		const mstudiobone_t *pStudioBone = pStudioHdr->pBone( pBoneFlexDriver->m_nBoneIndex );

		const int nControllerCount = pBoneFlexDriver->m_nControlCount;

		if (HasFlag(pStudioBone->flags, BoneFlags::UsedByBoneMerge))
		{
			// The local space version of the bone is not available if this is a bonemerged bone
			// so do the slow computation of the local version of the bone from boneToWorld

			if ( pStudioBone->parent < 0 )
			{
				if ( !bRootToWorldInvComputed )
				{
					MatrixInvert( mRootToWorld, mRootToWorldInv );
					bRootToWorldInvComputed = true;
				}

				MatrixMultiply( mRootToWorldInv, pBoneToWorld[ pBoneFlexDriver->m_nBoneIndex ], mBoneLocal );
			}
			else
			{
				MatrixInvert( pBoneToWorld[ pStudioBone->parent ], mParentInv );
				MatrixMultiply( mParentInv, pBoneToWorld[ pBoneFlexDriver->m_nBoneIndex ], mBoneLocal );
			}

			for ( int j = 0; j < nControllerCount; ++j )
			{
				const mstudioboneflexdrivercontrol_t *pController = pBoneFlexDriver->pBoneFlexDriverControl( j );
				const mstudioflexcontroller_t *pFlexController = pStudioHdr->pFlexcontroller( static_cast< LocalFlexController_t >( pController->m_nFlexControllerIndex ) );

				if ( pFlexController->localToGlobal < 0 )
					continue;

				Assert( pController->m_nFlexControllerIndex >= 0 && pController->m_nFlexControllerIndex < pStudioHdr->numflexcontrollers() );
				Assert( pController->m_nBoneComponent >= 0 && pController->m_nBoneComponent <= 2 );
				pflFlexControllerWeights[pFlexController->localToGlobal] =
					RemapValClamped( mBoneLocal[pController->m_nBoneComponent][3], pController->m_flMin, pController->m_flMax, 0.0f, 1.0f );
			}
		}
		else
		{
			// Use the local space version of the bone directly for non-bonemerged bones

			const Vector &position = pvPositions[ pBoneFlexDriver->m_nBoneIndex ];

			for ( int j = 0; j < nControllerCount; ++j )
			{
				const mstudioboneflexdrivercontrol_t *pController = pBoneFlexDriver->pBoneFlexDriverControl( j );
				const mstudioflexcontroller_t *pFlexController = pStudioHdr->pFlexcontroller( static_cast< LocalFlexController_t >( pController->m_nFlexControllerIndex ) );

				if ( pFlexController->localToGlobal < 0 )
					continue;

				Assert( pController->m_nFlexControllerIndex >= 0 && pController->m_nFlexControllerIndex < pStudioHdr->numflexcontrollers() );
				Assert( pController->m_nBoneComponent >= 0 && pController->m_nBoneComponent <= 2 );
				pflFlexControllerWeights[pFlexController->localToGlobal] =
					RemapValClamped( position[pController->m_nBoneComponent], pController->m_flMin, pController->m_flMax, 0.0f, 1.0f );
			}
		}
	}
}

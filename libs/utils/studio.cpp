//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "studio.h"

#include "studiomdl/bone_setup.h"
#include <unordered_map>


// preload up to 1 second worth of blocks ahead.
#ifdef _DEBUG
ConVar softbody_debug( "softbody_debug", "0", FCVAR_CHEAT );
ConVar softbody_debug_substr( "softbody_debug_substr", "", FCVAR_CHEAT );
#endif

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioanimdesc_t &studiohdr_t::pAnimdesc_Internal( int i ) const
{ 
	virtualmodel_t *pVModel = (virtualmodel_t *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t *pGroup = &pVModel->m_group[ pVModel->m_anim[i].group ];
	const studiohdr_t *pStudioHdr = pGroup->GetStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalAnimdesc( pVModel->m_anim[i].index );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

uint8_t *mstudioanimdesc_t::pAnimBlock( int block, int index, bool preloadIfMissing ) const
{
	if (block == -1)
	{
		return (uint8_t *) nullptr;
	}
	if (block == 0)
	{
		return (((uint8_t *)this) + index);
	}

	uint8_t *pAnimBlock = pStudiohdr()->GetAnimBlock( block, preloadIfMissing );
	if ( pAnimBlock )
	{
		return pAnimBlock + index;
	}

	return (uint8_t *) nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Indicates if the block has been preloaded already.
//			Returns true if the block is in memory, or is asynchronously loading.
//-----------------------------------------------------------------------------
bool mstudioanimdesc_t::hasAnimBlockBeenPreloaded( int block ) const
{
	return pStudiohdr()->hasAnimBlockBeenPreloaded( block );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

uint8_t *mstudioanimdesc_t::pAnim( int *piFrame ) const
{
	float flStall = 0;
	return pAnim( piFrame, flStall );
}

uint8_t *mstudioanimdesc_t::pAnim( int *piFrame, float &flStall ) const
{
	uint8_t *panim = nullptr;

	int block = animblock;
	int index = animindex;
	int section = 0;

	if (sectionframes != 0)
	{
		if (numframes > sectionframes && *piFrame == numframes - 1)
		{
			// last frame on long anims is stored separately
			*piFrame = 0;
			section = (numframes / sectionframes) + 1;
		}
		else
		{
			section = *piFrame / sectionframes;
			*piFrame -= section * sectionframes;
		}

		block = pSection( section )->animblock;
		index = pSection( section )->animindex;
	}

	if (block == -1)
	{
		// model needs to be recompiled
		return nullptr;
	}

	panim = pAnimBlock( block, index );

	// force a preload of future animations
	if ( sectionframes != 0 )
	{
		// calc how many sections ahead to try looking
		int maxSection = MIN( section + (1.0f * fps / sectionframes), (numframes / sectionframes) ) + 1;
		int prevBlock = block;

		for ( int i = section + 1; i <= maxSection; i++ )
		{
			// if this is a new block, preload it
			if ( pSection( i )->animblock != prevBlock )
			{
				bool preloaded = hasAnimBlockBeenPreloaded( pSection( i )->animblock );
				if ( preloaded == false )
				{
					// This will preload the block
					pAnimBlock( pSection( i )->animblock, pSection( i )->animindex, true );
					// Msg( "[{:8.3f}] precaching {}:{}:{:d}:{:d}.\n",  Plat_FloatTime(), pStudiohdr()->pszName(), pszName(), i, pSection( i )->animblock );
				}

				prevBlock = pSection( i )->animblock;
			}
		}
	}

	if (panim == nullptr)
	{
		// back up until a previously loaded block is found
		while (--section >= 0)
		{
			block = pSection( section )->animblock;
			index = pSection( section )->animindex;
			panim = pAnimBlock( block, index, false );
			if (panim)
			{
				// set it to the last frame in the last valid section
				*piFrame = sectionframes - 1;
				break;
			}
		}
	}

	// try to guess a valid stall time interval (tuned for the X360)
	flStall = 0.0f;
	if (panim == nullptr && section <= 0)
	{
		zeroframestalltime = Plat_FloatTime();
		flStall = 1.0f;
	}
	else if (panim != nullptr && zeroframestalltime != 0.0f)
	{
		float dt = Plat_FloatTime() - zeroframestalltime;
		if (dt >= 0.0)
		{
			flStall = SimpleSpline( clamp( (0.200f - dt) * 5.0, 0.0f, 1.0f ) );
		}

		if (flStall == 0.0f)
		{
			// disable stalltime
			zeroframestalltime = 0.0f;
		}
	}

	return panim;
}

mstudioikrule_t *mstudioanimdesc_t::pIKRule( int i ) const
{
	if (numikrules)
	{
		if (ikruleindex)
		{
			return (mstudioikrule_t *)(((uint8_t *)this) + ikruleindex) + i;
		}
		else
		{
			if (animblock == 0)
			{
				AssertOnce(0); // Should never happen
				return  (mstudioikrule_t *)(((uint8_t *)this) + animblockikruleindex) + i;
			}
			else
			{
				uint8_t *pAnimBlock = pStudiohdr()->GetAnimBlock( animblock );

				if ( pAnimBlock )
				{
					return (mstudioikrule_t *)(pAnimBlock + animblockikruleindex) + i;
				}
			}
		}
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

bool studiohdr_t::SequencesAvailable() const
{
	if (numincludemodels == 0)
	{
		return true;
	}

	return ( GetVirtualModel() != nullptr);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::GetNumSeq_Internal( void ) const
{
	virtualmodel_t *pVModel = (virtualmodel_t *)GetVirtualModel();
	Assert( pVModel );
	return pVModel->m_seq.size();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioseqdesc_t &studiohdr_t::pSeqdesc_Internal( int i ) const
{
	virtualmodel_t *pVModel = (virtualmodel_t *)GetVirtualModel();
	Assert( pVModel );

	if ( !pVModel )
	{
		return *pLocalSeqdesc( i );
	}

	virtualgroup_t *pGroup = &pVModel->m_group[ pVModel->m_seq[i].group ];
	const studiohdr_t *pStudioHdr = pGroup->GetStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalSeqdesc( pVModel->m_seq[i].index );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::iRelativeAnim_Internal( int baseseq, int relanim ) const
{
	virtualmodel_t *pVModel = (virtualmodel_t *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t *pGroup = &pVModel->m_group[ pVModel->m_seq[baseseq].group ];

	return pGroup->masterAnim[ relanim ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

const mstudioposeparamdesc_t &studiohdr_t::pPoseParameter( int i )
{
	if (numincludemodels == 0)
	{
		return *pLocalPoseParameter( i );
	}

	virtualmodel_t *pVModel = (virtualmodel_t *)GetVirtualModel();
	Assert( pVModel );

	if ( pVModel->m_pose[i].group == 0)
		return *pLocalPoseParameter( pVModel->m_pose[i].index );

	virtualgroup_t *pGroup = &pVModel->m_group[ pVModel->m_pose[i].group ];

	const studiohdr_t *pStudioHdr = pGroup->GetStudioHdr();
	Assert( pStudioHdr );

	return *pStudioHdr->pLocalPoseParameter( pVModel->m_pose[i].index );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int studiohdr_t::GetSharedPoseParameter( int iSequence, int iLocalPose ) const
{
	if (numincludemodels == 0)
	{
		return iLocalPose;
	}

	if (iLocalPose == -1)
		return iLocalPose;

	virtualmodel_t *pVModel = (virtualmodel_t *)GetVirtualModel();
	Assert( pVModel );

	virtualgroup_t *pGroup = &pVModel->m_group[ pVModel->m_seq[iSequence].group ];

	return pGroup->masterPose[iLocalPose];
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


int	studiohdr_t::CountAutoplaySequences() const
{
	int count = 0;
	for (int i = 0; i < GetNumSeq(); i++)
	{
		mstudioseqdesc_t &seqdesc = pSeqdesc( i );
		if (HasFlag(seqdesc.flags, StudioSequenceFlags::Autoplay))
		{
			count++;
		}
	}
	return count;
}

int	studiohdr_t::CopyAutoplaySequences( unsigned short *pOut, int outCount ) const
{
	int outIndex = 0;
	for (int i = 0; i < GetNumSeq() && outIndex < outCount; i++)
	{
		mstudioseqdesc_t &seqdesc = pSeqdesc( i );
		if (HasFlag(seqdesc.flags, StudioSequenceFlags::Autoplay))
		{
			pOut[outIndex] = i;
			outIndex++;
		}
	}
	return outIndex;
}

//-----------------------------------------------------------------------------
// Purpose:	maps local sequence bone to global bone
//-----------------------------------------------------------------------------

int	studiohdr_t::RemapSeqBone( int iSequence, int iLocalBone ) const
{
	// remap bone
	virtualmodel_t *pVModel = GetVirtualModel();
	if (pVModel)
	{
		const virtualgroup_t *pSeqGroup = pVModel->pSeqGroup( iSequence );
		return pSeqGroup->masterBone[iLocalBone];
	}
	return iLocalBone;
}






//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

CStudioHdr::CStudioHdr( void )
{
	Init(nullptr);
}

CStudioHdr::CStudioHdr( const studiohdr_t *pStudioHdr )
{
	Init( pStudioHdr );
}

void CStudioHdr::Init( const studiohdr_t *pStudioHdr )
{
	m_pStudioHdr = pStudioHdr;

	m_pVModel = nullptr;
	m_pSoftbody = nullptr;
	m_pStudioHdrCache.RemoveAll();

	if (m_pStudioHdr == nullptr)
	{
		return;
	}


	if (m_pStudioHdr->numincludemodels != 0)
	{
		ResetVModel( m_pStudioHdr->GetVirtualModel() );
	}

	m_boneFlags.EnsureCount( numbones() );
	m_boneParent.EnsureCount( numbones() );
	for (int i = 0; i < numbones(); i++)
	{
		m_boneFlags[i] = pBone( i )->flags;
		m_boneParent[i] = pBone( i )->parent;
	}



	m_pActivityToSequence = nullptr;
}

void CStudioHdr::Term()
{
	CActivityToSequenceMapping::ReleaseMapping( m_pActivityToSequence );
	m_pActivityToSequence = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

bool CStudioHdr::SequencesAvailable() const
{
	if (m_pStudioHdr->numincludemodels == 0)
	{
		return true;
	}

	if (m_pVModel == nullptr)
	{
		// repoll m_pVModel
		return (ResetVModel( m_pStudioHdr->GetVirtualModel() ) != nullptr);
	}
	else
		return true;
}


const virtualmodel_t * CStudioHdr::ResetVModel( const virtualmodel_t *pVModel ) const
{
	if (pVModel != nullptr)
	{
		m_pVModel = (virtualmodel_t *)pVModel;
		m_pStudioHdrCache.SetCount( m_pVModel->m_group.size() );

		int i;
		for (i = 0; i < m_pStudioHdrCache.size(); i++)
		{
			m_pStudioHdrCache[ i ] = nullptr;
		}

		return const_cast<virtualmodel_t *>(pVModel);
	}
	else
	{
		m_pVModel = nullptr;
		return nullptr;
	}
}

const studiohdr_t *CStudioHdr::GroupStudioHdr( int i )
{
	if ( !this )
	{
		ExecuteNTimes( 5, Warning( "Call to NULL CStudioHdr::GroupStudioHdr()\n" ) );
	}

	if ( !m_pStudioHdrCache.IsValidIndex( i ) )
	{
		const char *pszName;
		pszName = ( m_pStudioHdr ) ? m_pStudioHdr->pszName() : "<<null>>";
		ExecuteNTimes( 5, Warning( "Invalid index passed to CStudioHdr({})::GroupStudioHdr(): {:d} [{:d}]\n", pszName, i, m_pStudioHdrCache.size() ) );
		DebuggerBreakIfDebugging();
		return m_pStudioHdr; // return something known to probably exist, certainly things will be messed up, but hopefully not crash before the warning is noticed
	}

	const studiohdr_t *pStudioHdr = m_pStudioHdrCache[ i ];

	if (pStudioHdr == nullptr)
	{
		virtualgroup_t *pGroup = &m_pVModel->m_group[ i ];
		pStudioHdr = pGroup->GetStudioHdr();
		m_pStudioHdrCache[ i ] = pStudioHdr;
	}

	Assert( pStudioHdr );
	return pStudioHdr;
}





mstudioanimdesc_t &CStudioHdr::pAnimdesc_Internal( int i )
{
	const studiohdr_t *pStudioHdr = GroupStudioHdr( m_pVModel->m_anim[i].group );

	return *pStudioHdr->pLocalAnimdesc( m_pVModel->m_anim[i].index );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::GetNumSeq_Internal( void ) const
{
	return m_pVModel->m_seq.size();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

mstudioseqdesc_t &CStudioHdr::pSeqdesc_Internal( int i )
{
	Assert( i >= 0 && i < GetNumSeq() );
	if ( i < 0 || i >= GetNumSeq() )
	{
		// Avoid reading random memory.
		i = 0;
	}

	const studiohdr_t *pStudioHdr = GroupStudioHdr( m_pVModel->m_seq[i].group );

	return *pStudioHdr->pLocalSeqdesc( m_pVModel->m_seq[i].index );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::iRelativeAnim_Internal( int baseseq, int relanim ) const
{
	virtualgroup_t *pGroup = &m_pVModel->m_group[ m_pVModel->m_seq[baseseq].group ];

	return pGroup->masterAnim[ relanim ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::iRelativeSeq( int baseseq, int relseq ) const
{
	if (m_pVModel == nullptr)
	{
		return relseq;
	}

	Assert( m_pVModel );

	virtualgroup_t *pGroup = &m_pVModel->m_group[ m_pVModel->m_seq[baseseq].group ];

	return pGroup->masterSeq[ relseq ];
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

const mstudioposeparamdesc_t &CStudioHdr::pPoseParameter( int i )
{
	if (m_pVModel == nullptr)
	{
		return *m_pStudioHdr->pLocalPoseParameter( i );
	}

	if ( m_pVModel->m_pose[i].group == 0)
		return *m_pStudioHdr->pLocalPoseParameter( m_pVModel->m_pose[i].index );

	const studiohdr_t *pStudioHdr = GroupStudioHdr( m_pVModel->m_pose[i].group );

	return *pStudioHdr->pLocalPoseParameter( m_pVModel->m_pose[i].index );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CStudioHdr::GetSharedPoseParameter( int iSequence, int iLocalPose ) const
{
	if (m_pVModel == nullptr)
	{
		return iLocalPose;
	}

	if (iLocalPose == -1)
		return iLocalPose;

	Assert( m_pVModel );

	virtualgroup_t *pGroup = &m_pVModel->m_group[ m_pVModel->m_seq[iSequence].group ];

	return pGroup->masterPose[iLocalPose];
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


#if 0
int	CStudioHdr::CountAutoplaySequences() const
{
	int count = 0;
	for (int i = 0; i < GetNumSeq(); i++)
	{
		mstudioseqdesc_t &seqdesc = pSeqdesc( i );
		if (seqdesc.flags & StudioSequenceFlags::Autoplay)
		{
			count++;
		}
	}
	return count;
}

int	CStudioHdr::CopyAutoplaySequences( unsigned short *pOut, int outCount ) const
{
	int outIndex = 0;
	for (int i = 0; i < GetNumSeq() && outIndex < outCount; i++)
	{
		mstudioseqdesc_t &seqdesc = pSeqdesc( i );
		if (seqdesc.flags & StudioSequenceFlags::Autoplay)
		{
			pOut[outIndex] = i;
			outIndex++;
		}
	}
	return outIndex;
}

#endif

//-----------------------------------------------------------------------------
// Purpose:	maps local sequence bone to global bone
//-----------------------------------------------------------------------------

int	CStudioHdr::RemapSeqBone( int iSequence, int iLocalBone ) const
{
	// remap bone
	if (m_pVModel)
	{
		const virtualgroup_t *pSeqGroup = m_pVModel->pSeqGroup( iSequence );
		return pSeqGroup->masterBone[iLocalBone];
	}
	return iLocalBone;
}

#define USE_OLD_FLEX_RULES_INTERPRETER


//-----------------------------------------------------------------------------
//	propagate flags all the way down
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//	CODE PERTAINING TO ACTIVITY->SEQUENCE MAPPING SUBCLASS
//-----------------------------------------------------------------------------
#define iabs(i) (( (i) >= 0 ) ? (i) : -(i) )

CUtlSymbolTable g_ActivityModifiersTable;

extern void SetActivityForSequence( CStudioHdr *pstudiohdr, int i );
void CStudioHdr::CActivityToSequenceMapping::Initialize( const CStudioHdr * __restrict pstudiohdr )
{

	// Algorithm: walk through every sequence in the model, determine to which activity
	// it corresponds, and keep a count of sequences per activity. Once the total count
	// is available, allocate an array large enough to contain them all, update the
	// starting indices for every activity's section in the array, and go back through,
	// populating the array with its data.

	m_pStudioHdr = pstudiohdr->m_pStudioHdr;

	AssertMsg1( m_pSequenceTuples == NULL, "Tried to double-initialize sequence mapping for %s", pstudiohdr->pszName() );
	if ( m_pSequenceTuples != nullptr)
		return; // don't double initialize.

	SetValidation(pstudiohdr);

	if ( ! pstudiohdr->SequencesAvailable() )
		return; // nothing to do.

	// Some studio headers have no activities at all. In those
	// cases we can avoid a lot of this effort.
	bool bFoundOne = false;

	// for each sequence in the header...
	const int NumSeq = pstudiohdr->GetNumSeq();
	for ( int i = 0 ; i < NumSeq ; ++i )
	{
		const mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pstudiohdr)->pSeqdesc( i );
#if defined(SERVER_DLL) || defined(CLIENT_DLL) || defined(GAME_DLL)
		if (!(seqdesc.flags & StudioSequenceFlags::Activity))
		{
			// AssertMsg2( false, "Sequence %d on studiohdr %s didn't have its activity initialized!", i, pstudiohdr->pszName() );
			SetActivityForSequence( (CStudioHdr *)pstudiohdr, i );
		}
#endif

		// is there an activity associated with this sequence?
		if (seqdesc.activity >= 0)
		{
			bFoundOne = true;

			// look up if we already have an entry. First we need to make a speculative one --
			HashValueType entry(seqdesc.activity, 0, 1, iabs(seqdesc.actweight));
			UtlHashHandle_t handle = m_ActToSeqHash.Find(entry);
			if ( m_ActToSeqHash.IsValidHandle(handle) )
			{
				// we already have an entry and must update it by incrementing count
				HashValueType * __restrict toUpdate = &m_ActToSeqHash.Element(handle);
				toUpdate->count += 1;
				toUpdate->totalWeight += iabs(seqdesc.actweight);
				Assert( toUpdate->totalWeight > 0 );
			}
			else
			{
				// we do not have an entry yet; create one.
				m_ActToSeqHash.Insert(entry);
			}
		}
	}

	// if we found nothing, don't bother with any other initialization!
	if (!bFoundOne)
		return;

	// Now, create starting indices for each activity. For an activity n,
	// the starting index is of course the sum of counts [0..n-1].
	int sequenceCount = 0;
	int topActivity = 0; // this will store the highest seen activity number (used later to make an ad hoc map on the stack)
	for ( UtlHashHandle_t handle = m_ActToSeqHash.GetFirstHandle() ;
		  m_ActToSeqHash.IsValidHandle(handle) ;
		  handle = m_ActToSeqHash.GetNextHandle(handle) )
	{
		HashValueType &element = m_ActToSeqHash[handle];
		element.startingIdx = sequenceCount;
		sequenceCount += element.count;
		topActivity = MAX(topActivity, element.activityIdx);
	}


	// Allocate the actual array of sequence information. Note the use of restrict;
	// this is an important optimization, but means that you must never refer to this
	// array through m_pSequenceTuples in the scope of this function.
	SequenceTuple * __restrict tupleList = new SequenceTuple[sequenceCount];
	m_pSequenceTuples = tupleList; // save it off -- NEVER USE m_pSequenceTuples in this function!
	m_iSequenceTuplesCount = sequenceCount;



	// Now we're going to actually populate that list with the relevant data.
	// First, create an array on the stack to store how many sequences we've written
	// so far for each activity. (This is basically a very simple way of doing a map.)
	// This stack may potentially grow very large; so if you have problems with it,
	// go to a utlmap or similar structure.
	unsigned int allocsize = (topActivity + 1) * sizeof(int);
#define ALIGN_VALUE( val, alignment ) ( ( val + alignment - 1 ) & ~( alignment - 1 ) ) //  need macro for constant expression
	allocsize = ALIGN_VALUE(allocsize,16);
	int * __restrict seqsPerAct = static_cast<int *>(stackalloc(allocsize));
	memset(seqsPerAct, 0, allocsize);

	// okay, walk through all the sequences again, and write the relevant data into
	// our little table.
	for ( int i = 0 ; i < NumSeq ; ++i )
	{
		const mstudioseqdesc_t &seqdesc = ((CStudioHdr *)pstudiohdr)->pSeqdesc( i );
		if (seqdesc.activity >= 0)
		{
			const HashValueType &element = m_ActToSeqHash[m_ActToSeqHash.Find(HashValueType(seqdesc.activity, 0, 0, 0))];

			// If this assert trips, we've written more sequences per activity than we allocated
			// (therefore there must have been a miscount in the first for loop above).
			int tupleOffset = seqsPerAct[seqdesc.activity];
			Assert( tupleOffset < element.count );

			if ( seqdesc.numactivitymodifiers > 0 )
			{
				// add entries for this model's activity modifiers
				(tupleList + element.startingIdx + tupleOffset)->pActivityModifiers = new CUtlSymbol[ seqdesc.numactivitymodifiers ];
				(tupleList + element.startingIdx + tupleOffset)->iNumActivityModifiers = seqdesc.numactivitymodifiers;

				for ( int k = 0; k < seqdesc.numactivitymodifiers; k++ )
				{
					(tupleList + element.startingIdx + tupleOffset)->pActivityModifiers[ k ] = g_ActivityModifiersTable.AddString( seqdesc.pActivityModifier( k )->pszName() );
				}
			}
			else
			{
				(tupleList + element.startingIdx + tupleOffset)->pActivityModifiers = nullptr;
				(tupleList + element.startingIdx + tupleOffset)->iNumActivityModifiers = 0;
			}

			// You might be tempted to collapse this pointer math into a single pointer --
			// don't! the tuple list is marked __restrict above.
			(tupleList + element.startingIdx + tupleOffset)->seqnum = i; // store sequence number
			(tupleList + element.startingIdx + tupleOffset)->weight = iabs(seqdesc.actweight);

			// We can't have weights of 0
			// Assert( (tupleList + element.startingIdx + tupleOffset)->weight > 0 );
			if ( (tupleList + element.startingIdx + tupleOffset)->weight == 0 )
			{
				(tupleList + element.startingIdx + tupleOffset)->weight = 1;
			}

			seqsPerAct[seqdesc.activity] += 1;
		}
	}

#ifdef DBGFLAG_ASSERT
	// double check that we wrote exactly the right number of sequences.
	unsigned int chkSequenceCount = 0;
	for (int j = 0 ; j <= topActivity ; ++j)
	{
		chkSequenceCount += seqsPerAct[j];
	}
	Assert(chkSequenceCount == m_iSequenceTuplesCount);
#endif

}

int CStudioHdr::CActivityToSequenceMapping::NumSequencesForActivity( int forActivity )
{
	// If this trips, you've called this function on something that doesn't
	// have activities.
	//Assert(m_pSequenceTuples != NULL);
	if ( m_pSequenceTuples == nullptr)
		return 0;

	HashValueType entry(forActivity, 0, 0, 0);
	UtlHashHandle_t handle = m_ActToSeqHash.Find(entry);
	if (m_ActToSeqHash.IsValidHandle(handle))
	{
		return m_ActToSeqHash[handle].count;
	}
	else
	{
		return 0;
	}
}

static CStudioHdr::CActivityToSequenceMapping emptyMapping;

void CStudioHdr::CActivityToSequenceMapping::SetValidation( const CStudioHdr *RESTRICT pstudiohdr ) RESTRICT
{
	m_expectedVModel = pstudiohdr->GetVirtualModel();
}

struct StudioHdrToActivityMapEntry_t
{
	int32_t checksum;
	char name[64];
	int	nRefs;
	CStudioHdr::CActivityToSequenceMapping *pMap;
};

std::unordered_map<const studiohdr_t *, StudioHdrToActivityMapEntry_t> g_StudioHdrToActivityMaps;

CStudioHdr::CActivityToSequenceMapping *CStudioHdr::CActivityToSequenceMapping::FindMapping( const CStudioHdr *pHdr )
{


	if ( !pHdr->SequencesAvailable() || pHdr->GetNumSeq() <= 1 )
	{
		return &emptyMapping;
	}

	Assert( !pHdr->m_pActivityToSequence );

	const studiohdr_t *pRealHdr = pHdr->m_pStudioHdr;
	auto it = g_StudioHdrToActivityMaps.find( pRealHdr );
	if ( it != g_StudioHdrToActivityMaps.end() )
	{
		StudioHdrToActivityMapEntry_t &entry = it->second;
		if ( entry.checksum != pRealHdr->checksum || Q_strcmp( entry.name, pRealHdr->name ) != 0 )
		{
			AssertFatal( entry.nRefs == 0 );
			delete entry.pMap;
			g_StudioHdrToActivityMaps.erase( it );
		}
		else
		{
			entry.nRefs++;
			return entry.pMap;
		}
	}

	StudioHdrToActivityMapEntry_t &entry = g_StudioHdrToActivityMaps[pRealHdr];
	entry.checksum = pRealHdr->checksum;
	Q_strncpy( entry.name, pRealHdr->name, 64 );
	entry.nRefs = 1;
	entry.pMap = new CStudioHdr::CActivityToSequenceMapping;
	entry.pMap->Initialize( pHdr );

	return entry.pMap;
}

void CStudioHdr::CActivityToSequenceMapping::ReleaseMapping( CActivityToSequenceMapping *pMap )
{
	if ( pMap && pMap != &emptyMapping)
	{

		const auto it = g_StudioHdrToActivityMaps.find( pMap->m_pStudioHdr );
		if ( it != g_StudioHdrToActivityMaps.end() )
		{
			Assert( it->second.nRefs > 0 );
			it->second.nRefs--;
		}
		else
		{
			Assert( 0 );
		}
	}
}





//-----------------------------------------------------------------------------
//	
//-----------------------------------------------------------------------------


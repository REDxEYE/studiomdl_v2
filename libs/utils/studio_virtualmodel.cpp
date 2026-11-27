//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Builds virtualmodel_t (merged view of a model and its $includemodel models).
//          Ported from CS:GO public/studio_virtualmodel.cpp.
//
//          The host must provide studiohdr_t::FindModel (and GetVirtualModel,
//          virtualgroup_t::GetStudioHdr, ...). Callers must serialize use of a
//          virtualmodel_t and of the shared sequence/animation lookup tables below;
//          CS:GO's per-model mutex is not ported.
//
//=============================================================================//

#include <limits.h>
#include <map>
#include <string>
#include <vector>
#include "studio.h"
#include "tier1/strtools.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

// a string table to speed up searching for sequences in the current virtual model
struct CaseInsensitiveLess
{
	bool operator()( const std::string &a, const std::string &b ) const
	{
		return V_stricmp( a.c_str(), b.c_str() ) < 0;
	}
};

typedef std::map<std::string, short, CaseInsensitiveLess> ModelLookupTable_t;

struct modellookup_t
{
	ModelLookupTable_t seqTable;
	ModelLookupTable_t animTable;
};

static std::vector<modellookup_t> g_ModelLookup;
static int g_ModelLookupIndex = -1;

inline bool HasLookupTable()
{
	return g_ModelLookupIndex >= 0 ? true : false;
}

inline ModelLookupTable_t *GetSeqTable()
{
	return &g_ModelLookup[g_ModelLookupIndex].seqTable;
}

inline ModelLookupTable_t *GetAnimTable()
{
	return &g_ModelLookup[g_ModelLookupIndex].animTable;
}

class CModelLookupContext
{
public:
	CModelLookupContext(int group, const studiohdr_t *pStudioHdr);
	~CModelLookupContext();

private:
	int		m_lookupIndex;
};

CModelLookupContext::CModelLookupContext(int group, const studiohdr_t *pStudioHdr)
{
	m_lookupIndex = -1;
	if ( group == 0 && pStudioHdr->numincludemodels )
	{
		g_ModelLookup.emplace_back();
		m_lookupIndex = static_cast<int>( g_ModelLookup.size() ) - 1;
		g_ModelLookupIndex = m_lookupIndex;
	}
}

CModelLookupContext::~CModelLookupContext()
{
	if ( m_lookupIndex >= 0 )
	{
		Assert(m_lookupIndex == ((int)g_ModelLookup.size()-1));
		g_ModelLookup.pop_back();
		g_ModelLookupIndex = static_cast<int>( g_ModelLookup.size() ) - 1;
	}
}

void virtualmodel_t::AppendModels( int group, const studiohdr_t *pStudioHdr )
{
	// build a search table if necesary
	CModelLookupContext ctx(group, pStudioHdr);

	AppendSequences( group, pStudioHdr );
	AppendAnimations( group, pStudioHdr );
	AppendBonemap( group, pStudioHdr );
	AppendAttachments( group, pStudioHdr );
	AppendPoseParameters( group, pStudioHdr );
	AppendNodes( group, pStudioHdr );
	AppendIKLocks( group, pStudioHdr );

	struct HandleAndHeader_t
	{
		void				*handle;
		const studiohdr_t	*pHdr;
	};
	HandleAndHeader_t list[64];

	// determine quantity of valid include models in one pass only
	// temporarily cache results off, otherwise FindModel() causes ref counting problems
	int j;
	int nValidIncludes = 0;
	for (j = 0; j < pStudioHdr->numincludemodels; j++)
	{
		// find model (increases ref count)
		void *tmp = nullptr;
		const studiohdr_t *pTmpHdr = pStudioHdr->FindModel( &tmp, pStudioHdr->pModelGroup( j )->pszName() );
		if ( pTmpHdr )
		{
			if ( nValidIncludes >= ARRAYSIZE( list ) )
			{
				// would cause stack overflow
				Assert( 0 );
				break;
			}

			list[nValidIncludes].handle = tmp;
			list[nValidIncludes].pHdr = pTmpHdr;
			nValidIncludes++;
		}
	}

	if ( nValidIncludes )
	{
		m_group.EnsureCapacity( m_group.size() + nValidIncludes );
		for (j = 0; j < nValidIncludes; j++)
		{
			int group = m_group.AddToTail();
			m_group[group].cache = list[j].handle;
			AppendModels( group, list[j].pHdr );
		}
	}

	UpdateAutoplaySequences( pStudioHdr );
}

void virtualmodel_t::AppendSequences( int group, const studiohdr_t *pStudioHdr )
{
	int numCheck = m_seq.size();

	int j, k;

	CUtlVector< virtualsequence_t > seq;

	seq = m_seq;

	m_group[ group ].masterSeq.SetCount( pStudioHdr->numlocalseq );

	for (j = 0; j < pStudioHdr->numlocalseq; j++)
	{
		const mstudioseqdesc_t *seqdesc = pStudioHdr->pLocalSeqdesc( j );
		char *s1 = seqdesc->pszLabel();

		if ( HasLookupTable() )
		{
			k = numCheck;
			auto it = GetSeqTable()->find( s1 );
			if ( it != GetSeqTable()->end() )
			{
				k = it->second;
			}
		}
		else
		{
			for (k = 0; k < numCheck; k++)
			{
				const studiohdr_t *hdr = m_group[ seq[k].group ].GetStudioHdr();
				char *s2 = hdr->pLocalSeqdesc( seq[k].index )->pszLabel();
				if ( !V_stricmp( s1, s2 ) )
				{
					break;
				}
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualsequence_t tmp;
			tmp.group = group;
			tmp.index = j;
			tmp.flags = std::to_underlying( seqdesc->flags );
			tmp.activity = seqdesc->activity;
			k = seq.AddToTail( tmp );
		}
		else if ( HasFlag( m_group[ seq[k].group ].GetStudioHdr()->pLocalSeqdesc( seq[k].index )->flags, StudioSequenceFlags::Override ) )
		{
			// the one in memory is a forward declared sequence, override it
			virtualsequence_t tmp;
			tmp.group = group;
			tmp.index = j;
			tmp.flags = std::to_underlying( seqdesc->flags );
			tmp.activity = seqdesc->activity;
			seq[k] = tmp;
		}
		m_group[ group ].masterSeq[ j ] = k;
	}

	if ( HasLookupTable() )
	{
		for ( j = numCheck; j < seq.size(); j++ )
		{
			const studiohdr_t *hdr = m_group[ seq[j].group ].GetStudioHdr();
			const char *s1 = hdr->pLocalSeqdesc( seq[j].index )->pszLabel();
			GetSeqTable()->insert( { s1, (short)j } );
		}
	}

	m_seq = seq;
}


void virtualmodel_t::UpdateAutoplaySequences( const studiohdr_t *pStudioHdr )
{
	int autoplayCount = pStudioHdr->CountAutoplaySequences();
	m_autoplaySequences.SetCount( autoplayCount );
	pStudioHdr->CopyAutoplaySequences( m_autoplaySequences.data(), autoplayCount );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void virtualmodel_t::AppendAnimations( int group, const studiohdr_t *pStudioHdr )
{
	int numCheck = m_anim.size();

	CUtlVector< virtualgeneric_t > anim;
	anim = m_anim;

	int j, k;

	m_group[ group ].masterAnim.SetCount( pStudioHdr->numlocalanim );

	for (j = 0; j < pStudioHdr->numlocalanim; j++)
	{
		char *s1 = pStudioHdr->pLocalAnimdesc( j )->pszName();
		if ( HasLookupTable() )
		{
			k = numCheck;
			auto it = GetAnimTable()->find( s1 );
			if ( it != GetAnimTable()->end() )
			{
				k = it->second;
			}
		}
		else
		{
			for (k = 0; k < numCheck; k++)
			{
				char *s2 = m_group[ anim[k].group ].GetStudioHdr()->pLocalAnimdesc( anim[k].index )->pszName();
				if (V_stricmp( s1, s2 ) == 0)
				{
					break;
				}
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = anim.AddToTail( tmp );
		}

		m_group[ group ].masterAnim[ j ] = k;
	}
	
	if ( HasLookupTable() )
	{
		for ( j = numCheck; j < anim.size(); j++ )
		{
			const char *s1 = m_group[ anim[j].group ].GetStudioHdr()->pLocalAnimdesc( anim[j].index )->pszName();
			GetAnimTable()->insert( { s1, (short)j } );
		}
	}

	m_anim = anim;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void virtualmodel_t::AppendBonemap( int group, const studiohdr_t *pStudioHdr )
{
	const studiohdr_t *pBaseStudioHdr = m_group[ 0 ].GetStudioHdr( );

	m_group[ group ].boneMap.SetCount( pBaseStudioHdr->numbones );
	m_group[ group ].masterBone.SetCount( pStudioHdr->numbones );

	int j, k;

	if (group == 0)
	{
		for (j = 0; j < pStudioHdr->numbones; j++)
		{
			m_group[ group ].boneMap[ j ] = j;
			m_group[ group ].masterBone[ j ] = j;
		}
	}
	else
	{
		for (j = 0; j < pBaseStudioHdr->numbones; j++)
		{
			m_group[ group ].boneMap[ j ] = -1;
		}
		for (j = 0; j < pStudioHdr->numbones; j++)
		{
			// NOTE: studiohdr has a bone table - using the table is ~5% faster than this for alyx.mdl on a P4/3.2GHz
			for (k = 0; k < pBaseStudioHdr->numbones; k++)
			{
				if (V_stricmp( pStudioHdr->pBone( j )->pszName(), pBaseStudioHdr->pBone( k )->pszName() ) == 0)
				{
					break;
				}
			}
			if (k < pBaseStudioHdr->numbones)
			{
				m_group[ group ].masterBone[ j ] = k;
				m_group[ group ].boneMap[ k ] = j;

				// FIXME: these runtime messages don't display in hlmv
				if ((pStudioHdr->pBone( j )->parent == -1) || (pBaseStudioHdr->pBone( k )->parent == -1))
				{
					if ((pStudioHdr->pBone( j )->parent != -1) || (pBaseStudioHdr->pBone( k )->parent != -1))
					{
						Warning( "%s/%s : missmatched parent bones on \"%s\"\n", pBaseStudioHdr->pszName(), pStudioHdr->pszName(), pStudioHdr->pBone( j )->pszName() );
					}
				}
				else if (m_group[ group ].masterBone[ pStudioHdr->pBone( j )->parent ] != m_group[ 0 ].masterBone[ pBaseStudioHdr->pBone( k )->parent ])
				{
					Warning( "%s/%s : missmatched parent bones on \"%s\"\n", pBaseStudioHdr->pszName(), pStudioHdr->pszName(), pStudioHdr->pBone( j )->pszName() );
				}

				// merge the bone use flags and pass up the chain
				BoneFlags flags = (pStudioHdr->pBone( j )->flags | pBaseStudioHdr->pBone( k )->flags) & BoneFlags::UsedMask;
				int n = k;
				while (n != -1 && flags != pBaseStudioHdr->pBone( n )->flags)
				{
					((mstudiobone_t *)pBaseStudioHdr->pBone( n ))->flags |= flags;
					n = pBaseStudioHdr->pBone( n )->parent;
				}
			}
			else
			{
				m_group[ group ].masterBone[ j ] = -1;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void virtualmodel_t::AppendAttachments( int group, const studiohdr_t *pStudioHdr )
{
	int numCheck = m_attachment.size();

	CUtlVector< virtualgeneric_t > attachment;
	attachment = m_attachment;

	int j, k, n;

	m_group[ group ].masterAttachment.SetCount( pStudioHdr->numlocalattachments );

	for (j = 0; j < pStudioHdr->numlocalattachments; j++)
	{

		n = m_group[ group ].masterBone[ pStudioHdr->pLocalAttachment( j )->localbone ];
		
		// skip if the attachments bone doesn't exist in the root model
		if (n == -1)
		{
			continue;
		}
		
		
		char *s1 = pStudioHdr->pLocalAttachment( j )->pszName();
		for (k = 0; k < numCheck; k++)
		{
			char *s2 = m_group[ attachment[k].group ].GetStudioHdr()->pLocalAttachment( attachment[k].index )->pszName();

			if (V_stricmp( s1, s2 ) == 0)
			{
				break;
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = attachment.AddToTail( tmp );
		}

		m_group[ group ].masterAttachment[ j ] = k;
	}

	m_attachment = attachment;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void virtualmodel_t::AppendPoseParameters( int group, const studiohdr_t *pStudioHdr )
{
	int numCheck = m_pose.size();

	CUtlVector< virtualgeneric_t > pose;
	pose = m_pose;

	int j, k;

	m_group[ group ].masterPose.SetCount( pStudioHdr->numlocalposeparameters );

	for (j = 0; j < pStudioHdr->numlocalposeparameters; j++)
	{
		char *s1 = pStudioHdr->pLocalPoseParameter( j )->pszName();
		for (k = 0; k < numCheck; k++)
		{
			char *s2 = m_group[ pose[k].group ].GetStudioHdr()->pLocalPoseParameter( pose[k].index )->pszName();

			if (V_stricmp( s1, s2 ) == 0)
			{
				break;
			}
		}
		if (k == numCheck)
		{
			// no duplication
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = pose.AddToTail( tmp );
		}
		else
		{
			// duplicate, reset start and end to fit full dynamic range
			mstudioposeparamdesc_t *pPose1 = pStudioHdr->pLocalPoseParameter( j );
			mstudioposeparamdesc_t *pPose2 = m_group[ pose[k].group ].GetStudioHdr()->pLocalPoseParameter( pose[k].index );
			float start =  MIN( pPose2->end, MIN( pPose1->end, MIN( pPose2->start, pPose1->start ) ) );
			float end =  MAX( pPose2->end, MAX( pPose1->end, MAX( pPose2->start, pPose1->start ) ) );
			pPose2->start = start;
			pPose2->end = end;
		}

		m_group[ group ].masterPose[ j ] = k;
	}

	m_pose = pose;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

void virtualmodel_t::AppendNodes( int group, const studiohdr_t *pStudioHdr )
{
	int numCheck = m_node.size();

	CUtlVector< virtualgeneric_t > node;
	node = m_node;

	int j, k;

	m_group[ group ].masterNode.SetCount( pStudioHdr->numlocalnodes );

	for (j = 0; j < pStudioHdr->numlocalnodes; j++)
	{
		char *s1 = pStudioHdr->pszLocalNodeName( j );
		for (k = 0; k < numCheck; k++)
		{
			char *s2 = m_group[ node[k].group ].GetStudioHdr()->pszLocalNodeName( node[k].index );

			if (V_stricmp( s1, s2 ) == 0)
			{
				break;
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = node.AddToTail( tmp );
		}

		m_group[ group ].masterNode[ j ] = k;
	}

	m_node = node;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------


void virtualmodel_t::AppendIKLocks( int group, const studiohdr_t *pStudioHdr )
{
	int numCheck = m_iklock.size();

	CUtlVector< virtualgeneric_t > iklock;
	iklock = m_iklock;

	int j, k;

	for (j = 0; j < pStudioHdr->numlocalikautoplaylocks; j++)
	{
		int chain1 = pStudioHdr->pLocalIKAutoplayLock( j )->chain;
		for (k = 0; k < numCheck; k++)
		{
			int chain2 = m_group[ iklock[k].group ].GetStudioHdr()->pLocalIKAutoplayLock( iklock[k].index )->chain;

			if (chain1 == chain2)
			{
				break;
			}
		}
		// no duplication
		if (k == numCheck)
		{
			virtualgeneric_t tmp;
			tmp.group = group;
			tmp.index = j;
			k = iklock.AddToTail( tmp );
		}
	}

	m_iklock = iklock;
}

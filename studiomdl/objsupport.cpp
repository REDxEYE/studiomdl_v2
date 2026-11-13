//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//


//
// studiomdl.c: generates a studio .mdl file from a .qc script
// models/<scriptname>.mdl.
//


#pragma warning( disable : 4244 )
#pragma warning( disable : 4237 )
#pragma warning( disable : 4305 )


#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <cmath>
#include "tier1/utlbuffer.h"
#include "common/cmdlib.h"
#include "common/scriplib.h"
#include "mathlib/mathlib.h"
#include "studio.h"
#include "tier1/characterset.h"
#include "tier1_v2/path_utils.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/string_utils.hpp"

bool IsEnd( char const* pLine );

int faceCompare( const void *elem1, const void *elem2 );


void UnifyIndices( s_source_t *psource );

struct MtlInfo_t
{
	CUtlString m_MtlName;
	CUtlString m_TgaName;
};

static CUtlVector<MtlInfo_t> g_MtlLib;

void ParseMtlLib( CUtlBuffer &buf )
{
	int nCurrentMtl = -1;
	while ( buf.IsValid() )
	{
		buf.GetLine( g_studiomdl.input.szLine, sizeof(g_studiomdl.input.szLine) );

		if ( vstd::istarts_with(g_studiomdl.input.szLine, "newmtl ") )
		{
			char mtlName[1024];
			if ( sscanf( g_studiomdl.input.szLine, "newmtl %s", mtlName ) == 1 )
			{
				nCurrentMtl = g_MtlLib.AddToTail( );
				g_MtlLib[nCurrentMtl].m_MtlName = mtlName;
				g_MtlLib[nCurrentMtl].m_TgaName = "debugempty";
			}
			continue;
		}

		if ( vstd::istarts_with(g_studiomdl.input.szLine, "map_Kd ") )
		{
			if ( nCurrentMtl < 0 )
				continue;

			char tgaPath[MAX_PATH]{0};
			char tgaName[1024];
			if ( sscanf( g_studiomdl.input.szLine, "map_Kd %s", tgaPath ) == 1 )
			{
				Q_FileBase( tgaPath, tgaName, sizeof(tgaName) );
				g_MtlLib[nCurrentMtl].m_TgaName = tgaName;
			}
			continue;
		}
	}
}

const char *FindMtlEntry( const char *pTgaName )
{
	int nCount = g_MtlLib.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( !Q_stricmp( g_MtlLib[i].m_MtlName, pTgaName ) )
			return g_MtlLib[i].m_TgaName;
	}
	return pTgaName;
}									 

static bool ParseVertex( CUtlBuffer& bufParse, characterset_t &breakSet, int &v, int &t, int &n )
{
	char	cmd[1024];
	int nLen = bufParse.ParseToken( &breakSet, cmd, sizeof(cmd), false );
	if ( nLen <= 0 )
		return false;

	v = atoi( cmd );
	n = 0;
	t = 0;

	char c = *(char*)bufParse.PeekGet();
	bool bHasTexCoord = IN_CHARACTERSET( breakSet, c ) != 0;
	bool bHasNormal = false;
	if ( bHasTexCoord )
	{
		// Snag the '/'
		nLen = bufParse.ParseToken( &breakSet, cmd, sizeof(cmd), false );
		Assert( nLen == 1 );

		c = *(char*)bufParse.PeekGet();
		if ( !IN_CHARACTERSET( breakSet, c ) )
		{
			nLen = bufParse.ParseToken( &breakSet, cmd, sizeof(cmd), false );
			Assert( nLen > 0 );
			t = atoi( cmd );

			c = *(char*)bufParse.PeekGet();
			bHasNormal = IN_CHARACTERSET( breakSet, c ) != 0;
		}
		else
		{
			bHasNormal = true;
			bHasTexCoord = false;
		}

		if ( bHasNormal )
		{
			// Snag the '/'
			nLen = bufParse.ParseToken( &breakSet, cmd, sizeof(cmd), false );
			Assert( nLen == 1 );

			nLen = bufParse.ParseToken( &breakSet, cmd, sizeof(cmd), false );
			Assert( nLen > 0 );
			n = atoi( cmd );
		}
	}
	return true;
}


int Load_OBJ( s_source_t *psource )
{
	char	cmd[1024];
	int		i;
	int		material = -1;

	g_MtlLib.RemoveAll();

	if ( !OpenGlobalFile( psource->filename ) )
		return 0;

	std::string fullPath;
	if ( !GetGlobalFilePath( psource->filename, fullPath ) )
		return 0;

	const std::string_view fullDir = vstd::paths::directory( fullPath );

	if( !g_studiomdl.options.quiet )
	{
		std::print( "grabbing {}\n", psource->filename );
	}

    g_studiomdl.input.iLinecount = 0;

	psource->numbones = 1;
	strcpy( psource->localBone[0].name, "default" );
	psource->localBone[0].parent = -1;
	Assert( psource->m_Animations.Count() == 0 );
    s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim( psource, "BindPose" );
	pSourceAnim->numframes = 1;
	pSourceAnim->startframe = 0;
	pSourceAnim->endframe = 0;
	pSourceAnim->rawanim[0] = (s_bone_t *)calloc( 1, sizeof( s_bone_t ) );
	pSourceAnim->rawanim[0][0].pos.Init();
	pSourceAnim->rawanim[0][0].rot.Init();
	Build_Reference( psource, "BindPose" );

	characterset_t breakSet;
	CharacterSetBuild( &breakSet, "/\\" );

	while ( GetLineInput() ) 
	{
		Vector tmp;

		if ( strncmp( g_studiomdl.input.szLine, "v ", 2 ) == 0 )
		{
			Vector &vertex = g_studiomdl.input.vertex.emplace_back();
			sscanf( g_studiomdl.input.szLine, "v %f %f %f", &vertex.x, &vertex.y, &vertex.z );
			s_boneweight_t &boneWeight = g_studiomdl.input.bone.emplace_back();
			boneWeight.numbones = 1;
			boneWeight.bone[0] = 0;
			boneWeight.weight[0] = 1.0;
			continue;
		}
		
		if (strncmp( g_studiomdl.input.szLine, "vn ", 3 ) == 0)
		{
			Vector &normal = g_studiomdl.input.normal.emplace_back();
			sscanf( g_studiomdl.input.szLine, "vn %f %f %f", &normal.x, &normal.y, &normal.z );
			continue;
		}
		
		if (strncmp( g_studiomdl.input.szLine, "vt ", 3 ) == 0)
		{
			Vector2D &texcoord = g_studiomdl.input.texcoord[0].emplace_back();
			sscanf( g_studiomdl.input.szLine, "vt %f %f", &texcoord.x, &texcoord.y );
			texcoord.y = 1.0 - texcoord.y;
			continue;
		}
		
		if ( !Q_strncmp( g_studiomdl.input.szLine, "mtllib ", 7 ) )
		{
			sscanf( g_studiomdl.input.szLine, "mtllib %s", &cmd );
			CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );

			const std::string fullMtlLibPath = vstd::paths::join( fullDir, cmd );
			if ( g_pFullFileSystem->ReadFile( fullMtlLibPath.c_str(), nullptr, buf ) )
			{
				ParseMtlLib( buf );
			}
			continue;
		}

		if (strncmp( g_studiomdl.input.szLine, "usemtl ", 7 ) == 0)
		{
			sscanf( g_studiomdl.input.szLine, "usemtl %s", &cmd );

			const char *pTexture = FindMtlEntry( cmd );
			int texture = LookupTexture( pTexture );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( texture );
			continue;
		}

		if (strncmp( g_studiomdl.input.szLine, "f ", 2 ) == 0)
		{
			if ( material < 0 )
			{
				int texture = LookupTexture( "debugempty.tga" );
				psource->texmap[texture] = texture;
				material = UseTextureAsMaterial( texture );
			}

			int v0, n0, t0;
			int v1, n1, t1;
			int v2, n2, t2;

			s_tmpface_t f;

			// Are we specifying p only, p and t only, p and n only, or p and n and t?
			char *pData = g_studiomdl.input.szLine + 2;
			int nLen = Q_strlen( pData );

			CUtlBuffer bufParse( pData, nLen, CUtlBuffer::TEXT_BUFFER | CUtlBuffer::READ_ONLY );

			ParseVertex( bufParse, breakSet, v0, t0, n0 );
			ParseVertex( bufParse, breakSet, v1, t1, n1 );
			Assert( v0 <= static_cast<int>(g_studiomdl.input.vertex.size()) && t0 <= static_cast<int>(g_studiomdl.input.texcoord[0].size()) && n0 <= static_cast<int>(g_studiomdl.input.normal.size()) );
			Assert( v1 <= static_cast<int>(g_studiomdl.input.vertex.size()) && t1 <= static_cast<int>(g_studiomdl.input.texcoord[0].size()) && n1 <= static_cast<int>(g_studiomdl.input.normal.size()) );
			while ( bufParse.IsValid() )
			{
				if ( !ParseVertex( bufParse, breakSet, v2, t2, n2 ) )
					break;

				Assert( v2 <= static_cast<int>(g_studiomdl.input.vertex.size()) && t2 <= static_cast<int>(g_studiomdl.input.texcoord[0].size()) && n2 <= static_cast<int>(g_studiomdl.input.normal.size()) );
	
				auto& new_face = g_studiomdl.input.faces.emplace_back();
				new_face.material = material;
				new_face.a = v0 - 1; new_face.na = (n0 > 0) ? n0 - 1 : 0, new_face.ta[0] = (t0 > 0) ? t0 - 1 : 0;
				new_face.b = v2 - 1; new_face.nb = (n2 > 0) ? n2 - 1 : 0, new_face.tb[0] = (t2 > 0) ? t2 - 1 : 0;
				new_face.c = v1 - 1; new_face.nc = (n1 > 0) ? n1 - 1 : 0, new_face.tc[0] = (t1 > 0) ? t1 - 1 : 0;

				v1 = v2; t1 = t2; n1 = n2;
			}
			continue;
		}
	}

	UnifyIndices( psource );

	BuildIndividualMeshes( psource );

	g_studiomdl.input.inputFile.reset();

	return 1;
}



int AppendVTAtoOBJ( s_source_t *psource, char *filename, int frame )
{
	char	cmd[1024];
	int		i, j;
	int		material = 0;

	Vector tmp;
	matrix3x4_t m;

	AngleMatrix( RadianEuler( 1.570796, 0, 0 ), m );

	if ( !OpenGlobalFile( filename ) )
		return 0;

	if( !g_studiomdl.options.quiet )
	{
		std::print("grabbing {}\n", filename );
	}

    g_studiomdl.input.iLinecount = 0;

	g_studiomdl.input.vertex.clear();
	g_studiomdl.input.bone.clear();
	g_studiomdl.input.normal.clear();
	g_studiomdl.input.texcoord[0].clear();
	g_studiomdl.input.faces.clear();

	while ( GetLineInput() ) 
	{
		Vector tmp;

		if (strncmp( g_studiomdl.input.szLine, "v ", 2 ) == 0)
		{
			sscanf( g_studiomdl.input.szLine, "v %f %f %f", &tmp.x, &tmp.y, &tmp.z );
			VectorTransform( tmp, m, g_studiomdl.input.vertex.emplace_back() );

			s_boneweight_t &boneWeight = g_studiomdl.input.bone.emplace_back();
			boneWeight.numbones = 1;
			boneWeight.bone[0] = 0;
			boneWeight.weight[0] = 1.0;
		}
		else if (strncmp( g_studiomdl.input.szLine, "vn ", 3 ) == 0)
		{
			sscanf( g_studiomdl.input.szLine, "vn %f %f %f", &tmp.x, &tmp.y, &tmp.z );
			VectorRotate( tmp, m, g_studiomdl.input.normal.emplace_back() );
		}
		else if (strncmp( g_studiomdl.input.szLine, "vt ", 3 ) == 0)
		{
			Vector2D &texcoord = g_studiomdl.input.texcoord[0].emplace_back();
			sscanf( g_studiomdl.input.szLine, "vt %f %f", &texcoord.x, &texcoord.y );
		}
		else if (strncmp( g_studiomdl.input.szLine, "usemtl ", 7 ) == 0)
		{
			sscanf( g_studiomdl.input.szLine, "usemtl %s", &cmd );

			int texture = LookupTexture( cmd );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( texture );
		}
		else if (strncmp( g_studiomdl.input.szLine, "f ", 2 ) == 0)
		{
			int v0, n0, t0;
			int v1, n1, t1;
			int v2, n2, t2;
			int v3, n3, t3;


			j = sscanf( g_studiomdl.input.szLine, "f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d", &v0, &t0, &n0, &v1, &t1, &n1, &v2, &t2, &n2, &v3, &t3, &n3 );
			auto& new_face = g_studiomdl.input.faces.emplace_back();
			new_face.material = material;
			new_face.a = v0 - 1; new_face.na = n0 - 1, new_face.ta[0] = 0;
			new_face.b = v2 - 1; new_face.nb = n2 - 1, new_face.tb[0] = 0;
			new_face.c = v1 - 1; new_face.nc = n1 - 1, new_face.tc[0] = 0;

			Assert( v0 <= static_cast<int>(g_studiomdl.input.vertex.size()) && v1 <= static_cast<int>(g_studiomdl.input.vertex.size()) && v2 <= static_cast<int>(g_studiomdl.input.vertex.size()) );
			Assert( n0 <= static_cast<int>(g_studiomdl.input.normal.size()) && n1 <= static_cast<int>(g_studiomdl.input.normal.size()) && n2 <= static_cast<int>(g_studiomdl.input.normal.size()) );

			if (j == 12)
			{
				auto& extra_face = g_studiomdl.input.faces.emplace_back();
				extra_face.a = v0 - 1; extra_face.na = n0 - 1, extra_face.ta[0] = 0;
				extra_face.b = v3 - 1; extra_face.nb = n3 - 1, extra_face.tb[0] = 0;
				extra_face.c = v2 - 1; extra_face.nc = n2 - 1, extra_face.tc[0] = 0;
			}
		}
	}


	UnifyIndices( psource );

	s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim( psource, "BindPose" );
	if ( frame == 0 )
	{
		psource->numbones = 1;
		strcpy( psource->localBone[0].name, "default" );
		psource->localBone[0].parent = -1;
		pSourceAnim->numframes = 1;
		pSourceAnim->startframe = 0;
		pSourceAnim->endframe = 0;
		pSourceAnim->rawanim[0] = (s_bone_t *)calloc( 1, sizeof( s_bone_t ) );
		pSourceAnim->rawanim[0][0].pos.Init();
		pSourceAnim->rawanim[0][0].rot = RadianEuler( 1.570796, 0.0, 0.0 );
		Build_Reference( psource, "BindPose" );

		BuildIndividualMeshes( psource );
	}

	int t = frame;
	int count = static_cast<int>( g_studiomdl.input.v_listdata.size() );

	pSourceAnim->numvanims[t] = count;
	pSourceAnim->vanim[t] = (s_vertanim_t *)calloc( count, sizeof( s_vertanim_t ) );
	for (i = 0; i < count; i++)
	{
		pSourceAnim->vanim[t][i].vertex = i;
		pSourceAnim->vanim[t][i].pos = g_studiomdl.input.vertex[g_studiomdl.input.v_listdata[i].v];
		pSourceAnim->vanim[t][i].normal = g_studiomdl.input.normal[g_studiomdl.input.v_listdata[i].n];
	}

	g_studiomdl.input.inputFile.reset();

	return 1;
}

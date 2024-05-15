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
#include "studiomdl/studiomdl.h"
extern StudioMdlContext g_StudioMdlContext;

bool IsEnd( char const* pLine );
int SortAndBalanceBones( int iCount, int iMaxCount, int bones[], float weights[] );

int AddToVlist( int v, int m, int n, int t, int firstref );
void DecrementReferenceVlist( int uv, int numverts );
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
		buf.GetLine( g_StudioMdlContext.szLine, sizeof(g_StudioMdlContext.szLine) );

		if ( !Q_strnicmp( g_StudioMdlContext.szLine, "newmtl ", 7 ) )
		{
			char mtlName[1024];
			if ( sscanf( g_StudioMdlContext.szLine, "newmtl %s", mtlName ) == 1 )
			{
				nCurrentMtl = g_MtlLib.AddToTail( );
				g_MtlLib[nCurrentMtl].m_MtlName = mtlName;
				g_MtlLib[nCurrentMtl].m_TgaName = "debugempty";
			}
			continue;
		}

		if ( !Q_strnicmp( g_StudioMdlContext.szLine, "map_Kd ", 7 ) )
		{
			if ( nCurrentMtl < 0 )
				continue;

			char tgaPath[MAX_PATH];
			char tgaName[1024];
			if ( sscanf( g_StudioMdlContext.szLine, "map_Kd %s", tgaPath ) == 1 )
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

	char pFullPath[MAX_PATH];
	if ( !GetGlobalFilePath( psource->filename, pFullPath, sizeof(pFullPath) ) )
		return 0;

	char pFullDir[MAX_PATH];
	Q_ExtractFilePath( pFullPath, pFullDir, sizeof(pFullDir) );

	if( !g_StudioMdlContext.quiet )
	{
		printf( "grabbing %s\n", psource->filename );
	}

    g_StudioMdlContext.iLinecount = 0;

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

		if ( strncmp( g_StudioMdlContext.szLine, "v ", 2 ) == 0 )
		{
			i = g_StudioMdlContext.numverts++;

			sscanf( g_StudioMdlContext.szLine, "v %f %f %f", &g_StudioMdlContext.vertex[i].x, &g_StudioMdlContext.vertex[i].y, &g_StudioMdlContext.vertex[i].z );
			g_StudioMdlContext.bone[i].numbones = 1;
			g_StudioMdlContext.bone[i].bone[0] = 0;
			g_StudioMdlContext.bone[i].weight[0] = 1.0;
			continue;
		}
		
		if (strncmp( g_StudioMdlContext.szLine, "vn ", 3 ) == 0)
		{
			i = g_StudioMdlContext.numnormals++;
			sscanf( g_StudioMdlContext.szLine, "vn %f %f %f", &g_StudioMdlContext.normal[i].x, &g_StudioMdlContext.normal[i].y, &g_StudioMdlContext.normal[i].z );
			continue;
		}
		
		if (strncmp( g_StudioMdlContext.szLine, "vt ", 3 ) == 0)
		{
			i = g_StudioMdlContext.numtexcoords[0]++;
			sscanf( g_StudioMdlContext.szLine, "vt %f %f", &g_StudioMdlContext.texcoord[0][i].x, &g_StudioMdlContext.texcoord[0][i].y );
			g_StudioMdlContext.texcoord[0][i].y = 1.0 - g_StudioMdlContext.texcoord[0][i].y;
			continue;
		}
		
		if ( !Q_strncmp( g_StudioMdlContext.szLine, "mtllib ", 7 ) )
		{
			sscanf( g_StudioMdlContext.szLine, "mtllib %s", &cmd );
			CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );

			char pFullMtlLibPath[MAX_PATH];
			Q_ComposeFileName( pFullDir, cmd, pFullMtlLibPath, sizeof(pFullMtlLibPath) );
			if ( g_pFullFileSystem->ReadFile( pFullMtlLibPath, NULL, buf ) )
			{
				ParseMtlLib( buf );
			}
			continue;
		}

		if (strncmp( g_StudioMdlContext.szLine, "usemtl ", 7 ) == 0)
		{
			sscanf( g_StudioMdlContext.szLine, "usemtl %s", &cmd );

			const char *pTexture = FindMtlEntry( cmd );
			int texture = LookupTexture( pTexture );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( texture );
			continue;
		}

		if (strncmp( g_StudioMdlContext.szLine, "f ", 2 ) == 0)
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
			char *pData = g_StudioMdlContext.szLine + 2;
			int nLen = Q_strlen( pData );

			CUtlBuffer bufParse( pData, nLen, CUtlBuffer::TEXT_BUFFER | CUtlBuffer::READ_ONLY );

			ParseVertex( bufParse, breakSet, v0, t0, n0 );
			ParseVertex( bufParse, breakSet, v1, t1, n1 );
			Assert( v0 <= g_StudioMdlContext.numverts && t0 <= g_StudioMdlContext.numtexcoords[0] && n0 <= g_StudioMdlContext.numnormals );
			Assert( v1 <= g_StudioMdlContext.numverts && t1 <= g_StudioMdlContext.numtexcoords[0] && n1 <= g_StudioMdlContext.numnormals );
			while ( bufParse.IsValid() )
			{
				if ( !ParseVertex( bufParse, breakSet, v2, t2, n2 ) )
					break;

				Assert( v2 <= g_StudioMdlContext.numverts && t2 <= g_StudioMdlContext.numtexcoords[0] && n2 <= g_StudioMdlContext.numnormals );
	
				i = g_StudioMdlContext.numfaces++;
				f.material = material;
				f.a = v0 - 1; f.na = (n0 > 0) ? n0 - 1 : 0, f.ta[0] = (t0 > 0) ? t0 - 1 : 0;
				f.b = v2 - 1; f.nb = (n2 > 0) ? n2 - 1 : 0, f.tb[0] = (t2 > 0) ? t2 - 1 : 0;
				f.c = v1 - 1; f.nc = (n1 > 0) ? n1 - 1 : 0, f.tc[0] = (t1 > 0) ? t1 - 1 : 0;
				g_StudioMdlContext.face[i] = f;

				v1 = v2; t1 = t2; n1 = n2;
			}
			continue;
		}
	}

	UnifyIndices( psource );

	BuildIndividualMeshes( psource );

	fclose( g_StudioMdlContext.fpInput );

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

	if( !g_StudioMdlContext.quiet )
	{
		printf ("grabbing %s\n", filename );
	}

    g_StudioMdlContext.iLinecount = 0;

	g_StudioMdlContext.numverts = g_StudioMdlContext.numnormals = g_StudioMdlContext.numtexcoords[0] = g_StudioMdlContext.numfaces = 0;

	while ( GetLineInput() ) 
	{
		Vector tmp;

		if (strncmp( g_StudioMdlContext.szLine, "v ", 2 ) == 0)
		{
			i = g_StudioMdlContext.numverts++;

			sscanf( g_StudioMdlContext.szLine, "v %f %f %f", &tmp.x, &tmp.y, &tmp.z );
			VectorTransform( tmp, m, g_StudioMdlContext.vertex[i] );

			// printf("%f %f %f\n", g_StudioMdlContext.vertex[i].x, g_StudioMdlContext.vertex[i].y, g_StudioMdlContext.vertex[i].z );

			g_StudioMdlContext.bone[i].numbones = 1;
			g_StudioMdlContext.bone[i].bone[0] = 0;
			g_StudioMdlContext.bone[i].weight[0] = 1.0;
		}
		else if (strncmp( g_StudioMdlContext.szLine, "vn ", 3 ) == 0)
		{
			i = g_StudioMdlContext.numnormals++;
			sscanf( g_StudioMdlContext.szLine, "vn %f %f %f", &tmp.x, &tmp.y, &tmp.z );
			VectorRotate( tmp, m, g_StudioMdlContext.normal[i] );
		}
		else if (strncmp( g_StudioMdlContext.szLine, "vt ", 3 ) == 0)
		{
			i = g_StudioMdlContext.numtexcoords[0]++;
			sscanf( g_StudioMdlContext.szLine, "vt %f %f", &g_StudioMdlContext.texcoord[0][i].x, &g_StudioMdlContext.texcoord[0][i].y );
		}
		else if (strncmp( g_StudioMdlContext.szLine, "usemtl ", 7 ) == 0)
		{
			sscanf( g_StudioMdlContext.szLine, "usemtl %s", &cmd );

			int texture = LookupTexture( cmd );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( texture );
		}
		else if (strncmp( g_StudioMdlContext.szLine, "f ", 2 ) == 0)
		{
			int v0, n0, t0;
			int v1, n1, t1;
			int v2, n2, t2;
			int v3, n3, t3;

			s_tmpface_t f;

			i = g_StudioMdlContext.numfaces++;

			j = sscanf( g_StudioMdlContext.szLine, "f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d", &v0, &t0, &n0, &v1, &t1, &n1, &v2, &t2, &n2, &v3, &t3, &n3 );

			f.material = material;
			f.a = v0 - 1; f.na = n0 - 1, f.ta[0] = 0;
			f.b = v2 - 1; f.nb = n2 - 1, f.tb[0] = 0;
			f.c = v1 - 1; f.nc = n1 - 1, f.tc[0] = 0;

			Assert( v0 <= g_StudioMdlContext.numverts && v1 <= g_StudioMdlContext.numverts && v2 <= g_StudioMdlContext.numverts );
			Assert( n0 <= g_StudioMdlContext.numnormals && n1 <= g_StudioMdlContext.numnormals && n2 <= g_StudioMdlContext.numnormals );

			g_StudioMdlContext.face[i] = f;

			if (j == 12)
			{
				i = g_StudioMdlContext.numfaces++;
				f.a = v0 - 1; f.na = n0 - 1, f.ta[0] = 0;
				f.b = v3 - 1; f.nb = n3 - 1, f.tb[0] = 0;
				f.c = v2 - 1; f.nc = n2 - 1, f.tc[0] = 0;
				g_StudioMdlContext.face[i] = f;
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

	// printf("%d %d : %d\n", g_StudioMdlContext.numverts, g_StudioMdlContext.numnormals, numvlist );

	int t = frame;
	int count = g_numvlist;

	pSourceAnim->numvanims[t] = count;
	pSourceAnim->vanim[t] = (s_vertanim_t *)calloc( count, sizeof( s_vertanim_t ) );
	for (i = 0; i < count; i++)
	{
		pSourceAnim->vanim[t][i].vertex = i;
		pSourceAnim->vanim[t][i].pos = g_StudioMdlContext.vertex[v_listdata[i].v];
		pSourceAnim->vanim[t][i].normal = g_StudioMdlContext.normal[v_listdata[i].n];
	}

	fclose( g_StudioMdlContext.fpInput );

	return 1;
}

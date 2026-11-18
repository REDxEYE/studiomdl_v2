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
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
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

void UnifyIndices( StudioMdlState &ctx, s_source_t *psource );

struct MtlInfo_t
{
	std::string m_MtlName;
	std::string m_TgaName;
};

//-----------------------------------------------------------------------------
// Read cursor over text, with the semantics of the CUtlBuffer text calls it replaces (GetLine, ParseToken without
// comment parsing): reading past the end puts the cursor in the error state (IsValid() == false)
//-----------------------------------------------------------------------------
class CTextCursor
{
public:
	explicit CTextCursor( std::string_view text ) : m_Text( text ) {}

	bool IsValid() const { return !m_bError; }

	// The next character, 0 at the end
	char Peek() const { return m_nPos < m_Text.size() ? m_Text[m_nPos] : 0; }

	// Copies the next line, including its '\n' or '\r', into pLine (truncated to nMaxChars - 1 characters)
	void GetLine( char *pLine, size_t nMaxChars )
	{
		*pLine = 0;
		if ( m_bError )
			return;

		size_t nLen = 0;	// line length + 1 (the terminating zero), CUtlBuffer::PeekLineLength
		size_t i = m_nPos;
		for ( ; i < m_Text.size(); ++i )
		{
			if ( m_Text[i] == '\n' || m_Text[i] == '\r' )
			{
				nLen = i - m_nPos + 2;
				break;
			}
			if ( m_Text[i] == 0 )
			{
				nLen = i - m_nPos + 1;
				break;
			}
		}
		if ( i == m_Text.size() )
			nLen = ( i == m_nPos ) ? 0 : i - m_nPos + 1;

		if ( nLen == 0 )
		{
			m_bError = true;
			return;
		}

		const size_t nCopy = ( nLen <= nMaxChars ) ? nLen - 1 : nMaxChars - 1;
		m_Text.copy( pLine, nCopy, m_nPos );
		pLine[nCopy] = 0;
		m_nPos += nCopy;
		if ( nLen > nMaxChars )
			m_nPos += nLen - 1 - nMaxChars;	// (sic) CUtlBuffer leaves one character of the overlong line behind
	}

	// Skips whitespace, then reads a quoted string, a single break character or a word; returns its length, -1 at the end
	int ParseToken( const characterset_t &breaks, char *pTokenBuf, int nMaxLen )
	{
		pTokenBuf[0] = 0;
		if ( m_bError )
			return -1;
		while ( m_nPos < m_Text.size() && V_isspace( static_cast<unsigned char>( m_Text[m_nPos] ) ) )
			++m_nPos;

		char c = GetChar();
		if ( c == 0 )
			return -1;

		if ( c == '\"' )
		{
			int nLen = 0;
			while ( IsValid() )
			{
				c = GetChar();
				if ( c == '\"' || !c )
				{
					pTokenBuf[nLen] = 0;
					return nLen;
				}
				pTokenBuf[nLen] = c;
				if ( ++nLen == nMaxLen )
				{
					pTokenBuf[nLen - 1] = 0;
					return nMaxLen;
				}
			}
			pTokenBuf[nLen] = 0;
			return nLen;
		}

		if ( IN_CHARACTERSET( breaks, static_cast<unsigned char>( c ) ) )
		{
			pTokenBuf[0] = c;
			pTokenBuf[1] = 0;
			return 1;
		}

		int nLen = 0;
		while ( true )
		{
			pTokenBuf[nLen] = c;
			if ( ++nLen == nMaxLen )
			{
				pTokenBuf[nLen - 1] = 0;
				return nMaxLen;
			}
			c = GetChar();
			if ( !IsValid() )
				break;

			if ( IN_CHARACTERSET( breaks, static_cast<unsigned char>( c ) ) || c == '\"' || c <= ' ' )
			{
				--m_nPos;
				break;
			}
		}

		pTokenBuf[nLen] = 0;
		return nLen;
	}

private:
	char GetChar()
	{
		if ( m_nPos < m_Text.size() )
			return m_Text[m_nPos++];
		m_bError = true;
		return 0;
	}

	std::string_view m_Text;
	size_t m_nPos = 0;
	bool m_bError = false;
};

static void ParseMtlLib( StudioMdlState &ctx, std::vector<MtlInfo_t> &mtlLib, CTextCursor &buf )
{
	int nCurrentMtl = -1;
	while ( buf.IsValid() )
	{
		buf.GetLine( ctx.input.szLine, sizeof(ctx.input.szLine) );

		if ( vstd::istarts_with(ctx.input.szLine, "newmtl ") )
		{
			char mtlName[1024];
			if ( sscanf( ctx.input.szLine, "newmtl %s", mtlName ) == 1 )
			{
				nCurrentMtl = std::ssize( mtlLib );
				MtlInfo_t &mtl = mtlLib.emplace_back();
				mtl.m_MtlName = mtlName;
				mtl.m_TgaName = "debugempty";
			}
			continue;
		}

		if ( vstd::istarts_with(ctx.input.szLine, "map_Kd ") )
		{
			if ( nCurrentMtl < 0 )
				continue;

			char tgaPath[MAX_PATH]{0};
			char tgaName[1024];
			if ( sscanf( ctx.input.szLine, "map_Kd %s", tgaPath ) == 1 )
			{
				Q_FileBase( tgaPath, tgaName, sizeof(tgaName) );
				mtlLib[nCurrentMtl].m_TgaName = tgaName;
			}
			continue;
		}
	}
}

static const char *FindMtlEntry( const std::vector<MtlInfo_t> &mtlLib, const char *pTgaName )
{
	for ( const MtlInfo_t &mtl : mtlLib )
	{
		if ( !Q_stricmp( mtl.m_MtlName.c_str(), pTgaName ) )
			return mtl.m_TgaName.c_str();
	}
	return pTgaName;
}

static bool ParseVertex( CTextCursor& bufParse, characterset_t &breakSet, int &v, int &t, int &n )
{
	char	cmd[1024];
	int nLen = bufParse.ParseToken( breakSet, cmd, sizeof(cmd) );
	if ( nLen <= 0 )
		return false;

	v = atoi( cmd );
	n = 0;
	t = 0;

	char c = bufParse.Peek();
	bool bHasTexCoord = IN_CHARACTERSET( breakSet, c ) != 0;
	bool bHasNormal = false;
	if ( bHasTexCoord )
	{
		// Snag the '/'
		nLen = bufParse.ParseToken( breakSet, cmd, sizeof(cmd) );
		Assert( nLen == 1 );

		c = bufParse.Peek();
		if ( !IN_CHARACTERSET( breakSet, c ) )
		{
			nLen = bufParse.ParseToken( breakSet, cmd, sizeof(cmd) );
			Assert( nLen > 0 );
			t = atoi( cmd );

			c = bufParse.Peek();
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
			nLen = bufParse.ParseToken( breakSet, cmd, sizeof(cmd) );
			Assert( nLen == 1 );

			nLen = bufParse.ParseToken( breakSet, cmd, sizeof(cmd) );
			Assert( nLen > 0 );
			n = atoi( cmd );
		}
	}
	return true;
}


int Load_OBJ( StudioMdlState &ctx, s_source_t *psource )
{
	char	cmd[1024];
	int		i;
	int		material = -1;

	std::vector<MtlInfo_t> mtlLib;

	if ( !OpenGlobalFile( ctx, psource->filename ) )
		return 0;

	std::string fullPath;
	if ( !GetGlobalFilePath( ctx, psource->filename, fullPath ) )
		return 0;

	const std::string_view fullDir = vstd::paths::directory( fullPath );

	if( !ctx.options.quiet )
	{
		MdlPrint( "grabbing {}\n", psource->filename );
	}

    ctx.input.iLinecount = 0;

	psource->numbones = 1;
	strcpy( psource->localBone[0].name, "default" );
	psource->localBone[0].parent = -1;
	Assert( psource->m_Animations.Count() == 0 );
    s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim( psource, "BindPose" );
	pSourceAnim->numframes = 1;
	pSourceAnim->startframe = 0;
	pSourceAnim->endframe = 0;
	pSourceAnim->rawanim[0] = ctx.arena.CreateArray<s_bone_t>( 1 );
	pSourceAnim->rawanim[0][0].pos.Init();
	pSourceAnim->rawanim[0][0].rot.Init();
	Build_Reference( psource, "BindPose" );

	characterset_t breakSet;
	CharacterSetBuild( &breakSet, "/\\" );

	while ( GetLineInput( ctx ) ) 
	{
		Vector tmp;

		if ( strncmp( ctx.input.szLine, "v ", 2 ) == 0 )
		{
			Vector &vertex = ctx.input.vertex.emplace_back();
			sscanf( ctx.input.szLine, "v %f %f %f", &vertex.x, &vertex.y, &vertex.z );
			s_boneweight_t &boneWeight = ctx.input.bone.emplace_back();
			boneWeight.numbones = 1;
			boneWeight.bone[0] = 0;
			boneWeight.weight[0] = 1.0;
			ctx.input.sourceBone[ctx.input.bone.size() - 1] = {};
			continue;
		}
		
		if (strncmp( ctx.input.szLine, "vn ", 3 ) == 0)
		{
			Vector &normal = ctx.input.normal.emplace_back();
			sscanf( ctx.input.szLine, "vn %f %f %f", &normal.x, &normal.y, &normal.z );
			continue;
		}
		
		if (strncmp( ctx.input.szLine, "vt ", 3 ) == 0)
		{
			Vector2D &texcoord = ctx.input.texcoord[0].emplace_back();
			sscanf( ctx.input.szLine, "vt %f %f", &texcoord.x, &texcoord.y );
			texcoord.y = 1.0 - texcoord.y;
			continue;
		}
		
		if ( !Q_strncmp( ctx.input.szLine, "mtllib ", 7 ) )
		{
			sscanf( ctx.input.szLine, "mtllib %s", &cmd );

			const std::string fullMtlLibPath = vstd::paths::join( fullDir, cmd );
			// text mode like the filesystem's ReadFile into a text buffer
			if ( FILE *fp = vstd::paths::open_file( fullMtlLibPath, "rt" ) )
			{
				std::string text;
				char chunk[4096];
				for ( size_t nRead; ( nRead = fread( chunk, 1, sizeof( chunk ), fp ) ) > 0; )
					text.append( chunk, nRead );
				fclose( fp );

				CTextCursor buf( text );
				ParseMtlLib( ctx, mtlLib, buf );
			}
			continue;
		}

		if (strncmp( ctx.input.szLine, "usemtl ", 7 ) == 0)
		{
			sscanf( ctx.input.szLine, "usemtl %s", &cmd );

			const char *pTexture = FindMtlEntry( mtlLib, cmd );
			int texture = LookupTexture( ctx, pTexture );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( ctx, texture );
			continue;
		}

		if (strncmp( ctx.input.szLine, "f ", 2 ) == 0)
		{
			if ( material < 0 )
			{
				int texture = LookupTexture( ctx, "debugempty.tga" );
				psource->texmap[texture] = texture;
				material = UseTextureAsMaterial( ctx, texture );
			}

			int v0, n0, t0;
			int v1, n1, t1;
			int v2, n2, t2;

			s_tmpface_t f;

			// Are we specifying p only, p and t only, p and n only, or p and n and t?
			char *pData = ctx.input.szLine + 2;
			int nLen = Q_strlen( pData );

			CTextCursor bufParse( std::string_view( pData, nLen ) );

			ParseVertex( bufParse, breakSet, v0, t0, n0 );
			ParseVertex( bufParse, breakSet, v1, t1, n1 );
			Assert( v0 <= static_cast<int>(ctx.input.vertex.size()) && t0 <= static_cast<int>(ctx.input.texcoord[0].size()) && n0 <= static_cast<int>(ctx.input.normal.size()) );
			Assert( v1 <= static_cast<int>(ctx.input.vertex.size()) && t1 <= static_cast<int>(ctx.input.texcoord[0].size()) && n1 <= static_cast<int>(ctx.input.normal.size()) );
			while ( bufParse.IsValid() )
			{
				if ( !ParseVertex( bufParse, breakSet, v2, t2, n2 ) )
					break;

				Assert( v2 <= static_cast<int>(ctx.input.vertex.size()) && t2 <= static_cast<int>(ctx.input.texcoord[0].size()) && n2 <= static_cast<int>(ctx.input.normal.size()) );
	
				auto& new_face = ctx.input.faces.emplace_back();
				new_face.material = material;
				new_face.a = v0 - 1; new_face.na = (n0 > 0) ? n0 - 1 : 0, new_face.ta[0] = (t0 > 0) ? t0 - 1 : 0;
				new_face.b = v2 - 1; new_face.nb = (n2 > 0) ? n2 - 1 : 0, new_face.tb[0] = (t2 > 0) ? t2 - 1 : 0;
				new_face.c = v1 - 1; new_face.nc = (n1 > 0) ? n1 - 1 : 0, new_face.tc[0] = (t1 > 0) ? t1 - 1 : 0;

				v1 = v2; t1 = t2; n1 = n2;
			}
			continue;
		}
	}

	UnifyIndices( ctx, psource );

	BuildIndividualMeshes( ctx, psource );

	ctx.input.inputFile.reset();

	return 1;
}



int AppendVTAtoOBJ( StudioMdlState &ctx, s_source_t *psource, std::string_view filename, int frame )
{
	char	cmd[1024];
	int		i, j;
	int		material = 0;

	Vector tmp;
	matrix3x4_t m;

	AngleMatrix( RadianEuler( 1.570796, 0, 0 ), m );

	if ( !OpenGlobalFile( ctx, filename ) )
		return 0;

	if( !ctx.options.quiet )
	{
		MdlPrint("grabbing {}\n", filename );
	}

    ctx.input.iLinecount = 0;

	ctx.input.vertex.clear();
	ctx.input.bone.clear();
	ctx.input.sourceBone.clear();
	ctx.input.normal.clear();
	ctx.input.texcoord[0].clear();
	ctx.input.faces.clear();

	while ( GetLineInput( ctx ) ) 
	{
		Vector tmp;

		if (strncmp( ctx.input.szLine, "v ", 2 ) == 0)
		{
			sscanf( ctx.input.szLine, "v %f %f %f", &tmp.x, &tmp.y, &tmp.z );
			VectorTransform( tmp, m, ctx.input.vertex.emplace_back() );

			s_boneweight_t &boneWeight = ctx.input.bone.emplace_back();
			boneWeight.numbones = 1;
			boneWeight.bone[0] = 0;
			boneWeight.weight[0] = 1.0;
			ctx.input.sourceBone[ctx.input.bone.size() - 1] = {};
		}
		else if (strncmp( ctx.input.szLine, "vn ", 3 ) == 0)
		{
			sscanf( ctx.input.szLine, "vn %f %f %f", &tmp.x, &tmp.y, &tmp.z );
			VectorRotate( tmp, m, ctx.input.normal.emplace_back() );
		}
		else if (strncmp( ctx.input.szLine, "vt ", 3 ) == 0)
		{
			Vector2D &texcoord = ctx.input.texcoord[0].emplace_back();
			sscanf( ctx.input.szLine, "vt %f %f", &texcoord.x, &texcoord.y );
		}
		else if (strncmp( ctx.input.szLine, "usemtl ", 7 ) == 0)
		{
			sscanf( ctx.input.szLine, "usemtl %s", &cmd );

			int texture = LookupTexture( ctx, cmd );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( ctx, texture );
		}
		else if (strncmp( ctx.input.szLine, "f ", 2 ) == 0)
		{
			int v0, n0, t0;
			int v1, n1, t1;
			int v2, n2, t2;
			int v3, n3, t3;


			j = sscanf( ctx.input.szLine, "f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d", &v0, &t0, &n0, &v1, &t1, &n1, &v2, &t2, &n2, &v3, &t3, &n3 );
			auto& new_face = ctx.input.faces.emplace_back();
			new_face.material = material;
			new_face.a = v0 - 1; new_face.na = n0 - 1, new_face.ta[0] = 0;
			new_face.b = v2 - 1; new_face.nb = n2 - 1, new_face.tb[0] = 0;
			new_face.c = v1 - 1; new_face.nc = n1 - 1, new_face.tc[0] = 0;

			Assert( v0 <= static_cast<int>(ctx.input.vertex.size()) && v1 <= static_cast<int>(ctx.input.vertex.size()) && v2 <= static_cast<int>(ctx.input.vertex.size()) );
			Assert( n0 <= static_cast<int>(ctx.input.normal.size()) && n1 <= static_cast<int>(ctx.input.normal.size()) && n2 <= static_cast<int>(ctx.input.normal.size()) );

			if (j == 12)
			{
				auto& extra_face = ctx.input.faces.emplace_back();
				extra_face.a = v0 - 1; extra_face.na = n0 - 1, extra_face.ta[0] = 0;
				extra_face.b = v3 - 1; extra_face.nb = n3 - 1, extra_face.tb[0] = 0;
				extra_face.c = v2 - 1; extra_face.nc = n2 - 1, extra_face.tc[0] = 0;
			}
		}
	}


	UnifyIndices( ctx, psource );

	s_sourceanim_t *pSourceAnim = FindOrAddSourceAnim( psource, "BindPose" );
	if ( frame == 0 )
	{
		psource->numbones = 1;
		strcpy( psource->localBone[0].name, "default" );
		psource->localBone[0].parent = -1;
		pSourceAnim->numframes = 1;
		pSourceAnim->startframe = 0;
		pSourceAnim->endframe = 0;
		pSourceAnim->rawanim[0] = ctx.arena.CreateArray<s_bone_t>( 1 );
		pSourceAnim->rawanim[0][0].pos.Init();
		pSourceAnim->rawanim[0][0].rot = RadianEuler( 1.570796, 0.0, 0.0 );
		Build_Reference( psource, "BindPose" );

		BuildIndividualMeshes( ctx, psource );
	}

	int t = frame;
	int count = static_cast<int>( ctx.input.v_listdata.size() );

	pSourceAnim->numvanims[t] = count;
	pSourceAnim->vanim[t] = ctx.arena.CreateArray<s_vertanim_t>( count );
	for (i = 0; i < count; i++)
	{
		pSourceAnim->vanim[t][i].vertex = i;
		pSourceAnim->vanim[t][i].pos = ctx.input.vertex[ctx.input.v_listdata[i].v];
		pSourceAnim->vanim[t][i].normal = ctx.input.normal[ctx.input.v_listdata[i].n];
	}

	ctx.input.inputFile.reset();

	return 1;
}

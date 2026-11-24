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
#include "tier1_v2/path_utils.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/string_utils.hpp"
#include "line_parser.h"

bool IsEnd( char const* pLine );

void UnifyIndices( StudioMdlState &ctx, s_source_t *psource );

struct MtlInfo_t
{
	std::string m_MtlName;
	std::string m_TgaName;
};

static void ParseMtlLib( std::vector<MtlInfo_t> &mtlLib, std::string_view text )
{
	int nCurrentMtl = -1;
	while ( !text.empty() )
	{
		const auto end = text.find_first_of("\r\n");
		LineParser p( text.substr(0, end) );
		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
		const auto command = p.Word();
		if ( vstd::iequals(command, "newmtl") )
		{
			if ( const std::string_view mtlName = p.Word(); !mtlName.empty() )
			{
				nCurrentMtl = std::ssize( mtlLib );
				MtlInfo_t &mtl = mtlLib.emplace_back();
				mtl.m_MtlName = mtlName;
				mtl.m_TgaName = "debugempty";
			}
			continue;
		}

		if ( vstd::iequals(command, "map_Kd") )
		{
			if ( nCurrentMtl < 0 )
				continue;

			if ( const std::string_view tgaView = p.Word(); !tgaView.empty() )
			{
				char tgaPath[MAX_PATH];
				char tgaName[1024];
				if ( tgaView.size() >= sizeof(tgaPath) )
					MdlError( "MTL map_Kd path too long (max {}): {}", sizeof(tgaPath) - 1, tgaView );
				tgaView.copy( tgaPath, tgaView.size() );
				tgaPath[tgaView.size()] = 0;
				Q_FileBase( tgaPath, tgaName, sizeof(tgaName) );
				mtlLib[nCurrentMtl].m_TgaName = tgaName;
			}
			continue;
		}
	}
}

static std::string_view FindMtlEntry( const std::vector<MtlInfo_t> &mtlLib, std::string_view mtlName )
{
	for ( const MtlInfo_t &mtl : mtlLib )
	{
		if ( vstd::iequals( mtl.m_MtlName, mtlName ) )
			return mtl.m_TgaName;
	}
	return mtlName;
}

[[noreturn]] static void ObjLineError( StudioMdlState &ctx, std::string_view why )
{
	MdlError( "OBJ line {}: {}: {}", ctx.input.iLinecount, why, ctx.input.szLine.view() );
}

// "<key> x y z" (extra trailing fields, e.g. vertex colors, are ignored)
static void ReadObjVector( StudioMdlState &ctx, std::string_view key, Vector &out )
{
	LineParser p( ctx.input.szLine.view() );
	p.Consume( key );
	if ( !p.Read( out.x, out.y, out.z ) )
		ObjLineError( ctx, "malformed vector" );
}

// "vt u [v [w]]": v defaults to 0
static void ReadObjTexcoord( StudioMdlState &ctx, Vector2D &out )
{
	LineParser p( ctx.input.szLine.view() );
	p.Consume( "vt" );
	out.y = 0.0f;
	if ( !p.Number( out.x ) || ( !p.Empty() && !p.Consume("#") && !p.Number( out.y ) ) )
		ObjLineError( ctx, "malformed texcoord" );
}

struct ObjFaceVertex
{
	int v = 0, t = 0, n = 0; // 1-based; 0 = absent (t, n)
};

// One face token: v, v/t, v//n or v/t/n
static bool ParseFaceVertex( std::string_view token, ObjFaceVertex &out )
{
	out = {};
	LineParser p( token );
	if ( !p.Number( out.v, '/' ) )
		return false;
	if ( p.Consume( "/" ) )
	{
		if ( !p.Consume( "/" ) )
		{
			if ( !p.Number( out.t, '/' ) )
				return false;
			if ( !p.Consume( "/" ) )
				return p.Empty();
		}
		if ( !p.Number( out.n ) )
			return false;
	}
	return p.Empty();
}

// "f ..." as a triangle fan. VTA files need triangles or quads with normals and ignore texcoords.
static void AddObjFace( StudioMdlState &ctx, int material, bool isVta )
{
	LineParser p( ctx.input.szLine.view() );
	p.Consume( "f" );

	const auto inRange = []( int index, size_t count, int first ) {
		return index >= first && static_cast<size_t>( index ) <= count;
	};

	ObjFaceVertex first, previous;
	size_t count = 0;

	const auto setCorner = [&]( const ObjFaceVertex &fv, uint32_t &vi, uint32_t &ni, uint32_t &ti ) {
		vi = fv.v - 1;
		ni = ( fv.n > 0 ) ? fv.n - 1 : 0;
		ti = ( !isVta && fv.t > 0 ) ? fv.t - 1 : 0;
	};
	while ( !p.Empty() && !p.Consume("#") )
	{
		ObjFaceVertex vertex;
		if ( !ParseFaceVertex( p.Word(), vertex ) )
			ObjLineError( ctx, "malformed face vertex" );
		if ( !inRange( vertex.v, ctx.input.vertex.size(), 1 ) ||
			 ( !isVta && !inRange( vertex.t, ctx.input.texcoord[0].size(), 0 ) ) ||
			 !inRange( vertex.n, ctx.input.normal.size(), 0 ) )
			ObjLineError( ctx, "face index out of range" );
		if ( vertex.n == 0 && ( isVta || ctx.input.normal.empty() ) )
			ObjLineError( ctx, "face vertex has no normal" );
		if ( count == 0 )
			first = vertex;
		if ( count >= 2 )
		{
			if ( isVta && count >= 4 )
				ObjLineError( ctx, "face must have 3 or 4 vertices" );
			auto &face = ctx.input.faces.emplace_back();
			face.material = material;
			setCorner( first, face.a, face.na, face.ta[0] );
			setCorner( vertex, face.b, face.nb, face.tb[0] );
			setCorner( previous, face.c, face.nc, face.tc[0] );
		}
		previous = vertex;
		++count;
	}
	if ( count < 3 )
		ObjLineError( ctx, "face needs at least 3 vertices" );
}


int Load_OBJ( StudioMdlState &ctx, s_source_t *psource )
{
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

	while ( GetLineInput( ctx ) ) 
	{
		if ( strncmp( ctx.input.szLine, "v ", 2 ) == 0 )
		{
			Vector &vertex = ctx.input.vertex.emplace_back();
			ReadObjVector( ctx, "v", vertex );
			s_boneweight_t &boneWeight = ctx.input.bone.emplace_back();
			boneWeight.numbones = 1;
			boneWeight.bone[0] = 0;
			boneWeight.weight[0] = 1.0;
			ctx.input.sourceBone[ctx.input.bone.size() - 1] = {};
			continue;
		}
		
		if (strncmp( ctx.input.szLine, "vn ", 3 ) == 0)
		{
			ReadObjVector( ctx, "vn", ctx.input.normal.emplace_back() );
			continue;
		}
		
		if (strncmp( ctx.input.szLine, "vt ", 3 ) == 0)
		{
			Vector2D &texcoord = ctx.input.texcoord[0].emplace_back();
			ReadObjTexcoord( ctx, texcoord );
			texcoord.y = 1.0 - texcoord.y;
			continue;
		}
		
		if ( !Q_strncmp( ctx.input.szLine, "mtllib ", 7 ) )
		{
			LineParser p( ctx.input.szLine.view() );
			p.Consume( "mtllib" );
			const std::string_view mtlLibName = p.Word();
			if ( mtlLibName.empty() )
				continue;

			// built before ParseMtlLib reuses szLine as its line buffer
			const std::string fullMtlLibPath = vstd::paths::join( fullDir, mtlLibName );
			// text mode like the filesystem's ReadFile into a text buffer
			if ( FILE *fp = vstd::paths::open_file( fullMtlLibPath, "rt" ) )
			{
				std::string text;
				char chunk[4096];
				for ( size_t nRead; ( nRead = fread( chunk, 1, sizeof( chunk ), fp ) ) > 0; )
					text.append( chunk, nRead );
				fclose( fp );

				ParseMtlLib( mtlLib, text );
			}
			continue;
		}

		if (strncmp( ctx.input.szLine, "usemtl ", 7 ) == 0)
		{
			LineParser p( ctx.input.szLine.view() );
			p.Consume( "usemtl" );
			const std::string_view mtlName = p.Word();
			if ( mtlName.empty() )
				ObjLineError( ctx, "missing material name" );

			int texture = LookupTexture( ctx, FindMtlEntry( mtlLib, mtlName ) );
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

			AddObjFace( ctx, material, false );
			continue;
		}
	}

	if ( ctx.input.texcoord[0].empty() )
		ctx.input.texcoord[0].emplace_back(0, 0);

	UnifyIndices( ctx, psource );

	BuildIndividualMeshes( ctx, psource );

	ctx.input.inputFile.reset();

	return 1;
}



int AppendVTAtoOBJ( StudioMdlState &ctx, s_source_t *psource, std::string_view filename, int frame )
{
	int		i;
	int		material = 0;

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
			ReadObjVector( ctx, "v", tmp );
			VectorTransform( tmp, m, ctx.input.vertex.emplace_back() );

			s_boneweight_t &boneWeight = ctx.input.bone.emplace_back();
			boneWeight.numbones = 1;
			boneWeight.bone[0] = 0;
			boneWeight.weight[0] = 1.0;
			ctx.input.sourceBone[ctx.input.bone.size() - 1] = {};
		}
		else if (strncmp( ctx.input.szLine, "vn ", 3 ) == 0)
		{
			ReadObjVector( ctx, "vn", tmp );
			VectorRotate( tmp, m, ctx.input.normal.emplace_back() );
		}
		else if (strncmp( ctx.input.szLine, "vt ", 3 ) == 0)
		{
			ReadObjTexcoord( ctx, ctx.input.texcoord[0].emplace_back() );
		}
		else if (strncmp( ctx.input.szLine, "usemtl ", 7 ) == 0)
		{
			LineParser p( ctx.input.szLine.view() );
			p.Consume( "usemtl" );
			const std::string_view mtlName = p.Word();
			if ( mtlName.empty() )
				ObjLineError( ctx, "missing material name" );

			int texture = LookupTexture( ctx, mtlName );
			psource->texmap[texture] = texture;	// hack, make it 1:1
			material = UseTextureAsMaterial( ctx, texture );
		}
		else if (strncmp( ctx.input.szLine, "f ", 2 ) == 0)
		{
			AddObjFace( ctx, material, true );
		}
	}
	if ( ctx.input.texcoord[0].empty() )
		ctx.input.texcoord[0].emplace_back(0, 0);

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

//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include <stdlib.h>
#include "tier0/dbg.h"
#include "tier1/interface.h"
#include "studio.h"
#include "studiomdl/optimize.h"
#include "common/cmdlib.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/perfstats.h"
#include "tier1/tier1_logging.h"

extern StudioMdlContext g_StudioMdlContext;

extern void MdlError( char const *pMsg, ... );



static CBufferedLoggingListener s_BufferedLoggingListener;

void SpewPerfStats( studiohdr_t *pStudioHdr, const char *pFilename, unsigned int flags )
{
	char							fileName[260];
	vertexFileHeader_t				*pNewVvdHdr;
	vertexFileHeader_t				*pVvdHdr = 0;
	OptimizedModel::FileHeader_t	*pVtxHdr = 0;
//	studiohwdata_t					studioHWData;
	int								vvdSize = 0;
	
	const char						*prefix[] = { ".dx90.vtx", ".dx80.vtx", ".sw.vtx" };
	const int						numVtxFiles = ( g_gameinfo.bSupportsDX8 && !g_StudioMdlContext.fastBuild ) ? ARRAYSIZE( prefix ) : 1;
	bool							bExtraData = (pStudioHdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;


	if( !( flags & SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS ) )
	{
		LoggingSystem_PushLoggingState();
		LoggingSystem_RegisterLoggingListener( &s_BufferedLoggingListener );
	}

	// no stats on these
	if (!pStudioHdr->numbodyparts)
		return;

	// persist the vvd data
	Q_StripExtension( pFilename, fileName, sizeof( fileName ) );
	strcat( fileName, ".vvd" );

	if (FileExists( fileName ))
	{
		vvdSize = LoadFile( fileName, (void**)&pVvdHdr );
	}
	else
	{
		MdlError( "Could not open '%s'\n", fileName );
	}

	// validate header
	if (pVvdHdr->id != MODEL_VERTEX_FILE_ID)
	{
		MdlError( "Bad id for '%s' (got %d expected %d)\n", fileName, pVvdHdr->id, MODEL_VERTEX_FILE_ID);
	}
	if (pVvdHdr->version != MODEL_VERTEX_FILE_VERSION)
	{
		MdlError( "Bad version for '%s' (got %d expected %d)\n", fileName, pVvdHdr->version, MODEL_VERTEX_FILE_VERSION);
	}
	if (pVvdHdr->checksum != pStudioHdr->checksum)
	{
		MdlError( "Bad checksum for '%s' (got %d expected %d)\n", fileName, pVvdHdr->checksum, pStudioHdr->checksum);
	}

	if (pVvdHdr->numFixups)
	{
		// need to perform mesh relocation fixups
		// allocate a new copy
		pNewVvdHdr = (vertexFileHeader_t *)malloc( vvdSize );
		if (!pNewVvdHdr)
		{
			MdlError( "Error allocating %d bytes for Vertex File '%s'\n", vvdSize, fileName );
		}

		Studio_LoadVertexes( pVvdHdr, pNewVvdHdr, 0, true, bExtraData );

		// discard original
		free( pVvdHdr );

		pVvdHdr = pNewVvdHdr;
	}
	
	// iterate all ???.vtx files
	for (int j = 0; j< numVtxFiles; j++)
	{
		// make vtx filename
		Q_StripExtension( pFilename, fileName, sizeof( fileName ) );
		strcat( fileName, prefix[j] );

		// persist the vtx data
		if (FileExists(fileName))
		{
			LoadFile( fileName, (void**)&pVtxHdr );
		}
		else
		{
			MdlError( "Could not open '%s'\n", fileName );
		}

		// validate header
		if (pVtxHdr->version != OPTIMIZED_MODEL_FILE_VERSION)
		{
			MdlError( "Bad version for '%s' (got %d expected %d)\n", fileName, pVtxHdr->version, OPTIMIZED_MODEL_FILE_VERSION );
		}
		if (pVtxHdr->checkSum != pStudioHdr->checksum)
		{
			MdlError( "Bad checksum for '%s' (got %d expected %d)\n", fileName, pVtxHdr->checkSum, pStudioHdr->checksum );
		}

		// studio render will request these through cache interface
		pStudioHdr->SetVertexBase( (void *)pVvdHdr );
		pStudioHdr->SetIndexBase( (void *)pVtxHdr );

        /*
		g_pStudioRender->LoadModel( pStudioHdr, pVtxHdr, &studioHWData );

		if( flags & SPEWPERFSTATS_SHOWPERF )
		{
			if(  flags & SPEWPERFSTATS_SPREADSHEET )
			{
				printf( "%s,%s,%d,", fileName, prefix[j], studioHWData.m_NumLODs - studioHWData.m_RootLOD );
			}
			else
			{
				printf( "\n" );
				printf( "Performance Stats: %s\n", fileName );
				printf( "------------------\n" );
			}
		}

		int i;
		if( flags & SPEWPERFSTATS_SHOWPERF )
		{
			for( i = studioHWData.m_RootLOD; i < studioHWData.m_NumLODs; i++ )
			{
				DrawModelInfo_t drawModelInfo;
				drawModelInfo.m_Skin = 0;
				drawModelInfo.m_Body = 0;
				drawModelInfo.m_HitboxSet = 0;
				drawModelInfo.m_pClientEntity = 0;
				drawModelInfo.m_pColorMeshes = 0;
				drawModelInfo.m_pStudioHdr = pStudioHdr;
				drawModelInfo.m_pHardwareData = &studioHWData;	
				CUtlBuffer statsOutput( 0, 0, CUtlBuffer::TEXT_BUFFER );
				if( !( flags & SPEWPERFSTATS_SPREADSHEET ) )
				{
					printf( "LOD:%d\n", i );
				}
				drawModelInfo.m_Lod = i;
				DrawModelResults_t results;
				g_pStudioRender->GetPerfStats( &results, drawModelInfo, &statsOutput );
				if( flags & SPEWPERFSTATS_SPREADSHEET )
				{
					printf( "%d,%d,%d,", results.m_ActualTriCount, results.m_NumBatches, results.m_NumMaterials  );
				}
				else
				{
					printf( "    actual tris:%d\n", ( int )results.m_ActualTriCount );
					printf( "    texture memory bytes: %d (only valid in a rendering app)\n", ( int )results.m_TextureMemoryBytes );
					printf( ( char * )statsOutput.Base() );
				}
			}
			if( flags & SPEWPERFSTATS_SPREADSHEET )
			{
				printf( "\n" );
			}
		}
		g_pStudioRender->UnloadModel( &studioHWData );
        */
		free(pVtxHdr);
	}

	if (pVvdHdr)
		free(pVvdHdr);

	if( !( flags & SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS ) )
	{
		LoggingSystem_PopLoggingState();
		s_BufferedLoggingListener.EmitBufferedSpew();
	}
}


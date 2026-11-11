//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include <stdlib.h>

#include "studio.h"
#include "studiomdl/optimize.h"
#include "common/cmdlib.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/perfstats.h"
#include "studiomdl/output_files.hpp"

#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1/tier1_logging.h"
#include "tier1_v2/path_utils.hpp"


static CBufferedLoggingListener s_BufferedLoggingListener;

void SpewPerfStats(studiohdr_t* pStudioHdr, const std::string_view pFilename, unsigned int flags) {
    std::string fileName = std::string{pFilename};
    vertexFileHeader_t* pNewVvdHdr;
    vertexFileHeader_t* pVvdHdr = nullptr;
    OptimizedModel::FileHeader_t* pVtxHdr = nullptr;
    //	studiohwdata_t					studioHWData;
    int vvdSize = 0;

    const char* prefix[] = {".dx90.vtx", ".dx80.vtx", ".sw.vtx"};
    const int numVtxFiles = (g_gameinfo.bSupportsDX8 && !g_StudioMdlContext.fastBuild) ? ARRAYSIZE(prefix) : 1;
    bool bExtraData = (pStudioHdr->flags & STUDIOHDR_FLAGS_EXTRA_VERTEX_DATA) != 0;


    if (!(flags & SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS)) {
        LoggingSystem_PushLoggingState();
        LoggingSystem_RegisterLoggingListener(&s_BufferedLoggingListener);
    }

    // no stats on these
    if (!pStudioHdr->numbodyparts)
        return;

    // persist the vvd data
    vstd::paths::strip_extension(fileName);
    fileName+= ".vvd";

    vvdSize = LoadOutputFile(fileName, (void**) &pVvdHdr);

    // validate header
    if (pVvdHdr->id != MODEL_VERTEX_FILE_ID) {
        MdlError("Bad id for '{}' (got {} expected {})\n", fileName, pVvdHdr->id, MODEL_VERTEX_FILE_ID);
    }
    if (pVvdHdr->version != MODEL_VERTEX_FILE_VERSION) {
        MdlError("Bad version for '{}' (got {} expected {})\n", fileName, pVvdHdr->version, MODEL_VERTEX_FILE_VERSION);
    }
    if (pVvdHdr->checksum != pStudioHdr->checksum) {
        MdlError("Bad checksum for '{}' (got {} expected {})\n", fileName, pVvdHdr->checksum, pStudioHdr->checksum);
    }

    if (pVvdHdr->numFixups) {
        // need to perform mesh relocation fixups
        // allocate a new copy
        pNewVvdHdr = (vertexFileHeader_t*) malloc(vvdSize);
        if (!pNewVvdHdr) {
            MdlError("Error allocating {} bytes for Vertex File '{}'\n", vvdSize, fileName);
        }

        Studio_LoadVertexes(pVvdHdr, pNewVvdHdr, 0, true, bExtraData);

        // discard original
        free(pVvdHdr);

        pVvdHdr = pNewVvdHdr;
    }

    // iterate all ???.vtx files
    for (int j = 0; j < numVtxFiles; j++) {
        // make vtx filename
        fileName = pFilename;
        vstd::paths::strip_extension(fileName);
        fileName+= prefix[j];

        // persist the vtx data
        LoadOutputFile(fileName, (void**) &pVtxHdr);

        // validate header
        if (pVtxHdr->version != OPTIMIZED_MODEL_FILE_VERSION) {
            MdlError("Bad version for '{}' (got {} expected {})\n", fileName, pVtxHdr->version,
                     OPTIMIZED_MODEL_FILE_VERSION);
        }
        if (pVtxHdr->checkSum != pStudioHdr->checksum) {
            MdlError("Bad checksum for '{}' (got {} expected {})\n", fileName, pVtxHdr->checkSum, pStudioHdr->checksum);
        }

        // studio render will request these through cache interface
        pStudioHdr->SetVertexBase((void*) pVvdHdr);
        pStudioHdr->SetIndexBase((void*) pVtxHdr);

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

    if (!(flags & SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS)) {
        LoggingSystem_PopLoggingState();
        s_BufferedLoggingListener.EmitBufferedSpew();
    }
}

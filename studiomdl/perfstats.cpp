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

void SpewPerfStats(StudioMdlState &ctx, studiohdr_t* pStudioHdr, const std::string_view pFilename, unsigned int flags) {
    std::string fileName = std::string{pFilename};

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

    auto vvdFileOpt = LoadOutputFile(ctx, fileName);
    if (!vvdFileOpt)
        return;
    auto vvdFile = std::move(vvdFileOpt.value());

    auto* pVvdHdr = vvdFile.reinterpret_at<vertexFileHeader_t>(0);

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
        auto newVvdBuffer = vstd::Buffer(vvdFile.size());
        auto* pNewVvdHdr = newVvdBuffer.reinterpret_at<vertexFileHeader_t>(0);
        if (!pNewVvdHdr) {
            MdlError("Error allocating {} bytes for Vertex File '{}'\n", vvdFile.size(), fileName);
        }

        Studio_LoadVertexes(pVvdHdr, pNewVvdHdr, 0, true, bExtraData);
        vvdFile = std::move(newVvdBuffer);
        pNewVvdHdr = vvdFile.reinterpret_at<vertexFileHeader_t>(0);
        pVvdHdr = pNewVvdHdr;
    }

    // iterate all ???.vtx files
    // make vtx filename
    fileName = pFilename;
    vstd::paths::strip_extension(fileName);
    fileName+= ".dx90.vtx";

    // persist the vtx data
    auto vtxFileOpt = LoadOutputFile(ctx, fileName);
    if (!vtxFileOpt)
        return;
    auto vtxFile = std::move(vtxFileOpt.value());

    auto* pVtxHdr = vtxFile.reinterpret_at<OptimizedModel::FileHeader_t>(0);

    // validate header
    if (pVtxHdr->version != OPTIMIZED_MODEL_FILE_VERSION) {
        MdlError("Bad version for '{}' (got {} expected {})\n", fileName, pVtxHdr->version,
                 OPTIMIZED_MODEL_FILE_VERSION);
    }
    if (pVtxHdr->checkSum != pStudioHdr->checksum) {
        MdlError("Bad checksum for '{}' (got {} expected {})\n", fileName, pVtxHdr->checkSum, pStudioHdr->checksum);
    }

    if (!(flags & SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS)) {
        LoggingSystem_PopLoggingState();
        s_BufferedLoggingListener.EmitBufferedSpew();
    }
}

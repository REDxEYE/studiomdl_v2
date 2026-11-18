//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include <cstdio>
#include <print>
#include <stdlib.h>
#include <vector>

#include "studio.h"
#include "studiomdl/optimize.h"
#include "common/cmdlib.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/perfstats.h"
#include "studiomdl/output_files.hpp"
#include "studiomdl/studiomdl_log.hpp"

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

// Reads a whole .mdl and converts its header to the current version; the message prefix is the option name
static bool LoadStudioModel(const std::string_view option, const std::string_view mdlPath, std::vector<char> &buf) {
    FILE* fp = vstd::paths::open_file(mdlPath, "rb");
    if (!fp) {
        MdlPrint("{}: can't open '{}'\n", option, mdlPath);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    buf.resize(ftell(fp));
    fseek(fp, 0, SEEK_SET);
    fread(buf.data(), 1, buf.size(), fp);
    fclose(fp);

    studiohdr_t* pHdr = reinterpret_cast<studiohdr_t*>(buf.data());
    Studio_ConvertStudioHdrToNewVersion(pHdr);
    if (pHdr->version != STUDIO_VERSION) {
        MdlPrint("{}: '{}' is wrong version ({} should be {}).\n", option, mdlPath, pHdr->version, STUDIO_VERSION);
        return false;
    }
    return true;
}

int PrintModelSurfaceProps(const std::string_view mdlPath) {
    std::vector<char> buf;
    if (!LoadStudioModel("-PrintSurfaceProps", mdlPath, buf))
        return 1;

    const studiohdr_t* pHdr = reinterpret_cast<const studiohdr_t*>(buf.data());
    for (int i = 0; i < pHdr->numbones; i++) {
        MdlPrint("{}\n", pHdr->pBone(i)->pszSurfaceProp());
    }
    return 0;
}

int ReportModelPerf(StudioMdlState &ctx, const std::string_view mdlPath, const bool spreadsheet) {
    std::vector<char> buf;
    if (!LoadStudioModel("-mdlreport", mdlPath, buf))
        return 1;

    unsigned int flags = SPEWPERFSTATS_SHOWPERF;
    if (spreadsheet)
        flags |= SPEWPERFSTATS_SPREADSHEET;
    SpewPerfStats(ctx, reinterpret_cast<studiohdr_t*>(buf.data()), mdlPath, flags);
    return 0;
}

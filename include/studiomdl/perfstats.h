//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#pragma once


#include "studio.h"

enum
{
	SPEWPERFSTATS_SHOWSTUDIORENDERWARNINGS = 1,
	SPEWPERFSTATS_SHOWPERF = 2,
	SPEWPERFSTATS_SPREADSHEET = 4,
};

struct StudioMdlState;

void SpewPerfStats( StudioMdlState &ctx, studiohdr_t *pStudioHdr, std::string_view pFilename, unsigned int flags );

// -PrintSurfaceProps: prints the surface prop of every bone of a compiled .mdl; returns the exit code
int PrintModelSurfaceProps(std::string_view mdlPath);

// -mdlreport: prints the perf info of a compiled .mdl (and its .vvd/.dx90.vtx); returns the exit code
int ReportModelPerf(StudioMdlState &ctx, std::string_view mdlPath, bool spreadsheet);


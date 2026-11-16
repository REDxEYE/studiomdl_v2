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


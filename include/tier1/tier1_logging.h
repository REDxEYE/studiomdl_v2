//============ Copyright (c) Valve Corporation, All rights reserved. ============
//
// Tier1 logging helpers.
//
//===============================================================================

#pragma once

#if defined( COMPILER_MSVC )
#pragma once
#endif

#include "tier0/logging.h"
#include "utlbuffer.h"

class CBufferedLoggingListener : public ILoggingListener
{
public:
	CBufferedLoggingListener();

    void Log( const LoggingContext_t *pContext, const tchar *pMessage ) override;

	void EmitBufferedSpew();

private:

	CUtlBuffer m_StoredSpew;
};


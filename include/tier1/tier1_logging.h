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
#include "color.h"

class CBufferedLoggingListener : public ILoggingListener
{
public:
	CBufferedLoggingListener();

	virtual void Log( const LoggingContext_t *pContext, const tchar *pMessage );

	void EmitBufferedSpew();

private:

	CUtlBuffer m_StoredSpew;
};


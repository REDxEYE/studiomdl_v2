//
// Created by RED on 15.05.2024.
//

#pragma once

#include "common/cmdlib.h"
#include "studiomdl/platform_compat.h"
#include "studiomdl/studiomdl_log.hpp"

void MdlExceptionFilter(uint32_t code);
LONG __stdcall VExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo);

class CMdlLoggingListener : public CCmdLibStandardLoggingListener {
    virtual void Log(const LoggingContext_t *pContext, const tchar *pMessage);
};


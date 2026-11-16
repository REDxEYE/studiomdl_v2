//
// Created by RED on 15.05.2024.
//

#pragma once

#include "common/cmdlib.h"
#include "studiomdl/platform_compat.h"

struct StudioMdlState;

void MdlExceptionFilter(uint32_t code);
LONG __stdcall VExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo);

class CMdlLoggingListener : public CCmdLibStandardLoggingListener {
    void Log(const LoggingContext_t *pContext, const tchar *pMessage) override;
};

// Makes the diagnostics (MdlError/MdlWarning/MdlMessage, CMdlLoggingListener) compile-aware for its lifetime: -quiet
// prefix and suppression, warning limits, removal of the partial outputs on error, parseable result. Installed by the
// compile entry point; nested scopes restore the previous state. Without a scope the diagnostics only print.
class ScopedMdlDiagnostics {
public:
    explicit ScopedMdlDiagnostics(StudioMdlState &ctx);
    ~ScopedMdlDiagnostics();

    ScopedMdlDiagnostics(const ScopedMdlDiagnostics &) = delete;
    ScopedMdlDiagnostics &operator=(const ScopedMdlDiagnostics &) = delete;

private:
    StudioMdlState *m_pPrevious;
};


//
// Created by RED on 15.05.2024.
//

#pragma once

#include "common/cmdlib.h"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/platform_compat.h"
#include "tier0/dbg.h"

struct StudioMdlState;

void MdlExceptionFilter(uint32_t code);
LONG __stdcall VExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo);

class CMdlLoggingListener : public CCmdLibStandardLoggingListener {
    void Log(const LoggingContext_t *pContext, const tchar *pMessage) override;
};

// Makes the diagnostics (MdlError/MdlWarning/MdlMessage/MdlPrint, CMdlLoggingListener) compile-aware for its
// lifetime: -quiet prefix and suppression, warning limits and the diagnostics sink of ctx.diagnostics. Installed by
// the compile entry point; nested scopes restore the previous state. Without a scope the diagnostics only print.
class ScopedMdlDiagnostics {
public:
    explicit ScopedMdlDiagnostics(StudioMdlState &ctx);
    ~ScopedMdlDiagnostics();

    ScopedMdlDiagnostics(const ScopedMdlDiagnostics &) = delete;
    ScopedMdlDiagnostics &operator=(const ScopedMdlDiagnostics &) = delete;

private:
    StudioMdlState *m_pPrevious;
};

// What the console tool does for a compile that ended with MdlCompileError, after MdlError printed the message:
// prints "Aborted Processing" (when the model has a name), removes the partially written output files and prints
// the parseable result. CLI entry point only (stdout); a library host reports the error from the exception and has
// written nothing to disk before the compile succeeded.
void MdlAbortCompile(StudioMdlState &ctx);

// After a compile that ended with an exception (any kind): destroys what it left in the datamodel, every file and
// every element that belongs to none, so the next compile starts clean and EndProcessing finds nothing leaked.
// Returns the number of elements that belonged to no file. After a failed compile the CLI reports them as
// "Leaking N elements": with scratch elements RAII-owned (DmElementPtr) that is a real leak, normally 0.
int MdlDiscardDataModelContents();

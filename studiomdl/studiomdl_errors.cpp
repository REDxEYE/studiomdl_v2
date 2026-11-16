//
// Created by RED on 15.05.2024.
//

#include "studiomdl/platform_compat.h"
#include "studiomdl_errors.h"
#include "common/scriplib.h"
#include "studiomdl/studiomdl.h"
#include "datamodel/idatamodel.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"

#include <filesystem>
#include <print>
#include <string>

#include "../include/studiomdl/studiomdl_helpers.hpp"

// The diagnostics are called from ~400 places, libraries included, that know nothing about a compile, so they can't
// take the compile state as a parameter. Instead the compile entry point installs a ScopedMdlDiagnostics, and they read
// the options, paths and the -quiet header flag through this pointer. This is the ONLY implicit access to the compile
// state in studiomdl; nothing else may read it. nullptr (no scope, e.g. dmxconvert): print without the compile parts.
static thread_local StudioMdlState *s_pDiagnosticsState = nullptr;

ScopedMdlDiagnostics::ScopedMdlDiagnostics(StudioMdlState &ctx) : m_pPrevious(s_pDiagnosticsState) {
    s_pDiagnosticsState = &ctx;
}

ScopedMdlDiagnostics::~ScopedMdlDiagnostics() {
    s_pDiagnosticsState = m_pPrevious;
}

// In a -quiet run the first message is preceded by the QC path; every message is indented under it.
static void PrintQuietPrefix() {
    StudioMdlState *ctx = s_pDiagnosticsState;
    if (!ctx || !ctx->options.quiet)
        return;
    if (!ctx->core.quietHeaderPrinted) {
        std::print("{} :\n", ctx->paths.fullpath);
        ctx->core.quietHeaderPrinted = true;
    }
    std::print("\t");
}

void InstallMdlErrorHandler(ScriptTokenizer &tokenizer) {
    tokenizer.SetErrorHandler([](const std::string &message) { MdlError("{}", message); });
}

void MdlErrorV(std::string_view fmt, std::format_args args) {
    static constexpr const char *knownExtensions[] = {".mdl", ".ani", ".phy", ".sw.vtx", ".dx80.vtx", ".dx90.vtx", ".vvd"};

    StudioMdlState *ctx = s_pDiagnosticsState;

    PrintQuietPrefix();
    std::print("ERROR: {}", std::vformat(fmt, args));

    // delete premature files
    // unforunately, content is built without verification
    // ensuring that targets are not available, prevents check-in
    if (ctx && ctx->paths.hasModelName) {
        if (ctx->options.quiet) {
            std::print("\t");
        }

        // undescriptive errors in batch processes could be anonymous
        std::print("ERROR: Aborted Processing on '{}'\n", ctx->paths.outname);

        const std::string baseName = OutputModelPath(*ctx, "");
        for (const char *pExtension: knownExtensions) {
            std::error_code ec;
            std::filesystem::remove(vstd::paths::to_fs(baseName + pExtension), ec);
        }
    }

    for (int i = 0; i < g_pDataModel->NumFileIds(); ++i) {
        g_pDataModel->UnloadFile(g_pDataModel->GetFileId(i));
    }

    if (ctx && ctx->options.parseable_completion_output) {
        std::print("\nRESULT: ERROR\n");
    }

    exit(-1);
}

void MdlWarningV(std::string_view fmt, std::format_args args) {
    StudioMdlState *ctx = s_pDiagnosticsState;
    if (ctx && (ctx->options.noWarnings || ctx->options.maxWarnings == 0))
        return;

    uint16_t old = SetConsoleTextColor(1, 1, 0, 1);

    PrintQuietPrefix();
    std::print("WARNING: {}", std::vformat(fmt, args));

    if (ctx) {
        if (ctx->options.maxWarnings > 0)
            ctx->options.maxWarnings--;

        if (ctx->options.maxWarnings == 0) {
            if (ctx->options.quiet) {
                std::print("\t");
            }
            std::print("suppressing further warnings...\n");
        }
    }

    RestoreConsoleTextColor(old);
}

void MdlMessageV(std::string_view fmt, std::format_args args) {
    const StudioMdlState *ctx = s_pDiagnosticsState;
    if (!ctx || !ctx->options.quiet) {
        std::vprint_unicode(stdout, fmt, args);
    }
}

void CMdlLoggingListener::Log(const LoggingContext_t *pContext, const tchar *pMessage) {
    const StudioMdlState *ctx = s_pDiagnosticsState;
    if (pContext->m_Severity == LS_MESSAGE && ctx && ctx->options.quiet) {
        // suppress
    } else if (pContext->m_Severity == LS_WARNING) {
        MdlWarning("{}", pMessage);
    } else {
        CCmdLibStandardLoggingListener::Log(pContext, pMessage);
    }
}

//#ifndef _DEBUG

void MdlHandleCrash(const char *pMessage, bool bAssert) {
    MdlError("'{}' (assert: {:d})\n", pMessage, bAssert);
}

// This is called if we crash inside our crash handler. It just terminates the process immediately.
int32_t __stdcall MdlSecondExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    TerminateProcess(GetCurrentProcess(), 2);
    return EXCEPTION_EXECUTE_HANDLER; // (never gets here anyway)
}

void MdlExceptionFilter(uint32_t code) {
    // This is called if we crash inside our crash handler. It just terminates the process immediately.
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)MdlSecondExceptionFilter);

    //DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;

#define ERR_RECORD(name) { name, #name }
    struct {
        uint32_t code;
        const char *pReason;
    } errors[] =
            {
                    ERR_RECORD(EXCEPTION_ACCESS_VIOLATION),
                    ERR_RECORD(EXCEPTION_ARRAY_BOUNDS_EXCEEDED),
                    ERR_RECORD(EXCEPTION_BREAKPOINT),
                    ERR_RECORD(EXCEPTION_DATATYPE_MISALIGNMENT),
                    ERR_RECORD(EXCEPTION_FLT_DENORMAL_OPERAND),
                    ERR_RECORD(EXCEPTION_FLT_DIVIDE_BY_ZERO),
                    ERR_RECORD(EXCEPTION_FLT_INEXACT_RESULT),
                    ERR_RECORD(EXCEPTION_FLT_INVALID_OPERATION),
                    ERR_RECORD(EXCEPTION_FLT_OVERFLOW),
                    ERR_RECORD(EXCEPTION_FLT_STACK_CHECK),
                    ERR_RECORD(EXCEPTION_FLT_UNDERFLOW),
                    ERR_RECORD(EXCEPTION_ILLEGAL_INSTRUCTION),
                    ERR_RECORD(EXCEPTION_IN_PAGE_ERROR),
                    ERR_RECORD(EXCEPTION_INT_DIVIDE_BY_ZERO),
                    ERR_RECORD(EXCEPTION_INT_OVERFLOW),
                    ERR_RECORD(EXCEPTION_INVALID_DISPOSITION),
                    ERR_RECORD(EXCEPTION_NONCONTINUABLE_EXCEPTION),
                    ERR_RECORD(EXCEPTION_PRIV_INSTRUCTION),
                    ERR_RECORD(EXCEPTION_SINGLE_STEP),
                    ERR_RECORD(EXCEPTION_STACK_OVERFLOW),
                    ERR_RECORD(EXCEPTION_ACCESS_VIOLATION),
            };

    int nErrors = sizeof(errors) / sizeof(errors[0]);
    {
        int i;
        for (i = 0; i < nErrors; i++) {
            if (errors[i].code == code)
                MdlHandleCrash(errors[i].pReason, true);
        }

        if (i == nErrors) {
            MdlHandleCrash("Unknown reason", true);
        }
    }

    TerminateProcess(GetCurrentProcess(), 1);
}

LONG __stdcall VExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    MdlExceptionFilter(ExceptionInfo->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER; // (never gets here anyway)
}
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

#include <cstdio>
#include <filesystem>
#include <format>
#include <print>
#include <string>

#include "../include/studiomdl/studiomdl_helpers.hpp"

// The diagnostics are called from ~400 places, libraries included, that know nothing about a compile, so they can't
// take the compile state as a parameter. Instead the compile entry point installs a ScopedMdlDiagnostics, and they read
// the options, paths and the -quiet header flag through this pointer. This is the ONLY implicit access to the compile
// state in studiomdl; nothing else may read it. nullptr (no scope, e.g. dmxconvert): print without the compile parts.
static thread_local StudioMdlState *s_pDiagnosticsState = nullptr;

// With a sink: the part of the last MdlPrint text that has no line end yet (callers print a line in several pieces).
// It reaches the sink with the next message or when the scope ends, so no text is lost.
static thread_local std::string s_PendingLine;

static bool HasSink(const StudioMdlState *ctx) {
    return ctx && ctx->diagnostics.sink;
}

static void FlushPendingLine(const StudioMdlState *ctx) {
    if (s_PendingLine.empty())
        return;
    const std::string text = std::move(s_PendingLine);
    s_PendingLine.clear();
    if (HasSink(ctx))
        ctx->diagnostics.sink(MdlSeverity::Message, text);
}

// One complete message to the sink (which must exist)
static void EmitToSink(const StudioMdlState *ctx, const MdlSeverity severity, const std::string_view text) {
    FlushPendingLine(ctx);
    ctx->diagnostics.sink(severity, text);
}

// Message text of the exceptions: the text without its trailing line end
static std::string TrimmedMessage(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return std::string(text);
}

ScopedMdlDiagnostics::ScopedMdlDiagnostics(StudioMdlState &ctx) : m_pPrevious(s_pDiagnosticsState) {
    s_pDiagnosticsState = &ctx;
}

ScopedMdlDiagnostics::~ScopedMdlDiagnostics() {
    FlushPendingLine(s_pDiagnosticsState);
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
    tokenizer.SetErrorHandler([](const std::string &message, const std::string &file, const int line) {
        MdlErrorAt(file, line, "{}", message);
    });
}

void MdlErrorV(std::string_view fmt, std::format_args args) {
    MdlErrorAtV({}, 0, fmt, args);
}

// "ERROR: <text>" to the sink, or to stdout (with the -quiet prefix)
static void ReportError(StudioMdlState *ctx, const std::string &text) {
    if (HasSink(ctx)) {
        EmitToSink(ctx, MdlSeverity::Error, "ERROR: " + text);
    }
    else {
        PrintQuietPrefix();
        std::print("ERROR: {}", text);
    }
}

void MdlErrorAtV(std::string_view file, int line, std::string_view fmt, std::format_args args) {
    const std::string text = std::vformat(fmt, args);
    ReportError(s_pDiagnosticsState, text);
    throw MdlCompileError(TrimmedMessage(text), std::string(file), line);
}

int MdlDiscardDataModelContents() {
    if (!g_pDataModel)
        return 0;
    for (int i = 0; i < g_pDataModel->NumFileIds(); ++i) {
        g_pDataModel->UnloadFile(g_pDataModel->GetFileId(i));
    }
    const int unattached = g_pDataModel->GetAllocatedElementCount();
    g_pDataModel->UnloadFile(DMFILEID_INVALID);
    return unattached;
}

void MdlAbortCompile(StudioMdlState &ctx) {
    static constexpr const char *knownExtensions[] = {".mdl", ".ani", ".phy", ".sw.vtx", ".dx80.vtx", ".dx90.vtx", ".vvd"};

    // delete premature files
    // unforunately, content is built without verification
    // ensuring that targets are not available, prevents check-in
    if (ctx.paths.hasModelName) {
        if (ctx.options.quiet) {
            MdlPrint("\t");
        }

        // undescriptive errors in batch processes could be anonymous
        MdlPrint("ERROR: Aborted Processing on '{}'\n", ctx.paths.outname);

        const std::string baseName = OutputModelPath(ctx, "");
        for (const char *pExtension: knownExtensions) {
            std::error_code ec;
            std::filesystem::remove(vstd::paths::to_fs(baseName + pExtension), ec);
        }
    }

    if (ctx.options.parseable_completion_output) {
        MdlPrint("\nRESULT: ERROR\n");
    }
}

void MdlWarningV(std::string_view fmt, std::format_args args) {
    StudioMdlState *ctx = s_pDiagnosticsState;
    if (ctx && (ctx->options.noWarnings || ctx->options.maxWarnings == 0))
        return;

    if (ctx)
        ctx->diagnostics.warningCount++;

    if (HasSink(ctx)) {
        EmitToSink(ctx, MdlSeverity::Warning, "WARNING: " + std::vformat(fmt, args));
        if (ctx->options.maxWarnings > 0 && --ctx->options.maxWarnings == 0)
            EmitToSink(ctx, MdlSeverity::Warning, "suppressing further warnings...\n");
        return;
    }

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

void MdlPrintV(std::string_view fmt, std::format_args args) {
    StudioMdlState *ctx = s_pDiagnosticsState;
    if (!HasSink(ctx)) {
        std::vprint_unicode(stdout, fmt, args);
        return;
    }

    s_PendingLine += std::vformat(fmt, args);
    for (size_t end; (end = s_PendingLine.find('\n')) != std::string::npos;) {
        const std::string line = s_PendingLine.substr(0, end + 1);
        s_PendingLine.erase(0, end + 1);
        ctx->diagnostics.sink(MdlSeverity::Message, line);
    }
}

void MdlMessageV(std::string_view fmt, std::format_args args) {
    const StudioMdlState *ctx = s_pDiagnosticsState;
    if (!ctx || !ctx->options.quiet) {
        MdlPrintV(fmt, args);
    }
}

void CMdlLoggingListener::Log(const LoggingContext_t *pContext, const tchar *pMessage) {
    StudioMdlState *ctx = s_pDiagnosticsState;
    if (pContext->m_ChannelID == LOG_CONSOLE_OUTPUT) {
        // ConsolePrint text (progress of the libraries): shown as it is, also with -quiet, as the printf it replaced
        if (HasSink(ctx))
            MdlPrintV("{}", std::make_format_args(pMessage));
        else
            CCmdLibStandardLoggingListener::Log(pContext, pMessage);
    } else if (pContext->m_Severity == LS_MESSAGE && ctx && ctx->options.quiet) {
        // suppress
    } else if (pContext->m_Severity == LS_WARNING) {
        MdlWarning("{}", pMessage);
    } else if (HasSink(ctx)) {
        if ((pContext->m_Flags & LCF_DO_NOT_ECHO) != 0)
            return;
        if (pContext->m_Severity == LS_MESSAGE)
            MdlPrintV("{}", std::make_format_args(pMessage));
        else // LS_ERROR: fatal (Plat_ExitException follows), LS_ASSERT: reported, execution continues
            EmitToSink(ctx, pContext->m_Severity == LS_ERROR ? MdlSeverity::Error : MdlSeverity::Warning, pMessage);
    } else {
        CCmdLibStandardLoggingListener::Log(pContext, pMessage);
    }
}

//#ifndef _DEBUG

// Called from a signal handler / exception filter, which an exception can't leave: reports like MdlError, does what the
// console tool does after one and ends the process. (Only the console tool installs the crash handlers.)
[[noreturn]] static void MdlHandleCrash(const char *pMessage, bool bAssert) {
    StudioMdlState *ctx = s_pDiagnosticsState;
    ReportError(ctx, std::format("'{}' (assert: {:d})\n", pMessage, bAssert));
    if (ctx)
        MdlAbortCompile(*ctx);
    MdlDiscardDataModelContents();
    std::fflush(stdout);
    _exit(255);
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
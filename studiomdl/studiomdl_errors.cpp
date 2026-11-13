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

static bool g_bFirstWarning = true;

// In a -quiet run the first message is preceded by the QC path; every message is indented under it.
static void PrintQuietPrefix() {
    if (!g_studiomdl.options.quiet)
        return;
    if (g_bFirstWarning) {
        std::print("{} :\n", g_studiomdl.paths.fullpath);
        g_bFirstWarning = false;
    }
    std::print("\t");
}

void InstallMdlErrorHandler(ScriptTokenizer &tokenizer) {
    tokenizer.SetErrorHandler([](const std::string &message) { MdlError("{}", message); });
}

void MdlErrorV(std::string_view fmt, std::format_args args) {
    static const char *const knownExtensions[] = {".mdl", ".ani", ".phy", ".sw.vtx", ".dx80.vtx", ".dx90.vtx", ".vvd"};

    PrintQuietPrefix();
    std::print("ERROR: {}", std::vformat(fmt, args));

    // delete premature files
    // unforunately, content is built without verification
    // ensuring that targets are not available, prevents check-in
    if (g_studiomdl.paths.hasModelName) {
        if (g_studiomdl.options.quiet) {
            std::print("\t");
        }

        // undescriptive errors in batch processes could be anonymous
        std::print("ERROR: Aborted Processing on '{}'\n", g_studiomdl.paths.outname);

        const std::string baseName = OutputModelPath("");
        for (const char *pExtension: knownExtensions) {
            std::error_code ec;
            std::filesystem::remove(vstd::paths::to_fs(baseName + pExtension), ec);
        }
    }

    for (int i = 0; i < g_pDataModel->NumFileIds(); ++i) {
        g_pDataModel->UnloadFile(g_pDataModel->GetFileId(i));
    }

    if (g_studiomdl.options.parseable_completion_output) {
        std::print("\nRESULT: ERROR\n");
    }

    exit(-1);
}

void MdlWarningV(std::string_view fmt, std::format_args args) {
    if (g_studiomdl.options.noWarnings || g_studiomdl.options.maxWarnings == 0)
        return;

    uint16_t old = SetConsoleTextColor(1, 1, 0, 1);

    PrintQuietPrefix();
    std::print("WARNING: {}", std::vformat(fmt, args));

    if (g_studiomdl.options.maxWarnings > 0)
        g_studiomdl.options.maxWarnings--;

    if (g_studiomdl.options.maxWarnings == 0) {
        if (g_studiomdl.options.quiet) {
            std::print("\t");
        }
        std::print("suppressing further warnings...\n");
    }

    RestoreConsoleTextColor(old);
}

void MdlMessageV(std::string_view fmt, std::format_args args) {
    if (!g_studiomdl.options.quiet) {
        std::vprint_unicode(stdout, fmt, args);
    }
}

void CMdlLoggingListener::Log(const LoggingContext_t *pContext, const tchar *pMessage) {
    if (pContext->m_Severity == LS_MESSAGE && g_studiomdl.options.quiet) {
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
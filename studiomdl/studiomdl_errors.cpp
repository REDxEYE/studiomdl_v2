//
// Created by RED on 15.05.2024.
//

#include <processthreadsapi.h>
#include <errhandlingapi.h>
#include "studiomdl_errors.h"
#include "common/scriplib.h"
#include "studiomdl/studiomdl.h"
#include "datamodel/idatamodel.h"

static bool g_bFirstWarning = true;
extern bool g_bHasModelName;
extern bool g_bNoWarnings;
extern int g_maxWarnings;

void TokenError(const char *fmt, ...) {
    static char output[1024];
    va_list args;

    char *pFilename;
    int iLineNumber;

    if (GetTokenizerStatus(&pFilename, &iLineNumber)) {
                va_start(args, fmt);
        vsprintf(output, fmt, args);

        MdlError("%s(%d): - %s", pFilename, iLineNumber, output);
    } else {
                va_start(args, fmt);
        vsprintf(output, fmt, args);
        MdlError("%s", output);
    }
}

void MdlError(const char *fmt, ...) {
    static char output[1024];
    static char *knownExtensions[] = {".mdl", ".ani", ".phy", ".sw.vtx", ".dx80.vtx", ".dx90.vtx", ".vvd"};
    char fileName[MAX_PATH];
    char baseName[MAX_PATH];
    va_list args;

//	Assert( 0 );
    if (g_quiet) {
        if (g_bFirstWarning) {
            printf("%s :\n", g_fullpath);
            g_bFirstWarning = false;
        }
        printf("\t");
    }

    printf("ERROR: ");
            va_start(args, fmt);
    vprintf(fmt, args);

    // delete premature files
    // unforunately, content is built without verification
    // ensuring that targets are not available, prevents check-in
    if (g_bHasModelName) {
        if (g_quiet) {
            printf("\t");
        }

        // undescriptive errors in batch processes could be anonymous
        printf("ERROR: Aborted Processing on '%s'\n", g_outname);

        strcpy(fileName, gamedir);
        strcat(fileName, "models/");
        strcat(fileName, g_outname);
        Q_FixSlashes(fileName);
        Q_StripExtension(fileName, baseName, sizeof(baseName));

        for (int i = 0; i < ARRAYSIZE(knownExtensions); i++) {
            strcpy(fileName, baseName);
            strcat(fileName, knownExtensions[i]);

            // really need filesystem concept here
//			g_pFileSystem->RemoveFile( fileName );
            unlink(fileName);
        }
    }

    for (int i = 0; i < g_pDataModel->NumFileIds(); ++i) {
        g_pDataModel->UnloadFile(g_pDataModel->GetFileId(i));
    }

    if (g_parseable_completion_output) {
        printf("\nRESULT: ERROR\n");
    }

    exit(-1);
}


void MdlWarning(const char *fmt, ...) {
    va_list args;
    static char output[1024];

    if (g_bNoWarnings || g_maxWarnings == 0)
        return;

    ushort old = SetConsoleTextColor(1, 1, 0, 1);

    if (g_quiet) {
        if (g_bFirstWarning) {
            printf("%s :\n", g_fullpath);
            g_bFirstWarning = false;
        }
        printf("\t");
    }

    //Assert( 0 );

    printf("WARNING: ");
            va_start(args, fmt);
    vprintf(fmt, args);

    if (g_maxWarnings > 0)
        g_maxWarnings--;

    if (g_maxWarnings == 0) {
        if (g_quiet) {
            printf("\t");
        }
        printf("suppressing further warnings...\n");
    }

    RestoreConsoleTextColor(old);
}



void CMdlLoggingListener::Log(const LoggingContext_t *pContext, const tchar *pMessage) {
    if (pContext->m_Severity == LS_MESSAGE && g_quiet) {
        // suppress
    } else if (pContext->m_Severity == LS_WARNING) {
        MdlWarning("%s", pMessage);
    } else {
        CCmdLibStandardLoggingListener::Log(pContext, pMessage);
    }
}

//#ifndef _DEBUG

void MdlHandleCrash(const char *pMessage, bool bAssert) {
    MdlError("'%s' (assert: %d)\n", pMessage, bAssert);
}


// This is called if we crash inside our crash handler. It just terminates the process immediately.
LONG __stdcall MdlSecondExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    TerminateProcess(GetCurrentProcess(), 2);
    return EXCEPTION_EXECUTE_HANDLER; // (never gets here anyway)
}


void MdlExceptionFilter(unsigned long code) {
    // This is called if we crash inside our crash handler. It just terminates the process immediately.
    SetUnhandledExceptionFilter(MdlSecondExceptionFilter);

    //DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;

#define ERR_RECORD(name) { name, #name }
    struct {
        unsigned long code;
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

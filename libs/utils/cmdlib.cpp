//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// -----------------------
// cmdlib.c
// -----------------------
#include <sys/stat.h>

#include "tier0/platform.h"
#include "tier1_v2/buffer.hpp"
#include "tier1_v2/function_timing.hpp"
#ifdef IS_WINDOWS_PC
#include <windows.h>
#endif
#include "cmdlib.h"
#include "tier1/strtools.h"
#ifdef _WIN32
#include <conio.h>
#endif
#include "tier1/utlvector.h"
#include "tier1/utllinkedlist.h"
#include "tier1/keyvalues.h"
#include "filesystem_tools.h"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

#include <filesystem>
#include <string>
#include <vector>

#if defined( MPI )

#include "vmpi.h"
#include "vmpi_tools_shared.h"

#endif


#if defined( _WIN32 ) || defined( WIN32 )
#include <direct.h>
#endif


#if defined( _WIN32 ) || defined( WIN32 )

void CmdLib_FPrintf(FileHandle_t hFile, const char* pFormat, ...) {
    static CUtlVector<char> buf;
    if (buf.size() == 0) {
        buf.SetCount(1024);
    }

    va_list marker;
    va_start(marker, pFormat);

    while (1) {
        int ret = Q_vsnprintf(buf.data(), buf.size(), pFormat, marker);
        if (ret >= 0) {
            // Write the string.
            g_pFileSystem->Write(buf.data(), ret, hFile);

            break;
        }
        else {
            // Make the buffer larger.
            int newSize = buf.size() * 2;
            buf.SetCount(newSize);
            if (buf.size() != newSize) {
                Error("CmdLib_FPrintf: can't allocate space for text.");
            }
        }
    }

    va_end(marker);
}

char* CmdLib_FGets(char* pOut, int outSize, FileHandle_t hFile) {
    int iCur = 0;
    for (; iCur < (outSize - 1); iCur++) {
        char c;
        if (!g_pFileSystem->Read(&c, 1, hFile)) {
            if (iCur == 0)
                return NULL;
            else
                break;
        }

        pOut[iCur] = c;
        if (c == '\n')
            break;

        if (c == EOF) {
            if (iCur == 0)
                return NULL;
            else
                break;
        }
    }

    pOut[iCur] = 0;
    return pOut;
}


static unsigned short g_InitialColor = 0xFFFF;
static unsigned short g_LastColor = 0xFFFF;
static unsigned short g_BadColor = 0xFFFF;
static WORD g_BackgroundFlags = 0xFFFF;
static void GetInitialColors() {
    // Get the old background attributes.
    CONSOLE_SCREEN_BUFFER_INFO oldInfo;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &oldInfo);
    g_InitialColor = g_LastColor = oldInfo.wAttributes & (
                                       FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    g_BackgroundFlags = oldInfo.wAttributes & (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE |
                                               BACKGROUND_INTENSITY);

    g_BadColor = 0;
    if (g_BackgroundFlags & BACKGROUND_RED) {
        g_BadColor |= FOREGROUND_RED;
    }
    if (g_BackgroundFlags & BACKGROUND_GREEN) {
        g_BadColor |= FOREGROUND_GREEN;
    }
    if (g_BackgroundFlags & BACKGROUND_BLUE) {
        g_BadColor |= FOREGROUND_BLUE;
    }
    if (g_BackgroundFlags & BACKGROUND_INTENSITY) {
        g_BadColor |= FOREGROUND_INTENSITY;
    }
}

WORD SetConsoleTextColor(int red, int green, int blue, int intensity) {
    WORD ret = g_LastColor;

    g_LastColor = 0;
    if (red) {
        g_LastColor |= FOREGROUND_RED;
    }
    if (green) {
        g_LastColor |= FOREGROUND_GREEN;
    }
    if (blue) {
        g_LastColor |= FOREGROUND_BLUE;
    }
    if (intensity) {
        g_LastColor |= FOREGROUND_INTENSITY;
    }

    // Just use the initial color if there's a match...
    if (g_LastColor == g_BadColor) {
        g_LastColor = g_InitialColor;
    }

    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), g_LastColor | g_BackgroundFlags);
    return ret;
}

void RestoreConsoleTextColor(WORD color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color | g_BackgroundFlags);
    g_LastColor = color;
}


#if defined( CMDLIB_NODBGLIB )

// This can go away when everything is in bin.
void Error(char const* pMsg, ...) {
    va_list marker;
    va_start(marker, pMsg);
    vprintf(pMsg, marker);
    va_end(marker);

    exit(-1);
}

#else

bool g_bSuppressPrintfOutput = false;

void CCmdLibStandardLoggingListener::Log(const LoggingContext_t* pContext, const tchar* pMessage) {
    if ((pContext->m_Flags & LCF_DO_NOT_ECHO) != 0) {
        return;
    }

    WORD oldColor;
    Color spewColor = pContext->m_Color;
    if (spewColor == UNSPECIFIED_LOGGING_COLOR) {
        switch (pContext->m_Severity) {
            case LS_MESSAGE:
                spewColor = Color(255, 255, 255, 0);
                break;

            case LS_WARNING:
                spewColor = Color(255, 255, 0, 255);
                break;

            case LS_ERROR:
            case LS_ASSERT:
                spewColor = Color(255, 0, 0, 255);
                break;
        }
    }
    oldColor = SetConsoleTextColor(spewColor.r(), spewColor.g(), spewColor.b(), spewColor.a());

#ifdef MPI
if (pContext->m_Severity== LS_ASSERT)
	{
		// VMPI workers don't want to bring up dialogs and suchlike.
		// They need to have a special function installed to handle
		// the exceptions and write the minidumps.
		// Install the function after VMPI_Init with a call:
		// SetupToolsMinidumpHandler( VMPI_ExceptionFilter );
		if ( g_bUseMPI && !g_bMPIMaster && !Plat_IsInDebugSession() )
		{
			// Generating an exception and letting the
			// installed handler handle it
			::RaiseException
				(
				0,							// dwExceptionCode
				EXCEPTION_NONCONTINUABLE,	// dwExceptionFlags
				0,							// nNumberOfArguments,
				NULL						// const ULONG_PTR* lpArguments
				);

			// Never get here (non-continuable exception)

			VMPI_HandleCrash( pMessage, 0, NULL, true );
			exit( 0 );
		}
	}
#endif

if (!g_bSuppressPrintfOutput|| pContext->m_Severity== LS_ERROR)
	{
		printf( "%s", pMessage );
	}

OutputDebugString (pMessage);

	if (pContext->m_Severity== LS_ERROR)
	{
		if ( !g_bSuppressPrintfOutput )
		{
			printf( "\n" );
		}
		OutputDebugString( "\n" );
	}

RestoreConsoleTextColor (oldColor);
}

CCmdLibFileLoggingListener::CCmdLibFileLoggingListener() : m_pLogFile(FILESYSTEM_INVALID_HANDLE) {
}

void CCmdLibFileLoggingListener::Log(const LoggingContext_t* pContext, const tchar* pMessage) {
    if (m_pLogFile != FILESYSTEM_INVALID_HANDLE && (pContext->m_Flags & LCF_CONSOLE_ONLY) == 0) {
        CmdLib_FPrintf(m_pLogFile, "%s", pMessage);
        g_pFileSystem->Flush(m_pLogFile);
    }
}

void CCmdLibFileLoggingListener::Open(char const* pFilename) {
    Assert(m_pLogFile == FILESYSTEM_INVALID_HANDLE);
    m_pLogFile = g_pFileSystem->Open(pFilename, "a");

    Assert(m_pLogFile != FILESYSTEM_INVALID_HANDLE);
    if (!m_pLogFile) {
        Error("Can't create LogFile:\"{}\"\n", pFilename);
    }

    CmdLib_FPrintf(m_pLogFile, "\n\n\n");
}


void CCmdLibFileLoggingListener::Close() {
    if (g_pFileSystem && m_pLogFile != FILESYSTEM_INVALID_HANDLE) {
        g_pFileSystem->Close(m_pLogFile);
        m_pLogFile = FILESYSTEM_INVALID_HANDLE;
    }
}

CCmdLibStandardLoggingListener g_CmdLibOutputLoggingListener;
CCmdLibFileLoggingListener g_CmdLibFileLoggingListener;
bool g_bInstalledSpewFunction = false;

// Process-wide console/logging setup; later calls do nothing (a host may run several compiles)
void InstallSpewFunction() {
    if (!g_bInstalledSpewFunction) {
        g_bInstalledSpewFunction = true;
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);

        LoggingSystem_PushLoggingState();
        LoggingSystem_RegisterLoggingListener(&g_CmdLibOutputLoggingListener);
        LoggingSystem_RegisterLoggingListener(&g_CmdLibFileLoggingListener);
        GetInitialColors();
    }
}

void CmdLib_AllocError(size_t size) {
    Error("Error trying to allocate {:d} bytes.\n", size);
}


int CmdLib_NewHandler(size_t size) {
    CmdLib_AllocError(size);
    return 0;
}

void CmdLib_NewHandler_CPP() {
    CmdLib_NewHandler(0);
    std::abort();
}

void InstallAllocationFunctions() {



#if defined(_MSC_VER) && !defined(__clang__)
_set_new_mode (1);
#endif
std::set_new_handler (CmdLib_NewHandler_CPP);
}
#endif


#endif



// ---------------------------------------------------------------------------------------------------- //
// Helpers.
// ---------------------------------------------------------------------------------------------------- //

// Absolute path of a UTF-8 path relative to the current directory
std::string ExpandArg(std::string_view path) {
    if (vstd::paths::is_absolute(path)) {
        return std::string(path);
    }
    std::error_code ec;
    std::string cwd = vstd::paths::to_utf8(std::filesystem::current_path(ec));
    vstd::paths::fix_slashes(cwd);
    return vstd::paths::join(cwd, path, vstd::paths::native_separator);
}


// Path relative to qdir (the QC directory) made absolute
std::string ExpandPath(const std::string_view qdir, std::string_view path) {
    if (vstd::paths::is_absolute(path)) {
        return std::string(path);
    }
    return std::string(qdir) + std::string(path);
}


char* copystring(const char* s) {
    char* b;
    b = (char*) malloc(strlen(s) + 1);
    V_strcpy(b, s);
    return b;
}


FileHandle_t SafeOpenWrite(const std::string_view filename) {
    FileHandle_t f = g_pFileSystem->Open(filename, "wb");

    if (!f) {
        //Error( "Error opening {}: {}", filename, strerror( errno ) );
        // BUGBUG: No way to get equivalent of errno from IFileSystem!
        Error("Error opening {} ! (Check for write enable)\n", filename);
    }

    return f;
}

void CmdLib_AddBasePath(CmdLibContentPaths &content, const std::string_view path) {
    printf("CmdLib_AddBasePath( \"%.*s\" )\n", static_cast<int>(path.size()), path.data());
    vstd::paths::fix_slashes(content.basePaths.emplace_back(path));
}


void CmdLib_AddNewSearchPath(CmdLibContentPaths &content, const std::string_view path) {
    if (content.relativeDirs.empty()) // first call: remember all original paths
    {
        content.originalBasePaths = content.basePaths;
    }
    vstd::paths::fix_slashes(content.relativeDirs.emplace_back(path));

    // the cross-product of the original paths and the new relative paths; each original path follows its children
    content.basePaths.clear();
    for (const std::string &original: content.originalBasePaths) {
        for (const std::string &added: content.relativeDirs) {
            content.basePaths.push_back(original + added);
        }
        content.basePaths.push_back(original);
    }
}


std::optional<std::pair<std::string, std::string> > CmdLib_HasBasePath(const CmdLibContentPaths &content,
                                                                      const std::string_view fileNameSv) {
    std::string fileName(fileNameSv);
    vstd::paths::fix_slashes(fileName);
    for (const std::string &basePath: content.basePaths) {
        // see if we can rip the base off of the filename.
        if (vstd::istarts_with(fileName, basePath)) {
            return std::pair{basePath, fileName.substr(basePath.size())};
        }
    }
    return std::nullopt;
}

FileHandle_t SafeOpenRead(const CmdLibContentPaths &content, const std::string_view filename) {
    FileHandle_t f = nullptr;
    if (auto result = CmdLib_HasBasePath(content, filename)) {
        const auto &[basePath, remainder] = *result;
        const auto full_path = vstd::paths::join(basePath, remainder);
        f = g_pFileSystem->Open(full_path, "rb");
        if (f) {
            return f;
        }

        Error("Error opening {}\n", filename);
        return f;
    }
    f = g_pFileSystem->Open(filename, "rb");
    if (!f) {
        Error("Error opening {}", filename);
    }

    return f;
}

void SafeRead(FileHandle_t f, vstd::Buffer &buffer) {
    if (g_pFileSystem->Read(buffer.data(), buffer.size(), f) != buffer.size()) {
        Error("File read failure");
    }
}


void SafeWrite(FileHandle_t f, const vstd::Buffer &buffer) {
    if (g_pFileSystem->Write(buffer.data(), buffer.size(), f) != buffer.size()) {
        Error("File write failure");
    }
}


/*
==============
FileExists
==============
*/
qboolean FileExists(const char* filename) {
    const FileHandle_t file = g_pFileSystem->Open(filename, "rb");
    if (file == FILESYSTEM_INVALID_HANDLE) {
        return false;
    }
    g_pFileSystem->Close(file);
    return true;
}

/*
==============
LoadFile
==============
*/
std::optional<vstd::Buffer> LoadFile(const CmdLibContentPaths &content, const std::string_view filename) {
    if (const auto f = SafeOpenRead(content, filename); FILESYSTEM_INVALID_HANDLE != f) {
        vstd::Buffer fileBuffer;
        const size_t length = g_pFileSystem->Size(f);
        fileBuffer.resize(length);
        SafeRead(f, fileBuffer);
        g_pFileSystem->Close(f);
        return fileBuffer;
    }
    return std::nullopt;
}


/*
==============
SaveFile
==============
*/
void SaveFile(const std::string_view filename, const vstd::Buffer &buffer) {
    vstd::FunctionTimingRecorder _("Write");
    FileHandle_t f = SafeOpenWrite(filename);
    SafeWrite(f, buffer);
    g_pFileSystem->Close(f);
}

/*
====================
Extract file parts
====================
*/
// FIXME: should include the slash, otherwise
// backing to an empty path will be wrong when appending a slash


/*
==============
ParseNum / ParseHex
==============
*/


/*
============
CreatePath
============
*/

//-----------------------------------------------------------------------------
// Creates a path, path may already exist
//-----------------------------------------------------------------------------
#if defined( _WIN32 ) || defined( WIN32 )
void SafeCreatePath(char* path) {
    char* ptr;

    // skip past the drive path, but don't strip
    if (path[1] == ':') {
        ptr = strchr(path, '\\');
    }
    else {
        ptr = path;
    }
    while (ptr) {
        ptr = strchr(ptr + 1, '\\');
        if (ptr) {
            *ptr = '\0';
            _mkdir(path);
            *ptr = '\\';
        }
    }
}
#endif

/*
============
QCopyFile

  Used to archive source files
============
*/

#if !defined( _WIN32 ) && !defined( WIN32 )
// POSIX replacements for the Win32 console code above: ANSI colours on a tty, plain stdout/stderr otherwise.
#include <unistd.h>
#include <stdio.h>

unsigned short SetConsoleTextColor(int red, int green, int blue, int intensity) {
    if (isatty(fileno(stdout)))
        printf("\x1b[%dm", (intensity ? 90 : 30) + ((red ? 1 : 0) | (green ? 2 : 0) | (blue ? 4 : 0)));
    return 0;
}

void RestoreConsoleTextColor(unsigned short color) {
    if (isatty(fileno(stdout)))
        printf("\x1b[0m");
}

void CCmdLibStandardLoggingListener::Log(const LoggingContext_t* pContext, const tchar* pMessage) {
    if ((pContext->m_Flags & LCF_DO_NOT_ECHO) != 0)
        return;

    FILE* pOut = pContext->m_Severity >= LS_WARNING ? stderr : stdout;
    bool bColor = isatty(fileno(pOut)) != 0;
    if (bColor && pContext->m_Severity == LS_WARNING)
        fputs("\x1b[93m", pOut);
    else if (bColor && pContext->m_Severity >= LS_ASSERT)
        fputs("\x1b[91m", pOut);
    fputs(pMessage, pOut);
    if (bColor && pContext->m_Severity >= LS_WARNING)
        fputs("\x1b[0m", pOut);
    fflush(pOut);
}

CCmdLibStandardLoggingListener g_CmdLibOutputLoggingListener;

void InstallSpewFunction() {
    static bool s_bInstalled = false;
    if (s_bInstalled)
        return;
    s_bInstalled = true;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    LoggingSystem_PushLoggingState();
    LoggingSystem_RegisterLoggingListener(&g_CmdLibOutputLoggingListener);
}
#endif // !_WIN32

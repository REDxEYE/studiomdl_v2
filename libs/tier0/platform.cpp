//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "pch_tier0.h"
#include <ctime>
#include <cstring>

#if defined( _WIN32 )
#define WINDOWS_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0403
#include <windows.h>
#include <direct.h>
#include <io.h>
#endif

#include <cerrno>

#include "tier0/memalloc.h"
#include "dbg.h"
#include "common/cmdlib.h"

#if defined( PLATFORM_WINDOWS )

static LARGE_INTEGER g_PerformanceFrequency;
static double g_PerformanceCounterToS;
static double g_PerformanceCounterToMS;
static LARGE_INTEGER g_ClockStart;
static bool s_bTimeInitted;

static void InitTime()
{
    if( !s_bTimeInitted )
    {
        s_bTimeInitted = true;
        QueryPerformanceFrequency(&g_PerformanceFrequency);
        g_PerformanceCounterToS = 1.0 / g_PerformanceFrequency.QuadPart;
        g_PerformanceCounterToMS = 1e3 / g_PerformanceFrequency.QuadPart;
        QueryPerformanceCounter(&g_ClockStart);
    }
}

double Plat_FloatTime()
{
    if (! s_bTimeInitted )
        InitTime();

    LARGE_INTEGER CurrentTime;

    QueryPerformanceCounter( &CurrentTime );

    double fRawSeconds = (double)( CurrentTime.QuadPart - g_ClockStart.QuadPart ) * g_PerformanceCounterToS;

    return fRawSeconds;
}

uint32_t Plat_MSTime()
{
    if (! s_bTimeInitted )
        InitTime();

    LARGE_INTEGER CurrentTime;

    QueryPerformanceCounter( &CurrentTime );

    return (uint32_t) ( ( CurrentTime.QuadPart - g_ClockStart.QuadPart ) * g_PerformanceCounterToMS );
}

#elif defined( POSIX )

// Monotonic clock, measured from the first query ("module load" time for all practical purposes).
static bool s_bTimeInitted;
static uint64_t g_ClockStartUS;

static uint64_t Plat_MonotonicUS()
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static void InitTime()
{
    if ( !s_bTimeInitted )
    {
        g_ClockStartUS = Plat_MonotonicUS();
        s_bTimeInitted = true;
    }
}

double Plat_FloatTime()
{
    InitTime();
    return (double)( Plat_MonotonicUS() - g_ClockStartUS ) * 1e-6;
}

uint32_t Plat_MSTime()
{
    InitTime();
    return (uint32_t)( ( Plat_MonotonicUS() - g_ClockStartUS ) / 1000ull );
}

#endif // PLATFORM_WINDOWS/PLATFORM_PS3

void Plat_GetLocalTime(struct tm *pNow) {
    time_t ltime;
    time(&ltime);

    tm *pTime = localtime(&ltime);
    if (pTime)
        *pNow = *pTime;
    else
        memset(pNow, 0, sizeof(*pNow));
}

void Plat_GetModuleFilename(char *pOut, int nMaxBytes) {
#ifdef PLATFORM_WINDOWS_PC
    SetLastError( ERROR_SUCCESS ); // clear the error code
    GetModuleFileName( NULL, pOut, nMaxBytes );
    if ( GetLastError() != ERROR_SUCCESS )
        Error( "Plat_GetModuleFilename: The buffer given is too small (%d bytes).", nMaxBytes );
#else
    // We shouldn't need this on POSIX.
    Assert(false);
    pOut[0] = 0x00;    // Null the returned string in release builds
#endif
}

void Plat_ExitProcess(int nCode) {
#if defined( _WIN32 )
    // We don't want global destructors in our process OR in any DLL to get executed.
    // _exit() avoids calling global destructors in our module, but not in other DLLs.
    TerminateProcess( GetCurrentProcess(), nCode );
#else
    _exit(nCode);
#endif
}

bool Plat_IsInDebugSession() {
#if   defined( _WIN32 )
    return (IsDebuggerPresent() != 0);
#else
    return false;
#endif
}

void Plat_DebugString(const char *psz) {
#ifdef _CERT
    return; // do nothing!
#endif

#if   defined( _WIN32 )
    ::OutputDebugStringA( psz );
#else
    // do nothing?
#endif
}


#if defined( PLATFORM_WINDOWS_PC )
void Plat_MessageBox( const char *pTitle, const char *pMessage )
{
    MessageBox( NULL, pMessage, pTitle, MB_OK );
}
#endif

#if defined( POSIX )
//copied from platform_posix.cpp
static char g_CmdLine[ 2048 ] = "";
PLATFORM_INTERFACE void Plat_SetCommandLine( const char *cmdLine )
{
    strncpy( g_CmdLine, cmdLine, sizeof(g_CmdLine) );
    g_CmdLine[ sizeof(g_CmdLine) -1 ] = 0;
}
#endif

PLATFORM_INTERFACE const tchar *Plat_GetCommandLine() {
#if LINUX
    return CommandLine()->GetCmdLine();
#elif defined( TCHAR_IS_WCHAR )
    return GetCommandLineW();
#else
    return GetCommandLine();
#endif
}

char const *Plat_GetEnv(char const *pEnvVarName) {
    return getenv(pEnvVarName);
}

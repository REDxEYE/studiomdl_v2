//
// Windows <-> POSIX glue for the studiomdl sources.
//
// On Windows this just pulls in the system headers the sources used to include directly. On other platforms it
// provides the few Win32 names (_stat/_chdir/_access, OutputDebugString, the unhandled exception filter and
// TerminateProcess) that studiomdl uses, mapped to POSIX equivalents.
//

#pragma once

#include <cstdint>

#ifdef _WIN32
#pragma once

#include <windows.h>
#include <direct.h>
#include <io.h>
#include <processthreadsapi.h>
#include <errhandlingapi.h>
#include <minmax.h>

#else // POSIX

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>

// <minmax.h> provides min/max on Windows
using std::min;
using std::max;

#ifndef _stat
#define _stat stat
#endif
#ifndef _S_IFDIR
#define _S_IFDIR S_IFDIR
#endif
#define _chdir chdir
#define _access access

inline void OutputDebugString( const char *pString ) { fputs( pString, stderr ); }

// ---- structured exception handling emulation -------------------------------------------------------------------

#define __stdcall

// Win32 API boundary type (windows.h defines it on Windows): only for the emulated exception-filter signature
typedef int32_t LONG;

#define EXCEPTION_EXECUTE_HANDLER 1

#define EXCEPTION_ACCESS_VIOLATION          0xC0000005UL
#define EXCEPTION_DATATYPE_MISALIGNMENT     0x80000002UL
#define EXCEPTION_BREAKPOINT                0x80000003UL
#define EXCEPTION_SINGLE_STEP               0x80000004UL
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED     0xC000008CUL
#define EXCEPTION_FLT_DENORMAL_OPERAND      0xC000008DUL
#define EXCEPTION_FLT_DIVIDE_BY_ZERO        0xC000008EUL
#define EXCEPTION_FLT_INEXACT_RESULT        0xC000008FUL
#define EXCEPTION_FLT_INVALID_OPERATION     0xC0000090UL
#define EXCEPTION_FLT_OVERFLOW              0xC0000091UL
#define EXCEPTION_FLT_STACK_CHECK           0xC0000092UL
#define EXCEPTION_FLT_UNDERFLOW             0xC0000093UL
#define EXCEPTION_INT_DIVIDE_BY_ZERO        0xC0000094UL
#define EXCEPTION_INT_OVERFLOW              0xC0000095UL
#define EXCEPTION_PRIV_INSTRUCTION          0xC0000096UL
#define EXCEPTION_IN_PAGE_ERROR             0xC0000006UL
#define EXCEPTION_ILLEGAL_INSTRUCTION       0xC000001DUL
#define EXCEPTION_NONCONTINUABLE_EXCEPTION  0xC0000025UL
#define EXCEPTION_STACK_OVERFLOW            0xC00000FDUL
#define EXCEPTION_INVALID_DISPOSITION       0xC0000026UL

struct _EXCEPTION_RECORD {
	uint32_t ExceptionCode;
};

struct _EXCEPTION_POINTERS {
	_EXCEPTION_RECORD *ExceptionRecord;
};

typedef LONG ( __stdcall *LPTOP_LEVEL_EXCEPTION_FILTER )( struct _EXCEPTION_POINTERS *ExceptionInfo );

inline int GetCurrentProcess() { return 0; }

inline void TerminateProcess( int /*hProcess*/, unsigned int nExitCode ) { _exit( (int)nExitCode ); }

namespace StudioMdlCompat
{
	inline LPTOP_LEVEL_EXCEPTION_FILTER &CurrentFilter()
	{
		static LPTOP_LEVEL_EXCEPTION_FILTER s_pFilter = nullptr;
		return s_pFilter;
	}

	inline void SignalHandler( int nSignal )
	{
		uint32_t code = EXCEPTION_ACCESS_VIOLATION;
		switch ( nSignal )
		{
		case SIGFPE:  code = EXCEPTION_FLT_INVALID_OPERATION; break;
		case SIGILL:  code = EXCEPTION_ILLEGAL_INSTRUCTION; break;
		case SIGTRAP: code = EXCEPTION_BREAKPOINT; break;
		case SIGBUS:  code = EXCEPTION_DATATYPE_MISALIGNMENT; break;
		default:      code = EXCEPTION_ACCESS_VIOLATION; break;
		}

		if ( LPTOP_LEVEL_EXCEPTION_FILTER pFilter = CurrentFilter() )
		{
			// A fault inside the filter must terminate the process instead of recursing
			signal( nSignal, SIG_DFL );

			_EXCEPTION_RECORD record = { code };
			_EXCEPTION_POINTERS info = { &record };
			pFilter( &info );
		}
		_exit( 1 );
	}
}

// Installs the filter for the signals that correspond to the Windows exceptions
inline LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter( LPTOP_LEVEL_EXCEPTION_FILTER pFilter )
{
	LPTOP_LEVEL_EXCEPTION_FILTER pPrevious = StudioMdlCompat::CurrentFilter();
	StudioMdlCompat::CurrentFilter() = pFilter;

	static const int s_Signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP };
	for ( int nSignal : s_Signals )
	{
		signal( nSignal, pFilter ? StudioMdlCompat::SignalHandler : SIG_DFL );
	}
	return pPrevious;
}

#endif // _WIN32


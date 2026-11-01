//===== Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Legacy Msg/Warning/Error front end of the logging system.
//
//===========================================================================//

#include "platform.h"

#if defined( PLATFORM_WINDOWS_PC )
#define WIN_32_LEAN_AND_MEAN
#include <windows.h>				// IsBadReadPtr and IsBadWritePtr
#endif

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include "color.h"
#include "tier0/dbg.h"

void _ExitOnFatalAssert( const tchar* pFile, int line )
{
	Log_Msg( LOG_ASSERT, _T("Fatal assert failed: %s, line %d.  Application exiting.\n"), pFile, line );
	Plat_ExitProcess( EXIT_FAILURE );
}

//-----------------------------------------------------------------------------
// Templates to assist in validating pointers:
//-----------------------------------------------------------------------------
PLATFORM_INTERFACE void _AssertValidReadPtr( void* ptr, int count/* = 1*/ )
{
#if defined( _WIN32 )
	Assert( !IsBadReadPtr( ptr, count ) );
#else
	Assert( !count || ptr );
#endif
}

PLATFORM_INTERFACE void _AssertValidWritePtr( void* ptr, int count/* = 1*/ )
{
#if defined( _WIN32 )
	Assert( !IsBadWritePtr( ptr, count ) );
#else
	Assert( !count || ptr );
#endif
}

PLATFORM_INTERFACE void _AssertValidStringPtr( const tchar* ptr, int maxchar/* = 0xFFFFFF */ )
{
#if defined( _WIN32 )
	#ifdef TCHAR_IS_CHAR
		Assert( !IsBadStringPtr( ptr, maxchar ) );
	#else
		Assert( !IsBadStringPtrW( ptr, maxchar ) );
	#endif
#else
	Assert( ptr );
#endif
}

// Forward declare for internal use only.
CLoggingSystem *GetGlobalLoggingSystem();

#define Log_LegacyHelperColor( Channel, Severity, Color, MessageFormat ) \
	do \
	{ \
		CLoggingSystem *pLoggingSystem = GetGlobalLoggingSystem(); \
		if ( pLoggingSystem->IsChannelEnabled( Channel, Severity ) ) \
		{ \
			tchar formattedMessage[MAX_LOGGING_MESSAGE_LENGTH]; \
			va_list args; \
			va_start( args, MessageFormat ); \
			Tier0Internal_vsntprintf( formattedMessage, MAX_LOGGING_MESSAGE_LENGTH, MessageFormat, args ); \
			va_end( args ); \
			pLoggingSystem->LogDirect( Channel, Severity, Color, formattedMessage ); \
		} \
	} while( 0 )

#define Log_LegacyHelper( Channel, Severity, MessageFormat ) Log_LegacyHelperColor( Channel, Severity, pLoggingSystem->GetChannelColor( Channel ), MessageFormat )

void Msg( const tchar* pMsgFormat, ... )
{
	Log_LegacyHelper( LOG_GENERAL, LS_MESSAGE, pMsgFormat );
}

void Warning( const tchar *pMsgFormat, ... )
{
	Log_LegacyHelper( LOG_GENERAL, LS_WARNING, pMsgFormat );
}

void Error( const tchar *pMsgFormat, ... )
{
	Log_LegacyHelper( LOG_GENERAL, LS_ERROR, pMsgFormat );
	// Many places that call Error assume that execution will not continue afterwards so it
	// is important to exit here. The function prototype promises that this will happen.
	Plat_ExitProcess( 100 );
}

void DevMsg( const tchar *pMsgFormat, ... )
{
	Log_LegacyHelper( LOG_DEVELOPER, LS_MESSAGE, pMsgFormat );
}

void DevWarning( const tchar *pMsgFormat, ... )
{
	Log_LegacyHelper( LOG_DEVELOPER, LS_WARNING, pMsgFormat );
}

void ConColorMsg( const Color& clr, const tchar* pMsgFormat, ... )
{
	Log_LegacyHelperColor( LOG_CONSOLE, LS_MESSAGE, clr, pMsgFormat );
}

void ConMsg( const tchar *pMsgFormat, ... )
{
	Log_LegacyHelper( LOG_CONSOLE, LS_MESSAGE, pMsgFormat );
}

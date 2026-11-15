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
#include <cstdlib>
#include <format>
#include <string>
#include "color.h"
#include "tier0/dbg.h"

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

// Formats and logs the message if the channel accepts the severity (formatting is skipped otherwise)
static void LogFormatted( LoggingChannelID_t channel, LoggingSeverity_t severity, const Color *pColor, std::string_view fmt, std::format_args args )
{
	CLoggingSystem *pLoggingSystem = GetGlobalLoggingSystem();
	if ( !pLoggingSystem->IsChannelEnabled( channel, severity ) )
		return;
	const std::string message = std::vformat( fmt, args );
	pLoggingSystem->LogDirect( channel, severity, pColor ? *pColor : pLoggingSystem->GetChannelColor( channel ), message.c_str() );
}

void MsgV( std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_GENERAL, LS_MESSAGE, nullptr, fmt, args );
}

void WarningV( std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_GENERAL, LS_WARNING, nullptr, fmt, args );
}

void ErrorV( std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_GENERAL, LS_ERROR, nullptr, fmt, args );
	// Many places that call Error assume that execution will not continue afterwards so it
	// is important to exit here. The function prototype promises that this will happen.
	Plat_ExitProcess( 100 );
}

void DevMsgV( std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_DEVELOPER, LS_MESSAGE, nullptr, fmt, args );
}

void DevWarningV( std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_DEVELOPER, LS_WARNING, nullptr, fmt, args );
}

void ConColorMsgV( const Color &clr, std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_CONSOLE, LS_MESSAGE, &clr, fmt, args );
}

void ConMsgV( std::string_view fmt, std::format_args args )
{
	LogFormatted( LOG_CONSOLE, LS_MESSAGE, nullptr, fmt, args );
}

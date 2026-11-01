//============ Copyright (c) Valve Corporation, All rights reserved. ============
//
// Logging system declarations.
//
// The logging system is a channel-based output mechanism which allows
// subsystems to route their text/diagnostic output to various listeners
//
//===============================================================================

#pragma once

#if defined( COMPILER_MSVC )
#pragma once
#endif

#include "color.h"
#include "icommandline.h"
#include <stdio.h>

/*
	---- Logging System ----

	All Msg(), Warning(), Error(), DevMsg(), ConMsg() output is routed through channels
	(LOG_GENERAL, LOG_CONSOLE, LOG_DEVELOPER, ...) to the registered listeners.

		Log_Msg( ChannelID, [Color], Message, ... )
		Log_Warning( ChannelID, [Color], Message, ... )
		Log_Error( ChannelID, [Color], Message, ... )

	A channel is created with

		DEFINE_LOGGING_CHANNEL( LOG_ChannelName, "ChannelName", [Flags], [MinimumSeverity], [Color] );

	which defines a global channel ID variable named by the first parameter.
	[Flags] is an OR'd set of LoggingChannelFlags_t (default 0); [MinimumSeverity] is the
	lowest severity that is processed (default LS_MESSAGE). Variadic parameters to the
	Log_** macros are not evaluated if the channel is disabled for the severity.
*/

//////////////////////////////////////////////////////////////////////////
// Constants, Types, Forward Declares
//////////////////////////////////////////////////////////////////////////

class CLoggingSystem;

// Maximum length of a sprintf'ed logging message.
const int MAX_LOGGING_MESSAGE_LENGTH = 2048;

// Maximum length of a channel name.
const int MAX_LOGGING_IDENTIFIER_LENGTH = 32;

// Maximum number of logging channels.  Increase if needed.
const int MAX_LOGGING_CHANNEL_COUNT = 256;

// Maximum number of concurrent logging listeners in a given logging state.
const int MAX_LOGGING_LISTENER_COUNT = 16;

// An invalid color set on a channel to imply that it should use
// a device-dependent default color where applicable.
const Color UNSPECIFIED_LOGGING_COLOR( 0, 0, 0, 0 );

// An ID returned by the logging system to refer to a logging channel.
typedef int LoggingChannelID_t;

// A sentinel value indicating an invalid logging channel ID.
const LoggingChannelID_t INVALID_LOGGING_CHANNEL_ID = -1;

// The severity of a logging operation.
enum LoggingSeverity_t
{
	// An informative logging message.
	LS_MESSAGE = 0,

	// A warning, typically non-fatal
	LS_WARNING = 1,

	// A message caused by an Assert**() operation.
	LS_ASSERT = 2,

	// An error, typically fatal/unrecoverable.
	LS_ERROR = 3,

	// A placeholder level, higher than any legal value.
	// Not a real severity value!
	LS_HIGHEST_SEVERITY = 4,
};

// Action which should be taken by logging system as a result of
// a given logged message (returned by ILoggingResponsePolicy::OnLog()).
enum LoggingResponse_t
{
	LR_CONTINUE,
	LR_DEBUGGER,
	LR_ABORT,
};

// Logging channel behavior flags, set on channel creation.
enum LoggingChannelFlags_t
{
	// Indicates that the spew is only relevant to interactive consoles.
	LCF_CONSOLE_ONLY = 0x00000001,

	// Indicates that spew should not be echoed to any output devices.
	// A suitable logging listener must be registered which respects this flag
	// (e.g. a file logger).
	LCF_DO_NOT_ECHO = 0x00000002,
};

// A context structure passed to logging listeners and response policy classes.
struct LoggingContext_t
{
	// ID of the channel being logged to.
	LoggingChannelID_t m_ChannelID;
	// Flags associated with the channel.
	LoggingChannelFlags_t m_Flags;
	// Severity of the logging event.
	LoggingSeverity_t m_Severity;
	// Color of the message, falling back to the channel color.
	// UNSPECIFIED_LOGGING_COLOR means a suitable default should be chosen.
	Color m_Color;
};

// Interface for classes to handle logging output. Log() is called synchronously and
// serially on all listeners registered in the current logging state.
class ILoggingListener
{
public:
	virtual void Log( const LoggingContext_t *pContext, const tchar *pMessage ) = 0;
};

// Interface for policy classes which determine how to behave when a message is logged:
//   LR_CONTINUE (continue execution)
//   LR_DEBUGGER (break into debugger if one is present, otherwise continue)
//   LR_ABORT (terminate process immediately with a failure code of 1)
class ILoggingResponsePolicy
{
public:
	virtual LoggingResponse_t OnLog( const LoggingContext_t *pContext ) = 0;
};

//////////////////////////////////////////////////////////////////////////
// Common Logging Listeners & Logging Response Policies
//////////////////////////////////////////////////////////////////////////

// A basic logging listener which prints to stdout and the debug channel.
class CSimpleLoggingListener : public ILoggingListener
{
public:
	CSimpleLoggingListener( bool bQuietPrintf = false, bool bQuietDebugger = false ) :
		m_bQuietPrintf( bQuietPrintf ),
		m_bQuietDebugger( bQuietDebugger )
	{
	}

	virtual void Log( const LoggingContext_t *pContext, const tchar *pMessage )
	{
		if ( !m_bQuietPrintf )
		{
			_tprintf( _T("%s"), pMessage );
		}

#ifdef _WIN32
		if ( !m_bQuietDebugger && Plat_IsInDebugSession() )
		{
			Plat_DebugString( pMessage );
		}
#endif
	}

	// If set to true, does not print anything to stdout.
	bool m_bQuietPrintf;
	// If set to true, does not print anything to debugger.
	bool m_bQuietDebugger;
};

// A basic logging listener for GUI applications
class CSimpleWindowsLoggingListener : public ILoggingListener
{
public:
	virtual void Log( const LoggingContext_t *pContext, const tchar *pMessage )
	{
		if ( Plat_IsInDebugSession() )
		{
			Plat_DebugString( pMessage );
		}
		if ( pContext->m_Severity == LS_ERROR )
		{
			if ( Plat_IsInDebugSession() )
				DebuggerBreak();

			Plat_MessageBox( "Error", pMessage );
		}
	}
};

// Default logging response policy used when one is not specified.
class CDefaultLoggingResponsePolicy : public ILoggingResponsePolicy
{
public:
	virtual LoggingResponse_t OnLog( const LoggingContext_t *pContext )
	{
		if ( pContext->m_Severity == LS_ASSERT && !CommandLine()->FindParm( "-noassert" ) )
		{
			return LR_DEBUGGER;
		}
		else if ( pContext->m_Severity == LS_ERROR )
		{
			return LR_ABORT;
		}
		else
		{
			return LR_CONTINUE;
		}
	}
};

// A logging response policy which never terminates the process, even on error.
class CNonFatalLoggingResponsePolicy : public ILoggingResponsePolicy
{
public:
	virtual LoggingResponse_t OnLog( const LoggingContext_t *pContext )
	{
		if ( ( pContext->m_Severity == LS_ASSERT && !CommandLine()->FindParm( "-noassert" ) ) || pContext->m_Severity == LS_ERROR )
		{
			return LR_DEBUGGER;
		}
		else
		{
			return LR_CONTINUE;
		}
	}
};

//////////////////////////////////////////////////////////////////////////
// Central Logging System
//////////////////////////////////////////////////////////////////////////

// The central logging system; all tier0 functionality works with the single
// global instance (GetGlobalLoggingSystem()).
class CLoggingSystem
{
public:
	struct LoggingChannel_t;

	CLoggingSystem();
	~CLoggingSystem();

	// Register a logging channel with the logging system.
	// The same channel can be registered multiple times, but the parameters
	// in each call must either match across all calls or be set to defaults on any given call.
	// Not thread-safe; the channel definition macro calls this during static initialization.
	LoggingChannelID_t RegisterLoggingChannel( const char *pChannelName, int flags = 0, LoggingSeverity_t minimumSeverity = LS_MESSAGE, Color spewColor = UNSPECIFIED_LOGGING_COLOR );

	// Returns true if the given channel has been initialized.
	// The main purpose is catching m_nChannelCount being zero because no channels have been registered.
	bool IsValidChannelID( LoggingChannelID_t channelID ) const { return ( channelID >= 0 ) && ( channelID < m_nChannelCount ); }

	// Returns true if the given channel will spew at the given severity level.
	bool IsChannelEnabled( LoggingChannelID_t channelID, LoggingSeverity_t severity ) const { return IsValidChannelID( channelID ) && m_RegisteredChannels[channelID].IsEnabled( severity ); }

	// Sets the spew level of all channels with the given name.
	void SetChannelSpewLevelByName( const char *pName, LoggingSeverity_t minimumSeverity );

	Color GetChannelColor( LoggingChannelID_t channelID ) const { return m_RegisteredChannels[channelID].m_SpewColor; }

	// Saves the current logging state on a stack and starts a new, clear state
	// (no listeners, default logging response policy); PopLoggingState restores it.
	void PushLoggingState();
	void PopLoggingState();

	// Registers a logging listener (a class which handles logged messages).
	void RegisterLoggingListener( ILoggingListener *pListener );

	// Clears out all of the current logging state (removes all listeners,
	// sets the response policy to the default).
	void ResetCurrentLoggingState();

	// Sets a policy class to decide what should happen when messages of a
	// particular severity are logged (e.g. exit on error, break into debugger).
	// If pLoggingResponse is NULL, uses the default response policy class.
	void SetLoggingResponsePolicy( ILoggingResponsePolicy *pLoggingResponse );

	// Logs a message to the specified channel using a given severity and
	// spew color.  Passing in UNSPECIFIED_LOGGING_COLOR for 'color' allows
	// the logging listeners to provide a default.
	// NOTE: test 'IsChannelEnabled(channelID,severity)' before calling this!
	LoggingResponse_t LogDirect( LoggingChannelID_t channelID, LoggingSeverity_t severity, Color color, const tchar *pMessage );

	// Internal data to represent a logging channel.
	struct LoggingChannel_t
	{
		bool IsEnabled( LoggingSeverity_t severity ) const { return severity >= m_MinimumSeverity; }

		LoggingChannelID_t m_ID;
		LoggingChannelFlags_t m_Flags; // an OR'd combination of LoggingChannelFlags_t
		LoggingSeverity_t m_MinimumSeverity; // The minimum severity level required to activate this channel.
		Color m_SpewColor;
		char m_Name[MAX_LOGGING_IDENTIFIER_LENGTH];
	};

private:
	// Registered listeners and response policy; pushed/popped to save/restore them.
	struct LoggingState_t
	{
		// Index of the previous entry on the listener set stack.
		int m_nPreviousStackEntry;

		// Number of active listeners in this set.  Cannot exceed MAX_LOGGING_LISTENER_COUNT.
		// If set to -1, implies that this state structure is not in use.
		int m_nListenerCount;
		// Array of registered logging listener objects.
		ILoggingListener *m_RegisteredListeners[MAX_LOGGING_LISTENER_COUNT];

		// Specific policy class to determine behavior of logging system under specific message types.
		ILoggingResponsePolicy *m_pLoggingResponse;
	};

	LoggingState_t *GetCurrentState();

	int FindUnusedStateIndex();

	int m_nChannelCount;
	LoggingChannel_t m_RegisteredChannels[MAX_LOGGING_CHANNEL_COUNT];

	// The index of the current state of the logging system.
	int m_nGlobalStateIndex;

	// A pool of logging states used to store a stack.
	static const int MAX_LOGGING_STATE_COUNT = 16;
	LoggingState_t m_LoggingStates[MAX_LOGGING_STATE_COUNT];

	// Default policy class which determines behavior.
	CDefaultLoggingResponsePolicy m_DefaultLoggingResponse;

	// Default spew function.
	CSimpleLoggingListener m_DefaultLoggingListener;
};

//////////////////////////////////////////////////////////////////////////
// Logging Macros
//////////////////////////////////////////////////////////////////////////

#define InternalMsg( Channel, Severity, /* [Color], Message, */ ... ) do { if ( LoggingSystem_IsChannelEnabled( Channel, Severity ) ) LoggingSystem_Log( Channel, Severity, /* [Color], Message, */ ##__VA_ARGS__ ); } while( 0 )

#define Log_Msg( Channel, /* [Color], Message, */ ... ) InternalMsg( Channel, LS_MESSAGE, /* [Color], Message, */ ##__VA_ARGS__ )
#define Log_Warning( Channel, /* [Color], Message, */ ... ) InternalMsg( Channel, LS_WARNING, /* [Color], Message, */ ##__VA_ARGS__ )
#define Log_Error( Channel, /* [Color], Message, */ ... ) InternalMsg( Channel, LS_ERROR, /* [Color], Message, */ ##__VA_ARGS__ )
#define Log_Assert( Message, ... ) LoggingSystem_LogAssert( Message, ##__VA_ARGS__ )

#define DECLARE_LOGGING_CHANNEL( Channel ) extern LoggingChannelID_t Channel

#define DEFINE_LOGGING_CHANNEL( Channel, ChannelName, /* [Flags], [Severity], [Color] */ ... ) \
	LoggingChannelID_t Channel = LoggingSystem_RegisterLoggingChannel( ChannelName, ##__VA_ARGS__ )

//////////////////////////////////////////////////////////////////////////
// DLL Exports
//////////////////////////////////////////////////////////////////////////

// For documentation on these functions, please look at the corresponding function
// in CLoggingSystem (unless otherwise specified).
PLATFORM_INTERFACE LoggingChannelID_t LoggingSystem_RegisterLoggingChannel( const char *pName, int flags = 0, LoggingSeverity_t severity = LS_MESSAGE, Color color = UNSPECIFIED_LOGGING_COLOR );

PLATFORM_INTERFACE void LoggingSystem_RegisterLoggingListener( ILoggingListener *pListener );
PLATFORM_INTERFACE void LoggingSystem_ResetCurrentLoggingState();
PLATFORM_INTERFACE void LoggingSystem_SetLoggingResponsePolicy( ILoggingResponsePolicy *pResponsePolicy );
PLATFORM_INTERFACE void LoggingSystem_PushLoggingState();
PLATFORM_INTERFACE void LoggingSystem_PopLoggingState();

PLATFORM_INTERFACE bool LoggingSystem_IsChannelEnabled( LoggingChannelID_t channelID, LoggingSeverity_t severity );
PLATFORM_INTERFACE void LoggingSystem_SetChannelSpewLevelByName( const char *pName, LoggingSeverity_t minimumSeverity );

// Logs a variable-argument message to a given channel with the specified severity.
PLATFORM_INTERFACE LoggingResponse_t LoggingSystem_Log( LoggingChannelID_t channelID, LoggingSeverity_t severity, PRINTF_FORMAT_STRING const char *pMessageFormat, ... ) FMTFUNCTION( 3, 4 );
PLATFORM_OVERLOAD LoggingResponse_t LoggingSystem_Log( LoggingChannelID_t channelID, LoggingSeverity_t severity, Color spewColor, PRINTF_FORMAT_STRING const char *pMessageFormat, ... ) FMTFUNCTION( 4, 5 );

PLATFORM_INTERFACE LoggingResponse_t LoggingSystem_LogDirect( LoggingChannelID_t channelID, LoggingSeverity_t severity, Color spewColor, const char *pMessage );
PLATFORM_INTERFACE LoggingResponse_t LoggingSystem_LogAssert( PRINTF_FORMAT_STRING const char *pMessageFormat, ... ) FMTFUNCTION( 1, 2 );


//============ Copyright (c) Valve Corporation, All rights reserved. ============
//
// Logging system definitions.
//
//===============================================================================

#include "pch_tier0.h"
#include "logging.h"
#include "dbg.h"

#include <cstring>

//////////////////////////////////////////////////////////////////////////
// Define commonly used channels here
//////////////////////////////////////////////////////////////////////////
DEFINE_LOGGING_CHANNEL( LOG_GENERAL, "General" );

DEFINE_LOGGING_CHANNEL( LOG_ASSERT, "Assert" );

// Corresponds to ConMsg/ConColorMsg. Only errors are spewed by default.
DEFINE_LOGGING_CHANNEL( LOG_CONSOLE, "Console", LCF_CONSOLE_ONLY, LS_ERROR );

// Corresponds to DevMsg/DevWarning. Only errors are spewed by default.
DEFINE_LOGGING_CHANNEL( LOG_DEVELOPER, "Developer", LCF_CONSOLE_ONLY, LS_ERROR );

//////////////////////////////////////////////////////////////////////////
// Implementation
//////////////////////////////////////////////////////////////////////////

static CLoggingSystem *g_pGlobalLoggingSystem = nullptr;

// Channels are registered during static initialization, so the instance is created on first use.
static CLoggingSystem *GetGlobalLoggingSystem_Internal()
{
	static CLoggingSystem globalLoggingSystem;
	g_pGlobalLoggingSystem = &globalLoggingSystem;
	return &globalLoggingSystem;
}

CLoggingSystem *GetGlobalLoggingSystem()
{
	return ( g_pGlobalLoggingSystem == nullptr) ? GetGlobalLoggingSystem_Internal() : g_pGlobalLoggingSystem;
}

CLoggingSystem::CLoggingSystem() :
	m_nChannelCount( 0 ),
	m_nGlobalStateIndex( 0 )
{
	m_LoggingStates[0].m_nPreviousStackEntry = -1;

	m_LoggingStates[0].m_nListenerCount = 1;
	m_LoggingStates[0].m_RegisteredListeners[0] = &m_DefaultLoggingListener;
	m_LoggingStates[0].m_pLoggingResponse = &m_DefaultLoggingResponse;

	// Mark all other logging state blocks as unused.
	for ( int i = 1; i < MAX_LOGGING_STATE_COUNT; ++ i )
	{
		m_LoggingStates[i].m_nListenerCount = -1;
	}
}

CLoggingSystem::~CLoggingSystem()
{
}

LoggingChannelID_t CLoggingSystem::RegisterLoggingChannel( const char *pChannelName, int flags, LoggingSeverity_t severity, Color spewColor )
{
	if ( m_nChannelCount >= MAX_LOGGING_CHANNEL_COUNT )
	{
		// Out of logging channels... catastrophic fail!
		Log_Error( LOG_GENERAL, "Out of logging channels.\n" );
		Assert( 0 );
		return INVALID_LOGGING_CHANNEL_ID;
	}

	// Channels can be multiply defined, in which case return the ID of the existing channel.
	for ( int i = 0; i < m_nChannelCount; ++ i )
	{
		if ( V_tier0_stricmp( m_RegisteredChannels[i].m_Name, pChannelName ) == 0 )
		{
			// If a logging channel is registered multiple times, only one of the registrations should specify flags/severity/color.
			if ( m_RegisteredChannels[i].m_Flags == 0 && m_RegisteredChannels[i].m_MinimumSeverity == LS_MESSAGE && m_RegisteredChannels[i].m_SpewColor == UNSPECIFIED_LOGGING_COLOR )
			{
				m_RegisteredChannels[i].m_Flags = ( LoggingChannelFlags_t )flags;
				m_RegisteredChannels[i].m_MinimumSeverity = severity;
				m_RegisteredChannels[i].m_SpewColor = spewColor;
			}
			else
			{
				AssertMsg( flags == 0 || flags == m_RegisteredChannels[i].m_Flags, "Non-zero or mismatched flags specified in logging channel re-registration!" );
				AssertMsg( severity == LS_MESSAGE || severity == m_RegisteredChannels[i].m_MinimumSeverity, "Non-default or mismatched severity specified in logging channel re-registration!" );
				AssertMsg( spewColor == UNSPECIFIED_LOGGING_COLOR || spewColor == m_RegisteredChannels[i].m_SpewColor, "Non-default or mismatched color specified in logging channel re-registration!" );
			}

			return m_RegisteredChannels[i].m_ID;
		}
	}

	LoggingChannel_t &channel = m_RegisteredChannels[m_nChannelCount];
	channel.m_ID = m_nChannelCount;
	channel.m_Flags = ( LoggingChannelFlags_t )flags;
	channel.m_MinimumSeverity = severity;
	channel.m_SpewColor = spewColor;
	strncpy( channel.m_Name, pChannelName, MAX_LOGGING_IDENTIFIER_LENGTH );
	return m_nChannelCount ++;
}

void CLoggingSystem::SetChannelSpewLevelByName( const char *pName, LoggingSeverity_t minimumSeverity )
{
	for ( int i = 0; i < m_nChannelCount; ++ i )
	{
		if ( V_tier0_stricmp( m_RegisteredChannels[i].m_Name, pName ) == 0 )
		{
			m_RegisteredChannels[i].m_MinimumSeverity = minimumSeverity;
		}
	}
}

void CLoggingSystem::PushLoggingState()
{
	int nNewState = FindUnusedStateIndex();
	// Ensure we're not out of state blocks.
	Assert( nNewState != -1 );

	m_LoggingStates[nNewState].m_nListenerCount = 0;
	m_LoggingStates[nNewState].m_pLoggingResponse = &m_DefaultLoggingResponse;
	m_LoggingStates[nNewState].m_nPreviousStackEntry = m_nGlobalStateIndex;

	m_nGlobalStateIndex = nNewState;
}

void CLoggingSystem::PopLoggingState()
{
	int nCurrentState = m_nGlobalStateIndex;

	// Shouldn't be less than 0 (implies error during Push()) or 0 (implies that Push() was never called)
	Assert( nCurrentState > 0 );

	// Mark the current state as unused.
	m_LoggingStates[nCurrentState].m_nListenerCount = -1;
	m_nGlobalStateIndex = m_LoggingStates[nCurrentState].m_nPreviousStackEntry;
}

void CLoggingSystem::RegisterLoggingListener( ILoggingListener *pListener )
{
	LoggingState_t *pState = GetCurrentState();
	if ( pState->m_nListenerCount >= ARRAYSIZE(pState->m_RegisteredListeners) )
	{
		// Out of logging listener slots... catastrophic fail!
		Assert( 0 );
	}
	else
	{
		pState->m_RegisteredListeners[pState->m_nListenerCount] = pListener;
		++ pState->m_nListenerCount;
	}
}

void CLoggingSystem::ResetCurrentLoggingState()
{
	LoggingState_t *pState = GetCurrentState();
	pState->m_nListenerCount = 0;
	pState->m_pLoggingResponse = &m_DefaultLoggingResponse;
}

void CLoggingSystem::SetLoggingResponsePolicy( ILoggingResponsePolicy *pLoggingResponse )
{
	LoggingState_t *pState = GetCurrentState();
	pState->m_pLoggingResponse = pLoggingResponse ? pLoggingResponse : &m_DefaultLoggingResponse;
}

LoggingResponse_t CLoggingSystem::LogDirect( LoggingChannelID_t channelID, LoggingSeverity_t severity, Color color, const tchar *pMessage )
{
	Assert( IsValidChannelID( channelID ) );
	if ( !IsValidChannelID( channelID ) )
		return LR_CONTINUE;

	LoggingContext_t context;
	context.m_ChannelID = channelID;
	context.m_Flags = m_RegisteredChannels[channelID].m_Flags;
	context.m_Severity = severity;
	context.m_Color = ( color == UNSPECIFIED_LOGGING_COLOR ) ? m_RegisteredChannels[channelID].m_SpewColor : color;

	LoggingState_t *pState = GetCurrentState();

	for ( int i = 0; i < pState->m_nListenerCount; ++ i )
	{
		pState->m_RegisteredListeners[i]->Log( &context, pMessage );
	}

	LoggingResponse_t response = pState->m_pLoggingResponse->OnLog( &context );

	switch( response )
	{
	case LR_DEBUGGER:
		// Asserts put the debug break in the macro itself so the code breaks at the failure point.
		if ( severity != LS_ASSERT )
		{
			DebuggerBreakIfDebugging();
		}
		break;

	case LR_ABORT:
		Plat_ExitProcess( EXIT_FAILURE );
		break;

	default:
		break;
	}

	return response;
}

CLoggingSystem::LoggingState_t *CLoggingSystem::GetCurrentState()
{
	Assert( m_nGlobalStateIndex >= 0 && m_nGlobalStateIndex < MAX_LOGGING_STATE_COUNT );
	return &m_LoggingStates[m_nGlobalStateIndex];
}

int CLoggingSystem::FindUnusedStateIndex()
{
	for ( int i = 0; i < MAX_LOGGING_STATE_COUNT; ++ i )
	{
		if ( m_LoggingStates[i].m_nListenerCount < 0 )
		{
			return i;
		}
	}
	return -1;
}

LoggingChannelID_t LoggingSystem_RegisterLoggingChannel( const char *pName, int flags, LoggingSeverity_t severity, Color color )
{
	return GetGlobalLoggingSystem()->RegisterLoggingChannel( pName, flags, severity, color );
}

void LoggingSystem_ResetCurrentLoggingState()
{
	GetGlobalLoggingSystem()->ResetCurrentLoggingState();
}

void LoggingSystem_RegisterLoggingListener( ILoggingListener *pListener )
{
	GetGlobalLoggingSystem()->RegisterLoggingListener( pListener );
}

void LoggingSystem_SetLoggingResponsePolicy( ILoggingResponsePolicy *pResponsePolicy )
{
	GetGlobalLoggingSystem()->SetLoggingResponsePolicy( pResponsePolicy );
}

void LoggingSystem_PushLoggingState()
{
	GetGlobalLoggingSystem()->PushLoggingState();
}

void LoggingSystem_PopLoggingState()
{
	GetGlobalLoggingSystem()->PopLoggingState();
}

bool LoggingSystem_IsChannelEnabled( LoggingChannelID_t channelID, LoggingSeverity_t severity )
{
	return GetGlobalLoggingSystem()->IsChannelEnabled( channelID, severity );
}

void LoggingSystem_SetChannelSpewLevelByName( const char *pName, LoggingSeverity_t minimumSeverity )
{
	GetGlobalLoggingSystem()->SetChannelSpewLevelByName( pName, minimumSeverity );
}

LoggingResponse_t LoggingSystem_Log( LoggingChannelID_t channelID, LoggingSeverity_t severity, const char *pMessageFormat, ... )
{
	if ( !GetGlobalLoggingSystem()->IsChannelEnabled( channelID, severity ) )
		return LR_CONTINUE;

	tchar formattedMessage[MAX_LOGGING_MESSAGE_LENGTH];

	va_list args;
	va_start( args, pMessageFormat );
	Tier0Internal_vsntprintf( formattedMessage, MAX_LOGGING_MESSAGE_LENGTH, pMessageFormat, args );
	va_end( args );

	return GetGlobalLoggingSystem()->LogDirect( channelID, severity, UNSPECIFIED_LOGGING_COLOR, formattedMessage );
}

LoggingResponse_t LoggingSystem_Log( LoggingChannelID_t channelID, LoggingSeverity_t severity, Color spewColor, const char *pMessageFormat, ... )
{
	if ( !GetGlobalLoggingSystem()->IsChannelEnabled( channelID, severity ) )
		return LR_CONTINUE;

	tchar formattedMessage[MAX_LOGGING_MESSAGE_LENGTH];

	va_list args;
	va_start( args, pMessageFormat );
	Tier0Internal_vsntprintf( formattedMessage, MAX_LOGGING_MESSAGE_LENGTH, pMessageFormat, args );
	va_end( args );

	return GetGlobalLoggingSystem()->LogDirect( channelID, severity, spewColor, formattedMessage );
}

LoggingResponse_t LoggingSystem_LogDirect( LoggingChannelID_t channelID, LoggingSeverity_t severity, Color spewColor, const char *pMessage )
{
	if ( !GetGlobalLoggingSystem()->IsChannelEnabled( channelID, severity ) )
		return LR_CONTINUE;
	return GetGlobalLoggingSystem()->LogDirect( channelID, severity, spewColor, pMessage );
}

LoggingResponse_t LoggingSystem_LogAssert( const char *pMessageFormat, ... )
{
	if ( !GetGlobalLoggingSystem()->IsChannelEnabled( LOG_ASSERT, LS_ASSERT ) )
		return LR_CONTINUE;

	tchar formattedMessage[MAX_LOGGING_MESSAGE_LENGTH];

	va_list args;
	va_start( args, pMessageFormat );
	Tier0Internal_vsntprintf( formattedMessage, MAX_LOGGING_MESSAGE_LENGTH, pMessageFormat, args );
	va_end( args );

	return GetGlobalLoggingSystem()->LogDirect( LOG_ASSERT, LS_ASSERT, UNSPECIFIED_LOGGING_COLOR, formattedMessage );
}

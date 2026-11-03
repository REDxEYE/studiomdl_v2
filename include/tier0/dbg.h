//===== Copyright (c) Valve Corporation, All rights reserved. ========//
//
// Purpose: Asserts and the legacy Msg/Warning/Error logging front end.
//
//====================================================================//
#pragma once

#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#include "tier0/basetypes.h"
#include "logging.h"
#include <cmath>
#include <cstdio>
#include <cstdarg>

class Color;

//-----------------------------------------------------------------------------
// Assertions. Compiled out unless DBGFLAG_ASSERT / DBGFLAG_ASSERTFATAL is defined.
//
//   Assert( (f == 5) );
//   AssertMsg( (f == 5), ("F needs to be %d here!\n", 5) );
//   AssertFatal( p );   // exits the application when enabled
//-----------------------------------------------------------------------------

PLATFORM_INTERFACE void _ExitOnFatalAssert( const tchar* pFile, int line );

/* Used to define macros, never use these directly. */
#define  _AssertMsg( _exp, _msg, _executeExp, _bFatal )	\
	do {																\
		if (!(_exp)) 													\
		{ 																\
			LoggingResponse_t ret = Log_Assert( "%s (%d) : %s\n", __TFILE__, __LINE__, static_cast<const char*>( _msg ) );	\
			_executeExp; 												\
			if ( ret == LR_DEBUGGER )									\
			{															\
				DebuggerBreakIfDebugging();								\
				if ( _bFatal )											\
					_ExitOnFatalAssert( __TFILE__, __LINE__ );			\
			}															\
		}																\
	} while (0)

#define  _AssertMsgOnce( _exp, _msg, _bFatal ) \
	do {																\
		static bool fAsserted;											\
		if (!fAsserted )												\
		{ 																\
			_AssertMsg( _exp, _msg, (fAsserted = true), _bFatal );		\
		}																\
	} while (0)

// AssertFatal is used to detect an unrecoverable error condition and terminates the application.
#ifdef DBGFLAG_ASSERTFATAL
#define  AssertFatal( _exp )									_AssertMsg( _exp, _T("Assertion Failed: ") _T(#_exp), ((void)0), true )
#else
#define  AssertFatal( _exp )									((void)0)
#endif

// Assert is used to detect an important but survivable error.
#ifdef DBGFLAG_ASSERT

#define  Assert( _exp )           							_AssertMsg( _exp, _T("Assertion Failed: ") _T(#_exp), ((void)0), false )
#define  AssertMsg_( _exp, _msg )  							_AssertMsg( _exp, _msg, ((void)0), false )
#define  AssertOnce( _exp )       							_AssertMsgOnce( _exp, _T("Assertion Failed: ") _T(#_exp), false )
#define  AssertMsgOnce( _exp, _msg )  						_AssertMsgOnce( _exp, _msg, false )
#define  Verify( _exp )           							( _exp )
#define  VerifyEquals( _exp, _expectedValue )           	AssertMsg2( (_exp) == (_expectedValue), _T("Expected %d but got %d!"), (_expectedValue), (_exp) )
#define  AssertAlignedWidth( adr, width )					Assert( ( ( ( intptr_t ) ( adr ) ) & ( width - 1 ) ) == 0 )
#define  AssertAligned( adr )								AssertAlignedWidth( adr, 16 )

#define  AssertMsg(  _exp, _msg )  										AssertMsg_( _exp, _T( _msg ) )
#define  AssertMsg1( _exp, _msg, a1 )									AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1 )) )
#define  AssertMsg2( _exp, _msg, a1, a2 )								AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2 )) )
#define  AssertMsg3( _exp, _msg, a1, a2, a3 )							AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3 )) )
#define  AssertMsg4( _exp, _msg, a1, a2, a3, a4 )						AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3, a4 )) )
#define  AssertMsg5( _exp, _msg, a1, a2, a3, a4, a5 )					AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3, a4, a5 )) )
#define  AssertMsg6( _exp, _msg, a1, a2, a3, a4, a5, a6 )				AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3, a4, a5, a6 )) )
#define  AssertMsg7( _exp, _msg, a1, a2, a3, a4, a5, a6, a7 )			AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3, a4, a5, a6, a7 )) )
#define  AssertMsg8( _exp, _msg, a1, a2, a3, a4, a5, a6, a7, a8 )		AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3, a4, a5, a6, a7, a8 )) )
#define  AssertMsg9( _exp, _msg, a1, a2, a3, a4, a5, a6, a7, a8, a9 )	AssertMsg_( _exp, (const tchar *)(CDbgFmtMsg( _T( _msg ), a1, a2, a3, a4, a5, a6, a7, a8, a9 )) )

#else // DBGFLAG_ASSERT

#define  Assert( _exp )										((void)0)
#define  AssertOnce( _exp )									((void)0)
#define  AssertMsg( _exp, _msg )							((void)0)
#define  AssertMsgOnce( _exp, _msg )						((void)0)
#define  Verify( _exp )										(_exp)
#define  VerifyEquals( _exp, _expectedValue )           	(_exp)
#define  AssertAlignedWidth( adr, width )
#define  AssertAligned( adr )

#define  AssertMsg1( _exp, _msg, a1 )									((void)0)
#define  AssertMsg2( _exp, _msg, a1, a2 )								((void)0)
#define  AssertMsg3( _exp, _msg, a1, a2, a3 )							((void)0)
#define  AssertMsg4( _exp, _msg, a1, a2, a3, a4 )						((void)0)
#define  AssertMsg5( _exp, _msg, a1, a2, a3, a4, a5 )					((void)0)
#define  AssertMsg6( _exp, _msg, a1, a2, a3, a4, a5, a6 )				((void)0)
#define  AssertMsg7( _exp, _msg, a1, a2, a3, a4, a5, a6, a7 )			((void)0)
#define  AssertMsg8( _exp, _msg, a1, a2, a3, a4, a5, a6, a7, a8 )		((void)0)
#define  AssertMsg9( _exp, _msg, a1, a2, a3, a4, a5, a6, a7, a8, a9 )	((void)0)

#endif // DBGFLAG_ASSERT

#if defined( DBGFLAG_ASSERT ) && defined( _DEBUG )
#define DbgAssert( _exp )	Assert( _exp )
#else
#define DbgAssert( _exp )	((void)0)
#endif

// Source2 compatibility macro
#define AssertDbg( X ) DbgAssert( X )

#define STRINGIFY_INTERNAL(x) #x
#define STRINGIFY(x) STRINGIFY_INTERNAL(x)

//////////////////////////////////////////////////////////////////////////
// Legacy Logging System
//////////////////////////////////////////////////////////////////////////

// Channels which map the legacy logging system to the new system.

// Channel for all default Msg/Warning/Error commands.
PLATFORM_INTERFACE LoggingChannelID_t LOG_GENERAL;

// Channel for all asserts.
DECLARE_LOGGING_CHANNEL( LOG_ASSERT );
// Channel for all ConMsg and ConColorMsg commands.
DECLARE_LOGGING_CHANNEL( LOG_CONSOLE );
// Channel for all DevMsg and DevWarning commands.
DECLARE_LOGGING_CHANNEL( LOG_DEVELOPER );

// Legacy logging functions

// This function does not return.
PLATFORM_INTERFACE void Error( PRINTF_FORMAT_STRING const tchar *pMsg, ... ) FMTFUNCTION( 1, 2 );
#define Plat_FatalError( ... ) do { Log_Error( LOG_GENERAL, ##__VA_ARGS__ ); Plat_ExitProcess( EXIT_FAILURE ); } while( 0 )

PLATFORM_INTERFACE void Msg( PRINTF_FORMAT_STRING const tchar* pMsg, ... ) FMTFUNCTION( 1, 2 );
PLATFORM_INTERFACE void Warning( PRINTF_FORMAT_STRING const tchar *pMsg, ... ) FMTFUNCTION( 1, 2 );

PLATFORM_OVERLOAD void DevMsg( PRINTF_FORMAT_STRING const tchar* pMsg, ... ) FMTFUNCTION( 1, 2 );
PLATFORM_OVERLOAD void DevWarning( PRINTF_FORMAT_STRING const tchar *pMsg, ... ) FMTFUNCTION( 1, 2 );

PLATFORM_OVERLOAD void ConColorMsg( const Color& clr, PRINTF_FORMAT_STRING const tchar* pMsg, ... ) FMTFUNCTION( 2, 3 );
PLATFORM_OVERLOAD void ConMsg( PRINTF_FORMAT_STRING const tchar* pMsg, ... ) FMTFUNCTION( 1, 2 );

// You can use this macro like a runtime assert macro.
// If the condition fails, then Error is called with the message. This macro is called
// like AssertMsg, where msg must be enclosed in parenthesis:
//
// ErrorIfNot( bCondition, ("a b c %d %d %d", 1, 2, 3) );
#define ErrorIfNot( condition, msg ) \
	if ( (condition) )		\
		;					\
	else 					\
	{						\
		Error msg;			\
	}

#ifdef _DEBUG
template<typename DEST_POINTER_TYPE, typename SOURCE_POINTER_TYPE>
inline DEST_POINTER_TYPE assert_cast(SOURCE_POINTER_TYPE* pSource)
{
    Assert( static_cast<DEST_POINTER_TYPE>(pSource) == dynamic_cast<DEST_POINTER_TYPE>(pSource) );
    return static_cast<DEST_POINTER_TYPE>(pSource);
}
#else
#define assert_cast static_cast
#endif

//-----------------------------------------------------------------------------
// Templates to assist in validating pointers:

// Have to use these stubs so we don't have to include windows.h here.
PLATFORM_INTERFACE void _AssertValidReadPtr( void* ptr, int count = 1 );
PLATFORM_INTERFACE void _AssertValidWritePtr( void* ptr, int count = 1 );
PLATFORM_INTERFACE void _AssertValidStringPtr( const tchar* ptr, int maxchar );

#ifdef DBGFLAG_ASSERT
inline void AssertValidStringPtr( const tchar* ptr, int maxchar = 0xFFFFFF )		 { _AssertValidStringPtr( ptr, maxchar ); }
template<class T> inline void AssertValidReadPtr( T* ptr, int count = 1 )		     { _AssertValidReadPtr( (void*)ptr, count ); }
template<class T> inline void AssertValidWritePtr( T* ptr, int count = 1 )		     { _AssertValidWritePtr( (void*)ptr, count ); }
#else
inline void AssertValidStringPtr( const tchar* ptr, int maxchar = 0xFFFFFF ) 		 {	}
template<class T> inline void AssertValidReadPtr( T* ptr, int count = 1 )		     {  }
template<class T> inline void AssertValidWritePtr( T* ptr, int count = 1 )		     {  }
#endif

//-----------------------------------------------------------------------------
// Macro to protect functions that are not reentrant

#ifdef _DEBUG
class CReentryGuard
{
public:
	CReentryGuard(int *pSemaphore)
	 : m_pSemaphore(pSemaphore)
	{
		++(*m_pSemaphore);
	}

	~CReentryGuard()
	{
		--(*m_pSemaphore);
	}

private:
	int *m_pSemaphore;
};

#define ASSERT_NO_REENTRY() \
	static int fSemaphore##__LINE__; \
	Assert( !fSemaphore##__LINE__ ); \
	CReentryGuard ReentryGuard##__LINE__( &fSemaphore##__LINE__ )
#else
#define ASSERT_NO_REENTRY()
#endif

// Tier0 uses these for string functions since this abstraction is normally done in tier1.
#ifdef POSIX
	#define Tier0Internal_sntprintf  snprintf
	#define Tier0Internal_vsntprintf vsnprintf
	#define Tier0Internal_vsnprintf  vsnprintf
#else
	#define Tier0Internal_sntprintf  _sntprintf
	#define Tier0Internal_vsntprintf _vsntprintf
	#define Tier0Internal_vsnprintf  _vsnprintf
#endif

//-----------------------------------------------------------------------------
// Purpose: Inline string formatter for AssertMsgN
//-----------------------------------------------------------------------------
class CDbgFmtMsg
{
public:
	CDbgFmtMsg(PRINTF_FORMAT_STRING const tchar *pszFormat, ...)
	{
		va_list arg_ptr;

		va_start(arg_ptr, pszFormat);
		Tier0Internal_vsntprintf(m_szBuf, sizeof(m_szBuf)-1, pszFormat, arg_ptr);
		va_end(arg_ptr);

		m_szBuf[sizeof(m_szBuf)-1] = 0;
	}

	operator const tchar *() const
	{
		return m_szBuf;
	}

private:
	tchar m_szBuf[256];
};


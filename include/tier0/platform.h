//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose:
//
// $NoKeywords: $ 
//
//===========================================================================//

#pragma once

#if defined(__x86_64__) || defined(_WIN64)
#define PLATFORM_64BITS 1
#endif
#include <cstdarg>

#ifdef LINUX
#pragma GCC diagnostic ignored "-Wconversion-null"	// passing NULL to non-pointer argument 1
#pragma GCC diagnostic ignored "-Wpointer-arith"	// NULL used in arithmetic. Ie, vpanel == NULL where VPANEL is uint.
#endif


#ifdef __GCC__
#define COMPILER_GCC 1
#endif



#ifdef COMPILER_MSVC
#pragma once
#endif


#include "wchartypes.h"
#include "tier0/valve_off.h"


	#include <malloc.h>
	#include <memory.h>
	#include <limits.h>
	#include <float.h>
	#include <stdlib.h>
	#include <string.h>
#if defined( OSX ) || defined ( _LINUX )
	#include <signal.h>
	#include <stdarg.h>
#endif


#ifdef __cplusplus
#if defined( COMPILER_GCC )
	#include <new>
#else
	#include <new.h>
#endif
#endif

//-----------------------------------------------------------------------------
// Old-school defines we don't want to use moving forward
//-----------------------------------------------------------------------------
#if CROSS_PLATFORM_VERSION < 1

#if _MSC_VER >= 1800
#define	VECTORCALL __vectorcall 
#else 
#define	VECTORCALL 
#endif

#endif // CROSS_PLATFORM_VERSION < 1

#if defined(_STATIC_LINKED)
#include "staticlink/system.h"
#endif

//-----------------------------------------------------------------------------
// NOTE: All compiler defines are set up in the base VPC scripts
// COMPILER_MSVC, COMPILER_MSVC32, COMPILER_MSVC64, COMPILER_MSVCX360
// COMPILER_GCC
// The rationale for this is that we need COMPILER_MSVC for the pragma blocks
// #pragma once that occur at the top of all header files, therefore we can't
// place the defines for these in here.
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Set up platform defines.
//-----------------------------------------------------------------------------
#ifdef _WIN32
	#define IsPlatformLinux()	0
	#define IsPlatformPosix()	0
	#define IsPlatformOSX()		0
	#define PLATFORM_WINDOWS	1

		#define IsPlatformWindowsPC() 1
		#define PLATFORM_WINDOWS_PC 1

		#ifdef _WIN64
			#define IsPlatformWindowsPC64() 1
			#define PLATFORM_WINDOWS_PC64 1
		#else
			#define IsPlatformWindowsPC64() 0
		#endif

#elif defined(POSIX)
	#define IsPlatformWindowsPC()	0
	#define IsPlatformWindowsPC64()	0
	#define IsPlatformPosix()		1
	#define PLATFORM_POSIX 1

	#if defined( LINUX ) && !defined( OSX ) // for havok we define both symbols, so don't let the osx build wander down here
		#define IsPlatformLinux() 1
		#define IsPlatformOSX() 0
		#define PLATFORM_LINUX 1
	#elif defined ( OSX )
		#define IsPlatformLinux() 0
		#define IsPlatformOSX() 1
	#else
		#define IsPlatformLinux() 0
		#define IsPlatformOSX() 0
	#endif

#else
	#error
#endif

//-----------------------------------------------------------------------------
// Old-school defines we're going to support since much code uses them
//-----------------------------------------------------------------------------
#if CROSS_PLATFORM_VERSION < 2

#define IsLinux()	IsPlatformLinux() 
#define IsPosix()	IsPlatformPosix()

// Setup platform defines.
#ifdef COMPILER_MSVC
#define MSVC 1
#endif

#ifdef COMPILER_GCC
#define GNUC 1
#endif

#if defined( _WIN32 )
#define _WINDOWS 1
#endif

#ifdef PLATFORM_WINDOWS_PC
#define IS_WINDOWS_PC 1
#endif

#endif // CROSS_PLATFORM_VERSION < 2

//-----------------------------------------------------------------------------
// Set up platform type defines.
//-----------------------------------------------------------------------------
	#define IsPC()		1



//-----------------------------------------------------------------------------
// Set up build configuration defines.
//-----------------------------------------------------------------------------
#ifdef _DEBUG
#define IsDebug() 1
#else
#define IsDebug() 0
#endif


//-----------------------------------------------------------------------------
// Portable data types: fixed-width integers are the <cstdint> ones (int32_t, uint8_t, intptr_t, ...)
//-----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

#if !defined( COMPILER_MSVC )
	typedef void *HWND;
#endif



typedef float				float32;
typedef double				float64;


#ifdef PLATFORM_POSIX
typedef void * HINSTANCE;
#define _MAX_PATH PATH_MAX
#endif

// MSVC CRT uses 0x7fff while gcc uses MAX_INT, leading to mismatches between platforms
// As a result, we pick the least common denominator here.  This should be used anywhere
// you might typically want to use RAND_MAX
#define VALVE_RAND_MAX 0x7fff

#ifdef GNUC
#undef offsetof
// Note: can't use builtin offsetof because many use cases (esp. in templates) wouldn't compile due to restrictions on the builtin offsetof
//#define offsetof( type, var ) __builtin_offsetof( type, var ) 
#define offsetof(s,m)	( (size_t)&(((s *)0x1000000)->m) - 0x1000000u )
#else
#include <stddef.h>
#undef offsetof
#define offsetof(s,m)	(size_t)&(((s *)0)->m)
#endif



//-----------------------------------------------------------------------------
// Long is evil because it's treated differently by different compilers
// Preventing its use is nasty however. This #define, which should be
// turned on in individual VPC files, causes you to include tier0/valve_off.h
// before standard C + windows headers, and include tier0/valve_on.h after
// standard C + windows headers. So, there's some painful overhead to disabling long
//-----------------------------------------------------------------------------
#ifdef DISALLOW_USE_OF_LONG
	#define long			long_is_the_devil_stop_using_it_use_int32_or_int64
#endif


//-----------------------------------------------------------------------------
// Various compiler-specific keywords
//-----------------------------------------------------------------------------
#ifdef COMPILER_MSVC

	#ifdef FORCEINLINE
		#undef FORCEINLINE
	#endif
	#ifndef FASTCALL
		#define  FASTCALL			__fastcall
	#endif
	#define FORCEINLINE				__forceinline
	#define FORCEINLINE_TEMPLATE	__forceinline
	#define NULLTERMINATED			__nullterminated

	#define NO_VTABLE				__declspec( novtable )

	// Used for dll exporting and importing
	#define DLL_EXPORT				extern "C" __declspec( dllexport )
	#define DLL_IMPORT				extern "C" //__declspec( dllimport )
//    #define DLL_IMPORT

	// Can't use extern "C" when DLL exporting a class
	#define DLL_CLASS_EXPORT		__declspec( dllexport )
	#define DLL_CLASS_IMPORT		__declspec( dllimport )
//	#define DLL_CLASS_IMPORT

	// Can't use extern "C" when DLL exporting a global
	#define DLL_GLOBAL_EXPORT		extern __declspec( dllexport )
	#define DLL_GLOBAL_IMPORT		extern __declspec( dllimport )
//	#define DLL_GLOBAL_IMPORT       extern

	// decls for aligning data
	#define DECL_ALIGN(x)			__declspec( align( x ) )

	#define RESTRICT				__restrict
	#define FMTFUNCTION( a, b )



#elif defined ( COMPILER_GCC ) || defined( COMPILER_SNC )

	#if defined( PLATFORM_64BITS )
		#define  __stdcall
	#else
		#define  __stdcall			__attribute__ ((__stdcall__))
	#endif

	#define  FASTCALL
	#ifdef _LINUX_DEBUGGABLE
		#define  FORCEINLINE
	#else
			#define  FORCEINLINE inline __attribute__ ((always_inline))
	#endif

	// GCC 3.4.1 has a bug in supporting forced inline of templated functions
	// this macro lets us not force inlining in that case
	#define FORCEINLINE_TEMPLATE	inline
	#define NO_VTABLE

	#define NULLTERMINATED			

	// Used for dll exporting and importing
	#define DLL_DECLARATION_DEFAULT_VISIBILITY __attribute__ ((visibility("default")))
	#define DLL_EXPORT				extern "C" DLL_DECLARATION_DEFAULT_VISIBILITY 
	#define DLL_IMPORT				extern "C" 

	// Can't use extern "C" when DLL exporting a class
	#define DLL_CLASS_EXPORT		DLL_DECLARATION_DEFAULT_VISIBILITY 
	#define DLL_CLASS_IMPORT

	// Can't use extern "C" when DLL exporting a global
	#define DLL_GLOBAL_EXPORT		DLL_DECLARATION_DEFAULT_VISIBILITY 
	#define DLL_GLOBAL_IMPORT		extern

	#define DECL_ALIGN(x)			__attribute__( ( aligned( x ) ) )
#if defined(__clang__)
	// [will] - clang is very strict about restrict, and we have a bunch of core functions that use the keyword which have issues with it.
	// This seemed to be a cleaner solution for now so we don't have to fill core code with tons of #ifdefs.
	#define RESTRICT
#else
	#define RESTRICT				__restrict__
#endif
	#define FMTFUNCTION( fmtargnumber, firstvarargnumber ) __attribute__ (( format( printf, fmtargnumber, firstvarargnumber )))


#else

	#define DECL_ALIGN(x)			/* */

#endif

#if defined( GNUC ) // use pre-align on PS3
// gnuc has the align decoration at the end
#define ALIGN4
#define ALIGN8 
#define ALIGN16
#define ALIGN32

#undef ALIGN16_POST
#define ALIGN4_POST DECL_ALIGN(4)
#define ALIGN8_POST DECL_ALIGN(8)
#define ALIGN16_POST DECL_ALIGN(16)
#else
// MSVC has the align at the start of the struct
// PS3 SNC supports both
#define ALIGN4 DECL_ALIGN(4)
#define ALIGN8 DECL_ALIGN(8)
#define ALIGN16 DECL_ALIGN(16)
#define ALIGN32 DECL_ALIGN(32)

#define ALIGN4_POST
#define ALIGN8_POST
#define ALIGN16_POST
#endif


//-----------------------------------------------------------------------------
// Macro to assist in asserting constant invariants during compilation

// This implementation of compile time assert has zero cost (so it can safely be
// included in release builds) and can be used at file scope or function scope.
#ifdef __GNUC__
       #define COMPILE_TIME_ASSERT( pred ) typedef int UNIQUE_ID[ (pred) ? 1 : -1 ]
#else
       #if _MSC_VER >= 1600
       // If available use static_assert instead of weird language tricks. This
       // leads to much more readable messages when compile time assert constraints
       // are violated.
       #define COMPILE_TIME_ASSERT( pred ) static_assert( pred, "Compile time assert constraint is not true: " #pred )
       #else
       // Due to gcc bugs this can in rare cases (some template functions) cause redeclaration
       // errors when used multiple times in one scope. Fix by adding extra scoping.
       #define COMPILE_TIME_ASSERT( pred ) typedef char compile_time_assert_type[(pred) ? 1 : -1];
       #endif
#endif
// ASSERT_INVARIANT used to be needed in order to allow COMPILE_TIME_ASSERTs at global
// scope. However the new COMPILE_TIME_ASSERT macro supports that by default.
#define ASSERT_INVARIANT( pred )	COMPILE_TIME_ASSERT( pred )


// This can be used to declare an abstract (interface only) class.
// Classes marked abstract should not be instantiated.  If they are, and access violation will occur.
//
// Example of use:
//
// abstract_class CFoo
// {
//      ...
// }
//
// MSDN __declspec(novtable) documentation: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/vclang/html/_langref_novtable.asp
//
// Note: NJS: This is not enabled for regular PC, due to not knowing the implications of exporting a class with no no vtable.
//       It's probable that this shouldn't be an issue, but an experiment should be done to verify this.
//
	#define abstract_class class


//-----------------------------------------------------------------------------
// Disable annoying unhelpful warnings
//-----------------------------------------------------------------------------
#ifdef COMPILER_MSVC
// Remove warnings from warning level 4.
#pragma warning(disable : 4514) // warning C4514: 'acosl' : unreferenced inline function has been removed
#pragma warning(disable : 4100) // warning C4100: 'hwnd' : unreferenced formal parameter
#pragma warning(disable : 4127) // warning C4127: conditional expression is constant
#pragma warning(disable : 4512) // warning C4512: 'InFileRIFF' : assignment operator could not be generated
#pragma warning(disable : 4611) // warning C4611: interaction between '_setjmp' and C++ object destruction is non-portable
#pragma warning(disable : 4710) // warning C4710: function 'x' not inlined
#pragma warning(disable : 4702) // warning C4702: unreachable code
#pragma warning(disable : 4505) // unreferenced local function has been removed
#pragma warning(disable : 4239) // nonstandard extension used : 'argument' ( conversion from class Vector to class Vector& )
#pragma warning(disable : 4097) // typedef-name 'BaseClass' used as synonym for class-name 'CFlexCycler::CBaseFlex'
#pragma warning(disable : 4324) // Padding was added at the end of a structure
#pragma warning(disable : 4244) // type conversion warning.
#pragma warning(disable : 4305)	// truncation from 'const double ' to 'float '
#pragma warning(disable : 4786)	// Disable warnings about long symbol names
#pragma warning(disable : 4250) // 'X' : inherits 'Y::Z' via dominance
#pragma warning(disable : 4201) // nonstandard extension used : nameless struct/union
#pragma warning(disable : 4481) // warning C4481: nonstandard extension used: override specifier 'override'

#if _MSC_VER >= 1300
#pragma warning(disable : 4511)	// Disable warnings about private copy constructors
#pragma warning(disable : 4121)	// warning C4121: 'symbol' : alignment of a member was sensitive to packing
#pragma warning(disable : 4530)	// warning C4530: C++ exception handler used, but unwind semantics are not enabled. Specify /EHsc (disabled due to std headers having exception syntax)
#endif

#if _MSC_VER >= 1400
#pragma warning(disable : 4996)	// functions declared deprecated
#endif

// When we port to 64 bit, we'll have to resolve the int, ptr vs size_t 32/64 bit problems...
#if !defined( COMPILER_MSVC64 )
#if ( CROSS_PLATFORM_VERSION < 1 )
#pragma warning( disable : 4267 )	// conversion from 'size_t' to 'int', possible loss of data
#pragma warning( disable : 4311 )	// pointer truncation from 'char *' to 'int'
#pragma warning( disable : 4312 )	// conversion from 'unsigned int' to 'memhandle_t' of greater size
#endif
#endif
#elif defined( COMPILER_SNC )
#pragma diag_suppress=1700	   // warning 1700: class "%s" has virtual functions but non-virtual destructor
// Uncomment the following line if you want to investigate a specific compiler remark without all the noise:
// #pragma diag_suppress=1700, 83, 162, 182, 192, 194, 229, 238, 262, 341, 382, 401, 402, 403, 481, 817, 828, 833, 1363, 1771, 1774, 1779, 1780, 1783, 1785, 1786, 1788
#endif
// Pull in the /analyze code annotations.
#include "annotations.h"

#ifdef POSIX
#pragma GCC diagnostic ignored "-Wswitch-enum"				// enumeration values not handled in switch
#pragma GCC diagnostic ignored "-Wparentheses"				// using the result of an assignment as a condition without parentheses
#endif

#ifdef OSX
#pragma GCC diagnostic ignored "-Wconversion-null"			// passing NULL to non-pointer argument 1
#pragma GCC diagnostic ignored "-Wnull-arithmetic"			// NULL used in arithmetic. Ie, vpanel == NULL where VPANEL is uint.
#pragma GCC diagnostic ignored "-Wlogical-op-parentheses"	// '&&' within '||' (wants parenthesis)
#pragma GCC diagnostic ignored "-Wconstant-conversion"		// implicit truncation from x to y (where y is smaller size than x) changes value
#pragma GCC diagnostic ignored "-Wformat-security"			// format string is not a string literal (potentially insecure)
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"	// C-linkage specified, but returns user-defined type
#pragma GCC diagnostic ignored "-Wswitch"					// enumeration values not handled in switch
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"	// virtual functions but non-virtual destructor
#pragma GCC diagnostic ignored "-Wformat"					// type conversion, format/argument conflict
#pragma GCC diagnostic ignored "-Wbool-conversions"			// type conversion
#endif

//-----------------------------------------------------------------------------
// Stack-based allocation related helpers
//-----------------------------------------------------------------------------
#if defined( COMPILER_GCC ) || defined( COMPILER_SNC )

	#define stackalloc( _size )		alloca( ALIGN_VALUE( _size, 16 ) )

	#ifdef PLATFORM_OSX
		#define mallocsize( _p )	( malloc_size( _p ) )
	#else
		#define mallocsize( _p )	( malloc_usable_size( _p ) )
	#endif

#elif defined ( COMPILER_MSVC )

	#define stackalloc( _size )		_alloca( ALIGN_VALUE( _size, 16 ) )
	#define mallocsize( _p )		( _msize( _p ) )

#endif

#define  stackfree( _p )			0

//-----------------------------------------------------------------------------
// Used to break into the debugger
//-----------------------------------------------------------------------------
#ifdef COMPILER_MSVC64
	#define DebuggerBreak()		__debugbreak()
#elif COMPILER_MSVC32
	#define DebuggerBreak()		__debugbreak() //__asm { int 3 }
#elif COMPILER_GCC
	#if   defined( OSX )
		#define DebuggerBreak()  if ( Plat_IsInDebugSession() ) asm( "int3" ); else { raise(SIGTRAP); }
	#elif defined( PLATFORM_CYGWIN ) || defined( PLATFORM_POSIX )
		#define DebuggerBreak()		__asm__( "int $0x3;")
	#else
		#define DebuggerBreak()	raise(SIGTRAP)
	#endif
#else
#error DebuggerBreak() is not defined for this platform!
#endif


FORCEINLINE float fsel(float fComparand, float fValGE, float fLT)
{
	return fComparand >= 0 ? fValGE : fLT;
}
FORCEINLINE double fsel(double fComparand, double fValGE, double fLT)
{
	return fComparand >= 0 ? fValGE : fLT;
}


//-----------------------------------------------------------------------------
// DLL export for platform utilities
//-----------------------------------------------------------------------------
#ifndef STATIC_TIER0

#ifdef TIER0_DLL_EXPORT
#define PLATFORM_INTERFACE	DLL_EXPORT
#define PLATFORM_OVERLOAD	DLL_GLOBAL_EXPORT
#else
#define PLATFORM_INTERFACE	DLL_IMPORT
#define PLATFORM_OVERLOAD	DLL_GLOBAL_IMPORT
#endif

#else	// BUILD_AS_DLL

#define PLATFORM_INTERFACE	extern
#define PLATFORM_OVERLOAD

#endif	// BUILD_AS_DLL

//-----------------------------------------------------------------------------
// Returns true if debugger attached, false otherwise
//-----------------------------------------------------------------------------
#if defined( PLATFORM_WINDOWS ) || LINUX
PLATFORM_INTERFACE void Plat_DebugString( const tchar * );
#else
#define Plat_DebugString(s) ((void)0)
#endif

PLATFORM_INTERFACE bool Plat_IsInDebugSession();

#define	DebuggerBreakIfDebugging() if ( !Plat_IsInDebugSession() ) ; else DebuggerBreak()

//-----------------------------------------------------------------------------
// Message Box
//-----------------------------------------------------------------------------
#if defined( PLATFORM_WINDOWS_PC )
PLATFORM_INTERFACE void Plat_MessageBox( const char *pTitle, const tchar *pMessage );
#else
#define Plat_MessageBox( t, m ) ((void)0)
#endif


//-----------------------------------------------------------------------------
// Posix platform helpers
//-----------------------------------------------------------------------------
#ifdef PLATFORM_POSIX

// Visual Studio likes to put an underscore in front of anything that looks like a portable function.
#define _getcwd getcwd
#define _vsnprintf vsnprintf
#define _stat stat
#define _unlink unlink
#define _putenv putenv
#define _chdir chdir
#define _access access

#define strcmpi stricmp
#define stricmp strcasecmp
#define _alloca alloca
#define GetProcAddress dlsym
#define strnicmp strncasecmp
#define _tempnam tempnam
#define _mkdir(dir) mkdir( dir, S_IRWXU | S_IRWXG | S_IRWXO )
#define _wtoi(arg) wcstol(arg, NULL, 10)
#define _wtoi64(arg) wcstoll(arg, NULL, 10)

typedef uintptr_t HMODULE;
typedef void *HANDLE;
#define __cdecl

#if !defined( _snprintf )	// some vpc's define this on the command line
#define _snprintf snprintf
#endif

#include <alloca.h>
#include <unistd.h>											// get unlink
#include <errno.h>


#endif // PLATFORM_POSIX

//-----------------------------------------------------------------------------
// Generally useful platform-independent macros (move to another file?)
//-----------------------------------------------------------------------------

// need macro for constant expression
#define ALIGN_VALUE( val, alignment ) ( ( val + alignment - 1 ) & ~( alignment - 1 ) ) 

#define MAX_FILEPATH 512 

// Defines MAX_PATH
#ifndef MAX_PATH
	#define MAX_PATH  260
#endif

#ifdef _WIN32
#define MAX_UNICODE_PATH 32767
#else
#define MAX_UNICODE_PATH MAX_PATH
#endif

#define MAX_UNICODE_PATH_IN_UTF8 MAX_UNICODE_PATH*4

//-----------------------------------------------------------------------------
// Portability casting
//-----------------------------------------------------------------------------
template < typename Tdst, typename Tsrc > FORCEINLINE Tdst size_cast( Tsrc val )
{
	static_assert( sizeof( Tdst ) <= sizeof( uint64_t ) && sizeof( Tsrc ) <= sizeof( uint64_t ), "Okay in my defense there weren't any types larger than 64-bits when this code was written." );

#ifdef DEBUG
	if ( sizeof ( Tdst ) < sizeof ( Tsrc ) )
	{
		Tdst cmpValDst = ( Tdst )val;

		// If this fails, the source value didn't actually fit in the destination value--you'll need to 
		// change the return type's size to match the source type in the calling code. 
		if ( val != ( Tsrc )cmpValDst )
		{
			// Can't use assert here, and if this happens when running on a machine internally we should crash 
			// in preference to missing the problem ( so not DebuggerBreakIfDebugging() ).
			DebuggerBreak();
		}
	}
#endif
	return ( Tdst )val;
}

//-----------------------------------------------------------------------------
// Endian-ness: all supported targets (x86/x64) are little-endian.
//-----------------------------------------------------------------------------
#define PLAT_LITTLE_ENDIAN 1

template <typename T>
inline T DWordSwapC( T dw )
{
   static_assert( sizeof( T ) == sizeof( uint32_t ), "DWordSwapC needs a 32-bit type" );

   uint32_t temp;
   temp  =   *((uint32_t *)&dw) 				>> 24;
   temp |= ((*((uint32_t *)&dw) & 0x00FF0000) >> 8);
   temp |= ((*((uint32_t *)&dw) & 0x0000FF00) << 8);
   temp |= ((*((uint32_t *)&dw) & 0x000000FF) << 24);

   return *((T*)&temp);
}

#define BigLong( val )				DWordSwapC( val )
#define SwapLong( val )				BigLong( val )
#define LittleLong( val )			( val )
#define LittleDWord( val )			( val )

// Pass floats by pointer for swapping to avoid truncation in the fpu
#define LittleFloat( pOut, pIn )	( *pOut = *pIn )

inline uint32_t LoadLittleDWord( uint32_t *base, unsigned int dwordIndex )
{
	return LittleDWord( base[dwordIndex] );
}

inline void StoreLittleDWord( uint32_t *base, unsigned int dwordIndex, uint32_t dword )
{
	base[dwordIndex] = LittleDWord(dword);
}

inline int64_t CastPtrToInt64( const void *p )
{
	return (int64_t)( (uintptr_t)p );
}

PLATFORM_INTERFACE double			Plat_FloatTime();		// Returns time in seconds since the module was loaded.
PLATFORM_INTERFACE uint32_t			Plat_MSTime();			// Time in milliseconds.

// Get the local calendar time.
// Same as time() followed by localtime(), but non-crash-prone.
PLATFORM_INTERFACE void				Plat_GetLocalTime( struct tm *pNow );

// Get the process' executable filename.
PLATFORM_INTERFACE void				Plat_GetModuleFilename( char *pOut, int nMaxBytes );

PLATFORM_INTERFACE void				Plat_ExitProcess( int nCode );

#define Plat_FastMemset memset

// DVD sector size, used for BSP lump alignment
#define XBOX_DVD_SECTORSIZE			2048

#define PLATFORM_EXT ""

//-----------------------------------------------------------------------------
// There is no requirement that a va_list be usable in multiple calls,
// but the Steam code does this.  Linux64 does not support reuse, whereas
// Windows does, so Linux64 breaks on code that was written and working
// on Windows.  Fortunately Linux has va_copy, which provides a simple
// way to let a va_list be used multiple times.  Unfortunately Windows
// does not have va_copy, so here we provide things to hide the difference.
//-----------------------------------------------------------------------------

class CReuseVaList
{
public:
	CReuseVaList( va_list List )
	{
#if defined(LINUX) || defined(OSX)
		va_copy( m_ReuseList, List );
#else
		m_ReuseList = List;
#endif
	}
	~CReuseVaList()
	{
#if defined(LINUX) || defined(OSX)
		va_end( m_ReuseList );
#endif
	}

	va_list m_ReuseList;
};

//-----------------------------------------------------------------------------
// C++11 helpers
//-----------------------------------------------------------------------------
#define VALVE_CPP11 1

#if VALVE_CPP11
template <class T> struct C11RemoveReference { typedef T Type; };
template <class T> struct C11RemoveReference<T&> { typedef T Type;  };
template <class T> struct C11RemoveReference<T&&> { typedef T Type;  };

template <class T>
inline typename C11RemoveReference<T>::Type&& Move( T&& obj )
{
	return static_cast< typename C11RemoveReference<T>::Type&& >( obj );
}
#endif

//-----------------------------------------------------------------------------
// Methods to invoke the constructor, copy constructor, and destructor
//-----------------------------------------------------------------------------

template <class T>
inline T* Construct( T* pMemory )
{
	return ::new( pMemory ) T;
}

template <class T, typename ARG1>
inline T* Construct( T* pMemory, ARG1 a1 )
{
	return ::new( pMemory ) T( a1 );
}

template <class T, typename ARG1, typename ARG2>
inline T* Construct( T* pMemory, ARG1 a1, ARG2 a2 )
{
	return ::new( pMemory ) T( a1, a2 );
}

template <class T, typename ARG1, typename ARG2, typename ARG3>
inline T* Construct( T* pMemory, ARG1 a1, ARG2 a2, ARG3 a3 )
{
	return ::new( pMemory ) T( a1, a2, a3 );
}

template <class T, typename ARG1, typename ARG2, typename ARG3, typename ARG4>
inline T* Construct( T* pMemory, ARG1 a1, ARG2 a2, ARG3 a3, ARG4 a4 )
{
	return ::new( pMemory ) T( a1, a2, a3, a4 );
}

template <class T, typename ARG1, typename ARG2, typename ARG3, typename ARG4, typename ARG5>
inline T* Construct( T* pMemory, ARG1 a1, ARG2 a2, ARG3 a3, ARG4 a4, ARG5 a5 )
{
	return ::new( pMemory ) T( a1, a2, a3, a4, a5 );
}

template <class T>
inline T* CopyConstruct( T* pMemory, T const& src )
{
	return ::new( pMemory ) T(src);
}

// [will] - Fixing a clang compile: unable to create a pseudo-destructor (aka a destructor that does nothing) for float __attribute__((__vector_size__(16)))
// Fixed by specializing the Destroy function to not call destructor for that type.
#if defined( __clang__ ) || defined (LINUX)

template <class T>
inline void Destruct( T* pMemory );

template <>
inline void Destruct( float __attribute__((__vector_size__(16)))* pMemory );

#endif // __clang__

template <class T>
inline void Destruct( T* pMemory )
{
	pMemory->~T();

#ifdef _DEBUG
	memset( pMemory, 0xDD, sizeof(T) );
#endif
}

// [will] - Fixing a clang compile: unable to create a pseudo-destructor (aka a destructor that does nothing) for float __attribute__((__vector_size__(16)))
// Fixed by specializing the Destroy function to not call destructor for that type.
#if defined( __clang__ ) || defined (LINUX)

template <>
inline void Destruct( float __attribute__((__vector_size__(16)))* pMemory )
{
#ifdef _DEBUG
	memset( pMemory, 0xDD, sizeof( float __attribute__((__vector_size__(16))) ) );
#endif
}

#endif // __clang__

//
// GET_OUTER()
//
// A platform-independent way for a contained class to get a pointer to its
// owner. If you know a class is exclusively used in the context of some
// "outer" class, this is a much more space efficient way to get at the outer
// class than having the inner class store a pointer to it.
//
//	class COuter
//	{
//		class CInner // Note: this does not need to be a nested class to work
//		{
//			void PrintAddressOfOuter()
//			{
//				printf( "Outer is at 0x%x\n", GET_OUTER( COuter, m_Inner ) );
//			}
//		};
//
//		CInner m_Inner;
//		friend class CInner;
//	};

#define GET_OUTER( OuterType, OuterMember ) \
   ( ( OuterType * ) ( (uint8_t *)this - offsetof( OuterType, OuterMember ) ) )

#include "tier0/valve_on.h"

#if defined(TIER0_DLL_EXPORT)
#undef stricmp
#undef strcmpi
#define stricmp(s1,s2) V_tier0_stricmp( s1, s2 )
#define strcmpi(s1,s2) V_tier0_stricmp( s1, s2 )
#else
int	_V_stricmp	  (const char *s1, const char *s2 );
int	V_strncasecmp (const char *s1, const char *s2, int n);

// A special high-performance case-insensitive compare function that in
// a single call distinguishes between exactly matching strings,
// strings equal in case-insensitive way, and not equal strings:
//   returns 0 if strings match exactly
//   returns >0 if strings match in a case-insensitive way, but do not match exactly
//   returns <0 if strings do not match even in a case-insensitive way
int	_V_stricmp_NegativeForUnequal	  ( const char *s1, const char *s2 );

#undef stricmp
#undef strcmpi
#define stricmp(s1,s2) _V_stricmp(s1, s2)
#define strcmpi(s1,s2) _V_stricmp(s1, s2)
#undef strnicmp
#define strnicmp V_strncasecmp 
#endif

// Use AlignedByteArray_t if you need an appropriately aligned array of T with no constructor (e.g CUtlMemoryFixed):
//  - usage:		AlignedByteArray_t< NUM, T >
//  - same as:		byte[ NUM*sizeof(T) ]
//  - BUT:			avoids calling T's constructor
//  - AND:			has same alignment as T
// [ Thanks to CygnusX1: http://stackoverflow.com/questions/5134217/aligning-data-on-the-stack-c ]

// ALIGN_N( n ) before a struct (MSVC) / ALIGN_N_POST( n ) after it (GCC/clang)
#if defined( GNUC )
#define ALIGN_N( _align_ )
#define ALIGN_N_POST( _align_ ) DECL_ALIGN( _align_ )
#else
#define ALIGN_N( _align_ ) DECL_ALIGN( _align_ )
#define ALIGN_N_POST( _align_ )
#endif

// !!! NOTE: if you get a compile error here, you are using VALIGNOF on an abstract type :NOTE !!!
#define VALIGNOF_PORTABLE( type ) ( sizeof( AlignOf_t<type> ) - sizeof( type ) )

#if defined( COMPILER_GCC ) || defined( COMPILER_MSVC )
#define VALIGNOF( type ) __alignof( type )
#define VALIGNOF_TEMPLATE_SAFE( type ) VALIGNOF_PORTABLE( type )
#else
#error "PORT: Code only tested with MSVC! Must validate with new compiler, and use built-in keyword if available."
#endif

// Use ValidateAlignment to sanity-check alignment usage when allocating arrays of an aligned type
#define ALIGN_ASSERT( pred ) { COMPILE_TIME_ASSERT( pred ); }
template< class T, int ALIGN >
inline void ValidateAlignmentExplicit(void)
{
	// Alignment must be a power of two
	ALIGN_ASSERT((ALIGN & (ALIGN - 1)) == 0);
	// Alignment must not imply gaps in the array (which the CUtlMemory pattern does not allow for)
	ALIGN_ASSERT(ALIGN <= sizeof(T));
	// Alignment must be a multiple of the size of the object type, or elements will *NOT* be aligned!
	ALIGN_ASSERT((sizeof(T) % ALIGN) == 0);
	// Alignment should be a multiple of the base alignment of T
//	ALIGN_ASSERT((ALIGN % VALIGNOF(T)) == 0);
}

// Portable alternative to __alignof
template<class T> struct AlignOf_t { AlignOf_t(){} AlignOf_t & operator=(const AlignOf_t &) { return *this; } uint8_t b; T t; };

template < size_t NUM, class T, int ALIGN > struct AlignedByteArrayExplicit_t{};
template < size_t NUM, class T > struct AlignedByteArray_t : public AlignedByteArrayExplicit_t< NUM, T, VALIGNOF_TEMPLATE_SAFE(T) > {};

#define DECLARE_ALIGNED_BYTE_ARRAY( ALIGN ) \
	template < size_t NUM, class T > \
	struct ALIGN_N( ALIGN ) AlignedByteArrayExplicit_t< NUM, T, ALIGN > \
	{ \
		/* NOTE: verify alignment in the constructor (which may be wrong if this is heap-allocated, for ALIGN > MEMALLOC_MAX_AUTO_ALIGN) */ \
		AlignedByteArrayExplicit_t()	{ if ( (ALIGN-1) & (size_t)this ) DebuggerBreakIfDebugging(); } \
		T *			Base( void )		{ ValidateAlignmentExplicit<T,ALIGN>(); return (T *)&m_Data; } \
		const T *	Base( void ) const	{ ValidateAlignmentExplicit<T,ALIGN>(); return (const T *)&m_Data; } \
	private: \
		uint8_t m_Data[ NUM*sizeof( T ) ]; \
	} ALIGN_N_POST( ALIGN );

DECLARE_ALIGNED_BYTE_ARRAY(1);
DECLARE_ALIGNED_BYTE_ARRAY(2);
DECLARE_ALIGNED_BYTE_ARRAY(4);
DECLARE_ALIGNED_BYTE_ARRAY(8);
DECLARE_ALIGNED_BYTE_ARRAY(16);
DECLARE_ALIGNED_BYTE_ARRAY(32);
DECLARE_ALIGNED_BYTE_ARRAY(64);
DECLARE_ALIGNED_BYTE_ARRAY(128);

// Tier0 uses this for faster stricmp.
PLATFORM_INTERFACE int V_tier0_stricmp( const char *a, const char *b );

//-----------------------------------------------------------------------------

PLATFORM_INTERFACE char const * Plat_GetEnv(char const *pEnvVarName);


//====== Copyright 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: String Tools
//
//===========================================================================//

// These are redefined in the project settings to prevent anyone from using them.
// We in this module are of a higher caste and thus are privileged in their use.
#include "tier1_v2/string_utils.hpp"
#ifdef strncpy
	#undef strncpy
#endif

#ifdef _snprintf
	#undef _snprintf
#endif

#if defined( sprintf )
	#undef sprintf
#endif

#if defined( vsprintf )
	#undef vsprintf
#endif

#ifdef _vsnprintf
#ifdef _WIN32
	#undef _vsnprintf
#endif
#endif

#ifdef vsnprintf
#ifndef _WIN32
	#undef vsnprintf
#endif
#endif

#if defined( strcat )
	#undef strcat
#endif

#ifdef strncat
	#undef strncat
#endif

// NOTE: I have to include stdio + stdarg first so vsnprintf gets compiled in
#include <stdio.h>

#include "tier0/basetypes.h"
#include "tier0/platform.h"

#ifdef stricmp
#undef stricmp
#endif

#ifdef POSIX

#include <iconv.h>

#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#define stricmp strcasecmp
#define _strtoi64 strtoll
#define _strtoui64 strtoull
#elif _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _WIN32
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#endif

#include "tier1/strtools.h"
#include <string.h>
#include "tier1/utldict.h"
#include "tier1/characterset.h"
#include "tier1/utlstring.h"
#include "tier1/fmtstr.h"
#include "tier1_v2/path_utils.hpp"




#ifndef NDEBUG
static volatile const char *pDebugString;
#define DEBUG_LINK_CHECK pDebugString = "tier1.lib built debug!"
#else
#define DEBUG_LINK_CHECK
#endif

void _V_memset (void *dest, int fill, int count)
{
	DEBUG_LINK_CHECK;
	Assert( count >= 0 );

	memset(dest,fill,count);
}

void _V_memcpy (void *dest, const void *src, int count)
{
	Assert( count >= 0 );

	memcpy( dest, src, count );
}

void _V_memmove(void *dest, const void *src, int count)
{
	Assert( count >= 0 );

	memmove( dest, src, count );
}

int _V_memcmp (const void *m1, const void *m2, int count)
{
	DEBUG_LINK_CHECK;
	Assert( count >= 0 );

	return memcmp( m1, m2, count );
}

int	_V_strlen(const char *str)
{
#ifdef POSIX
	if ( !str )
		return 0;
#endif
	return ( int )strlen( str );
}

#ifdef OSX
size_t strnlen( const char *s, size_t n )
{
	const char *p = (const char *)memchr( s, 0, n );
	return (p ? p - s : n);
}
#endif

int	_V_strnlen(const char *str, int count )
{
#ifdef POSIX
	if ( !str )
		return 0;
#endif
	return ( int )strnlen( str, count );
}

void _V_strcpy (char *dest, const char *src)
{
	DEBUG_LINK_CHECK;

	strcpy( dest, src );
}

int	_V_wcslen(const wchar_t *pwch)
{
	return ( int )wcslen( pwch );
}

char *_V_strrchr(const char *s, char c)
{
    int len = V_strlen(s);
    s += len;
    while (len--)
	if (*--s == c) return (char *)s;
    return nullptr;
}

int _V_strcmp (const char *s1, const char *s2)
{


	return strcmp( s1, s2 );
}

int _V_wcscmp (const wchar_t *s1, const wchar_t *s2)
{
	while (1)
	{
		if (*s1 != *s2)
			return *s1 < *s2 ? -1 : 1; // strings not equal
		if (!*s1)
			return 0;               // strings are equal
		s1++;
		s2++;
	}
	
	return -1;
}



int	_V_stricmp( const char *s1, const char *s2 )
{


	// It is not uncommon to compare a string to itself. Since stricmp
	// is expensive and pointer comparison is cheap, this simple test
	// can save a lot of cycles, and cache pollution.
	// This also implicitly does the s1 and s2 both equal to NULL check
	// that the POSIX code used to have.
	if ( s1 == s2 )
		return 0;

#ifdef POSIX
	if ( s1 == nullptr)
		return -1;
	if ( s2 == nullptr)
		return 1;
	
	return stricmp( s1, s2 );
#else	
	uint8_t const *pS1 = ( uint8_t const * ) s1;
	uint8_t const *pS2 = ( uint8_t const * ) s2;
	for(;;)
	{
		int c1 = *( pS1++ );
		int c2 = *( pS2++ );
		if ( c1 == c2 )
		{
			if ( !c1 ) return 0;
		}
		else
		{
			if ( ! c2 )
			{
				return c1 - c2;
			}
			c1 = FastASCIIToLower( c1 );
			c2 = FastASCIIToLower( c2 );
			if ( c1 != c2 )
			{
				return c1 - c2;
			}
		}
		c1 = *( pS1++ );
		c2 = *( pS2++ );
		if ( c1 == c2 )
		{
			if ( !c1 ) return 0;
		}
		else
		{
			if ( ! c2 )
			{
				return c1 - c2;
			}
			c1 = FastASCIIToLower( c1 );
			c2 = FastASCIIToLower( c2 );
			if ( c1 != c2 )
			{
				return c1 - c2;
			}
		}
	}
#endif
}

// A special high-performance case-insensitive compare function
// returns 0 if strings match exactly
// returns >0 if strings match in a case-insensitive way, but do not match exactly
// returns <0 if strings do not match even in a case-insensitive way
int	_V_stricmp_NegativeForUnequal( const char *s1, const char *s2 )
{


	// It is not uncommon to compare a string to itself. Since stricmp
	// is expensive and pointer comparison is cheap, this simple test
	// can save a lot of cycles, and cache pollution.
	if ( s1 == s2 )
		return 0;

	uint8_t const *pS1 = ( uint8_t const * ) s1;
	uint8_t const *pS2 = ( uint8_t const * ) s2;
	int iExactMatchResult = 1;
	for(;;)
	{
		int c1 = *( pS1++ );
		int c2 = *( pS2++ );
		if ( c1 == c2 )
		{
			// strings are case-insensitive equal, coerce accumulated
			// case-difference to 0/1 and return it
			if ( !c1 ) return !iExactMatchResult;
		}
		else
		{
			if ( ! c2 )
			{
				// c2=0 and != c1  =>  not equal
				return -1;
			}
			iExactMatchResult = 0;
			c1 = FastASCIIToLower( c1 );
			c2 = FastASCIIToLower( c2 );
			if ( c1 != c2 )
			{
				// strings are not equal
				return -1;
			}
		}
		c1 = *( pS1++ );
		c2 = *( pS2++ );
		if ( c1 == c2 )
		{
			// strings are case-insensitive equal, coerce accumulated
			// case-difference to 0/1 and return it
			if ( !c1 ) return !iExactMatchResult;
		}
		else
		{
			if ( ! c2 )
			{
				// c2=0 and != c1  =>  not equal
				return -1;
			}
			iExactMatchResult = 0;
			c1 = FastASCIIToLower( c1 );
			c2 = FastASCIIToLower( c2 );
			if ( c1 != c2 )
			{
				// strings are not equal
				return -1;
			}
		}
	}
}


char *_V_strstr( const char *s1, const char *search )
{
	return (char *)strstr( s1, search );
}

char *_V_strupr( char *start )
{
	return strupr( start );
}

// ASCII only: bytes >= 0x80 (UTF-8 sequences in paths) are kept as they are.
char *_V_strlower( char *start )
{
	for ( char *p = start; *p; ++p )
	{
		if ( *p >= 'A' && *p <= 'Z' )
			*p = (char)( *p - 'A' + 'a' );
	}
	return start;
}

wchar_t *_V_wcsupr (wchar_t *start)
{
	return _wcsupr( start );
}

wchar_t *_V_wcslower (wchar_t *start)
{
	return _wcslwr( start );
}

int V_strncmp(const char *s1, const char *s2, int count)
{
	Assert( count >= 0 );


	while ( count-- > 0 )
	{
		if ( *s1 != *s2 )
			return *s1 < *s2 ? -1 : 1; // string different
		if ( *s1 == '\0' )
			return 0; // null terminator hit - strings the same
		s1++;
		s2++;
	}

	return 0; // count characters compared the same
}

char *V_strnlwr(char *s, size_t count)
{
	Assert( count >= 0 );

	char* pRet = s;
	if ( !s || !count )
		return s;

	while ( -- count > 0 )
	{
		if ( !*s )
			return pRet; // reached end of string

		*s = tolower( *s );
		++s;
	}

	*s = 0; // null-terminate original string at "count-1"
	return pRet;
}


int V_strncasecmp (const char *s1, const char *s2, int n)
{
	Assert( n >= 0 );

	
	while ( n-- > 0 )
	{
		int c1 = *s1++;
		int c2 = *s2++;

		if (c1 != c2)
		{
			if (c1 >= 'a' && c1 <= 'z')
				c1 -= ('a' - 'A');
			if (c2 >= 'a' && c2 <= 'z')
				c2 -= ('a' - 'A');
			if (c1 != c2)
				return c1 < c2 ? -1 : 1;
		}
		if ( c1 == '\0' )
			return 0; // null terminator hit - strings the same
	}
	
	return 0; // n characters compared the same
}

int V_strcasecmp( const char *s1, const char *s2 )
{


	return V_stricmp( s1, s2 );
}

int V_strnicmp (const char *s1, const char *s2, int n)
{
	DEBUG_LINK_CHECK;
	Assert( n >= 0 );

	return V_strncasecmp( s1, s2, n );
}


std::optional<std::string_view> string_after_prefix(const std::string_view str, const std::string_view prefix)
{
    auto prefix_offset = vstd::ifind(str, prefix);
    if (prefix_offset == std::string_view::npos) {
        return std::nullopt;
    }
	return str.substr(prefix_offset + prefix.size());
}

const char *StringAfterPrefixCaseSensitive( const char *str, const char *prefix )
{
	do
	{
		if ( !*prefix )
			return str;
	}
	while ( *str++ == *prefix++ );
	return nullptr;
}


int64_t V_atoi64( const char *str )
{
	int64_t             val;
	int64_t             sign;
	int64_t             c;
	
	Assert( str );
	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else if (*str == '+')
	{
		sign = 1;
		str++;
	}
	else
	{
		sign = 1;
	}
		
	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val*sign;
		}
	}
	
//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}
	
//
// assume decimal
//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val*sign;
		val = val*10 + c - '0';
	}
	
	return 0;
}

uint64_t V_atoui64( const char *str )
{
	uint64_t             val;
	uint64_t             c;

	Assert( str );

	val = 0;

	//
	// check for hex
	//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val;
		}
	}

	//
	// check for character
	//
	if (str[0] == '\'')
	{
		return str[1];
	}

	//
	// assume decimal
	//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val;
		val = val*10 + c - '0';
	}

	return 0;
}

int V_atoi( const char *str )
{ 
	return (int)V_atoi64( str );
}

float V_atof (const char *str)
{
	return (float)V_atod( str );
}

double V_atod(const char *str)
{
	DEBUG_LINK_CHECK;
	double			val;
	int             sign;
	int             c;
	int             decimal, total;
	
	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else if (*str == '+')
	{
		sign = 1;
		str++;
	}
	else
	{
		sign = 1;
	}
		
	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val*16) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val*16) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val*16) + c - 'A' + 10;
			else
				return val*sign;
		}
	}
	
//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}
	
//
// assume decimal
//
	decimal = -1;
	total = 0;
	int exponent = 0;
	while (1)
	{
		c = *str++;
		if (c == '.')
		{
			if ( decimal != -1 )
			{
				break;
			}

			decimal = total;
			continue;
		}
		if (c <'0' || c > '9')
		{
			if ( c == 'e' || c == 'E' )
			{
				exponent = V_atoi(str);
			}
			break;
		}
		val = val*10 + c - '0';
		total++;
	}

	if ( exponent != 0 )
	{
		val *= pow( 10.0, exponent );
	}
	if (decimal == -1)
		return val*sign;
	while (total > decimal)
	{
		val /= 10;
		total--;
	}
	
	return val*sign;
}

//-----------------------------------------------------------------------------
// Normalizes a float string in place.  
//
// (removes leading zeros, trailing zeros after the decimal point, and the decimal point itself where possible)
//-----------------------------------------------------------------------------
void V_normalizeFloatString( char* pFloat )
{
	// If we have a decimal point, remove trailing zeroes:
	if( strchr( pFloat,'.' ) )
	{
		int len = V_strlen(pFloat);

		while( len > 1 && pFloat[len - 1] == '0' )
		{
			pFloat[len - 1] = '\0';
			len--;
		}

		if( len > 1 && pFloat[ len - 1 ] == '.' )
		{
			pFloat[len - 1] = '\0';
			len--;
		}
	}

	// TODO: Strip leading zeros

}

//-----------------------------------------------------------------------------
// Finds a string in another string with a case insensitive test
//-----------------------------------------------------------------------------
const char* V_stristr( const char* pStr, const char* pSearch )
{
	Assert( pStr );
	Assert( pSearch );
	if (!pStr || !pSearch) 
		return nullptr;

	const char* pLetter = pStr;

	// Check the entire string
	while (*pLetter != 0)
	{
		// Skip over non-matches
		if ( FastASCIIToLower( *pLetter ) == FastASCIIToLower( *pSearch) )
		{
			// Check for match
			const char* pMatch = pLetter + 1;
			const char* pTest = pSearch + 1;
			while (*pTest != 0)
			{
				// We've run off the end; don't bother.
				if (*pMatch == 0)
					return nullptr;

				if ( FastASCIIToLower( *pMatch) != FastASCIIToLower( *pTest ) )
					break;

				++pMatch;
				++pTest;
			}

			// Found a match!
			if ( *pTest == 0 )
				return pLetter;
		}

		++pLetter;
	}

	return nullptr;
}

char* V_stristr( char* pStr, const char* pSearch )
{
	return (char*)V_stristr( (const char*)pStr, pSearch );
}

//-----------------------------------------------------------------------------
// Finds a string in another string with a case insensitive test w/ length validation
//-----------------------------------------------------------------------------
const char* V_strnistr( const char* pStr, const char* pSearch, int n )
{
	Assert( pStr );
	Assert( pSearch );
	if (!pStr || !pSearch) 
		return nullptr;

	const char* pLetter = pStr;

	// Check the entire string
	while (*pLetter != 0)
	{
		if ( n <= 0 )
			return nullptr;

		// Skip over non-matches
		if (FastASCIIToLower(*pLetter) == FastASCIIToLower(*pSearch))
		{
			int n1 = n - 1;

			// Check for match
			const char* pMatch = pLetter + 1;
			const char* pTest = pSearch + 1;
			while (*pTest != 0)
			{
				if ( n1 <= 0 )
					return nullptr;

				// We've run off the end; don't bother.
				if (*pMatch == 0)
					return nullptr;

				if (FastASCIIToLower(*pMatch) != FastASCIIToLower(*pTest))
					break;

				++pMatch;
				++pTest;
				--n1;
			}

			// Found a match!
			if (*pTest == 0)
				return pLetter;
		}

		++pLetter;
		--n;
	}

	return nullptr;
}

const char* V_strnchr( const char* pStr, char c, int n )
{
	const char* pLetter = pStr;
	const char* pLast = pStr + n;

	// Check the entire string
	while ( (pLetter < pLast) && (*pLetter != 0) )
	{
		if (*pLetter == c)
			return pLetter;
		++pLetter;
	}
	return nullptr;
}



void V_strncpy( char *pDest, const char *pSrc, int maxLen )
{
	Assert( maxLen >= sizeof( *pDest ) );
	DEBUG_LINK_CHECK;

	// NOTE: Never never use strncpy! Here's what it actually does, which is not what we want!

	// (from MSDN)
	// The strncpy function copies the initial count characters of strSource to strDest
	// and returns strDest. If count is less than or equal to the length of strSource, 
	// a null character is not appended automatically to the copied string. If count 
	// is greater than the length of strSource, the destination string is padded with 
	// null characters up to length count. The behavior of strncpy is undefined 
	// if the source and destination strings overlap.
	// strncpy( pDest, pSrc, maxLen );

	// FIXME: This could be optimized to do copies a dword at a time maybe?
	char *pLast = pDest + maxLen - 1;
	while ( (pDest < pLast) && (*pSrc != 0) )
	{
		*pDest = *pSrc;
		++pDest; ++pSrc;
	}
	*pDest = 0;
}

// warning C6053: Call to 'wcsncpy' might not zero-terminate string 'pDest'
// warning C6059: Incorrect length parameter in call to 'strncat'. Pass the number of remaining characters, not the buffer size of 'argument 1'
// warning C6386: Buffer overrun: accessing 'argument 1', the writable size is 'destBufferSize' bytes, but '1000' bytes might be written
// These warnings were investigated through code inspection and writing of tests and they are
// believed to all be spurious.
#ifdef _PREFAST_
#pragma warning( push )
#pragma warning( disable : 6053 6059 6386 )
#endif

void V_wcsncpy( OUT_Z_BYTECAP(maxLenInBytes) wchar_t *pDest, wchar_t const *pSrc, int maxLenInBytes )
{
	Assert( maxLenInBytes >= sizeof( *pDest ) );

	int maxLen = maxLenInBytes / sizeof(wchar_t);

	wcsncpy( pDest, pSrc, maxLen );
	if( maxLen )
	{
		pDest[maxLen-1] = 0;
	}
}

#ifdef _PREFAST_
// Suppress warnings about _vsnwprintf and _vsnprintf not zero-terminating the buffers.
// We explicitly null-terminate in the cases that matter.
#pragma warning( disable : 6053 )
#endif
int V_snwprintf( OUT_Z_CAP(maxLenInNumWideCharacters) wchar_t *pDest, int maxLenInNumWideCharacters, PRINTF_FORMAT_STRING const wchar_t *pFormat, ... )
{
	Assert( maxLenInNumWideCharacters >= 0 );

	va_list marker;

	va_start( marker, pFormat );
#ifdef _WIN32
	int len = _vsnwprintf( pDest, maxLenInNumWideCharacters, pFormat, marker );
#elif POSIX
	int len = vswprintf( pDest, maxLenInNumWideCharacters, pFormat, marker );
#else
#error "define vsnwprintf type."
#endif
	va_end( marker );

	// Len < 0 represents an overflow
	// Len == maxLen represents exactly fitting with no NULL termination
	//	Len can be > maxLen on Linux systems when the output was truncated
	if ( ( len < 0 ) ||
		 ( maxLenInNumWideCharacters > 0 && len >= maxLenInNumWideCharacters ) )
	{
		len = maxLenInNumWideCharacters - 1;
		pDest[maxLenInNumWideCharacters-1] = 0;
	}
	
	return len;
}



int V_snprintf( char *pDest, int maxLen, char const *pFormat, ... )
{
	Assert( maxLen > 0 );

	va_list marker;

	va_start( marker, pFormat );
#ifdef _WIN32
	int len = _vsnprintf( pDest, maxLen, pFormat, marker );
#elif POSIX
	int len = vsnprintf( pDest, maxLen, pFormat, marker );
#else
	#error "define vsnprintf type."
#endif
	va_end( marker );

	// Len < 0 represents an overflow
	// Len == maxLen represents exactly fitting with no NULL termination
	if ( ( len < 0 ) ||
		 ( maxLen > 0 && len >= maxLen ) )
	{
		len = maxLen - 1;
		pDest[maxLen-1] = 0;
	}

	return len;
}


int V_vsnprintf( char *pDest, int maxLen, const char *pFormat, va_list params )
{
	Assert( maxLen > 0 );

	int len = _vsnprintf( pDest, maxLen, pFormat, params );

	if ( ( len < 0 ) ||
		 ( maxLen > 0 && len >= maxLen ) )
	{
		len = maxLen - 1;
		pDest[maxLen-1] = 0;
	}

	return len;
}


int V_vsnprintfRet( char *pDest, int maxLen, const char *pFormat, va_list params, bool *pbTruncated )
{
	Assert( maxLen > 0 );

	int len = _vsnprintf( pDest, maxLen, pFormat, params );

	bool bTruncated = ( len < 0 ) || ( len >= maxLen );
	if ( pbTruncated )
	{
		*pbTruncated = bTruncated;
	}

	if( bTruncated && maxLen > 0 )
	{
		len = maxLen - 1;
		pDest[maxLen-1] = 0;
	}

	return len;
}


//-----------------------------------------------------------------------------
// Purpose: If COPY_ALL_CHARACTERS == max_chars_to_copy then we try to add the whole pSrc to the end of pDest, otherwise
//  we copy only as many characters as are specified in max_chars_to_copy (or the # of characters in pSrc if thats's less).
// Input  : *pDest - destination buffer
//			*pSrc - string to append
//			destBufferSize - sizeof the buffer pointed to by pDest
//			max_chars_to_copy - COPY_ALL_CHARACTERS in pSrc or max # to copy
// Output : char * the copied buffer
//-----------------------------------------------------------------------------
char *V_strncat( char *pDest, const char *pSrc, size_t maxLenInBytes, int nMaxCharsToCopy )
{
	DEBUG_LINK_CHECK;
	size_t charstocopy = (size_t)0;

	Assert( nMaxCharsToCopy >= 0 || nMaxCharsToCopy == COPY_ALL_CHARACTERS );
	
	size_t len = V_strlen(pDest);
	size_t srclen = V_strlen( pSrc );
	if ( nMaxCharsToCopy == COPY_ALL_CHARACTERS )
	{
		charstocopy = srclen;
	}
	else
	{
		charstocopy = MIN( nMaxCharsToCopy, (int)srclen );
	}

	if ( len + charstocopy >= maxLenInBytes )
	{
		charstocopy = maxLenInBytes - len - 1;
	}

	// charstocopy can end up negative if you fill a buffer and then pass in a smaller
	// buffer size. Yes, this actually happens.
	// Cast to ptrdiff_t is necessary in order to check for negative (size_t is unsigned)
	if ( charstocopy <= 0 )
	{
		return pDest;
	}

	ANALYZE_SUPPRESS( 6059 ); // warning C6059: : Incorrect length parameter in call to 'strncat'. Pass the number of remaining characters, not the buffer size of 'argument 1'
	char *pOut = strncat( pDest, pSrc, charstocopy );
	pOut[maxLenInBytes-1] = 0;
	return pOut;
}

//-----------------------------------------------------------------------------
// Purpose: If COPY_ALL_CHARACTERS == max_chars_to_copy then we try to add the whole pSrc to the end of pDest, otherwise
//  we copy only as many characters as are specified in max_chars_to_copy (or the # of characters in pSrc if thats's less).
// Input  : *pDest - destination buffer
//			*pSrc - string to append
//			maxLenInCharacters - sizeof the buffer in characters pointed to by pDest
//			max_chars_to_copy - COPY_ALL_CHARACTERS in pSrc or max # to copy
// Output : char * the copied buffer
//-----------------------------------------------------------------------------
wchar_t *V_wcsncat( INOUT_Z_BYTECAP(maxLenInBytes) wchar_t *pDest, const wchar_t *pSrc, int maxLenInBytes, int nMaxCharsToCopy )
{
	DEBUG_LINK_CHECK;
	size_t charstocopy = (size_t)0;

    Assert( maxLenInBytes >= 0 );
    
	int maxLenInCharacters = maxLenInBytes / sizeof( wchar_t );

	size_t len = wcslen(pDest);
	size_t srclen = wcslen( pSrc );
	if ( nMaxCharsToCopy <= COPY_ALL_CHARACTERS )
	{
		charstocopy = srclen;
	}
	else
	{
		charstocopy = (size_t)MIN( nMaxCharsToCopy, (int)srclen );
	}

	if ( len + charstocopy >= (size_t)maxLenInCharacters )
	{
		charstocopy = maxLenInCharacters - len - 1;
	}

	if ( !charstocopy )
	{
		return pDest;
	}

	wchar_t *pOut = wcsncat( pDest, pSrc, charstocopy );
	pOut[maxLenInCharacters-1] = 0;
	return pOut;
}



//-----------------------------------------------------------------------------
// Purpose: Converts value into x.xx MB/ x.xx KB, x.xx bytes format, including commas
// Input  : value - 
//			2 - 
//			false - 
// Output : char
//-----------------------------------------------------------------------------
#define NUM_PRETIFYMEM_BUFFERS 8
char *V_pretifymem( float value, int digitsafterdecimal /*= 2*/, bool usebinaryonek /*= false*/ )
{
	static char output[ NUM_PRETIFYMEM_BUFFERS ][ 32 ];
	static int  current;

	float		onekb = usebinaryonek ? 1024.0f : 1000.0f;
	float		onemb = onekb * onekb;

	char *out = output[ current ];
	current = ( current + 1 ) & ( NUM_PRETIFYMEM_BUFFERS -1 );

	char suffix[ 8 ];

	// First figure out which bin to use
	if ( value > onemb )
	{
		value /= onemb;
		V_snprintf( suffix, sizeof( suffix ), " MB" );
	}
	else if ( value > onekb )
	{
		value /= onekb;
		V_snprintf( suffix, sizeof( suffix ), " KB" );
	}
	else
	{
		V_snprintf( suffix, sizeof( suffix ), " bytes" );
	}

	char val[ 32 ];

	// Clamp to >= 0
	digitsafterdecimal = MAX( digitsafterdecimal, 0 );

	// If it's basically integral, don't do any decimals
	if ( FloatMakePositive( value - (int)value ) < 0.00001 )
	{
		V_snprintf( val, sizeof( val ), "%i%s", (int)value, suffix );
	}
	else
	{
		char fmt[ 32 ];

		// Otherwise, create a format string for the decimals
		V_snprintf( fmt, sizeof( fmt ), "%%.%if%s", digitsafterdecimal, suffix );
		V_snprintf( val, sizeof( val ), fmt, value );
	}

	// Copy from in to out
	char *i = val;
	char *o = out;

	// Search for decimal or if it was integral, find the space after the raw number
	char *dot = strstr( i, "." );
	if ( !dot )
	{
		dot = strstr( i, " " );
	}

	// Compute position of dot
	int pos = dot - i;
	// Don't put a comma if it's <= 3 long
	pos -= 3;

	while ( *i )
	{
		// If pos is still valid then insert a comma every third digit, except if we would be
		//  putting one in the first spot
		if ( pos >= 0 && !( pos % 3 ) )
		{
			// Never in first spot
			if ( o != out )
			{
				*o++ = ',';
			}
		}

		// Count down comma position
		pos--;

		// Copy rest of data as normal
		*o++ = *i++;
	}

	// Terminate
	*o = 0;

	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Returns a string representation of an integer with commas
//			separating the 1000s (ie, 37,426,421)
// Input  : value -		Value to convert
// Output : Pointer to a static buffer containing the output
//-----------------------------------------------------------------------------
#define NUM_PRETIFYNUM_BUFFERS 8 // Must be a power of two
char *V_pretifynum( int64_t inputValue )
{
	static char output[ NUM_PRETIFYMEM_BUFFERS ][ 32 ];
	static int  current;

	// Point to the output buffer.
	char * const out = output[ current ];
	// Track the output buffer end for easy calculation of bytes-remaining.
	const char* const outEnd = out + sizeof( output[ current ] );

	// Point to the current output location in the output buffer.
	char *pchRender = out;
	// Move to the next output pointer.
	current = ( current + 1 ) & ( NUM_PRETIFYMEM_BUFFERS -1 );

	*out = 0;

	// In order to handle the most-negative int64 we need to negate it
	// into a uint64.
	uint64_t value;
	// Render the leading minus sign, if necessary
	if ( inputValue < 0 )
	{
		V_snprintf( pchRender, 32, "-" );
		value = (uint64_t)-inputValue;
		// Advance our output pointer.
		pchRender += V_strlen( pchRender );
	}
	else
	{
		value = (uint64_t)inputValue;
	}

	// Now let's find out how big our number is. The largest number we can fit
	// into 63 bits is about 9.2e18. So, there could potentially be six
	// three-digit groups.

	// We need the initial value of 'divisor' to be big enough to divide our
	// number down to 1-999 range.
	uint64_t divisor = 1;
	// Loop more than six times to avoid integer overflow.
	for ( int i = 0; i < 6; ++i )
	{
		// If our divisor is already big enough then stop.
		if ( value < divisor * 1000 )
			break;

		divisor *= 1000;
	}

	// Print the leading batch of one to three digits.
	int toPrint = value / divisor;
	V_snprintf( pchRender, outEnd - pchRender, "%d", toPrint );

	for (;;)
	{
		// Advance our output pointer.
		pchRender += V_strlen( pchRender );
		// Adjust our value to be printed and our divisor.
		value -= toPrint * divisor;
		divisor /= 1000;
		if ( !divisor )
			break;

		// The remaining blocks of digits always include a comma and three digits.
		toPrint = value / divisor;
		V_snprintf( pchRender, outEnd - pchRender, ",%03d", toPrint );
	}

	return out;
}


//-----------------------------------------------------------------------------
// Purpose: Converts a UTF8 string into a unicode string
//-----------------------------------------------------------------------------
int _V_UTF8ToUnicode( const char *pUTF8, wchar_t *pwchDest, int cubDestSizeInBytes )
{
	Assert( cubDestSizeInBytes >= sizeof( *pwchDest ) );
	pwchDest[0] = 0;
	if ( !pUTF8 )
		return 0;
#ifdef _WIN32
	int cchResult = MultiByteToWideChar( CP_UTF8, 0, pUTF8, -1, pwchDest, cubDestSizeInBytes / sizeof(wchar_t) );
#elif POSIX
	int cchResult = mbstowcs( pwchDest, pUTF8, cubDestSizeInBytes / sizeof(wchar_t) );
#endif
	pwchDest[(cubDestSizeInBytes / sizeof(wchar_t)) - 1] = 0;
	return cchResult;
}

//-----------------------------------------------------------------------------
// Purpose: Converts a unicode string into a UTF8 (standard) string
//-----------------------------------------------------------------------------
int _V_UnicodeToUTF8( const wchar_t *pUnicode, char *pUTF8, int cubDestSizeInBytes )
{
	if ( cubDestSizeInBytes > 0 )
	{
		pUTF8[0] = 0;
	}

#ifdef _WIN32
	int cchResult = WideCharToMultiByte( CP_UTF8, 0, pUnicode, -1, pUTF8, cubDestSizeInBytes, NULL, NULL );
#elif POSIX
	int cchResult = 0;
	if ( pUnicode && pUTF8 )
		cchResult = wcstombs( pUTF8, pUnicode, cubDestSizeInBytes );
#endif

	if ( cubDestSizeInBytes > 0 )
	{
		pUTF8[cubDestSizeInBytes - 1] = 0;
	}

	return cchResult;
}


//-----------------------------------------------------------------------------
// Purpose: Converts a ucs2 string to a unicode (wchar_t) one, no-op on win32
//-----------------------------------------------------------------------------
int _V_UCS2ToUnicode( const ucs2 *pUCS2, wchar_t *pUnicode, int cubDestSizeInBytes )
{
	Assert( cubDestSizeInBytes >= sizeof( *pUnicode ) );
	
	pUnicode[0] = 0;
#ifdef _WIN32
	int cchResult = V_wcslen( pUCS2 );
	V_memcpy( pUnicode, pUCS2, cubDestSizeInBytes );
#else
	iconv_t conv_t = iconv_open( "UCS-4LE", "UCS-2LE" );
	int cchResult = -1;
	size_t nLenUnicde = cubDestSizeInBytes;
	size_t nMaxUTF8 = cubDestSizeInBytes;
	char *pIn = (char *)pUCS2;
	char *pOut = (char *)pUnicode;
	if (conv_t != (iconv_t)-1)
	{
		cchResult = 0;
		cchResult = iconv( conv_t, &pIn, &nLenUnicde, &pOut, &nMaxUTF8 );
		iconv_close( conv_t );
		if ( (int)cchResult < 0 )
			cchResult = 0;
		else
			cchResult = nMaxUTF8;
	}
#endif
	pUnicode[(cubDestSizeInBytes / sizeof(wchar_t)) - 1] = 0;
	return cchResult;	

}

#ifdef _PREFAST_
#pragma warning( pop ) // Restore the /analyze warnings
#endif


//-----------------------------------------------------------------------------
// Purpose: Converts a wchar_t string into a UCS2 string -noop on windows
//-----------------------------------------------------------------------------
int _V_UnicodeToUCS2( const wchar_t *pUnicode, int cubSrcInBytes, char *pUCS2, int cubDestSizeInBytes )
{
	 // TODO: MACMERGE: Figure out how to convert from 2-byte Win32 wchars to platform wchar_t type that can be 4 bytes
#if defined( _WIN32 )
	// Figure out which buffer is smaller and convert from bytes to character
	// counts.
	int cchResult = MIN(cubSrcInBytes/sizeof(wchar_t), cubDestSizeInBytes/sizeof(wchar_t) );
	wchar_t *pDest = (wchar_t*)pUCS2;
	wcsncpy( pDest, pUnicode, cchResult );
	// Make sure we NULL-terminate.
	pDest[ cchResult - 1 ] = 0;

#elif defined (POSIX)
	iconv_t conv_t = iconv_open( "UCS-2LE", "UTF-32LE" );
	size_t cchResult = -1;
	size_t nLenUnicde = cubSrcInBytes;
	size_t nMaxUCS2 = cubDestSizeInBytes;
	char *pIn = (char*)pUnicode;
	char *pOut = pUCS2;
	if (conv_t != (iconv_t)-1)
	{
		cchResult = 0;
		cchResult = iconv( conv_t, &pIn, &nLenUnicde, &pOut, &nMaxUCS2 );
		iconv_close( conv_t );
		if ( (int)cchResult < 0 )
			cchResult = 0;
		else
			cchResult = cubSrcInBytes / sizeof( wchar_t );
	}
#else
	#error Must be implemented for this platform
#endif
	return cchResult;	
}


//-----------------------------------------------------------------------------
// Purpose: Converts a ucs-2 (windows wchar_t) string into a UTF8 (standard) string
//-----------------------------------------------------------------------------
int _V_UCS2ToUTF8( const ucs2 *pUCS2, char *pUTF8, int cubDestSizeInBytes )
{
	pUTF8[0] = 0;
#ifdef _WIN32
	// under win32 wchar_t == ucs2, sigh
	int cchResult = WideCharToMultiByte( CP_UTF8, 0, pUCS2, -1, pUTF8, cubDestSizeInBytes, NULL, NULL );
#elif defined(POSIX)
	iconv_t conv_t = iconv_open( "UTF-8", "UCS-2LE" );
	size_t cchResult = -1;
	size_t nLenUnicde = cubDestSizeInBytes;
	size_t nMaxUTF8 = cubDestSizeInBytes;
	char *pIn = (char *)pUCS2;
	char *pOut = (char *)pUTF8;
	if (conv_t != (iconv_t)-1)
	{
		cchResult = 0;
		cchResult = iconv( conv_t, &pIn, &nLenUnicde, &pOut, &nMaxUTF8 );
		iconv_close( conv_t );
		if ( (int)cchResult < 0 )
			cchResult = 0;
		else
			cchResult = nMaxUTF8;
	}
#endif
	pUTF8[cubDestSizeInBytes - 1] = 0;
	return cchResult;	
}


//-----------------------------------------------------------------------------
// Purpose: Converts a UTF8 to ucs-2 (windows wchar_t)
//-----------------------------------------------------------------------------
int _V_UTF8ToUCS2( const char *pUTF8, int cubSrcInBytes, ucs2 *pUCS2, int cubDestSizeInBytes )
{
	Assert( cubDestSizeInBytes >= sizeof(pUCS2[0]) );
	pUCS2[0] = 0;
#ifdef _WIN32
	// under win32 wchar_t == ucs2, sigh
	int cchResult = MultiByteToWideChar( CP_UTF8, 0, pUTF8, -1, pUCS2, cubDestSizeInBytes / sizeof(wchar_t) );
#elif defined(POSIX)
	iconv_t conv_t = iconv_open( "UCS-2LE", "UTF-8" );
	size_t cchResult = -1;
	size_t nLenUnicde = cubSrcInBytes;
	size_t nMaxUTF8 = cubDestSizeInBytes;
	char *pIn = (char *)pUTF8;
	char *pOut = (char *)pUCS2;
	if (conv_t != (iconv_t)-1)
	{
		cchResult = 0;
		cchResult = iconv( conv_t, &pIn, &nLenUnicde, &pOut, &nMaxUTF8 );
		iconv_close( conv_t );
		if ( (int)cchResult < 0 )
			cchResult = 0;
		else
			cchResult = cubSrcInBytes;

	}
#endif
	pUCS2[ (cubDestSizeInBytes/sizeof(ucs2)) - 1] = 0;
	return cchResult;	
}



//-----------------------------------------------------------------------------
// Purpose: Returns the 4 bit nibble for a hex character
// Input  : c - 
// Output : unsigned char
//-----------------------------------------------------------------------------
static unsigned char V_nibble( char c )
{
	if ( ( c >= '0' ) &&
		 ( c <= '9' ) )
	{
		 return (unsigned char)(c - '0');
	}

	if ( ( c >= 'A' ) &&
		 ( c <= 'F' ) )
	{
		 return (unsigned char)(c - 'A' + 0x0a);
	}

	if ( ( c >= 'a' ) &&
		 ( c <= 'f' ) )
	{
		 return (unsigned char)(c - 'a' + 0x0a);
	}

	return '0';
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *in - 
//			numchars - 
//			*out - 
//			maxoutputbytes - 
//-----------------------------------------------------------------------------
void V_hextobinary( const char *in, int numchars, uint8_t *out, int maxoutputbytes )
{
	int len = V_strlen( in );
	numchars = MIN( len, numchars );
	// Make sure it's even
	numchars = ( numchars ) & ~0x1;

	// Must be an even # of input characters (two chars per output byte)
	Assert( numchars >= 2 );

	memset( out, 0x00, maxoutputbytes );

	uint8_t *p;
	int i;

	p = out;
	for ( i = 0; 
		 ( i < numchars ) && ( ( p - out ) < maxoutputbytes ); 
		 i+=2, p++ )
	{
		*p = ( V_nibble( in[i] ) << 4 ) | V_nibble( in[i+1] );		
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *in - 
//			inputbytes - 
//			*out - 
//			outsize - 
//-----------------------------------------------------------------------------
void V_binarytohex( const uint8_t *in, int inputbytes, char *out, int outsize )
{
	Assert( outsize >= 1 );
	char doublet[10];
	int i;

	out[0]=0;

	for ( i = 0; i < inputbytes; i++ )
	{
		unsigned char c = in[i];
		V_snprintf( doublet, sizeof( doublet ), "%02x", c );
		V_strncat( out, doublet, outsize, COPY_ALL_CHARACTERS );
	}
}

#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')


//-----------------------------------------------------------------------------
// Purpose: Extracts the base name of a file (no path, no extension, assumes '/' or '\' as path separator)
// Input  : *in - 
//			*out - 
//			maxlen - 
//-----------------------------------------------------------------------------
void V_FileBase( const char *in, char *out, int maxlen )
{
	Assert( maxlen >= 1 );
	Assert( in );
	Assert( out );

	if ( !in || !in[ 0 ] )
	{
		*out = 0;
		return;
	}

	int len, start, end;

	len = V_strlen( in );
	
	// scan backward for '.'
	end = len - 1;
	while ( end&& in[end] != '.' && !PATHSEPARATOR( in[end] ) )
	{
		end--;
	}
	
	if ( in[end] != '.' )		// no '.', copy to end
	{
		end = len-1;
	}
	else 
	{
		end--;					// Found ',', copy to left of '.'
	}

	// Scan backward for '/'
	start = len-1;
	while ( start >= 0 && !PATHSEPARATOR( in[start] ) )
	{
		start--;
	}

	if ( start < 0 || !PATHSEPARATOR( in[start] ) )
	{
		start = 0;
	}
	else 
	{
		start++;
	}

	// Length of new sting
	len = end - start + 1;

	int maxcopy = MIN( len + 1, maxlen );

	// Copy partial string
	V_strncpy( out, &in[start], maxcopy );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *ppath - 
//-----------------------------------------------------------------------------
void V_StripTrailingSlash( char *ppath )
{
	Assert( ppath );

	int len = V_strlen( ppath );
	if ( len > 0 )
	{
		if ( PATHSEPARATOR( ppath[ len - 1 ] ) )
		{
			ppath[ len - 1 ] = 0;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *in - 
//			*out - 
//			outSize - 
//-----------------------------------------------------------------------------
void V_StripExtension( const char *in, char *out, int outSize )
{
	// Find the last dot. If it's followed by a dot or a slash, then it's part of a 
	// directory specifier like ../../somedir/./blah.

	// scan backward for '.'
	int end = V_strlen( in ) - 1;
	while ( end > 0 && in[end] != '.' && !PATHSEPARATOR( in[end] ) )
	{
		--end;
	}

	if (end > 0 && !PATHSEPARATOR( in[end] ) && end < outSize)
	{
		int nChars = MIN( end, outSize-1 );
		if ( out != in )
		{
			memcpy( out, in, nChars );
		}
		out[nChars] = 0;
	}
	else
	{
		// nothing found
		if ( out != in )
		{
			V_strncpy( out, in, outSize );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *path - 
//			*extension - 
//			pathStringLength - 
//-----------------------------------------------------------------------------
void V_DefaultExtension( char *path, const char *extension, int pathStringLength )
{
	Assert( path );
	Assert( pathStringLength >= 1 );
	Assert( extension );

	char    *src;

	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	src = path + V_strlen(path) - 1;

	while ( !PATHSEPARATOR( *src ) && ( src > path ) )
	{
		if (*src == '.')
		{
			// it has an extension
			return;                 
		}
		src--;
	}

	// Concatenate the desired extension
	char pTemp[MAX_PATH]{0};
	if ( extension[0] != '.' )
	{
		pTemp[0] = '.';
		V_strncpy( &pTemp[1], extension, sizeof(pTemp) - 1 );
		extension = pTemp;
	}
	V_strncat( path, extension, pathStringLength, COPY_ALL_CHARACTERS );
}

//-----------------------------------------------------------------------------
// Purpose: Force extension...
// Input  : *path - 
//			*extension - 
//			pathStringLength - 
//-----------------------------------------------------------------------------
void V_SetExtension( char *path, const char *extension, int pathStringLength )
{
	V_StripExtension( path, path, pathStringLength );

	// This fails if the filename has multiple extensions (i.e. "filename.360.vtex_c").
	//V_DefaultExtension( path, extension, pathStringLength );

	// Concatenate the desired extension
	char pTemp[MAX_PATH]{0};
	if ( extension[0] != '.' )
	{
		pTemp[0] = '.';
		V_strncpy( &pTemp[1], extension, sizeof(pTemp) - 1 );
		extension = pTemp;
	}
	V_strncat( path, extension, pathStringLength, COPY_ALL_CHARACTERS );
}

//-----------------------------------------------------------------------------
// Purpose: Remove final filename from string
// Input  : *path - 
// Output : void  V_StripFilename
//-----------------------------------------------------------------------------
void  V_StripFilename (char *path)
{
	int             length;

	length = V_strlen( path )-1;
	if ( length <= 0 )
		return;

	while ( length > 0 && 
		!PATHSEPARATOR( path[length] ) )
	{
		length--;
	}

	path[ length ] = 0;
}

#ifdef _WIN32
#define CORRECT_PATH_SEPARATOR '\\'
#define INCORRECT_PATH_SEPARATOR '/'
#elif POSIX
#define CORRECT_PATH_SEPARATOR '/'
#define INCORRECT_PATH_SEPARATOR '\\'
#endif

//-----------------------------------------------------------------------------
// Purpose: Changes all '/' or '\' characters into separator
// Input  : *pname - 
//			separator - 
//-----------------------------------------------------------------------------
void V_FixSlashes( char *pname, char separator /* = CORRECT_PATH_SEPARATOR */ )
{
	while ( *pname )
	{
		if ( *pname == INCORRECT_PATH_SEPARATOR || *pname == CORRECT_PATH_SEPARATOR )
		{
			*pname = separator;
		}
		pname++;
	}
}


//-----------------------------------------------------------------------------
// Purpose: This function fixes cases of filenames like materials\\blah.vmt or somepath\otherpath\\ and removes the extra double slash.
//-----------------------------------------------------------------------------
void V_FixDoubleSlashes( char *pStr )
{
	int len = V_strlen( pStr );

	for ( int i=1; i < len-1; i++ )
	{
		if ( (pStr[i] == '/' || pStr[i] == '\\') && (pStr[i+1] == '/' || pStr[i+1] == '\\') )
		{
			// This means there's a double slash somewhere past the start of the filename. That 
			// can happen in Hammer if they use a material in the root directory. You'll get a filename 
			// that looks like 'materials\\blah.vmt'
			V_memmove( &pStr[i], &pStr[i+1], len - i );
			--len;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Strip off the last directory from dirName
// Input  : *dirName - 
//			maxlen - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool V_StripLastDir( char *dirName, int maxlen )
{
	if( dirName[0] == 0 || 
		!V_stricmp( dirName, "./" ) || 
		!V_stricmp( dirName, ".\\" ) )
		return false;
	
	int len = V_strlen( dirName );

	Assert( len < maxlen );

	// skip trailing slash
	if ( PATHSEPARATOR( dirName[len-1] ) )
	{
		len--;
	}

	bool bHitColon = false;
	while ( len > 0 )
	{
		if ( PATHSEPARATOR( dirName[len-1] ) )
		{
			dirName[len] = 0;
			V_FixSlashes( dirName, CORRECT_PATH_SEPARATOR );
			return true;
		}
		else if ( dirName[len-1] == ':' )
		{
			bHitColon = true;
		}

		len--;
	}

	// If we hit a drive letter, then we're done.
	// Ex: If they passed in c:\, then V_StripLastDir should return "" and false.
	if ( bHitColon )
	{
		dirName[0] = 0;
		return false;
	}

	// Allow it to return an empty string and true. This can happen if something like "tf2/" is passed in.
	// The correct behavior is to strip off the last directory ("tf2") and return true.
	if ( len == 0 && !bHitColon )
	{
		V_snprintf( dirName, maxlen, ".%c", CORRECT_PATH_SEPARATOR );
		return true;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns a pointer to the beginning of the unqualified file name 
//			(no path information)
// Input:	in - file name (may be unqualified, relative or absolute path)
// Output:	pointer to unqualified file name
//-----------------------------------------------------------------------------
const char * V_UnqualifiedFileName( const char * in )
{
	if ( !in || !in[0] )
		return in;

	// back up until the character after the first path separator we find,
	// or the beginning of the string
	const char * out = in + strlen( in ) - 1;
	while ( ( out > in ) && ( !PATHSEPARATOR( *( out-1 ) ) ) )
		out--;
	return out;
}


//-----------------------------------------------------------------------------
// Purpose: Composes a path and filename together, inserting a path separator
//			if need be
// Input:	path - path to use
//			filename - filename to use
//			dest - buffer to compose result in
//			destSize - size of destination buffer
//-----------------------------------------------------------------------------
void V_ComposeFileName( const char *path, const char *filename, char *dest, int destSize )
{
	V_strncpy( dest, path, destSize );
	V_FixSlashes( dest );
	V_AppendSlash( dest, destSize );
	V_strncat( dest, filename, destSize, COPY_ALL_CHARACTERS );
	V_FixSlashes( dest );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *path - 
//			*dest - 
//			destSize - 
// Output : void V_ExtractFilePath
//-----------------------------------------------------------------------------
bool V_ExtractFilePath (const char *path, char *dest, int destSize )
{
	Assert( destSize >= 1 );
	if ( destSize < 1 )
	{
		return false;
	}

	// Last char
	int len = V_strlen(path);
	const char *src = path + (len ? len-1 : 0);

	// back up until a \ or the start
	while ( src != path && !PATHSEPARATOR( *(src-1) ) )
	{
		src--;
	}

	int copysize = MIN( src - path, destSize - 1 );
	memcpy( dest, path, copysize );
	dest[copysize] = 0;

	return copysize != 0 ? true : false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *path - 
//			*dest - 
//			destSize - 
// Output : void V_ExtractFileExtension
//-----------------------------------------------------------------------------
void V_ExtractFileExtension( const char *path, char *dest, int destSize )
{
	*dest = 0;
	const char * extension = V_GetFileExtension( path );
	if (nullptr != extension )
		V_strncpy( dest, extension, destSize );
}



//-----------------------------------------------------------------------------
// Purpose: Returns a pointer to the file extension within a file name string
// Input:	in - file name 
// Output:	pointer to beginning of extension (after the "."), or NULL
//				if there is no extension
//-----------------------------------------------------------------------------
const char *V_GetFileExtension( const char *path )
{
	int len = V_strlen( path );
	if ( len <= 1 )
		return nullptr;

	const char *src = path + len - 1;

	//
	// back up until a . or the start
	//
	while (src != path && *(src-1) != '.' )
		src--;

	// check to see if the '.' is part of a pathname
	if (src == path || PATHSEPARATOR( *src ) )
	{		
		return nullptr;  // no extension
	}

	return src;
}

bool V_RemoveDotSlashes( char *pFilename, char separator )
{
	// Remove '//' or '\\'
	char *pIn = pFilename;
	char *pOut = pFilename;

	// (But skip a leading separator, for leading \\'s in network paths)
	if ( *pIn && PATHSEPARATOR( *pIn ) )
	{
		*pOut = *pIn;
		++pIn;
		++pOut;
	}

	bool bPrevPathSep = false;
	while ( *pIn )
	{
		bool bIsPathSep = PATHSEPARATOR( *pIn );
		if ( !bIsPathSep || !bPrevPathSep )
		{
			*pOut++ = *pIn;
		}
		bPrevPathSep = bIsPathSep;
		++pIn;
	}
	*pOut = 0;

	// Get rid of "./"'s
	pIn = pFilename;
	pOut = pFilename;
	while ( *pIn )
	{
		// The logic on the second line is preventing it from screwing up "../"
		if ( pIn[0] == '.' && PATHSEPARATOR( pIn[1] ) &&
			(pIn == pFilename || pIn[-1] != '.') )
		{
			pIn += 2;
		}
		else
		{
			*pOut = *pIn;
			++pIn;
			++pOut;
		}
	}
	*pOut = 0;

	// Get rid of a trailing "/." (needless).
	int len = V_strlen( pFilename );
	if ( len > 2 && pFilename[len-1] == '.' && PATHSEPARATOR( pFilename[len-2] ) )
	{
		pFilename[len-2] = 0;
	}

	// Each time we encounter a "..", back up until we've read the previous directory name,
	// then get rid of it.
	pIn = pFilename;
	while ( *pIn )
	{
		if ( pIn[0] == '.' && 
			 pIn[1] == '.' && 
			 (pIn == pFilename || PATHSEPARATOR(pIn[-1])) &&	// Preceding character must be a slash.
			 (pIn[2] == 0 || PATHSEPARATOR(pIn[2])) )			// Following character must be a slash or the end of the string.
		{
			char *pEndOfDots = pIn + 2;
			char *pStart = pIn - 2;

			// Ok, now scan back for the path separator that starts the preceding directory.
			while ( 1 )
			{
				if ( pStart < pFilename )
					return false;

				if ( PATHSEPARATOR( *pStart ) )
					break;

				--pStart;
			}

			// Now slide the string down to get rid of the previous directory and the ".."
			if ( pStart == pFilename && PATHSEPARATOR( *pStart ) && *pEndOfDots == 0 )
			{
				// "/dir/.." is the root, not an empty string
				pFilename[1] = 0;
			}
			else
			{
				memmove( pStart, pEndOfDots, strlen( pEndOfDots ) + 1 );
			}

			// Start over.
			pIn = pFilename;
		}
		else
		{
			++pIn;
		}
	}
	
	V_FixSlashes( pFilename, separator );	
	return true;
}


void V_AppendSlash( char *pStr, int strSize, char separator )
{
	int len = V_strlen( pStr );
	if ( len > 0 && !PATHSEPARATOR(pStr[len-1]) )
	{
		if ( len+1 >= strSize )
			Plat_FatalError( "V_AppendSlash: ran out of space on %s.", pStr );
		
		pStr[len] = separator;
		pStr[len+1] = 0;
	}
}


bool V_MakeAbsolutePath( char *pOut, int outLen, const char *pPath, const char *pStartingDir )
{
	if ( V_IsAbsolutePath( pPath ) )
	{
		// pPath is not relative.. just copy it.
		V_strncpy( pOut, pPath, outLen );
	}
	else
	{
		// Make sure the starting directory is absolute..
		if ( pStartingDir && V_IsAbsolutePath( pStartingDir ) )
		{
			V_strncpy( pOut, pStartingDir, outLen );
		}
		else
		{
			if ( !V_GetCurrentDirectory( pOut, outLen ) )
				Plat_FatalError( "V_MakeAbsolutePath: can't get the current directory." );

			if ( pStartingDir )
			{
				V_AppendSlash( pOut, outLen );
				V_strncat( pOut, pStartingDir, outLen, COPY_ALL_CHARACTERS );
			}
		}

		// Concatenate the paths.
		V_AppendSlash( pOut, outLen );
		V_strncat( pOut, pPath, outLen, COPY_ALL_CHARACTERS );
	}

	V_FixSlashes(pOut);

	bool bRet = true;
	if (!V_RemoveDotSlashes(pOut))
	{
		V_strncpy(pOut, pPath, outLen);
		V_FixSlashes(pOut);
		bRet = false;
	}

	return bRet;
}


//-----------------------------------------------------------------------------
// Makes a relative path
//-----------------------------------------------------------------------------
bool V_MakeRelativePath( const char *pFullPath, const char *pDirectory, char *pRelativePath, int nBufLen )
{
	pRelativePath[0] = 0;

	const char *pPath = pFullPath;
	const char *pDir = pDirectory;

	// Strip out common parts of the path
	const char *pLastCommonPath = nullptr;
	const char *pLastCommonDir = nullptr;
	while ( *pPath && ( tolower( *pPath ) == tolower( *pDir ) || 
						( PATHSEPARATOR( *pPath ) && ( PATHSEPARATOR( *pDir ) || (*pDir == 0) ) ) ) )
	{
		if ( PATHSEPARATOR( *pPath ) )
		{
			pLastCommonPath = pPath + 1;
			pLastCommonDir = pDir + 1;
		}
		if ( *pDir == 0 )
		{
			--pLastCommonDir;
			break;
		}
		++pDir; ++pPath;
	}

	// Nothing in common
	if ( !pLastCommonPath )
		return false;

	// For each path separator remaining in the dir, need a ../
	int nOutLen = 0;
	bool bLastCharWasSeparator = true;
	for ( ; *pLastCommonDir; ++pLastCommonDir )
	{
		if ( PATHSEPARATOR( *pLastCommonDir ) )
		{
			pRelativePath[nOutLen++] = '.';
			pRelativePath[nOutLen++] = '.';
			pRelativePath[nOutLen++] = CORRECT_PATH_SEPARATOR;
			bLastCharWasSeparator = true;
		}
		else
		{
			bLastCharWasSeparator = false;
		}
	}

	// Deal with relative paths not specified with a trailing slash
	if ( !bLastCharWasSeparator )
	{
		pRelativePath[nOutLen++] = '.';
		pRelativePath[nOutLen++] = '.';
		pRelativePath[nOutLen++] = CORRECT_PATH_SEPARATOR;
	}

	// Copy the remaining part of the relative path over, fixing the path separators
	for ( ; *pLastCommonPath; ++pLastCommonPath )
	{
		if ( PATHSEPARATOR( *pLastCommonPath ) )
		{
			pRelativePath[nOutLen++] = CORRECT_PATH_SEPARATOR;
		}
		else
		{
			pRelativePath[nOutLen++] = *pLastCommonPath;
		}

		// Check for overflow
		if ( nOutLen == nBufLen - 1 )
			break;
	}

	pRelativePath[nOutLen] = 0;
	return true;
}




//-----------------------------------------------------------------------------
// small helper function shared by lots of modules
//-----------------------------------------------------------------------------
bool V_IsAbsolutePath( const char *pStr )
{
	if ( !pStr[0] )
		return false;

#if defined( PLATFORM_WINDOWS )
	if ( !pStr[1] )
		return false;

	bool bIsAbsolute = ( pStr[0] && pStr[1] == ':' ) || 
	  ( ( pStr[0] == '/' || pStr[0] == '\\' ) && ( pStr[1] == '/' || pStr[1] == '\\' ) );
#else
	// POSIX: only a leading '/' is absolute ("C:..." is a valid relative file name, and so is "\\foo").
	bool bIsAbsolute = pStr[0] == '/';
#endif


	
	return bIsAbsolute;
}

//-----------------------------------------------------------------------------
// Fixes up a file name, removing dot slashes, fixing slashes, converting to lowercase, etc.
//-----------------------------------------------------------------------------
void V_FixupPathName( char *pOut, int nOutLen, const char *pPath )
{
	V_strncpy( pOut, pPath, nOutLen );
	V_FixSlashes( pOut );
	V_RemoveDotSlashes( pOut );
	V_FixDoubleSlashes( pOut );
	V_strlower( pOut );
}


// Copies at most nCharsToCopy bytes from pIn into pOut.
// Returns false if it would have overflowed pOut's buffer.
static bool CopyToMaxChars( char *pOut, int outSize, const char *pIn, int nCharsToCopy )
{
	if ( outSize == 0 )
		return false;

	int iOut = 0;
	while ( *pIn && nCharsToCopy > 0 )
	{
		if ( iOut == (outSize-1) )
		{
			pOut[iOut] = 0;
			return false;
		}
		pOut[iOut] = *pIn;
		++iOut;
		++pIn;
		--nCharsToCopy;
	}
	
	pOut[iOut] = 0;
	return true;
}


// Returns true if it completed successfully.
// If it would overflow pOut, it fills as much as it can and returns false.
bool V_StrSubst( 
	const char *pIn, 
	const char *pMatch,
	const char *pReplaceWith,
	char *pOut,
	int outLen,
	bool bCaseSensitive
	)
{
	int replaceFromLen = V_strlen( pMatch );
	int replaceToLen = V_strlen( pReplaceWith );

	const char *pInStart = pIn;
	char *pOutPos = pOut;
	pOutPos[0] = 0;

	while ( 1 )
	{
		int nRemainingOut = outLen - (pOutPos - pOut);

		const char *pTestPos = ( bCaseSensitive ? V_strstr( pInStart, pMatch ) : V_stristr( pInStart, pMatch ) );
		if ( pTestPos )
		{
			// Found an occurence of pMatch. First, copy whatever leads up to the string.
			int copyLen = pTestPos - pInStart;
			if ( !CopyToMaxChars( pOutPos, nRemainingOut, pInStart, copyLen ) )
				return false;
			
			// Did we hit the end of the output string?
			if ( copyLen > nRemainingOut-1 )
				return false;

			pOutPos += V_strlen( pOutPos );
			nRemainingOut = outLen - (pOutPos - pOut);

			// Now add the replacement string.
			if ( !CopyToMaxChars( pOutPos, nRemainingOut, pReplaceWith, replaceToLen ) )
				return false;

			pInStart += copyLen + replaceFromLen;
			pOutPos += replaceToLen;			
		}
		else
		{
			// We're at the end of pIn. Copy whatever remains and get out.
			int copyLen = V_strlen( pInStart );
			V_strncpy( pOutPos, pInStart, nRemainingOut );
			return ( copyLen <= nRemainingOut-1 );
		}
	}
}


char* AllocString( const char *pStr, int nMaxChars )
{
	int allocLen;
	if ( nMaxChars == -1 )
		allocLen = V_strlen( pStr ) + 1;
	else
		allocLen = MIN( V_strlen(pStr), nMaxChars ) + 1;

	char *pOut = new char[allocLen];
	V_strncpy( pOut, pStr, allocLen );
	return pOut;
}




void V_SplitString2( const char *pString, const char **pSeparators, int nSeparators, CUtlVector<char*> &outStrings )
{
	// We must pass in an empty outStrings buffer or call outStrings.PurgeAndDeleteElements between
	// calls.
	Assert( outStrings.Count() == 0 );
	// This will make outStrings empty but it will not free any memory that the elements were pointing to.
	outStrings.Purge();
	const char *pCurPos = pString;
	while ( 1 )
	{
		int iFirstSeparator = -1;
		const char *pFirstSeparator = nullptr;
		for ( int i=0; i < nSeparators; i++ )
		{
			const char *pTest = V_stristr( pCurPos, pSeparators[i] );
			if ( pTest && (!pFirstSeparator || pTest < pFirstSeparator) )
			{
				iFirstSeparator = i;
				pFirstSeparator = pTest;
			}
		}

		if ( pFirstSeparator )
		{
			// Split on this separator and continue on.
			int separatorLen = V_strlen( pSeparators[iFirstSeparator] );
			if ( pFirstSeparator > pCurPos )
			{
				outStrings.AddToTail( AllocString( pCurPos, pFirstSeparator-pCurPos ) );
			}

			pCurPos = pFirstSeparator + separatorLen;
		}
		else
		{
			// Copy the rest of the string
			if ( V_strlen( pCurPos ) )
			{
				outStrings.AddToTail( AllocString( pCurPos, -1 ) );
			}
			return;
		}
	}
}


void V_SplitString( const char *pString, const char *pSeparator, CUtlVector<char*> &outStrings )
{
	V_SplitString2( pString, &pSeparator, 1, outStrings );
}

void V_SplitString2(const char *pString, const char * const *pSeparators, int nSeparators, CUtlVector<CUtlString> &outStrings, bool bIncludeEmptyStrings)
{
	outStrings.Purge();
	const char *pCurPos = pString;
	for (;;)
	{
		int iFirstSeparator = -1;
		const char *pFirstSeparator = nullptr;
		for (int i = 0; i < nSeparators; i++)
		{
			const char *pTest = V_stristr_fast(pCurPos, pSeparators[i]);
			if (pTest && (!pFirstSeparator || pTest < pFirstSeparator))
			{
				iFirstSeparator = i;
				pFirstSeparator = pTest;
			}
		}

		if (pFirstSeparator)
		{
			// Split on this separator and continue on.
			int separatorLen = (int)strlen(pSeparators[iFirstSeparator]);
			if (pFirstSeparator > pCurPos || (pFirstSeparator == pCurPos && bIncludeEmptyStrings))
			{
				outStrings[outStrings.AddToTail()].SetDirect(pCurPos, (int)(pFirstSeparator - pCurPos));
			}

			pCurPos = pFirstSeparator + separatorLen;
		}
		else
		{
			// Copy the rest of the string, if there's anything there
			if (pCurPos[0] != 0)
			{
				outStrings[outStrings.AddToTail()].Set(pCurPos);
			}
			return;
		}
	}
}

void V_SplitString(const char *pString, const char *pSeparator, CUtlVector<CUtlString> &outStrings, bool bIncludeEmptyStrings)
{
	V_SplitString2(pString, &pSeparator, 1, outStrings, bIncludeEmptyStrings);
}





bool V_GetCurrentDirectory( char *pOut, int maxLen )
{
	std::error_code ec;
	const std::string dir = vstd::paths::to_utf8( std::filesystem::current_path( ec ) );
	if ( ec || maxLen <= 0 || dir.size() >= (size_t)maxLen )
		return false;
	memcpy( pOut, dir.c_str(), dir.size() + 1 );
	return true;
}



// This function takes a slice out of pStr and stores it in pOut.
// It follows the Python slice convention:
// Negative numbers wrap around the string (-1 references the last character).
// Numbers are clamped to the end of the string.
void V_StrSlice( const char *pStr, int firstChar, int lastCharNonInclusive, char *pOut, int outSize )
{
	if ( outSize == 0 )
		return;
	
	int length = V_strlen( pStr );

	// Fixup the string indices.
	if ( firstChar < 0 )
	{
		firstChar = length - (-firstChar % length);
	}
	else if ( firstChar >= length )
	{
		pOut[0] = 0;
		return;
	}

	if ( lastCharNonInclusive < 0 )
	{
		lastCharNonInclusive = length - (-lastCharNonInclusive % length);
	}
	else if ( lastCharNonInclusive > length )
	{
		lastCharNonInclusive %= length;
	}

	if ( lastCharNonInclusive <= firstChar )
	{
		pOut[0] = 0;
		return;
	}

	int copyLen = lastCharNonInclusive - firstChar;
	if ( copyLen <= (outSize-1) )
	{
		memcpy( pOut, &pStr[firstChar], copyLen );
		pOut[copyLen] = 0;
	}
	else
	{
		memcpy( pOut, &pStr[firstChar], outSize-1 );
		pOut[outSize-1] = 0;
	}
}


void V_StrLeft( const char *pStr, int nChars, char *pOut, int outSize )
{
	if ( nChars == 0 )
	{
		if ( outSize != 0 )
			pOut[0] = 0;

		return;
	}

	V_StrSlice( pStr, 0, nChars, pOut, outSize );
}


void V_StrRight( const char *pStr, int nChars, char *pOut, int outSize )
{
	int len = V_strlen( pStr );
	if ( nChars >= len )
	{
		V_strncpy( pOut, pStr, outSize );
	}
	else
	{
		V_StrSlice( pStr, -nChars, V_strlen( pStr ), pOut, outSize );
	}
}

//-----------------------------------------------------------------------------
// Convert multibyte to wchar + back
//-----------------------------------------------------------------------------
void V_strtowcs( const char *pString, int nInSize, wchar_t *pWString, int nOutSizeInBytes )
{
	Assert( nOutSizeInBytes >= sizeof(pWString[0]) );
#ifdef _WIN32
	int nOutSizeInChars = nOutSizeInBytes / sizeof(pWString[0]);
	int result = MultiByteToWideChar( CP_UTF8, 0, pString, nInSize, pWString, nOutSizeInChars );
	// If the string completely fails to fit then MultiByteToWideChar will return 0.
	// If the string exactly fits but with no room for a null-terminator then MultiByteToWideChar
	// will happily fill the buffer and omit the null-terminator, returning nOutSizeInChars.
	// Either way we need to return an empty string rather than a bogus and possibly not
	// null-terminated result.
	if ( result <= 0 || result >= nOutSizeInChars )
	{
		// If nInSize includes the null-terminator then a result of nOutSizeInChars is
		// legal. We check this by seeing if the last character in the output buffer is
		// a zero.
		if ( result == nOutSizeInChars && pWString[ nOutSizeInChars - 1 ] == 0)
		{
			// We're okay! Do nothing.
		}
		else
		{
			// The string completely to fit. Null-terminate the buffer.
			*pWString = L'\0';
		}
	}
	else
	{
		// We have successfully converted our string. Now we need to null-terminate it, because
		// MultiByteToWideChar will only do that if nInSize includes the source null-terminator!
		pWString[ result ] = 0;
	}
#elif POSIX
	if ( mbstowcs( pWString, pString, nOutSizeInBytes / sizeof(pWString[0]) ) <= 0 )
	{
		*pWString = 0;
	}
#endif
}

void V_wcstostr( const wchar_t *pWString, int nInSize, char *pString, int nOutSizeInChars )
{
#ifdef _WIN32
	int result = WideCharToMultiByte( CP_UTF8, 0, pWString, nInSize, pString, nOutSizeInChars, NULL, NULL );
	// If the string completely fails to fit then MultiByteToWideChar will return 0.
	// If the string exactly fits but with no room for a null-terminator then MultiByteToWideChar
	// will happily fill the buffer and omit the null-terminator, returning nOutSizeInChars.
	// Either way we need to return an empty string rather than a bogus and possibly not
	// null-terminated result.
	if ( result <= 0 || result >= nOutSizeInChars )
	{
		// If nInSize includes the null-terminator then a result of nOutSizeInChars is
		// legal. We check this by seeing if the last character in the output buffer is
		// a zero.
		if ( result == nOutSizeInChars && pWString[ nOutSizeInChars - 1 ] == 0)
		{
			// We're okay! Do nothing.
		}
		else
		{
			*pString = '\0';
		}
	}
	else
	{
		// We have successfully converted our string. Now we need to null-terminate it, because
		// MultiByteToWideChar will only do that if nInSize includes the source null-terminator!
		pString[ result ] = '\0';
	}
#elif POSIX
	if ( wcstombs( pString, pWString, nOutSizeInChars ) <= 0 )
	{
		*pString = '\0';
	}
#endif
}



//--------------------------------------------------------------------------------
// backslashification
//--------------------------------------------------------------------------------

static char s_BackSlashMap[]="\tt\nn\rr\"\"\\\\";


	
static char s_hex[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};


// The following characters are not allowed to begin a line for Asian language line-breaking
// purposes.  They include the right parenthesis/bracket, space character, period, exclamation, 
// question mark, and a number of language-specific characters for Chinese, Japanese, and Korean
static const wchar_t wszCantBeginLine[] =
{
	0x0020, 0x0021, 0x0025, 0x0029,	0x002c, 0x002e, 0x003a, 0x003b,
	0x003e, 0x003f, 0x005d, 0x007d,	0x00a2, 0x00a8, 0x00b0, 0x00b7, 
	0x00bb,	0x02c7, 0x02c9,	0x2010, 0x2013, 0x2014, 0x2015,	0x2016, 
	0x2019, 0x201d, 0x201e,	0x201f, 0x2020, 0x2021, 0x2022,	0x2025, 
	0x2026, 0x2027, 0x203a, 0x203c,	0x2047, 0x2048, 0x2049, 0x2103,
	0x2236, 0x2574, 0x3001, 0x3002,	0x3003, 0x3005, 0x3006, 0x3009,
	0x300b, 0x300d, 0x300f, 0x3011,	0x3015, 0x3017, 0x3019, 0x301b,
	0x301c,	0x301e, 0x301f, 0x303b, 0x3041, 0x3043, 0x3045, 0x3047, 
	0x3049,	0x3063, 0x3083, 0x3085, 0x3087,	0x308e, 0x3095, 0x3096, 
	0x30a0,	0x30a1, 0x30a3, 0x30a5, 0x30a7,	0x30a9, 0x30c3, 0x30e3, 
	0x30e5,	0x30e7, 0x30ee, 0x30f5, 0x30f6,	0x30fb, 0x30fd, 0x30fe, 
	0x30fc,	0x31f0, 0x31f1, 0x31f2, 0x31f3,	0x31f4, 0x31f5, 0x31f6, 
	0x31f7,	0x31f8, 0x31f9, 0x31fa, 0x31fb,	0x31fc, 0x31fd, 0x31fe, 
	0x31ff,	0xfe30, 0xfe31, 0xfe32, 0xfe33,	0xfe36, 0xfe38, 0xfe3a,	
	0xfe3c, 0xfe3e, 0xfe40, 0xfe42, 0xfe44,	0xfe4f, 0xfe50, 0xfe51, 
	0xfe52,	0xfe53, 0xfe54, 0xfe55, 0xfe56,	0xfe57, 0xfe58, 0xfe5a, 
	0xfe5c, 0xfe5e, 0xff01,	0xff02, 0xff05, 0xff07, 0xff09,	0xff0c, 
	0xff0e, 0xff1a, 0xff1b,	0xff1f, 0xff3d, 0xff40, 0xff5c,	0xff5d, 
	0xff5e, 0xff60, 0xff64
};

// The following characters are not allowed to end a line for Asian Language line-breaking
// purposes.  They include left parenthesis/bracket, currency symbols, and an number
// of language-specific characters for Chinese, Japanese, and Korean
static const wchar_t wszCantEndLine[] =
{
	0x0024, 0x0028, 0x002a, 0x003c, 0x005b, 0x005c, 0x007b, 0x00a3,	
	0x00a5, 0x00ab, 0x00ac, 0x00b7, 0x02c6, 0x2018,	0x201c, 0x201f, 
	0x2035, 0x2039, 0x3005, 0x3007,	0x3008, 0x300a, 0x300c, 0x300e, 
	0x3010,	0x3014, 0x3016, 0x3018, 0x301a, 0x301d, 0xfe34, 0xfe35, 
	0xfe37, 0xfe39, 0xfe3b, 0xfe3d, 0xfe3f,	0xfe41, 0xfe43, 0xfe59, 
	0xfe5b,	0xfe5d, 0xff04, 0xff08, 0xff0e,	0xff3b, 0xff5b, 0xff5f, 
	0xffe1,	0xffe5, 0xffe6
};

// Can't break between some repeated punctuation patterns ("--", "...", "<asian period repeated>")
static const wchar_t wszCantBreakRepeated[] =
{
	0x002d, 0x002e, 0x3002
};

#if defined( LINUX )
inline int __cdecl iswascii(wchar_t c) { return ((unsigned)(c) < 0x80); } // not defined in wctype.h on the PS3
#endif





#if defined( LINUX )
extern "C" void qsort_s( void *base, size_t num, size_t width, int (*compare )(void *, const void *, const void *), void * context );
#endif

void V_qsort_s( void *base, size_t num, size_t width, int ( __cdecl *compare )(void *, const void *, const void *), void * context ) 
{
#if defined OSX
	// the arguments are swapped 'round on the mac - awesome, huh?
	return qsort_r( base, num, width, context, compare );
#elif defined LINUX
	// FIXME: still not finding qsort_s, even though it's defined in qsort_s.cpp
	// What's up with that?
	return;
#else
	return qsort_s( base, num, width, compare, context );
#endif
}

class CBoyerMooreSearch 
{
public:
	explicit CBoyerMooreSearch( const uint8_t *pNeedle, int nNeedleSize );

	int Search( const uint8_t *pHayStack, int nHayStackLength );

private:
	int m_JumpTable[256];
	int m_nNeedleSize;
	const uint8_t *m_pNeedle;
};

CBoyerMooreSearch::CBoyerMooreSearch( const uint8_t *pNeedle, int nNeedleSize )
{
	m_pNeedle = pNeedle;
	m_nNeedleSize = nNeedleSize;

	int i = 0;

	// All jumps by size of search string by default
	for ( i = 0; i < 256; ++i )
	{
		m_JumpTable[ i ] = m_nNeedleSize;
	}

	// Now for each character in the needle, if it matches, we jump by less on failure
	for ( i = 0; i < m_nNeedleSize - 1; ++i )
	{
		m_JumpTable[m_pNeedle[i]] = m_nNeedleSize - i - 1;
	}
}

int CBoyerMooreSearch::Search( const uint8_t *pHayStack, int nHayStackLength )
{
	if ( m_nNeedleSize > nHayStackLength )
	{
		return -1;
	}

	int k = m_nNeedleSize - 1;
	while ( k < nHayStackLength ) 
	{
		int j = m_nNeedleSize - 1;
		int i = k;
		while ( j >= 0 && 
			pHayStack[i] == m_pNeedle[j] ) 
		{
			j--;
			i--;
		}
		if (j == -1)
		{
			return i + 1;
		}
		k += m_JumpTable[ pHayStack[ k ] ];
	}

	return -1;
}


#ifdef WIN32
// Win32 CRT doesn't support the full range of UChar32, has no extended planes
inline int V_iswspace( int c ) { return ( c <= 0xFFFF ) ? iswspace( (wint_t)c ) : 0; }
#else
#define V_iswspace(x) iswspace(x)
#endif



struct HtmlEntity_t
{
	unsigned short uCharCode;
	const char *pchEntity;
	int nEntityLength;
};

const static HtmlEntity_t g_BasicHTMLEntities[] = {
		{ '"', "&quot;", 6 },
		{ '\'', "&#039;", 6 },
		{ '<', "&lt;", 4 },
		{ '>', "&gt;", 4 },
		{ '&', "&amp;", 5 },
		{ 0, nullptr, 0 } // sentinel for end of array
};

const static HtmlEntity_t g_WhitespaceEntities[] = {
		{ ' ', "&nbsp;", 6 },
		{ '\n', "<br>", 4 },
		{ 0, nullptr, 0 } // sentinel for end of array
};


struct Tier1FullHTMLEntity_t
{
	uchar32 uCharCode;
	const char *pchEntity;
	int nEntityLength;
};


#pragma warning( push )
#pragma warning( disable : 4428 ) // universal-character-name encountered in source
const Tier1FullHTMLEntity_t g_Tier1_FullHTMLEntities[] =
{
	{ L'"', "&quot;", 6 },
	{ L'\'', "&apos;", 6 },
	{ L'&', "&amp;", 5 },
	{ L'<', "&lt;", 4 },
	{ L'>', "&gt;", 4 },
	{ L' ', "&nbsp;", 6 },
	{ L'\u2122', "&trade;", 7 },
	{ L'\u00A9', "&copy;", 6 },
	{ L'\u00AE', "&reg;", 5 },
	{ L'\u2013', "&ndash;", 7 },
	{ L'\u2014', "&mdash;", 7 },
	{ L'\u20AC', "&euro;", 6 },
	{ L'\u00A1', "&iexcl;", 7 },
	{ L'\u00A2', "&cent;", 6 },
	{ L'\u00A3', "&pound;", 7 },
	{ L'\u00A4', "&curren;", 8 },
	{ L'\u00A5', "&yen;", 5 },
	{ L'\u00A6', "&brvbar;", 8 },
	{ L'\u00A7', "&sect;", 6 },
	{ L'\u00A8', "&uml;", 5 },
	{ L'\u00AA', "&ordf;", 6 },
	{ L'\u00AB', "&laquo;", 7 },
	{ L'\u00AC', "&not;", 8 },
	{ L'\u00AD', "&shy;", 5 },
	{ L'\u00AF', "&macr;", 6 },
	{ L'\u00B0', "&deg;", 5 },
	{ L'\u00B1', "&plusmn;", 8 },
	{ L'\u00B2', "&sup2;", 6 },
	{ L'\u00B3', "&sup3;", 6 },
	{ L'\u00B4', "&acute;", 7 },
	{ L'\u00B5', "&micro;", 7 },
	{ L'\u00B6', "&para;", 6 },
	{ L'\u00B7', "&middot;", 8 },
	{ L'\u00B8', "&cedil;", 7 },
	{ L'\u00B9', "&sup1;", 6 },
	{ L'\u00BA', "&ordm;", 6 },
	{ L'\u00BB', "&raquo;", 7 },
	{ L'\u00BC', "&frac14;", 8 },
	{ L'\u00BD', "&frac12;", 8 },
	{ L'\u00BE', "&frac34;", 8 },
	{ L'\u00BF', "&iquest;", 8 },
	{ L'\u00D7', "&times;", 7 },
	{ L'\u00F7', "&divide;", 8 },
	{ L'\u00C0', "&Agrave;", 8 },
	{ L'\u00C1', "&Aacute;", 8 },
	{ L'\u00C2', "&Acirc;", 7 },
	{ L'\u00C3', "&Atilde;", 8 },
	{ L'\u00C4', "&Auml;", 6 },
	{ L'\u00C5', "&Aring;", 7 },
	{ L'\u00C6', "&AElig;", 7 },
	{ L'\u00C7', "&Ccedil;", 8 },
	{ L'\u00C8', "&Egrave;", 8 },
	{ L'\u00C9', "&Eacute;", 8 },
	{ L'\u00CA', "&Ecirc;", 7 },
	{ L'\u00CB', "&Euml;", 6 },
	{ L'\u00CC', "&Igrave;", 8 },
	{ L'\u00CD', "&Iacute;", 8 },
	{ L'\u00CE', "&Icirc;", 7 },
	{ L'\u00CF', "&Iuml;", 6 },
	{ L'\u00D0', "&ETH;", 5 },
	{ L'\u00D1', "&Ntilde;", 8 },
	{ L'\u00D2', "&Ograve;", 8 },
	{ L'\u00D3', "&Oacute;", 8 },
	{ L'\u00D4', "&Ocirc;", 7 },
	{ L'\u00D5', "&Otilde;", 8 },
	{ L'\u00D6', "&Ouml;", 6 },
	{ L'\u00D8', "&Oslash;", 8 },
	{ L'\u00D9', "&Ugrave;", 8 },
	{ L'\u00DA', "&Uacute;", 8 },
	{ L'\u00DB', "&Ucirc;", 7 },
	{ L'\u00DC', "&Uuml;", 6 },
	{ L'\u00DD', "&Yacute;", 8 },
	{ L'\u00DE', "&THORN;", 7 },
	{ L'\u00DF', "&szlig;", 7 },
	{ L'\u00E0', "&agrave;", 8 },
	{ L'\u00E1', "&aacute;", 8 },
	{ L'\u00E2', "&acirc;", 7 },
	{ L'\u00E3', "&atilde;", 8 },
	{ L'\u00E4', "&auml;", 6 },
	{ L'\u00E5', "&aring;", 7 },
	{ L'\u00E6', "&aelig;", 7 },
	{ L'\u00E7', "&ccedil;", 8 },
	{ L'\u00E8', "&egrave;", 8 },
	{ L'\u00E9', "&eacute;", 8 },
	{ L'\u00EA', "&ecirc;", 7 },
	{ L'\u00EB', "&euml;", 6 },
	{ L'\u00EC', "&igrave;", 8 },
	{ L'\u00ED', "&iacute;", 8 },
	{ L'\u00EE', "&icirc;", 7 },
	{ L'\u00EF', "&iuml;", 6 },
	{ L'\u00F0', "&eth;", 5 },
	{ L'\u00F1', "&ntilde;", 8 },
	{ L'\u00F2', "&ograve;", 8 },
	{ L'\u00F3', "&oacute;", 8 },
	{ L'\u00F4', "&ocirc;", 7 },
	{ L'\u00F5', "&otilde;", 8 },
	{ L'\u00F6', "&ouml;", 6 },
	{ L'\u00F8', "&oslash;", 8 },
	{ L'\u00F9', "&ugrave;", 8 },
	{ L'\u00FA', "&uacute;", 8 },
	{ L'\u00FB', "&ucirc;", 7 },
	{ L'\u00FC', "&uuml;", 6 },
	{ L'\u00FD', "&yacute;", 8 },
	{ L'\u00FE', "&thorn;", 7 },
	{ L'\u00FF', "&yuml;", 6 },
	{ 0, nullptr, 0 } // sentinel for end of array
};
#pragma warning( pop )




static const char *g_pszSimpleBBCodeReplacements[] = {
	"[b]", "<b>",
	"[/b]", "</b>",
	"[i]", "<i>",
	"[/i]", "</i>",
	"[u]", "<u>",
	"[/u]", "</u>",
	"[s]", "<s>",
	"[/s]", "</s>",
	"[code]", "<pre>",
	"[/code]", "</pre>",
	"[h1]", "<h1>",
	"[/h1]", "</h1>",
	"[list]", "<ul>",
	"[/list]", "</ul>",
	"[*]", "<li>",
	"[/url]", "</a>",
	"[img]", "<img src=\"",
	"[/img]", "\"></img>",
};








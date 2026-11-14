//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once



#include "tier1/utlmemory.h"
#include "tier1/strtools.h"
#include "limits.h"

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t) -1)
#endif

//-----------------------------------------------------------------------------
// Base class, containing simple memory management
//-----------------------------------------------------------------------------
class CUtlBinaryBlock
{
public:
	CUtlBinaryBlock( int growSize = 0, int initSize = 0 );
	~CUtlBinaryBlock()
	{
#ifdef _DEBUG
		m_nActualLength = 0x7BADF00D;
#else
		m_nActualLength = 0;
#endif
	}

	// NOTE: nInitialLength indicates how much of the buffer starts full
	CUtlBinaryBlock( void* pMemory, int nSizeInBytes, int nInitialLength );
	CUtlBinaryBlock( const void* pMemory, int nSizeInBytes );
	
	CUtlBinaryBlock( const CUtlBinaryBlock& src );
	CUtlBinaryBlock &operator=( const CUtlBinaryBlock &src );

#if VALVE_CPP11
	CUtlBinaryBlock( CUtlBinaryBlock&& src );
	CUtlBinaryBlock &operator=( CUtlBinaryBlock&& src );
#endif

	void		Get( void *pValue, int nMaxLen ) const;
	void		Set( const void *pValue, int nLen );
	const void	*Get( ) const;
	void		*Get( );

	unsigned char& operator[]( int i );
	const unsigned char& operator[]( int i ) const;

	int			Length() const;
	void		SetLength( int nLength );	// Undefined memory will result
	bool		IsEmpty() const;
	void		Clear();
	void		Purge();

	bool		IsReadOnly() const;


	// Test for equality
	bool operator==( const CUtlBinaryBlock &src ) const;

private:
	CUtlMemory<unsigned char> m_Memory;
	int m_nActualLength;
};


//-----------------------------------------------------------------------------
// class inlines
//-----------------------------------------------------------------------------

#if VALVE_CPP11
inline CUtlBinaryBlock::CUtlBinaryBlock( CUtlBinaryBlock&& src )
: m_Memory( Move(src.m_Memory) )
, m_nActualLength( src.m_nActualLength )
{
	src.m_nActualLength = 0;
}

inline CUtlBinaryBlock& CUtlBinaryBlock::operator= ( CUtlBinaryBlock&& src )
{
	int length = src.m_nActualLength;
	src.m_nActualLength = 0;

	m_Memory = Move( src.m_Memory );
	m_nActualLength = length;

	return *this;
}
#endif

inline const void *CUtlBinaryBlock::Get( ) const
{
	return m_Memory.Base();
}

inline void *CUtlBinaryBlock::Get( )
{
	return m_Memory.Base();
}

inline int CUtlBinaryBlock::Length() const
{
	return m_nActualLength;
}

inline unsigned char& CUtlBinaryBlock::operator[]( int i )
{
	return m_Memory[i];
}

inline const unsigned char& CUtlBinaryBlock::operator[]( int i ) const
{
	return m_Memory[i];
}

inline bool CUtlBinaryBlock::IsReadOnly() const
{
	return m_Memory.IsReadOnly();
}

inline bool CUtlBinaryBlock::IsEmpty() const
{
	return Length() == 0;
}

inline void CUtlBinaryBlock::Clear()
{
	SetLength( 0 );
}

inline void CUtlBinaryBlock::Purge()
{
	SetLength( 0 );
	m_Memory.Purge();
}


//-----------------------------------------------------------------------------
// Simple string class. 
// NOTE: This is *not* optimal! Use in tools, but not runtime code
//-----------------------------------------------------------------------------
class CUtlString
{
public:
	CUtlString(); // = default;
	CUtlString( const char *pString ); // initialize from c-style string

	// Attaches the string to external memory. Useful for avoiding a copy
	CUtlString( void* pMemory, int nSizeInBytes, int nInitialLength );
	CUtlString( const void* pMemory, int nSizeInBytes );

	// Copy/move constructor/assignment
	// Moves are extremely efficient as the underlying memory is not copied, just the pointers.
	CUtlString( const CUtlString& string ); // = default;
	CUtlString &operator=( const CUtlString &src ); // = default;

#if VALVE_CPP11
	CUtlString( CUtlString&& moveFrom );    // = default;
	CUtlString &operator=( CUtlString&& moveFrom ); // = default;
#endif

	// Also can assign from a regular C-style string
	CUtlString &operator=( const char *src );

	char *Access() { return Get(); }
	const char	*Get( ) const;
	char		*Get();
	char*		GetForModify();

	void Clear() { Set(nullptr); }

	// CUtlString can be used anywhere a c-style string would be required
	// via this implicit conversion
	operator const char*() const;

	// for compatibility switching items from UtlSymbol
	const char  *String() const { return Get(); }

	// Returns strlen
	int			Length() const;
	bool		IsEmpty() const;

	// GS - Added for chromehtml
	bool IsValid()
	{
		return (Length() != 0);
	}

	// Sets the length (used to serialize into the buffer )
	// Note: If nLen != 0, then this adds an extra byte for a null-terminator.	
	void		Set( const char *pValue );
	void		SetLength( int nLen );
	void		Purge();

	void		Swap(CUtlString &src);

	// Case Change
	void		ToUpper();
	void		ToLower( );
	void		Append( const char *pchAddition );
	void		Append(const char *pchAddition, int nMaxChars);
	void		Append(char chAddition) {
		char temp[2] = { chAddition, 0 };
		Append(temp);
	}

	// Strips the trailing slash
	void		StripTrailingSlash();

	char operator[] ( int idx ) const;
	char& operator[] ( int idx );

	// Test for equality
	bool operator==( const CUtlString &src ) const;
	bool operator==( const char *src ) const;
	bool operator!=( const CUtlString &src ) const { return !operator==( src ); }
	bool operator!=( const char *src ) const { return !operator==( src ); }

	// If these are not defined, CUtlString as rhs will auto-convert
	// to const char* and do logical operations on the raw pointers. Ugh.
	inline friend bool operator==( const char *lhs, const CUtlString &rhs ) { return rhs.operator==( lhs ); }
	inline friend bool operator!=( const char *lhs, const CUtlString &rhs ) { return rhs.operator!=( lhs ); }

	CUtlString &operator+=( const CUtlString &rhs );
	CUtlString &operator+=( const char *rhs );
	CUtlString &operator+=( char c );
	CUtlString &operator+=( int rhs );
	CUtlString &operator+=( double rhs );
	
	CUtlString operator+( const char *pOther )const;
	CUtlString operator+( int rhs )const;

	bool MatchesPattern( const CUtlString &Pattern, int nFlags = 0 );		// case SENSITIVE, use * for wildcard in pattern string

#if ! defined(SWIG)
	// Don't let SWIG see the PRINTF_FORMAT_STRING attribute or it will complain.
	int Format( PRINTF_FORMAT_STRING const char *pFormat, ... ) FMTFUNCTION( 2, 3 );
	int FormatV( PRINTF_FORMAT_STRING const char *pFormat, va_list marker );
#else
	int Format( const char *pFormat, ... );
	int FormatV( const char *pFormat, va_list marker );
#endif
	
	void SetDirect( const char *pValue, int nChars );

	// Defining AltArgumentType_t hints that associative container classes should
	// also implement Find/Insert/Remove functions that take const char* params.
	typedef const char *AltArgumentType_t;

	// Take a piece out of the string.
	// If you only specify nStart, it'll go from nStart to the end.
	// You can use negative numbers and it'll wrap around to the start.
	CUtlString Slice( int32_t nStart=0, int32_t nEnd=INT_MAX );

	// Grab a substring starting from the left or the right side.
	CUtlString Left( int32_t nChars );
	CUtlString Right( int32_t nChars );

	CUtlString Remove(char const *pTextToRemove, bool bCaseSensitive) const;

	// Replace all instances of one character with another.
	CUtlString Replace( char cFrom, char cTo );

	/// Replace one string with the other (single pass).  Passing a NULL to pchTo is same as calling Remove
	CUtlString Replace( char const *pchFrom, const char *pchTo, bool bCaseSensitive = false ) const; 

		/// helper func for caseless replace
	CUtlString ReplaceCaseless( char const *pchFrom, const char *pchTo ) const { return Replace( pchFrom, pchTo, false ); }

	void RemoveDotSlashes(char separator = CORRECT_PATH_SEPARATOR);

	// Trim whitespace
	void		TrimLeft( const char *szTargets = "\t\r\n " );
	void		TrimRight( const char *szTargets = "\t\r\n " );
	void		Trim( const char *szTargets = "\t\r\n " );

	// Calls right through to V_MakeAbsolutePath.
	CUtlString AbsPath( const char *pStartingDir= nullptr) const;	

	CUtlString AbsPath(const char *pStartingDir, bool bLowercaseName) const
	{
		CUtlString result = AbsPath(pStartingDir);
		if (bLowercaseName)
		{
			result.ToLower();
		}
		return result;
	}

	// Gets the filename (everything except the path.. c:\a\b\c\somefile.txt -> somefile.txt).
	CUtlString UnqualifiedFilename() const;
	
	// Strips off one directory. Uses V_StripLastDir but strips the last slash also!
	CUtlString DirName();

	// Get a string with the extension removed (with V_StripExtension).
	CUtlString StripExtension() const;

	// Get a string with the filename removed (uses V_UnqualifiedFileName and also strips the last slash)
	CUtlString StripFilename() const;

	// Get a string with the base filename (with V_FileBase).
	CUtlString GetBaseFilename() const;

	// Get a string with the file extension (with V_FileBase).
	CUtlString GetExtension() const;

	// Works like V_ComposeFileName.
	static CUtlString PathJoin( const char *pStr1, const char *pStr2 );

	// These can be used for utlvector sorts.
	static int __cdecl SortCaseInsensitive( const CUtlString *pString1, const CUtlString *pString2 );
	static int __cdecl SortCaseSensitive( const CUtlString *pString1, const CUtlString *pString2 );

	// From Src2

	void FixSlashes( char cSeparator = CORRECT_PATH_SEPARATOR )
	{
		for ( int nLength = Length() - 1; nLength >= 0; nLength-- )
		{
			char *pname = (char*)&m_Storage[ nLength ];
			if ( *pname == INCORRECT_PATH_SEPARATOR || *pname == CORRECT_PATH_SEPARATOR )
			{
				*pname = cSeparator;
			}
		}
	}

	bool IsEqual_CaseSensitive( const char *src ) const
	{
		if ( !src )
		{
			return ( Length() == 0 );
		}
		return ( V_strcmp( Get(), src ) == 0 );
	}

	bool IsEqual_CaseInsensitive(const char *src) const
	{
		if (!src)
		{
			return (Length() == 0);
		}
		return (V_stricmp(Get(), src) == 0);
	}


private:
	CUtlBinaryBlock m_Storage;
};

//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------

#if VALVE_CPP11
inline CUtlString::CUtlString( CUtlString&& moveFrom )
: m_Storage( Move( moveFrom.m_Storage ) )
{
}

inline CUtlString& CUtlString::operator=( CUtlString&& moveFrom )
{
	m_Storage = Move( moveFrom.m_Storage );
	return *this;
}
#endif

inline bool CUtlString::IsEmpty() const
{
	return Length() == 0;
}

inline int __cdecl CUtlString::SortCaseInsensitive( const CUtlString *pString1, const CUtlString *pString2 )
{
	return V_stricmp( pString1->String(), pString2->String() );
}

inline int __cdecl CUtlString::SortCaseSensitive( const CUtlString *pString1, const CUtlString *pString2 )
{
	return V_strcmp( pString1->String(), pString2->String() );
}

inline char CUtlString::operator [] ( int index ) const
{
	return Get()[index];
}

inline char& CUtlString::operator[] ( int index )
{
	return Access()[index];
}

inline CUtlString::operator const char *( ) const
{
	return Get();
}

//-----------------------------------------------------------------------------
// Purpose: Implementation of low-level string functionality for character types.
//-----------------------------------------------------------------------------

template < typename T >
class StringFuncs
{
public:
	static T		*Duplicate( const T *pValue );
	static void		 Copy( T *out_pOut, const T *pIn, int iLength );
	static int		 Compare( const T *pLhs, const T *pRhs );
	static int		 Length( const T *pValue );
	static const T  *FindChar( const T *pStr, const T cSearch );
	static const T	*EmptyString();
};

template < >
class StringFuncs<char>
{
public:
	static char		  *Duplicate( const char *pValue ) { return strdup( pValue ); }
	static void		   Copy( char *out_pOut, const char *pIn, int iLength ) { strncpy( out_pOut, pIn, iLength ); }
	static int		   Compare( const char *pLhs, const char *pRhs ) { return strcmp( pLhs, pRhs ); }
	static int		   Length( const char *pValue ) { return (int)strlen( pValue ); }
	static const char *FindChar( const char *pStr, const char cSearch ) { return strchr( pStr, cSearch ); }
	static const char *EmptyString() { return ""; }
};

template < >
class StringFuncs<wchar_t>
{
public:
	static wchar_t		 *Duplicate( const wchar_t *pValue ) { return wcsdup( pValue ); }
	static void			  Copy( wchar_t *out_pOut, const wchar_t  *pIn, int iLength ) { wcsncpy( out_pOut, pIn, iLength ); }
	static int			  Compare( const wchar_t *pLhs, const wchar_t *pRhs ) { return wcscmp( pLhs, pRhs ); }
	static int			  Length( const wchar_t *pValue ) { return (int) wcslen( pValue ); }
	static const wchar_t *FindChar( const wchar_t *pStr, const wchar_t cSearch ) { return wcschr( pStr, cSearch ); }
	static const wchar_t *EmptyString() { return L""; }
};

//-----------------------------------------------------------------------------
// Dirt-basic auto-release string class. Not intended for manipulation,
// can be stored in a container or forwarded as a functor parameter.
// Note the benefit over CUtlString: sizeof(CUtlConstString) == sizeof(char*).
// Also note: null char* pointers are treated identically to empty strings.
//-----------------------------------------------------------------------------

template < typename T = char >
class CUtlConstStringBase
{
public:
	CUtlConstStringBase() : m_pString(nullptr) {}
	CUtlConstStringBase( const T *pString ) : m_pString(nullptr) { Set( pString ); }
	CUtlConstStringBase( const CUtlConstStringBase& src ) : m_pString(nullptr) { Set( src.m_pString ); }
	~CUtlConstStringBase() { Set(nullptr); }

	void Set( const T *pValue );
	void Clear() { Set(nullptr); }

	const T *Get() const { return m_pString ? m_pString : StringFuncs<T>::EmptyString(); }
	operator const T*() const { return m_pString ? m_pString : StringFuncs<T>::EmptyString(); }

	bool IsEmpty() const { return m_pString == nullptr; } // Note: empty strings are never stored by Set

	int Compare( const T *rhs ) const;

	// Logical ops
	bool operator<( const T *rhs ) const { return Compare( rhs ) < 0; }
	bool operator==( const T *rhs ) const { return Compare( rhs ) == 0; }
	bool operator!=( const T *rhs ) const { return Compare( rhs ) != 0; }
	bool operator<( const CUtlConstStringBase &rhs ) const { return Compare( rhs.m_pString ) < 0; }
	bool operator==( const CUtlConstStringBase &rhs ) const { return Compare( rhs.m_pString ) == 0; }
	bool operator!=( const CUtlConstStringBase &rhs ) const { return Compare( rhs.m_pString ) != 0; }

	// If these are not defined, CUtlConstString as rhs will auto-convert
	// to const char* and do logical operations on the raw pointers. Ugh.
	inline friend bool operator<( const T *lhs, const CUtlConstStringBase &rhs ) { return rhs.Compare( lhs ) > 0; }
	inline friend bool operator==( const T *lhs, const CUtlConstStringBase &rhs ) { return rhs.Compare( lhs ) == 0; }
	inline friend bool operator!=( const T *lhs, const CUtlConstStringBase &rhs ) { return rhs.Compare( lhs ) != 0; }

	CUtlConstStringBase &operator=( const T *src ) { Set( src ); return *this; }
	CUtlConstStringBase &operator=( const CUtlConstStringBase &src ) { Set( src.m_pString ); return *this; }

	// Defining AltArgumentType_t is a hint to containers that they should
	// implement Find/Insert/Remove functions that take const char* params.
	typedef const T *AltArgumentType_t;

protected:
	const T *m_pString;
};

template < typename T >
void CUtlConstStringBase<T>::Set( const T *pValue )
{
	if ( pValue != m_pString )
	{
		free( ( void* ) m_pString );
		m_pString = pValue && pValue[0] ? StringFuncs<T>::Duplicate( pValue ) : NULL;
	}
}

template < typename T >
int CUtlConstStringBase<T>::Compare( const T *rhs ) const
{
	if ( m_pString )
	{
		if ( rhs )
			return StringFuncs<T>::Compare( m_pString, rhs );
		else
			return 1;
	}
	else
	{
		if ( rhs )
			return -1;
		else
			return 0;
	}
}

inline CUtlString CUtlString::operator+( const char *pOther )const
{
	CUtlString s = *this;
	s += pOther;
	return s;
}



typedef	CUtlConstStringBase<char>		CUtlConstString;
typedef	CUtlConstStringBase<wchar_t>	CUtlConstWideString;


int V_vscprintf(const char *format, va_list argptr);

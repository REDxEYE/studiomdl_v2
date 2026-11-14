//========= Copyright (c) Valve Corporation, All rights reserved. =============
//
// Purpose: CUtlBinaryBlock and CUtlString implementation
//
//=============================================================================

#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include "tier1/strtools.h"
#include <cctype>

// NOTE: This has to be the last file included!


static const int64_t k_nMillion = 1000000;

//-----------------------------------------------------------------------------
// Purpose: Helper: Find s substring
//-----------------------------------------------------------------------------
static ptrdiff_t IndexOf(const char *pstrToSearch, const char *pstrTarget)
{
	const char *pstrHit = V_strstr(pstrToSearch, pstrTarget);
	if (pstrHit == nullptr)
	{
		return -1;	// Not found.
	}
	return (pstrHit - pstrToSearch);
}


//-----------------------------------------------------------------------------
// Purpose: Helper: kill all whitespace.
//-----------------------------------------------------------------------------
static size_t RemoveWhitespace(char *pszString)
{
	if (pszString == nullptr)
		return 0;

	char *pstrDest = pszString;
	size_t cRemoved = 0;
	for (char *pstrWalker = pszString; *pstrWalker != 0; pstrWalker++)
	{
		if (!V_isspace((unsigned char)*pstrWalker))
		{
			*pstrDest = *pstrWalker;
			pstrDest++;
		}
		else
			cRemoved += 1;
	}
	*pstrDest = 0;

	return cRemoved;
}

int V_vscprintf(const char *format, va_list params)
{
#ifdef _WIN32
	return _vscprintf(format, params);
#else
	return vsnprintf(nullptr, 0, format, params);
#endif
}

//-----------------------------------------------------------------------------
// Base class, containing simple memory management
//-----------------------------------------------------------------------------
CUtlBinaryBlock::CUtlBinaryBlock( int growSize, int initSize ) 
{
	m_Memory.Init( growSize, initSize );

	m_nActualLength = 0;
}

CUtlBinaryBlock::CUtlBinaryBlock( void* pMemory, int nSizeInBytes, int nInitialLength ) : m_Memory( (unsigned char*)pMemory, nSizeInBytes )
{
	m_nActualLength = nInitialLength;
}

CUtlBinaryBlock::CUtlBinaryBlock( const void* pMemory, int nSizeInBytes ) : m_Memory( (const unsigned char*)pMemory, nSizeInBytes )
{
	m_nActualLength = nSizeInBytes;
}

CUtlBinaryBlock::CUtlBinaryBlock( const CUtlBinaryBlock& src )
{
	Set( src.Get(), src.Length() );
}

void CUtlBinaryBlock::Get( void *pValue, int nLen ) const
{
	Assert( nLen > 0 );
	if ( m_nActualLength < nLen )
	{
		nLen = m_nActualLength;
	}

	if ( nLen > 0 )
	{
		memcpy( pValue, m_Memory.Base(), nLen );
	}
}

void CUtlBinaryBlock::SetLength( int nLength )
{
	Assert( !m_Memory.IsReadOnly() );

	m_nActualLength = nLength;
	if ( nLength > m_Memory.NumAllocated() )
	{
		int nOverFlow = nLength - m_Memory.NumAllocated();
		m_Memory.Grow( nOverFlow );

		// If the reallocation failed, clamp length
		if ( nLength > m_Memory.NumAllocated() )
		{
			m_nActualLength = m_Memory.NumAllocated();
		}
	}

#ifdef _DEBUG
	if ( m_Memory.NumAllocated() > m_nActualLength )
	{
		memset( ( ( char * )m_Memory.Base() ) + m_nActualLength, 0xEB, m_Memory.NumAllocated() - m_nActualLength );
	}
#endif
}


void CUtlBinaryBlock::Set( const void *pValue, int nLen )
{
	Assert( !m_Memory.IsReadOnly() );

	if ( !pValue )
	{
		nLen = 0;
	}

	SetLength( nLen );

	if ( m_nActualLength )
	{
		if ( ( ( const char * )m_Memory.Base() ) >= ( ( const char * )pValue ) + nLen ||
			 ( ( const char * )m_Memory.Base() ) + m_nActualLength <= ( ( const char * )pValue ) )
		{
			memcpy( m_Memory.Base(), pValue, m_nActualLength );
		}
		else
		{
			memmove( m_Memory.Base(), pValue, m_nActualLength );
		}
	}
}


CUtlBinaryBlock &CUtlBinaryBlock::operator=( const CUtlBinaryBlock &src )
{
	Assert( !m_Memory.IsReadOnly() );
	Set( src.Get(), src.Length() );
	return *this;
}


bool CUtlBinaryBlock::operator==( const CUtlBinaryBlock &src ) const
{
	if ( src.Length() != Length() )
		return false;

	return !memcmp( src.Get(), Get(), Length() );
}


//-----------------------------------------------------------------------------
// Simple string class. 
//-----------------------------------------------------------------------------
CUtlString::CUtlString()
{
}

CUtlString::CUtlString( const char *pString )
{
	Set( pString );
}

CUtlString::CUtlString( const CUtlString& string )
{
	Set( string.Get() );
}

// Attaches the string to external memory. Useful for avoiding a copy
CUtlString::CUtlString( void* pMemory, int nSizeInBytes, int nInitialLength ) : m_Storage( pMemory, nSizeInBytes, nInitialLength )
{
}

CUtlString::CUtlString( const void* pMemory, int nSizeInBytes ) : m_Storage( pMemory, nSizeInBytes )
{
}


//-----------------------------------------------------------------------------
// Purpose: Set directly and don't look for a null terminator in pValue.
//-----------------------------------------------------------------------------
void CUtlString::SetDirect( const char *pValue, int nChars )
{
	if ( nChars > 0 )
	{
		m_Storage.SetLength( nChars+1 );
		m_Storage.Set( pValue, nChars+1 );
		m_Storage[nChars] = 0;
	}
	else
	{
		m_Storage.SetLength( 0 );
	}
}


void CUtlString::Set( const char *pValue )
{
	Assert( !m_Storage.IsReadOnly() );
	int nLen = pValue ? Q_strlen(pValue) + 1 : 0;
	m_Storage.Set( pValue, nLen );
}


// Returns strlen
int CUtlString::Length() const
{
	return m_Storage.Length() ? m_Storage.Length() - 1 : 0;
}

// Sets the length (used to serialize into the buffer )
void CUtlString::SetLength( int nLen )
{
	Assert( !m_Storage.IsReadOnly() );

	// Add 1 to account for the NULL
	m_Storage.SetLength( nLen > 0 ? nLen + 1 : 0 );
}

const char *CUtlString::Get( ) const
{
	if ( m_Storage.Length() == 0 )
	{
		return "";
	}

	return reinterpret_cast< const char* >( m_Storage.Get() );
}

char *CUtlString::Get()
{
	Assert( !m_Storage.IsReadOnly() );

	if ( m_Storage.Length() == 0 )
	{
		// In general, we optimise away small mallocs for empty strings
		// but if you ask for the non-const bytes, they must be writable
		// so we can't return "" here, like we do for the const version - jd
		m_Storage.SetLength( 1 );
		m_Storage[ 0 ] = '\0';
	}

	return reinterpret_cast< char* >( m_Storage.Get() );
}

char *CUtlString::GetForModify()
{
	return Get();
}

void CUtlString::Purge()
{
	m_Storage.Purge();
}

void CUtlString::ToUpper()
{
	for (int nLength = Length() - 1; nLength >= 0; nLength--)
	{
		m_Storage[nLength] = toupper(m_Storage[nLength]);
	}
}

void CUtlString::ToLower()
{
	for( int nLength = Length() - 1; nLength >= 0; nLength-- )
	{
		m_Storage[ nLength ] = tolower( m_Storage[ nLength ] );
	}
}


CUtlString &CUtlString::operator=( const CUtlString &src )
{
	Assert( !m_Storage.IsReadOnly() );
	m_Storage = src.m_Storage;
	return *this;
}

CUtlString &CUtlString::operator=( const char *src )
{
	Assert( !m_Storage.IsReadOnly() );
	Set( src );
	return *this;
}

bool CUtlString::operator==( const CUtlString &src ) const
{
	return m_Storage == src.m_Storage;
}

bool CUtlString::operator==( const char *src ) const
{
	return ( strcmp( Get(), src ) == 0 );
}

CUtlString &CUtlString::operator+=( const CUtlString &rhs )
{
	Assert( !m_Storage.IsReadOnly() );

	const int lhsLength( Length() );
	const int rhsLength( rhs.Length() );
	const int requestedLength( lhsLength + rhsLength );

	SetLength( requestedLength );
	const int allocatedLength( Length() );
	const int copyLength( allocatedLength - lhsLength < rhsLength ? allocatedLength - lhsLength : rhsLength );
	memcpy( Get() + lhsLength, rhs.Get(), copyLength );
	m_Storage[ allocatedLength ] = '\0';

	return *this;
}

CUtlString &CUtlString::operator+=( const char *rhs )
{
	Assert( !m_Storage.IsReadOnly() );

	const int lhsLength( Length() );
	const int rhsLength( Q_strlen( rhs ) );
	const int requestedLength( lhsLength + rhsLength );

	SetLength( requestedLength );
	const int allocatedLength( Length() );
	const int copyLength( allocatedLength - lhsLength < rhsLength ? allocatedLength - lhsLength : rhsLength );
	memcpy( Get() + lhsLength, rhs, copyLength );
	m_Storage[ allocatedLength ] = '\0';

	return *this;
}

CUtlString &CUtlString::operator+=( char c )
{
	Assert( !m_Storage.IsReadOnly() );

	int nLength = Length();
	SetLength( nLength + 1 );
	m_Storage[ nLength ] = c;
	m_Storage[ nLength+1 ] = '\0';
	return *this;
}

CUtlString &CUtlString::operator+=( int rhs )
{
	Assert( !m_Storage.IsReadOnly() );
	Assert( sizeof( rhs ) == 4 );

	char tmpBuf[ 12 ];	// Sufficient for a signed 32 bit integer [ -2147483648 to +2147483647 ]
	Q_snprintf( tmpBuf, sizeof( tmpBuf ), "%d", rhs );
	tmpBuf[ sizeof( tmpBuf ) - 1 ] = '\0';

	return operator+=( tmpBuf );
}

CUtlString &CUtlString::operator+=( double rhs )
{
	Assert( !m_Storage.IsReadOnly() );

	char tmpBuf[ 256 ];	// How big can doubles be???  Dunno.
	Q_snprintf( tmpBuf, sizeof( tmpBuf ), "%lg", rhs );
	tmpBuf[ sizeof( tmpBuf ) - 1 ] = '\0';

	return operator+=( tmpBuf );
}

bool CUtlString::MatchesPattern( const CUtlString &Pattern, int nFlags )
{
	const char *pszSource = String();
	const char *pszPattern = Pattern.String();

	return V_StringMatchesPattern( pszSource, pszPattern, nFlags );
}


int CUtlString::Format( const char *pFormat, ... )
{
	va_list marker;

	va_start( marker, pFormat );
	int len = FormatV( pFormat, marker );
	va_end( marker );

	return len;
}

//--------------------------------------------------------------------------------------------------
// This can be called from functions that take varargs.
//--------------------------------------------------------------------------------------------------

int CUtlString::FormatV( const char *pFormat, va_list marker )
{
	char tmpBuf[ 4096 ];	//< Nice big 4k buffer, as much memory as my first computer had, a Radio Shack Color Computer

	//va_start( marker, pFormat );
	int len = V_vsprintf_safe( tmpBuf, pFormat, marker );
	//va_end( marker );
	Set( tmpBuf );
	return len;
}

//-----------------------------------------------------------------------------
// Strips the trailing slash
//-----------------------------------------------------------------------------
void CUtlString::StripTrailingSlash()
{
	if ( IsEmpty() )
		return;

	int nLastChar = Length() - 1;
	char c = m_Storage[ nLastChar ];
	if ( c == '\\' || c == '/' )
	{
		m_Storage[ nLastChar ] = 0;
		m_Storage.SetLength( m_Storage.Length() - 1 );
	}
}

CUtlString CUtlString::Slice( int32_t nStart, int32_t nEnd )
{
	if ( nStart < 0 )
		nStart = Length() - (-nStart % Length());
	else if ( nStart >= Length() )
		nStart = Length();

	if ( nEnd == INT32_MAX )
		nEnd = Length();
	else if ( nEnd < 0 )
		nEnd = Length() - (-nEnd % Length());
	else if ( nEnd >= Length() )
		nEnd = Length();
	
	if ( nStart >= nEnd )
		return CUtlString( "" );

	const char *pIn = String();

	CUtlString ret;
	ret.m_Storage.SetLength( nEnd - nStart + 1 );
	char *pOut = (char*)ret.m_Storage.Get();

	memcpy( ret.m_Storage.Get(), &pIn[nStart], nEnd - nStart );
	pOut[nEnd - nStart] = 0;

	return ret;
}

// Grab a substring starting from the left or the right side.
CUtlString CUtlString::Left( int32_t nChars )
{
	return Slice( 0, nChars );
}

CUtlString CUtlString::Right( int32_t nChars )
{
	return Slice( -nChars );
}

// Get a string with the specified substring removed

CUtlString CUtlString::Remove( char const *pTextToRemove, bool bCaseSensitive ) const
{
	int nTextToRemoveLength = pTextToRemove ? V_strlen( pTextToRemove ) : 0;
	CUtlString outputString;
	const char *pSrc = Get();
	if ( pSrc )
	{
		while ( *pSrc )
		{
			char const *pNextOccurrence = bCaseSensitive ? V_strstr( pSrc, pTextToRemove ) : V_stristr( pSrc, pTextToRemove );
			if ( !pNextOccurrence )
			{
				// append remaining string
				outputString += pSrc;
				break;
			}

			int nNumCharsToCopy = pNextOccurrence - pSrc;
			if ( nNumCharsToCopy )
			{
				// append up to the undesired substring
				CUtlString temp = pSrc;
				temp = temp.Left( nNumCharsToCopy );
				outputString += temp;
			}

			// skip past undesired substring
			pSrc = pNextOccurrence + nTextToRemoveLength;
		}
	}

	return outputString;
}

CUtlString CUtlString::Replace( char const *pchFrom, const char *pchTo, bool bCaseSensitive /*= false*/ ) const
{
	if ( !pchTo )
	{
		return Remove( pchFrom, bCaseSensitive );
	}

	int nTextToReplaceLength = pchFrom ? V_strlen( pchFrom ) : 0;
	CUtlString outputString;
	const char *pSrc = Get();
	if ( pSrc )
	{
		while ( *pSrc )
		{
			char const *pNextOccurrence = bCaseSensitive ? V_strstr( pSrc, pchFrom ) : V_stristr( pSrc, pchFrom );
			if ( !pNextOccurrence )
			{
				// append remaining string
				outputString += pSrc;
				break;
			}

			int nNumCharsToCopy = pNextOccurrence - pSrc;
			if ( nNumCharsToCopy )
			{
				// append up to the undesired substring
				CUtlString temp = pSrc;
				temp = temp.Left( nNumCharsToCopy );
				outputString += temp;
			}

			// Append the replacement
			outputString += pchTo;

			// skip past undesired substring
			pSrc = pNextOccurrence + nTextToReplaceLength;
		}
	}

	return outputString;

}



CUtlString CUtlString::Replace( char cFrom, char cTo )
{
	CUtlString ret = *this;
	int len = ret.Length();
	for ( int i=0; i < len; i++ )
	{
		if ( ret.m_Storage[i] == cFrom )
			ret.m_Storage[i] = cTo;
	}

	return ret;
}

void CUtlString::RemoveDotSlashes(char separator)
{
	V_RemoveDotSlashes(Get(), separator);
}

void CUtlString::Swap( CUtlString &src )
{
	CUtlString tmp = src;
	src = *this;
	*this = tmp;
}


CUtlString CUtlString::AbsPath( const char *pStartingDir ) const
{
	char szNew[MAX_PATH]{0};
	V_MakeAbsolutePath( szNew, sizeof( szNew ), this->String(), pStartingDir );
	return CUtlString( szNew );
}

CUtlString CUtlString::UnqualifiedFilename() const
{
	const char *pFilename = V_UnqualifiedFileName( this->String() );
	return CUtlString( pFilename );
}

CUtlString CUtlString::DirName()
{
	CUtlString ret( this->String() );
	V_StripLastDir( (char*)ret.m_Storage.Get(), ret.m_Storage.Length() );
	V_StripTrailingSlash( (char*)ret.m_Storage.Get() );
	return ret;
}

CUtlString CUtlString::StripExtension() const
{
	char szTemp[MAX_FILEPATH];
	V_StripExtension( String(), szTemp, sizeof( szTemp ) );
	return CUtlString( szTemp );
}

CUtlString CUtlString::StripFilename() const
{
	const char *pFilename = V_UnqualifiedFileName( Get() ); // NOTE: returns 'Get()' on failure, never NULL
	int nCharsToCopy = pFilename - Get();
	CUtlString result;
	result.SetDirect( Get(), nCharsToCopy );
	result.StripTrailingSlash();
	return result;
}

CUtlString CUtlString::GetBaseFilename() const
{
	char szTemp[MAX_FILEPATH];
	V_FileBase( String(), szTemp, sizeof( szTemp ) );
	return CUtlString( szTemp );
}

CUtlString CUtlString::GetExtension() const
{
	char szTemp[MAX_FILEPATH];
	V_ExtractFileExtension( String(), szTemp, sizeof( szTemp ) );
	return CUtlString( szTemp );
}


CUtlString CUtlString::PathJoin( const char *pStr1, const char *pStr2 )
{
	char szPath[MAX_PATH]{0};
	V_ComposeFileName( pStr1, pStr2, szPath, sizeof( szPath ) );
	return CUtlString( szPath );
}


//-----------------------------------------------------------------------------
// Purpose: concatenate the provided string to our current content
//-----------------------------------------------------------------------------
void CUtlString::Append( const char *pchAddition )
{
	(*this) += pchAddition;
}

void CUtlString::Append(const char *pchAddition, int nMaxChars)
{
	const int nLen = V_strlen(pchAddition);
	if (nMaxChars < 0 || nLen <= nMaxChars)
	{
		Append(pchAddition);
	}
	else
	{
		char* pchAdditionDup = V_strdup(pchAddition);
		pchAdditionDup[nMaxChars] = 0;
		Append(pchAdditionDup);
		delete[] pchAdditionDup;
	}
}

//--------------------------------------------------------------------------------------------------
// Trim
//--------------------------------------------------------------------------------------------------

void CUtlString::TrimLeft( const char *szTargets )
{
	int i;

	if ( IsEmpty() )
	{
		return;
	}

	char* pSrc = Get();

	for ( i = 0; pSrc[ i ] != 0; i++ )
	{
		bool bWhitespace = false;

		for ( int j = 0; szTargets[ j ] != 0; j++ )
		{
			if ( pSrc[ i ] == szTargets[ j ] )
			{
				bWhitespace = true;
				break;
			}
		}

		if ( !bWhitespace )
		{
			break;
		}
	}

	// We have some whitespace to remove
	if ( i > 0 )
	{
		memmove( pSrc, &pSrc[ i ], Length() - i );
		SetLength( Length() - i );
		m_Storage[ Length() ] = '\0';
	}
}

void CUtlString::TrimRight( const char *szTargets )
{
	const int nLastCharIndex = Length() - 1;
	int i;

	char* pSrc = Get();

	for ( i = nLastCharIndex; i >= 0; i-- )
	{
		bool bWhitespace = false;

		for ( int j = 0; szTargets[ j ] != 0; j++ )
		{
			if ( pSrc[ i ] == szTargets[ j ] )
			{
				bWhitespace = true;
				break;
			}
		}

		if ( !bWhitespace )
		{
			break;
		}
	}

	// We have some whitespace to remove
	if ( i < nLastCharIndex )
	{
		pSrc[ i + 1 ] = 0;
		SetLength( i + 1 );
	}
}

void CUtlString::Trim( const char *szTargets )
{
	TrimLeft( szTargets );
	TrimRight( szTargets );
}
//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines a symbol table
//
// $Header: $
// $NoKeywords: $
//=============================================================================//

#pragma warning (disable:4514)

#include "utlsymbol.h"


#include <cctype>

//-----------------------------------------------------------------------------
// globals
//-----------------------------------------------------------------------------

CUtlSymbolTable* CUtlSymbol::s_pSymbolTable = nullptr;
bool CUtlSymbol::s_bAllowStaticSymbolTable = true;


//-----------------------------------------------------------------------------
// symbol methods
//-----------------------------------------------------------------------------

void CUtlSymbol::Initialize()
{
	// If this assert fails, then the module that this call is in has chosen to disallow
	// use of the static symbol table. Usually, it's to prevent confusion because it's easy
	// to accidentally use the global symbol table when you really want to use a specific one.
	Assert( s_bAllowStaticSymbolTable );

	// necessary to allow us to create global symbols
	static bool symbolsInitialized = false;
	if (!symbolsInitialized)
	{
		s_pSymbolTable = new CUtlSymbolTable;
		symbolsInitialized = true;
	}
}
//-----------------------------------------------------------------------------
// Purpose: Singleton to delete table on exit from module
//-----------------------------------------------------------------------------
class CCleanupUtlSymbolTable
{
public:
	~CCleanupUtlSymbolTable()
	{
		delete CUtlSymbol::s_pSymbolTable;
		CUtlSymbol::s_pSymbolTable = nullptr;
	}
};

static CCleanupUtlSymbolTable g_CleanupSymbolTable;

CUtlSymbolTable* CUtlSymbol::CurrTable()
{
	Initialize();
	return s_pSymbolTable; 
}


//-----------------------------------------------------------------------------
// string->symbol->string
//-----------------------------------------------------------------------------

CUtlSymbol::CUtlSymbol( const char* pStr )
{
	m_Id = CurrTable()->AddString( pStr );
}

const char* CUtlSymbol::String( ) const
{
	return CurrTable()->String(m_Id);
}


//-----------------------------------------------------------------------------
// checks if the symbol matches a string
//-----------------------------------------------------------------------------

bool CUtlSymbol::operator==( const char* pStr ) const
{
	if (m_Id == UTL_INVAL_SYMBOL) 
		return false;
	return strcmp( String(), pStr ) == 0;
}



//-----------------------------------------------------------------------------
// symbol table stuff
//-----------------------------------------------------------------------------
bool CUtlSymbolTable::Equal::operator()( std::string_view a, std::string_view b ) const
{
	if ( !m_insensitive )
		return a == b;

	if ( a.size() != b.size() )
		return false;

	for ( size_t i = 0; i < a.size(); i++ )
	{
		if ( std::toupper( (unsigned char)a[i] ) != std::toupper( (unsigned char)b[i] ) )
			return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CUtlSymbolTable::CUtlSymbolTable( int /*growSize*/, int initSize, bool caseInsensitive ) :
	m_lookup( initSize > 0 ? initSize : 0, Hash{ caseInsensitive }, Equal{ caseInsensitive } )
{
}

CUtlSymbolTable::~CUtlSymbolTable() = default;


CUtlSymbol CUtlSymbolTable::Find( const char* pString ) const
{
	if (!pString)
		return CUtlSymbol();

	auto it = m_lookup.find( std::string_view( pString ) );
	return it == m_lookup.end() ? CUtlSymbol() : CUtlSymbol( it->second );
}


//-----------------------------------------------------------------------------
// Finds and/or creates a symbol based on the string
//-----------------------------------------------------------------------------

CUtlSymbol CUtlSymbolTable::AddString( const char* pString )
{
	if (!pString)
		return CUtlSymbol( UTL_INVAL_SYMBOL );

	return AddStringView( pString );
}

CUtlSymbol CUtlSymbolTable::AddString(std::string_view string) {
	if (string.empty())
		return CUtlSymbol( UTL_INVAL_SYMBOL );

	return AddStringView( string );
}

CUtlSymbol CUtlSymbolTable::AddStringView( std::string_view string )
{
	auto it = m_lookup.find( string );
	if ( it != m_lookup.end() )
		return CUtlSymbol( it->second );

	if ( m_strings.size() == UTL_INVAL_SYMBOL )
		return CUtlSymbol();
	const auto id = static_cast<UtlSymId_t>( m_strings.size() );
	const std::string& stored = m_strings.emplace_back( string );
	m_lookup.emplace( std::string_view( stored ), id );
	return CUtlSymbol( id );
}


//-----------------------------------------------------------------------------
// Look up the string associated with a particular symbol
//-----------------------------------------------------------------------------

const char* CUtlSymbolTable::String( CUtlSymbol id ) const
{
	if (!id.IsValid()) 
		return "";
	
	Assert( (size_t)(UtlSymId_t)id < m_strings.size() );
	return m_strings[id].c_str();
}


//-----------------------------------------------------------------------------
// Remove all symbols in the table.
//-----------------------------------------------------------------------------

void CUtlSymbolTable::RemoveAll()
{
	m_lookup.clear();
	m_strings.clear();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pFileName - 
// Output : FileNameHandle_t
//-----------------------------------------------------------------------------
FileNameHandle_t CUtlFilenameSymbolTable::FindOrAddFileName( const char *pFileName )
{
	if ( !pFileName )
	{
		return nullptr;
	}

	// find first
	FileNameHandle_t hFileName = FindFileName( pFileName );
	if ( hFileName )
	{
		return hFileName;
	}

	// Fix slashes+dotslashes and make lower case first..
	char fn[ MAX_PATH ];
	Q_strncpy( fn, pFileName, sizeof( fn ) );
	Q_RemoveDotSlashes( fn );
#ifdef _WIN32
	strlwr( fn );
#endif

	// Split the filename into constituent parts
	char basepath[ MAX_PATH ];
	Q_ExtractFilePath( fn, basepath, sizeof( basepath ) );
	char filename[ MAX_PATH ];
	Q_strncpy( filename, fn + Q_strlen( basepath ), sizeof( filename ) );

	// not found, lock and look again
	FileNameHandleInternal_t handle;

	handle.SetPath( m_PathStringPool.FindStringHandle( basepath ) );
	handle.SetFile( m_FileStringPool.FindStringHandle( filename ) );
	if ( handle.GetPath() && handle.GetFile() )
	{
		// found

		return *( FileNameHandle_t * )( &handle );
	}

	// safely add it
	handle.SetPath( m_PathStringPool.ReferenceStringHandle( basepath ) );
	handle.SetFile( m_FileStringPool.ReferenceStringHandle( filename ) );


	return *( FileNameHandle_t * )( &handle );
}

FileNameHandle_t CUtlFilenameSymbolTable::FindFileName( const char *pFileName )
{
	if ( !pFileName )
	{
		return nullptr;
	}

	// Fix slashes+dotslashes and make lower case first..
	char fn[ MAX_PATH ];
	Q_strncpy( fn, pFileName, sizeof( fn ) );
	Q_RemoveDotSlashes( fn );
#ifdef _WIN32
	strlwr( fn );
#endif

	// Split the filename into constituent parts
	char basepath[ MAX_PATH ];
	Q_ExtractFilePath( fn, basepath, sizeof( basepath ) );
	char filename[ MAX_PATH ];
	Q_strncpy( filename, fn + Q_strlen( basepath ), sizeof( filename ) );

	FileNameHandleInternal_t handle;


	handle.SetPath( m_PathStringPool.FindStringHandle( basepath ) );
	handle.SetFile( m_FileStringPool.FindStringHandle( filename ) );



	if ( ( handle.GetPath() == 0 )  || ( handle.GetFile() == 0 ) )
		return nullptr;

	return *( FileNameHandle_t * )( &handle );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : handle - 
// Output : const char
//-----------------------------------------------------------------------------
bool CUtlFilenameSymbolTable::String( const FileNameHandle_t& handle, char *buf, int buflen )
{
	buf[ 0 ] = 0;

	FileNameHandleInternal_t *internalFileHandle = ( FileNameHandleInternal_t * )&handle;
	if ( !internalFileHandle )
	{
		return false;
	}


	const char *path = m_PathStringPool.HandleToString( internalFileHandle->GetPath() );
	const char *fn = m_FileStringPool.HandleToString( internalFileHandle->GetFile() );


	if ( !path || !fn )
	{
		return false;
	}

	Q_strncpy( buf, path, buflen );
	Q_strncat( buf, fn, buflen, COPY_ALL_CHARACTERS );

	return true;
}

void CUtlFilenameSymbolTable::RemoveAll()
{
	m_PathStringPool.FreeAll();
	m_FileStringPool.FreeAll();
}


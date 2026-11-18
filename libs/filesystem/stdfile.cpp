//===========================================================================//
//
// Purpose: Lowest level of the filesystem on std::filebuf / std::filesystem.
//
//===========================================================================//

#include "filesystem/stdfile.h"
#include "filesystem/file_overlay.h"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "tier1_v2/function_timing.hpp"

namespace fs = std::filesystem;

//-----------------------------------------------------------------------------
// Host-provided files (file_overlay.h)
//-----------------------------------------------------------------------------
static std::atomic<IFileOverlay *> s_pOverlay{nullptr};

std::string FS_NormalizeOverlayPath( std::string_view path )
{
	std::error_code ec;
	fs::path normalized = vstd::paths::to_fs( path );
	const fs::path absolute = fs::absolute( normalized, ec );
	if ( !ec )
	{
		normalized = absolute;
	}
	std::string text = vstd::paths::to_utf8( normalized.lexically_normal() );
	std::replace( text.begin(), text.end(), '\\', '/' );
#ifdef _WIN32
	vstd::make_lower_ascii( text );
#endif
	return text;
}

ScopedFileOverlay::ScopedFileOverlay( IFileOverlay &overlay ) : m_pPrevious( s_pOverlay.exchange( &overlay ) )
{
}

ScopedFileOverlay::~ScopedFileOverlay()
{
	s_pOverlay = m_pPrevious;
}

// The overlay's file at pPath, or null (no overlay installed, or it doesn't provide the file)
static FileOverlayData FindOverlayFile( const char *pPath )
{
	IFileOverlay *pOverlay = s_pOverlay.load();
	if ( !pOverlay )
	{
		return nullptr;
	}
	return pOverlay->Find( FS_NormalizeOverlayPath( pPath ) );
}

// A read-only stream over the bytes of an overlay file (shared with the overlay, so they outlive the stream)
class COverlayBuf : public std::streambuf
{
public:
	explicit COverlayBuf( FileOverlayData data ) : m_Data( std::move( data ) )
	{
		char *pBegin = const_cast<char *>( reinterpret_cast<const char *>( m_Data->data() ) );
		setg( pBegin, pBegin, pBegin + m_Data->size() );
	}

protected:
	pos_type seekoff( off_type off, std::ios::seekdir dir, std::ios::openmode ) override
	{
		const off_type size = egptr() - eback();
		const off_type base = dir == std::ios::cur ? gptr() - eback() : dir == std::ios::end ? size : 0;
		const off_type pos = base + off;
		if ( pos < 0 || pos > size )
		{
			return pos_type( off_type( -1 ) );
		}
		setg( eback(), eback() + pos, egptr() );
		return pos_type( pos );
	}

	pos_type seekpos( pos_type pos, std::ios::openmode which ) override
	{
		return seekoff( off_type( pos ), std::ios::beg, which );
	}

private:
	FileOverlayData m_Data;
};

//-----------------------------------------------------------------------------
// CStdFile
//-----------------------------------------------------------------------------
CStdFile *CStdFile::Open( const char *pFileName, const char *pOptions, int64_t *pSize )
{
	const bool bPlus = strchr( pOptions, '+' ) != nullptr;
	std::ios::openmode mode;
	switch ( pOptions[0] )
	{
	case 'r': mode = std::ios::in; if ( bPlus ) mode |= std::ios::out; break;
	case 'w': mode = std::ios::out | std::ios::trunc; if ( bPlus ) mode |= std::ios::in; break;
	case 'a': mode = std::ios::out | std::ios::app; if ( bPlus ) mode |= std::ios::in; break;
	default: return nullptr;
	}
	if ( strchr( pOptions, 'b' ) )
	{
		mode |= std::ios::binary;
	}

	const bool bReadOnly = pOptions[0] == 'r' && !bPlus;
	if ( bReadOnly )
	{
		if ( FileOverlayData data = FindOverlayFile( pFileName ) )
		{
			CStdFile *pOverlayFile = new CStdFile;
			pOverlayFile->m_pMemory = std::make_unique<COverlayBuf>( data );
			pOverlayFile->m_pBuf = pOverlayFile->m_pMemory.get();
			if ( pSize )
			{
				*pSize = (int64_t)data->size();
			}
			return pOverlayFile;
		}
	}

	fs::path path = vstd::paths::to_fs(pFileName);
	CStdFile *pFile = new CStdFile;
	if ( !pFile->m_File.open( path, mode ) )
	{
		// Content paths are often written with the wrong case; only look them up for reading.
		std::string resolved;
		if ( !bReadOnly || !FS_ResolvePathCaseInsensitive( pFileName, resolved ) || !pFile->m_File.open( vstd::paths::to_fs(resolved), mode ) )
		{
			delete pFile;
			return nullptr;
		}
		path = vstd::paths::to_fs(resolved);
	}

	if ( pSize )
	{
		std::error_code ec;
		const std::uintmax_t nSize = fs::file_size( path, ec );
		*pSize = ec ? 0 : (int64_t)nSize;
	}
	return pFile;
}

void CStdFile::Seek( int64_t nPos, int nSeekType )
{
	const std::ios::seekdir dir = ( nSeekType == SEEK_CUR ) ? std::ios::cur : ( nSeekType == SEEK_END ) ? std::ios::end : std::ios::beg;
	m_pBuf->pubseekoff( nPos, dir, std::ios::in | std::ios::out );
	m_bEof = false;
}

int64_t CStdFile::Tell()
{
	return (int64_t)m_pBuf->pubseekoff( 0, std::ios::cur, std::ios::in | std::ios::out );
}

size_t CStdFile::Read( void *pDest, size_t nSize )
{
	const std::streamsize nRead = m_pBuf->sgetn( static_cast<char *>( pDest ), (std::streamsize)nSize );
	if ( (size_t)nRead < nSize )
	{
		m_bEof = true;
	}
	return (size_t)nRead;
}

size_t CStdFile::Write( const void *pSrc, size_t nSize )
{
	const std::streamsize nWritten = m_pBuf->sputn( static_cast<const char *>( pSrc ), (std::streamsize)nSize );
	if ( (size_t)nWritten < nSize )
	{
		m_bError = true;
	}
	return (size_t)nWritten;
}

int CStdFile::VPrintf( const char *pFormat, va_list args )
{
	va_list sizeArgs;
	va_copy( sizeArgs, args );
	const int nLength = vsnprintf( nullptr, 0, pFormat, sizeArgs );
	va_end( sizeArgs );
	if ( nLength < 0 )
	{
		m_bError = true;
		return nLength;
	}

	std::vector<char> text( (size_t)nLength + 1 );
	vsnprintf( text.data(), text.size(), pFormat, args );
	return (int)Write( text.data(), (size_t)nLength );
}

int CStdFile::Printf( const char *pFormat, ... )
{
	va_list args;
	va_start( args, pFormat );
	const int nLength = VPrintf( pFormat, args );
	va_end( args );
	return nLength;
}

bool CStdFile::Flush()
{
	return m_pBuf->pubsync() == 0;
}

char *CStdFile::Gets( char *pDest, int nDestSize )
{
	if ( nDestSize <= 0 )
	{
		return nullptr;
	}

	int n = 0;
	while ( n < nDestSize - 1 )
	{
		const std::filebuf::int_type c = m_pBuf->sbumpc();
		if ( std::filebuf::traits_type::eq_int_type( c, std::filebuf::traits_type::eof() ) )
		{
			m_bEof = true;
			break;
		}
		pDest[n++] = std::filebuf::traits_type::to_char_type( c );
		if ( pDest[n - 1] == '\n' )
		{
			break;
		}
	}

	if ( n == 0 )
	{
		return nullptr;
	}
	pDest[n] = '\0';
	return pDest;
}

//-----------------------------------------------------------------------------
// Paths
//-----------------------------------------------------------------------------
bool FS_ResolvePathCaseInsensitive( const char *pPath, std::string &resolved )
{
	std::error_code ec;
	const fs::path path = vstd::paths::to_fs(pPath);
	if ( fs::exists( path, ec ) )
	{
		resolved = vstd::paths::to_utf8(path);
		return true;
	}

	fs::path current = path.root_path();
	for ( const fs::path &part : path.relative_path() )
	{
		fs::path candidate = current / part;
		if ( !fs::exists( candidate, ec ) )
		{
			const std::string name = vstd::paths::to_utf8(part);
			bool bFound = false;
			const fs::path dir = current.empty() ? fs::path( "." ) : current;
			for ( fs::directory_iterator it( dir, ec ), end; !ec && it != end; it.increment( ec ) )
			{
				if ( vstd::iequals( vstd::paths::to_utf8( it->path().filename() ), name ) )
				{
					candidate = current / it->path().filename();
					bFound = true;
					break;
				}
			}
			if ( !bFound )
			{
				return false;
			}
		}
		current = candidate;
	}

	resolved = vstd::paths::to_utf8(current);
	return true;
}

// pPath as given, or its case-insensitive match if pPath doesn't exist.
static bool ExistingPath( const char *pPath, fs::path &path )
{
	std::error_code ec;
	path = vstd::paths::to_fs(pPath);
	if ( fs::exists( path, ec ) )
	{
		return true;
	}
	std::string resolved;
	if ( !FS_ResolvePathCaseInsensitive( pPath, resolved ) )
	{
		return false;
	}
	path = vstd::paths::to_fs(resolved);
	return true;
}

bool FS_Stat( const char *pPath, FileStat_t &stat )
{
	if ( FileOverlayData data = FindOverlayFile( pPath ) )
	{
		stat.nSize = (int64_t)data->size();
		stat.nModifyTime = 0;
		stat.bDirectory = false;
		stat.bWritable = false;
		return true;
	}

	std::error_code ec;
	fs::path path;
	if ( !ExistingPath( pPath, path ) )
	{
		return false;
	}
	const fs::file_status status = fs::status( path, ec );
	if ( ec )
	{
		return false;
	}

	stat.bDirectory = fs::is_directory( status );
	stat.bWritable = ( status.permissions() & fs::perms::owner_write ) != fs::perms::none;

	const std::uintmax_t nSize = stat.bDirectory ? 0 : fs::file_size( path, ec );
	stat.nSize = ec ? 0 : (int64_t)nSize;

	const fs::file_time_type modifyTime = fs::last_write_time( path, ec );
	stat.nModifyTime = ec ? 0 : std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::clock_cast<std::chrono::system_clock>( modifyTime ).time_since_epoch() ).count();
	return true;
}

bool FS_SetWritable( const char *pPath, bool bWritable )
{
	fs::path path;
	if ( !ExistingPath( pPath, path ) )
	{
		return false;
	}
	std::error_code ec;
	fs::permissions( path, fs::perms::owner_write, bWritable ? fs::perm_options::add : fs::perm_options::remove, ec );
	return !ec;
}

bool FS_CreateDirectories( const char *pPath )
{
	std::error_code ec;
	const fs::path path = vstd::paths::to_fs(pPath);
	fs::create_directories( path, ec );
	return fs::is_directory( path, ec );
}

bool FS_RemoveFile( const char *pPath )
{
	std::error_code ec;
	return fs::remove( vstd::paths::to_fs(pPath), ec ) && !ec;
}

bool FS_RenameFile( const char *pOldPath, const char *pNewPath )
{
	std::error_code ec;
	fs::rename( vstd::paths::to_fs(pOldPath), vstd::paths::to_fs(pNewPath), ec );
	return !ec;
}

bool FS_GetCurrentDirectory( char *pDest, size_t nDestSize )
{
	std::error_code ec;
	const std::string dir = vstd::paths::to_utf8(fs::current_path( ec ));
	if ( ec || dir.size() >= nDestSize )
	{
		return false;
	}
	memcpy( pDest, dir.c_str(), dir.size() + 1 );
	return true;
}

//-----------------------------------------------------------------------------
// Directory search
//-----------------------------------------------------------------------------
class CStdFileFind
{
public:
	std::vector<StdFindData_t> m_Entries;
	size_t m_nNext = 0;
};

// '*' matches any run of characters, '?' any single character; case-insensitive.
static bool WildcardMatchNoCase( const char *pPattern, const char *pName )
{
	const char *pStar = nullptr;
	const char *pResume = nullptr;
	while ( *pName )
	{
		if ( *pPattern == '*' )
		{
			pStar = pPattern++;
			pResume = pName;
		}
		else if ( *pPattern == '?' || ( *pPattern && std::tolower( (unsigned char)*pPattern ) == std::tolower( (unsigned char)*pName ) ) )
		{
			++pPattern;
			++pName;
		}
		else if ( pStar )
		{
			pPattern = pStar + 1;
			pName = ++pResume;
		}
		else
		{
			return false;
		}
	}
	while ( *pPattern == '*' )
	{
		++pPattern;
	}
	return *pPattern == '\0';
}

CStdFileFind *FS_FindFirst( const char *pWildCard, StdFindData_t &data )
{
	std::string wildCard( pWildCard );
	std::replace( wildCard.begin(), wildCard.end(), '\\', '/' );

	const size_t nSlash = wildCard.rfind( '/' );
	const std::string dir = ( nSlash == std::string::npos ) ? "." : wildCard.substr( 0, nSlash ? nSlash : 1 );
	const std::string pattern = ( nSlash == std::string::npos ) ? wildCard : wildCard.substr( nSlash + 1 );
	const bool bMatchAll = pattern == "*" || pattern == "*.*";

	std::error_code ec;
	fs::path dirPath = vstd::paths::to_fs(dir);
	if ( !fs::is_directory( dirPath, ec ) )
	{
		std::string resolved;
		if ( !FS_ResolvePathCaseInsensitive( dir.c_str(), resolved ) )
		{
			return nullptr;
		}
		dirPath = vstd::paths::to_fs(resolved);
	}

	CStdFileFind *pFind = new CStdFileFind;
	for ( fs::directory_iterator it( dirPath, ec ), end; !ec && it != end; it.increment( ec ) )
	{
		const std::string name = vstd::paths::to_utf8(it->path().filename());
		if ( name.size() >= sizeof( data.cFileName ) )
		{
			continue;
		}
		if ( !bMatchAll && !WildcardMatchNoCase( pattern.c_str(), name.c_str() ) )
		{
			continue;
		}

		StdFindData_t entry;
		memcpy( entry.cFileName, name.c_str(), name.size() + 1 );
		std::error_code typeError;
		entry.bDirectory = it->is_directory( typeError );
		pFind->m_Entries.push_back( entry );
	}

	if ( pFind->m_Entries.empty() )
	{
		delete pFind;
		return nullptr;
	}

	std::sort( pFind->m_Entries.begin(), pFind->m_Entries.end(),
		[]( const StdFindData_t &a, const StdFindData_t &b ) { return strcmp( a.cFileName, b.cFileName ) < 0; } );
	FS_FindNext( pFind, data );
	return pFind;
}

bool FS_FindNext( CStdFileFind *hFind, StdFindData_t &data )
{
	if ( !hFind || hFind->m_nNext >= hFind->m_Entries.size() )
	{
		return false;
	}
	data = hFind->m_Entries[hFind->m_nNext++];
	return true;
}

void FS_FindClose( CStdFileFind *hFind )
{
	delete hFind;
}

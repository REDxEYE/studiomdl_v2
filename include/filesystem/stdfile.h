//===========================================================================//
//
// Purpose: Lowest level of the filesystem: open files, stat, permissions,
//          directory search and directory/file management, implemented only
//          with std::filebuf and std::filesystem (no platform branches).
//          All char paths are UTF-8 (see tier1_v2/path_utils.hpp).
//
//===========================================================================//

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------
// An open file. Seek origins are SEEK_SET / SEEK_CUR / SEEK_END.
//-----------------------------------------------------------------------------
class CStdFile
{
public:
	// pOptions is an fopen-style mode ("rb", "w", "a+", "r+b", ...); 't' is accepted and ignored.
	// Read-only opens fall back to a case-insensitive path lookup.
	// Returns NULL if the file can't be opened; *pSize receives the file size when given.
	static CStdFile *Open( const char *pFileName, const char *pOptions, int64_t *pSize );

	void Seek( int64_t nPos, int nSeekType );
	int64_t Tell();
	bool IsEof() const { return m_bEof; }
	bool HasError() const { return m_bError; }

	// Returns the number of bytes read / written.
	size_t Read( void *pDest, size_t nSize );
	size_t Write( const void *pSrc, size_t nSize );
	int VPrintf( const char *pFormat, va_list args );
	int Printf( const char *pFormat, ... );
	bool Flush();

	// fgets semantics: reads up to nDestSize - 1 chars, keeps the '\n', NULL at end of file.
	char *Gets( char *pDest, int nDestSize );

private:
	CStdFile() = default;

	std::filebuf m_Buf;
	bool m_bEof = false;
	bool m_bError = false;
};

//-----------------------------------------------------------------------------
// Paths
//-----------------------------------------------------------------------------

// Finds pPath with every component matched case-insensitively (exact match preferred).
// Returns false if no such file or directory exists.
bool FS_ResolvePathCaseInsensitive( const char *pPath, std::string &resolved );

struct FileStat_t
{
	int64_t nSize;			// bytes (0 for directories)
	int64_t nModifyTime;	// seconds since the Unix epoch
	bool bDirectory;
	bool bWritable;
};

// Both fall back to a case-insensitive match of pPath; FS_Stat returns false if it doesn't exist.
bool FS_Stat( const char *pPath, FileStat_t &stat );
bool FS_SetWritable( const char *pPath, bool bWritable );

bool FS_CreateDirectories( const char *pPath );	// with all parents; true if it exists afterwards
bool FS_RemoveFile( const char *pPath );
bool FS_RenameFile( const char *pOldPath, const char *pNewPath );
bool FS_GetCurrentDirectory( char *pDest, size_t nDestSize );

//-----------------------------------------------------------------------------
// Directory search: pWildCard is "dir/pattern", pattern supports '*' and '?',
// matched case-insensitively ("*.*" matches everything). "." and ".." are not
// returned; results are sorted by name.
//-----------------------------------------------------------------------------
struct StdFindData_t
{
	char cFileName[260];
	bool bDirectory;
};

class CStdFileFind;

// Returns nullptr when nothing matches.
CStdFileFind *FS_FindFirst( const char *pWildCard, StdFindData_t &data );
bool FS_FindNext( CStdFileFind *hFind, StdFindData_t &data );
void FS_FindClose( CStdFileFind *hFind );


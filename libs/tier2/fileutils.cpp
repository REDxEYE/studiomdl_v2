//===== Copyright � 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Helper methods + classes for file access
//
//===========================================================================//

#include "tier2/fileutils.h"
#include "tier1/strtools.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"

// NOTE: This has to be the last file included!


//-----------------------------------------------------------------------------
// Builds a directory which is a subdirectory of the current mod
//-----------------------------------------------------------------------------
void GetModSubdirectory( const char *pSubDir, char *pBuf, int nBufLen )
{
	// Compute starting directory
	Assert( g_pFullFileSystem->GetSearchPath( IBaseFileSystem::PathID::Mod, false, NULL, 0 ) < nBufLen );
	g_pFullFileSystem->GetSearchPath( IBaseFileSystem::PathID::Mod, false, pBuf, nBufLen );
	char *pSemi = strchr( pBuf, ';' );
	if ( pSemi )
	{
		*pSemi = 0;
	}

	Q_StripTrailingSlash( pBuf );
	if ( pSubDir )
	{
		int nLen = Q_strlen( pSubDir );
		Q_strncat( pBuf, "\\", nBufLen, 1 );
		Q_strncat( pBuf, pSubDir, nBufLen, nLen );
	}

	Q_FixSlashes( pBuf );
}


//-----------------------------------------------------------------------------
// Builds a directory which is a subdirectory of the current mod's *content*
//-----------------------------------------------------------------------------
void GetModContentSubdirectory( const char *pSubDir, char *pBuf, int nBufLen )
{
	char pTemp[ MAX_PATH ]{};
	GetModSubdirectory( pSubDir, pTemp, sizeof(pTemp) );
	ComputeModContentFilename( pTemp, pBuf, nBufLen );
}


//-----------------------------------------------------------------------------
// Generates a filename under the 'game' subdirectory given a subdirectory of 'content'
//-----------------------------------------------------------------------------
void ComputeModFilename( const char *pContentFileName, char *pBuf, size_t nBufLen )
{
	char pRelativePath[ MAX_PATH ];
	if ( !g_pFullFileSystem->FullPathToRelativePathEx( pContentFileName, IBaseFileSystem::PathID::Content, pRelativePath, sizeof(pRelativePath) ) )
	{
		Q_strncpy( pBuf, pContentFileName, nBufLen );
		return;
	}

	char pGameRoot[ MAX_PATH ];
	g_pFullFileSystem->GetSearchPath( IBaseFileSystem::PathID::Game, false, pGameRoot, sizeof(pGameRoot) );
	char *pSemi = strchr( pGameRoot, ';' );
	if ( pSemi )
	{
		*pSemi = 0;
	}

	Q_ComposeFileName( pGameRoot, pRelativePath, pBuf, nBufLen );
}


//-----------------------------------------------------------------------------
// Generates a filename under the 'content' subdirectory given a subdirectory of 'game'
//-----------------------------------------------------------------------------
void ComputeModContentFilename( const char *pGameFileName, char *pBuf, size_t nBufLen )
{
	char pRelativePath[ MAX_PATH ];
	if ( !g_pFullFileSystem->FullPathToRelativePathEx( pGameFileName, IBaseFileSystem::PathID::Game, pRelativePath, sizeof(pRelativePath) ) )
	{
		Q_strncpy( pBuf, pGameFileName, nBufLen );
		return;
	}

	char pContentRoot[ MAX_PATH ];
	g_pFullFileSystem->GetSearchPath( IBaseFileSystem::PathID::Content, false, pContentRoot, sizeof(pContentRoot) );
	char *pSemi = strchr( pContentRoot, ';' );
	if ( pSemi )
	{
		*pSemi = 0;
	}

	Q_ComposeFileName( pContentRoot, pRelativePath, pBuf, nBufLen );
}



//-----------------------------------------------------------------------------
// Returns the search path as a list of paths
//-----------------------------------------------------------------------------
void GetSearchPath( CUtlVector< CUtlString > &path, const char *pPathID )
{
	int nMaxLen = g_pFullFileSystem->GetSearchPath( IBaseFileSystem::ToPathID( pPathID ), false, nullptr, 0 );
	char *pBuf = (char*)stackalloc( nMaxLen );
	g_pFullFileSystem->GetSearchPath( IBaseFileSystem::ToPathID( pPathID ), false, pBuf, nMaxLen );

	char *pSemi;
	while (nullptr != ( pSemi = strchr( pBuf, ';' ) ) )
	{
		*pSemi = 0;
		path.AddToTail( pBuf );
		pBuf = pSemi + 1;
	}
	path.AddToTail( pBuf );
}

//-----------------------------------------------------------------------------
// Given file name in the current dir generate a full path to it.
//-----------------------------------------------------------------------------
bool GenerateFullPath( const char *pFileName, char const *pPathID, char *pBuf, int nBufLen )
{
	if ( V_IsAbsolutePath( pFileName ) )
	{
		V_strncpy( pBuf, pFileName, nBufLen );
		return true;
	}

	const char *pFullPath = g_pFullFileSystem->RelativePathToFullPath( pFileName, IBaseFileSystem::ToPathID( pPathID ), pBuf, nBufLen );
	if ( pFullPath && Q_IsAbsolutePath( pFullPath ) )
		return true;

	char pDir[ MAX_PATH ];
	if ( !g_pFullFileSystem->GetCurrentDirectory( pDir, sizeof( pDir ) ) )
		return false;

	V_ComposeFileName( pDir, pFileName, pBuf, nBufLen );
	V_RemoveDotSlashes( pBuf );
	return true;
}


void CBaseFile::ReadFile( CUtlBuffer &fileData )
{
	int nFileSize = Size();
	fileData.EnsureCapacity( Size() );
	int nSize = Read( fileData.Base(), nFileSize );
	fileData.SeekPut( CUtlBuffer::SEEK_HEAD, nSize );
}



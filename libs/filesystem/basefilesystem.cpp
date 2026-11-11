//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "basefilesystem.h"
#include "tier0/threadtools.h"
#include "tier1/characterset.h"
#include "tier1/keyvalues.h"
#include "tier1/lzmaDecoder.h"
#include "tier1/fmtstr.h"
#include "tier1_v2/function_timing.hpp"
#include "tier1_v2/path_utils.hpp"

#ifndef DEDICATED
#include "keyvaluescompiler.h"
#endif

#define FS_DVDDEV_REMAP_ROOT ""
#define FS_DVDDEV_ROOT "dvddev???:::"
#define FS_EXCLUDE_PATHS_FILENAME "allbad_exclude_paths.txt"

#define IsDvdDevPathString( x ) false


#pragma warning( disable : 4355 )  // warning C4355: 'this' : used in base member initializer list


static void AddSeperatorAndFixPath( char *str );

// Case-insensitive symbol table for path IDs.
CUtlSymbolTable g_PathIDTable( 0, 32, true );

int g_iNextSearchPathID = 1;


void FixUpPathCaseForPS3(const char* pFilePath)
{
    char* prev_ptr = nullptr;
    char* last_ptr = nullptr;
	// This is really bad but the EA code does it so let's give it a try for now
	char* pFilePathNonConst = const_cast< char * >( pFilePath );
	char* ptr = pFilePathNonConst;

	//Convert all "\" to forward "/" and reformat relative paths
    while ( *ptr )
    {
        if ( *ptr == '\\' || *ptr == '/' )
        {
            *ptr='/';
            while(ptr[1]=='\\' || ptr[1] == '/') //Get rid of multiple slashes
            {
                strcpy(ptr+1,ptr+2);
            }
             if(strncmp(ptr+1,"..",2)==0 && ptr[3]!=0 && ptr[4]!=0 && last_ptr) //Some relative paths are used at runtime in Team Fortress
             {
                 //printf("Changing relative path %s to ... ", pFilePathNonConst);
                 strcpy(last_ptr+1, ptr+4); //Remove relative path
                 if(strncmp(last_ptr+1,"..",2)==0 && last_ptr[3]!=0 && last_ptr[4]!=0 && prev_ptr) //Sometimes get /../../ strings
                 {
                     strcpy(prev_ptr+1, last_ptr+4);
                     if(strncmp(prev_ptr+1,"..",2)==0)
                     {
                         printf("Error: Can't process PS3 filenames containing /../../../\n");
                         Assert(0);
                     }
                 }
                 //printf("%s\n", pFilePathNonConst);
             }
            prev_ptr = last_ptr;
            last_ptr = ptr;
        }
        ptr++;
    }

    // terrible, terrible cruft: savegames (*.HL?) are written with uppercase from a million
	// different places. For now, I'm just going to leave them alone here, rather than try
	// to find every single possible place that has a savegame go through it (as an alias
	// of a copy of an alias of a string that's impossible to track by grepping). Y-U-C-K.
	if ( V_strstr(pFilePath, ".HL") )
	{
		return;
	}


	//PS3 file system is case sensitive (though this isn't enforced for /app_home/)
    if(pFilePathNonConst[0]=='/')
    {
		// if we're in the USRDIR directory, don't mess with paths up to that point
		char *pAfterUsrDir = V_strstr(pFilePathNonConst, "USRDIR");
		if ( pAfterUsrDir )
		{
			strlwr( pAfterUsrDir + 6 );
		}
        else if ((strnicmp(pFilePathNonConst,"/app_home/",10)==0) || (strnicmp(pFilePathNonConst,"/dev_bdvd/",10)==0) || (strnicmp(pFilePathNonConst,"/host_root/",11)==0))
        {
            strlwr(pFilePathNonConst+10);
        }
	    else if (strnicmp(pFilePathNonConst,"/dev_hdd0/game/",15)==0)
	    {
		    strlwr(pFilePathNonConst+15);
	    }
	    else
	    {
		    //Lowercase everything after second "/"
		    ptr=strchr(pFilePathNonConst,'/');
		    if (ptr) ptr=strchr(ptr+1,'/');
		    if (ptr) strlwr(ptr);
	    }
    }
    else
    {
	    //Lowercase everything
	    strlwr(pFilePathNonConst);
    }

}

// Look for cases like materials\\blah.vmt.
bool V_CheckDoubleSlashes( const char *pStr )
{
	int len = V_strlen( pStr );

	for ( int i=1; i < len-1; i++ )
	{
		if ( (pStr[i] == '/' || pStr[i] == '\\') && (pStr[i+1] == '/' || pStr[i+1] == '\\') )
			return true;
	}
	return false;
}

//
// Format relative filename when used under a search path
// allows "symlinking" official workshop locations into
// official locations in shipping depots.
//
// Returns passed pFileName if no symlinking occurs,
// or pointer to temp symlink buffer containing the symlink target.
//
static char const * V_FormatFilenameForSymlinking( char (&tempSymlinkBuffer)[MAX_PATH], char const *pFileName )
{
	if ( !pFileName )
		return nullptr;

	if ( !V_strnicmp( pFileName, "maps", 4 ) &&
		 ( ( pFileName[4] == CORRECT_PATH_SEPARATOR ) || ( pFileName[4] == INCORRECT_PATH_SEPARATOR ) ) &&
		 !V_strnicmp( pFileName + 5, "workshop", 8 ) &&
		 ( ( pFileName[13] == CORRECT_PATH_SEPARATOR ) || ( pFileName[13] == INCORRECT_PATH_SEPARATOR ) ) )
	{
		//    maps/workshop/
		if ( false
			/** Removed for partner depot **/ && ( pFileName[23] == CORRECT_PATH_SEPARATOR || pFileName[23] == INCORRECT_PATH_SEPARATOR ) )
		{
			Q_snprintf( tempSymlinkBuffer, ARRAYSIZE( tempSymlinkBuffer ), "maps%c%s", pFileName[4], pFileName + 24 );
			return tempSymlinkBuffer;
		}
	}

	static bool bLoadBannedWords = ( !!CommandLine()->FindParm( "-usebanlist" ) ) || (!!CommandLine()->FindParm( "-perfectworld" ) );
	if ( bLoadBannedWords )
	{
		if ( !V_strnicmp( pFileName, "maps", 4 ) &&
			( ( pFileName[ 4 ] == CORRECT_PATH_SEPARATOR ) || ( pFileName[ 4 ] == INCORRECT_PATH_SEPARATOR ) ) &&
			!V_strnicmp( pFileName + 5, "ar_monastery", 12 ) )
		{
			//	maps/ar_monastery -> maps/ar_shoots
			Q_snprintf( tempSymlinkBuffer, ARRAYSIZE( tempSymlinkBuffer ), "maps%car_shoots%s", pFileName[ 4 ], pFileName + 17 );
			return tempSymlinkBuffer;
		}
	}

	return pFileName; // nothing symlinked here
}


// This can be used to easily fix a filename on the stack.
#define CHECK_DOUBLE_SLASHES( x ) Assert( V_CheckDoubleSlashes(x) == false );


// Win32 dedicated.dll contains both filesystem_steam.cpp and filesystem_stdio.cpp, so it has two
// CBaseFileSystem objects.  We'll let it manage BaseFileSystem() itself.
#if !( defined( _WIN32 ) && defined( DEDICATED ) )
static CBaseFileSystem *g_pBaseFileSystem;
CBaseFileSystem *BaseFileSystem()
{
	return g_pBaseFileSystem;
}
#endif


struct FileOpenDuplicateTime_t
{
	char m_szName[ MAX_PATH ];
	int m_nLoadCount;
	float m_flAccumulatedMicroSeconds;

	FileOpenDuplicateTime_t()
	{
		m_szName[ 0 ] = '\0';
		m_nLoadCount = 0;
		m_flAccumulatedMicroSeconds = 0.0f;
	}
};
CUtlVector< FileOpenDuplicateTime_t* > g_FileOpenDuplicateTimes;	// Used to debug approximate time spent reading files duplicate times

#if defined( TRACK_BLOCKING_IO )

// If we hit more than 100 items in a frame, we're probably doing a level load...
#define MAX_ITEMS	100

class CBlockingFileItemList : public IBlockingFileItemList
{
public:
	CBlockingFileItemList( CBaseFileSystem *fs )
		:
		m_pFS( fs ),
		m_bLocked( false )
	{
	}

	// You can't call any of the below calls without calling these methods!!!!
	virtual void	LockMutex()
	{
		Assert( !m_bLocked );
		if ( m_bLocked )
			return;
		m_bLocked = true;
		m_pFS->BlockingFileAccess_EnterCriticalSection();
	}

	virtual void	UnlockMutex()
	{
		Assert( m_bLocked );
		if ( !m_bLocked )
			return;

		m_pFS->BlockingFileAccess_LeaveCriticalSection();
		m_bLocked = false;
	}

	virtual int First() const
	{
		if ( !m_bLocked )
		{
			Error( "CBlockingFileItemList::First() w/o calling EnterCriticalSectionFirst!" );
		}
		return m_Items.Head();
	}

	virtual int Next( int i ) const
	{
		if ( !m_bLocked )
		{
			Error( "CBlockingFileItemList::Next() w/o calling EnterCriticalSectionFirst!" );
		}
		return m_Items.Next( i );
	}

	virtual int InvalidIndex() const
	{
		return m_Items.InvalidIndex();
	}

	virtual const FileBlockingItem& Get( int index ) const
	{
		if ( !m_bLocked )
		{
			Error( "CBlockingFileItemList::Get( %d ) w/o calling EnterCriticalSectionFirst!", index );
		}
		return m_Items[ index ];
	}

	virtual void Reset()
	{
		if ( !m_bLocked )
		{
			Error( "CBlockingFileItemList::Reset() w/o calling EnterCriticalSectionFirst!" );
		}
		m_Items.RemoveAll();
	}

	void Add( const FileBlockingItem& item )
	{
		// Ack, should use a linked list probably...
		while ( m_Items.Count() > MAX_ITEMS )
		{
			m_Items.Remove( m_Items.Head() );
		}
		m_Items.AddToTail( item );
	}


private:
	CUtlLinkedList< FileBlockingItem, unsigned short >	m_Items;
	CBaseFileSystem						*m_pFS;
	bool								m_bLocked;
};
#endif

CUtlSymbol	CBaseFileSystem::m_GamePathID;
CUtlSymbol	CBaseFileSystem::m_BSPPathID;
bool		CBaseFileSystem::m_bSearchPathsPatchedAfterInstall;

CUtlVector< FileNameHandle_t > CBaseFileSystem::m_ExcludeFilePaths;

CUtlBuffer	g_UpdateZipBuffer;
CUtlBuffer	g_XLSPPatchZipBuffer;



//-----------------------------------------------------------------------------
// IIOStats implementation
//-----------------------------------------------------------------------------

#ifndef _CERT
class CIoStats : public IIoStats
{
public:
	CIoStats();
	~CIoStats();

	virtual void OnFileSeek( int nTimeInMs );
	virtual void OnFileRead( int nTimeInMs, int nBytesRead );
	virtual void OnFileOpen( const char * pFileName );
	virtual int GetNumberOfFileSeeks();
	virtual int GetTimeInFileSeek();
	virtual int GetNumberOfFileReads();
	virtual int GetTimeInFileReads();
	virtual int GetFileReadTotalSize();
	virtual int GetNumberOfFileOpens();
	void Reset();

private:
	int m_nNumberOfFileSeeks;
	int m_nTimeInFileSeek;
	int m_nNumberOfFileReads;
	int m_nTimeInFileRead;
	int m_nFileReadTotalSize;
	int m_nNumberOfFileOpens;
};

static CIoStats s_IoStats;

CIoStats::CIoStats()
:
m_nNumberOfFileSeeks( 0 ),
m_nTimeInFileSeek( 0 ),
m_nNumberOfFileReads( 0 ),
m_nTimeInFileRead( 0 ),
m_nFileReadTotalSize( 0 ),
m_nNumberOfFileOpens( 0 )
{
	// Do nothing...
}

CIoStats::~CIoStats()
{
	// Do nothing...
}

void CIoStats::OnFileSeek( int nTimeInMs )
{
	++m_nNumberOfFileSeeks;
	m_nTimeInFileSeek += nTimeInMs;
}

void CIoStats::OnFileRead( int nTimeInMs, int nBytesRead )
{
	++m_nNumberOfFileReads;
	m_nTimeInFileRead += nTimeInMs;
	m_nFileReadTotalSize += nBytesRead;
}

void CIoStats::OnFileOpen( const char * pFileName )
{
	++m_nNumberOfFileOpens;
}

int CIoStats::GetNumberOfFileSeeks()
{
	return m_nNumberOfFileSeeks;
}

int CIoStats::GetTimeInFileSeek()
{
	return m_nTimeInFileSeek;
}

int CIoStats::GetNumberOfFileReads()
{
	return m_nNumberOfFileReads;
}

int CIoStats::GetTimeInFileReads()
{
	return m_nTimeInFileRead;
}

int CIoStats::GetFileReadTotalSize()
{
	return m_nFileReadTotalSize;
}

int CIoStats::GetNumberOfFileOpens()
{
	return m_nNumberOfFileOpens;
}

void CIoStats::Reset()
{
	m_nNumberOfFileSeeks = 0;
	m_nTimeInFileSeek = 0;
	m_nNumberOfFileReads = 0;
	m_nTimeInFileRead = 0;
	m_nFileReadTotalSize = 0;
	m_nNumberOfFileOpens = 0;
}
#endif

//-----------------------------------------------------------------------------
// constructor
//-----------------------------------------------------------------------------

CBaseFileSystem::CBaseFileSystem()
{
#if !( defined(_WIN32) && defined(DEDICATED) )
	g_pBaseFileSystem = this;
#endif
	g_pFullFileSystem = this;			// Left in for non tier Apps, tools, etc...

	// If this changes then FileNameHandleInternal_t/FileNameHandle_t needs to be fixed!!!
	Assert( sizeof( CUtlSymbol ) == sizeof( short ) );

	// Clear out statistics
	memset( &m_Stats, 0, sizeof(m_Stats) );

	m_fwLevel    = FILESYSTEM_WARNING_REPORTUNCLOSED;
	m_pfnWarning = nullptr;
	m_bOutputDebugString = false;
	m_DirtyDiskReportFunc = nullptr;

	m_pThreadPool = nullptr;
#if defined( TRACK_BLOCKING_IO )
	m_pBlockingItems = new CBlockingFileItemList( this );
	m_bBlockingFileAccessReportingEnabled = false;
	m_bAllowSynchronousLogging = true;
#endif

	m_iMapLoad = 0;


#ifdef SUPPORT_IODELAY_MONITORING
	m_pDelayThread = nullptr;
	m_flDelayLimit = 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseFileSystem::~CBaseFileSystem()
{
	m_PathIDInfos.PurgeAndDeleteElements();
#if defined( TRACK_BLOCKING_IO )
	delete m_pBlockingItems;
#endif

}


//-----------------------------------------------------------------------------
// Init/shutdown (not called, see basefilesystem.h)
//-----------------------------------------------------------------------------
bool CBaseFileSystem::Init()
{
//	m_FileTracker2.InitAsyncThread();


	// This is a special tag to allow iterating just the BSP file, it doesn't show up in the list per se, but gets converted to "GAME" in the filter function
	m_BSPPathID = g_PathIDTable.AddString( "BSP" );
	m_GamePathID = g_PathIDTable.AddString( "GAME" );

	if ( getenv( "fs_debug" ) )
	{
		m_bOutputDebugString = true;
	}

	return true;
}

void CBaseFileSystem::Shutdown()
{

	RemoveAllSearchPaths();

}

void CBaseFileSystem::BuildExcludeList()
{
    return;
}

//-----------------------------------------------------------------------------
// Computes a full write path
//-----------------------------------------------------------------------------
inline void CBaseFileSystem::ComputeFullWritePath( char* pDest, int maxlen, const char *pRelativePath, const char *pWritePathID )
{
	Q_strncpy( pDest, GetWritePath( pRelativePath, pWritePathID ), maxlen );
	Q_strncat( pDest, pRelativePath, maxlen, COPY_ALL_CHARACTERS );
	Q_FixSlashes( pDest );
}

void CBaseFileSystem::InstallDirtyDiskReportFunc( FSDirtyDiskReportFunc_t func )
{
	m_DirtyDiskReportFunc = func;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *fullpath -
//-----------------------------------------------------------------------------
void CBaseFileSystem::LogAccessToFile( char const *accesstype, char const *fullpath, char const *options )
{
	int c = m_LogFuncs.Count();
	if ( !c )
		return;

	for ( int i = 0; i < c; ++i )
	{
		( m_LogFuncs[ i ] )( fullpath, options );
	}
}


//-----------------------------------------------------------------------------
// Opens a file on disk; nullptr when it can't be opened
//-----------------------------------------------------------------------------
CStdFile *CBaseFileSystem::FOpen( const char *filename, const char *options, unsigned flags, int64_t *size, CFileLoadInfo *pInfo )
{
	if ( m_NonexistingFilesExtensions.GetNumStrings() )
	{
		if ( char const *pszExt = V_GetFileExtension( filename ) )
		{
			UtlSymId_t symFound = m_NonexistingFilesExtensions.Find( pszExt );
			if ( ( symFound != UTL_INVAL_SYMBOL ) && m_NonexistingFilesExtensions[ symFound ] )
			{
				DevWarning( "Known VPK-only extension [%s], file {%s} declared missing. Run with -fullfsvalveds to search filesystem.\n", pszExt, filename );
				return nullptr;
			}
		}
	}

	AUTOBLOCKREPORTER_FN( FOpen, this, true, filename, FILESYSTEM_BLOCKING_SYNCHRONOUS, FileBlockingItem::FB_ACCESS_OPEN );

	if ( pInfo )
		pInfo->m_bLoadedFromSteamCache = false;

	// stop newline characters at end of filename
	Assert( !strchr( filename, '\n' ) && !strchr( filename, '\r' ) );
	CStdFile *fp = CStdFile::Open( filename, options, size );
	if ( fp )
		LogAccessToFile( "open", filename, options );

	return fp;
}

void CBaseFileSystem::GetFileNameForHandle( FileHandle_t handle, char *buf, size_t buflen )
{
	V_strncpy( buf, "Unknown", buflen );
}

//-----------------------------------------------------------------------------
// Closes a file opened by FOpen
//-----------------------------------------------------------------------------
void CBaseFileSystem::FClose( CStdFile *fp )
{
	delete fp;
}


void CBaseFileSystem::PrintSearchPaths( void )
{
	int i;
	Msg( "---------------\n" );
	Msg( "%-20s %s\n", "Path ID:", "File Path:" );
	int c = m_SearchPaths.Count();
	for( i = 0; i < c; i++ )
	{
		CSearchPath *pSearchPath = &m_SearchPaths[i];

		const char *pszPack = "";
		const char *pszType = "";

		Msg( "%-20s \"%s\" %s%s\n", (const char *)pSearchPath->GetPathIDString(), pSearchPath->GetPathString(), pszType, pszPack );
	}
}


//-----------------------------------------------------------------------------
// Purpose: This is where search paths are created.  map files are created at head of list (they occur after
//  file system paths have already been set ) so they get highest priority.  Otherwise, we add the disk (non-packfile)
//  path and then the paks if they exist for the path
// Input  : *pPath -
//-----------------------------------------------------------------------------
void CBaseFileSystem::AddSearchPathInternal(const std::string_view pPath, const std::string_view pathID, SearchPathAdd_t addType, int iForceInsertIndex)
{

	// Clean up the name
	std::string newPath;
	if ( !pPath.empty() )
	{
        if ( vstd::paths::is_absolute( pPath ) )
        {
            newPath = pPath;
        }
        else
        {
            newPath = vstd::paths::make_absolute_path(pPath);
        }
#ifdef _WIN32
	    vstd::make_lower_ascii(newPath);
#endif
        vstd::paths::fix_slashes(newPath);
        vstd::paths::append_slash(newPath);
    }

	// Make sure that it doesn't already exist
	CUtlSymbol pathSym, pathIDSym;
	pathSym = g_PathIDTable.AddString( newPath);
	pathIDSym = g_PathIDTable.AddString( pathID );
	int i;
	int c = m_SearchPaths.Count();
	int id = 0;
	for ( i = 0; i < c; i++ )
	{
		CSearchPath *pSearchPath = &m_SearchPaths[i];
		if ( pSearchPath->GetPath() == pathSym && pSearchPath->GetPathID() == pathIDSym )
		{
			if ( ( addType == PATH_ADD_TO_HEAD && i == 0 ) || ( addType == PATH_ADD_TO_TAIL ) )
			{
				return; // this entry is already at the head
			}
			else
			{
				m_SearchPaths.Remove(i); // remove it from its current position so we can add it back to the head
				i--;
				c--;
			}
		}
		if ( !id && pSearchPath->GetPath() == pathSym )
		{
			// get first found - all reference the same store
			id = pSearchPath->m_storeId;
		}
	}

	if (!id)
	{
		id = g_iNextSearchPathID++;
	}

	// Add to list
	bool bAdded = false;
	int nIndex = m_SearchPaths.Count();

    if ( addType == PATH_ADD_TO_HEAD )
    {
        nIndex = m_SearchPaths.Count() - nIndex;
        Assert( nIndex >= 0 );
    }

    if ( IsPC() || !bAdded )
    {
        // Grab last entry and set the path
        m_SearchPaths.InsertBefore( nIndex );
	}

	// setup the 'base' path
	CSearchPath *sp = &m_SearchPaths[ nIndex ];
	sp->SetPath( pathSym );
	sp->m_pPathIDInfo = FindOrAddPathIDInfo( pathIDSym, -1 );

	// all matching paths have a reference to the same store
	sp->m_storeId = id;

	// classify the dvddev path
	if ( IsDvdDevPathString( newPath ) )
	{
		sp->m_bIsDvdDevPath = true;
	}
}

//-----------------------------------------------------------------------------
// Create the search path.
//-----------------------------------------------------------------------------
void CBaseFileSystem::AddSearchPath(const std::string_view pPath, const std::string_view pathID, SearchPathAdd_t addType)
{
	// The PC has no concept of update/dlc discovery, it explicitly adds them now
	// This layout matches the Xbox's search path layout, when the Xbox does late bind the DLC
	// any platform, game, or mod search paths get subverted in order to prepend the DLC path

	int currCount = m_SearchPaths.Count();

	AddSearchPathInternal( pPath, pathID, addType, true );

	if ( currCount != m_SearchPaths.Count() )
	{
#if !defined( DEDICATED ) && !defined( _CERT )

#endif
	}
}

//-----------------------------------------------------------------------------
// Patches the search path after install has finished. This is a hack until
// a reboot occurs. This is really bad, but our system has no fundamental
// way of manipulating search paths, so had to bury this here. This is designed
// to be innvoked ONCE after the installer has completed to swap in the
// cache based paths. The reboot causes a normal search path build out using
// the proper paths.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::FixupSearchPathsAfterInstall()
{

	return m_bSearchPathsPatchedAfterInstall;
}


//-----------------------------------------------------------------------------
// Returns the search path, each path is separated by ;s. Returns the length of the string returned
// Pack search paths include the pack name, so that callers can still form absolute paths
// and that absolute path can be sent to the filesystem, and mounted as a file inside a pack.
//-----------------------------------------------------------------------------
int CBaseFileSystem::GetSearchPath( const char *pathID, bool bGetPackFiles, char *pPath, int nMaxLen )
{

	int nLen = 0;
	if ( nMaxLen )
	{
		pPath[0] = 0;
	}

	CSearchPathsIterator iter( this, pathID, bGetPackFiles ? FILTER_NONE : FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		if ( nLen >= nMaxLen )
		{
			// Add 1 for the semicolon if our length is not 0
			nLen += (nLen > 0) ? 1 : 0;
            nLen += Q_strlen( pSearchPath->GetPathString() );
			continue;
		}

		if ( nLen != 0 )
		{
			pPath[nLen++] = ';';
		}
	}

	// Return 1 extra for the NULL terminator
	return nLen + 1;
}

//-----------------------------------------------------------------------------
// Returns the search path IDs, each path is separated by ;s. Returns the length of the string returned
//-----------------------------------------------------------------------------
int CBaseFileSystem::GetSearchPathID( char *pPath, int nMaxLen )
{

	if ( nMaxLen )
	{
		pPath[0] = 0;
	}

	// determine unique PathIDs
	CUtlVector< CUtlSymbol > list;
	for ( int i = 0 ; i < m_SearchPaths.Count(); i++ )
	{
		CUtlSymbol pathID = m_SearchPaths[i].GetPathID();
		if ( pathID.IsValid() && list.Find( pathID ) == -1 )
		{
			list.AddToTail( pathID );
			V_strncat( pPath, m_SearchPaths[i].GetPathIDString(), nMaxLen );
			V_strncat( pPath, ";", nMaxLen );
		}
	}

	return strlen( pPath ) + 1;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pPath -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::RemoveSearchPath( const char *pPath, const char *pathID )
{
	char tempSymlinkBuffer[MAX_PATH];
	pPath = V_FormatFilenameForSymlinking( tempSymlinkBuffer, pPath );

	char newPath[ MAX_FILEPATH ];
	newPath[ 0 ] = 0;


	if ( pPath )
	{
		// +2 for '\0' and potential slash added at end.
		Q_strncpy( newPath, pPath, sizeof( newPath ) );
#ifdef _WIN32 // don't do this on linux!
		Q_strlower( newPath );
#endif
		if ( V_stristr( newPath, ".bsp" ) )
		{
			Q_FixSlashes( newPath );
		}
		else
		{
			AddSeperatorAndFixPath( newPath );
		}
	}
	pPath = newPath;

	CUtlSymbol lookup = g_PathIDTable.AddString( pPath );
	CUtlSymbol id = g_PathIDTable.AddString( pathID );

	bool bret = false;

	// Count backward since we're possibly deleting one or more pack files, too
	int i;
	int c = m_SearchPaths.Count();
	for( i = c - 1; i >= 0; i-- )
	{
		if ( newPath && m_SearchPaths[i].GetPath() != lookup )
			continue;

		if ( FilterByPathID( &m_SearchPaths[i], id ) )
			continue;

		m_SearchPaths.Remove( i );
		bret = true;
	}
	return bret;
}


//-----------------------------------------------------------------------------
// Purpose: Removes all search paths for a given pathID, such as all "GAME" paths.
// Input  : pathID -
//-----------------------------------------------------------------------------
void CBaseFileSystem::RemoveSearchPaths( const char *pathID )
{
	int nCount = m_SearchPaths.Count();
	for (int i = nCount - 1; i >= 0; i--)
	{
		if (!Q_stricmp(m_SearchPaths.Element(i).GetPathIDString(), pathID))
		{
			m_SearchPaths.FastRemove(i);
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseFileSystem::CSearchPath *CBaseFileSystem::FindWritePath( const char *pFilename, const char *pathID )
{
	CUtlSymbol lookup = g_PathIDTable.AddString( pathID );


	// a pathID has been specified, find the first match in the path list
	int c = m_SearchPaths.Count();
	for ( int i = 0; i < c; i++ )
	{
		// pak files are not allowed to be written to...
		CSearchPath *pSearchPath = &m_SearchPaths[i];

		if ( pathID && ( pSearchPath->GetPathID() != lookup ) )
		{
			// not the right pathID
			continue;
		}

		return pSearchPath;
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// Purpose: Finds a search path that should be used for writing to, given a pathID.
//-----------------------------------------------------------------------------
const char *CBaseFileSystem::GetWritePath( const char *pFilename, const char *pathID )
{
	CSearchPath *pSearchPath = nullptr;
	if ( pathID && pathID[ 0 ] != '\0' )
	{
		pSearchPath = FindWritePath( pFilename, pathID );
		if ( pSearchPath )
		{
			return pSearchPath->GetPathString();
		}

		FileSystemWarning( FILESYSTEM_WARNING, "Requested non-existent write path %s!\n", pathID );
	}

	pSearchPath = FindWritePath( pFilename, "DEFAULT_WRITE_PATH" );
	if ( pSearchPath )
	{
		return pSearchPath->GetPathString();
	}

	pSearchPath = FindWritePath( pFilename, nullptr); // okay, just return the first search path added!
	if ( pSearchPath )
	{
		return pSearchPath->GetPathString();
	}

	// Hope this is reasonable!!
	return ".\\";
}

//-----------------------------------------------------------------------------
// Reads/writes files to utlbuffers.  Attempts alignment fixups for optimal read
//-----------------------------------------------------------------------------
char* g_pszReadFilename;

bool CBaseFileSystem::ReadToBuffer( FileHandle_t fp, CUtlBuffer &buf, int nMaxBytes, FSAllocFunc_t pfnAlloc )
{
	int nBytesToRead = Size( fp );
	if ( nBytesToRead == 0 )
	{
		// no data in file
		return true;
	}

	if ( nMaxBytes > 0 )
	{
		// can't read more than file has
		nBytesToRead = MIN( nMaxBytes, nBytesToRead );
	}

	int nBytesRead = 0;
	int nBytesOffset = 0;

	int iStartPos = Tell( fp );

	if ( nBytesToRead != 0 )
	{
		int nBytesDestBuffer = nBytesToRead;
		unsigned nSizeAlign = 0, nBufferAlign = 0, nOffsetAlign = 0;

		bool bBinary = !( buf.IsText() && !buf.ContainsCRLF() );

		if ( bBinary && !IsPosix() && !buf.IsExternallyAllocated() && !pfnAlloc &&
			( buf.TellPut() == 0 ) && ( buf.TellGet() == 0 ) && ( iStartPos % 4 == 0 ) &&
			GetOptimalIOConstraints( fp, &nOffsetAlign, &nSizeAlign, &nBufferAlign ) )
		{
			// correct conditions to allow an optimal read
			if ( iStartPos % nOffsetAlign != 0 )
			{
				// move starting position back to nearest alignment
				nBytesOffset = ( iStartPos % nOffsetAlign );
				Assert ( ( iStartPos - nBytesOffset ) % nOffsetAlign == 0 );
				Seek( fp, -nBytesOffset, FILESYSTEM_SEEK_CURRENT );

				// going to read from aligned start, increase target buffer size by offset alignment
				nBytesDestBuffer += nBytesOffset;
			}

			// snap target buffer size to its size alignment
			// add additional alignment slop for target pointer adjustment
			nBytesDestBuffer = AlignValue( nBytesDestBuffer, nSizeAlign ) + nBufferAlign;
		}

		if ( !pfnAlloc )
		{
			buf.EnsureCapacity( nBytesDestBuffer + buf.TellPut() );
		}
		else
		{
			// caller provided allocator
			void *pMemory = (*pfnAlloc)( g_pszReadFilename, nBytesDestBuffer );
			buf.SetExternalBuffer( pMemory, nBytesDestBuffer, 0, buf.GetFlags() & ~CUtlBuffer::EXTERNAL_GROWABLE );
		}

		int seekGet = -1;
		if ( nBytesDestBuffer != nBytesToRead )
		{
			// doing optimal read, align target pointer
			int nAlignedBase = AlignValue( (uint8_t *)buf.Base(), nBufferAlign ) - (uint8_t *)buf.Base();
			buf.SeekPut( CUtlBuffer::SEEK_HEAD, nAlignedBase );

			// the buffer read position is slid forward to ignore the addtional
			// starting offset alignment
			seekGet = nAlignedBase + nBytesOffset;
		}

		nBytesRead = ReadEx( buf.PeekPut(), nBytesDestBuffer - nBufferAlign, nBytesToRead + nBytesOffset, fp );
		buf.SeekPut( CUtlBuffer::SEEK_CURRENT, nBytesRead );

		if ( seekGet != -1 )
		{
			// can only seek the get after data has been put, otherwise buffer sets overflow error
			buf.SeekGet( CUtlBuffer::SEEK_HEAD, seekGet );
		}

		Seek( fp, iStartPos + ( nBytesRead - nBytesOffset ), FILESYSTEM_SEEK_HEAD );
	}

	return (nBytesRead != 0);
}

//-----------------------------------------------------------------------------
// Reads/writes files to utlbuffers
// NOTE NOTE!!
// If you change this implementation, copy it into CBaseVMPIFileSystem::ReadFile
// in vmpi_filesystem.cpp
//-----------------------------------------------------------------------------
bool CBaseFileSystem::ReadFile( const char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes, int nStartingByte, FSAllocFunc_t pfnAlloc )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	bool bBinary = !( buf.IsText() && !buf.ContainsCRLF() );

	FileHandle_t fp = Open( pFileName, ( bBinary ) ? "rb" : "rt", pPath );
	if ( !fp )
		return false;

	if ( nStartingByte != 0 )
	{
		Seek( fp, nStartingByte, FILESYSTEM_SEEK_HEAD );
	}

	if ( pfnAlloc )
	{
		g_pszReadFilename = (char *)pFileName;
	}

	bool bSuccess = ReadToBuffer( fp, buf, nMaxBytes, pfnAlloc );

	Close( fp );

	return bSuccess;
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int CBaseFileSystem::ReadFileEx( const char *pFileName, const char *pPath, void **ppBuf, bool bNullTerminate, bool bOptimalAlloc, int nMaxBytes, int nStartingByte, FSAllocFunc_t pfnAlloc )
{
	FileHandle_t fp = Open( pFileName, "rb", pPath );
	if ( !fp )
	{
		return 0;
	}
	int nBytesToRead = Size( fp );
	int nBytesRead = 0;
	if ( nMaxBytes > 0 )
	{
		nBytesToRead = MIN( nMaxBytes, nBytesToRead );
		if ( bNullTerminate )
		{
			nBytesToRead--;
		}
	}

	if ( nBytesToRead != 0 )
	{
		int nBytesBuf;
		if ( !*ppBuf )
		{
			nBytesBuf = nBytesToRead + ( ( bNullTerminate ) ? 1 : 0 );

			if ( !pfnAlloc && !bOptimalAlloc )
			{
				*ppBuf = new uint8_t[nBytesBuf];
			}
			else if ( !pfnAlloc )
			{
				*ppBuf = AllocOptimalReadBuffer( fp, nBytesBuf, 0 );
				nBytesBuf = GetOptimalReadSize( fp, nBytesBuf );
			}
			else
			{
				*ppBuf = (*pfnAlloc)( pFileName, nBytesBuf );
			}
		}
		else
		{
			nBytesBuf = nMaxBytes;
		}

		if ( nStartingByte != 0 )
		{
			Seek( fp, nStartingByte, FILESYSTEM_SEEK_HEAD );
		}

		nBytesRead = ReadEx( *ppBuf, nBytesBuf, nBytesToRead, fp );

		if ( bNullTerminate )
		{
			((uint8_t *)(*ppBuf))[nBytesToRead] = 0;
		}
	}

	Close( fp );
	return nBytesRead;
}


//-----------------------------------------------------------------------------
// NOTE NOTE!!
// If you change this implementation, copy it into CBaseVMPIFileSystem::WriteFile
// in vmpi_filesystem.cpp
//-----------------------------------------------------------------------------
bool CBaseFileSystem::WriteFile( const char *pFileName, const char *pPath, CUtlBuffer &buf )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	const char *pWriteFlags = "wb";
	if ( buf.IsText() && !buf.ContainsCRLF() )
	{
		pWriteFlags = "wt";
	}

	FileHandle_t fp = Open( pFileName, pWriteFlags, pPath );
	if ( !fp )
		return false;

	int nBytesWritten = Write( buf.Base(), buf.TellMaxPut(), fp );

	Close( fp );
	return (nBytesWritten != 0);
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseFileSystem::RemoveAllSearchPaths( void )
{
	// Sergiy: AaronS said it is a good idea to destroy these paths in reverse order
	while( m_SearchPaths.Count() )
	{
		m_SearchPaths.Remove( m_SearchPaths.Count() - 1 );
	}
	//m_PackFileHandles.Purge();
}


class CFileOpenInfo
{
public:
	CFileOpenInfo( CBaseFileSystem *pFileSystem, const char *pFileName, const CBaseFileSystem::CSearchPath *path, const char *pOptions, int flags, char **ppszResolvedFilename, bool bTrackCRCs ) :
		m_pFileSystem( pFileSystem ), m_pFileName( pFileName ), m_pPath( path ), m_pOptions( pOptions ), m_Flags( flags ), m_ppszResolvedFilename( ppszResolvedFilename ), m_bTrackCRCs( bTrackCRCs )
	{
		m_pFileHandle = nullptr;
		m_bLoadedFromSteamCache = m_bSteamCacheOnly = false;

		if ( m_ppszResolvedFilename )
			*m_ppszResolvedFilename = nullptr;
	}

	~CFileOpenInfo()
	{
	}

	void SetAbsolutePath( const char *pFormat, ... )
	{
		va_list marker;
		va_start( marker, pFormat );
		V_vsnprintf( m_AbsolutePath, sizeof( m_AbsolutePath ), pFormat, marker );
		va_end( marker );

		V_FixSlashes( m_AbsolutePath );
	}

	void SetResolvedFilename( const char *pStr )
	{
		if ( m_ppszResolvedFilename )
		{
			Assert( !( *m_ppszResolvedFilename ) );
			*m_ppszResolvedFilename = strdup( pStr );
		}
	}

	// Decides if the file must come from Steam or if it can be allowed to come off disk.
	void DetermineFileLoadInfoParameters( CFileLoadInfo &fileLoadInfo, bool bIsAbsolutePath )
	{
        fileLoadInfo.m_bSteamCacheOnly = false;
	}

public:
	CBaseFileSystem *m_pFileSystem;

	// These are output parameters.
	CFileHandle *m_pFileHandle;
	char **m_ppszResolvedFilename;

	const char *m_pFileName;
	const CBaseFileSystem::CSearchPath *m_pPath;
	const char *m_pOptions;
	int m_Flags;
	bool m_bTrackCRCs;

	// Stats about how the file was opened and how we asked the stdio/steam filesystem to open it.
	// Used to decide whether or not we need to generate and store CRCs.
	bool m_bLoadedFromSteamCache;	// Did it get loaded out of the Steam cache?
	bool m_bSteamCacheOnly;			// Are we asking that this file only come from Steam?

	char m_AbsolutePath[MAX_FILEPATH]{0};	// This is set
};

void CBaseFileSystem::HandleOpenRegularFile( CFileOpenInfo &openInfo, bool bIsAbsolutePath )
{
	// Setup the parameters for the call (like to tell Steam to force the file to come out of the Steam caches or not).
	CFileLoadInfo fileLoadInfo;
	openInfo.DetermineFileLoadInfoParameters( fileLoadInfo, bIsAbsolutePath );

	// xbox dvddev mode needs to convolve non-compliant fatx filenames
	// purposely placing this at this level, so only loose files pay the burden
	const char *pFilename = openInfo.m_AbsolutePath;

	int64_t size;
	CStdFile *fp = FOpen( pFilename, openInfo.m_pOptions, openInfo.m_Flags, &size, &fileLoadInfo );
	if ( fp )
	{
		if ( m_bOutputDebugString )
		{
			fprintf( stderr, "fs_debug: %s\n", openInfo.m_AbsolutePath );
		}

		openInfo.m_pFileHandle = new CFileHandle(this);
		openInfo.m_pFileHandle->m_pFile = fp;
		openInfo.m_pFileHandle->m_type = FT_NORMAL;
		openInfo.m_pFileHandle->m_nLength = size;

		openInfo.SetResolvedFilename( openInfo.m_AbsolutePath );

		// Remember what was returned by the Steam filesystem.
		openInfo.m_bLoadedFromSteamCache = fileLoadInfo.m_bLoadedFromSteamCache;
		openInfo.m_bSteamCacheOnly = fileLoadInfo.m_bSteamCacheOnly;
	}
}


//-----------------------------------------------------------------------------
// Purpose: The base file search goes through here
// Input  : *path -
//			*pFileName -
//			*pOptions -
//			packfile -
//			*filetime -
// Output : FileHandle_t
//-----------------------------------------------------------------------------
FileHandle_t CBaseFileSystem::FindFile(
	const CSearchPath *path,
	const char *pFileName,
	const char *pOptions,
	unsigned flags,
	char **ppszResolvedFilename,
	bool bTrackCRCs )
{

	char tempSymlinkBuffer[MAX_PATH]{0};
	pFileName = V_FormatFilenameForSymlinking( tempSymlinkBuffer, pFileName );

	CFileOpenInfo openInfo( this, pFileName, path, pOptions, flags, ppszResolvedFilename, bTrackCRCs );
	bool bIsAbsolutePath = V_IsAbsolutePath( pFileName );
	if ( bIsAbsolutePath )
	{
		openInfo.SetAbsolutePath( "%s", pFileName );
	}
	else
	{
		// Caller provided a relative path
        openInfo.SetAbsolutePath( "%s%s", path->GetPathString(), pFileName );
	}

	// now have an absolute name
	HandleOpenRegularFile( openInfo, bIsAbsolutePath );
	return (FileHandle_t)openInfo.m_pFileHandle;
}


FileHandle_t CBaseFileSystem::FindFileInSearchPaths(
	const char *pFileName,
	const char *pOptions,
	const char *pathID,
	unsigned flags,
	char **ppszResolvedFilename,
	bool bTrackCRCs )
{
	// Run through all the search paths.
	PathTypeFilter_t pathFilter = FILTER_NONE;

#define IsPakStrictMode() true



	CSearchPathsIterator iter( this, &pFileName, pathID, pathFilter );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		FileHandle_t filehandle = FindFile( pSearchPath, pFileName, pOptions, flags, ppszResolvedFilename, bTrackCRCs );
		if ( filehandle )
			return filehandle;
	}

	return ( FileHandle_t )nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FileHandle_t CBaseFileSystem::OpenForRead( const char *pFileName, const char *pOptions, unsigned flags, const char *pathID, char **ppszResolvedFilename )
{
	return FindFileInSearchPaths( pFileName, pOptions, pathID, flags, ppszResolvedFilename, true );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FileHandle_t CBaseFileSystem::OpenForWrite( const char *pFileName, const char *pOptions, const char *pathID )
{
	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pathID, tempPathID );

	// Opening for write or append uses the write path
	// Unless an absolute path is specified...
	const char *pTmpFileName;
	char szScratchFileName[MAX_PATH];
	if ( Q_IsAbsolutePath( pFileName ) )
	{
		pTmpFileName = pFileName;
	}
	else
	{
		ComputeFullWritePath( szScratchFileName, sizeof( szScratchFileName ), pFileName, pathID );
		pTmpFileName = szScratchFileName;
	}

	int64_t size;
	CStdFile *fp = FOpen( pTmpFileName, pOptions, 0, &size );
	if ( !fp )
	{
		return ( FileHandle_t )nullptr;
	}

	CFileHandle *fh = new CFileHandle( this );
	fh->m_nLength = size;
	fh->m_type = FT_NORMAL;
	fh->m_pFile = fp;

	return ( FileHandle_t )fh;
}


// This looks for UNC-type filename specifiers, which should be used instead of
// passing in path ID. So if it finds //mod/cfg/config.cfg, it translates
// pFilename to "cfg/config.cfg" and pPathID to "mod" (mod is placed in tempPathID).
void CBaseFileSystem::ParsePathID( const char* &pFilename, const char* &pPathID, char tempPathID[MAX_PATH] )
{
	tempPathID[0] = 0;

	if ( !pFilename || pFilename[0] == 0 )
		return;

	// FIXME: Pain! Backslashes are used to denote network drives, forward to denote path ids
	// HOORAY! We call FixSlashes everywhere. That will definitely not work
	// I'm not changing it yet though because I don't know how painful the bugs would be that would be generated
	bool bIsForwardSlash = ( pFilename[0] == '/' && pFilename[1] == '/' );
//	bool bIsBackwardSlash = ( pFilename[0] == '\\' && pFilename[1] == '\\' );
	if ( !bIsForwardSlash ) //&& !bIsBackwardSlash )
		return;

	// Parse out the path ID.
	const char *pIn = &pFilename[2];
	char *pOut = tempPathID;
	while ( *pIn && !PATHSEPARATOR( *pIn ) && (pOut - tempPathID) < (MAX_PATH-1) )
	{
		*pOut++ = *pIn++;
	}

	*pOut = 0;

	// They're specifying two path IDs. Ignore the one passed-in.
	// AND only warn if they are inconsistent
	if ( pPathID && Q_stricmp( pPathID, tempPathID ) )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS: Specified two path IDs (%s, %s).\n", pFilename, pPathID );
	}
	if ( tempPathID[0] == '*' )
	{
		// * means NULL.
		pPathID = nullptr;
	}
	else
	{
		pPathID = tempPathID;
	}

	// Move pFilename up past the part with the path ID.
	if ( *pIn == 0 )
		pFilename = pIn;
	else
		pFilename = pIn + 1;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FileHandle_t CBaseFileSystem::Open( const char *pFileName, const char *pOptions, const char *pathID )
{
	return OpenEx( pFileName, pOptions, 0, pathID );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FileHandle_t CBaseFileSystem::OpenEx( const char *pFileName, const char *pOptions, unsigned flags, const char *pathID, char **ppszResolvedFilename )
{


	if ( !pFileName )
		return (FileHandle_t)nullptr;

#ifndef _CERT
	s_IoStats.OnFileOpen( pFileName );
#endif

	NoteIO();
	CHECK_DOUBLE_SLASHES( pFileName );

	// Allow for UNC-type syntax to specify the path ID.
	char tempPathID[MAX_PATH]{0};
	ParsePathID( pFileName, pathID, tempPathID );
	char tempFileName[MAX_PATH]{0};
	Q_strncpy( tempFileName, pFileName, sizeof(tempFileName) );
	Q_FixSlashes( tempFileName );
#ifdef _WIN32
	Q_strlower( tempFileName );
#endif

	FileHandle_t hFile;

	// Try each of the search paths in succession
	// FIXME: call createdirhierarchy upon opening for write.
	if ( strstr( pOptions, "r" ) && !strstr( pOptions, "+" ) )
	{
		hFile = OpenForRead( tempFileName, pOptions, flags, pathID, ppszResolvedFilename );
	}
	else
	{
		hFile = OpenForWrite( tempFileName, pOptions, pathID );
	}

	return hFile;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseFileSystem::Close( FileHandle_t file )
{
	if ( !file )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Close NULL file handle!\n" );
		return;
	}

	delete (CFileHandle*)file;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseFileSystem::Seek( FileHandle_t file, int pos, FileSystemSeek_t whence )
{

	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "Tried to Seek NULL file handle!\n" );
		return;
	}

#ifndef _CERT
	int nTimeStart = Plat_MSTime();
#endif
	fh->Seek( pos, whence );
#ifndef _CERT
	s_IoStats.OnFileSeek( Plat_MSTime() - nTimeStart );
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : file -
// Output : unsigned int
//-----------------------------------------------------------------------------
unsigned int CBaseFileSystem::Tell( FileHandle_t file )
{


	if ( !file )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Tell NULL file handle!\n" );
		return 0;
	}


	// Pack files are relative
	return (( CFileHandle *)file)->Tell();
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : file -
// Output : unsigned int
//-----------------------------------------------------------------------------
unsigned int CBaseFileSystem::Size( FileHandle_t file )
{


	if ( !file )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Size NULL file handle!\n" );
		return 0;
	}

	return ((CFileHandle *)file)->Size();
}



//-----------------------------------------------------------------------------
// Purpose:
// Input  : file -
// Output : unsigned int
//-----------------------------------------------------------------------------
unsigned int CBaseFileSystem::Size( const char* pFileName, const char *pPathID )
{

	CHECK_DOUBLE_SLASHES( pFileName );

	// handle the case where no name passed...
	if ( !pFileName || !pFileName[0] )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Size NULL filename!\n" );
		return 0;
	}

	if ( IsPC() )
	{
		// If we have a whitelist and it's forcing the file to load from Steam instead of from disk,
		// then do this the slow way, otherwise we'll get the wrong file size (i.e. the size of the file on disk).
        FileHandle_t fh = Open( pFileName, "rb", pPathID );
        if ( fh )
        {
            unsigned int ret = Size( fh );
            Close( fh );
            return ret;
        }
        else
        {
            return 0;
        }
	}

	// Ok, fall through to the fast path.
	int iSize = 0;

	CSearchPathsIterator iter( this, &pFileName, pPathID );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		iSize = FastFindFile( pSearchPath, pFileName );
		if ( iSize >= 0 )
		{
			break;
		}
	}
	return iSize;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *path -
//			*pFileName -
// Output : long
//-----------------------------------------------------------------------------
int64_t CBaseFileSystem::FastFileTime( const CSearchPath *path, const char *pFileName )
{
	FileStat_t buf;

	char tempSymlinkBuffer[MAX_PATH];
	pFileName = V_FormatFilenameForSymlinking( tempSymlinkBuffer, pFileName );


    // Is it an absolute path?
    char tempFileName[ MAX_FILEPATH ];

    if ( Q_IsAbsolutePath( pFileName ) )
    {
        Q_strncpy( tempFileName, pFileName, sizeof( tempFileName ) );
    }
    else
    {
        bool bFileInVpk = false;

        if ( !bFileInVpk )
        {
            Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", path->GetPathString(), pFileName );
        }
    }
    Q_FixSlashes( tempFileName );
    if ( FS_Stat( tempFileName, buf ) )
    {
        return buf.nModifyTime;
    }
	return ( 0L );
}

int CBaseFileSystem::FastFindFile( const CSearchPath *path, const char *pFileName )
{
	FileStat_t buf;

	char tempSymlinkBuffer[MAX_PATH];
	pFileName = V_FormatFilenameForSymlinking( tempSymlinkBuffer, pFileName );

	char tempFileName[ MAX_FILEPATH ];

	// Is it an absolute path?
	bool bRelativePath = !Q_IsAbsolutePath( pFileName );

	if ( !bRelativePath )
	{
		Q_strncpy( tempFileName, pFileName, sizeof( tempFileName ) );
	}
	else
	{
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", path->GetPathString(), pFileName );
	}
	Q_FixSlashes( tempFileName );
	if ( FS_Stat( tempFileName, buf ) )
	{
		LogAccessToFile( "stat", tempFileName, "" );
		return buf.nSize;
	}
	return ( -1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CBaseFileSystem::EndOfFile( FileHandle_t file )
{
	if ( !file )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to EndOfFile NULL file handle!\n" );
		return true;
	}

	return ((CFileHandle *)file)->EndOfFile();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CBaseFileSystem::Read( void *pOutput, int size, FileHandle_t file )
{
	return ReadEx( pOutput, size, size, file );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CBaseFileSystem::ReadEx( void *pOutput, int destSize, int size, FileHandle_t file )
{

	NoteIO();
	if ( !file )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Read NULL file handle!\n" );
		return 0;
	}
	if ( size < 0 )
	{
		return 0;
	}

#ifndef _CERT
	int nTimeStart = Plat_MSTime();
#endif
	int nRet = ((CFileHandle*)file)->Read(pOutput, destSize, size );
#ifndef _CERT
	s_IoStats.OnFileRead( Plat_MSTime() - nTimeStart, size );
#endif

	return nRet;

}

//-----------------------------------------------------------------------------
// Purpose: Takes a passed in KeyValues& head and fills in the precompiled keyvalues data into it.
// Input  : head -
//			type -
//			*filename -
//			*pPathID -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::LoadKeyValues( KeyValues& head, KeyValuesPreloadType_t type, char const *filename, char const *pPathID /*= 0*/ )
{
	bool bret = true;

#ifndef DEDICATED
	char tempPathID[MAX_PATH];
	ParsePathID( filename, pPathID, tempPathID );
	bret = head.LoadFromFile( this, filename, pPathID );
#else
	bret = head.LoadFromFile( this, filename, pPathID );
#endif
	return bret;
}


//-----------------------------------------------------------------------------
// Purpose: If the "PreloadedData" hasn't been purged, then this'll try and instance the KeyValues using the fast path of
/// compiled keyvalues loaded during startup.
// Otherwise, it'll just fall through to the regular KeyValues loading routines
// Input  : type -
//			*filename -
//			*pPathID -
// Output : KeyValues
//-----------------------------------------------------------------------------
KeyValues *CBaseFileSystem::LoadKeyValues( KeyValuesPreloadType_t type, char const *filename, char const *pPathID /*= 0*/ )
{
	KeyValues *kv = new KeyValues( filename );
	if ( kv )
	{
		kv->LoadFromFile( this, filename, pPathID );
	}

	return kv;
}

//-----------------------------------------------------------------------------
// Purpose: This is the fallback method of reading the name of the first key in the file
// Input  : *filename -
//			*pPathID -
//			*rootName -
//			bufsize -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::LookupKeyValuesRootKeyName( char const *filename, char const *pPathID, char *rootName, size_t bufsize )
{
	if ( FileExists( filename, pPathID ) )
	{
		// open file and get shader name
		FileHandle_t hFile = Open( filename, "r", pPathID );
		if ( hFile == FILESYSTEM_INVALID_HANDLE )
		{
			return false;
		}

		char buf[ 128 ];
		ReadLine( buf, sizeof( buf ), hFile );
		Close( hFile );

		// The name will possibly come in as "foo"\n

		// So we need to strip the starting " character
		char *pStart = buf;
		if ( *pStart == '\"' )
		{
			++pStart;
		}
		// Then copy the rest of the string
		Q_strncpy( rootName, pStart, bufsize );

		// And then strip off the \n and the " character at the end, in that order
		int len = Q_strlen( pStart );
		while ( len > 0 && rootName[ len - 1 ] == '\n' )
		{
			rootName[ len - 1 ] = 0;
			--len;
		}
		while ( len > 0 && rootName[ len - 1 ] == '\"' )
		{
			rootName[ len - 1 ] = 0;
			--len;
		}
	}
	else
	{
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CBaseFileSystem::Write( void const* pInput, int size, FileHandle_t file )
{


	AUTOBLOCKREPORTER_FH( Write, this, true, file, FILESYSTEM_BLOCKING_SYNCHRONOUS, FileBlockingItem::FB_ACCESS_WRITE );

	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Write NULL file handle!\n" );
		return 0;
	}
	return fh->Write( pInput, size );

}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CBaseFileSystem::FPrintf( FileHandle_t file, const char *pFormat, ... )
{
	va_list args;
	va_start( args, pFormat );

	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to FPrintf NULL file handle!\n" );
		return 0;
	}
/*
	if ( !fh->GetFileHandle() )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to FPrintf NULL file pointer inside valid file handle!\n" );
		return 0;
	}
	*/


	char buffer[65535];
	int len = vsnprintf( buffer, sizeof( buffer), pFormat, args );
	len = fh->Write( buffer, len );
	//int len = FS_vfprintf( fh->GetFileHandle() , pFormat, args );
	va_end( args );


	return len;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CBaseFileSystem::IsOk( FileHandle_t file )
{
	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to IsOk NULL file handle!\n" );
		return false;
	}

	return fh->IsOK();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseFileSystem::Flush( FileHandle_t file )
{

	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Flush NULL file handle!\n" );
		return;
	}

	fh->Flush();

}

bool CBaseFileSystem::Precache( const char *pFileName, const char *pPathID)
{
	CHECK_DOUBLE_SLASHES( pFileName );

	// Allow for UNC-type syntax to specify the path ID.
	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pPathID, tempPathID );
	Assert( pPathID );

	// Really simple, just open, the file, read it all in and close it.
	// We probably want to use file mapping to do this eventually.
	FileHandle_t f = Open( pFileName, "rb", pPathID );
	if ( !f )
		return false;

	// not for consoles, the read discard is a negative benefit, slow and clobbers small drive caches
	if ( IsPC() )
	{
		char buffer[16384];
		while( sizeof(buffer) == Read(buffer,sizeof(buffer),f) );
	}

	Close( f );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
char *CBaseFileSystem::ReadLine( char *pOutput, int maxChars, FileHandle_t file )
{

	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to ReadLine NULL file handle!\n" );
		return nullptr;
	}
	m_Stats.nReads++;

	int nRead = 0;

	// Read up to maxchars:
	while( nRead < ( maxChars - 1 ) )
	{
		// Are we at the end of the file?
		if( 1 != fh->Read( pOutput + nRead, 1 ) )
			break;

		// Translate for text mode files:
		if( fh->m_type == FT_PACK_TEXT && pOutput[nRead] == '\r' )
		{
			// Ignore \r
			continue;
		}

		// We're done when we hit a '\n'
		if( pOutput[nRead] == '\n' )
		{
			nRead++;
			break;
		}

		// Get outta here if we find a NULL.
		if( pOutput[nRead] == '\0' )
		{
			pOutput[nRead] = '\n';
			nRead++;
			break;
		}

		nRead++;
	}

	if( nRead < maxChars )
		pOutput[nRead] = '\0';


	m_Stats.nBytesRead += nRead;
	return ( nRead ) ? pOutput : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pFileName -
// Output : long
//-----------------------------------------------------------------------------
int64_t CBaseFileSystem::GetFileTime( const char *pFileName, const char *pPathID )
{


	CHECK_DOUBLE_SLASHES( pFileName );

	CSearchPathsIterator iter( this, &pFileName, pPathID );

	char tempFileName[MAX_PATH];
	Q_strncpy( tempFileName, pFileName, sizeof(tempFileName) );
	Q_FixSlashes( tempFileName );
#ifdef _WIN32
	Q_strlower( tempFileName );
#endif

	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		int64_t ft = FastFileTime( pSearchPath, tempFileName );
		if ( ft != 0L )
		{
			if ( m_LogFuncs.Count() )
			{
				char pTmpFileName[ MAX_FILEPATH ];
				if ( strchr( tempFileName, ':' ) ) //
				{
					Q_strncpy( pTmpFileName, tempFileName, sizeof( pTmpFileName ) );
				}
				else
				{
					Q_snprintf( pTmpFileName, sizeof( pTmpFileName ), "%s%s", pSearchPath->GetPathString(), tempFileName );
				}

				Q_FixSlashes( pTmpFileName );

				LogAccessToFile( "filetime", pTmpFileName, "" );
			}

			return ft;
		}
	}
	return 0L;
}

int64_t CBaseFileSystem::GetPathTime( const char *pFileName, const char *pPathID )
{


	CSearchPathsIterator iter( this, &pFileName, pPathID );

	char tempFileName[MAX_PATH];
	Q_strncpy( tempFileName, pFileName, sizeof(tempFileName) );
	Q_FixSlashes( tempFileName );
#ifdef _WIN32
	Q_strlower( tempFileName );
#endif

	int64_t pathTime = 0L;
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		int64_t ft = FastFileTime( pSearchPath, tempFileName );
		if ( ft > pathTime )
			pathTime = ft;
		if ( ft != 0L )
		{
			if (  m_LogFuncs.Count() )
			{
				char pTmpFileName[ MAX_FILEPATH ];
				if ( strchr( tempFileName, ':' ) )
				{
					Q_strncpy( pTmpFileName, tempFileName, sizeof( pTmpFileName ) );
				}
				else
				{
					Q_snprintf( pTmpFileName, sizeof( pTmpFileName ), "%s%s", pSearchPath->GetPathString(), tempFileName );
				}

				Q_FixSlashes( pTmpFileName );

				LogAccessToFile( "filetime", pTmpFileName, "" );
			}
		}
	}
	return pathTime;
}



void CBaseFileSystem::MarkAllCRCsUnverified()
{


//	m_FileTracker2.MarkAllCRCsUnverified();
}


void CBaseFileSystem::CacheFileCRCs( const char *pPathname, ECacheCRCType eType, IFileList *pFilter )
{


	// Get a list of the unique search path names (mod, game, platform, etc).
	CUtlDict<int,int> searchPathNames;
	for ( int i = 0; i <  m_SearchPaths.Count(); i++ )
	{
		CSearchPath *pSearchPath = &m_SearchPaths[i];
		if ( searchPathNames.Find( pSearchPath->GetPathIDString() ) == searchPathNames.InvalidIndex() )
			searchPathNames.Insert( pSearchPath->GetPathIDString() );
	}

}

EFileCRCStatus CBaseFileSystem::CheckCachedFileHash( const char *pPathID, const char *pRelativeFilename, int nFileFraction, FileHash_t *pFileHash )
{
//	return m_FileTracker2.CheckCachedFileHash( pPathID, pRelativeFilename, nFileFraction, pFileHash );
    return EFileCRCStatus::k_eFileCRCStatus_CantOpenFile;
}


int CBaseFileSystem::GetUnverifiedFileHashes( CUnverifiedFileHash *pFiles, int nMaxFiles )
{
//	return m_FileTracker2.GetUnverifiedFileHashes( pFiles, nMaxFiles );
    return 0;
}



//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pString -
//			maxCharsIncludingTerminator -
//			fileTime -
//-----------------------------------------------------------------------------
void CBaseFileSystem::FileTimeToString( char *pString, int maxCharsIncludingTerminator, int64_t fileTime )
{
	{
		time_t time = fileTime;
		V_strncpy( pString, ctime( &time ), maxCharsIncludingTerminator );

		// We see a linefeed at the end of these strings...if there is one, gobble it up
		int len = V_strlen( pString );
		if ( pString[ len - 1 ] == '\n' )
		{
			pString[ len - 1 ] = '\0';
		}

		pString[maxCharsIncludingTerminator-1] = '\0';
	}
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pFileName -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::FileExists( const char *pFileName, const char *pPathID )
{

	NoteIO();

	CHECK_DOUBLE_SLASHES( pFileName );

	CSearchPathsIterator iter( this, &pFileName, pPathID );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		int size = FastFindFile( pSearchPath, pFileName );
		if ( size >= 0 )
		{
			return true;
		}
	}
	return false;
}

bool CBaseFileSystem::IsFileWritable( char const *pFileName, char const *pPathID /*=0*/ )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	FileStat_t buf;

	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pPathID, tempPathID );

	if ( Q_IsAbsolutePath( pFileName ) )
	{
		return FS_Stat( pFileName, buf ) && buf.bWritable;
	}

	CSearchPathsIterator iter( this, &pFileName, pPathID, FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFileName );
		if ( FS_Stat( tempFileName, buf ) && buf.bWritable )
		{
			return true;
		}
	}
	return false;
}


bool CBaseFileSystem::SetFileWritable( char const *pFileName, bool writable, const char *pPathID /*= 0*/ )
{
	CHECK_DOUBLE_SLASHES( pFileName );
	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pPathID, tempPathID );

	if ( Q_IsAbsolutePath( pFileName ) )
	{
		return FS_SetWritable( pFileName, writable );
	}

	CSearchPathsIterator iter( this, &pFileName, pPathID, FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		char tempFilename[ MAX_FILEPATH ];
		Q_snprintf( tempFilename, sizeof( tempFilename ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFilename );
		if ( FS_SetWritable( tempFilename, writable ) )
		{
			return true;
		}
	}

	// Failure
	return false;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pFileName -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::IsDirectory( const char *pFileName, const char *pathID )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	// Allow for UNC-type syntax to specify the path ID.
	FileStat_t buf;

	char pTempBuf[MAX_PATH];
	Q_strncpy( pTempBuf, pFileName, sizeof(pTempBuf) );
	Q_StripTrailingSlash( pTempBuf );
	pFileName = pTempBuf;

	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pathID, tempPathID );
	if ( Q_IsAbsolutePath( pFileName ) )
	{
		return FS_Stat( pFileName, buf ) && buf.bDirectory;
	}

	CSearchPathsIterator iter( this, &pFileName, pathID, FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFileName );
		if ( FS_Stat( tempFileName, buf ) && buf.bDirectory )
		{
			return true;
		}
	}
	return ( false );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *path -
//-----------------------------------------------------------------------------
void CBaseFileSystem::CreateDirHierarchy( const char *pRelativePath, const char *pathID )
{
	CHECK_DOUBLE_SLASHES( pRelativePath );

	// Allow for UNC-type syntax to specify the path ID.
	char tempPathID[MAX_PATH];
 	ParsePathID( pRelativePath, pathID, tempPathID );

	char szScratchFileName[MAX_PATH];
	if ( !Q_IsAbsolutePath( pRelativePath ) )
	{
		Assert( pathID );
		ComputeFullWritePath( szScratchFileName, sizeof( szScratchFileName ), pRelativePath, pathID );
	}
	else
	{
		Q_strncpy( szScratchFileName, pRelativePath, sizeof(szScratchFileName) );
	}
	FS_CreateDirectories( szScratchFileName );
}


//-----------------------------------------------------------------------------
// Purpose: Given an absolute path, do a find first find next on it and build
// a list of files.  Physical file system only
//-----------------------------------------------------------------------------
void CBaseFileSystem::FindFileAbsoluteListHelper( CUtlVector< CUtlString > &outAbsolutePathNames, FindData_t &findData, const char *pAbsoluteFindName )
{
	char path[MAX_PATH];
	V_strncpy( path, pAbsoluteFindName, sizeof(path) );
	V_StripFilename( path );

	findData.findHandle = FS_FindFirst( pAbsoluteFindName, findData.findData );

	while ( findData.findHandle != nullptr )
	{
		char result[MAX_PATH];
		V_ComposeFileName( path, findData.findData.cFileName, result, sizeof(result) );

		outAbsolutePathNames.AddToTail( result );

		if ( !FS_FindNext( findData.findHandle, findData.findData ) )
		{
			FS_FindClose( findData.findHandle );
			findData.findHandle = nullptr;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Searches for a file in all paths and results absolute path names
// for the file, works in pack files (zip and vpk) too.  Lets you search for
// something like sound/sound.cache and get a list of every sound cache
//-----------------------------------------------------------------------------
void CBaseFileSystem::FindFileAbsoluteList( CUtlVector< CUtlString > &outAbsolutePathNames, const char *pWildCard, const char *pPathID )
{
	// TODO: figure out what PS3 does without VPKs


	outAbsolutePathNames.Purge();

	FindData_t findData;
	if ( pPathID )
	{
		findData.m_FilterPathID = g_PathIDTable.AddString( pPathID );
	}
	int maxlen = strlen( pWildCard ) + 1;
	findData.wildCardString.AddMultipleToTail( maxlen );
	Q_strncpy( findData.wildCardString.Base(), pWildCard, maxlen );
	Q_FixSlashes( findData.wildCardString.Base() );
	findData.findHandle = nullptr;

	if ( Q_IsAbsolutePath( pWildCard ) )
	{
		FindFileAbsoluteListHelper( outAbsolutePathNames, findData, pWildCard );
	}
	else
	{
		int c = m_SearchPaths.Count();
		for (	findData.currentSearchPathID = 0;
			findData.currentSearchPathID < c;
			findData.currentSearchPathID++ )
		{
			CSearchPath *pSearchPath = &m_SearchPaths[findData.currentSearchPathID];


			if ( FilterByPathID( pSearchPath, findData.m_FilterPathID ) )
				continue;

			// already visited this path
			if ( findData.m_VisitedSearchPaths.MarkVisit( *pSearchPath ) )
				continue;

			char tempFileName[ MAX_FILEPATH ];
			Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), findData.wildCardString.Base() );
			Q_FixSlashes( tempFileName );

			FindFileAbsoluteListHelper( outAbsolutePathNames, findData, tempFileName );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pWildCard -
//			*pHandle -
// Output : const char
//-----------------------------------------------------------------------------
const char *CBaseFileSystem::FindFirstEx( const char *pWildCard, const char *pPathID, FileFindHandle_t *pHandle )
{
	CHECK_DOUBLE_SLASHES( pWildCard );

	return FindFirstHelper( pWildCard, pPathID, pHandle, nullptr);
}


const char *CBaseFileSystem::FindFirstHelper( const char *pWildCard, const char *pPathID, FileFindHandle_t *pHandle, int *pFoundStoreID )
{

 	Assert( pWildCard );
 	Assert( pHandle );

	FileFindHandle_t hTmpHandle = m_FindData.AddToTail();
	FindData_t *pFindData = &m_FindData[hTmpHandle];
	Assert( pFindData );
	if ( pPathID )
	{
		pFindData->m_FilterPathID = g_PathIDTable.AddString( pPathID );
	}
	int maxlen = strlen( pWildCard ) + 1;
	pFindData->wildCardString.AddMultipleToTail( maxlen );
	Q_strncpy( pFindData->wildCardString.Base(), pWildCard, maxlen );
	Q_FixSlashes( pFindData->wildCardString.Base() );
	pFindData->findHandle = nullptr;

	if ( Q_IsAbsolutePath( pWildCard ) )
	{



		pFindData->findHandle = FS_FindFirst( pWildCard, pFindData->findData );
		pFindData->currentSearchPathID = -1;
	}
	else
	{
		int c = m_SearchPaths.Count();
		for (	pFindData->currentSearchPathID = 0;
				pFindData->currentSearchPathID < c;
				pFindData->currentSearchPathID++ )
		{
			CSearchPath *pSearchPath = &m_SearchPaths[pFindData->currentSearchPathID];


			if ( FilterByPathID( pSearchPath, pFindData->m_FilterPathID ) )
				continue;

			// already visited this path
			if ( pFindData->m_VisitedSearchPaths.MarkVisit( *pSearchPath ) )
				continue;

			char tempFileName[ MAX_FILEPATH ];
			Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFindData->wildCardString.Base() );
			Q_FixSlashes( tempFileName );




			pFindData->findHandle = FS_FindFirst( tempFileName, pFindData->findData );
			pFindData->m_CurrentStoreID = pSearchPath->m_storeId;

			if ( pFindData->findHandle != nullptr )
				break;
		}
	}

	// We have a result from the filesystem
	if( pFindData->findHandle != nullptr )
	{
		// Remember that we visited this file already.
		pFindData->m_VisitedFiles.Insert( pFindData->findData.cFileName, 0 );

		if ( pFoundStoreID )
			*pFoundStoreID = pFindData->m_CurrentStoreID;

		*pHandle = hTmpHandle;
		return pFindData->findData.cFileName;
	}
	// Handle failure here
	pFindData = nullptr;
	m_FindData.Remove(hTmpHandle);
	*pHandle = -1;

	return nullptr;
}

const char *CBaseFileSystem::FindFirst( const char *pWildCard, FileFindHandle_t *pHandle )
{
	return FindFirstEx( pWildCard, nullptr, pHandle );
}


// Get the next file, trucking through the path. . don't check for duplicates.
bool CBaseFileSystem::FindNextFileHelper( FindData_t *pFindData, int *pFoundStoreID )
{
	// PAK files???

	// Try the same search path that we were already searching on.
	if( FS_FindNext( pFindData->findHandle, pFindData->findData ) )
	{
		if ( pFoundStoreID )
			*pFoundStoreID = pFindData->m_CurrentStoreID;

		return true;
	}

	// This happens when we searched a full path
	if ( pFindData->currentSearchPathID < 0 )
		return false;

	pFindData->currentSearchPathID++;

	if ( pFindData->findHandle != nullptr )
	{
		FS_FindClose( pFindData->findHandle );
	}
	pFindData->findHandle = nullptr;

	int c = m_SearchPaths.Count();
	for( ; pFindData->currentSearchPathID < c; ++pFindData->currentSearchPathID )
	{
		CSearchPath *pSearchPath = &m_SearchPaths[pFindData->currentSearchPathID];

		if ( FilterByPathID( pSearchPath, pFindData->m_FilterPathID ) )
			continue;

		// already visited this path
		if ( pFindData->m_VisitedSearchPaths.MarkVisit( *pSearchPath ) )
			continue;

		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFindData->wildCardString.Base() );
		Q_FixSlashes( tempFileName );




		pFindData->findHandle = FS_FindFirst( tempFileName, pFindData->findData );
		pFindData->m_CurrentStoreID = pSearchPath->m_storeId;
		if ( pFindData->findHandle != nullptr )
		{
			if ( pFoundStoreID )
				*pFoundStoreID = pFindData->m_CurrentStoreID;

			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : handle -
// Output : const char
//-----------------------------------------------------------------------------
const char *CBaseFileSystem::FindNext( FileFindHandle_t handle )
{

	FindData_t *pFindData = &m_FindData[handle];

	while( 1 )
	{
		if( FindNextFileHelper( pFindData, nullptr) )
		{
			if ( pFindData->m_VisitedFiles.Find( pFindData->findData.cFileName ) == -1 )
			{
				pFindData->m_VisitedFiles.Insert( pFindData->findData.cFileName, 0 );
				return pFindData->findData.cFileName;
			}
		}
		else
		{
			return nullptr;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : handle -
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::FindIsDirectory( FileFindHandle_t handle )
{
	FindData_t *pFindData = &m_FindData[handle];
	return pFindData->findData.bDirectory;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : handle -
//-----------------------------------------------------------------------------
void CBaseFileSystem::FindClose( FileFindHandle_t handle )
{
	if ( ( handle < 0 ) || ( !m_FindData.IsInList( handle ) ) )
		return;

	FindData_t *pFindData = &m_FindData[handle];
	Assert(pFindData);

	if ( pFindData->findHandle != nullptr)
	{
		FS_FindClose( pFindData->findHandle );
	}
	pFindData->findHandle = nullptr;

	pFindData->wildCardString.Purge();
	m_FindData.Remove( handle );
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pFileName -
//-----------------------------------------------------------------------------
void CBaseFileSystem::GetLocalCopy( const char *pFileName )
{
	// do nothing. . everything is local.
}


//-----------------------------------------------------------------------------
// Converts a partial path into a full path
// Relative paths that are pack based are returned as an absolute path .../zip?.zip/foo
// A pack absolute path can be sent back in for opening, and the file will be properly
// detected as pack based and mounted inside the pack.
//-----------------------------------------------------------------------------
const char *CBaseFileSystem::RelativePathToFullPath( const char *pFileName, const char *pPathID, char *pFullPath, int fullPathBufferSize, PathTypeFilter_t pathFilter, PathTypeQuery_t *pPathType )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	FileStat_t buf;

	if ( pPathType )
	{
		*pPathType = PATH_IS_NORMAL;
	}


	// Fill in the default in case it's not found...
	Q_strncpy( pFullPath, pFileName, fullPathBufferSize );

	if ( IsPC() && pathFilter == FILTER_NONE )
	{
		// X360TBD: PC legacy behavior never returned pack paths
		// do legacy behavior to ensure naive callers don't break
		pathFilter = FILTER_CULLPACK;
	}

	CSearchPathsIterator iter( this, &pFileName, pPathID, pathFilter );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != nullptr; pSearchPath = iter.GetNext() )
	{
		int		dummy;
		int64_t dummy64;

		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFileName );

		bool bFound = FS_Stat( tempFileName, buf );
		if ( bFound )
		{
			Q_strncpy( pFullPath, tempFileName, fullPathBufferSize );
			if ( pPathType && pSearchPath->m_bIsDvdDevPath )
			{
				*pPathType |= PATH_IS_DVDDEV;
			}
			return pFullPath;
		}
	}

	// not found
	return nullptr;
}


const char *CBaseFileSystem::GetLocalPath( const char *pFileName, char *pLocalPath, int localPathBufferSize )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	return RelativePathToFullPath( pFileName, nullptr, pLocalPath, localPathBufferSize );
}


//-----------------------------------------------------------------------------
// Returns true on success, otherwise false if it can't be resolved
//-----------------------------------------------------------------------------
bool CBaseFileSystem::FullPathToRelativePathEx( const char *pFullPath, const char *pPathId, char *pRelative, int nMaxLen )
{
	CHECK_DOUBLE_SLASHES( pFullPath );

	int nInlen = Q_strlen( pFullPath );
	if ( nInlen <= 0 )
	{
		pRelative[ 0 ] = 0;
		return false;
	}

	Q_strncpy( pRelative, pFullPath, nMaxLen );

	char pInPath[ MAX_FILEPATH ];
	Q_strncpy( pInPath, pFullPath, sizeof( pInPath ) );
#ifdef _WIN32
	Q_strlower( pInPath );
#endif
	Q_FixSlashes( pInPath );

	CUtlSymbol lookup;
	if ( pPathId )
	{
		lookup = g_PathIDTable.AddString( pPathId );
	}

	int c = m_SearchPaths.Count();
	for( int i = 0; i < c; i++ )
	{
		// Skip paths that are not on the specified search path
		if ( FilterByPathID( &m_SearchPaths[i], lookup ) )
			continue;

		char pSearchBase[ MAX_FILEPATH ];
		Q_strncpy( pSearchBase, m_SearchPaths[i].GetPathString(), sizeof( pSearchBase ) );
#ifdef _WIN32
		Q_strlower( pSearchBase );
#endif
		Q_FixSlashes( pSearchBase );
		int nSearchLen = Q_strlen( pSearchBase );
		if ( Q_strnicmp( pSearchBase, pInPath, nSearchLen ) )
			continue;

		Q_strncpy( pRelative, &pInPath[ nSearchLen ], nMaxLen );
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Obsolete version
//-----------------------------------------------------------------------------
bool CBaseFileSystem::FullPathToRelativePath( const char *pFullPath, char *pRelative, int nMaxLen )
{
	return FullPathToRelativePathEx( pFullPath, nullptr, pRelative, nMaxLen );
}


//-----------------------------------------------------------------------------
// Deletes a file
//-----------------------------------------------------------------------------
void CBaseFileSystem::RemoveFile( char const* pRelativePath, const char *pathID )
{
	CHECK_DOUBLE_SLASHES( pRelativePath );

	// Allow for UNC-type syntax to specify the path ID.
	char tempPathID[MAX_PATH];
	ParsePathID( pRelativePath, pathID, tempPathID );


	// Opening for write or append uses Write Path
	char szScratchFileName[MAX_PATH];
	if ( Q_IsAbsolutePath( pRelativePath ) )
	{
		Q_strncpy( szScratchFileName, pRelativePath, sizeof( szScratchFileName ) );
	}
	else
	{
		ComputeFullWritePath( szScratchFileName, sizeof( szScratchFileName ), pRelativePath, pathID );
	}
	if ( !FS_RemoveFile( szScratchFileName ) )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "Unable to remove %s!\n", szScratchFileName );
	}
}


//-----------------------------------------------------------------------------
// Renames a file
//-----------------------------------------------------------------------------
bool CBaseFileSystem::RenameFile( char const *pOldPath, char const *pNewPath, const char *pathID )
{
	Assert( pOldPath && pNewPath );

	CHECK_DOUBLE_SLASHES( pOldPath );
	CHECK_DOUBLE_SLASHES( pNewPath );

	// Allow for UNC-type syntax to specify the path ID.
	char pPathIdCopy[MAX_PATH];
	const char *pOldPathId = pathID;
	if ( pathID )
	{
		Q_strncpy( pPathIdCopy, pathID, sizeof( pPathIdCopy ) );
		pOldPathId = pPathIdCopy;
	}

	char tempOldPathID[MAX_PATH];
	ParsePathID( pOldPath, pOldPathId, tempOldPathID );
	Assert( pOldPathId );

	// Allow for UNC-type syntax to specify the path ID.
	char tempNewPathID[MAX_PATH];
	ParsePathID( pNewPath, pathID, tempNewPathID );
	Assert( pathID );

	char pNewFileName[ MAX_PATH ];
	char szScratchFileName[MAX_PATH];

	// The source file may be in a fallback directory, so just resolve the actual path, don't assume pathid...
	RelativePathToFullPath( pOldPath, pOldPathId, szScratchFileName, sizeof( szScratchFileName ) );

	// Figure out the dest path
	if ( !Q_IsAbsolutePath( pNewPath ) )
	{
		ComputeFullWritePath( pNewFileName, sizeof( pNewFileName ), pNewPath, pathID );
	}
	else
	{
		Q_strncpy( pNewFileName, pNewPath, sizeof(pNewFileName) );
	}

	// Make sure the directory exitsts, too
	char pPathOnly[ MAX_PATH ];
	Q_strncpy( pPathOnly, pNewFileName, sizeof( pPathOnly ) );
	Q_StripFilename( pPathOnly );
	CreateDirHierarchy( pPathOnly, pathID );

	// Now copy the file over
	if ( !FS_RenameFile( szScratchFileName, pNewFileName ) )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "Unable to rename %s to %s!\n", szScratchFileName, pNewFileName );
		return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : **ppdir -
//-----------------------------------------------------------------------------
bool CBaseFileSystem::GetCurrentDirectory( char* pDirectory, int maxlen )
{
	if ( maxlen <= 0 || !FS_GetCurrentDirectory( pDirectory, (size_t)maxlen ) )
		return false;

	Q_FixSlashes(pDirectory);

	// Strip the last slash
	int len = strlen(pDirectory);
	if ( pDirectory[ len-1 ] == CORRECT_PATH_SEPARATOR )
	{
		pDirectory[ len-1 ] = 0;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : pfnWarning - warning function callback
//-----------------------------------------------------------------------------
void CBaseFileSystem::SetWarningFunc( void (*pfnWarning)( const char *fmt, ... ) )
{
	m_pfnWarning = pfnWarning;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : level -
//-----------------------------------------------------------------------------
void CBaseFileSystem::SetWarningLevel( FileWarningLevel_t level )
{
	m_fwLevel = level;
}

const FileSystemStatistics *CBaseFileSystem::GetFilesystemStatistics()
{
	return &m_Stats;
}




//-----------------------------------------------------------------------------
// Purpose:
// Input  : level -
//			*fmt -
//			... -
//-----------------------------------------------------------------------------
void CBaseFileSystem::FileSystemWarning( FileWarningLevel_t level, const char *fmt, ... )
{
#ifdef _CERT
	return;
#endif

	if ( level > m_fwLevel )
		return;

	va_list argptr;
    char warningtext[ 4096 ];

    va_start( argptr, fmt );
    Q_vsnprintf( warningtext, sizeof( warningtext ), fmt, argptr );
    va_end( argptr );

	// Dump to stdio
	printf( "%s", warningtext );
	if ( m_pfnWarning )
	{
		(*m_pfnWarning)( warningtext );
	}
	else
	{
#ifdef _WIN32
		Plat_DebugString( warningtext );
#endif
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseFileSystem::CSearchPath::CSearchPath( void )
{
	m_Path = g_PathIDTable.AddString( "" );
	m_pDebugPath = "";

	m_storeId = 0;
	m_pPackFile = nullptr;
	m_pPathIDInfo = nullptr;
	m_bIsDvdDevPath = false;
	m_bIsLocalizedPath = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseFileSystem::CSearchPath::~CSearchPath( void )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseFileSystem::CSearchPath *CBaseFileSystem::CSearchPathsIterator::GetFirst()
{
	if ( m_SearchPaths.Count() )
	{
		m_visits.Reset();
		m_iCurrent = -1;
		m_bExcluded = false;
		return GetNext();
	}
	return &m_EmptySearchPath;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBaseFileSystem::CSearchPath *CBaseFileSystem::CSearchPathsIterator::GetNext()
{
	CSearchPath *pSearchPath = nullptr;

	// PURPOSELY!!! split the 360 dvddev logic from the nominal (shipping) case
	// the logic is permuted slightly to do the right kind of filtering in dvddev or strict mode
	// 360 can optionally ignore and exclude a local search path in dvddev mode
	// excluding a local search path falls through to its cloned dvddev cache path
	// map paths are exempt from this exclusion logic
    // nominal behavior
    for ( m_iCurrent++; m_iCurrent < m_SearchPaths.Count(); m_iCurrent++ )
    {
        pSearchPath = &m_SearchPaths[m_iCurrent];

        if ( ( m_PathTypeFilter == FILTER_CULLLOCALIZED || m_PathTypeFilter == FILTER_CULLLOCALIZED_ANY ) && pSearchPath->m_bIsLocalizedPath )
        {
            continue;
        }

        if ( CBaseFileSystem::FilterByPathID( pSearchPath, m_pathID ) )
            continue;

        if ( !m_visits.MarkVisit( *pSearchPath ) )
            break;
    }

	if ( m_iCurrent < m_SearchPaths.Count() )
	{
		return pSearchPath;
	}

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Adds a filesystem logging function
//-----------------------------------------------------------------------------
void CBaseFileSystem::AddLoggingFunc( FileSystemLoggingFunc_t logFunc )
{
	Assert(!m_LogFuncs.IsValidIndex(m_LogFuncs.Find(logFunc)));
	m_LogFuncs.AddToTail(logFunc);
}

//-----------------------------------------------------------------------------
// Purpose: Removes a filesystem logging function
//-----------------------------------------------------------------------------
void CBaseFileSystem::RemoveLoggingFunc( FileSystemLoggingFunc_t logFunc )
{
	m_LogFuncs.FindAndRemove(logFunc);
}

//-----------------------------------------------------------------------------
// Make sure that slashes are of the right kind and that there is a slash at the
// end of the filename.
// WARNING!!: assumes that you have an extra byte allocated in the case that you need
// a slash at the end.
//-----------------------------------------------------------------------------
static void AddSeperatorAndFixPath( char *str )
{
	char *lastChar = &str[strlen( str ) - 1];
	if( *lastChar != CORRECT_PATH_SEPARATOR && *lastChar != INCORRECT_PATH_SEPARATOR )
	{
		lastChar[1] = CORRECT_PATH_SEPARATOR;
		lastChar[2] = '\0';
	}
	Q_FixSlashes( str );


}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pFileName -
// Output : FileNameHandle_t
//-----------------------------------------------------------------------------
FileNameHandle_t CBaseFileSystem::FindOrAddFileName( char const *pFileName )
{
	return m_FileNames.FindOrAddFileName( pFileName );
}

FileNameHandle_t CBaseFileSystem::FindFileName( char const *pFileName )
{
	return m_FileNames.FindFileName( pFileName );
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : handle -
// Output : char const
//-----------------------------------------------------------------------------
bool CBaseFileSystem::String( const FileNameHandle_t& handle, char *buf, int buflen )
{
	return m_FileNames.String( handle, buf, buflen );
}

int	CBaseFileSystem::GetPathIndex( const FileNameHandle_t &handle )
{
	return m_FileNames.PathIndex(handle);
}

CBaseFileSystem::CPathIDInfo* CBaseFileSystem::FindOrAddPathIDInfo( const CUtlSymbol &id, int bByRequestOnly )
{
	for ( int i=0; i < m_PathIDInfos.Count(); i++ )
	{
		CBaseFileSystem::CPathIDInfo *pInfo = m_PathIDInfos[i];
		if ( pInfo->GetPathID() == id )
		{
			if ( bByRequestOnly != -1 )
			{
				pInfo->m_bByRequestOnly = (bByRequestOnly != 0);
			}
			return pInfo;
		}
	}

	// Add a new one.
	CBaseFileSystem::CPathIDInfo *pInfo = new CBaseFileSystem::CPathIDInfo;
	m_PathIDInfos.AddToTail( pInfo );
	pInfo->SetPathID( id );
	pInfo->m_bByRequestOnly = (bByRequestOnly == 1);
	return pInfo;
}


void CBaseFileSystem::MarkPathIDByRequestOnly( const char *pPathID, bool bRequestOnly )
{
	FindOrAddPathIDInfo( g_PathIDTable.AddString( pPathID ), bRequestOnly );
}

bool CBaseFileSystem::IsFileInReadOnlySearchPath(const char *pPath, const char *pathID)
{
	//TODO: implementme!
	return false;
}

#if defined( TRACK_BLOCKING_IO )

void CBaseFileSystem::EnableBlockingFileAccessTracking( bool state )
{
	m_bBlockingFileAccessReportingEnabled = state;
}

bool CBaseFileSystem::IsBlockingFileAccessEnabled() const
{
	return m_bBlockingFileAccessReportingEnabled;
}

IBlockingFileItemList *CBaseFileSystem::RetrieveBlockingFileAccessInfo()
{
	Assert( m_pBlockingItems );
	return m_pBlockingItems;
}

void CBaseFileSystem::RecordBlockingFileAccess( bool synchronous, const FileBlockingItem& item )
{
	AUTO_LOCK( m_BlockingFileMutex );

	// Not tracking anything
	if ( !m_bBlockingFileAccessReportingEnabled )
		return;

	if ( synchronous && !m_bAllowSynchronousLogging && ( item.m_ItemType == FILESYSTEM_BLOCKING_SYNCHRONOUS ) )
		return;

	// Track it
	m_pBlockingItems->Add( item );
}

bool CBaseFileSystem::SetAllowSynchronousLogging( bool state )
{
	bool oldState = m_bAllowSynchronousLogging;
	m_bAllowSynchronousLogging = state;
	return oldState;
}

void CBaseFileSystem::BlockingFileAccess_EnterCriticalSection()
{
	m_BlockingFileMutex.Lock();
}

void CBaseFileSystem::BlockingFileAccess_LeaveCriticalSection()
{
	m_BlockingFileMutex.Unlock();
}

#endif // TRACK_BLOCKING_IO

// "<EXT> File", the name Windows Explorer uses for unregistered types; no shell lookup.
bool CBaseFileSystem::GetFileTypeForFullPath( char const *pFullPath, wchar_t *buf, size_t bufSizeInBytes )
{
	const size_t nMaxChars = bufSizeInBytes / sizeof( wchar_t );
	if ( nMaxChars == 0 )
		return false;

	char ext[32];
	Q_ExtractFileExtension( pFullPath, ext, sizeof( ext ) );

	size_t n = 0;
	for ( const char *p = ext; *p && n + 1 < nMaxChars; ++p )
		buf[n++] = (wchar_t)toupper( (unsigned char)*p );
	for ( const wchar_t *p = L" File"; *p && n + 1 < nMaxChars; ++p )
		buf[n++] = *p;
	buf[n] = L'\0';
	return false;
}


bool CBaseFileSystem::GetOptimalIOConstraints( FileHandle_t hFile, unsigned *pOffsetAlign, unsigned *pSizeAlign, unsigned *pBufferAlign )
{
	if ( pOffsetAlign )
		*pOffsetAlign = 1;
	if ( pSizeAlign )
		*pSizeAlign = 1;
	if ( pBufferAlign )
		*pBufferAlign = 1;
	return false;
}


bool CBaseFileSystem::GetStringFromKVPool( CRC32_t poolKey, unsigned int key, char *pOutBuff, int buflen )
{
	// xbox only
	{
		Assert( 0 );
		return false;
	}


	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Constructs a file handle
// Input  : base file system handle
// Output :
//-----------------------------------------------------------------------------
CFileHandle::CFileHandle( CBaseFileSystem* fs )
{
	Init( fs );
}

CFileHandle::~CFileHandle()
{
	Assert( IsValid() );
	delete[] m_pszTrueFileName;

	if ( m_pFile )
	{
		m_fs->FClose( m_pFile );
		m_pFile = nullptr;
	}

	m_nMagic = FREE_MAGIC;
}

void CFileHandle::Init( CBaseFileSystem *fs )
{
	m_nMagic = MAGIC;
	m_pFile = nullptr;
	m_nLength = 0;
	m_type = FT_NORMAL;

	m_fs = fs;

	m_pszTrueFileName = nullptr;
}

bool CFileHandle::IsValid()
{
	return ( m_nMagic == MAGIC );
}

bool CFileHandle::IsOK()
{
	if ( m_pFile )
	{
		return ( IsValid() && !m_pFile->HasError() );
	}
	else if ( m_pPackFileHandle )
	{
		return IsValid();
	}

	m_fs->FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to IsOk NULL file pointer inside valid file handle!\n" );
	return false;
}

void CFileHandle::Flush()
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		m_pFile->Flush();
	}
}

int CFileHandle::Read( void* pBuffer, int nLength )
{
	Assert( IsValid() );
	return Read( pBuffer, -1, nLength );
}

int CFileHandle::Read( void* pBuffer, int nDestSize, int nLength )
{
	Assert( IsValid() );

	// Is this a regular file or a pack file?
	if ( m_pFile )
	{
		return (int)m_pFile->Read( pBuffer, nLength );
	}

	return 0;
}

int CFileHandle::Write( const void* pBuffer, int nLength )
{
	Assert( IsValid() );

	if ( !m_pFile )
	{
		m_fs->FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Write NULL file pointer inside valid file handle!\n" );
		return 0;
	}

	return (int)m_pFile->Write( pBuffer, nLength );
}

int CFileHandle::Seek( int64_t nOffset, int nWhence )
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		m_pFile->Seek( nOffset, nWhence );
		// TODO - FS_fseek should return the resultant offset
		return 0;
	}

	return -1;
}

int CFileHandle::Tell()
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		return (int)m_pFile->Tell();
	}

	return -1;
}

int CFileHandle::Size()
{
	Assert( IsValid() );

	int nReturnedSize = -1;

	if ( m_pFile  )
	{
		nReturnedSize = m_nLength;
	}

	return nReturnedSize;
}

int64_t CFileHandle::AbsoluteBaseOffset()
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		return 0;
	}
    return 0;
}

bool CFileHandle::EndOfFile()
{
	Assert( IsValid() );

	return ( Tell() >= Size() );
}

void CBaseFileSystem::MarkLocalizedPath( CSearchPath *sp )
{
// game console only for now
}

IIoStats *CBaseFileSystem::GetIoStats()
{
#ifndef _CERT
	return &s_IoStats;
#else
	return NULL;
#endif
}







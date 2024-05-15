//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "basefilesystem.h"
#include "tier0/vprof.h"
#include "tier1/characterset.h"
#include "tier1/keyvalues.h"
#include "tier1/lzmaDecoder.h"
#include "tier1/fmtstr.h"

#ifndef DEDICATED
#include "keyvaluescompiler.h"
#endif
#include "ifilelist.h"

#ifdef IS_WINDOWS_PC
// Needed for getting file type string
#define WIN32_LEAN_AND_MEAN
#include <shellapi.h>
#endif

#if defined( _X360 )
#include "xbox\xbox_win32stubs.h"
#undef GetCurrentDirectory
#endif

#ifdef _PS3

#include "ps3/ps3_core.h"
#include "ps3_pathinfo.h"
#include "tls_ps3.h"
#include <cell/fios.h>

// extern bool g_bUseBdvdGameData;
#ifndef PLATFORM_EXT
#pragma message("PLATFORM_EXT define is missing, wtf?")
#define PLATFORM_EXT ".ps3"
#endif // ifndef PLATFORM_EXT

void getcwd(...) { AssertMsg(false, "getcwd does not exist on PS3\n"); }
bool SetupFios();
bool TeardownFios();

#endif // _PS3

#ifdef _X360
	#define FS_DVDDEV_REMAP_ROOT "d:"
	#define FS_DVDDEV_ROOT "d:\\dvddev"
	#define FS_EXCLUDE_PATHS_FILENAME "xbox_exclude_paths.txt"
#elif defined( _PS3 )
	#define FS_DVDDEV_REMAP_ROOT g_pPS3PathInfo->GameImagePath()
	#define FS_DVDDEV_ROOT "/app_home/dvddev"
	#define FS_EXCLUDE_PATHS_FILENAME "ps3_exclude_paths.txt"
#else
	#define FS_DVDDEV_REMAP_ROOT ""
	#define FS_DVDDEV_ROOT "dvddev???:::"
	#define FS_EXCLUDE_PATHS_FILENAME "allbad_exclude_paths.txt"
#endif

#ifdef _GAMECONSOLE
static bool IsDvdDevPathString( char const *szPath )
{
	if ( IsGameConsole() && StringAfterPrefix( szPath, FS_DVDDEV_ROOT ) &&
		szPath[ sizeof( FS_DVDDEV_ROOT ) - 1 ] == CORRECT_PATH_SEPARATOR )
	{
		return true;
	}
	else if ( IsX360() )
	{
		const char *pFirstDir = V_strstr( szPath, ":" );
		if ( pFirstDir )
		{
			// skip past colon/slash
			pFirstDir += 2;
			return ( V_strnicmp( pFirstDir, "dvddev", 6 ) == false );
		}
	}

	return false;
}
#else
#define IsDvdDevPathString( x ) false
#endif

// memdbgon must be the last include file in a .cpp file!!!
// #include "tier0/memdbgon.h


#pragma warning( disable : 4355 )  // warning C4355: 'this' : used in base member initializer list


ConVar fs_report_sync_opens( "fs_report_sync_opens", "0", FCVAR_RELEASE, "0:Off, 1:Always, 2:Not during map load" );
ConVar fs_report_sync_opens_callstack( "fs_report_sync_opens_callstack", "0", 0, "0 to not display the call-stack when we hit a fs_report_sync_opens warning. Set to 1 to display the call-stack." );
ConVar fs_report_long_reads( "fs_report_long_reads", "0", 0, "0:Off, 1:All (for tracking accumulated duplicate read times), >1:Microsecond threshold" );
ConVar fs_warning_mode( "fs_warning_mode", "0", 0, "0:Off, 1:Warn main thread, 2:Warn other threads"  );
ConVar fs_monitor_read_from_pack( "fs_monitor_read_from_pack", "0", 0, "0:Off, 1:Any, 2:Sync only" );

#if IsPlatformPS3()
ConVar fs_fios_spew_prefetches( "fs_fios_spew_prefetches", "0", 0, "Set this to 1 to output prefetch operations, otherwise set this to 0." );
ConVar fs_fios_enabled( "fs_fios_enabled", "0", 0, "Set this to 1 to enable FIOS, otherwise set this to 0." );
#endif

#define BSPOUTPUT	0	// bsp output flag -- determines type of fs_log output to generate


static void AddSeperatorAndFixPath( char *str );

// Case-insensitive symbol table for path IDs.
CUtlSymbolTableMT g_PathIDTable( 0, 32, true );

int g_iNextSearchPathID = 1;

#if defined (_PS3)
	
	// Copied from zip_utils.cpp (we don't want to add the file to the project (for now))
	BEGIN_BYTESWAP_DATADESC( ZIP_EndOfCentralDirRecord )
		DEFINE_FIELD( signature, FIELD_INTEGER ),
		DEFINE_FIELD( numberOfThisDisk, FIELD_SHORT ),
		DEFINE_FIELD( numberOfTheDiskWithStartOfCentralDirectory, FIELD_SHORT ),
		DEFINE_FIELD( nCentralDirectoryEntries_ThisDisk, FIELD_SHORT ),
		DEFINE_FIELD( nCentralDirectoryEntries_Total, FIELD_SHORT ),
		DEFINE_FIELD( centralDirectorySize, FIELD_INTEGER ),
		DEFINE_FIELD( startOfCentralDirOffset, FIELD_INTEGER ),
		DEFINE_FIELD( commentLength, FIELD_SHORT ),
	END_BYTESWAP_DATADESC()

	BEGIN_BYTESWAP_DATADESC( ZIP_FileHeader )
		DEFINE_FIELD( signature, FIELD_INTEGER ),
		DEFINE_FIELD( versionMadeBy, FIELD_SHORT ),
		DEFINE_FIELD( versionNeededToExtract, FIELD_SHORT ),
		DEFINE_FIELD( flags, FIELD_SHORT ),
		DEFINE_FIELD( compressionMethod, FIELD_SHORT ),
		DEFINE_FIELD( lastModifiedTime, FIELD_SHORT ),
		DEFINE_FIELD( lastModifiedDate, FIELD_SHORT ),
		DEFINE_FIELD( crc32, FIELD_INTEGER ),
		DEFINE_FIELD( compressedSize, FIELD_INTEGER ),
		DEFINE_FIELD( uncompressedSize, FIELD_INTEGER ),
		DEFINE_FIELD( fileNameLength, FIELD_SHORT ),
		DEFINE_FIELD( extraFieldLength, FIELD_SHORT ),
		DEFINE_FIELD( fileCommentLength, FIELD_SHORT ),
		DEFINE_FIELD( diskNumberStart, FIELD_SHORT ),
		DEFINE_FIELD( internalFileAttribs, FIELD_SHORT ),
		DEFINE_FIELD( externalFileAttribs, FIELD_INTEGER ),
		DEFINE_FIELD( relativeOffsetOfLocalHeader, FIELD_INTEGER ),
	END_BYTESWAP_DATADESC()
#endif

void FixUpPathCaseForPS3(const char* pFilePath)
{
    char* prev_ptr = NULL;
    char* last_ptr = NULL;
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
		return NULL;

	if ( !V_strnicmp( pFileName, "maps", 4 ) &&
		 ( ( pFileName[4] == CORRECT_PATH_SEPARATOR ) || ( pFileName[4] == INCORRECT_PATH_SEPARATOR ) ) &&
		 !V_strnicmp( pFileName + 5, "workshop", 8 ) &&
		 ( ( pFileName[13] == CORRECT_PATH_SEPARATOR ) || ( pFileName[13] == INCORRECT_PATH_SEPARATOR ) ) )
	{
		//    maps/workshop/
		if ( ( false
			/** Removed for partner depot **/
			) &&
			( ( pFileName[23] == CORRECT_PATH_SEPARATOR ) || ( pFileName[23] == INCORRECT_PATH_SEPARATOR ) ) )
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
#if !( defined(_WIN32) && defined(DEDICATED) ) || defined( _PS3 )
static CBaseFileSystem *g_pBaseFileSystem;
CBaseFileSystem *BaseFileSystem()
{
	return g_pBaseFileSystem;
}
#endif

ConVar filesystem_buffer_size( "filesystem_buffer_size", "0", 0, "Size of per file buffers. 0 for none" );


class CFileHandleTimer : public CFastTimer
{
public:
	FileHandle_t m_hFile;
	char m_szName[ MAX_PATH ];
};

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
CThreadFastMutex g_FileOpenDuplicateTimesMutex;

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
	CInterlockedInt m_nNumberOfFileSeeks;
	CInterlockedInt m_nTimeInFileSeek;
	CInterlockedInt m_nNumberOfFileReads;
	CInterlockedInt m_nTimeInFileRead;
	CInterlockedInt m_nFileReadTotalSize;
	CInterlockedInt m_nNumberOfFileOpens;
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
	: m_FileWhitelist( NULL )/*,m_FileTracker2( this )*/
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
	m_pfnWarning = NULL;
	m_pLogFile   = NULL;
	m_bOutputDebugString = false;
	m_WhitelistSpewFlags = 0;
	m_DirtyDiskReportFunc = NULL;

	m_pThreadPool = NULL;
#if defined( TRACK_BLOCKING_IO )
	m_pBlockingItems = new CBlockingFileItemList( this );
	m_bBlockingFileAccessReportingEnabled = false;
	m_bAllowSynchronousLogging = true;
#endif

	m_iMapLoad = 0;


#ifdef SUPPORT_IODELAY_MONITORING
	m_pDelayThread = NULL;
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
// Methods of IAppSystem
//-----------------------------------------------------------------------------
void *CBaseFileSystem::QueryInterface( const char *pInterfaceName )
{
	// We also implement the IMatSystemSurface interface
	if (!Q_strncmp(	pInterfaceName, BASEFILESYSTEM_INTERFACE_VERSION, Q_strlen(BASEFILESYSTEM_INTERFACE_VERSION) + 1))
		return (IBaseFileSystem*)this;

	return NULL;
}


#ifdef _PS3
// this is strictly a debug variable used to catch errors where we load and tear down more than one filesystem:
static int s_PS3_libfs_ref_count = 0;
#endif
InitReturnVal_t CBaseFileSystem::Init()
{
//	m_FileTracker2.InitAsyncThread();

	InitReturnVal_t nRetVal = BaseClass::Init();
	if ( nRetVal != INIT_OK )
		return nRetVal;

#ifdef _PS3
	// load the PS3's file system module to memory
	AssertMsg1( s_PS3_libfs_ref_count == 0, "%d CBaseFileSystems were instantiated!\n", s_PS3_libfs_ref_count+1 );
	if ( cellSysmoduleLoadModule(CELL_SYSMODULE_FS) != CELL_OK )
	{
		Error( "Could not load system libfs!\n" );
	}
	else
	{
		s_PS3_libfs_ref_count += 1;
	}
#endif

	// This is a special tag to allow iterating just the BSP file, it doesn't show up in the list per se, but gets converted to "GAME" in the filter function
	m_BSPPathID = g_PathIDTable.AddString( "BSP" );
	m_GamePathID = g_PathIDTable.AddString( "GAME" );

	if ( getenv( "fs_debug" ) )
	{
		m_bOutputDebugString = true;
	}

	const char *logFileName = CommandLine()->ParmValue( "-fs_log" );
	if ( logFileName )
	{
		m_pLogFile = fopen( logFileName, "w" ); // STEAM OK
		if ( !m_pLogFile )
			return INIT_FAILED;
		fprintf( m_pLogFile, "@echo off\n" );
		fprintf( m_pLogFile, "setlocal\n" );
		const char *fs_target = CommandLine()->ParmValue( "-fs_target" );
		if( fs_target )
		{
			fprintf( m_pLogFile, "set fs_target=\"%s\"\n", fs_target );
		}
		fprintf( m_pLogFile, "if \"%%fs_target%%\" == \"\" goto error\n" );
		fprintf( m_pLogFile, "@echo on\n" );
	}

	if ( IsGameConsole() )
	{
		BuildExcludeList();
	}

#if defined( _X360 )
	MEM_ALLOC_CREDIT();

#if defined( _DEMO )
	// under demo conditions cannot allow install or use existing install
	// slam to expected state, do not override
	m_bLaunchedFromXboxHDD = false;
	m_bFoundXboxImageInCache = false;
	m_bAllowXboxInstall = false;
	m_bDVDHosted = true;
#else
	// determine the type of system where we launched from
	// this allows other systems (like the installer) to conditionalize the install process
	// MS may very well auto-install for us at a later date
	DWORD dwDummyFlags;
	char szFileSystemName[MAX_PATH];
	DWORD dwResult = GetVolumeInformation(
		"D:\\",
		NULL,
		0,
		NULL,
		0,
		&dwDummyFlags,
		szFileSystemName,
		sizeof( szFileSystemName ) );
	if ( dwResult != 0 )
	{
		m_bLaunchedFromXboxHDD = ( V_stricmp( szFileSystemName, "FATX" ) == 0 );
	}

	if ( m_DVDMode == DVDMODE_STRICT )
	{
		// must be in a strict dvd environment and not explicitly disabled
		if ( !CommandLine()->FindParm( "-noinstall" ) )
		{
			// the install is allowed if we launched from anywhere but the HDD
			// or it can be tested from the HDD by forcing with command line options
			m_bAllowXboxInstall = ( m_bLaunchedFromXboxHDD == false ) ||
								( CommandLine()->FindParm( "-installer" ) != 0 ) ||
								( CommandLine()->FindParm( "-install" ) != 0 );
			if ( m_bAllowXboxInstall )
			{
				// install may have already occurred
				m_bFoundXboxImageInCache = IsAlreadyInstalledToXboxHDDCache();
				if ( m_bFoundXboxImageInCache )
				{
					// we are using the installed image
					// no further installer activity is ever allowed (as the targets will be opened)
					m_bAllowXboxInstall = false;
				}
			}
		}

		// The update zip is designed to be held resident to avoid MU yanking or other transient issues.
		// The zip is expected to be < 100K and is a special compressed format.
		const char *pszUpdatePath = "UPDATE:\\update\\update" PLATFORM_EXT ".zip";
		if ( !IsCert() && !FileExists( pszUpdatePath ) )
		{
			// allows us to fallback and test when it is in the image
			pszUpdatePath = "D:\\update\\update" PLATFORM_EXT ".zip";
		}
		ReadFile( pszUpdatePath, NULL, g_UpdateZipBuffer, 0, 0 );
	}

	// if we are in any way HDD based, we do not want the reduced DVD experience
	m_bDVDHosted = ( m_bAllowXboxInstall || CommandLine()->FindParm( "-dvdtest" ) || 
					( !m_bLaunchedFromXboxHDD && !m_bFoundXboxImageInCache )  );
#endif
#elif defined( _PS3 )
	m_bLaunchedFromXboxHDD = true;
	m_bFoundXboxImageInCache = false;
	m_bAllowXboxInstall = false;
	m_bDVDHosted = false;

	SetupFios();
#endif

	return INIT_OK;
}

void CBaseFileSystem::Shutdown()
{

#if !defined( _X360 ) && !defined( _PS3 )
	if( m_pLogFile )
	{
		if( CommandLine()->FindParm( "-fs_logbins" ) >= 0 )
		{
			char cwd[MAX_FILEPATH];
			getcwd( cwd, MAX_FILEPATH-1 );
			fprintf( m_pLogFile, "set binsrc=\"%s\"\n", cwd );
			fprintf( m_pLogFile, "mkdir \"%%fs_target%%\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\hl2.exe\" \"%%fs_target%%\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\hl2.dat\" \"%%fs_target%%\"\n" );
			fprintf( m_pLogFile, "mkdir \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\*.asi\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\materialsystem.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\shaderapidx9.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\filesystem_stdio.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\soundemittersystem.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\stdshader*.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\shader_nv*.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\launcher.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\engine.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\mss32.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\tier0.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\vgui2.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\vguimatsurface.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\voice_miles.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\vphysics.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\vstdlib.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\studiorender.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\bin\\vaudio_miles.dll\" \"%%fs_target%%\\bin\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\hl2\\resource\\*.ttf\" \"%%fs_target%%\\hl2\\resource\"\n" );
			fprintf( m_pLogFile, "copy \"%%binsrc%%\\hl2\\bin\\gameui.dll\" \"%%fs_target%%\\hl2\\bin\"\n" );
		}
		fprintf( m_pLogFile, "goto done\n" );
		fprintf( m_pLogFile, ":error\n" );
		fprintf( m_pLogFile, "echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\"\n" );
		fprintf( m_pLogFile, "echo ERROR: must set fs_target=targetpath (ie. \"set fs_target=u:\\destdir\")!\n" );
		fprintf( m_pLogFile, "echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\"\n" );
		fprintf( m_pLogFile, ":done\n" );
		fclose( m_pLogFile ); // STEAM OK
	}
#endif

	RemoveAllSearchPaths();
	Trace_DumpUnclosedFiles();

#if defined( _PS3 )
	TeardownFios();

	if ( --s_PS3_libfs_ref_count == 0 )
	{
		cellSysmoduleUnloadModule(CELL_SYSMODULE_FS);
	}
	else
	{
		AssertMsg( false, "More than one CBaseFileSystem was instantiated! Failsafe triggered to refcount sysutil libfs.\n" );
	}
#endif

	BaseClass::Shutdown();
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


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : src1 - 
//			src2 - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::OpenedFileLessFunc( COpenedFile const& src1, COpenedFile const& src2 )
{
	return src1.m_pFile < src2.m_pFile;
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
	LOCAL_THREAD_LOCK();

	if ( m_fwLevel >= FILESYSTEM_WARNING_REPORTALLACCESSES )
	{
		FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES, "---FS%s:  %s %s (%.3f)\n", ThreadInMainThread() ? "" : "[a]", accesstype, fullpath, Plat_FloatTime() );
	}

	int c = m_LogFuncs.Count();
	if ( !c )
		return;

	for ( int i = 0; i < c; ++i )
	{
		( m_LogFuncs[ i ] )( fullpath, options );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *filename - 
//			*options - 
// Output : FILE
//-----------------------------------------------------------------------------
FILE *CBaseFileSystem::Trace_FOpen( const char *filename, const char *options, unsigned flags, int64 *size, CFileLoadInfo *pInfo )
{
	if ( m_NonexistingFilesExtensions.GetNumStrings() )
	{
		if ( char const *pszExt = V_GetFileExtension( filename ) )
		{
			AUTO_LOCK( m_OpenedFilesMutex );
			UtlSymId_t symFound = m_NonexistingFilesExtensions.Find( pszExt );
			if ( ( symFound != UTL_INVAL_SYMBOL ) && m_NonexistingFilesExtensions[ symFound ] )
			{
				DevWarning( "Known VPK-only extension [%s], file {%s} declared missing. Run with -fullfsvalveds to search filesystem.\n", pszExt, filename );
				return NULL;
			}
		}
	}

#ifdef NONEXISTING_FILES_CACHE_SUPPORT
	bool bReadOnlyRequest = !strchr(options,'w') && !strchr(options,'a') && !strchr(options,'+');

	static bool s_bNeverCheckFS = !CommandLine()->FindParm( "-alwayscheckfs" );
	if ( s_bNeverCheckFS )
	{
		AUTO_LOCK( m_OpenedFilesMutex );

		UtlSymId_t symFound = m_NonexistingFilesCache.Find( filename );
		if ( symFound != UTL_INVAL_SYMBOL )
		{
			double &refCacheTime = m_NonexistingFilesCache[ symFound ];

			if ( bReadOnlyRequest )
			{
				if ( refCacheTime != 0.0 )
				{
					Warning( "Trace_FOpen: duplicate request for missing file: %s [was missing %.3f sec ago]\n", filename, Plat_FloatTime() - refCacheTime );
					return NULL;	// we looked for this file already, it doesn't exist
				}
				else
				{
					// This file was previously missing, but a write request was made and could have created the file, so this read call should fall through
				}
			}
			else
			{
				// This is possibly a write request, so remove cached ENOENT record
				Warning( "Trace_FOpen: possibly write request for missing file: %s [was missing %.3f sec ago]\n", filename, Plat_FloatTime() - refCacheTime );
				refCacheTime = 0.0f;
			}
		}
		else
		{
			// Nothing known about this file, fall through into syscall to fopen
		}
	}
#endif

	AUTOBLOCKREPORTER_FN( Trace_FOpen, this, true, filename, FILESYSTEM_BLOCKING_SYNCHRONOUS, FileBlockingItem::FB_ACCESS_OPEN );

	FILE *fp = FS_fopen( filename, options, flags, size, pInfo );

#ifdef NONEXISTING_FILES_CACHE_SUPPORT
	if ( s_bNeverCheckFS && !fp && bReadOnlyRequest )
	{
		double dblNow = Plat_FloatTime();

		AUTO_LOCK( m_OpenedFilesMutex );
		m_NonexistingFilesCache[ filename ] = dblNow;
		Warning( "Trace_FOpen: missing file: %s [will never check again]\n", filename );
	}
#endif

	if ( fp )
	{
		if ( options[0] == 'r' )
		{
			FS_setbufsize(fp, filesystem_buffer_size.GetInt() );
		}
		else
		{
			FS_setbufsize(fp, 32*1024 );
		}

		AUTO_LOCK( m_OpenedFilesMutex );
		COpenedFile file;

		file.SetName( filename );
		file.m_pFile = fp;

		m_OpenedFiles.AddToTail( file );

		LogAccessToFile( "open", filename, options );
	}

	return fp;
}

void CBaseFileSystem::GetFileNameForHandle( FileHandle_t handle, char *buf, size_t buflen )
{
	V_strncpy( buf, "Unknown", buflen );
	/*
	CFileHandle *fh = ( CFileHandle *)handle;
	if ( !fh )
	{
		buf[ 0 ] = 0;
		return;
	}

	// Pack file filehandles store the underlying name for convenience
	if ( fh->IsPack() )
	{
		Q_strncpy( buf, fh->Name(), buflen );
		return;
	}

	AUTO_LOCK( m_OpenedFilesMutex );

	COpenedFile file;
	file.m_pFile = fh->GetFileHandle();

	int result = m_OpenedFiles.Find( file );
	if ( result != -1 )
	{
		COpenedFile found = m_OpenedFiles[ result ];
		Q_strncpy( buf, found.GetName(), buflen );
	}
	else
	{
		buf[ 0 ] = 0;
	}
	*/
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *fp - 
//-----------------------------------------------------------------------------
void CBaseFileSystem::Trace_FClose( FILE *fp )
{
	if ( fp )
	{
		m_OpenedFilesMutex.Lock();

		COpenedFile file;
		file.m_pFile = fp;

		int result = m_OpenedFiles.Find( file );
		if ( result != -1 /*m_OpenedFiles.InvalidIdx()*/ )
		{
			COpenedFile found = m_OpenedFiles[ result ];
			if ( m_fwLevel >= FILESYSTEM_WARNING_REPORTALLACCESSES )
			{
				FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES, "---FS%s:  close %s %p %i (%.3f)\n", ThreadInMainThread() ? "" : "[a]", found.GetName(), fp, m_OpenedFiles.Count(), Plat_FloatTime() );
			}
			m_OpenedFiles.Remove( result );
		}
		else
		{
			Assert( 0 );

			if ( m_fwLevel >= FILESYSTEM_WARNING_REPORTALLACCESSES )
			{
				FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES, "Tried to close unknown file pointer %p\n", fp );
			}
		}

		m_OpenedFilesMutex.Unlock();

		FS_fclose( fp );
	}
}


void CBaseFileSystem::Trace_FRead( int size, FILE* fp )
{
	if ( !fp || m_fwLevel < FILESYSTEM_WARNING_REPORTALLACCESSES_READ )
		return;

	AUTO_LOCK( m_OpenedFilesMutex );

	COpenedFile file;
	file.m_pFile = fp;

	int result = m_OpenedFiles.Find( file );

	if( result != -1 )
	{
		COpenedFile found = m_OpenedFiles[ result ];
		
		FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES_READ, "---FS%s:  read %s %i %p (%.3f)\n", ThreadInMainThread() ? "" : "[a]", found.GetName(), size, fp, Plat_FloatTime()  );
	} 
	else
	{
		FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES_READ, "Tried to read %i bytes from unknown file pointer %p\n", size, fp );
		
	}
}

void CBaseFileSystem::Trace_FWrite( int size, FILE* fp )
{
	if ( !fp || m_fwLevel < FILESYSTEM_WARNING_REPORTALLACCESSES_READWRITE )
		return;

	COpenedFile file;
	file.m_pFile = fp;

	AUTO_LOCK( m_OpenedFilesMutex );

	int result = m_OpenedFiles.Find( file );

	if( result != -1 )
	{
		COpenedFile found = m_OpenedFiles[ result ];

		FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES_READWRITE, "---FS%s:  write %s %i %p\n", ThreadInMainThread() ? "" : "[a]", found.GetName(), size, fp  );
	} 
	else
	{
		FileSystemWarning( FILESYSTEM_WARNING_REPORTALLACCESSES_READWRITE, "Tried to write %i bytes from unknown file pointer %p\n", size, fp );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFileSystem::Trace_DumpUnclosedFiles( void )
{
	AUTO_LOCK( m_OpenedFilesMutex );
	for ( int i = 0 ; i < m_OpenedFiles.Count(); i++ )
	{
		COpenedFile *found = &m_OpenedFiles[ i ];

		if ( m_fwLevel >= FILESYSTEM_WARNING_REPORTUNCLOSED )
		{
			FileSystemWarning( FILESYSTEM_WARNING_REPORTUNCLOSED, "File %s was never closed\n", found->GetName() );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFileSystem::PrintOpenedFiles( void )
{
	FileWarningLevel_t saveLevel = m_fwLevel;
	m_fwLevel = FILESYSTEM_WARNING_REPORTUNCLOSED;
	Trace_DumpUnclosedFiles();
	m_fwLevel = saveLevel;
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

	if ( IsGameConsole() && m_ExcludeFilePaths.Count() )
	{
		// dump current list
		Msg( "\nExclude Paths:\n" );
		char szPath[MAX_PATH];
		for ( int i = 0; i < m_ExcludeFilePaths.Count(); i++ )
		{
			if ( String( m_ExcludeFilePaths[i], szPath, sizeof( szPath ) ) )
			{
				Msg( "\"%s\"\n", szPath );
			}
		}
	}

}


//-----------------------------------------------------------------------------
// Purpose: This is where search paths are created.  map files are created at head of list (they occur after
//  file system paths have already been set ) so they get highest priority.  Otherwise, we add the disk (non-packfile)
//  path and then the paks if they exist for the path
// Input  : *pPath - 
//-----------------------------------------------------------------------------
void CBaseFileSystem::AddSearchPathInternal( const char *pPath, const char *pathID, SearchPathAdd_t addType, int iForceInsertIndex )
{
	Assert( ThreadInMainThread() );

	// Clean up the name
	char newPath[ MAX_FILEPATH ];
	if ( pPath[0] == 0 )
	{
		newPath[0] = newPath[1] = 0;
	}
	else
	{
		if ( IsGameConsole() || Q_IsAbsolutePath( pPath ) )
		{
			Q_strncpy( newPath, pPath, sizeof( newPath ) );
		}
		else
		{
			Q_MakeAbsolutePath( newPath, sizeof(newPath), pPath );
		}
#ifdef _WIN32
		Q_strlower( newPath );
#endif
		AddSeperatorAndFixPath( newPath );
	}

	// Make sure that it doesn't already exist
	CUtlSymbol pathSym, pathIDSym;
	pathSym = g_PathIDTable.AddString( newPath );
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
void CBaseFileSystem::AddSearchPath( const char *pPath, const char *pathID, SearchPathAdd_t addType )
{
	char tempSymlinkBuffer[MAX_PATH];
	pPath = V_FormatFilenameForSymlinking( tempSymlinkBuffer, pPath );

#if !defined( _X360 )
	// The PC has no concept of update/dlc discovery, it explicitly adds them now
	// This layout matches the Xbox's search path layout, when the Xbox does late bind the DLC
	// any platform, game, or mod search paths get subverted in order to prepend the DLC path
	const char *pGameName = "csgo";

	// CSGO compatibility VPKs
	if ( const char *pActualPathID = pathID ? StringAfterPrefix( pathID, "COMPAT:" ) : NULL )
	{
		if ( addType != PATH_ADD_TO_HEAD )
			return;	// in non-vpk mode compatibility paths can only be added to head

		// Build compatibility syntax path and proceed with adding
		pathID = pActualPathID;
		V_sprintf_safe( tempSymlinkBuffer, "csgo/compatibility/%s/", pPath );
		pPath = tempSymlinkBuffer;
	}

	if ( V_stristr( pPath, pGameName ) &&
		( !Q_stricmp( pathID, "GAME" ) || !Q_stricmp( pathID, "MOD" ) || !Q_stricmp( pathID, "PLATFORM" ) ) )
	{
		char szPathHead[MAX_PATH];
		char szUpdatePath[MAX_PATH];
		V_strncpy( szPathHead, pPath, sizeof( szPathHead ) );
		V_StripLastDir( szPathHead, sizeof( szPathHead ) );

		// xlsppatch trumps all
		V_ComposeFileName( szPathHead, "xlsppatch", szUpdatePath, sizeof( szUpdatePath ) );
		struct _stat buf;
		if ( FS_stat( szUpdatePath, &buf ) != -1 )
		{
			// found
			AddSearchPathInternal( szUpdatePath, pathID, addType, true );
		}

		// followed by update
		V_ComposeFileName( szPathHead, "update", szUpdatePath, sizeof( szUpdatePath ) );
		if ( FS_stat( szUpdatePath, &buf ) != -1 )
		{
			// found
			AddSearchPathInternal( szUpdatePath, pathID, addType, true );
		}

		// followed by dlc
		if ( !Q_stricmp( pathID, "GAME" ) || !Q_stricmp( pathID, "MOD" ) )
		{
			// DS would have all DLC dirs
			// find highest DLC dir available
			int nHighestDLC = 1;
			for ( ;nHighestDLC <= 99; nHighestDLC++ )
			{
				V_ComposeFileName( szPathHead, CFmtStr( "%s_dlc%d", pGameName, nHighestDLC ), szUpdatePath, sizeof( szUpdatePath ) );
				if ( FS_stat( szUpdatePath, &buf ) == -1 )
				{
					// does not exist, highest dlc available is previous
					nHighestDLC--;
					break;
				}

				V_ComposeFileName( szPathHead, CFmtStr( "%s_dlc%d/dlc_disabled.txt", pGameName, nHighestDLC ), szUpdatePath, sizeof( szUpdatePath ) );
				if ( FS_stat( szUpdatePath, &buf ) != -1 )
				{
					// disabled, highest dlc available is previous
					nHighestDLC--;
					break;
				}
			}
		
			// mount in correct order
			for ( ;nHighestDLC >= 1; nHighestDLC-- )
			{
				V_ComposeFileName( szPathHead, CFmtStr( "%s_dlc%d", pGameName, nHighestDLC ), szUpdatePath, sizeof( szUpdatePath ) );
				AddSearchPathInternal( szUpdatePath, pathID, addType, true );
			}
		}
	}
#endif

	int currCount = m_SearchPaths.Count();

	AddSearchPathInternal( pPath, pathID, addType, true );

	if ( currCount != m_SearchPaths.Count() )
	{
#if !defined( DEDICATED ) && !defined( _CERT )
		if ( IsGameConsole() )
		{
			// spew updated search paths
			// PrintSearchPaths();
		}
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
#if defined( _X360 )
	if ( m_bSearchPathsPatchedAfterInstall )
	{
		// do not want to ever call this twice
		return true;
	}

	AsyncFinishAll();

	// this is incredibly hardcoded and fragile
	// after shipping need to revisit and generalize for installs
	// this assumes exact knowledge of how zips are mounted and the install footprint
	for ( int i = 0; i < m_SearchPaths.Count(); i++ )
	{
		const char *pPathID = m_SearchPaths[i].GetPathIDString();
		if ( V_stricmp( pPathID, "GAME" ) && V_stricmp( pPathID, "MOD" ) )
		{
			// only consider these paths
			continue;
		}

		const char *pPath = m_SearchPaths[i].GetPathString();
		const char *pColon = strchr( pPath, ':' );
		if ( !pColon || 
			!V_stristr( pPath, "csgo" ) || 
			V_stristr( pPath, "_lv" ) || 
			V_stristr( pPath, "_dlc" ) ||
			V_stristr( pPath, "_tempcontent" ) )
		{
			// ignore relative paths, can't patch those
			// ignore any non csgo path
			// ignore lv, dlc, tempcontent path, not installing those zips
			continue;
		}
		if ( !m_SearchPaths[i].GetPackFile() || m_SearchPaths[i].GetPackFile()->m_bIsMapPath )
		{
			// ignore non pack based paths
			// ignore bsps
			continue;
		}
		if ( !V_stristr( m_SearchPaths[i].GetPackFile()->m_ZipName.String(), "zip0" PLATFORM_EXT ".zip" ) )
		{
			// only patching zip0
			continue;
		}

		// Not installing localized data
		if ( m_SearchPaths[i].m_bIsLocalizedPath )
		{
			continue;
		}

		char szNewPath[MAX_PATH];
		V_snprintf( szNewPath, sizeof( szNewPath ), "%s%s", CACHE_PATH_CSTIKRE15, pColon+1 );
		V_FixSlashes( szNewPath );

		int lastCount = m_SearchPaths.Count();
		AddSearchPathInternal( szNewPath, pPathID, PATH_ADD_TO_TAIL_ATINDEX, true, i );
		int numNewPaths = m_SearchPaths.Count() - lastCount;
		if ( numNewPaths )
		{
			// skip paths all the paths we just added
			i += numNewPaths;
			// this is really bad, skip past the zip we just considered, the next iteration will skip us to the next zip
			i++;
		}

		m_bSearchPathsPatchedAfterInstall = true;
	}

	if ( m_bSearchPathsPatchedAfterInstall )
	{
		// cache paths got added
		// shutdown non cache paths
		// must do multiple passes until no removal occurs
		bool bRemoved;
		while ( 1 )
		{
			bRemoved = false;

			for ( int i = 0; i < m_SearchPaths.Count(); i++ )
			{
				const char *pPathID = m_SearchPaths[i].GetPathIDString();
				if ( V_stricmp( pPathID, "GAME" ) && V_stricmp( pPathID, "MOD" ) )
				{
					// only consider these paths
					continue;
				}
			
				const char *pPath = m_SearchPaths[i].GetPathString();
				const char *pColon = strchr( pPath, ':' );
				if ( !pColon || 
					!V_stristr( pPath, "csgo" ) || 
					V_stristr( pPath, "_lv" ) || 
					V_stristr( pPath, "_dlc" ) ||
					V_stristr( pPath, "_tempcontent" ) )
				{
					// ignore relative paths, can't patch those
					// ignore any non csgo path
					// ignore lv, dlc, or tempcontent path, not installing those zips
					continue;
				}
				if ( !m_SearchPaths[i].GetPackFile() || m_SearchPaths[i].GetPackFile()->m_bIsMapPath )
				{
					// ignore non pack based paths
					// ignore bsps
					continue;
				}

				// Not installing localized data
				if ( m_SearchPaths[i].m_bIsLocalizedPath )
				{
					continue;
				}

				if ( V_stristr( pPath, "cache:" ) || !V_stristr( m_SearchPaths[i].GetPackFile()->m_ZipName.String(), "zip0" PLATFORM_EXT ".zip" ) )
				{
					// ignore any cache oriented paths
					// only want to remove non-cache paths of zip0.360.zip
					continue;
				}

				m_SearchPaths.Remove( i );
				bRemoved = true;
				break;
			}

			if ( !bRemoved )
			{
				break;
			}
		}
	}
#endif

	return m_bSearchPathsPatchedAfterInstall;
}


//-----------------------------------------------------------------------------
// Returns the search path, each path is separated by ;s. Returns the length of the string returned
// Pack search paths include the pack name, so that callers can still form absolute paths
// and that absolute path can be sent to the filesystem, and mounted as a file inside a pack.
//-----------------------------------------------------------------------------
int CBaseFileSystem::GetSearchPath( const char *pathID, bool bGetPackFiles, char *pPath, int nMaxLen )
{
	AUTO_LOCK( m_SearchPathsMutex );

	int nLen = 0;
	if ( nMaxLen )
	{
		pPath[0] = 0;
	}

	CSearchPathsIterator iter( this, pathID, bGetPackFiles ? FILTER_NONE : FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
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
	AUTO_LOCK( m_SearchPathsMutex );

	if ( nMaxLen )
	{
		pPath[0] = 0;
	}

	// determine unique PathIDs
	CUtlVector< CUtlSymbol > list;
	for ( int i = 0 ; i < m_SearchPaths.Count(); i++ )
	{
		CUtlSymbol pathID = m_SearchPaths[i].GetPathID();
		if ( pathID != UTL_INVAL_SYMBOL && list.Find( pathID ) == -1 )
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

	if ( const char *pActualPathID = pathID ? StringAfterPrefix( pathID, "COMPAT:" ) : NULL )
	{
		int c = m_SearchPaths.Count();
		for ( int i = c - 1; i >= 0; i-- )
		{
			char newCompatPath[ MAX_PATH ] = {};
			sprintf( newCompatPath, "/csgo/compatibility/%s/", pPath );
			Q_FixSlashes( newCompatPath );
			if ( V_strstr( m_SearchPaths[ i ].GetPathString(), newCompatPath ) )
			{
				m_SearchPaths.Remove( i );
				return true;
			}
		}
	}

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

	AUTO_LOCK( m_SearchPathsMutex );

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

	return NULL;
}


//-----------------------------------------------------------------------------
// Purpose: Finds a search path that should be used for writing to, given a pathID.
//-----------------------------------------------------------------------------
const char *CBaseFileSystem::GetWritePath( const char *pFilename, const char *pathID )
{
	CSearchPath *pSearchPath = NULL;
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

	pSearchPath = FindWritePath( pFilename, NULL ); // okay, just return the first search path added!
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
#ifdef _PS3
#define g_pszReadFilename GetTLSGlobals()->pFileSystemReadFilename
#else
CTHREADLOCALPTR(char) g_pszReadFilename;
#endif

bool CBaseFileSystem::ReadToBuffer( FileHandle_t fp, CUtlBuffer &buf, int nMaxBytes, FSAllocFunc_t pfnAlloc )
{
	SetBufferSize( fp, 0 );  // TODO: what if it's a pack file? restore buffer size?

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
			int nAlignedBase = AlignValue( (byte *)buf.Base(), nBufferAlign ) - (byte *)buf.Base();
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

	if ( IsGameConsole() )
	{
		// callers are sloppy, always want optimal
		bOptimalAlloc = true;
	}

	SetBufferSize( fp, 0 );  // TODO: what if it's a pack file? restore buffer size?

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
				*ppBuf = new byte[nBytesBuf];
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
			((byte *)(*ppBuf))[nBytesToRead] = 0;
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
	AUTO_LOCK( m_SearchPathsMutex );
	// Sergiy: AaronS said it is a good idea to destroy these paths in reverse order
	while( m_SearchPaths.Count() )
	{
		m_SearchPaths.Remove( m_SearchPaths.Count() - 1 );
	}
	//m_PackFileHandles.Purge();
}


void CBaseFileSystem::LogFileAccess( const char *pFullFileName )
{
	if( !m_pLogFile )
	{
		return;
	}
	if ( IsPS3() )
	{
		AssertMsg( false, "LogFileAccess broken on PS3\n" );
		return;
	}
	char buf[1024];
#if BSPOUTPUT
	Q_snprintf( buf, sizeof( buf ), "%s\n%s\n", pShortFileName, pFullFileName);
	fprintf( m_pLogFile, "%s", buf ); // STEAM OK
#else
	char cwd[MAX_FILEPATH];
	getcwd( cwd, MAX_FILEPATH-1 );
	Q_strcat( cwd, "\\", sizeof( cwd ) );
	if( Q_strnicmp( cwd, pFullFileName, strlen( cwd ) ) == 0 )
	{
		const char *pFileNameWithoutExeDir = pFullFileName + strlen( cwd );
		char targetPath[ MAX_FILEPATH ];
		strcpy( targetPath, "%fs_target%\\" );
		strcat( targetPath, pFileNameWithoutExeDir );
		Q_snprintf( buf, sizeof( buf ), "copy \"%s\" \"%s\"\n", pFullFileName, targetPath );
		char tmp[ MAX_FILEPATH ];
		Q_strncpy( tmp, targetPath, sizeof( tmp ) );
		Q_StripFilename( tmp );
		fprintf( m_pLogFile, "mkdir \"%s\"\n", tmp ); // STEAM OK
		fprintf( m_pLogFile, "%s", buf ); // STEAM OK
	}
	else
	{
		Assert( 0 );
	}
#endif
}

class CFileOpenInfo
{
public:
	CFileOpenInfo( CBaseFileSystem *pFileSystem, const char *pFileName, const CBaseFileSystem::CSearchPath *path, const char *pOptions, int flags, char **ppszResolvedFilename, bool bTrackCRCs ) : 
		m_pFileSystem( pFileSystem ), m_pFileName( pFileName ), m_pPath( path ), m_pOptions( pOptions ), m_Flags( flags ), m_ppszResolvedFilename( ppszResolvedFilename ), m_bTrackCRCs( bTrackCRCs )
	{
		// Multiple threads can access the whitelist simultaneously. 
		// That's fine, but make sure it doesn't get freed by another thread.
		if ( IsPC() )
		{
			m_pWhitelist = m_pFileSystem->m_FileWhitelist.AddRef();
		}
		else
		{
			m_pWhitelist = NULL;
		}
		m_pFileHandle = NULL;
		m_bLoadedFromSteamCache = m_bSteamCacheOnly = false;
		
		if ( m_ppszResolvedFilename )
			*m_ppszResolvedFilename = NULL;
	}
	
	~CFileOpenInfo()
	{
		if ( IsGameConsole() )
		{
			return;
		}

		m_pFileSystem->m_FileWhitelist.ReleaseRef( m_pWhitelist );
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

	// Handles telling CFileTracker about the file we just opened so it can remember
	// where the file came from, and possibly calculate a CRC if necessary.
	void HandleFileCRCTracking( const char *pRelativeFileName, bool bIsAbsolutePath )
	{
		if ( IsGameConsole() )
		{
			return;
		}

		
		if ( m_pFileHandle )
		{
			FILE *fp = m_pFileHandle->m_pFile;
			int64 nLength = m_pFileHandle->m_nLength;
			// we always record hashes of everything we load. we may filter later.
//			m_pFileSystem->m_FileTracker2.NoteFileLoadedFromDisk( pRelativeFileName, m_pPath->GetPathIDString(), fp, nLength );
        }

	}

	// Decides if the file must come from Steam or if it can be allowed to come off disk.
	void DetermineFileLoadInfoParameters( CFileLoadInfo &fileLoadInfo, bool bIsAbsolutePath )
	{
		if ( IsGameConsole() )
		{
			fileLoadInfo.m_bSteamCacheOnly = false;
			return;
		}

		if ( m_bTrackCRCs && m_pWhitelist && m_pWhitelist->m_pAllowFromDiskList && !bIsAbsolutePath )
		{
			Assert( !V_IsAbsolutePath( m_pFileName ) ); // (This is what bIsAbsolutePath is supposed to tell us..)
			// Ask the whitelist if this file must come from Steam.
			fileLoadInfo.m_bSteamCacheOnly = !m_pWhitelist->m_pAllowFromDiskList->IsFileInList( m_pFileName );
		}
		else
		{
			fileLoadInfo.m_bSteamCacheOnly = false;
		}	
	}

public:
	CBaseFileSystem *m_pFileSystem;
	CWhitelistSpecs *m_pWhitelist;

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
	
	char m_AbsolutePath[MAX_FILEPATH];	// This is set 
};

void CBaseFileSystem::HandleOpenRegularFile( CFileOpenInfo &openInfo, bool bIsAbsolutePath )
{
	// Setup the parameters for the call (like to tell Steam to force the file to come out of the Steam caches or not).
	CFileLoadInfo fileLoadInfo;
	openInfo.DetermineFileLoadInfoParameters( fileLoadInfo, bIsAbsolutePath );
	
	// xbox dvddev mode needs to convolve non-compliant fatx filenames
	// purposely placing this at this level, so only loose files pay the burden
	const char *pFilename = openInfo.m_AbsolutePath;

	int64 size;
	FILE *fp = Trace_FOpen( pFilename, openInfo.m_pOptions, openInfo.m_Flags, &size, &fileLoadInfo );
	if ( fp )
	{
		if ( m_pLogFile )
		{
			LogFileAccess( openInfo.m_AbsolutePath );
		}

		if ( m_bOutputDebugString )
		{
#ifdef _WIN32
			Plat_DebugString( "fs_debug: " );
			Plat_DebugString( openInfo.m_AbsolutePath );
			Plat_DebugString( "\n" );
#elif POSIX
			fprintf(stderr, "fs_debug: %s\n", openInfo.m_AbsolutePath );
#endif
		}

		openInfo.m_pFileHandle = new CFileHandle(this);
		openInfo.m_pFileHandle->m_pFile = fp;
		openInfo.m_pFileHandle->m_type = FT_NORMAL;
		openInfo.m_pFileHandle->m_nLength = size;

		openInfo.SetResolvedFilename( openInfo.m_AbsolutePath );
		
		// Remember what was returned by the Steam filesystem and track the CRC.
		openInfo.m_bLoadedFromSteamCache = fileLoadInfo.m_bLoadedFromSteamCache;
		openInfo.m_bSteamCacheOnly = fileLoadInfo.m_bSteamCacheOnly;
		openInfo.HandleFileCRCTracking( openInfo.m_pFileName, bIsAbsolutePath );
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
	VPROF( "CBaseFileSystem::FindFile" );

	char tempSymlinkBuffer[MAX_PATH];
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

#if defined( _GAMECONSOLE ) && defined( _DEBUG )
	// -pakfallbackfs will perform a filesystem search if the
	// requested file is not in pak zip (very expensive!)
	static
		enum PakFallback_t
		{
			PAK_FALLBACK_UNKNOWN, PAK_FALLBACK_ALLOW, PAK_FALLBACK_RETAIL
		}
		s_PakFallbackType = PAK_FALLBACK_UNKNOWN;
	if ( s_PakFallbackType == PAK_FALLBACK_UNKNOWN )
	{
		s_PakFallbackType = CommandLine()->FindParm( "-pakfallbackfs" ) ? PAK_FALLBACK_ALLOW : PAK_FALLBACK_RETAIL;
	}
#define IsPakStrictMode() ( s_PakFallbackType != PAK_FALLBACK_ALLOW )
#else
#define IsPakStrictMode() true
#endif
	
	if ( IsGameConsole() && IsPakStrictMode() )
	{
		if ( flags & FSOPEN_NEVERINPACK )
		{
			pathFilter = FILTER_CULLPACK;
		}
		else
		{
			// most all files on the dvd are expected to be in the pack
			// don't allow disk paths to be searched, which is very expensive on the dvd
			pathFilter = FILTER_CULLNONPACK;
		}
	}

	CSearchPathsIterator iter( this, &pFileName, pathID, pathFilter );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		FileHandle_t filehandle = FindFile( pSearchPath, pFileName, pOptions, flags, ppszResolvedFilename, bTrackCRCs );
		if ( filehandle )
			return filehandle;
	}

	return ( FileHandle_t )0;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
FileHandle_t CBaseFileSystem::OpenForRead( const char *pFileName, const char *pOptions, unsigned flags, const char *pathID, char **ppszResolvedFilename )
{
	VPROF( "CBaseFileSystem::OpenForRead" );
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

	int64 size;
	FILE *fp = Trace_FOpen( pTmpFileName, pOptions, 0, &size );
	if ( !fp )
	{
		return ( FileHandle_t )0;
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
		pPathID = NULL;
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
	VPROF_BUDGET( "CBaseFileSystem::Open", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

	if ( !pFileName )
		return (FileHandle_t)0;

#ifndef _CERT
	s_IoStats.OnFileOpen( pFileName );
#endif

	NoteIO();
	CFileHandleTimer *pTimer = NULL;
	bool bReportLongLoads = ( fs_report_long_reads.GetInt() > 0 );

	if ( bReportLongLoads )
	{
		// When a file is opened we add it to the list and note the time
		pTimer = new CFileHandleTimer;
		if ( pTimer != NULL )
		{
			// Need the lock only when adding to the vector, not during construction
			AUTO_LOCK( m_FileHandleTimersMutex );
			m_FileHandleTimers.AddToTail( pTimer );
			pTimer->Start();
		}
	}

	CHECK_DOUBLE_SLASHES( pFileName );

	if ( fs_report_sync_opens.GetInt() > 0 && ThreadInMainThread() && 
		 !bReportLongLoads ) // If we're reporting timings we have to delay this spew till after the file has been closed
	{
		Warning( "File::Open( %s ) on main thread.\n", pFileName );

#if !defined(_CERT) && !IsPS3() && !IsX360()
		if ( fs_report_sync_opens_callstack.GetInt() > 0 )
		{
			// GetCallstack() does not work on PS3, it is using TLS which is not supported in cross-platform manner
			const int CALLSTACK_SIZE = 16;
			void * pAddresses[CALLSTACK_SIZE];
			const int CALLSTACK_SKIP = 1;
			int nCount = GetCallStack( pAddresses, CALLSTACK_SIZE, CALLSTACK_SKIP );
			if ( nCount != 0)
			{
				// Allocate dynamically instead of using the stack, this path is going to be very rarely used
				const int BUFFER_SIZE = 4096;
				char * pBuffer = new char[ BUFFER_SIZE ];
				TranslateStackInfo( pAddresses, CALLSTACK_SIZE, pBuffer, BUFFER_SIZE, "\n", TSISTYLEFLAG_DEFAULT );
				Msg( "%s\n", pBuffer );
				delete[] pBuffer;
			}
		}
#endif
	}

	// Allow for UNC-type syntax to specify the path ID.
	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pathID, tempPathID );
	char tempFileName[MAX_PATH];
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

	if ( bReportLongLoads )
	{
		// Save the file handle for ID when we close it
		if ( hFile && pTimer )
		{
			pTimer->m_hFile = hFile;
			Q_strncpy( pTimer->m_szName, pFileName, sizeof( pTimer->m_szName ) );

			// See if we've opened this file before so we can accumulate time spent rereading files
			FileOpenDuplicateTime_t *pFileOpenDuplicate = NULL;

			AUTO_LOCK( g_FileOpenDuplicateTimesMutex );
			for ( int nFileOpenDuplicate = g_FileOpenDuplicateTimes.Count() - 1; nFileOpenDuplicate >= 0; --nFileOpenDuplicate )
			{
				FileOpenDuplicateTime_t *pTempFileOpenDuplicate = g_FileOpenDuplicateTimes[ nFileOpenDuplicate ];
				if ( Q_stricmp( pFileName, pTempFileOpenDuplicate->m_szName ) == 0 )
				{
					// Found it!
					pFileOpenDuplicate = pTempFileOpenDuplicate;
					break;
				}
			}

			if ( pFileOpenDuplicate == NULL )
			{
				// We haven't opened this file before, so add it to the list
				pFileOpenDuplicate = new FileOpenDuplicateTime_t;
				if ( pFileOpenDuplicate != NULL )
				{
					g_FileOpenDuplicateTimes.AddToTail( pFileOpenDuplicate );
					Q_strncpy( pFileOpenDuplicate->m_szName, pFileName, sizeof( pTimer->m_szName ) );
				}
			}

			// Increment the number of times we've opened this file
			if ( pFileOpenDuplicate != NULL )
			{
				pFileOpenDuplicate->m_nLoadCount++;
			}
		}
		else
		{
			// File didn't open, pop it off the list
			if ( pTimer != NULL )
			{
				// We need the lock only when removing from the vector, deleting the timer does not need it
				AUTO_LOCK( m_FileHandleTimersMutex );
				for ( int nTimer = m_FileHandleTimers.Count() - 1; nTimer >= 0; --nTimer )
				{
					CFileHandleTimer *pLocalTimer = m_FileHandleTimers[ nTimer ];
					if ( pLocalTimer == pTimer )
					{
						m_FileHandleTimers.Remove( nTimer );
						break;
					}
				}
				delete pTimer;
			}
		}
	}

	return hFile;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFileSystem::Close( FileHandle_t file )
{
	VPROF_BUDGET( "CBaseFileSystem::Close", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
	if ( !file )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to Close NULL file handle!\n" );
		return;
	}

	unsigned long ulLongLoadThreshold = fs_report_long_reads.GetInt();
	if ( ulLongLoadThreshold > 0 )
	{
		// Let's find the nTimer that matches the file (we assume that we only have to close once 
		CFileHandleTimer *pTimer = NULL;
		{
			AUTO_LOCK( m_FileHandleTimersMutex );
			// Still do from the end to the beginning for consistency with previous code (and make access to Count() only once).
			for ( int nTimer = m_FileHandleTimers.Count() - 1; nTimer >= 0; --nTimer )
			{
				CFileHandleTimer *pLocalTimer = m_FileHandleTimers[ nTimer ];
				if ( pLocalTimer && pLocalTimer->m_hFile == file )
				{
					pTimer = pLocalTimer;
					m_FileHandleTimers.Remove( nTimer );
					break;
				}
			}
		}

		// m_FileHandleTimers is not locked here (but we can still access pTimer)
		if ( pTimer != NULL )
		{
			// Found the file, report the time between opening and closing
			pTimer->End();

			unsigned long ulMicroseconds = pTimer->GetDuration().GetMicroseconds();

			if ( ulLongLoadThreshold <= ulMicroseconds )
			{
				Warning( "Open( %lu microsecs, %s )\n", ulMicroseconds, pTimer->m_szName );
			}

			// Accumulate time spent if this file has been opened at least twice
			{
				AUTO_LOCK( g_FileOpenDuplicateTimesMutex );
				for ( int nFileOpenDuplicate = 0; nFileOpenDuplicate < g_FileOpenDuplicateTimes.Count(); nFileOpenDuplicate++ )
				{
					FileOpenDuplicateTime_t *pFileOpenDuplicate = g_FileOpenDuplicateTimes[ nFileOpenDuplicate ];
					if ( Q_stricmp( pTimer->m_szName, pFileOpenDuplicate->m_szName ) == 0 )
					{
						if ( pFileOpenDuplicate->m_nLoadCount > 1 )
						{
							pFileOpenDuplicate->m_flAccumulatedMicroSeconds += pTimer->GetDuration().GetMicrosecondsF();
						}
						break;
					}
				}
			}

			delete pTimer;
		}
	}

	delete (CFileHandle*)file;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFileSystem::Seek( FileHandle_t file, int pos, FileSystemSeek_t whence )
{
	VPROF_BUDGET( "CBaseFileSystem::Seek", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
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
	VPROF_BUDGET( "CBaseFileSystem::Tell", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

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
	VPROF_BUDGET( "CBaseFileSystem::Size", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

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
	VPROF_BUDGET( "CBaseFileSystem::Size", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
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
		CWhitelistSpecs *pWhitelist = m_FileWhitelist.AddRef();
		if ( pWhitelist )
		{
			bool bAllowFromDisk = pWhitelist->m_pAllowFromDiskList->IsFileInList( pFileName );
			m_FileWhitelist.ReleaseRef( pWhitelist );
			
			if ( !bAllowFromDisk )
			{
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
		}
	}
	
	// Ok, fall through to the fast path.
	int iSize = 0;

	CSearchPathsIterator iter( this, &pFileName, pPathID );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
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
long CBaseFileSystem::FastFileTime( const CSearchPath *path, const char *pFileName )
{
	struct	_stat buf;
	
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




    if ( FS_stat( tempFileName, &buf ) != -1 )
    {
        return buf.st_mtime;
    }
#ifdef LINUX
    // Support Linux and its case sensitive file system
    char realName[MAX_PATH];
    const char *pRealName = findFileInDirCaseInsensitive( tempFileName, realName );
    if ( pRealName && FS_stat( pRealName, &buf ) != -1 )
    {
        return buf.st_mtime;
    }
#endif

	return ( 0L );
}

int CBaseFileSystem::FastFindFile( const CSearchPath *path, const char *pFileName )
{
	struct	_stat buf;
	
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


	

#if defined(_PS3)
	FixUpPathCaseForPS3(tempFileName);
#endif
	if ( FS_stat( tempFileName, &buf ) != -1 )
	{
		LogAccessToFile( "stat", tempFileName, "" );
		return buf.st_size;
	}

#ifdef LINUX
	// Support Linux and its case sensitive file system
	char realName[MAX_PATH];
	if ( findFileInDirCaseInsensitive( tempFileName, realName ) && FS_stat( realName, &buf ) != -1 )
	{
		return buf.st_size;
	}
#endif

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
	VPROF_BUDGET( "CBaseFileSystem::Read", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
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
	VPROF_BUDGET( "CBaseFileSystem::Write", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

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
	VPROF_BUDGET( "CBaseFileSystem::FPrintf", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
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
void CBaseFileSystem::SetBufferSize( FileHandle_t file, unsigned nBytes )
{
	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to SetBufferSize NULL file handle!\n" );
		return;
	}
	fh->SetBufferSize( nBytes );
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
	VPROF_BUDGET( "CBaseFileSystem::Flush", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
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
	VPROF_BUDGET( "CBaseFileSystem::ReadLine", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
	CFileHandle *fh = ( CFileHandle *)file;
	if ( !fh )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "FS:  Tried to ReadLine NULL file handle!\n" );
		return NULL;
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
	return ( nRead ) ? pOutput : NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pFileName - 
// Output : long
//-----------------------------------------------------------------------------
long CBaseFileSystem::GetFileTime( const char *pFileName, const char *pPathID )
{
	VPROF_BUDGET( "CBaseFileSystem::GetFileTime", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

	CHECK_DOUBLE_SLASHES( pFileName );

	CSearchPathsIterator iter( this, &pFileName, pPathID );

	char tempFileName[MAX_PATH];
	Q_strncpy( tempFileName, pFileName, sizeof(tempFileName) );
	Q_FixSlashes( tempFileName );
#ifdef _WIN32
	Q_strlower( tempFileName );
#endif

	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		long ft = FastFileTime( pSearchPath, tempFileName );
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

long CBaseFileSystem::GetPathTime( const char *pFileName, const char *pPathID )
{
	VPROF_BUDGET( "CBaseFileSystem::GetFileTime", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

	CSearchPathsIterator iter( this, &pFileName, pPathID );

	char tempFileName[MAX_PATH];
	Q_strncpy( tempFileName, pFileName, sizeof(tempFileName) );
	Q_FixSlashes( tempFileName );
#ifdef _WIN32
	Q_strlower( tempFileName );
#endif

	long pathTime = 0L;
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		long ft = FastFileTime( pSearchPath, tempFileName );
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
	if ( IsGameConsole() )
	{
		return;
	}

//	m_FileTracker2.MarkAllCRCsUnverified();
}


void CBaseFileSystem::CacheFileCRCs( const char *pPathname, ECacheCRCType eType, IFileList *pFilter )
{
	if ( IsGameConsole() )
	{
		return;
	}

	// Get a list of the unique search path names (mod, game, platform, etc).
	CUtlDict<int,int> searchPathNames;
	m_SearchPathsMutex.Lock();
	for ( int i = 0; i <  m_SearchPaths.Count(); i++ )
	{
		CSearchPath *pSearchPath = &m_SearchPaths[i];
		if ( searchPathNames.Find( pSearchPath->GetPathIDString() ) == searchPathNames.InvalidIndex() )
			searchPathNames.Insert( pSearchPath->GetPathIDString() );
	}
	m_SearchPathsMutex.Unlock();

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



int CBaseFileSystem::GetWhitelistSpewFlags()
{
	return m_WhitelistSpewFlags;
}


void CBaseFileSystem::SetWhitelistSpewFlags( int flags )
{
	m_WhitelistSpewFlags = flags;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pString - 
//			maxCharsIncludingTerminator - 
//			fileTime - 
//-----------------------------------------------------------------------------
void CBaseFileSystem::FileTimeToString( char *pString, int maxCharsIncludingTerminator, long fileTime )
{
	if ( IsGameConsole() )
	{
		char szTemp[ 256 ];

		time_t time = fileTime;
		V_strncpy( szTemp, ctime( &time ), sizeof( szTemp ) );
		char *pFinalColon = Q_strrchr( szTemp, ':' );
		if ( pFinalColon )
			*pFinalColon = '\0';

		// Clip off the day of the week
		V_strncpy( pString, szTemp + 4, maxCharsIncludingTerminator );
	}
	else
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
	VPROF_BUDGET( "CBaseFileSystem::FileExists", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
	NoteIO();

	CHECK_DOUBLE_SLASHES( pFileName );

	CSearchPathsIterator iter( this, &pFileName, pPathID );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
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

	struct	_stat buf;

	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pPathID, tempPathID );

	if ( Q_IsAbsolutePath( pFileName ) )
	{

		

		if ( FS_stat( pFileName, &buf ) != -1 )
		{
#ifdef WIN32
			if ( buf.st_mode & _S_IWRITE )
#elif defined( _PS3 )
			if( buf.st_mode & S_IWUSR )
#elif POSIX
			if ( buf.st_mode & S_IWRITE )
#else
			if ( buf.st_mode & S_IWRITE )
#endif
			{
				return true;
			}
		}
		return false;
	}

	CSearchPathsIterator iter( this, &pFileName, pPathID, FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFileName );


		

		if ( FS_stat( tempFileName, &buf ) != -1 )
		{
#ifdef WIN32
			if ( buf.st_mode & _S_IWRITE )
#elif defined( _PS3 )
			if( buf.st_mode & S_IWUSR )
#elif POSIX
			if ( buf.st_mode & S_IWRITE )
#else
			if ( buf.st_mode & S_IWRITE )
#endif
			{
				return true;
			}
		}
	}
	return false;
}


bool CBaseFileSystem::SetFileWritable( char const *pFileName, bool writable, const char *pPathID /*= 0*/ )
{
	CHECK_DOUBLE_SLASHES( pFileName );

#ifdef _WIN32
	int pmode = writable ? ( _S_IWRITE | _S_IREAD ) : ( _S_IREAD );
#elif defined( _PS3 )
	int pmode = writable ? ( S_IWUSR | S_IRUSR ) : ( S_IRUSR );
#else
	int pmode = writable ? ( S_IWRITE | S_IREAD ) : ( S_IREAD );
#endif

	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pPathID, tempPathID );

	if ( Q_IsAbsolutePath( pFileName ) )
	{

		

		return ( FS_chmod( pFileName, pmode ) == 0 );
	}

	CSearchPathsIterator iter( this, &pFileName, pPathID, FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		char tempFilename[ MAX_FILEPATH ];
		Q_snprintf( tempFilename, sizeof( tempFilename ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFilename );


		

		if ( FS_chmod( tempFilename, pmode ) == 0 )
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
	struct	_stat buf;

	char pTempBuf[MAX_PATH];
	Q_strncpy( pTempBuf, pFileName, sizeof(pTempBuf) );
	Q_StripTrailingSlash( pTempBuf );
	pFileName = pTempBuf;

	char tempPathID[MAX_PATH];
	ParsePathID( pFileName, pathID, tempPathID );
	if ( Q_IsAbsolutePath( pFileName ) )
	{

		

		if ( FS_stat( pFileName, &buf ) != -1 )
		{
			if ( buf.st_mode & _S_IFDIR )
				return true;
		}
		return false;
	}

	CSearchPathsIterator iter( this, &pFileName, pathID, FILTER_CULLPACK );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFileName );


		

		if ( FS_stat( tempFileName, &buf ) != -1 )
		{
			if ( buf.st_mode & _S_IFDIR )
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

	Q_FixSlashes( szScratchFileName );

	int len = strlen( szScratchFileName ) + 1;
	char *end = szScratchFileName + len;
	char *s = szScratchFileName;
	while( s < end )
    {
		if	(	PATHSEPARATOR( *s ) && 
				s != szScratchFileName && 
				( IsLinux() || IsPS3() || *( s - 1 ) != ':' ) 
			)
        {
			char save = *s;
			*s = '\0';
#if defined( _WIN32 )
			_mkdir( szScratchFileName );
#elif defined( _PS3 )
			CellFsStat status;
			//Only create is the path doesn't exist already - Jawad.
			if ( cellFsStat( szScratchFileName, &status ) != CELL_FS_SUCCEEDED )
				cellFsMkdir( szScratchFileName, CELL_FS_DEFAULT_CREATE_MODE_1 );
#elif defined( POSIX )
			mkdir( szScratchFileName, S_IRWXU |  S_IRGRP |  S_IROTH );// owner has rwx, rest have r
#endif
			*s = save;
        }
		s++;
    }

#if defined( _WIN32 )
	_mkdir( szScratchFileName );
#elif defined( _PS3 )
	CellFsStat status;
	if ( cellFsStat( szScratchFileName, &status ) != CELL_FS_SUCCEEDED )
		cellFsMkdir( szScratchFileName, CELL_FS_DEFAULT_CREATE_MODE_1 );
#elif defined( POSIX )
	mkdir( szScratchFileName, S_IRWXU |  S_IRGRP |  S_IROTH );
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Given an absolute path, do a find first find next on it and build
// a list of files.  Physical file system only
//-----------------------------------------------------------------------------
void CBaseFileSystem::FindFileAbsoluteListHelper( CUtlVector< CUtlString > &outAbsolutePathNames, FindData_t &findData, const char *pAbsoluteFindName )
{
	// TODO: figure out what PS3 does without VPKs
#ifndef _PS3

	

	char path[MAX_PATH];
	V_strncpy( path, pAbsoluteFindName, sizeof(path) );
	V_StripFilename( path );

	findData.findHandle = FS_FindFirstFile( pAbsoluteFindName, &findData.findData );

	while ( findData.findHandle != INVALID_HANDLE_VALUE )
	{
		char result[MAX_PATH];
		V_ComposeFileName( path, findData.findData.cFileName, result, sizeof(result) );

		outAbsolutePathNames.AddToTail( result );

		if ( !FS_FindNextFile( findData.findHandle, &findData.findData ) )
		{	
			FS_FindClose( findData.findHandle );
			findData.findHandle = INVALID_HANDLE_VALUE;
		}
	}
#else
	Error( "Not implemented!\n" );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Searches for a file in all paths and results absolute path names 
// for the file, works in pack files (zip and vpk) too.  Lets you search for 
// something like sound/sound.cache and get a list of every sound cache
//-----------------------------------------------------------------------------
void CBaseFileSystem::FindFileAbsoluteList( CUtlVector< CUtlString > &outAbsolutePathNames, const char *pWildCard, const char *pPathID )
{
	// TODO: figure out what PS3 does without VPKs
	VPROF_BUDGET( "CBaseFileSystem::FindFileAbsoluteList", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );

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
	findData.findHandle = INVALID_HANDLE_VALUE;

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

	return FindFirstHelper( pWildCard, pPathID, pHandle, NULL );
}


const char *CBaseFileSystem::FindFirstHelper( const char *pWildCard, const char *pPathID, FileFindHandle_t *pHandle, int *pFoundStoreID )
{
	VPROF_BUDGET( "CBaseFileSystem::FindFirst", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
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
	pFindData->findHandle = INVALID_HANDLE_VALUE;

	if ( Q_IsAbsolutePath( pWildCard ) )
	{

		

		pFindData->findHandle = FS_FindFirstFile( pWildCard, &pFindData->findData );
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


			

			pFindData->findHandle = FS_FindFirstFile( tempFileName, &pFindData->findData );
			pFindData->m_CurrentStoreID = pSearchPath->m_storeId;

			if ( pFindData->findHandle != INVALID_HANDLE_VALUE )
				break;
		}
	}

	// We have a result from the filesystem 
	if( pFindData->findHandle != INVALID_HANDLE_VALUE )
	{
		// Remember that we visited this file already.
		pFindData->m_VisitedFiles.Insert( pFindData->findData.cFileName, 0 );

		if ( pFoundStoreID )
			*pFoundStoreID = pFindData->m_CurrentStoreID;

		*pHandle = hTmpHandle;
		return pFindData->findData.cFileName;
	}
	// Handle failure here
	pFindData = 0;
	m_FindData.Remove(hTmpHandle);
	*pHandle = -1;

	return NULL;
}

const char *CBaseFileSystem::FindFirst( const char *pWildCard, FileFindHandle_t *pHandle )
{
	return FindFirstEx( pWildCard, NULL, pHandle );
}


// Get the next file, trucking through the path. . don't check for duplicates.
bool CBaseFileSystem::FindNextFileHelper( FindData_t *pFindData, int *pFoundStoreID )
{
	// PAK files???

	// Try the same search path that we were already searching on.
	if( FS_FindNextFile( pFindData->findHandle, &pFindData->findData ) )
	{
		if ( pFoundStoreID )
			*pFoundStoreID = pFindData->m_CurrentStoreID;

		return true;
	}

	// This happens when we searched a full path
	if ( pFindData->currentSearchPathID < 0 )
		return false;

	pFindData->currentSearchPathID++;

	if ( pFindData->findHandle != INVALID_HANDLE_VALUE )
	{
		FS_FindClose( pFindData->findHandle );
	}
	pFindData->findHandle = INVALID_HANDLE_VALUE;

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


		

		pFindData->findHandle = FS_FindFirstFile( tempFileName, &pFindData->findData );
		pFindData->m_CurrentStoreID = pSearchPath->m_storeId;
		if ( pFindData->findHandle != INVALID_HANDLE_VALUE )
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
	VPROF_BUDGET( "CBaseFileSystem::FindNext", VPROF_BUDGETGROUP_OTHER_FILESYSTEM );
	FindData_t *pFindData = &m_FindData[handle];

	while( 1 )
	{
		if( FindNextFileHelper( pFindData, NULL ) )
		{
			if ( pFindData->m_VisitedFiles.Find( pFindData->findData.cFileName ) == -1 )
			{
				pFindData->m_VisitedFiles.Insert( pFindData->findData.cFileName, 0 );
				return pFindData->findData.cFileName;
			}
		}
		else
		{
			return NULL;
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
	return !!( pFindData->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY );
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

	if ( pFindData->findHandle != INVALID_HANDLE_VALUE)
	{
		FS_FindClose( pFindData->findHandle );
	}
	pFindData->findHandle = INVALID_HANDLE_VALUE;

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

	struct	_stat buf;

	if ( pPathType )
	{
		*pPathType = PATH_IS_NORMAL;
	}

#ifdef _PS3
	// crush the filename to lowercase
	char lowercasedname[256];
	V_strncpy( lowercasedname, pFileName, 255 );
	V_strnlwr( lowercasedname, 255 );
	pFileName = lowercasedname;
#endif

	// Fill in the default in case it's not found...
	Q_strncpy( pFullPath, pFileName, fullPathBufferSize );

	if ( IsPC() && pathFilter == FILTER_NONE )
	{
		// X360TBD: PC legacy behavior never returned pack paths
		// do legacy behavior to ensure naive callers don't break
		pathFilter = FILTER_CULLPACK;
	}

	CSearchPathsIterator iter( this, &pFileName, pPathID, pathFilter );
	for ( CSearchPath *pSearchPath = iter.GetFirst(); pSearchPath != NULL; pSearchPath = iter.GetNext() )
	{
		int		dummy;
		int64 dummy64;

		char tempFileName[ MAX_FILEPATH ];
		Q_snprintf( tempFileName, sizeof( tempFileName ), "%s%s", pSearchPath->GetPathString(), pFileName );
		Q_FixSlashes( tempFileName );

		bool bFound = FS_stat( tempFileName, &buf ) != -1;
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
	return NULL;
}


const char *CBaseFileSystem::GetLocalPath( const char *pFileName, char *pLocalPath, int localPathBufferSize )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	return RelativePathToFullPath( pFileName, NULL, pLocalPath, localPathBufferSize );
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
	return FullPathToRelativePathEx( pFullPath, NULL, pRelative, nMaxLen );
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

	Assert( pathID || !IsGameConsole() );

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
	int fail = unlink( szScratchFileName );
	if ( fail != 0 )
	{
		FileSystemWarning( FILESYSTEM_WARNING, "Unable to remove %s! (errno %x)\n", szScratchFileName, errno );
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
	int fail = rename( szScratchFileName, pNewFileName );
	if (fail != 0)
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
#if defined( _WIN32 ) && !defined( _X360 )
	if ( !::GetCurrentDirectoryA( maxlen, pDirectory ) )
#elif ( defined( POSIX ) && !defined( _PS3 ) ) || defined( _X360 )
	if ( !getcwd( pDirectory, maxlen ) )
#endif
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

	if ( ( fs_warning_mode.GetInt() == 1 && !ThreadInMainThread() ) || ( fs_warning_mode.GetInt() == 2 && ThreadInMainThread() ) )
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
CBaseFileSystem::COpenedFile::COpenedFile( void )
{
	m_pFile = NULL;
	m_pName = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseFileSystem::COpenedFile::~COpenedFile( void )
{
	delete[] m_pName;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : src - 
//-----------------------------------------------------------------------------
CBaseFileSystem::COpenedFile::COpenedFile( const COpenedFile& src )
{
	m_pFile = src.m_pFile;
	if ( src.m_pName )
	{
		int len = strlen( src.m_pName ) + 1;
		m_pName = new char[ len ];
		Q_strncpy( m_pName, src.m_pName, len );
	}
	else
	{
		m_pName = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : src - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseFileSystem::COpenedFile::operator==( const CBaseFileSystem::COpenedFile& src ) const
{
	return src.m_pFile == m_pFile;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *name - 
//-----------------------------------------------------------------------------
void CBaseFileSystem::COpenedFile::SetName( char const *name )
{
	delete[] m_pName;
	int len = strlen( name ) + 1;
	m_pName = new char[ len ];
	Q_strncpy( m_pName, name, len );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : char
//-----------------------------------------------------------------------------
char const *CBaseFileSystem::COpenedFile::GetName( void )
{
	return m_pName ? m_pName : "???";
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseFileSystem::CSearchPath::CSearchPath( void )
{
	m_Path = g_PathIDTable.AddString( "" );
	m_pDebugPath = "";

	m_storeId = 0;
	m_pPackFile = NULL;
	m_pPathIDInfo = NULL;
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
	CSearchPath *pSearchPath = NULL;

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

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Load/unload a DLL
//-----------------------------------------------------------------------------
CSysModule *CBaseFileSystem::LoadModule( const char *pFileName, const char *pPathID, bool bValidatedDllOnly )
{
	CHECK_DOUBLE_SLASHES( pFileName );

	bool bPathIsGameBin = false; bPathIsGameBin; // Touch the var for !Win64 build compiler warnings.
	LogFileAccess( pFileName );
	if ( !pPathID )
	{
		pPathID = "EXECUTABLE_PATH"; // default to the bin dir
	}
	else if ( IsPlatformWindowsPC64() )
	{
		bPathIsGameBin = V_strcmp( "GAMEBIN", pPathID ) == 0;
	}

#if defined(POSIX) && defined(PLATFORM_64BITS)
	bPathIsGameBin = V_strcmp( "GAMEBIN", pPathID ) == 0;
#endif

	char tempPathID[ MAX_PATH ];
	ParsePathID( pFileName, pPathID, tempPathID );
	
	CUtlSymbol lookup = g_PathIDTable.AddString( pPathID );

	// a pathID has been specified, find the first match in the path list
	int c = m_SearchPaths.Count();
	for ( int i = 0; i < c; i++ )
	{

		if ( FilterByPathID( &m_SearchPaths[i], lookup ) )
			continue;

		Q_snprintf( tempPathID, sizeof(tempPathID), "%s%s", m_SearchPaths[i].GetPathString(), pFileName ); // append the path to this dir.
		CSysModule *pModule = Sys_LoadModule( tempPathID );
		if ( pModule ) 
		{
			// we found the binary in one of our search paths
			return pModule;
		}
		else if ( IsPlatformWindowsPC64() && bPathIsGameBin )
		{
			Q_snprintf( tempPathID, sizeof( tempPathID ), "%s%s%s%s", m_SearchPaths[ i ].GetPathString(), "x64", CORRECT_PATH_SEPARATOR_S, pFileName ); // append the path to this dir.
			pModule = Sys_LoadModule( tempPathID );
			if ( pModule )
			{
				// we found the binary in a 64-bit location.
				return pModule;
			}
		}
#if defined(POSIX) && defined(PLATFORM_64BITS)
		else if ( bPathIsGameBin )
		{
#if defined(LINUX)
			const char* plat_dir = "linux64";
#else
			const char* plat_dir = "osx64";
#endif
			Q_snprintf( tempPathID, sizeof( tempPathID ), "%s%s%s%s", m_SearchPaths[ i ].GetPathString(), plat_dir, CORRECT_PATH_SEPARATOR_S, pFileName ); // append the path to this dir.
			pModule = Sys_LoadModule( tempPathID );
			if ( pModule )
			{
				// we found the binary in a 64-bit location.
				return pModule;
			}
		}
#endif
	}

	// couldn't load it from any of the search paths, let LoadLibrary try
	return Sys_LoadModule( pFileName );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseFileSystem::UnloadModule( CSysModule *pModule )
{
	Sys_UnloadModule( pModule );
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

	if ( IsGameConsole() )
	{
		// 360 FS won't resolve any path with ../
		V_RemoveDotSlashes( str );
	}
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

bool CBaseFileSystem::GetFileTypeForFullPath( char const *pFullPath, wchar_t *buf, size_t bufSizeInBytes )
{
#if !defined( _X360 ) && !defined( POSIX )
	wchar_t wcharpath[512];
	::MultiByteToWideChar( CP_UTF8, 0, pFullPath, -1, wcharpath, sizeof( wcharpath ) / sizeof(wchar_t) );
	wcharpath[(sizeof( wcharpath ) / sizeof(wchar_t)) - 1] = L'\0';

	SHFILEINFOW info = { 0 };
	DWORD_PTR dwResult = SHGetFileInfoW( 
		wcharpath,
		0,
		&info,
		sizeof( info ),
		SHGFI_TYPENAME 
	);
	if ( dwResult )
	{
		wcsncpy( buf, info.szTypeName, ( bufSizeInBytes / sizeof( wchar_t  ) ) );
		buf[( bufSizeInBytes / sizeof( wchar_t ) ) - 1] = L'\0';
		return true;
	}
	else
#endif
	{
		char ext[32];
		Q_ExtractFileExtension( pFullPath, ext, sizeof( ext ) );
#ifdef POSIX		
		V_snwprintf( buf, ( bufSizeInBytes / sizeof( wchar_t ) ) - 1, L"%s File", V_strupr( ext ) ); // Matches what Windows does
#else
		V_snwprintf( buf, ( bufSizeInBytes / sizeof( wchar_t ) ) - 1, L".%S", ext );
#endif
		buf[( bufSizeInBytes / sizeof( wchar_t ) ) - 1] = L'\0';
	}
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
	if ( !IsGameConsole() )
	{
		Assert( 0 );
		return false;
	}

	AUTO_LOCK( m_SearchPathsMutex );

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

	if ( m_pPackFileHandle )
	{
		delete m_pPackFileHandle;
		m_pPackFileHandle = NULL;
	}

	if ( m_pFile )
	{
		m_fs->Trace_FClose( m_pFile );
		m_pFile = NULL;
	}

	m_nMagic = FREE_MAGIC;
}

void CFileHandle::Init( CBaseFileSystem *fs )
{
	m_nMagic = MAGIC;
	m_pFile = NULL;
	m_nLength = 0;
	m_type = FT_NORMAL;		
	m_pPackFileHandle = NULL;

	m_fs = fs;

	m_pszTrueFileName = 0;
}

bool CFileHandle::IsValid()
{
	return ( m_nMagic == MAGIC );
}

int CFileHandle::GetSectorSize()
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		return m_fs->FS_GetSectorSize( m_pFile );
	}
	else
	{
		return -1;
	}
}

bool CFileHandle::IsOK()
{
	if ( m_pFile )
	{
		return ( IsValid() && m_fs->FS_ferror( m_pFile ) == 0 );
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
		m_fs->FS_fflush( m_pFile );
	}
}

void CFileHandle::SetBufferSize( int nBytes )
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		m_fs->FS_setbufsize( m_pFile, nBytes );
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
		return m_fs->FS_fread( pBuffer, nDestSize, nLength, m_pFile );
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

	size_t nBytesWritten = m_fs->FS_fwrite( (void*)pBuffer, nLength, m_pFile  );

	m_fs->Trace_FWrite(nBytesWritten,m_pFile);

	return nBytesWritten;
}

int CFileHandle::Seek( int64 nOffset, int nWhence )
{
	Assert( IsValid() );

	if ( m_pFile )
	{
		m_fs->FS_fseek( m_pFile, nOffset, nWhence );
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
		return m_fs->FS_ftell( m_pFile );
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

int64 CFileHandle::AbsoluteBaseOffset()
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
#ifdef _GAMECONSOLE
	const char *pPath = g_PathIDTable.String( sp->GetPath() );
	if ( !pPath || !*pPath )
		return;

	if ( !XBX_IsAudioLocalized() )
	{
		return;
	}

	const char *pLanguage = XBX_GetLanguageString();
	if ( !pLanguage || !V_stricmp( pLanguage, "english" ) )
	{
		return;
	}

	int languagelen = V_strlen( pLanguage );
	int pathlen = V_strlen( pPath );
	// ignore trailing slash
	if ( pPath[ pathlen - 1 ] == '\\' || pPath[ pathlen - 1 ] == '/' )
	{
		--pathlen;
	}

	if ( pathlen > languagelen &&
		 V_strnicmp( pPath + pathlen - languagelen, pLanguage, languagelen ) == 0 )
	{
		sp->m_bIsLocalizedPath = true;
	}
#endif
}
#ifdef SUPPORT_IODELAY_MONITORING
class CIODelayAlarmThread : public CThread
{
public:
	CIODelayAlarmThread( CBaseFileSystem *pFileSystem );
	void WakeUp( void );
	CBaseFileSystem *m_pFileSystem;
	CThreadEvent m_hThreadEvent;

	volatile bool m_bThreadShouldExit;
	// CThread Overrides
	virtual int Run( void );
};


CIODelayAlarmThread::CIODelayAlarmThread( CBaseFileSystem *pFileSystem )
{
	m_pFileSystem = pFileSystem;
	m_bThreadShouldExit = false;

}

void CIODelayAlarmThread::WakeUp( void )
{
	m_hThreadEvent.Set();
}

int CIODelayAlarmThread::Run( void )
{
	while( ! m_bThreadShouldExit )
	{
		uint32 nWaitTime = 1000;
		float flCurTimeout = m_pFileSystem->m_flDelayLimit;
		if ( flCurTimeout > 0. )
		{
			nWaitTime = ( uint32 )( 1000.0 * flCurTimeout );
			m_hThreadEvent.Wait( nWaitTime );
		}
		// check for overflow 
		float flCurTime = Plat_FloatTime();
		if ( flCurTime - m_pFileSystem->m_flLastIOTime > m_pFileSystem->m_flDelayLimit )
		{
			Warning( " %f elapsed w/o i/o\n", flCurTime - m_pFileSystem->m_flLastIOTime );
			DebuggerBreakIfDebugging();
			m_pFileSystem->m_flLastIOTime = MAX( Plat_FloatTime(), m_pFileSystem->m_flLastIOTime );
		}
	}
	return 0;
}


#endif // SUPPORT_IODELAY_MONITORING


IIoStats *CBaseFileSystem::GetIoStats()
{
#ifndef _CERT
	return &s_IoStats;
#else
	return NULL;
#endif
}

CON_COMMAND( fs_dump_open_duplicate_times, "Set fs_report_long_reads 1 before loading to use this. Prints a list of files that were opened more than once and ~how long was spent reading from them." )
{
	float flTotalTime = 0.0f, flAccumulatedMilliseconds;

	AUTO_LOCK( g_FileOpenDuplicateTimesMutex );
	for ( int nFileOpenDuplicate = 0; nFileOpenDuplicate< g_FileOpenDuplicateTimes.Count(); nFileOpenDuplicate++ )
	{
		FileOpenDuplicateTime_t *pFileOpenDuplicate = g_FileOpenDuplicateTimes[ nFileOpenDuplicate ];
		if ( pFileOpenDuplicate )
		{
			if ( pFileOpenDuplicate->m_nLoadCount > 1 )
			{
				flTotalTime += pFileOpenDuplicate->m_flAccumulatedMicroSeconds;
				flAccumulatedMilliseconds = pFileOpenDuplicate->m_flAccumulatedMicroSeconds / 1000.0f;
				DevMsg( "Times Opened: %3i\t\tAccumulated Time: %10.5fms\t\tAverage Time per Open: %13.8fms\t\tFile: %s\n", 
						pFileOpenDuplicate->m_nLoadCount, flAccumulatedMilliseconds, ( flAccumulatedMilliseconds / pFileOpenDuplicate->m_nLoadCount ), 
						pFileOpenDuplicate->m_szName );
			}
		}
	}

	DevMsg( "Total Seconds: %.5f\n", flTotalTime / 1000000.0f );
}

CON_COMMAND( fs_clear_open_duplicate_times, "Clear the list of files that have been opened." )
{
	AUTO_LOCK( g_FileOpenDuplicateTimesMutex );
	for ( int nFileOpenDuplicate = 0; nFileOpenDuplicate< g_FileOpenDuplicateTimes.Count(); nFileOpenDuplicate++ )
	{
		FileOpenDuplicateTime_t *pFileOpenDuplicate = g_FileOpenDuplicateTimes[ nFileOpenDuplicate ];
		delete pFileOpenDuplicate;
		g_FileOpenDuplicateTimes[ nFileOpenDuplicate ] = NULL;
	}

	g_FileOpenDuplicateTimes.RemoveAll();
}

#if IsPlatformPS3()

class CFiosAllocator : public cell::fios::allocator
{
public:
	void* Allocate(uint32_t size, uint32_t flags, const char* pFile, int line)
	{
		() pFile;
		() line;
		return memalign(FIOS_ALIGNMENT_FROM_MEMFLAGS(flags), size);
	}

	void Deallocate(void* pMemory, uint32_t flags, const char* pFile, int line)
	{
		() flags;
		() pFile;
		() line;
		free(pMemory);
	}

	void* Reallocate(void* pMemory, uint32_t newSize, uint32_t flags, const char* pFile, int line)
	{
		() pMemory;
		() newSize;
		() flags;
		() pFile;
		() line;
		return NULL; /* fios does not use Reallocate */
	}
};

// Configure FIOS, allows prefetching of whole files of files within pack files.
class CFiosConfiguration
{
public:
	CFiosConfiguration();
	~CFiosConfiguration();

	bool Setup();
	bool Teardown();

	void FlushCache();

	// These methods are not thread safe.
	bool PrefetchFile( const char * pFileName, int nPriority, bool bPersist );
	bool PrefetchFile( const char * pFileName, int nPriority, bool bPersist, int64 nOffset, int64 nSize );

	void SuspendPrefetches( const char *pWhy );
	void ResumePrefetches( const char *pWhy );

	void PrintPrefetches();
	void PrintPrefetch( int nSlot );
	int ClearFinishedPrefetches();

	const char * GetPathCached() const;

	void CancelAllPrefetches();

	bool IsPrefetchingDone();

private:

	void CancelAndRespawnPrefetches( bool bPersistent );
	int ResumePrefetchesToZero();
	void SuspendPrefetchesToN( int nNumberOfSuspends );

	CFiosAllocator					m_Allocator;
	cell::fios::media *				m_SysCacheMedia;
	cell::fios::scheduler *			m_SchedulerForHDD;
	cell::fios::schedulercache *	m_SchedulerCache;
	cell::fios::media *				m_DevBdvdMedia;
	cell::fios::scheduler *			m_MainScheduler;

	class CPrefetchInfo
	{
	public:
		CPrefetchInfo( cell::fios::op * pOp, const char * pFileName, int64 nOffset, int64 nSize, const cell::fios::opattr_t & opAttributes, bool bPersistent )
			:
			m_OpAttributes( opAttributes ),
			m_nOffset( nOffset ),
			m_nSize( nSize ),
			m_pOp( pOp ),
			m_bPersitent( bPersistent )
		{
			Assert( pOp != NULL );
			V_strncpy( m_FileName, pFileName, sizeof( m_FileName ) );
		}

		// Clone but use a different cell::fios::op *.
		CPrefetchInfo( cell::fios::op * pOp, const CPrefetchInfo & other )
			:
			m_OpAttributes( other.m_OpAttributes ),
			m_nOffset( other.m_nOffset ),
			m_nSize( other.m_nSize ),
			m_pOp( pOp ),
			m_bPersitent( other.m_bPersitent )
		{
			Assert( pOp != NULL );
			V_strncpy( m_FileName, other.m_FileName, sizeof( m_FileName ) );
		}

		cell::fios::op * GetOp() const
		{
			return m_pOp;
		}

		const char * GetFileName() const
		{
			return m_FileName;
		}

		int64 GetOffset() const
		{
			return m_nOffset;
		}

		int64 GetSize() const
		{
			return m_nSize;
		}

		const cell::fios::opattr_t & GetOpAttributes() const
		{
			return m_OpAttributes;
		}

		bool IsPersistent() const
		{
			return m_bPersitent;
		}

	private:
		char					m_FileName[256];
		cell::fios::opattr_t	m_OpAttributes;
		int64					m_nOffset;
		int64					m_nSize;
		cell::fios::op *		m_pOp;
		bool					m_bPersitent;
	};

	CUtlVector< CPrefetchInfo * > m_Prefetches;
};

CFiosConfiguration g_FiosConfiguration;

bool SetupFios()
{
	if ( fs_fios_enabled.GetBool() )
	{
		return g_FiosConfiguration.Setup();
	}
	else
	{
		return false;
	}
}

bool TeardownFios()
{
	if ( fs_fios_enabled.GetBool() )
	{
		return g_FiosConfiguration.Teardown();
	}
	else
	{
		return false;
	}
}

const char * CFiosConfiguration::GetPathCached() const
{
	return g_pPS3PathInfo->GameImagePath();
}

CFiosConfiguration::CFiosConfiguration()
	:
	m_Allocator(),
	m_SysCacheMedia( NULL ),
	m_SchedulerForHDD( NULL ),
	m_SchedulerCache( NULL ),
	m_DevBdvdMedia( NULL ),
	m_MainScheduler( NULL ),
	m_Prefetches( 4, 32 )				// 32 allows us to prefetch 16 high priority prefetches at the same time as one low priority prefetch
										// With the low priority being canceled and restarted every time.
{
	// Do nothing...
}

CFiosConfiguration::~CFiosConfiguration()
{
	// Do nothing...
	Assert( m_Prefetches.Count() == 0 );
}

// SONY overrides new with the types ps3media and schedulercache.
#include "memdbgoff.h"

bool CFiosConfiguration::Setup()
{
	cell::fios::fios_parameters parameters = FIOS_PARAMETERS_INITIALIZER;

	parameters.pAllocator = &m_Allocator;
	parameters.pLargeMemcpy = 0; // Use memcpy
	parameters.pVprintf = 0;     // Use vprintf

	// Enable this only to see if the cache is working as expected
	//parameters.profiling = cell::fios::kProfileOps | cell::fios::kProfileCache;

	cell::fios::FIOSInit( &parameters );

	m_DevBdvdMedia = new cell::fios::ps3media( GetPathCached() );
	if ( m_DevBdvdMedia == NULL )
	{
		Warning( "[CFiosConfiguration::Setup] Can't allocate dev bdvd media.\n" );
		return false;
	}

	m_SysCacheMedia = new cell::fios::ps3media( g_pPS3PathInfo->SystemCachePath() );
	if ( m_SysCacheMedia == NULL )
	{
		Warning( "[CFiosConfiguration::Setup] Can't allocate system cache media.\n" );
		return false;
	}

	m_SchedulerForHDD = cell::fios::scheduler::createSchedulerForMedia( m_SysCacheMedia );
	if ( m_SchedulerForHDD == NULL )
	{
		Warning( "[CFiosConfiguration::Setup] Can't create scheduler for media.\n" );
		return false;
	}

	// Use SONY-like configuration:
	const int BLOCK_SIZE = 1024 * 1024;		// 1 Mb blocks.
	const int NUMBER_OF_BLOCKS = 1800;		// 1800 blocks mean that the cache is going to be 1.8 Gb big.
											// We leave 237 MB for the other usages.
											// In theory, without counting other systems, we can go up to 2037 blocks.

	// To remove one potential cause for a CERT issue, let's remove the usage of this flag (#if 0)
	// This is a speculative fix for crashes that would not exist for the end user (although SONY states that we should not ship with this setting turned on).
#if defined(_CERT) && 0
	const bool bCheckModification = false;	// No need to scan for modifications at release time (faster)
#else
	const bool bCheckModification = true;	// But we want to double-check during development to avoid stale data
#endif

	m_SchedulerCache = new cell::fios::schedulercache( m_DevBdvdMedia, m_SchedulerForHDD,
		"FILECACHE",					// Cache directory
		0x0123456789ABCDEFll,			// diskId. For one disk only title, we can hard-code it
		true,							// Use single-file as it is faster.
		bCheckModification, NUMBER_OF_BLOCKS, BLOCK_SIZE);
	if ( m_SchedulerCache == NULL )
	{
		Warning( "[CFiosConfiguration::Setup] Can't allocate m_SchedulerCache.\n" );
		return false;
	}

	m_MainScheduler = cell::fios::scheduler::createSchedulerForMedia( m_SchedulerCache );
	if (m_MainScheduler == NULL )
	{
		Warning( "[CFiosConfiguration::Setup] Can't allocate m_MainScheduler.\n" );
		return false;
	}

	// Starts the prefetches 0.5 second after no disk-usage.
	// This should make the IO faster for normal cases, prefetch will start when sounds / animations have not been needed IO for a while.
	// Except it creates huge IO issues. These functions are not reliable at all, created deadlocks, etc... Re-enable it on the PS4.
	// m_MainScheduler->setPrefetchDelay( cell::fios::FIOSMillisecondsToAbstime( 500 ) );
	// m_MainScheduler->setPrefetchThrottle( cell::fios::FIOSMillisecondsToAbstime( 100 ) );		// One prefetch command every 3 game frames
																									// This will make the prefetches two times slower.

	m_MainScheduler->setDefault();

	// Currently we are not creating a RAM cache (this could speed up some small reads). 
	return true;
}

bool CFiosConfiguration::Teardown()
{
	// Tearing down, we need to make sure suspend state is reset (FIOS limitation).
	ResumePrefetchesToZero();

	// Ops are taken cared of when we destroy the scheduler, but here we want to make sure PrefetchInfo are also deallocated correctly
	CancelAllPrefetches();

	// We cancel all outstanding operations
	m_MainScheduler->cancelAllOps();

	// Wait for the main scheduler to be idle, so all the operations are effectively canceled (otherwise FIOS may crash in shutdownAndCancelOps).
	while ( m_MainScheduler->isIdle() == false )
	{
		ThreadSleep( 1 );
	}

	// sometimes, files aren't closed by file system, and this will prevent the scheduler from shutting down
	m_MainScheduler->closeAllFiles();
	
	m_MainScheduler->shutdownAndCancelOps();

	cell::fios::scheduler::destroyScheduler( m_MainScheduler );
	m_MainScheduler = NULL;
	delete m_SchedulerCache;
	m_SchedulerCache = NULL;
	m_SchedulerForHDD->shutdownAndCancelOps();
	cell::fios::scheduler::destroyScheduler( m_SchedulerForHDD );
	m_SchedulerForHDD = NULL;
	delete m_SysCacheMedia;
	m_SysCacheMedia = NULL;
	delete m_DevBdvdMedia;
	m_DevBdvdMedia = NULL;

	cell::fios::FIOSTerminate();
	return true;
}

// Turn it back on
#include "memdbgon.h"

void CFiosConfiguration::FlushCache()
{
	m_SchedulerCache->flush();
}

bool CFiosConfiguration::PrefetchFile( const char * pFileName, int nPriority, bool bPersist )
{
#if 0
	// Because prefetching a big file is going to block all the other prefetches (and mess up the priorities if we need some prefetch done ASAP)
	// We are going to split a big prefetch in smaller pieces.

	// This is not necessary anymore:
	// If we cancel a prefetch in the middle and restart it, all the file portion that have been already prefetched before the cancellation
	// will be directly accounted for. I.e. there is not a lot of overhead to cancel and restart a prefetch.

	int64 nFileSize;
	cell::fios::err_t err = cell::fios::scheduler::getDefaultScheduler()->getFileSizeSync( NULL, pFileName, &nFileSize );
	if ( err < 0 )
	{
		Warning( "Can't retrieve size of file '%s'.\n", pFileName );
		return false;
	}

	const int MAX_PREFETCH_BLOCK_SIZE = 16 * 1024 * 1024;		// We prefetch 16 Mb at a time max
																// This is around 2 seconds of prefetching at the BluRay speed.
																// A 1.2 Gb file (like the sound zip file on Portal 2) will use 75 prefetch commands.
																// And will take a little bit more than 2 minutes to be prefetched.
	int64 nOffset = 0;
	int64 nRemainingSize = nFileSize;
	while ( nRemainingSize != 0 )
	{
		int64 nPrefetchSize = MIN( nRemainingSize, MAX_PREFETCH_BLOCK_SIZE );
		if ( PrefetchFile( pFileName, nPriority, bPersist, nOffset, nPrefetchSize ) == false)
		{
			return false;
		}
		nRemainingSize -= nPrefetchSize;
		nOffset += nPrefetchSize;
	}
	return true;
#else
	return PrefetchFile( pFileName, nPriority, bPersist, 0, FIOS_OFF_T_MAX );
#endif
}

bool CFiosConfiguration::PrefetchFile( const char * pFileName, int nPriority, bool bPersist, int64 nOffset, int64 nSize )
{
	// Before we prefetch more, let's clear the old prefetches
	ClearFinishedPrefetches();

	cell::fios::opattr_t opattr = FIOS_OPATTR_INITIALIZER;
	opattr.deadline = bPersist ? kDEADLINE_LATER : kDEADLINE_ASAP;	// If not persistent, assume that we need this sooner rather than later
																	// ASAP maybe a bit high (but still lower than NOW)
																	// This is to handle the case where non-persistent files (usually for a given map)
																	// are prefetched before persistent files (usually for the game in general).

																	// Note that FIOS doe not seem to care about the deadline for prefetches (or the priority). :(
	opattr.priority = nPriority;
	opattr.pCallback = NULL;
	opattr.opflags = bPersist ? cell::fios::kOPF_CACHEPERSIST : 0;
	opattr.pLicense = 0;
	cell::fios::op * pOp = cell::fios::scheduler::getDefaultScheduler()->prefetchFile( &opattr, pFileName, nOffset, nSize );
	if ( pOp == NULL )
	{
		Warning( "FIOS error: Can't prefetch the file '%s'.\n", pFileName );
		return false;
	}

	CPrefetchInfo * pPrefetchInfo = new CPrefetchInfo( pOp, pFileName, nOffset, nSize, opattr, bPersist );
	m_Prefetches.AddToTail( pPrefetchInfo );

	if ( bPersist == false )
	{
		// If the prefetch is not persistent, it is deemed higher priority than persistent prefetch
		// (as they are map specific prefetching, so need to happen sooner rather than later).
		// Due to incorrect priorities in the FIOS prefetching engine, we are going to cancel persistent prefetches
		// And recreate them (they will be at the end of the list and recreated just after, as FIOS seems to execute them in order :().
		// This is a workaround that we hope will work.

		// We can't suspend / resume individual op. We can suspend / resume all prefetches but that will not help us here.
		CancelAndRespawnPrefetches( true );
	}
	return true;
}

void CFiosConfiguration::SuspendPrefetches( const char *pWhy )
{
	if ( fs_fios_spew_prefetches.GetBool() )
	{
		Msg( "[Fios] Suspend prefetches. %s\n", pWhy );
	}
	cell::fios::scheduler::getDefaultScheduler()->suspendPrefetch();
}

void CFiosConfiguration::ResumePrefetches( const char *pWhy )
{
	if ( fs_fios_spew_prefetches.GetBool() )
	{
		Msg( "[Fios] Resume prefetches. %s\n", pWhy );
	}
	cell::fios::scheduler::getDefaultScheduler()->resumePrefetch();
}

void CFiosConfiguration::CancelAndRespawnPrefetches( bool bPersistent )
{
	// This code will add new prefetches, but we are only interested in the prefetch already there

	// Note that this code is pretty inefficient if called several times in a row
	// (like when we prefetch non-persistent file one after the other).
	// But it should not happen often enough to be a big performance issue (we are talking about microseconds, not milliseconds).
	int nSize = m_Prefetches.Count();
	for ( int i = 0 ; i < nSize ; ++i )
	{
		CPrefetchInfo * pPrefetchInfo = m_Prefetches[i];
		if ( pPrefetchInfo->IsPersistent() == false )
		{
			// We want to keep these.
			continue;
		}
		cell::fios::op * pOp = pPrefetchInfo->GetOp();
		if ( pOp->isDone() || pOp->isCancelled() )
		{
			// Already done or canceled, nothing to cancel...
			continue;
		}
		// We cancel it
		pOp->cancel();
		// And recreate it, it will be pretty much pushed at the end of the FIOS stack.
		// If the previous op happened to have finished earlier, re-prefetching it will not do another I/O operation.

		cell::fios::op * pNewOp = cell::fios::scheduler::getDefaultScheduler()->prefetchFile( &pPrefetchInfo->GetOpAttributes(), pPrefetchInfo->GetFileName(),
																		   pPrefetchInfo->GetOffset(), pPrefetchInfo->GetSize() );
		if ( pNewOp == NULL )
		{
			Warning( "FIOS error: Can't prefetch the file '%s'.\n", pPrefetchInfo->GetFileName() );
			continue;		// Not restarting the prefetch is not critical but could reduce the game experience
							// Continue with the other files
		}

		CPrefetchInfo * pNewPrefetchInfo = new CPrefetchInfo( pNewOp, *pPrefetchInfo );
		m_Prefetches.AddToTail( pNewPrefetchInfo );
	}

	// In case the canceled ops are done by now (this will also clear the old CPrefetchInfo).
	ClearFinishedPrefetches();
}

void CFiosConfiguration::PrintPrefetches()
{
	int nSize = m_Prefetches.Count();
	if ( nSize != 0 )
	{
		for (int i = 0 ; i < nSize ; ++i )
		{
			PrintPrefetch( i );
		}
	}
	else
	{
		Msg( "No prefetch in progress.\n" );
	}
}

void CFiosConfiguration::PrintPrefetch( int nSlot )
{
	CPrefetchInfo * pPrefetchInfo = m_Prefetches[nSlot];
	cell::fios::op * pOp = pPrefetchInfo->GetOp();

	int nPriority = pOp->getPriority();
	const float ONE_MEGABYTE = 1024.0f * 1024.0f;
	int64_t nRequestedSize = pOp->getRequestedSize();		// Note that if the file is 1.2 Gb (like the sound zip file in Portal2), the reported size will be incorrect.
	if ( nRequestedSize == FIOS_OFF_T_MAX )
	{
		// We only get the size when we print it (not in normal process).
		int64 nFileSize;
		cell::fios::err_t err = cell::fios::scheduler::getDefaultScheduler()->getFileSizeSync( NULL, pPrefetchInfo->GetFileName(), &nFileSize );
		nRequestedSize = nFileSize - pPrefetchInfo->GetOffset();
	}

	float fRequestedSize = ( nRequestedSize >= 0 ) ? ( ( float )( nRequestedSize ) / ONE_MEGABYTE ) : -1.f;	// Sometimes the size is unknown, use -1 in this case
	float fFullfilledSize = ( float )( pOp->getFulfilledSize() ) / ONE_MEGABYTE;

	if ( pOp->isDone() || pOp->isCancelled() )
	{
		cell::fios::err_t nError = pOp->getError();
		if ( nError < 0 )
		{
			Msg( "Slot[%d] File: '%s' - Priority: %d - Requested size: %0.1f Mb - Fulfilled size: %0.1f Mb - Error: 0x%08X.\n",
				nSlot, pPrefetchInfo->GetFileName(), nPriority, fRequestedSize, fFullfilledSize, nError );
		}
		else
		{
			Msg( "Slot[%d] File: '%s' - Priority: %d - Requested size: %0.1f Mb - Fulfilled size: %0.1f Mb - Done.\n",
				nSlot, pPrefetchInfo->GetFileName(), nPriority, fRequestedSize, fFullfilledSize );
		}
	}
	else
	{
		int millisecondsToCompletion = cell::fios::FIOSAbstimeToMilliseconds( pOp->getEstimatedCompletion()  );
		float fSecondsToCompletion = (float)millisecondsToCompletion / 1000.0f;

		Msg( "Slot[%d] File: '%s' - Priority: %d - Requested size: %0.1f Mb - Fulfilled size: %0.1f Mb - Estimated completion: %0.3f seconds.\n",
			nSlot, pPrefetchInfo->GetFileName(), nPriority, fRequestedSize, fFullfilledSize, fSecondsToCompletion );
	}
}

int CFiosConfiguration::ClearFinishedPrefetches()
{
	int nCleared = 0;
	int nSize = m_Prefetches.Count();
	// From end to beginning to indices are preserved during the scan
	for ( int i = nSize - 1 ; i >= 0 ; --i )
	{
		CPrefetchInfo * pPrefetchInfo = m_Prefetches[i];
		cell::fios::op * pOp = pPrefetchInfo->GetOp();
		if ( pOp->isDone() || pOp->isCancelled() )
		{
			// Remove it from the list
			delete pPrefetchInfo;
			++nCleared;
			cell::fios::scheduler::getDefaultScheduler()->deleteOp( pOp );

			m_Prefetches.Remove( i );
		}
	}
	return nCleared;
}

void CFiosConfiguration::CancelAllPrefetches()
{
	int nSize = m_Prefetches.Count();
	for ( int i = 0 ; i < nSize ; ++i )
	{
		CPrefetchInfo * pPrefetchInfo = m_Prefetches[i];
		cell::fios::op * pOp = pPrefetchInfo->GetOp();
		if ( pOp->isDone() || pOp->isCancelled() )
		{
			continue;
		}
		pOp->cancel();
	}

	// The prefetches may actually have been suspended (like during map loading).
	// cancel() may not do anything in this suspended state, so let's resume so they can complete.
	int nNumberOfSuspends = ResumePrefetchesToZero();

	// This is going to be a blocking call until all the ops are considered done (after cancellation).
	// Ops are canceled very quickly though.
	while ( m_Prefetches.Count() != 0 )
	{
		ClearFinishedPrefetches();
	}

	// Once it is over, we re-suspend the same number of times the prefetches (so the state is the same as when we entered the function)
	SuspendPrefetchesToN( nNumberOfSuspends );
}

int CFiosConfiguration::ResumePrefetchesToZero()
{
	uint32 nNumberOfSuspends = cell::fios::scheduler::getDefaultScheduler()->getPrefetchSuspendCount();

	for ( int i = 0 ; i < nNumberOfSuspends ; ++i )
	{
		cell::fios::scheduler::getDefaultScheduler()->resumePrefetch( "Resume so CancelAllPrefetches() can complete." );
	}
	Assert( cell::fios::scheduler::getDefaultScheduler()->getPrefetchSuspendCount() == 0 );
	return ( int )nNumberOfSuspends;
}

void CFiosConfiguration::SuspendPrefetchesToN( int nNumberOfSuspends )
{
	for ( int i = 0 ; i < nNumberOfSuspends ; ++i )
	{
		cell::fios::scheduler::getDefaultScheduler()->suspendPrefetch( "Suspends restored after CancelAllPrefetches() has completed." );
	}
	Assert( cell::fios::scheduler::getDefaultScheduler()->getPrefetchSuspendCount() == nNumberOfSuspends );
}

bool CFiosConfiguration::IsPrefetchingDone()
{
	int nSize = m_Prefetches.Count();
	for ( int i = 0 ; i < nSize ; ++i )
	{
		CPrefetchInfo * pPrefetchInfo = m_Prefetches[i];
		cell::fios::op * pOp = pPrefetchInfo->GetOp();
		if ( ( pOp->isDone() || pOp->isCancelled() ) == false )
		{
			return false;
		}
	}
	return true;			// All are done or canceled
}

bool CBaseFileSystem::PrefetchFile( const char *pFileName, int nPriority, bool bPersist )
{
	if ( fs_fios_enabled.GetBool() )
	{
		return g_FiosConfiguration.PrefetchFile( pFileName, nPriority, bPersist );
	}
	else
	{
		return false;
	}
}

bool CBaseFileSystem::PrefetchFile( const char *pFileName, int nPriority, bool bPersist, int64 nOffset, int64 nSize )
{
	if ( fs_fios_enabled.GetBool() )
	{
		return g_FiosConfiguration.PrefetchFile( pFileName, nPriority, bPersist, nOffset, nSize );
	}
	else
	{
		return false;
	}
}

void CBaseFileSystem::FlushCache()
{
	if ( fs_fios_enabled.GetBool() )
	{
		g_FiosConfiguration.FlushCache();
	}
}

void CBaseFileSystem::SuspendPrefetches( const char *pWhy )
{
	if ( fs_fios_enabled.GetBool() )
	{
		g_FiosConfiguration.SuspendPrefetches( pWhy );
	}
}

bool g_bUseFiosHddCache = true;

void CBaseFileSystem::OnSaveStateChanged( bool bSaving )
{
	static CInterlockedInt nPrefetchesDueToSaving = 0;

	if ( bSaving )
	{
		// If we are saving, we want to reduce the HDD access as much as possible
		// That way, normal IO and saving IO don't compete for the HDD usage.
		// Normal IO will not be slowed down by the saving (we will use the BluRay instead),
		// And hopefully the saving will be faster too.
		SuspendPrefetches( "Saving" );
		++nPrefetchesDueToSaving;
		g_bUseFiosHddCache = false;					// Let's stop using the HDD cache (read and write)
	}
	else
	{
		// In case, OnSaveStateChanged( false ) is not called as many times as OnSaveStateChanged( true ),
		// let's restore the prefetch state (until nPrefetchesDueToSaving == 0).
		// This is a paranoid code, in case we don't have expected parity.
		for ( ; ; )
		{
			int nResult = --nPrefetchesDueToSaving;
			if ( nResult < 0 )
			{
				// We decremented too far
				++nPrefetchesDueToSaving;			// Put back the value of 0
				break;								// We are done with the resume prefetches related to the save
			}
			ResumePrefetches( "Save finished" );
			// Let's continue until nPrefetchesDueToSaving == 0
		}
		g_bUseFiosHddCache = true;					// Safe to re-use the cache again
	}
}

bool CBaseFileSystem::IsPrefetchingDone()
{
	return g_FiosConfiguration.IsPrefetchingDone( );
}

void CBaseFileSystem::ResumePrefetches( const char *pWhy )
{
	if ( fs_fios_enabled.GetBool() )
	{
		g_FiosConfiguration.ResumePrefetches( pWhy );
	}
}

CON_COMMAND( fs_fios_flush_cache, "Flushes the FIOS HDD cache." )
{
	if ( fs_fios_enabled.GetBool() )
	{
		g_FiosConfiguration.FlushCache();
		Msg( "FIOS cache flushed.\n" );
	}
}

CON_COMMAND( fs_fios_print_prefetches, "Displays all the prefetches currently in progress." )
{
	if ( fs_fios_enabled.GetBool() )
	{
		g_FiosConfiguration.PrintPrefetches();

		// Clear it after displaying the list so the user has a chance to view the one finished
		int nCleared = g_FiosConfiguration.ClearFinishedPrefetches();
		if ( nCleared != 0 )
		{
			Msg( "%d prefetch(s) finished and removed from the list.\n", nCleared );
		}
	}
}

CON_COMMAND( fs_fios_prefetch_file, "Prefetches a file: </PS3_GAME/USRDIR/filename.bin>.\nThe preftech is medium priority and persistent." )
{
	if ( fs_fios_enabled.GetBool() )
	{
		if ( args.ArgC() == 2 )
		{
			// Minimum priority and persistent for full zip file
			// As the assumption is all the data in the zip files have been organized to stay prefetched (like for sounds).
			// We want it persistent so we know after a while it will always stay there, but it is a lower priority prefetch
			// as it may not be used before a while. (short term / non-persistent preftech have a more immediate use).
			bool bSucceeded = g_FiosConfiguration.PrefetchFile( args.Arg( 1 ), -128, true );
			if ( bSucceeded == false )
			{
				Warning( "Prefetch failed. Check if there are other prefetches going on with the command 'fs_fios_print_prefetches'.\n");
			}
		}
		else
		{
			Warning( "Incorrect parameter for the command 'fs_fios_prefetch_file'. Please use a file name (like \"/PS3_GAME/USRDIR/filename.bin\".\n" );
		}
	}
}

CON_COMMAND( fs_fios_prefetch_file_in_pack, "Prefetches a file in a pack: <portal2/models/container_ride/fineDebris_part5.ani>.\nThe preftech is medium priority and non-persistent.")
{
	if ( fs_fios_enabled.GetBool() )
	{
		if ( args.ArgC() == 2 )
		{
			// Higher priority and non-persistent for single file (we don't want the file to pollute the cache, it is just used for short term optimization, like in the next map).
			// But it has to be higher priority than the long term persistent prefetches as there is more chance the prefetch is going to help shortly.
			const char * pFileInPack = args.Arg( 1 );

			char packFileName[256];
			int64 nPosition = 0, nLength = 0;
			if ( g_pBaseFileSystem->GetPackFileInfoFromRelativePath( pFileInPack, "GAME", packFileName, sizeof(packFileName), nPosition, nLength ) )
			{
				const char * pPathCached = g_FiosConfiguration.GetPathCached();
				const int nPathCachedLength = strlen( pPathCached );
				if ( memcmp( packFileName, pPathCached, nPathCachedLength ) == 0 )
				{
					Msg( "Prefetching file: '%s' - Pack file: '%s' - Position: %lld - Size: %lld.\n", pFileInPack, packFileName, nPosition, nLength );
					// We have to skip the path cached as FIOS expects a path relative to that.
					// Priority is set higher than average, just to differentiate with persistent prefetches.
					bool bSucceeded = g_FiosConfiguration.PrefetchFile( packFileName + nPathCachedLength, -127, false, nPosition, nLength );
					if ( bSucceeded == false )
					{
						Warning( "Prefetch failed. Check if there are other prefetches going on with the command 'fs_fios_print_prefetches'.\n" );
					}
				}
				else
				{
					Warning( "Can't prefetch file '%s' in the pack file '%s' as the pack file is not a under the cached path '%s'.\n", pFileInPack, packFileName, pPathCached );
				}
			}
			else
			{
				Warning( "Can't find the corresponding pack file for '%s'.\n", pFileInPack );
			}
		}
		else
		{
			Warning( "Incorrect parameter for the command 'fs_fios_prefetch_file_in_pack'. Please use a file name (like \"portal2/models/container_ride/fineDebris_part5.ani\".\n" );
		}
	}
}

CON_COMMAND( fs_fios_cancel_prefetches, "Cancels all the prefetches in progress." )
{
	if ( fs_fios_enabled.GetBool() )
	{
		g_FiosConfiguration.CancelAllPrefetches();
	}
}

#else

// Fake commands for other platforms...
CON_COMMAND( fs_fios_flush_cache, "Flushes the FIOS HDD cache." )
{
}

CON_COMMAND( fs_fios_print_prefetches, "Displays all the prefetches currently in progress." )
{
}

CON_COMMAND( fs_fios_prefetch_file, "Prefetches a file: </PS3_GAME/USRDIR/filename.bin>.\nThe preftech is medium priority and persistent." )
{
}

CON_COMMAND( fs_fios_prefetch_file_in_pack, "Prefetches a file in a pack: <portal2/models/container_ride/fineDebris_part5.ani>.\nThe preftech is medium priority and non-persistent.")
{
}

CON_COMMAND( fs_fios_cancel_prefetches, "Cancels all the prefetches in progress." )
{
}

#endif




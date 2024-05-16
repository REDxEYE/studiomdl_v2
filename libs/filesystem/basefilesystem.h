//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef BASEFILESYSTEM_H
#define BASEFILESYSTEM_H

#include "tier0/platform.h"

#ifdef _PS3
#include <sdk_version.h>
#if CELL_SDK_VERSION >= 0x085007
#define getenv(x) NULL   //TEMP REMOVE THIS - RP
#endif
#endif
#ifdef _WIN32
#pragma once
#endif

#if defined( _WIN32 )

#if !defined( _X360 ) && !defined( _PS3 ) && !defined(LINUX)

#include <io.h>
#include <direct.h>

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#endif
#undef GetCurrentDirectory
#undef GetJob
#undef AddJob


#elif defined( POSIX ) && !defined( _PS3 )
#include <unistd.h> // unlink
#include "linux_support.h"
#define INVALID_HANDLE_VALUE (void *)-1

// undo the prepended "_" 's
#define _chmod chmod
#define _stat stat
#define _alloca alloca
#define _S_IFDIR S_IFDIR



#elif defined(_PS3)
#include <unistd.h> // unlink
#define INVALID_HANDLE_VALUE ( void * )-1

// undo the prepended "_" 's
#define _chmod chmod
#define _stat stat
#define _alloca alloca
#define _S_IFDIR S_IFDIR

#endif


#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <cstring>
#include "tier1/utldict.h"

#include <ctime>
#include "tier1/refcount.h"
#include "filesystem.h"
#include "tier1/utlvector.h"
#include "tier1/UtlStringMap.h"
#include <cstdarg>
#include "tier1/utlrbtree.h"
#include "tier1/utlsymbol.h"
#include "tier1/utllinkedlist.h"
#include "tier1/utlstring.h"
#include "tier1/utlsortvector.h"
#include "tier1/utldict.h"
#include "tier1/tier1.h"


#ifdef _WIN32
#define CORRECT_PATH_SEPARATOR '\\'
#define INCORRECT_PATH_SEPARATOR '/'
#elif defined(POSIX)
#define CORRECT_PATH_SEPARATOR '/'
#define INCORRECT_PATH_SEPARATOR '\\'
#endif

#ifdef    _WIN32
#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')
#elif defined(POSIX)
#define PATHSEPARATOR(c) ((c) == '/')
#endif    //_WIN32

#define MAX_FILEPATH 512

#if !defined( _X360 )
#define SUPPORT_IODELAY_MONITORING
#endif

#ifdef _PS3
#define FILE_ATTRIBUTE_DIRECTORY S_IFDIR

typedef struct 
{
    // public data
    int dwFileAttributes;
    char cFileName[PATH_MAX]; // the file name returned from the call

    int numMatches;
    struct dirent **namelist;
} FIND_DATA;

#define WIN32_FIND_DATA FIND_DATA

#endif // _PS3

extern CUtlSymbolTable g_PathIDTable;


enum FileMode_t {
    FM_BINARY,
    FM_TEXT
};

enum FileType_t {
    FT_NORMAL,
    FT_PACK_BINARY,
    FT_PACK_TEXT,
#if defined(_PS3)
    FT_RUNTIME_PS3,
#endif
};

#if defined(_PS3)
void FixUpPathCaseForPS3( const char* pFilePath );
#endif

class IThreadPool;

class CBlockingFileItemList;

class KeyValues;

class CCompiledKeyValuesReader;

class CBaseFileSystem;

class CPackFileHandle;

class CPackFile;

class IFileList;

class CFileOpenInfo;

class CWhitelistSpecs {
public:
    IFileList *m_pWantCRCList;
    IFileList *m_pAllowFromDiskList;
};

//-----------------------------------------------------------------------------

class CIODelayAlarmThread;

class CFileHandle {
public:
    CFileHandle(CBaseFileSystem *fs);

    virtual ~CFileHandle();

    void Init(CBaseFileSystem *fs);

    int GetSectorSize();

    bool IsOK();

    void Flush();

    void SetBufferSize(int nBytes);

    int Read(void *pBuffer, int nLength);

    int Read(void *pBuffer, int nDestSize, int nLength);

    int Write(const void *pBuffer, int nLength);

    int Seek(int64 nOffset, int nWhence);

    int Tell();

    int Size();

    int64 AbsoluteBaseOffset();

    bool EndOfFile();

    char *m_pszTrueFileName;

    char const *Name() const { return m_pszTrueFileName ? m_pszTrueFileName : ""; }

    void SetName(char const *pName) {
        Assert(pName);
        Assert(!m_pszTrueFileName);
        int len = Q_strlen(pName);
        m_pszTrueFileName = new char[len + 1];
        memcpy(m_pszTrueFileName, pName, len + 1);
    }

    CPackFileHandle *m_pPackFileHandle;
    int64 m_nLength;
    FileType_t m_type;
    FILE *m_pFile;

protected:
    CBaseFileSystem *m_fs;


    enum {
        MAGIC = 'CFHa',
        FREE_MAGIC = 'FreM'
    };
    unsigned int m_nMagic;

    bool IsValid();
};


class CFileLoadInfo {
public:
    bool m_bSteamCacheOnly;            // If Steam and this is true, then the file is only looked for in the Steam caches.
    bool m_bLoadedFromSteamCache;    // If Steam, this tells whether the file was loaded off disk or the Steam cache.
#ifdef _PS3
    Ps3FileType_t m_ps3Filetype;
#endif
};


abstract_class CBaseFileSystem : public CTier2AppSystem<IFileSystem> {
    friend class CFileHandle;

    friend class CFileOpenInfo;

    typedef CTier2AppSystem<IFileSystem> BaseClass;

public:
    CBaseFileSystem();

    ~CBaseFileSystem();

    // Methods of IAppSystem
    virtual void *QueryInterface(const char *pInterfaceName);

    virtual InitReturnVal_t Init();

    virtual void Shutdown();

    void ParsePathID(const char *&pFilename, const char *&pPathID, char tempPathID[MAX_PATH]);

    // file handling
    virtual FileHandle_t Open(const char *pFileName, const char *pOptions, const char *pathID);

    virtual FileHandle_t OpenEx(const char *pFileName, const char *pOptions, unsigned flags = 0, const char *pathID = 0,
                                char **ppszResolvedFilename = NULL);

    virtual void Close(FileHandle_t);

    virtual void Seek(FileHandle_t file, int pos, FileSystemSeek_t method);

    virtual unsigned int Tell(FileHandle_t file);

    virtual unsigned int Size(FileHandle_t file);

    virtual unsigned int Size(const char *pFileName, const char *pPathID);

    virtual void SetBufferSize(FileHandle_t file, unsigned nBytes);

    virtual bool IsOk(FileHandle_t file);

    virtual void Flush(FileHandle_t file);

    virtual bool Precache(const char *pFileName, const char *pPathID);

    virtual bool EndOfFile(FileHandle_t file);

    virtual int Read(void *pOutput, int size, FileHandle_t file);

    virtual int ReadEx(void *pOutput, int sizeDest, int size, FileHandle_t file);

    virtual int Write(void const *pInput, int size, FileHandle_t file);

    virtual char *ReadLine(char *pOutput, int maxChars, FileHandle_t file);

    virtual int FPrintf(FileHandle_t file, PRINTF_FORMAT_STRING const char *pFormat, ...) FMTFUNCTION(3, 4);

    // Reads/writes files to utlbuffers
    virtual bool ReadFile(const char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes, int nStartingByte,
                          FSAllocFunc_t pfnAlloc = NULL);

    virtual bool WriteFile(const char *pFileName, const char *pPath, CUtlBuffer &buf);

    virtual int
    ReadFileEx(const char *pFileName, const char *pPath, void **ppBuf, bool bNullTerminate, bool bOptimalAlloc,
               int nMaxBytes = 0, int nStartingByte = 0, FSAllocFunc_t pfnAlloc = NULL);

    virtual bool ReadToBuffer(FileHandle_t hFile, CUtlBuffer &buf, int nMaxBytes = 0, FSAllocFunc_t pfnAlloc = NULL);

    // Optimal buffer
    bool
    GetOptimalIOConstraints(FileHandle_t hFile, unsigned *pOffsetAlign, unsigned *pSizeAlign, unsigned *pBufferAlign);

    void *AllocOptimalReadBuffer(FileHandle_t hFile, unsigned nSize, unsigned nOffset) { return malloc(nSize); }

    void FreeOptimalReadBuffer(void *p) { free(p); }

    // Gets the current working directory
    virtual bool GetCurrentDirectory(char *pDirectory, int maxlen);

    // this isn't implementable on STEAM as is.
    virtual void CreateDirHierarchy(const char *path, const char *pathID);

    // returns true if the file is a directory
    virtual bool IsDirectory(const char *pFileName, const char *pathID);

    // path info
    virtual const char *GetLocalPath(const char *pFileName, char *pLocalPath, int localPathBufferSize);

    virtual bool FullPathToRelativePath(const char *pFullpath, char *pRelative, int maxlen);

    // removes a file from disk
    virtual void RemoveFile(char const *pRelativePath, const char *pathID);

    // Remove all search paths (including write path?)
    virtual void RemoveAllSearchPaths(void);

    // Purpose: Removes all search paths for a given pathID, such as all "GAME" paths.
    virtual void RemoveSearchPaths(const char *pathID);

    // STUFF FROM IFileSystem
    // Add paths in priority order (mod dir, game dir, ....)
    // Can also add pak files (errr, NOT YET!)
    void AddSearchPath(const char *pPath, const char *pathID, SearchPathAdd_t addType) override;

    bool RemoveSearchPath(const char *pPath, const char *pathID) override;

    void PrintSearchPaths() override;

    void MarkPathIDByRequestOnly(const char *pPathID, bool bRequestOnly) override;

    bool IsFileInReadOnlySearchPath(const char *pPath, const char *pathID = 0) override;

    bool FileExists(const char *pFileName, const char *pPathID = NULL) override;

    long GetFileTime(const char *pFileName, const char *pPathID = NULL) override;

    bool IsFileWritable(char const *pFileName, const char *pPathID = NULL) override;

    bool SetFileWritable(char const *pFileName, bool writable, const char *pPathID = 0) override;

    void FileTimeToString(char *pString, int maxChars, long fileTime) override;

    const char *FindFirst(const char *pWildCard, FileFindHandle_t *pHandle) override;

    const char *FindFirstEx(const char *pWildCard, const char *pPathID, FileFindHandle_t *pHandle) override;

    const char *FindNext(FileFindHandle_t handle) override;

    bool FindIsDirectory(FileFindHandle_t handle) override;

    void FindClose(FileFindHandle_t handle) override;

    void
    FindFileAbsoluteList(CUtlVector<CUtlString> &outAbsolutePathNames, const char *pWildCard,
                         const char *pPathID) override;

    void PrintOpenedFiles() override;

    void SetWarningFunc(void (*pfnWarning)(const char *fmt, ...)) override;

    void SetWarningLevel(FileWarningLevel_t level) override;

    void AddLoggingFunc(FileSystemLoggingFunc_t logFunc) override;

    void RemoveLoggingFunc(FileSystemLoggingFunc_t logFunc) override;

    bool RenameFile(char const *pOldPath, char const *pNewPath, const char *pathID) override;

    void GetLocalCopy(const char *pFileName) override;

    FileNameHandle_t FindOrAddFileName(char const *pFileName) override;

    FileNameHandle_t FindFileName(char const *pFileName) override;

    bool String(const FileNameHandle_t &handle, char *buf, int buflen) override;

    int GetPathIndex(const FileNameHandle_t &handle) override;

    long GetPathTime(const char *pFileName, const char *pPathID);

    void MarkAllCRCsUnverified() override;

    void CacheFileCRCs(const char *pPathname, ECacheCRCType eType, IFileList *pFilter) override;

    EFileCRCStatus CheckCachedFileHash(const char *pPathID, const char *pRelativeFilename, int nFileFraction,
                                       FileHash_t *pFileHash) override;

    int GetUnverifiedFileHashes(CUnverifiedFileHash *pFiles, int nMaxFiles) override;

    void InstallDirtyDiskReportFunc(FSDirtyDiskReportFunc_t func) override;


    // Returns the file system statistics retreived by the implementation.  Returns NULL if not supported.
    const FileSystemStatistics *GetFilesystemStatistics() override;

    // Load dlls
    CSysModule *LoadModule(const char *pFileName, const char *pPathID, bool bValidatedDllOnly) override;

    void UnloadModule(CSysModule *pModule) override;

    //--------------------------------------------------------
    // pack files
    //--------------------------------------------------------

    // converts a partial path into a full path
    // can be filtered to restrict path types and can provide info about resolved path
    const char *
    RelativePathToFullPath(const char *pFileName, const char *pPathID, char *pFullPath, int fullPathBufferSize,
                           PathTypeFilter_t pathFilter = FILTER_NONE, PathTypeQuery_t *pPathType = NULL) override;


    // Returns the search path, each path is separated by ;s. Returns the length of the string returned
    int GetSearchPath(const char *pathID, bool bGetPackFiles, char *pPath, int nMaxLen) override;

    int GetSearchPathID(char *pPath, int nMaxLen) override;


    bool GetFileTypeForFullPath(char const *pFullPath, wchar_t *buf, size_t bufSizeInBytes) override;

    bool FullPathToRelativePathEx(const char *pFullpath, const char *pPathId, char *pRelative, int maxlen) override;

    // If the "PreloadedData" hasn't been purged, then this'll try and instance the KeyValues using the fast path of compiled keyvalues loaded during startup.
    // Otherwise, it'll just fall through to the regular KeyValues loading routines
    KeyValues *LoadKeyValues(KeyValuesPreloadType_t type, char const *filename, char const *pPathID = 0) override;

    bool
    LoadKeyValues(KeyValues &head, KeyValuesPreloadType_t type, char const *filename, char const *pPathID = 0) override;

    bool FixupSearchPathsAfterInstall() override;

    FSDirtyDiskReportFunc_t GetDirtyDiskReportFunc() override { return m_DirtyDiskReportFunc; }


    void BuildExcludeList();

    bool GetStringFromKVPool(CRC32_t poolKey, unsigned int key, char *pOutBuff, int buflen) override;

    IIoStats *GetIoStats() override;

public:
    //------------------------------------
    // Synchronous path for file operations
    //------------------------------------
    class CPathIDInfo {
    public:
        const CUtlSymbol &GetPathID() const;

        const char *GetPathIDString() const;

        void SetPathID(CUtlSymbol sym);

    public:
        // See MarkPathIDByRequestOnly.
        bool m_bByRequestOnly=false;

    private:
        CUtlSymbol m_PathID;
    };

    ////////////////////////////////////////////////
    // IMPLEMENTATION DETAILS FOR CBaseFileSystem //
    ////////////////////////////////////////////////

    class CSearchPath {
    public:
        CSearchPath(void);

        ~CSearchPath(void);

        const char *GetPathString() const;

        // Path ID ("game", "mod", "gamebin") accessors.
        const CUtlSymbol &GetPathID() const;

        const char *GetPathIDString() const;

        // Search path (c:\hl2\hl2) accessors.
        void SetPath(CUtlSymbol id);

        const CUtlSymbol &GetPath() const;

        int m_storeId;

        // Used to track if its search
        CPathIDInfo *m_pPathIDInfo;

        bool m_bIsDvdDevPath;

    private:
        CUtlSymbol m_Path;
        const char *m_pDebugPath;
        CPackFile *m_pPackFile;
    public:
        bool m_bIsLocalizedPath;
    };

    static void MarkLocalizedPath(CSearchPath *sp);

    class CSearchPathsVisits {
    public:
        void Reset() {
            m_Visits.RemoveAll();
        }

        bool MarkVisit(const CSearchPath &searchPath) {
            if (m_Visits.Find(searchPath.m_storeId) == m_Visits.InvalidIndex()) {
                MEM_ALLOC_CREDIT();
                m_Visits.AddToTail(searchPath.m_storeId);
                return false;
            }
            return true;
        }

    private:
        CUtlVector<int> m_Visits;    // This is a copy of IDs for the search paths we've visited, so
    };

    class CSearchPathsIterator {
    public:
        CSearchPathsIterator(CBaseFileSystem *pFileSystem, const char **ppszFilename, const char *pszPathID,
                             PathTypeFilter_t pathTypeFilter = FILTER_NONE)
                : m_iCurrent(-1),
                  m_PathTypeFilter(pathTypeFilter),
                  m_bExcluded(false) {
            char tempPathID[MAX_PATH];
            if (*ppszFilename && (*ppszFilename)[0] == '/' &&
                (*ppszFilename)[1] == '/') // ONLY '//' (and not '\\') for our special format
            {
                // Allow for UNC-type syntax to specify the path ID.
                pFileSystem->ParsePathID(*ppszFilename, pszPathID, tempPathID);
            }
            if (pszPathID) {
                m_pathID = g_PathIDTable.AddString(pszPathID);
            } else {
                m_pathID = UTL_INVAL_SYMBOL;
            }

            if (*ppszFilename && !Q_IsAbsolutePath(*ppszFilename)) {
                // Copy paths to minimize mutex lock time
                CopySearchPaths(pFileSystem->m_SearchPaths);
                V_strncpy(m_Filename, *ppszFilename, sizeof(m_Filename));
                V_FixSlashes(m_Filename);
            } else {
                // If it's an absolute path, it isn't worth using the paths at all. Simplify
                // client logic by pretending there's a search path of 1
                m_EmptyPathIDInfo.m_bByRequestOnly = false;
                m_EmptySearchPath.m_pPathIDInfo = &m_EmptyPathIDInfo;
                m_EmptySearchPath.SetPath(m_pathID);
                m_EmptySearchPath.m_storeId = -1;
                m_Filename[0] = '\0';
            }
        }

        CSearchPathsIterator(CBaseFileSystem *pFileSystem, const char *pszPathID,
                             PathTypeFilter_t pathTypeFilter = FILTER_NONE)
                : m_iCurrent(-1),
                  m_PathTypeFilter(pathTypeFilter),
                  m_bExcluded(false) {
            if (pszPathID) {
                m_pathID = g_PathIDTable.AddString(pszPathID);
            } else {
                m_pathID = UTL_INVAL_SYMBOL;
            }
            // Copy paths to minimize mutex lock time
            CopySearchPaths(pFileSystem->m_SearchPaths);
            m_Filename[0] = '\0';
        }

        CSearchPath *GetFirst();

        CSearchPath *GetNext();

    private:
        CSearchPathsIterator(const CSearchPathsIterator &);

        void operator=(const CSearchPathsIterator &);

        void CopySearchPaths(const CUtlVector<CSearchPath> &searchPaths) {
            m_SearchPaths = searchPaths;
        }

        int m_iCurrent;
        CUtlSymbol m_pathID;
        CUtlVector<CSearchPath> m_SearchPaths;
        CSearchPathsVisits m_visits;
        CSearchPath m_EmptySearchPath;
        CPathIDInfo m_EmptyPathIDInfo;
        PathTypeFilter_t m_PathTypeFilter;
        char m_Filename[MAX_PATH];    // set for relative names only
        bool m_bExcluded;
    };

    friend class CSearchPathsIterator;

    struct FindData_t {
        WIN32_FIND_DATA findData;
        int currentSearchPathID;
        CUtlVector<char> wildCardString;
        HANDLE findHandle;
        CSearchPathsVisits m_VisitedSearchPaths;    // This is a copy of IDs for the search paths we've visited, so avoids searching duplicate paths.
        int m_CurrentStoreID;        // CSearchPath::m_storeId of the current search path.

        CUtlSymbol m_FilterPathID;            // What path ID are we looking at? Ignore all others. (Only set by FindFirstEx).

        CUtlDict<int, int> m_VisitedFiles;            // We go through the search paths in priority order, and we use this to make sure
        // that we don't return the same file more than once.
    };

    friend class CSearchPath;

    // logging functions
    CUtlVector<FileSystemLoggingFunc_t> m_LogFuncs;

    CUtlVector<CSearchPath> m_SearchPaths;
    CUtlVector<CPathIDInfo *> m_PathIDInfos;
    CUtlLinkedList<FindData_t> m_FindData;

    int m_iMapLoad;

    // Global list of pack file handles
    CUtlVector<CPackFile *> m_ZipFiles;

    FILE *m_pLogFile;
    bool m_bOutputDebugString;

    IThreadPool *m_pThreadPool;

    // Statistics:
    FileSystemStatistics m_Stats;

#ifdef SUPPORT_IODELAY_MONITORING
    float m_flDelayLimit;
    float m_flLastIOTime;


    FORCEINLINE void NoteIO() {
        if (m_pDelayThread) {
            m_flLastIOTime = Plat_FloatTime();
        }
    }

    CIODelayAlarmThread *m_pDelayThread;

#else
    FORCEINLINE void NoteIO( void )
    {
    }
#endif


#if defined( TRACK_BLOCKING_IO )
    CBlockingFileItemList	*m_pBlockingItems;
    bool					m_bBlockingFileAccessReportingEnabled;
    bool					m_bAllowSynchronousLogging;

    friend class			CBlockingFileItemList;
    friend class			CAutoBlockReporter;
#endif

//	CFileTracker2	m_FileTracker2;

protected:
    //----------------------------------------------------------------------------
    // Purpose: Functions implementing basic file system behavior.
    //----------------------------------------------------------------------------
    virtual FILE *
    FS_fopen(const char *filename, const char *options, unsigned flags, int64 *size, CFileLoadInfo *pInfo) = 0;

    virtual void FS_setbufsize(FILE *fp, unsigned nBytes) = 0;

    virtual void FS_fclose(FILE *fp) = 0;

    virtual void FS_fseek(FILE *fp, int64 pos, int seekType) = 0;

    virtual long FS_ftell(FILE *fp) = 0;

    virtual int FS_feof(FILE *fp) = 0;

    size_t FS_fread(void *dest, size_t size, FILE *fp) { return FS_fread(dest, (size_t) -1, size, fp); }

    virtual size_t FS_fread(void *dest, size_t destSize, size_t size, FILE *fp) = 0;

    virtual size_t FS_fwrite(const void *src, size_t size, FILE *fp) = 0;

    virtual bool FS_setmode(FILE *fp, FileMode_t mode) { return false; }

    virtual size_t FS_vfprintf(FILE *fp, const char *fmt, va_list list) = 0;

    virtual int FS_ferror(FILE *fp) = 0;

    virtual int FS_fflush(FILE *fp) = 0;

    virtual char *FS_fgets(char *dest, int destSize, FILE *fp) = 0;

    virtual int FS_stat(const char *path, struct _stat *buf) = 0;

    virtual int FS_chmod(const char *path, int pmode) = 0;

    virtual HANDLE FS_FindFirstFile(const char *findname, WIN32_FIND_DATA *dat) = 0;

    virtual bool FS_FindNextFile(HANDLE handle, WIN32_FIND_DATA *dat) = 0;

    virtual bool FS_FindClose(HANDLE handle) = 0;

    virtual int FS_GetSectorSize(FILE *) { return 1; }


    void GetFileNameForHandle(FileHandle_t handle, char *buf, size_t buflen);

protected:
    //-----------------------------------------------------------------------------
    // Purpose: For tracking unclosed files
    // NOTE:  The symbol table could take up memory that we don't want to eat here.
    // In that case, we shouldn't store them in a table, or we should store them as locally allocates stings
    //  so we can control the size
    //-----------------------------------------------------------------------------
    class COpenedFile {
    public:
        COpenedFile(void);

        ~COpenedFile(void);

        COpenedFile(const COpenedFile &src);

        bool operator==(const COpenedFile &src) const;

        void SetName(char const *name);

        char const *GetName(void);

        FILE *m_pFile;
        char *m_pName;
    };

    //CUtlRBTree< COpenedFile, int > m_OpenedFiles;
    CUtlVector<COpenedFile> m_OpenedFiles;
    CUtlStringMap<bool> m_NonexistingFilesExtensions;
#ifdef NONEXISTING_FILES_CACHE_SUPPORT
    CUtlStringMap< double >		m_NonexistingFilesCache;
#endif

    static bool OpenedFileLessFunc(COpenedFile const &src1, COpenedFile const &src2);

    FileWarningLevel_t m_fwLevel;

    void (*m_pfnWarning)(const char *fmt, ...);

    FILE *
    Trace_FOpen(const char *filename, const char *options, unsigned flags, int64 *size, CFileLoadInfo *pInfo = NULL);

    void Trace_FClose(FILE *fp);

    void Trace_FRead(int size, FILE *file);

    void Trace_FWrite(int size, FILE *file);

    void Trace_DumpUnclosedFiles(void);

public:
    void LogAccessToFile(char const *accesstype, char const *fullpath, char const *options);

    void FileSystemWarning(FileWarningLevel_t level, const char *fmt, ...);

protected:
    // Note: if pFoundStoreID is passed in, then it will set that to the CSearchPath::m_storeId value of the search path it found the file in.
    const char *
    FindFirstHelper(const char *pWildCard, const char *pPathID, FileFindHandle_t *pHandle, int *pFoundStoreID);

    bool FindNextFileHelper(FindData_t *pFindData, int *pFoundStoreID);

    void FindFileAbsoluteListHelper(CUtlVector<CUtlString> &outAbsolutePathNames, FindData_t &findData,
                                    const char *pAbsoluteFindName);


    // Goes through all the search paths (or just the one specified) and calls FindFile on them. Returns the first successful result, if any.
    FileHandle_t FindFileInSearchPaths(const char *pFileName, const char *pOptions, const char *pathID, unsigned flags,
                                       char **ppszResolvedFilename = NULL, bool bTrackCRCs = false);

    void HandleOpenRegularFile(CFileOpenInfo &openInfo, bool bIsAbsolutePath);

    FileHandle_t FindFile(const CSearchPath *path, const char *pFileName, const char *pOptions, unsigned flags,
                          char **ppszResolvedFilename = NULL, bool bTrackCRCs = false);

    int FastFindFile(const CSearchPath *path, const char *pFileName);

    long FastFileTime(const CSearchPath *path, const char *pFileName);

    const char *GetWritePath(const char *pFilename, const char *pathID);

    // Computes a full write path
    void ComputeFullWritePath(char *pDest, int maxlen, const char *pWritePathID, char const *pRelativePath);

    void
    AddSearchPathInternal(const char *pPath, const char *pathID, SearchPathAdd_t addType, int iForceInsertIndex = 0);

    // Opens a file for read or write
    FileHandle_t OpenForRead(const char *pFileName, const char *pOptions, unsigned flags, const char *pathID,
                             char **ppszResolvedFilename = NULL);

    FileHandle_t OpenForWrite(const char *pFileName, const char *pOptions, const char *pathID);

    CSearchPath *FindWritePath(const char *pFilename, const char *pathID);

    // Helper function for fs_log file logging
    void LogFileAccess(const char *pFullFileName);

#if IsPlatformPS3()
    virtual bool PrefetchFile( const char *pFileName, int nPriority, bool bPersist );
    virtual bool PrefetchFile( const char *pFileName, int nPriority, bool bPersist, int64 nOffset, int64 nSize );
    virtual void FlushCache();
    virtual void SuspendPrefetches( const char *pWhy );
    virtual void ResumePrefetches( const char *pWhy );
    virtual void OnSaveStateChanged( bool bSaving );
    virtual bool IsPrefetchingDone();
#endif

    bool LookupKeyValuesRootKeyName(char const *filename, char const *pPathID, char *rootName, size_t bufsize);

    // If bByRequestOnly is -1, then it will default to false if it doesn't already exist, and it
    // won't change it if it does already exist. Otherwise, it will be set to the value of bByRequestOnly.
    CPathIDInfo *FindOrAddPathIDInfo(const CUtlSymbol &id, int bByRequestOnly);

    static bool FilterByPathID(const CSearchPath *pSearchPath, const CUtlSymbol &pathID);

    // Global/shared filename/path table
    CUtlFilenameSymbolTable m_FileNames;

    // This manages most of the info we use for pure servers (whether files came from Steam caches or off-disk, their CRCs, which ones are unverified, etc).
    FSDirtyDiskReportFunc_t m_DirtyDiskReportFunc;

    static CUtlSymbol m_GamePathID;
    static CUtlSymbol m_BSPPathID;

    static bool m_bSearchPathsPatchedAfterInstall;

    // Pack exclude paths are strictly for 360 to allow holes in search paths and pack files
    // which fall through to support new or dynamic data on the host pc.
    struct ExcludeFilePath_t {
        FileNameHandle_t m_hName;
        FileNameHandle_t m_hFixedName;
    };
    static CUtlVector<FileNameHandle_t> m_ExcludeFilePaths;

};

inline const CUtlSymbol &CBaseFileSystem::CPathIDInfo::GetPathID() const {
    return m_PathID;
}


inline const char *CBaseFileSystem::CPathIDInfo::GetPathIDString() const {
    return g_PathIDTable.String(m_PathID);
}


inline const char *CBaseFileSystem::CSearchPath::GetPathString() const {
    return g_PathIDTable.String(m_Path);
}


inline void CBaseFileSystem::CPathIDInfo::SetPathID(CUtlSymbol sym) {
    m_PathID = sym;
}


inline const CUtlSymbol &CBaseFileSystem::CSearchPath::GetPathID() const {
    return m_pPathIDInfo->GetPathID();
}


inline const char *CBaseFileSystem::CSearchPath::GetPathIDString() const {
    return m_pPathIDInfo->GetPathIDString();
}


inline void CBaseFileSystem::CSearchPath::SetPath(CUtlSymbol id) {
    m_Path = id;
    m_pDebugPath = g_PathIDTable.String(m_Path);
    MarkLocalizedPath(this);
}


inline const CUtlSymbol &CBaseFileSystem::CSearchPath::GetPath() const {
    return m_Path;
}


inline bool CBaseFileSystem::FilterByPathID(const CSearchPath *pSearchPath, const CUtlSymbol &pathID) {
    if ((UtlSymId_t) pathID == UTL_INVAL_SYMBOL) {
        // They didn't specify a specific search path, so if this search path's path ID is by
        // request only, then ignore it.
        return pSearchPath->m_pPathIDInfo->m_bByRequestOnly;
    } else {
        // Bit of a hack, but specifying "BSP" as the search path will search in "GAME" for only the map/.bsp pack file path
        if (pathID == m_BSPPathID) {
            if (pSearchPath->GetPathID() != m_GamePathID)
                return true;

            return false;
        } else {
            return (pSearchPath->GetPathID() != pathID);
        }
    }
}


#if defined( TRACK_BLOCKING_IO )

class CAutoBlockReporter
{
public:

    CAutoBlockReporter( CBaseFileSystem *fs, bool synchronous, char const *filename, int eBlockType, int nTypeOfAccess ) :
        m_pFS( fs ),
        m_Item( eBlockType, filename, 0.0f, nTypeOfAccess ),
        m_bSynchronous( synchronous )
    {
        Assert( m_pFS );
        m_Timer.Start();
    }

    CAutoBlockReporter( CBaseFileSystem *fs, bool synchronous, FileHandle_t handle, int eBlockType, int nTypeOfAccess ) :
        m_pFS( fs ),
        m_Item( eBlockType, NULL, 0.0f, nTypeOfAccess ),
        m_bSynchronous( synchronous )
    {
        Assert( m_pFS );
        char name[ 512 ];
        m_pFS->GetFileNameForHandle( handle, name, sizeof( name ) );
        m_Item.SetFileName( name );
        m_Timer.Start();
    }

    ~CAutoBlockReporter()
    {
        m_Timer.End();
        m_Item.m_flElapsed = m_Timer.GetDuration().GetSeconds();
        m_pFS->RecordBlockingFileAccess( m_bSynchronous, m_Item );
    }

private:

    CBaseFileSystem		*m_pFS;

    CFastTimer			m_Timer;
    FileBlockingItem	m_Item;
    bool				m_bSynchronous;
};

#define AUTOBLOCKREPORTER_FN( name, fs, sync, filename, blockType, accessType )		CAutoBlockReporter block##name( fs, sync, filename, blockType, accessType );
#define AUTOBLOCKREPORTER_FH( name, fs, sync, handle, blockType, accessType )		CAutoBlockReporter block##name( fs, sync, handle, blockType, accessType );

#else

#define AUTOBLOCKREPORTER_FN(name, fs, sync, filename, blockType, accessType)    // Nothing
#define AUTOBLOCKREPORTER_FH(name, fs, sync, handle, blockType, accessType)    // Nothing

#endif

// singleton accessor
CBaseFileSystem *BaseFileSystem();

//#include "tier0/memdbgoff.h"

#endif // BASEFILESYSTEM_H

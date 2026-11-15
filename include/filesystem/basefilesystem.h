//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#pragma once

#include "tier0/platform.h"



#include "filesystem/stdfile.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "tier1_v2/string_utils.hpp"

#include "filesystem.h"
#include "tier1/utlvector.h"
#include "tier1/UtlStringMap.h"
#include "tier1/utlsymbol.h"
#include "tier1/utllinkedlist.h"
#include "tier1/utlstring.h"


#ifdef _WIN32
#define CORRECT_PATH_SEPARATOR '\\'
#define INCORRECT_PATH_SEPARATOR '/'
#elif defined(POSIX)
#define CORRECT_PATH_SEPARATOR '/'
#define INCORRECT_PATH_SEPARATOR '\\'
#endif

#define PATHSEPARATOR(c) ((c) == '\\' || (c) == '/')

#define MAX_FILEPATH 512

#define SUPPORT_IODELAY_MONITORING



enum FileMode_t {
    FM_BINARY,
    FM_TEXT
};

enum FileType_t {
    FT_NORMAL,
    FT_PACK_BINARY,
    FT_PACK_TEXT,
};


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

    bool IsOK();

    void Flush();

    int Read(void *pBuffer, int nLength);

    int Read(void *pBuffer, int nDestSize, int nLength);

    int Write(const void *pBuffer, int nLength);

    int Seek(int64_t nOffset, int nWhence);

    int Tell();

    int Size();

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
    int64_t m_nLength;
    FileType_t m_type;
    CStdFile *m_pFile;

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
};


class CBaseFileSystem : public IFileSystem {
    friend class CFileHandle;

    friend class CFileOpenInfo;

public:
    CBaseFileSystem();

    ~CBaseFileSystem();

    // Never called: Valve's app framework never registered the filesystem as an app system, so these did not run
    // before the interface system was removed either (path IDs "BSP"/"GAME", fs_debug).
    bool Init();

    // UNC-like "//pathid/file" syntax: strips the prefix from the file name and overrides the path ID ("*" = any)
    void ParsePathID(std::string_view &fileName, PathID &pathID);

    // Same, for NUL-terminated names (pFilename stays NUL-terminated: it is advanced past the prefix)
    void ParsePathID(const char *&pFilename, PathID &pathID);

    // file handling
    FileHandle_t Open(std::string_view fileName, std::string_view options, PathID pathID) override;

    FileHandle_t OpenEx(std::string_view fileName, std::string_view options, unsigned flags = 0, PathID pathID = PathID::Unspecified,
                        char **ppszResolvedFilename = nullptr) override;

    void Close(FileHandle_t) override;

    void Seek(FileHandle_t file, int pos, FileSystemSeek_t method) override;

    unsigned int Tell(FileHandle_t file) override;

    unsigned int Size(FileHandle_t file) override;

    unsigned int Size(const char *pFileName, PathID pathID) override;

    bool IsOk(FileHandle_t file) override;

    void Flush(FileHandle_t file) override;

    bool Precache(const char *pFileName, PathID pathID) override;

    bool EndOfFile(FileHandle_t file) override;

    int Read(void *pOutput, size_t size, FileHandle_t file) override;

    int ReadEx(void *pOutput, int sizeDest, int size, FileHandle_t file) override;

    int Write(void const *pInput, size_t size, FileHandle_t file) override;

    char *ReadLine(char *pOutput, int maxChars, FileHandle_t file) override;

    int FPrintf(FileHandle_t file, PRINTF_FORMAT_STRING const char *pFormat, ...) override FMTFUNCTION(3, 4);

    // Reads/writes files to utlbuffers
    bool ReadFile(const char *pFileName, PathID pathID, CUtlBuffer &buf, int nMaxBytes, int nStartingByte,
                  FSAllocFunc_t pfnAlloc = nullptr) override;

    bool WriteFile(const char *pFileName, PathID pathID, CUtlBuffer &buf) override;

    int
    ReadFileEx(const char *pFileName, PathID pathID, void **ppBuf, bool bNullTerminate, bool bOptimalAlloc,
               int nMaxBytes = 0, int nStartingByte = 0, FSAllocFunc_t pfnAlloc = nullptr) override;

    bool ReadToBuffer(FileHandle_t hFile, CUtlBuffer &buf, int nMaxBytes = 0, FSAllocFunc_t pfnAlloc = nullptr) override;

    // Optimal buffer
    bool
    GetOptimalIOConstraints(FileHandle_t hFile, unsigned *pOffsetAlign, unsigned *pSizeAlign, unsigned *pBufferAlign) override;

    void *AllocOptimalReadBuffer(FileHandle_t hFile, unsigned nSize, unsigned nOffset) override { return malloc(nSize); }

    void FreeOptimalReadBuffer(void *p) override { free(p); }

    // Gets the current working directory
    bool GetCurrentDirectory(char *pDirectory, int maxlen) override;

    // this isn't implementable on STEAM as is.
    void CreateDirHierarchy(const char *path, PathID pathID) override;

    // returns true if the file is a directory
    bool IsDirectory(const char *pFileName, PathID pathID) override;

    // path info
    const char *GetLocalPath(const char *pFileName, char *pLocalPath, int localPathBufferSize) override;

    bool FullPathToRelativePath(const char *pFullpath, char *pRelative, int maxlen) override;

    // removes a file from disk
    void RemoveFile(char const *pRelativePath, PathID pathID) override;

    // Remove all search paths (including write path?)
    void RemoveAllSearchPaths(void) override;

    // Purpose: Removes all search paths for a given pathID, such as all "GAME" paths.
    void RemoveSearchPaths(PathID pathID) override;

    // STUFF FROM IFileSystem
    // Add paths in priority order (mod dir, game dir, ....)
    // Can also add pak files (errr, NOT YET!)
    void AddSearchPath(std::string_view pPath, PathID pathID, SearchPathAdd_t addType) override;

    bool RemoveSearchPath(const char *pPath, PathID pathID) override;

    void PrintSearchPaths() override;

    void MarkPathIDByRequestOnly(PathID pathID, bool bRequestOnly) override;

    bool IsFileInReadOnlySearchPath(const char *pPath, PathID pathID = PathID::Unspecified) override;

    bool FileExists(const char *pFileName, PathID pathID = PathID::Unspecified) override;

    int64_t GetFileTime(const char *pFileName, PathID pathID = PathID::Unspecified) override;

    bool IsFileWritable(char const *pFileName, PathID pathID = PathID::Unspecified) override;

    bool SetFileWritable(char const *pFileName, bool writable, PathID pathID = PathID::Unspecified) override;

    void FileTimeToString(char *pString, int maxChars, int64_t fileTime) override;

    const char *FindFirst(const char *pWildCard, FileFindHandle_t *pHandle) override;

    const char *FindFirstEx(const char *pWildCard, PathID pathID, FileFindHandle_t *pHandle) override;

    const char *FindNext(FileFindHandle_t handle) override;

    bool FindIsDirectory(FileFindHandle_t handle) override;

    void FindClose(FileFindHandle_t handle) override;

    void
    FindFileAbsoluteList(CUtlVector<CUtlString> &outAbsolutePathNames, const char *pWildCard,
                         PathID pathID) override;

    void SetWarningFunc(void (*pfnWarning)(const char *fmt, ...)) override;

    void SetWarningLevel(FileWarningLevel_t level) override;

    void AddLoggingFunc(FileSystemLoggingFunc_t logFunc) override;

    void RemoveLoggingFunc(FileSystemLoggingFunc_t logFunc) override;

    bool RenameFile(char const *pOldPath, char const *pNewPath, PathID pathID) override;

    void GetLocalCopy(const char *pFileName) override;

    FileNameHandle_t FindOrAddFileName(char const *pFileName) override;

    FileNameHandle_t FindFileName(char const *pFileName) override;

    bool String(const FileNameHandle_t &handle, char *buf, int buflen) override;

    int GetPathIndex(const FileNameHandle_t &handle) override;

    int64_t GetPathTime(const char *pFileName, PathID pathID) override;

    void MarkAllCRCsUnverified() override;

    void CacheFileCRCs(const char *pPathname, ECacheCRCType eType, IFileList *pFilter) override;

    EFileCRCStatus CheckCachedFileHash(PathID pathID, const char *pRelativeFilename, int nFileFraction,
                                       FileHash_t *pFileHash) override;

    int GetUnverifiedFileHashes(CUnverifiedFileHash *pFiles, int nMaxFiles) override;

    void InstallDirtyDiskReportFunc(FSDirtyDiskReportFunc_t func) override;


    // Returns the file system statistics retreived by the implementation.  Returns NULL if not supported.
    const FileSystemStatistics *GetFilesystemStatistics() override;

    //--------------------------------------------------------
    // pack files
    //--------------------------------------------------------

    // converts a partial path into a full path
    // can be filtered to restrict path types and can provide info about resolved path
    const char *
    RelativePathToFullPath(const char *pFileName, PathID pathID, char *pFullPath, int fullPathBufferSize,
                           PathTypeFilter_t pathFilter = FILTER_NONE, PathTypeQuery_t *pPathType = nullptr) override;


    // Returns the search path, each path is separated by ;s. Returns the length of the string returned
    int GetSearchPath(PathID pathID, bool bGetPackFiles, char *pPath, int nMaxLen) override;

    int GetSearchPathID(char *pPath, int nMaxLen) override;


    bool GetFileTypeForFullPath(char const *pFullPath, wchar_t *buf, size_t bufSizeInBytes) override;

    bool FullPathToRelativePathEx(const char *pFullpath, PathID pathID, char *pRelative, int maxlen) override;

    // If the "PreloadedData" hasn't been purged, then this'll try and instance the KeyValues using the fast path of compiled keyvalues loaded during startup.
    // Otherwise, it'll just fall through to the regular KeyValues loading routines
    KeyValues *LoadKeyValues(KeyValuesPreloadType_t type, char const *filename, PathID pathID = PathID::Unspecified) override;

    bool
    LoadKeyValues(KeyValues &head, KeyValuesPreloadType_t type, char const *filename, PathID pathID = PathID::Unspecified) override;

    bool FixupSearchPathsAfterInstall() override;

    FSDirtyDiskReportFunc_t GetDirtyDiskReportFunc() override { return m_DirtyDiskReportFunc; }


    bool GetStringFromKVPool(CRC32_t poolKey, unsigned int key, char *pOutBuff, int buflen) override;

    IIoStats *GetIoStats() override;

public:
    //------------------------------------
    // Synchronous path for file operations
    //------------------------------------
    ////////////////////////////////////////////////
    // IMPLEMENTATION DETAILS FOR CBaseFileSystem //
    ////////////////////////////////////////////////

    class CSearchPath {
    public:
        const char *GetPathString() const { return m_Path.c_str(); }

        // Search path (c:\hl2\hl2), with a trailing separator
        const std::string &GetPath() const { return m_Path; }

        void SetPath(std::string path) { m_Path = std::move(path); }

        PathID m_PathID = PathID::Unspecified;

        int m_storeId = 0;

        bool m_bIsDvdDevPath = false;
        bool m_bIsLocalizedPath = false;

    private:
        std::string m_Path;
    };

    static void MarkLocalizedPath(CSearchPath *sp);

    class CSearchPathsVisits {
    public:
        void Reset() {
            m_Visits.RemoveAll();
        }

        bool MarkVisit(const CSearchPath &searchPath) {
            if (m_Visits.Find(searchPath.m_storeId) == m_Visits.InvalidIndex()) {
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
        CSearchPathsIterator(CBaseFileSystem *pFileSystem, const char **ppszFilename, PathID pathID,
                             PathTypeFilter_t pathTypeFilter = FILTER_NONE)
                : m_pFileSystem(pFileSystem),
                  m_iCurrent(-1),
                  m_PathTypeFilter(pathTypeFilter),
                  m_bExcluded(false) {
            if (*ppszFilename && (*ppszFilename)[0] == '/' &&
                (*ppszFilename)[1] == '/') // ONLY '//' (and not '\\') for our special format
            {
                // Allow for UNC-type syntax to specify the path ID. The view stays a suffix of the NUL-terminated name.
                std::string_view fileName(*ppszFilename);
                pFileSystem->ParsePathID(fileName, pathID);
                *ppszFilename = fileName.data();
            }
            m_pathID = pathID;

            if (*ppszFilename && !Q_IsAbsolutePath(*ppszFilename)) {
                // Copy paths to minimize mutex lock time
                CopySearchPaths(pFileSystem->m_SearchPaths);
                V_strncpy(m_Filename, *ppszFilename, sizeof(m_Filename));
                V_FixSlashes(m_Filename);
            } else {
                // If it's an absolute path, it isn't worth using the paths at all. Simplify
                // client logic by pretending there's a search path of 1
                m_EmptySearchPath.m_storeId = -1;
                m_Filename[0] = '\0';
            }
        }

        CSearchPathsIterator(CBaseFileSystem *pFileSystem, PathID pathID,
                             PathTypeFilter_t pathTypeFilter = FILTER_NONE)
                : m_pFileSystem(pFileSystem),
                  m_iCurrent(-1),
                  m_pathID(pathID),
                  m_PathTypeFilter(pathTypeFilter),
                  m_bExcluded(false) {
            // Copy paths to minimize mutex lock time
            CopySearchPaths(pFileSystem->m_SearchPaths);
            m_Filename[0] = '\0';
        }

        CSearchPathsIterator(const CSearchPathsIterator &) = delete;

        CSearchPathsIterator &operator=(const CSearchPathsIterator &) = delete;

        CSearchPath *GetFirst();

        CSearchPath *GetNext();

    private:
        void CopySearchPaths(const std::vector<CSearchPath> &searchPaths) {
            m_SearchPaths = searchPaths;
        }

        const CBaseFileSystem *m_pFileSystem;
        int m_iCurrent;
        PathID m_pathID = PathID::Unspecified;
        std::vector<CSearchPath> m_SearchPaths;
        CSearchPathsVisits m_visits;
        CSearchPath m_EmptySearchPath;
        PathTypeFilter_t m_PathTypeFilter;
        char m_Filename[MAX_PATH]{0};    // set for relative names only
        bool m_bExcluded;
    };

    friend class CSearchPathsIterator;

    struct FindData_t {
        StdFindData_t findData;
        int currentSearchPathID;
        CUtlVector<char> wildCardString;
        CStdFileFind *findHandle;
        CSearchPathsVisits m_VisitedSearchPaths;    // This is a copy of IDs for the search paths we've visited, so avoids searching duplicate paths.
        int m_CurrentStoreID;        // CSearchPath::m_storeId of the current search path.

        PathID m_FilterPathID = PathID::Unspecified;            // What path ID are we looking at? Ignore all others. (Only set by FindFirstEx).

        std::set<std::string, vstd::iless> m_VisitedFiles;            // We go through the search paths in priority order, and we use this to make sure
        // that we don't return the same file more than once.
    };

    friend class CSearchPath;

    // logging functions
    CUtlVector<FileSystemLoggingFunc_t> m_LogFuncs;

    // std::vector: CSearchPath owns a std::string, which CUtlVector's memmove-based shifting can't relocate
    std::vector<CSearchPath> m_SearchPaths;
    // See MarkPathIDByRequestOnly: unspecified lookups skip these path IDs
    std::array<bool, static_cast<size_t>(PathID::Count)> m_PathIDByRequestOnly{};
    CUtlLinkedList<FindData_t> m_FindData;

    int m_iMapLoad;

    // Global list of pack file handles
    CUtlVector<CPackFile *> m_ZipFiles;

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
    void GetFileNameForHandle(FileHandle_t handle, char *buf, size_t buflen);

protected:
    CUtlStringMap<bool> m_NonexistingFilesExtensions;

    FileWarningLevel_t m_fwLevel;

    void (*m_pfnWarning)(const char *fmt, ...);

    CStdFile *FOpen(const char *filename, const char *options, unsigned flags, int64_t *size,
                    CFileLoadInfo *pInfo = nullptr);

    void FClose(CStdFile *fp);

public:
    void LogAccessToFile(char const *accesstype, char const *fullpath, char const *options);

    void FileSystemWarning(FileWarningLevel_t level, const char *fmt, ...);

protected:
    // Note: if pFoundStoreID is passed in, then it will set that to the CSearchPath::m_storeId value of the search path it found the file in.
    const char *
    FindFirstHelper(const char *pWildCard, PathID pathID, FileFindHandle_t *pHandle, int *pFoundStoreID);

    bool FindNextFileHelper(FindData_t *pFindData, int *pFoundStoreID);

    void FindFileAbsoluteListHelper(CUtlVector<CUtlString> &outAbsolutePathNames, FindData_t &findData,
                                    const char *pAbsoluteFindName);


    // Goes through all the search paths (or just the one specified) and calls FindFile on them. Returns the first successful result, if any.
    FileHandle_t FindFileInSearchPaths(const std::string &fileName, const std::string &options, PathID pathID, unsigned flags,
                                       char **ppszResolvedFilename = nullptr, bool bTrackCRCs = false);

    void HandleOpenRegularFile(CFileOpenInfo &openInfo, bool bIsAbsolutePath);

    FileHandle_t FindFile(const CSearchPath *path, std::string_view fileName, const std::string &options, unsigned flags,
                          char **ppszResolvedFilename = nullptr, bool bTrackCRCs = false);

    int FastFindFile(const CSearchPath *path, const char *pFileName);

    int64_t FastFileTime(const CSearchPath *path, const char *pFileName);

    // Directory (with trailing separator) to write relative names to for the path ID
    const std::string &GetWritePath(PathID pathID);

    // Write directory + relative path, slashes fixed
    std::string ComputeFullWritePath(std::string_view relativePath, PathID writePathID);

    void
    AddSearchPathInternal(std::string_view pPath, PathID pathID, SearchPathAdd_t addType, int iForceInsertIndex = 0);

    // Opens a file for read or write
    FileHandle_t OpenForRead(const std::string &fileName, const std::string &options, unsigned flags, PathID pathID,
                             char **ppszResolvedFilename = nullptr);

    FileHandle_t OpenForWrite(const std::string &fileName, const std::string &options, PathID pathID);

    // First search path tagged pathID (any path for Unspecified)
    CSearchPath *FindWritePath(PathID pathID);


    bool LookupKeyValuesRootKeyName(char const *filename, PathID pathID, char *rootName, size_t bufsize);

    // True if the search path must be skipped for a lookup with the given path ID
    bool FilterByPathID(const CSearchPath &searchPath, PathID pathID) const;

    // Global/shared filename/path table
    CUtlFilenameSymbolTable m_FileNames;

    // This manages most of the info we use for pure servers (whether files came from Steam caches or off-disk, their CRCs, which ones are unverified, etc).
    FSDirtyDiskReportFunc_t m_DirtyDiskReportFunc;


    static bool m_bSearchPathsPatchedAfterInstall;

    // Pack exclude paths are strictly for 360 to allow holes in search paths and pack files
    // which fall through to support new or dynamic data on the host pc.
    struct ExcludeFilePath_t {
        FileNameHandle_t m_hName;
        FileNameHandle_t m_hFixedName;
    };
    static CUtlVector<FileNameHandle_t> m_ExcludeFilePaths;

};

inline bool CBaseFileSystem::FilterByPathID(const CSearchPath &searchPath, const PathID pathID) const {
    if (pathID == PathID::Unspecified) {
        // They didn't specify a specific search path, so if this search path's path ID is by
        // request only, then ignore it.
        return m_PathIDByRequestOnly[static_cast<size_t>(searchPath.m_PathID)];
    }
    return searchPath.m_PathID != pathID;
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


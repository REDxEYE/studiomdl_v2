//====== Copyright 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#undef PROTECTED_THINGS_ENABLE
#undef PROTECT_FILEIO_FUNCTIONS
#include "tier1_v2/string_utils.hpp"
#ifndef POSIX
#undef fopen
#endif

#if defined( _WIN32 )
#include <windows.h>
#include <io.h>
#include <process.h>
#elif defined( POSIX )
#include <unistd.h>
#endif
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "tier1/strtools.h"
#include "tier1_v2/path_utils.hpp"
#include "filesystem_init.h"
#include "tier1/keyvalues.h"
#include "tier1/smartptr.h"


#define GAMEINFO_FILENAME			"gameinfo.txt"
#define GAMEINFO_FILENAME_ALTERNATE	"gameinfo.txt"

static char g_FileSystemError[256];
static bool s_bUseVProjectBinDir = false;
static FSErrorMode_t g_FileSystemErrorMode = FS_ERRORMODE_AUTO;

// This class lets you modify environment variables, and it restores the original value
// when it goes out of scope.
class CTempEnvVar {
public:
    CTempEnvVar(const char* pVarName) {
        m_bRestoreOriginalValue = true;
        m_pVarName = pVarName;

        const char* pValue = nullptr;

#if defined( _WIN32 )
        // Use GetEnvironmentVariable instead of getenv because getenv doesn't pick up changes
        // to the process environment after the DLL was loaded.
        char szBuf[4096];
        if (GetEnvironmentVariable(m_pVarName, szBuf, sizeof(szBuf)) != 0) {
            pValue = szBuf;
        }
#else
        // LINUX BUG: see above
        pValue = getenv(pVarName);
#endif

        if (pValue) {
            m_bExisted = true;
            m_OriginalValue = pValue;
        }
        else {
            m_bExisted = false;
        }
    }

    ~CTempEnvVar() {
        if (m_bRestoreOriginalValue) {
            // Restore the original value.
            if (m_bExisted) {
                SetValue("%s", m_OriginalValue.c_str());
            }
            else {
                ClearValue();
            }
        }
    }

    void SetRestoreOriginalValue(bool bRestore) {
        m_bRestoreOriginalValue = bRestore;
    }

    int GetValue(char* pszBuf, int nBufSize) {
        if (!pszBuf || (nBufSize <= 0))
            return 0;

#if defined( _WIN32 )
        // Use GetEnvironmentVariable instead of getenv because getenv doesn't pick up changes
        // to the process environment after the DLL was loaded.
        return GetEnvironmentVariable(m_pVarName, pszBuf, nBufSize);
#else
        // LINUX BUG: see above
        const char* pszOut = getenv(m_pVarName);
        if (!pszOut) {
            *pszBuf = '\0';
            return 0;
        }

        Q_strncpy(pszBuf, pszOut, nBufSize);
        return Q_strlen(pszBuf);
#endif
    }

    void SetValue(const char* pValue, ...) {
        char valueString[4096];
        va_list marker;
        va_start(marker, pValue);
        Q_vsnprintf(valueString, sizeof(valueString), pValue, marker);
        va_end(marker);

#if defined( WIN32 )
        char str[4096];
        Q_snprintf(str, sizeof(str), "%s=%s", m_pVarName, valueString);
        _putenv(str);
#else
        setenv(m_pVarName, valueString, 1);
#endif
    }

    void ClearValue() {
#if defined( WIN32 )
        char str[512];
        Q_snprintf(str, sizeof(str), "%s=", m_pVarName);
        _putenv(str);
#else
        setenv(m_pVarName, "", 1);
#endif
    }

private:
    bool m_bRestoreOriginalValue;
    const char* m_pVarName;
    bool m_bExisted;
    std::string m_OriginalValue;
};


class CSteamEnvVars {
public:
    CSteamEnvVars() : m_SteamAppId("SteamAppId"),
                      m_SteamUserPassphrase("SteamUserPassphrase"),
                      m_SteamAppUser("SteamAppUser"),
                      m_Path("path") {
    }

    void SetRestoreOriginalValue_ALL(bool bRestore) {
        m_SteamAppId.SetRestoreOriginalValue(bRestore);
        m_SteamUserPassphrase.SetRestoreOriginalValue(bRestore);
        m_SteamAppUser.SetRestoreOriginalValue(bRestore);
        m_Path.SetRestoreOriginalValue(bRestore);
    }

    CTempEnvVar m_SteamAppId;
    CTempEnvVar m_SteamUserPassphrase;
    CTempEnvVar m_SteamAppUser;
    CTempEnvVar m_Path;
};

// ---------------------------------------------------------------------------------------------------- //
// Module interface.
// ---------------------------------------------------------------------------------------------------- //

CFSSearchPathsInit::CFSSearchPathsInit() {
    m_pDirectoryName.clear();
    m_ModPath.clear();
}


CFSSteamSetupInfo::CFSSteamSetupInfo() {
    m_pDirectoryName = nullptr;
    m_bOnlyUseDirectoryName = false;
    m_bToolsMode = true;
    m_bNoGameInfo = false;
}


CFSMountContentInfo::CFSMountContentInfo() {
    m_bToolsMode = true;
    m_pDirectoryName.clear();
    m_pFileSystem = nullptr;
}


void AddGameBinDir(IFileSystem* pFileSystem, const char* pLocation, bool bForceHead = false) {
    char temp[MAX_PATH];
    Q_snprintf(temp, sizeof(temp), "%s%cbin", pLocation, CORRECT_PATH_SEPARATOR);
    pFileSystem->AddSearchPath(temp, IBaseFileSystem::PathID::GameBin, bForceHead ? PATH_ADD_TO_HEAD : PATH_ADD_TO_TAIL);
}

KeyValues* ReadKeyValuesFile(const char* pFilename) {
    // Read in the gameinfo.txt file and null-terminate it.
    std::ifstream file(vstd::paths::to_fs(pFilename), std::ios::binary);
    if (!file)
        return nullptr;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    KeyValues* kv = new KeyValues("");
    if (!kv->LoadFromBuffer(pFilename, text.c_str())) {
        kv->deleteThis();
        return nullptr;
    }

    return kv;
}

static bool Sys_GetExecutableName(std::string &out) {
    namespace fs = std::filesystem;

    fs::path executablePath;

#ifdef _WIN32
    std::wstring buffer(32768, L'\0');

    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size())
    );

    if (length == 0 || length >= buffer.size())
        return false;

    buffer.resize(length);
    executablePath = fs::path(buffer);

#else
    std::vector<char> buffer(256);

    while (true) {
        const ssize_t length = readlink(
            "/proc/self/exe", buffer.data(), buffer.size()
        );

        if (length < 0)
            return false;

        if (static_cast<size_t>(length) < buffer.size()) {
            executablePath = fs::path(
                std::string(buffer.data(), static_cast<size_t>(length))
            );
            break;
        }

        buffer.resize(buffer.size() * 2);
    }
#endif

    executablePath = executablePath.lexically_normal();

#if defined(__cpp_char8_t)
    const auto utf8 = executablePath.u8string();
    out.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
    out = executablePath.u8string();
#endif

    return true;
}

bool FileSystem_GetExecutableDir(std::string &exedir) {
    exedir[0] = 0;

    if (s_bUseVProjectBinDir) {
        std::string_view project = GetVProjectCmdLineValue();
        if (project.empty()) {
            // Check their registry.
            project = getenv(GAMEDIR_TOKEN);
        }
        if (!project.empty()) {
            exedir = std::format("{}{}..{}bin", project, CORRECT_PATH_SEPARATOR, CORRECT_PATH_SEPARATOR);
            return true;
        }
        return false;
    }

    if (!Sys_GetExecutableName(exedir))
        return false;
    vstd::paths::strip_filename(exedir);
    vstd::paths::fix_slashes(exedir);
    return true;
}

static bool FileSystem_GetBaseDir(std::string &baseDir) {
    if (FileSystem_GetExecutableDir(baseDir)) {
        vstd::paths::strip_filename(baseDir);
        return true;
    }

    return false;
}

std::string_view GetVProjectCmdLineValue() {
    const auto game_path = CommandLine()->ParmValue("-game");
    const auto vproject = CommandLine()->ParmValue("-vproject");
    if (!game_path && !vproject) {
        MdlError("No game path specified: no -game or -vproject supplied");
    }
    return vproject.value_or(game_path.value_or("!<SHOULD NEVER HAPPEN>!"));
}

FSReturnCode_t SetupFileSystemError(FSReturnCode_t retVal, const char* pMsg, ...) {
    va_list marker;
    va_start(marker, pMsg);
    Q_vsnprintf(g_FileSystemError, sizeof(g_FileSystemError), pMsg, marker);
    va_end(marker);

    Warning("{}\n", g_FileSystemError);

    // Run vconfig?
    // Don't do it if they specifically asked for it not to, or if they manually specified a vconfig with -game or -vproject.

    if (g_FileSystemErrorMode == FS_ERRORMODE_AUTO) {
        // this may happen when we eject disk while loading the game.
        {
            Error("{}\n", g_FileSystemError);
        }
    }

    return retVal;
}

FSReturnCode_t LoadGameInfoFile(
    const std::string_view pDirectoryName,
    KeyValues*&pMainFile,
    KeyValues*&pFileSystemInfo,
    KeyValues*&pSearchPaths) {
    // If GameInfo.txt exists under pBaseDir, then this is their game directory.
    // All the filesystem mappings will be in this file.
    std::string gameinfoFilename(pDirectoryName);
    vstd::paths::append_slash(gameinfoFilename);
    gameinfoFilename += GAMEINFO_FILENAME;
    vstd::paths::fix_slashes(gameinfoFilename);
    pMainFile = ReadKeyValuesFile(gameinfoFilename.c_str());

    if (!pMainFile) {
        return SetupFileSystemError(FS_MISSING_GAMEINFO_FILE, "%s is missing.", gameinfoFilename.c_str());
    }

    pFileSystemInfo = pMainFile->FindKey("FileSystem");
    if (!pFileSystemInfo) {
        pMainFile->deleteThis();
        return SetupFileSystemError(FS_INVALID_GAMEINFO_FILE, "%s is not a valid format.", gameinfoFilename.c_str());
    }

    // Now read in all the search paths.
    pSearchPaths = pFileSystemInfo->FindKey("SearchPaths");
    if (!pSearchPaths) {
        pMainFile->deleteThis();
        return SetupFileSystemError(FS_INVALID_GAMEINFO_FILE, "%s is not a valid format.", gameinfoFilename.c_str());
    }
    return FS_OK;
}

// checks the registry for the low violence setting
// Check "HKEY_CURRENT_USER\Software\Valve\Source\Settings" and "User Token 2" or "User Token 3"
bool IsLowViolenceBuild(void) {
#if defined( _LOWVIOLENCE )
    // a low violence build can not be-undone
    return true;
#endif

    // Users can opt into low violence mode on the command-line.
    if (CommandLine()->FindParm("-lv") != 0)
        return true;

#if defined(_WIN32)
    HKEY hKey;
    char szValue[64];
    DWORD len = sizeof(szValue) - 1;
    bool retVal = false;

    if (IsPC() && RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Valve\\Source\\Settings", NULL, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
        // User Token 2
        if (RegQueryValueEx(hKey, "User Token 2", NULL, NULL, (unsigned char*) szValue, &len) == ERROR_SUCCESS) {
            if (Q_strlen(szValue) > 0) {
                retVal = true;
            }
        }

        if (!retVal) {
            // reset "len" for the next check
            len = sizeof(szValue) - 1;

            // User Token 3
            if (RegQueryValueEx(hKey, "User Token 3", NULL, NULL, (unsigned char*) szValue, &len) == ERROR_SUCCESS) {
                if (Q_strlen(szValue) > 0) {
                    retVal = true;
                }
            }
        }

        RegCloseKey(hKey);
    }

    return retVal;
#elif POSIX
    return false;
#else
#error "Fix me"
#endif
}

static void FileSystem_AddLoadedSearchPath(
    CFSSearchPathsInit &initInfo,
    const char* pPathID,
    bool* bFirstGamePath,
    const char* pBaseDir,
    const char* pLocation,
    bool bLowViolence) {
    char fullLocationPath[MAX_PATH];
    Q_MakeAbsolutePath(fullLocationPath, sizeof(fullLocationPath), pLocation, pBaseDir);

    // Now resolve any ./'s.
    V_FixSlashes(fullLocationPath);
    if (!V_RemoveDotSlashes(fullLocationPath))
        Error("FileSystem_AddLoadedSearchPath - Can't resolve pathname for '{}'", fullLocationPath);

    // Add language, mod, and gamebin search paths automatically.
    if (Q_stricmp(pPathID, "game") == 0) {
        bool bDoAllPaths = true;

        // next add the low violence path
        if (bLowViolence && bDoAllPaths) {
            char szPath[MAX_PATH];
            Q_snprintf(szPath, sizeof(szPath), "%s_lv", fullLocationPath);
            initInfo.m_pFileSystem->AddSearchPath(szPath, IBaseFileSystem::ToPathID(pPathID), PATH_ADD_TO_TAIL);
        }

        if (CommandLine()->FindParm("-tempcontent") != 0 && bDoAllPaths) {
            char szPath[MAX_PATH];
            Q_snprintf(szPath, sizeof(szPath), "%s_tempcontent", fullLocationPath);
            initInfo.m_pFileSystem->AddSearchPath(szPath, IBaseFileSystem::ToPathID(pPathID), PATH_ADD_TO_TAIL);
        }

        // mark the first "game" dir as the "MOD" dir
        if (*bFirstGamePath) {
            *bFirstGamePath = false;
            initInfo.m_pFileSystem->AddSearchPath(fullLocationPath, IBaseFileSystem::PathID::Mod, PATH_ADD_TO_TAIL);
            initInfo.m_ModPath = fullLocationPath;
        }

        if (bDoAllPaths) {
            // add the game bin
            AddGameBinDir(initInfo.m_pFileSystem, fullLocationPath);
        }
    }

    initInfo.m_pFileSystem->AddSearchPath(fullLocationPath, IBaseFileSystem::ToPathID(pPathID), PATH_ADD_TO_TAIL);
}


bool FileSystem_IsHldsUpdateToolDedicatedServer() {
    // To determine this, we see if the directory our executable was launched from is "orangebox".
    // We only are under "orangebox" if we're run from hldsupdatetool.
    std::string baseDir;
    if (!FileSystem_GetBaseDir(baseDir))
        return false;

    vstd::paths::fix_slashes(baseDir);
    vstd::paths::strip_trailing_slash(baseDir);
    auto last_dir = vstd::paths::file_name(baseDir);
    return (!last_dir.empty() && vstd::iequals(last_dir, "orangebox") == 0);
}

#ifdef ENGINE_DLL
extern void FileSystem_UpdateAddonSearchPaths(IFileSystem* pFileSystem);
#endif

FSReturnCode_t FileSystem_LoadSearchPaths(CFSSearchPathsInit &initInfo) {
    if (!initInfo.m_pFileSystem || initInfo.m_pDirectoryName.empty())
        return SetupFileSystemError(FS_INVALID_PARAMETERS,
                                    "FileSystem_LoadSearchPaths: Invalid parameters specified.");

    KeyValues *pMainFile, *pFileSystemInfo, *pSearchPaths;
    FSReturnCode_t retVal = LoadGameInfoFile(initInfo.m_pDirectoryName, pMainFile, pFileSystemInfo, pSearchPaths);
    if (retVal != FS_OK)
        return retVal;

    // All paths except those marked with |gameinfo_path| are relative to the base dir.
    std::string baseDir;
    if (!FileSystem_GetBaseDir(baseDir))
        return SetupFileSystemError(FS_INVALID_PARAMETERS, "FileSystem_GetBaseDir failed.");

    initInfo.m_ModPath[0] = 0;

#define GAMEINFOPATH_TOKEN		"|gameinfo_path|"
#define BASESOURCEPATHS_TOKEN	"|all_source_engine_paths|"

    bool bLowViolence = IsLowViolenceBuild();
    bool bFirstGamePath = true;

    for (KeyValues* pCur = pSearchPaths->GetFirstValue(); pCur; pCur = pCur->GetNextValue()) {
        const char* pPathID = pCur->GetName();
        const char* pLocation = pCur->GetString();

        if (Q_stristr(pLocation, GAMEINFOPATH_TOKEN) == pLocation) {
            pLocation += strlen(GAMEINFOPATH_TOKEN);
            FileSystem_AddLoadedSearchPath(initInfo, pPathID, &bFirstGamePath, initInfo.m_pDirectoryName.c_str(),
                                           pLocation,
                                           bLowViolence);
        }
        else if (Q_stristr(pLocation, BASESOURCEPATHS_TOKEN) == pLocation) {
            // This is a special identifier that tells it to add the specified path for all source engine versions equal to or prior to this version.
            // So in Orange Box, if they specified:
            //		|all_source_engine_paths|hl2
            // it would add the ep2\hl2 folder and the base (ep1-era) hl2 folder.
            //
            // We need a special identifier in the gameinfo.txt here because the base hl2 folder exists in different places.
            // In the case of a game or a Steam-launched dedicated server, all the necessary prior engine content is mapped in with the Steam depots,
            // so we can just use the path as-is.

            // In the case of an hldsupdatetool dedicated server, the base hl2 folder is "..\..\hl2" (since we're up in the 'orangebox' folder).

            pLocation += strlen(BASESOURCEPATHS_TOKEN);

            // Add the Orange-box path (which also will include whatever the depots mapped in as well if we're
            // running a Steam-launched app).
            FileSystem_AddLoadedSearchPath(initInfo, pPathID, &bFirstGamePath, baseDir.c_str(), pLocation,
                                           bLowViolence);

            if (FileSystem_IsHldsUpdateToolDedicatedServer()) {
                // If we're using the hldsupdatetool dedicated server, then go up a directory to get the ep1-era files too.
                char ep1EraPath[MAX_PATH];
                V_snprintf(ep1EraPath, sizeof(ep1EraPath), "..%c%s", CORRECT_PATH_SEPARATOR, pLocation);
                FileSystem_AddLoadedSearchPath(initInfo, pPathID, &bFirstGamePath, baseDir.c_str(), ep1EraPath,
                                               bLowViolence);
            }
        }
        else {
            FileSystem_AddLoadedSearchPath(initInfo, pPathID, &bFirstGamePath, baseDir.c_str(), pLocation,
                                           bLowViolence);
        }
    }


    pMainFile->deleteThis();

    //
    // Set up search paths for add-ons
    //
    if (IsPC()) {
#ifdef ENGINE_DLL
        FileSystem_UpdateAddonSearchPaths(initInfo.m_pFileSystem);
#endif
    }

    // Add the "platform" directory as a game searchable path
    char pPlatformPath[MAX_PATH];
    V_ComposeFileName(baseDir.c_str(), "platform", pPlatformPath, sizeof(pPlatformPath));
    initInfo.m_pFileSystem->AddSearchPath(pPlatformPath, IBaseFileSystem::PathID::Game, PATH_ADD_TO_TAIL);

    // these specialized tool paths are not used on 360 and cause a costly constant perf tax, so inhibited
    if (IsPC()) {
        // Create a content search path based on the game search path
        char szContentRoot[MAX_PATH];
        V_strncpy(szContentRoot, baseDir.c_str(), sizeof(szContentRoot));
        char* pRootEnd = V_strrchr(szContentRoot, '\\');
        if (pRootEnd) {
            *pRootEnd = '\0';
        }

        V_strncat(szContentRoot, "\\content", sizeof(szContentRoot));

        int nLen = initInfo.m_pFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, nullptr, 0);
        char* pSearchPath = (char*) stackalloc(nLen * sizeof(char));
        initInfo.m_pFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, pSearchPath, nLen);
        char* pPath = pSearchPath;
        while (pPath) {
            char* pSemiColon = strchr(pPath, ';');
            if (pSemiColon) {
                *pSemiColon = 0;
            }

            Q_StripTrailingSlash(pPath);
            Q_FixSlashes(pPath);

            const char* pCurPath = pPath;
            pPath = pSemiColon ? pSemiColon + 1 : nullptr;

            char pRelativePath[MAX_PATH];
            char pContentPath[MAX_PATH];
            if (!Q_MakeRelativePath(pCurPath, baseDir.c_str(), pRelativePath, sizeof(pRelativePath)))
                continue;

            Q_ComposeFileName(szContentRoot, pRelativePath, pContentPath, sizeof(pContentPath));
            initInfo.m_pFileSystem->AddSearchPath(pContentPath, IBaseFileSystem::PathID::Content);
        }

        // Also, mark specific path IDs as "by request only". That way, we won't waste time searching in them
        // when people forget to specify a search path.
        initInfo.m_pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::Content, true);
    }

    // Also, mark specific path IDs as "by request only". That way, we won't waste time searching in them
    // when people forget to specify a search path.
    initInfo.m_pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::ExecutablePath, true);
    initInfo.m_pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::GameBin, true);
    initInfo.m_pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::Mod, true);

    // Add the write path last.
    if (initInfo.m_ModPath[0] != 0) {
        initInfo.m_pFileSystem->AddSearchPath(initInfo.m_ModPath, IBaseFileSystem::PathID::DefaultWritePath, PATH_ADD_TO_TAIL);
    }

#ifdef _DEBUG
    initInfo.m_pFileSystem->PrintSearchPaths();
#endif

#if defined( ENABLE_RUNTIME_STACK_TRANSLATION )
    //copy search paths to stack tools so it can grab pdb's from all over. But only on internal or Steam Beta builds
    if ((CommandLine()->FindParm("-steam") == 0) || //not steam
        (CommandLine()->FindParm("-internalbuild") != 0)) //steam beta is ok
    {
        char szSearchPaths[4096];
        int iLength1 = initInfo.m_pFileSystem->GetSearchPath(IBaseFileSystem::PathID::ExecutablePath, false, szSearchPaths, 4096);
        if (iLength1 == 1)
            iLength1 = 0;

        int iLength2 = initInfo.m_pFileSystem->GetSearchPath(IBaseFileSystem::PathID::GameBin, false, szSearchPaths + iLength1,
                                                             4096 - iLength1);
        if ((iLength2 > 1) && (iLength1 > 1)) {
            szSearchPaths[iLength1 - 1] = ';'; //replace first null terminator
        }

        const char* szAdditionalPath = CommandLine()->ParmValue("-AdditionalPDBSearchPath");
        if (szAdditionalPath && szAdditionalPath[0]) {
            int iLength = iLength1;
            if (iLength2 > 1)
                iLength += iLength2;

            if (iLength != 0) {
                szSearchPaths[iLength - 1] = ';'; //replaces null terminator
            }
            V_strncpy(&szSearchPaths[iLength], szAdditionalPath, 4096 - iLength);
        }

        SetStackTranslationSymbolSearchPath(szSearchPaths);
        //MessageBox( NULL, szSearchPaths, "Search Paths", 0 );
    }
#endif

    return FS_OK;
}

static bool DoesFileExistIn(const std::string_view directoryName, const std::string_view filename) {
    std::error_code ec;
    return std::filesystem::exists(vstd::paths::to_fs(vstd::paths::join(directoryName, filename)), ec);
}

namespace {
    SuggestGameInfoDirFn_t& GetSuggestGameInfoDirFn() {
        static SuggestGameInfoDirFn_t s_pfnSuggestGameInfoDir = nullptr;
        return s_pfnSuggestGameInfoDir;
    }
}; // `anonymous` namespace

SuggestGameInfoDirFn_t SetSuggestGameInfoDirFn(SuggestGameInfoDirFn_t pfnNewFn) {
    SuggestGameInfoDirFn_t &rfn = GetSuggestGameInfoDirFn();
    SuggestGameInfoDirFn_t pfnOldFn = rfn;
    rfn = pfnNewFn;
    return pfnOldFn;
}

static FSReturnCode_t TryLocateGameInfoFile(char* pOutDir, int outDirLen, bool bBubbleDir) {
    // Retain a copy of suggested path for further attempts
    CArrayAutoPtr<char> spchCopyNameBuffer(new char [outDirLen]);
    Q_strncpy(spchCopyNameBuffer.Get(), pOutDir, outDirLen);
    spchCopyNameBuffer[outDirLen - 1] = 0;

    // Make appropriate slashes ('/' - Linux style)
    for (char *pchFix = spchCopyNameBuffer.Get(),
                 *pchEnd = pchFix + outDirLen;
         pchFix < pchEnd; ++pchFix) {
        if ('\\' == *pchFix) {
            *pchFix = '/';
        }
    }

    // Have a look in supplied path
    do {
        if (DoesFileExistIn(pOutDir, GAMEINFO_FILENAME)) {
            return FS_OK;
        }
    } while (bBubbleDir && Q_StripLastDir(pOutDir, outDirLen));

    // Make an attempt to resolve from "content -> game" directory
    Q_strncpy(pOutDir, spchCopyNameBuffer.Get(), outDirLen);
    pOutDir[outDirLen - 1] = 0;
    if (char* pchContentFix = Q_stristr(pOutDir, "/content/")) {
        V_strcpy(pchContentFix, "/game/");
        memmove(pchContentFix + 6, pchContentFix + 9, pOutDir + outDirLen - (pchContentFix + 9));

        // Try in the mapped "game" directory
        do {
            if (DoesFileExistIn(pOutDir, GAMEINFO_FILENAME)) {
                return FS_OK;
            }
        } while (bBubbleDir && Q_StripLastDir(pOutDir, outDirLen));
    }

    // Could not find it here
    return FS_MISSING_GAMEINFO_FILE;
}

FSReturnCode_t LocateGameInfoFile(const CFSSteamSetupInfo &fsInfo, std::string &pOutDir) {
    // Engine and Hammer don't want to search around for it.
    if (fsInfo.m_bOnlyUseDirectoryName) {
        if (!fsInfo.m_pDirectoryName)
            return SetupFileSystemError(FS_MISSING_GAMEINFO_FILE,
                                        "bOnlyUseDirectoryName=1 and pDirectoryName=NULL.");

        bool bExists = DoesFileExistIn(fsInfo.m_pDirectoryName, GAMEINFO_FILENAME);

        if (!bExists) {
            return SetupFileSystemError(FS_MISSING_GAMEINFO_FILE,
                                        "Setup file '%s' doesn't exist in subdirectory '%s'.\nCheck your -game parameter or VCONFIG setting.",
                                        GAMEINFO_FILENAME, fsInfo.m_pDirectoryName);
        }
        pOutDir = fsInfo.m_pDirectoryName;
        return FS_OK;
    }

    // First, check for overrides on the command line or environment variables.
    std::string project(GetVProjectCmdLineValue());

    if (!project.empty()) {
        if (DoesFileExistIn(project, GAMEINFO_FILENAME)) {
            pOutDir = vstd::paths::make_absolute_path(pOutDir, project);
            return FS_OK;
        }


        if (fsInfo.m_bNoGameInfo) {
            // fsInfo.m_bNoGameInfo is set by the Steam dedicated server, before it knows which mod to use.
            // Steam dedicated server doesn't need a gameinfo.txt, because we'll ask which mod to use, even if
            // -game is supplied on the command line.
            pOutDir = "";
            return FS_OK;
        }
        else {
            // They either specified vproject on the command line or it's in their registry. Either way,
            // we don't want to continue if they've specified it but it's not valid.
            goto ShowError;
        }
    }

    if (fsInfo.m_bNoGameInfo) {
        pOutDir = "";
        return FS_OK;
    }

    // Ask the application if it can provide us with a game info directory
    {
        bool bBubbleDir = true;
        SuggestGameInfoDirFn_t pfnSuggestGameInfoDirFn = GetSuggestGameInfoDirFn();
        pOutDir.resize(512);
        if (pfnSuggestGameInfoDirFn && (*pfnSuggestGameInfoDirFn)(&fsInfo, pOutDir.data(), pOutDir.size(), &bBubbleDir)
            &&
            FS_OK == TryLocateGameInfoFile(pOutDir.data(), pOutDir.size(), bBubbleDir))
            return FS_OK;
    }
    project = getenv(GAMEDIR_TOKEN);
    // Try to use the environment variable / registry
    if (!project.empty()) {
        pOutDir = vstd::paths::make_absolute_path(pOutDir, project);
        pOutDir.resize(512);
        return TryLocateGameInfoFile(pOutDir.data(), pOutDir.size(), false);
    }
    if (IsPC()) {
        Warning("Warning: falling back to auto detection of vproject directory.\n");

        // Now look for it in the directory they passed in.
        if (fsInfo.m_pDirectoryName)
            pOutDir = vstd::paths::make_absolute_path(pOutDir, fsInfo.m_pDirectoryName);
        else
            pOutDir = vstd::paths::make_absolute_path(pOutDir, ".");

        pOutDir.resize(512);
        if (FS_OK == TryLocateGameInfoFile(pOutDir.data(), pOutDir.size(), true))
            return FS_OK;

        // Use the CWD
        pOutDir = vstd::paths::to_utf8(std::filesystem::current_path());
        pOutDir.resize(512);
        if (FS_OK == TryLocateGameInfoFile(pOutDir.data(), pOutDir.size(), true))
            return FS_OK;
    }

ShowError:
    return SetupFileSystemError(FS_MISSING_GAMEINFO_FILE,
                                "Unable to find %s. Solutions:\n\n"
                                "1. Read http://www.valve-erc.com/srcsdk/faq.html#NoGameDir\n"
                                "2. Run vconfig to specify which game you're working on.\n"
                                "3. Add -game <path> on the command line where <path> is the directory that %s is in.\n",
                                GAMEINFO_FILENAME, GAMEINFO_FILENAME);
}

FSReturnCode_t FileSystem_SetBasePaths(IFileSystem* pFileSystem) {
    pFileSystem->RemoveSearchPaths(IBaseFileSystem::PathID::ExecutablePath);

    std::string executablePath;
    if (!FileSystem_GetExecutableDir(executablePath))
        return SetupFileSystemError(FS_INVALID_PARAMETERS, "FileSystem_GetExecutableDir failed.");

    pFileSystem->AddSearchPath(executablePath, IBaseFileSystem::PathID::ExecutablePath);
    return FS_OK;
}

//-----------------------------------------------------------------------------
// Sets up the steam environment + gets back the gameinfo.txt path
//-----------------------------------------------------------------------------
FSReturnCode_t FileSystem_SetupSteamEnvironment(CFSSteamSetupInfo &fsInfo) {
    // First, locate the directory with gameinfo.txt.
    FSReturnCode_t ret = LocateGameInfoFile(fsInfo, fsInfo.m_GameInfoPath);
    if (ret != FS_OK)
        return ret;

    // This is so that processes spawned by this application will have the same VPROJECT
#if defined( WIN32 )
    std::string pEnvBuf = std::format("{}={}", GAMEDIR_TOKEN, fsInfo.m_GameInfoPath);
    _putenv(pEnvBuf.c_str());
#else
    setenv(GAMEDIR_TOKEN, fsInfo.m_GameInfoPath.c_str(), 1);
#endif

    CSteamEnvVars steamEnvVars;

    return FS_OK;
}


//-----------------------------------------------------------------------------
// Mounds a particular steam cache
//-----------------------------------------------------------------------------
FSReturnCode_t FileSystem_MountContent(CFSMountContentInfo &mountContentInfo) {
    // This part is Steam-only.

    return FileSystem_SetBasePaths(mountContentInfo.m_pFileSystem);
}

void FileSystem_SetErrorMode(FSErrorMode_t errorMode) {
    g_FileSystemErrorMode = errorMode;
}

//-----------------------------------------------------------------------------
// Adds the platform folder to the search path.
//-----------------------------------------------------------------------------
void FileSystem_AddSearchPath_Platform(IFileSystem* pFileSystem, const std::string_view gameInfoPath) {
    std::string platform(gameInfoPath);
    vstd::paths::strip_trailing_slash(platform);
    platform += "/../platform";

    pFileSystem->AddSearchPath(platform, IBaseFileSystem::PathID::Platform);
}

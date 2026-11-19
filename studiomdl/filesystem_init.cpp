//====== Copyright 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: Game directory and search path setup from gameinfo.txt
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
#elif defined( POSIX )
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include "filesystem/stdfile.h"
#include "tier1/strtools.h"
#include "tier1_v2/path_utils.hpp"
#include "filesystem_init.h"
#include "tier1/keyvalues.h"
#include "studiomdl/studiomdl_log.hpp"


constexpr const char* GAMEINFO_FILENAME = "gameinfo.txt";

// Prints the message and stops (Error); the return code is for callers once errors stop being fatal
template<typename... Args>
static FSReturnCode_t SetupFileSystemError(const FSReturnCode_t retVal, std::format_string<Args...> fmt,
                                           Args &&... args) {
    const std::string message = std::format(fmt, std::forward<Args>(args)...);
    Warning("{}\n", message);
    Error("{}\n", message);
    return retVal;
}

static std::unique_ptr<KeyValues> ReadKeyValuesFile(const char* pFilename) {
    // Read in the gameinfo.txt file and null-terminate it. Through the filesystem layer (CStdFile), so a file the
    // library host provides (file_overlay.h) is found too.
    int64_t size = 0;
    const std::unique_ptr<CStdFile> file(CStdFile::Open(pFilename, "rb", &size));
    if (!file)
        return nullptr;
    std::string text(static_cast<size_t>(size), '\0');
    text.resize(file->Read(text.data(), text.size()));

    auto kv = std::make_unique<KeyValues>("");
    if (!kv->LoadFromBuffer(pFilename, text.c_str()))
        return nullptr;

    return kv;
}

static bool Sys_GetExecutableName(std::string &out) {
    namespace fs = std::filesystem;

    fs::path executablePath;

#ifdef _WIN32
    std::wstring buffer(32768, L'\0');

    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

    if (length == 0 || length >= buffer.size())
        return false;

    buffer.resize(length);
    executablePath = fs::path(buffer);

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // fails with the required size
    std::string raw(size, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0)
        return false;
    raw.resize(std::strlen(raw.c_str()));
    executablePath = fs::path(raw);

#else
    std::vector<char> buffer(256);

    while (true) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());

        if (length < 0)
            return false;

        if (static_cast<size_t>(length) < buffer.size()) {
            executablePath = fs::path(std::string(buffer.data(), static_cast<size_t>(length)));
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
    if (!Sys_GetExecutableName(exedir))
        return false;
    vstd::paths::strip_filename(exedir);
    vstd::paths::fix_slashes(exedir);
    return true;
}

// Parent of the executable directory: gameinfo.txt search paths without |gameinfo_path| are relative to it
static bool FileSystem_GetBaseDir(std::string &baseDir) {
    if (FileSystem_GetExecutableDir(baseDir)) {
        vstd::paths::strip_filename(baseDir);
        return true;
    }

    return false;
}

static std::string_view GetVProjectCmdLineValue() {
    const auto game_path = CommandLine()->ParmValue("-game");
    const auto vproject = CommandLine()->ParmValue("-vproject");
    if (!game_path && !vproject) {
        MdlError("No game path specified: no -game or -vproject supplied");
    }
    return vproject.value_or(game_path.value_or("!<SHOULD NEVER HAPPEN>!"));
}

static bool DoesFileExistIn(const std::string_view directoryName, const std::string_view filename) {
    std::error_code ec;
    return std::filesystem::exists(vstd::paths::to_fs(vstd::paths::join(directoryName, filename)), ec);
}

FSReturnCode_t FileSystem_LocateGameInfo(std::string &gameInfoDir) {
    const std::string project(GetVProjectCmdLineValue());
    if (!DoesFileExistIn(project, GAMEINFO_FILENAME)) {
        return SetupFileSystemError(FS_MISSING_GAMEINFO_FILE,
                                    "Unable to find {}. Solutions:\n\n"
                                    "1. Read http://www.valve-erc.com/srcsdk/faq.html#NoGameDir\n"
                                    "2. Run vconfig to specify which game you're working on.\n"
                                    "3. Add -game <path> on the command line where <path> is the directory that {} is in.\n",
                                    GAMEINFO_FILENAME, GAMEINFO_FILENAME);
    }
    gameInfoDir = vstd::paths::make_absolute_path("", project);
    return FS_OK;
}

static FSReturnCode_t LoadGameInfoFile(const std::string_view directoryName, std::unique_ptr<KeyValues> &pMainFile,
                                       KeyValues*&pSearchPaths) {
    // If GameInfo.txt exists under pBaseDir, then this is their game directory.
    // All the filesystem mappings will be in this file.
    std::string gameinfoFilename(directoryName);
    vstd::paths::append_slash(gameinfoFilename);
    gameinfoFilename += GAMEINFO_FILENAME;
    vstd::paths::fix_slashes(gameinfoFilename);
    pMainFile = ReadKeyValuesFile(gameinfoFilename.c_str());

    if (!pMainFile) {
        return SetupFileSystemError(FS_MISSING_GAMEINFO_FILE, "{} is missing.", gameinfoFilename);
    }

    KeyValues* pFileSystemInfo = pMainFile->FindKey("FileSystem");
    if (!pFileSystemInfo)
        return SetupFileSystemError(FS_INVALID_GAMEINFO_FILE, "{} is not a valid format.", gameinfoFilename);

    // Now read in all the search paths.
    pSearchPaths = pFileSystemInfo->FindKey("SearchPaths");
    if (!pSearchPaths)
        return SetupFileSystemError(FS_INVALID_GAMEINFO_FILE, "{} is not a valid format.", gameinfoFilename);
    return FS_OK;
}

static void FileSystem_AddLoadedSearchPath(IFileSystem* pFileSystem, std::string &modPath,
                                           const IBaseFileSystem::PathID pathID,
                                           bool* bFirstGamePath, const std::string_view baseDir,
                                           const std::string_view location) {
    vstd::FixedString<MAX_PATH> fullLocationPath = vstd::paths::make_absolute_path(location, baseDir);

    // Now resolve any ./'s.
    V_FixSlashes(fullLocationPath);
    if (!V_RemoveDotSlashes(fullLocationPath))
        Error("FileSystem_AddLoadedSearchPath - Can't resolve pathname for '{}'", fullLocationPath);

    if (pathID == IBaseFileSystem::PathID::Game) {
        if (CommandLine()->FindParm("-tempcontent") != 0) {
            vstd::FixedString<MAX_PATH> szPath;
            szPath.format("{}_tempcontent", fullLocationPath);
            pFileSystem->AddSearchPath(szPath.view(), pathID, PATH_ADD_TO_TAIL);
        }

        // mark the first "game" dir as the "MOD" dir
        if (*bFirstGamePath) {
            *bFirstGamePath = false;
            pFileSystem->AddSearchPath(fullLocationPath.view(), IBaseFileSystem::PathID::Mod, PATH_ADD_TO_TAIL);
            modPath = fullLocationPath;
        }
    }

    pFileSystem->AddSearchPath(fullLocationPath.view(), pathID, PATH_ADD_TO_TAIL);
}

FSReturnCode_t FileSystem_LoadSearchPaths(IFileSystem* pFileSystem, const std::string_view gameInfoDir) {
    if (!pFileSystem || gameInfoDir.empty())
        return SetupFileSystemError(FS_INVALID_PARAMETERS, "FileSystem_LoadSearchPaths: Invalid parameters specified.");

    std::unique_ptr<KeyValues> pMainFile;
    KeyValues* pSearchPaths = nullptr;
    if (const auto retVal = LoadGameInfoFile(gameInfoDir, pMainFile, pSearchPaths); retVal != FS_OK)
        return retVal;

    // All paths except those marked with |gameinfo_path| are relative to the base dir.
    std::string baseDir;
    if (!FileSystem_GetBaseDir(baseDir))
        return SetupFileSystemError(FS_INVALID_PARAMETERS, "FileSystem_GetBaseDir failed.");

    constexpr std::string_view GAMEINFOPATH_TOKEN = "|gameinfo_path|";
    constexpr std::string_view BASESOURCEPATHS_TOKEN = "|all_source_engine_paths|";

    const std::string gameInfoDirString(gameInfoDir);
    std::string modPath;
    bool firstGamePath = true;

    for (KeyValues* pCur = pSearchPaths->GetFirstValue(); pCur; pCur = pCur->GetNextValue()) {
        const IBaseFileSystem::PathID pathID = IBaseFileSystem::ToPathID(pCur->GetName());
        // Views pCur's string storage: valid until pMainFile goes
        std::string_view location = pCur->GetString();

        if (vstd::istarts_with(location, GAMEINFOPATH_TOKEN)) {
            location.remove_prefix(GAMEINFOPATH_TOKEN.size());
            FileSystem_AddLoadedSearchPath(pFileSystem, modPath, pathID, &firstGamePath, gameInfoDirString,
                                           location);
        }
        else if (vstd::istarts_with(location, BASESOURCEPATHS_TOKEN)) {
            location.remove_prefix(BASESOURCEPATHS_TOKEN.size());
            FileSystem_AddLoadedSearchPath(pFileSystem, modPath, pathID, &firstGamePath, baseDir, location);

            // Valve meant this "..\<path>" (the ep1-era content) for the hldsupdatetool dedicated server layout only,
            // but its check was inverted, so it has always been added; it also feeds the CONTENT mirror below.
            const std::string ep1EraPath = std::format("..{}{}", CORRECT_PATH_SEPARATOR, location);
            FileSystem_AddLoadedSearchPath(pFileSystem, modPath, pathID, &firstGamePath, baseDir, ep1EraPath);
        }
        else {
            FileSystem_AddLoadedSearchPath(pFileSystem, modPath, pathID, &firstGamePath, baseDir, location);
        }
    }

    pMainFile.reset();

    // Add the "platform" directory as a game searchable path
    char pPlatformPath[MAX_PATH];
    V_ComposeFileName(baseDir.c_str(), "platform", pPlatformPath, sizeof(pPlatformPath));
    pFileSystem->AddSearchPath(pPlatformPath, IBaseFileSystem::PathID::Game, PATH_ADD_TO_TAIL);

    // Create a content search path based on the game search path
    {
        char szContentRoot[MAX_PATH];
        V_strncpy(szContentRoot, baseDir.c_str(), sizeof(szContentRoot));
        char* pRootEnd = V_strrchr(szContentRoot, '\\');
        if (pRootEnd) {
            *pRootEnd = '\0';
        }

        V_strncat(szContentRoot, "\\content", sizeof(szContentRoot));

        const int nLen = pFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, nullptr, 0);
        std::string searchPath(nLen, '\0');
        pFileSystem->GetSearchPath(IBaseFileSystem::PathID::Game, false, searchPath.data(), nLen);
        char* pPath = searchPath.data();
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
            pFileSystem->AddSearchPath(pContentPath, IBaseFileSystem::PathID::Content);
        }
    }

    // Also, mark specific path IDs as "by request only". That way, we won't waste time searching in them
    // when people forget to specify a search path.
    pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::Content, true);
    pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::ExecutablePath, true);
    pFileSystem->MarkPathIDByRequestOnly(IBaseFileSystem::PathID::Mod, true);

    // Add the write path last.
    if (!modPath.empty()) {
        pFileSystem->AddSearchPath(modPath, IBaseFileSystem::PathID::DefaultWritePath, PATH_ADD_TO_TAIL);
    }

#ifdef _DEBUG
    pFileSystem->PrintSearchPaths();
#endif

    return FS_OK;
}

FSReturnCode_t FileSystem_SetBasePaths(IFileSystem* pFileSystem) {
    pFileSystem->RemoveSearchPaths(IBaseFileSystem::PathID::ExecutablePath);

    std::string executablePath;
    if (!FileSystem_GetExecutableDir(executablePath))
        return SetupFileSystemError(FS_INVALID_PARAMETERS, "FileSystem_GetExecutableDir failed.");

    pFileSystem->AddSearchPath(executablePath, IBaseFileSystem::PathID::ExecutablePath);
    return FS_OK;
}

void FileSystem_AddSearchPath_Platform(IFileSystem* pFileSystem, const std::string_view gameInfoPath) {
    std::string platform(gameInfoPath);
    vstd::paths::strip_trailing_slash(platform);
    platform += "/../platform";

    pFileSystem->AddSearchPath(platform, IBaseFileSystem::PathID::Platform);
}

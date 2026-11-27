//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Game directory and search path setup from gameinfo.txt
//
//=============================================================================//

#pragma once

#include <string>
#include <string_view>

#include "filesystem.h"


enum FSReturnCode_t {
    FS_OK,
    FS_MISSING_GAMEINFO_FILE,
    FS_INVALID_GAMEINFO_FILE,
    FS_INVALID_PARAMETERS,
};

// Errors are printed and are fatal (Error()); the return codes are for callers that keep going in a later DLL mode.

// The game directory: -vproject, else -game (one of them is required, MdlError otherwise). It must contain
// gameinfo.txt. gameInfoDir gets the absolute directory.
FSReturnCode_t FileSystem_LocateGameInfo(std::string &gameInfoDir);

// Adds the search paths listed in <gameInfoDir>/gameinfo.txt: entries starting with |gameinfo_path| are relative to
// gameInfoDir, the others (and |all_source_engine_paths|) to `baseDir`, by default the parent of the executable
// directory. The first "game" path also becomes MOD and, last, DEFAULT_WRITE_PATH; "-tempcontent" adds
// <game path>_tempcontent. Then <base>/platform as GAME, the CONTENT mirror of every GAME path, and the
// by-request-only marks.
FSReturnCode_t FileSystem_LoadSearchPaths(IFileSystem *pFileSystem, std::string_view gameInfoDir,
                                          std::string_view baseDir = {});

// EXECUTABLE_PATH = the directory of the executable (by request only)
FSReturnCode_t FileSystem_SetBasePaths(IFileSystem *pFileSystem);

// PLATFORM = <gameInfoDir>/../platform
void FileSystem_AddSearchPath_Platform(IFileSystem *pFileSystem, std::string_view gameInfoPath);

bool FileSystem_GetExecutableDir(std::string &exedir);

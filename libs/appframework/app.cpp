#include "appframework/app.h"

#include "filesystem.h"
#include "filesystem_init.h"
#include "tier0/icommandline.h"
#include "tier0/logging.h"
#include "tier1/strtools.h"

static CSimpleLoggingListener s_SimpleLoggingListener;

int CApplication::Run(const int argc, char **argv) {
    CommandLine()->CreateCmdLine(argc, argv);
    FileSystem_SetErrorMode(FS_ERRORMODE_AUTO);

    // Mirrors Valve's app system group: a failing FileSystem_SetBasePaths exits with 0 before the application starts
    if (FileSystem_SetBasePaths(g_pFullFileSystem) != FS_OK) {
        LoggingSystem_ResetCurrentLoggingState();
        LoggingSystem_RegisterLoggingListener(&s_SimpleLoggingListener);
        return 0;
    }

    int nRetVal = -1;
    if (Create() && PreInit() && Init()) {
        nRetVal = Main();
        Shutdown();
        PostShutdown();
    }

    // The logging listeners registered by the application may be gone after Destroy
    LoggingSystem_ResetCurrentLoggingState();
    LoggingSystem_RegisterLoggingListener(&s_SimpleLoggingListener);
    Destroy();
    return nRetVal;
}

bool CApplication::SetupSearchPaths(const char *pStartingDir, bool bOnlyUseStartingDir, bool bIsTool) {
    CFSSteamSetupInfo steamInfo;
    steamInfo.m_pDirectoryName = pStartingDir;
    steamInfo.m_bOnlyUseDirectoryName = bOnlyUseStartingDir;
    steamInfo.m_bToolsMode = bIsTool;
    steamInfo.m_bSetSteamDLLPath = true;
    steamInfo.m_bSteam = g_pFullFileSystem->IsSteam();
    if (FileSystem_SetupSteamEnvironment(steamInfo) != FS_OK)
        return false;

    CFSMountContentInfo fsInfo;
    fsInfo.m_pFileSystem = g_pFullFileSystem;
    fsInfo.m_bToolsMode = bIsTool;
    fsInfo.m_pDirectoryName = steamInfo.m_GameInfoPath;
    if (FileSystem_MountContent(fsInfo) != FS_OK)
        return false;

    // Finally, load the search paths for the "GAME" path.
    CFSSearchPathsInit searchPathsInit;
    searchPathsInit.m_pDirectoryName = steamInfo.m_GameInfoPath;
    searchPathsInit.m_pFileSystem = fsInfo.m_pFileSystem;
    if (FileSystem_LoadSearchPaths(searchPathsInit) != FS_OK)
        return false;

    FileSystem_AddSearchPath_Platform(fsInfo.m_pFileSystem, steamInfo.m_GameInfoPath);
    m_GameInfoPath = steamInfo.m_GameInfoPath;
    return true;
}

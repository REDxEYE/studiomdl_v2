#include "appframework/app.h"

#include "filesystem_init.h"
#include "interfaces/interfaces.h"
#include "tier0/icommandline.h"
#include "tier0/logging.h"
#include "tier1/strtools.h"

static CSimpleLoggingListener s_SimpleLoggingListener;

int CApplication::Run(const int argc, char **argv) {
    CommandLine()->CreateCmdLine(argc, argv);

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

bool CApplication::SetupSearchPaths() {
    std::string gameInfoPath;
    if (FileSystem_LocateGameInfo(gameInfoPath) != FS_OK)
        return false;

    // The search paths of the "GAME" path and friends, from gameinfo.txt
    if (FileSystem_LoadSearchPaths(g_pFullFileSystem, gameInfoPath) != FS_OK)
        return false;

    FileSystem_AddSearchPath_Platform(g_pFullFileSystem, gameInfoPath);
    m_GameInfoPath = std::move(gameInfoPath);
    return true;
}

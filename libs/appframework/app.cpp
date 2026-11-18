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
    std::exception_ptr failure;
    try {
        if (Create() && PreInit() && Init()) {
            nRetVal = Main();
            Shutdown();
            PostShutdown();
        }
    }
    catch (...) {
        failure = std::current_exception();
    }

    if (failure) {
        // The application reports the failure and returns the exit code; one that doesn't handle it lets it
        // propagate once the application is destroyed
        try {
            nRetVal = OnException(failure);
            failure = nullptr;
        }
        catch (...) {
            failure = std::current_exception();
        }
    }

    // The logging listeners registered by the application may be gone after Destroy
    LoggingSystem_ResetCurrentLoggingState();
    LoggingSystem_RegisterLoggingListener(&s_SimpleLoggingListener);
    Destroy();
    if (failure)
        std::rethrow_exception(failure);
    return nRetVal;
}

int CApplication::OnException(const std::exception_ptr &error) {
    std::rethrow_exception(error);
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

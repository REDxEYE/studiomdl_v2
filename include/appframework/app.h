#pragma once

#include <string>

#include "tier0/platform.h"

//-----------------------------------------------------------------------------
// Minimal console application skeleton. Libraries are linked directly; there are no interface factories.
//
// Run() calls, in order:
//   Create        parse the command line, set up spew
//   PreInit       search paths (filesystem base paths are set before this)
//   Init          initialize the libraries the application uses
//   Main          the actual work; its return value is the exit code
//   Shutdown      shut the libraries down, reverse order of Init
//   PostShutdown
//   Destroy
// A failing Create/PreInit/Init returns -1 and skips Main, Shutdown and PostShutdown (Init cleans up after itself);
// Destroy always runs.
//-----------------------------------------------------------------------------
class CApplication {
public:
    virtual ~CApplication() = default;

    int Run(int argc, char **argv);

protected:
    virtual bool Create() { return true; }
    virtual bool PreInit() { return true; }
    virtual bool Init() { return true; }
    virtual int Main() = 0;
    virtual void Shutdown() {}
    virtual void PostShutdown() {}
    virtual void Destroy() {}

    // Locates gameinfo.txt from pStartingDir and mounts its search paths on g_pFullFileSystem
    bool SetupSearchPaths(const char *pStartingDir, bool bOnlyUseStartingDir, bool bIsTool);
    std::string_view GetGameInfoPath() const { return m_GameInfoPath; }

private:
    std::string m_GameInfoPath{};
};

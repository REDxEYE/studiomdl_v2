#pragma once

#include <exception>
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
// Destroy always runs. An exception out of any step skips the remaining steps and goes to OnException (the
// application reports it and returns the exit code); Destroy runs after that.
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

    // Called with the exception that ended Create..PostShutdown; returns the exit code of Run. The steps that ran
    // are not undone (their libraries may be half initialized): clean up here. The default rethrows, and Run then
    // rethrows once the application is destroyed.
    virtual int OnException(const std::exception_ptr &error);

    // Locates gameinfo.txt (-game / -vproject) and mounts its search paths on g_pFullFileSystem
    bool SetupSearchPaths();
    std::string_view GetGameInfoPath() const { return m_GameInfoPath; }

private:
    std::string m_GameInfoPath{};
};

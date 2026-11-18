//
// Created by RED on 15.05.2024.
//

#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "appframework/app.h"

struct StudioMdlState;
class ScopedMdlDiagnostics;
enum class MdlSeverity : int;

// Runs one compile from a studiomdl command line (argv[0] is the program name, arguments are UTF-8) and returns its
// exit code (255 for a compile error, with the same output the console tool always printed). Several runs in one
// process are independent (BeginProcessing/EndProcessing reset the process-wide state); a failing compile leaves the
// process reusable.
int RunStudioMdl(int argc, char** argv);

// The same run for a library host (smdl_run_command_line): the process is left alone (no crash handler, no stdout
// buffering change, the working directory stays: sources are looked up relative to the QC's directory anyway). With a
// sink every message goes there, one complete message per call, and nothing is written to stdout; without one the
// output is the console tool's. The caller holds the process-wide compile lock.
int RunStudioMdlLibrary(int argc, char** argv, std::function<void(MdlSeverity, std::string_view)> sink);

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CStudioMDLApp : public CApplication {
public:
    CStudioMDLApp();
    CStudioMDLApp(bool library, std::function<void(MdlSeverity, std::string_view)> sink);
    ~CStudioMDLApp() override;

protected:
    // Starts with BeginProcessing() (before PreInit adds the compile's search paths); EndProcessing() runs in Shutdown,
    // or in Destroy when an earlier step failed
    bool Create() override;

    bool PreInit() override;

    int Main() override;

    void Shutdown() override;

    // A failed compile or setup: reports it as the console tool always did and returns the exit code (see the .cpp)
    int OnException(const std::exception_ptr &error) override;

    void Destroy() override;

private:
    // library: see RunStudioMdlLibrary
    bool m_Library = false;
    std::function<void(MdlSeverity, std::string_view)> m_Sink;

    bool ParseArguments(StudioMdlState &ctx);

    // The compile state, created by Create() and released by Destroy(); the diagnostics read it for that whole
    // span (declared after the state so it is uninstalled first)
    std::unique_ptr<StudioMdlState> m_pState;
    std::unique_ptr<ScopedMdlDiagnostics> m_pDiagnostics;
};

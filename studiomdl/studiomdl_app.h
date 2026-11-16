//
// Created by RED on 15.05.2024.
//

#pragma once

#include <memory>

#include "appframework/app.h"

struct StudioMdlState;
class ScopedMdlDiagnostics;

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CStudioMDLApp : public CApplication {
protected:
    // Starts with BeginProcessing() (before PreInit adds the compile's search paths); EndProcessing() runs in Shutdown,
    // or in Destroy when an earlier step failed
    bool Create() override;

    bool PreInit() override;

    int Main() override;

    void Shutdown() override;

    void Destroy() override;

private:
    bool ParseArguments(StudioMdlState &ctx);

    // The compile state, created by Create() and released by Destroy(); the diagnostics read it for that whole
    // span (declared after the state so it is uninstalled first)
    std::unique_ptr<StudioMdlState> m_pState;
    std::unique_ptr<ScopedMdlDiagnostics> m_pDiagnostics;
};

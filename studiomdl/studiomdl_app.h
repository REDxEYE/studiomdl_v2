//
// Created by RED on 15.05.2024.
//

#pragma once

#include "appframework/app.h"

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CStudioMDLApp : public CApplication {
protected:
    bool Create() override;

    bool PreInit() override;

    bool Init() override;

    int Main() override;

    void Shutdown() override;

    void Destroy() override;

private:
    bool ParseArguments();
};

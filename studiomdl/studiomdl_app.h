//
// Created by RED on 15.05.2024.
//

#ifndef STUDIOMDL_V2_STUDIOMDL_APP_H
#define STUDIOMDL_V2_STUDIOMDL_APP_H

#include "appframework/IAppSystemGroup.h"

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CStudioMDLApp : public CDefaultAppSystemGroup<CSteamAppSystemGroup> {
    typedef CDefaultAppSystemGroup<CSteamAppSystemGroup> BaseClass;

public:
    // Methods of IApplication
    virtual bool Create();

    virtual bool PreInit();

    virtual int Main();

    virtual void PostShutdown();

    virtual void Destroy();

private:
    int Main_StripModel();

    int Main_StripVhv();

    int Main_MakeVsi();

private:
    bool ParseArguments();
};

#endif //STUDIOMDL_V2_STUDIOMDL_APP_H

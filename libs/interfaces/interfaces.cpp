//===== Copyright � 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A higher level link library for general use in the game and tools.
//
//===========================================================================//

#include "tier0/platform.h"

#include "interfaces/interfaces.h"

#include <array>

#include "tier0/commonmacros.h"
#include "tier0/dbg.h"


//-----------------------------------------------------------------------------
// Tier 1 libraries
//-----------------------------------------------------------------------------
/* All Tier 1 libraries are removed */

//-----------------------------------------------------------------------------
// Tier 2 libraries
//-----------------------------------------------------------------------------
IFileSystem *g_pFullFileSystem = nullptr;
IMaterialSystem *materials = nullptr;
IMaterialSystem *g_pMaterialSystem = nullptr;
IMdlLib *mdllib = nullptr;

//-----------------------------------------------------------------------------
// Tier3 libraries
//-----------------------------------------------------------------------------
IMeshSystem *g_pMeshSystem = nullptr;
IDataCache *g_pDataCache = nullptr;
IMDLCache *g_pMDLCache = nullptr;
IMDLCache *mdlcache = nullptr;


namespace {
    //-----------------------------------------------------------------------------
    // Mapping of interface string to globals
    //-----------------------------------------------------------------------------
    struct InterfaceGlobals_t {
        template<typename T>
        constexpr InterfaceGlobals_t(const char *name, T *iface) noexcept {
            m_pInterfaceName = name;
            m_ppGlobal = static_cast<void *>(iface);
        }

        const char *m_pInterfaceName;
        void *m_ppGlobal;
    };
}

static constinit std::array g_pInterfaceGlobals =
{
    // InterfaceGlobals_t{CVAR_INTERFACE_VERSION, &cvar},
    // InterfaceGlobals_t{CVAR_INTERFACE_VERSION, &g_pCVar},
    InterfaceGlobals_t{MATERIAL_SYSTEM_INTERFACE_VERSION, &materials},
    InterfaceGlobals_t{MDLLIB_INTERFACE_VERSION, &mdllib},
    InterfaceGlobals_t{MDLCACHE_INTERFACE_VERSION, &g_pMDLCache},
    InterfaceGlobals_t{MDLCACHE_INTERFACE_VERSION, &mdlcache},
    InterfaceGlobals_t{MESHSYSTEM_INTERFACE_VERSION, &g_pMeshSystem},
};


//-----------------------------------------------------------------------------
// The # of times this DLL has been connected
//-----------------------------------------------------------------------------
static int s_nConnectionCount = 0;


namespace {
    //-----------------------------------------------------------------------------
    // At each level of connection, we're going to keep track of which interfaces
    // we filled in. When we disconnect, we'll clear those interface pointers out.
    //-----------------------------------------------------------------------------
    struct ConnectionRegistration_t {
        template<typename T>
        constexpr ConnectionRegistration_t(T *globalStorage, const int connectionPhase) noexcept {
            m_ppGlobalStorage = static_cast<void *>(globalStorage);
            m_nConnectionPhase = connectionPhase;
        }

        ConnectionRegistration_t() noexcept : m_ppGlobalStorage(nullptr), m_nConnectionPhase(0) {
        }

        void *m_ppGlobalStorage;
        int m_nConnectionPhase;
    };
}

static int s_nRegistrationCount = 0;
static std::array<ConnectionRegistration_t, g_pInterfaceGlobals.size() + 1> s_pConnectionRegistration;

void RegisterInterface(CreateInterfaceFn factory, const char *pInterfaceName, void **ppGlobal) {
    if (!(*ppGlobal)) {
        *ppGlobal = factory(pInterfaceName, nullptr);
        if (*ppGlobal) {
            Assert(s_nRegistrationCount < ARRAYSIZE(s_pConnectionRegistration));
            ConnectionRegistration_t &reg = s_pConnectionRegistration[s_nRegistrationCount++];
            reg = {ppGlobal, s_nConnectionCount};
        }
    }
}

void ReconnectInterface(CreateInterfaceFn factory, const char *pInterfaceName, void **ppGlobal) {
    *ppGlobal = factory(pInterfaceName, nullptr);

    bool bFound = false;
    Assert(s_nRegistrationCount < ARRAYSIZE(s_pConnectionRegistration));
    for (int i = 0; i < s_nRegistrationCount; ++i) {
        ConnectionRegistration_t &reg = s_pConnectionRegistration[i];
        if (reg.m_ppGlobalStorage != ppGlobal)
            continue;

        reg.m_ppGlobalStorage = ppGlobal;
        bFound = true;
    }

    if (!bFound && *ppGlobal) {
        Assert(s_nRegistrationCount < ARRAYSIZE(s_pConnectionRegistration));
        ConnectionRegistration_t &reg = s_pConnectionRegistration[s_nRegistrationCount++];
        reg.m_ppGlobalStorage = ppGlobal;
        reg.m_nConnectionPhase = s_nConnectionCount;
    }
}


//-----------------------------------------------------------------------------
// Call this to connect to all tier 1 libraries.
// It's up to the caller to check the globals it cares about to see if ones are missing
//-----------------------------------------------------------------------------
void ConnectInterfaces(CreateInterfaceFn *pFactoryList, const int nFactoryCount) {
    if (s_nRegistrationCount < 0) {
        Error("APPSYSTEM: In ConnectInterfaces(), s_nRegistrationCount is %d!\n", s_nRegistrationCount);
    }
    else if (s_nRegistrationCount == 0) {
        for (int i = 0; i < nFactoryCount; ++i) {
            for (int j = 0; j < ARRAYSIZE(g_pInterfaceGlobals); ++j) {
                RegisterInterface(pFactoryList[i], g_pInterfaceGlobals[j].m_pInterfaceName,
                                  (void **) g_pInterfaceGlobals[j].m_ppGlobal);
            }
        }
    }
    else {
        // This is no longer questionable: ConnectInterfaces() is expected to be called multiple times for a file that exports multiple interfaces.
        // Warning("APPSYSTEM: ConnectInterfaces() was called twice for the same DLL.\nThis is expected behavior in building reslists, but questionable otherwise.\n");
        for (int i = 0; i < nFactoryCount; ++i) {
            for (int j = 0; j < ARRAYSIZE(g_pInterfaceGlobals); ++j) {
                ReconnectInterface(pFactoryList[i], g_pInterfaceGlobals[j].m_pInterfaceName,
                                   (void **) g_pInterfaceGlobals[j].m_ppGlobal);
            }
        }
    }
    ++s_nConnectionCount;
}

void DisconnectInterfaces() {
    Assert(s_nConnectionCount > 0);
    if (--s_nConnectionCount < 0)
        return;

    for (int i = 0; i < s_nRegistrationCount; ++i) {
        if (s_pConnectionRegistration[i].m_nConnectionPhase != s_nConnectionCount)
            continue;

        // Disconnect!
        *(void **) (s_pConnectionRegistration[i].m_ppGlobalStorage) = nullptr;
    }
}


//-----------------------------------------------------------------------------
// Reloads an interface
//-----------------------------------------------------------------------------
void ReconnectInterface(CreateInterfaceFn factory, const char *pInterfaceName) {
    for (int i = 0; i < ARRAYSIZE(g_pInterfaceGlobals); ++i) {
        if (strcmp(g_pInterfaceGlobals[i].m_pInterfaceName, pInterfaceName))
            continue;
        ReconnectInterface(factory, g_pInterfaceGlobals[i].m_pInterfaceName,
                           (void **) g_pInterfaceGlobals[i].m_ppGlobal);
    }
}

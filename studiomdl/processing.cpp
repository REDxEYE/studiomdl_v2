#include "studiomdl/processing.hpp"

#include "filesystem.h"
#include "filesystem_init.h"
#include "interfaces/interfaces.h"
#include "tier2/tier2dm.h"

static bool s_bProcessing = false;

bool BeginProcessing() {
    EndProcessing();

    // Only the base paths; the compile adds the paths of its game and content directories
    g_pFullFileSystem->RemoveAllSearchPaths();
    if (FileSystem_SetBasePaths(g_pFullFileSystem) != FS_OK)
        return false;

    s_bProcessing = true;
    if (!InitDataModel()) {
        EndProcessing();
        return false;
    }
    return true;
}

void EndProcessing() {
    if (!s_bProcessing)
        return;
    s_bProcessing = false;

    // Destroys every element and file, then drops the element types, serializers and updaters
    ShutdownDataModel();
    g_pFullFileSystem->RemoveAllSearchPaths();
}

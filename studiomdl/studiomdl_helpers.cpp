//
// Created by red_eye on 11/11/26.
//

#include "studiomdl/studiomdl_helpers.hpp"

#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"

std::string OutputModelPath(const std::string_view extension) {
    std::string path = g_StudioMdlContext.gameDir;
    path += "models/";
    path += vstd::paths::without_extension(g_studiomdl.outname.view());
    path += extension;
    vstd::paths::fix_slashes(path);
    return path;
}

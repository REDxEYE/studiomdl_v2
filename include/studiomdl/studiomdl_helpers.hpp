//
// Created by red_eye on 11/11/26.
//

#pragma once
#include <string>
#include <string_view>

struct StudioMdlState;

// <gameDir>models/<$modelname without extension><extension>: UTF-8, native separators
std::string OutputModelPath(StudioMdlState &ctx, std::string_view extension);
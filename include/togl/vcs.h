#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace togl
{
// Returns translated GLSL for one exact combo, or throws with a diagnostic.
std::string LoadVcsShader(const std::filesystem::path &file, uint32_t staticID, uint32_t dynamicID);
bool ExportVcsShaders(const std::filesystem::path &file, const std::filesystem::path &output,
                      uint32_t sampleCount, std::optional<uint32_t> staticID);
}

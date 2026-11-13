//
// "Flex optimization": keeps the flexed-vertex cache of the renderer from overflowing.
//
// studiorender caches flexed vertices per model draw and gives every mesh that has flexes a slice of that cache at a
// 16-bit offset, the size of the slice being the mesh's whole vertex count. When the meshes with flexes that are drawn
// together add up to more than 65535 vertices the offsets wrap and meshes read each other's flex results.
//
// FlexOptimizeSources() splits every flexed mesh into a mesh with the faces that touch a moving vertex (keeps the flex
// data) and a mesh with the remaining faces (no flex data, does not count against the cache), inside the same model so
// bodygroups keep working. CheckFlexCacheBudget() reports the final budget: the flexed meshes of all models of all bodyparts.
//

#pragma once

#include <vector>

// Number of vertices the flexed-vertex cache of studiorender can address per model draw
constexpr int FLEX_CACHE_MAX_VERTICES = 65535;

struct s_source_t;
enum FlexOptimizeMode_t : int;

// Parses "auto", "on" or "off"; returns false (mode unchanged) for anything else
bool ParseFlexOptimizeMode(std::string_view text, FlexOptimizeMode_t &mode);

// Splits flexed meshes of all active sources; runs after the script is parsed, before ClampMaxVerticesPerModel
void FlexOptimizeSources();

// True once FlexOptimizeSources() decided to optimize this model (mode on, or auto and over budget)
bool FlexOptimizeIsActive();

// Per source vertex: true if a flex of the source can move it (see FlexOptimizeSources)
std::vector<bool> FlexOptimizeMovingVertices(s_source_t *pSource);

// Points the skin family columns of the alias materials created by FlexOptimizeSources() at the textures of their
// base materials; runs at the end of SetSkinValues
void FlexOptimizeFixupSkinFamilies();

// Reports the flex cache budget of the processed models; runs after SimplifyModel
void CheckFlexCacheBudget();


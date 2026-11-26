#pragma once

// SFM VertexLitGeneric/UnlitGeneric shader combo selection. Static layouts come from
// tools/sfm_shader_combos.py (sfm_shader_combos.inc); the selection logic follows the SFM
// stdshader_dx9.dll draw functions the layouts were recovered from.

#include <compare>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

struct ComboSlot
{
    const char *name;
    uint32_t weight;
    uint32_t count;
};

struct ComboLayout
{
    const char *shader;
    uint32_t staticCount;
    uint32_t dynamicCount;
    std::span<const ComboSlot> combos; // not "slots": Qt defines that as a macro
};

struct ShaderCombo
{
    std::string vs, ps; // VCS file stems
    uint32_t vsStatic = 0, vsDynamic = 0, psStatic = 0, psDynamic = 0;
    auto operator<=>(const ShaderCombo &) const = default;
};

// VMT-derived inputs of the selection; anything not listed here is left at its default combo value.
struct ComboFeatures
{
    bool unlit = false;
    bool phong = false;
    bool halfLambert = false;
    bool phongHalfLambert = false;
    bool pixelFog = true; // generated constructors: !( $flags & MATERIAL_VAR_VERTEXFOG ), i.e. no $vertexfog
};

// Static combo id of `shader` with the named combos set; unnamed combos are 0. Throws std::runtime_error
// for unknown shaders, unknown combo names and out-of-range values.
uint32_t StaticComboId(std::string_view shader, std::initializer_list<std::pair<std::string_view, uint32_t>> values);

ShaderCombo SelectCombo(const ComboFeatures &features);

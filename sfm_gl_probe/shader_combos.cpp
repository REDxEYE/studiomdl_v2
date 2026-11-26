#include "shader_combos.h"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace
{
#include "sfm_shader_combos.inc"
#include "sfm_shader_combos_manual.inc"

// Every layout must cover exactly its VCS static count with weights that are running products of counts.
constexpr bool Consistent(const ComboLayout &layout)
{
    uint32_t weight = 1;
    for (const auto &slot : layout.combos)
    {
        if (slot.weight != weight || slot.count == 0)
            return false;
        weight *= slot.count;
    }
    return weight == layout.staticCount;
}
static_assert(std::ranges::all_of(k_extractedLayouts, Consistent) && std::ranges::all_of(k_manualLayouts, Consistent));

const ComboLayout *FindLayout(std::string_view shader)
{
    for (const auto &layouts : {std::span<const ComboLayout>(k_extractedLayouts), std::span<const ComboLayout>(k_manualLayouts)})
        if (const auto it = std::ranges::find(layouts, shader, &ComboLayout::shader); it != layouts.end())
            return &*it;
    return nullptr;
}

// Dynamic combos are set from per-draw state; the viewport draws one directional light, no flashlight,
// no fog, uncompressed pre-skinned vertices. Values from the same DLL's dynamic GetIndex functions:
// vertexlit_and_unlit_generic_vs30 (0x10054190): COMPRESSED_VERTS 1, DYNAMIC_LIGHT 2, SKINNING 4, ...
// phong_ps30: NUM_LIGHTS has weight 4 (one light => 4).
constexpr uint32_t genericVsDynamicLight = 2;
constexpr uint32_t phongPsOneLight = 4;
} // namespace

uint32_t StaticComboId(std::string_view shader, std::initializer_list<std::pair<std::string_view, uint32_t>> values)
{
    const auto *layout = FindLayout(shader);
    if (!layout)
        throw std::runtime_error(std::format("no combo layout for {}", shader));
    uint32_t id = 0;
    for (const auto &[name, value] : values)
    {
        const auto slot = std::ranges::find_if(layout->combos, [&](const ComboSlot &s) { return s.name && name == s.name; });
        if (slot == layout->combos.end())
            throw std::runtime_error(std::format("{} has no combo {}", shader, name));
        if (value >= slot->count)
            throw std::runtime_error(std::format("{} combo {} = {} is outside 0..{}", shader, name, value, slot->count - 1));
        id += slot->weight * value;
    }
    return id;
}

ShaderCombo SelectCombo(const ComboFeatures &f)
{
    ShaderCombo combo;
    if (f.phong)
    {
        combo.vs = "phong_vs30";
        combo.ps = "phong_ps30";
        // SFM runs at dxlevel 95, so DOPIXELFOG follows $vertexfog alone.
        combo.vsStatic = StaticComboId(combo.vs, {{"DOPIXELFOG", f.pixelFog}});
        combo.psStatic = StaticComboId(combo.ps, {{"PHONG_HALFLAMBERT", f.phongHalfLambert}});
        combo.psDynamic = phongPsOneLight;
        return combo;
    }
    combo.vs = "vertexlit_and_unlit_generic_vs30";
    combo.ps = "vertexlit_and_unlit_generic_ps30";
    combo.vsStatic = StaticComboId(combo.vs, {{"HALFLAMBERT", f.halfLambert && !f.unlit}, {"DOPIXELFOG", f.pixelFog}});
    combo.vsDynamic = f.unlit ? 0 : genericVsDynamicLight;
    combo.psStatic = StaticComboId(combo.ps, {{"DIFFUSELIGHTING", !f.unlit}, {"DOPIXELFOG", f.pixelFog}});
    return combo;
}

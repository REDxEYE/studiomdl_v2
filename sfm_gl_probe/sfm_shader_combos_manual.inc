// Hand-derived from SFM stdshader_dx9.dll: these vertex shaders' static index is inlined into the
// SetVertexShader call, so tools/sfm_shader_combos.py cannot pair it with a GetIndex function.

// phong_vs30, phong draw function 0x100380F0:
//   48 * ( DECAL + 2 * ( WORLD_NORMAL + 2 * ( this[2] + 2 * this[3] ) ) )
// this[2], this[3] come from the generated constructor 0x10037750:
//   this[2] = dxlevel >= 92 && !( $flags & MATERIAL_VAR_VERTEXFOG ), this[3] = 0.
constexpr ComboSlot k_phong_vs30_slots[] = {
    {"DECAL", 1, 2},
    {"WORLD_NORMAL", 2, 2},
    {"DOPIXELFOG", 4, 2},
    {"HARDWAREFOGBLEND", 8, 1},
};

// vertexlit_and_unlit_generic_bump_vs30, VertexLitGeneric draw function 0x10055320:
//   48 * ( HALFLAMBERT + 4 * ( DECAL + 4 * ( WORLD_NORMAL + 2 * ( STATICLIGHT3 + this[6] + 2 * this[7] ) ) ) + 2 )
// The constant 2 is USE_WITH_2B = 1 (weight 2); NORMALIZEEYEVEC = 0 sits at weight 8. this[6], this[7]
// come from the generated constructor 0x10054310 with the same fog expressions as phong_vs30.
constexpr ComboSlot k_vertexlit_and_unlit_generic_bump_vs30_slots[] = {
    {"HALFLAMBERT", 1, 2},
    {"USE_WITH_2B", 2, 2},
    {"DECAL", 4, 2},
    {"NORMALIZEEYEVEC", 8, 2},
    {"WORLD_NORMAL", 16, 2},
    {"STATICLIGHT3", 32, 1},
    {"DOPIXELFOG", 32, 2},
    {"HARDWAREFOGBLEND", 64, 1},
};

constexpr ComboLayout k_manualLayouts[] = {
    {"phong_vs30", 8, 48, k_phong_vs30_slots},
    {"vertexlit_and_unlit_generic_bump_vs30", 64, 48, k_vertexlit_and_unlit_generic_bump_vs30_slots},
};

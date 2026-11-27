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

// eyes_vs30 / eyes_ps30, Eyes_dx9 draw function 0x1001AD10 (SM3 branch):
//   VS: ( INTRO + 2 * ( HALFLAMBERT + 2 * ( dxlevel >= 92 ) ) ) << 6;  PS: static 0.
// The top slot is unnamed in the DLL; like phong_vs30's this[2] it is taken to be DOPIXELFOG.
constexpr ComboSlot k_eyes_vs30_slots[] = {
    {"INTRO", 1, 2},
    {"HALFLAMBERT", 2, 2},
    {"DOPIXELFOG", 4, 2},
};

// eye_refract_vs30 / eye_refract_ps30, EyeRefract_dx9 draw function 0x10018420 (HasFastVertexTextures branch):
//   VS: 480 * ( INTRO + 2 * ( HALFLAMBERT + 2 * ( FLASHLIGHT + 2 * ( LIGHTWARPTEXTURE + 2 * ( WORLD_NORMAL + 2 * ( dxlevel >= 92 ) ) ) ) ) )
//   PS: 40 * ( FLASHLIGHT + 2 * ( LIGHTWARPTEXTURE + 2 * ( SPHERETEXKILLCOMBO + 2 * ( RAYTRACESPHERE + 2 * ( FLASHLIGHTDEPTHFILTERMODE + 3 * WORLD_NORMAL ) ) ) ) )
constexpr ComboSlot k_eye_refract_vs30_slots[] = {
    {"INTRO", 1, 2},
    {"HALFLAMBERT", 2, 2},
    {"FLASHLIGHT", 4, 2},
    {"LIGHTWARPTEXTURE", 8, 2},
    {"WORLD_NORMAL", 16, 2},
    {"DOPIXELFOG", 32, 2},
};
constexpr ComboSlot k_eye_refract_ps30_slots[] = {
    {"FLASHLIGHT", 1, 2},
    {"LIGHTWARPTEXTURE", 2, 2},
    {"SPHERETEXKILLCOMBO", 4, 2},
    {"RAYTRACESPHERE", 8, 2},
    {"FLASHLIGHTDEPTHFILTERMODE", 16, 3},
    {"WORLD_NORMAL", 48, 2},
};

constexpr ComboLayout k_manualLayouts[] = {
    {"phong_vs30", 8, 48, k_phong_vs30_slots},
    {"vertexlit_and_unlit_generic_bump_vs30", 64, 48, k_vertexlit_and_unlit_generic_bump_vs30_slots},
    {"eyes_vs30", 8, 64, k_eyes_vs30_slots},
    {"eyes_ps30", 1, 4, {}},
    {"eye_refract_vs30", 64, 480, k_eye_refract_vs30_slots},
    {"eye_refract_ps30", 96, 40, k_eye_refract_ps30_slots},
};

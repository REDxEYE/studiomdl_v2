/* studiomdl C API: model-wide settings: material paths, skins, transform, surfaceprop/contents, flags, eye/illum
 * position, bbox/cbox, keyvalues, include models, animation defaults and compile options.
 *
 * Every setter replaces what an earlier call of the same setter stored (the add_* functions accumulate, in call
 * order). Enabled/disabled arguments are C booleans (0/1); calling a flag setter with 0 removes the flag again.
 * The values reach the compile unchanged: the equivalent QC command of every function is named in its comment, and a
 * model built with these calls compiles to the same bytes as a QC with those commands. A QC that issues a command
 * several times with different values has no single equivalent (the builder keeps the last value of a setter).
 *
 * Call-time checks: null/empty names, non-finite numbers, ids that are not bones of the model, the format limits
 * named below. Everything else (a surface property or bone name the compile can't resolve) is reported by the compile
 * exactly as the QC command reports it.
 */
#ifndef STUDIOMDL_C_SMDL_GLOBALS_H
#define STUDIOMDL_C_SMDL_GLOBALS_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------------------------ */
/* Identity and paths */

/* $internalname: the name stored in the .mdl besides its path (the file name by default). */
SMDL_API smdl_status smdl_model_set_internal_name(smdl_model *model, const char *name);

/* $cdmaterials: a folder (relative to materials/) where the material names of the geometries are searched. A
 * trailing '/' is added when missing. At most 16 paths. The order of the calls is the search order. */
SMDL_API smdl_status smdl_model_add_material_path(smdl_model *model, const char *path);

/* $includemodel: another model (path relative to models/, with extension) whose sequences this model can use.
 * At most 128. Needs a game directory at compile time when a sequence refers to the included model. */
SMDL_API smdl_status smdl_model_add_include_model(smdl_model *model, const char *path);

/* ------------------------------------------------------------------------------------------------------------ */
/* Skins (texture groups) and material replacement */

/* $texturegroup "skinfamilies": adds one skin family, a list of material names (the names the geometries of the
 * model use, with or without extension). The first family added is the default skin (the materials the geometries
 * refer to) and fixes the number of materials per family: every later family must have as many entries and entry i
 * is the material that replaces entry i of the default family in that skin. The compile checks that the default
 * family lists the materials of the model. The family's index (0 = default) is returned through out_index (may be NULL).
 * At most 128 materials in all families together (MAXSTUDIOSKINS). */
SMDL_API smdl_status smdl_model_add_skin_family(smdl_model *model, const char *const *materials, size_t count,
                                                uint32_t *out_index);

/* $overridematerial: every material reference of the model's geometries becomes `name`. Applied after the
 * geometries are processed, like the QC command placed after the $body commands. */
SMDL_API smdl_status smdl_model_override_material(smdl_model *model, const char *name);

/* -t <replacement> [<texture it replaces>] of the command line: the loaders replace the named material (every
 * material when `replaces` is NULL or empty) by `replacement`. At most 16. */
SMDL_API smdl_status smdl_model_add_texture_replacement(smdl_model *model, const char *replacement,
                                                        const char *replaces);

/* ------------------------------------------------------------------------------------------------------------ */
/* Transform of the sources (applies to every geometry that does not set its own scale) */

/* $scale: factor applied to positions and rest poses of the geometries (default 1). Finite and not 0. */
SMDL_API smdl_status smdl_model_set_scale(smdl_model *model, float scale);

/* $origin x y z: offset applied to the model (the root bones' positions). */
SMDL_API smdl_status smdl_model_set_origin(smdl_model *model, const float origin[3]);

/* The optional angle of $origin: rotation around Z in degrees (the compile adds the 90 degrees of studiomdl's built-in
 * rotation, as the QC command does). When smdl_model_set_up_axis is also called the angle replaces the Z rotation of
 * the up axis (the QC order matters there too: the later command wins). */
SMDL_API smdl_status smdl_model_set_origin_angle(smdl_model *model, float degrees);

/* $upaxis: the up axis of the art. Z is studiomdl's default (-Z behaves the same, the way the QC command does). */
typedef enum smdl_up_axis {
    SMDL_UP_AXIS_X = 0,
    SMDL_UP_AXIS_NEG_X = 1,
    SMDL_UP_AXIS_Y = 2,
    SMDL_UP_AXIS_NEG_Y = 3,
    SMDL_UP_AXIS_Z = 4,
    SMDL_UP_AXIS_NEG_Z = 5
} smdl_up_axis;
SMDL_API smdl_status smdl_model_set_up_axis(smdl_model *model, smdl_up_axis axis);

/* $autocenter: static props only (the compile warns and ignores it otherwise) */
SMDL_API smdl_status smdl_model_set_autocenter(smdl_model *model, int enabled);

/* $centerbonesonverts: move the bones to the center of the vertices they own */
SMDL_API smdl_status smdl_model_set_center_bones_on_verts(smdl_model *model, int enabled);

/* ------------------------------------------------------------------------------------------------------------ */
/* Surface properties and contents */

/* $surfaceprop: the default surface property of the model (name of an entry of the game's surfaceproperties.txt;
 * without a game directory the compile accepts any name and uses the built-in defaults). */
SMDL_API smdl_status smdl_model_set_surfaceprop(smdl_model *model, const char *name);

/* $jointsurfaceprop: surface property of one bone (replaces the default for that bone) */
SMDL_API smdl_status smdl_model_set_bone_surfaceprop(smdl_model *model, smdl_bone_id bone, const char *name);

/* $contents / $jointcontents: the words of the QC commands as bits. The compile starts from SOLID, `grate` adds GRATE
 * and removes SOLID, `ladder` and `monster` add their bit, `solid` adds SOLID, `notsolid` removes SOLID (a
 * remove wins over an add of the same bit). flags 0 leaves the default (SOLID). */
typedef enum smdl_contents_flags {
    SMDL_CONTENTS_SOLID = 1,
    SMDL_CONTENTS_GRATE = 2,
    SMDL_CONTENTS_LADDER = 4,
    SMDL_CONTENTS_MONSTER = 8,
    SMDL_CONTENTS_NOTSOLID = 16
} smdl_contents_flags;
SMDL_API smdl_status smdl_model_set_contents(smdl_model *model, uint32_t flags);
SMDL_API smdl_status smdl_model_set_bone_contents(smdl_model *model, smdl_bone_id bone, uint32_t flags);

/* ------------------------------------------------------------------------------------------------------------ */
/* Flags of the model header */

/* $staticprop: a prop without moving parts. The compile stores only the bind pose; a static prop that has no
 * sequence gets the default sequence "BindPose" (see the compile description in smdl_compile.h / ApplyFinish). */
SMDL_API smdl_status smdl_model_set_static_prop(smdl_model *model, int enabled);

/* $opaque, $mostlyopaque ($opaque wins whichever is set) */
SMDL_API smdl_status smdl_model_set_opaque(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_mostly_opaque(smdl_model *model, int enabled);

/* $ambientboost, $noforcedfade, $donotcastshadows, $casttextureshadows, $subd, $obsolete, $forcephonemecrossfade */
SMDL_API smdl_status smdl_model_set_ambient_boost(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_no_forced_fade(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_do_not_cast_shadows(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_cast_texture_shadows(smdl_model *model, int enabled);
/* $subd: the model is a quad subdivision surface; required for geometries with quads (smdl_geometry_set_quads) */
SMDL_API smdl_status smdl_model_set_subd(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_obsolete(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_force_phoneme_crossfade(smdl_model *model, int enabled);

/* $constantdirectionallight: the model is lit by a constant light with this dot product (0..1, stored in 8 bits) */
SMDL_API smdl_status smdl_model_set_constant_directional_light(smdl_model *model, int enabled, float dot);

/* ------------------------------------------------------------------------------------------------------------ */
/* Header values */

/* $eyeposition: the position of the eyes relative to the model origin (the values as the QC command takes them) */
SMDL_API smdl_status smdl_model_set_eye_position(smdl_model *model, const float position[3]);

/* $illumposition: the point the model's lighting is computed at. attachment_bone SMDL_NO_BONE: a plain position;
 * otherwise a rigid attachment "__illumPosition" to that bone is created (it takes the attachment index after the
 * attachments created before it: the compile applies it before the attachments of the rig part, as a QC that lists
 * $illumposition before its $attachment commands). */
SMDL_API smdl_status smdl_model_set_illum_position(smdl_model *model, const float position[3],
                                                   smdl_bone_id attachment_bone);

/* $maxeyedeflection: degrees */
SMDL_API smdl_status smdl_model_set_max_eye_deflection(smdl_model *model, float degrees);

/* $gamma: the gamma the texture colors are authored in (default 2.2) */
SMDL_API smdl_status smdl_model_set_gamma(smdl_model *model, float gamma);

/* $bbox / $cbox: hull and view bounding box. $bboxonlyverts: the hull comes from the vertices only (not from
 * bones and sequence boxes), $skipboneinbbox: the bone origin is not part of the boxes. */
SMDL_API smdl_status smdl_model_set_bbox(smdl_model *model, const float mins[3], const float maxs[3]);
SMDL_API smdl_status smdl_model_set_cbox(smdl_model *model, const float mins[3], const float maxs[3]);
SMDL_API smdl_status smdl_model_set_bbox_only_verts(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_skip_bone_in_bbox(smdl_model *model, int enabled);

/* $collisionprecision */
SMDL_API smdl_status smdl_model_set_collision_precision(smdl_model *model, float precision);

/* $keyvalues: appends the KeyValues text `text` (the content of the QC block without the outer braces, e.g.
 * "prop_data { base Wooden.Large }"). The text is parsed here like the QC block: unbalanced braces fail the call
 * with SMDL_ERROR_SYNTAX and the message says what is wrong. Several calls append in order. */
SMDL_API smdl_status smdl_model_add_keyvalues(smdl_model *model, const char *text);

/* The `qc_path` entry of the .mdl's key values: the text a QC compile records for its QC file (the path of the QC
 * relative to the content root). A model built in code has no QC, so by default the .mdl carries no qc_path entry;
 * set it to record where the model was built from, or to reproduce a QC compile byte for byte. NULL or "" = no
 * entry. The compile does not interpret the text. */
SMDL_API smdl_status smdl_model_set_qc_path(smdl_model *model, const char *path);

/* ------------------------------------------------------------------------------------------------------------ */
/* Animation and LOD defaults of the model */

/* $minlod / -minlod and $allowrootlods: LOD limits (levels, >= 0). A minimum above the allowed root LODs raises
 * them, with a warning, as the QC commands do. */
SMDL_API smdl_status smdl_model_set_min_lod(smdl_model *model, int min_lod);
SMDL_API smdl_status smdl_model_set_allow_root_lods(smdl_model *model, int count);

/* $defaultfadein / $defaultfadeout: seconds, >= 0 */
SMDL_API smdl_status smdl_model_set_default_fade_in_time(smdl_model *model, float seconds);
SMDL_API smdl_status smdl_model_set_default_fade_out_time(smdl_model *model, float seconds);

/* $motionrollback: how far back simple motion extraction pulls from the last frame */
SMDL_API smdl_status smdl_model_set_motion_rollback(smdl_model *model, float rollback);

/* $clampworldspace: world space limits of animation motion */
SMDL_API smdl_status smdl_model_set_clamp_worldspace(smdl_model *model, const float mins[3], const float maxs[3]);

/* $allowactivityname: an activity name that is allowed although the engine does not know it. Accumulates. */
SMDL_API smdl_status smdl_model_add_allowed_activity_name(smdl_model *model, const char *name);

/* $erroronsequenceremappingfailure, $modelhasnosequences ($modelintentionallyhaszerosequences) */
SMDL_API smdl_status smdl_model_set_error_on_sequence_remap_failure(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_model_has_no_sequences(smdl_model *model, int enabled);

/* $calctransitions: computes the transition graph between sequence nodes (multistage) */
SMDL_API smdl_status smdl_model_set_calc_transitions(smdl_model *model, int enabled);

/* $skiptransition: forbids direct transitions between every pair of the listed nodes (2..10 names; nodes are
 * numbered in the order the compile first sees their names: the sequences' entry/exit node names, then these). Applied
 * after the sequences, like the QC command placed after them. Several calls accumulate. */
SMDL_API smdl_status smdl_model_add_skip_transition(smdl_model *model, const char *const *nodes, size_t count);

/* $sectionframes: animations longer than `frames` are cut into sections of that length (at least `min_frames`
 * long); 0 disables. */
SMDL_API smdl_status smdl_model_set_section_frames(smdl_model *model, int frames, int min_frames);

/* $animblocksize: size of the animation blocks in KB (a value below 1024 is in KB, above in bytes); 0 = no animation
 * blocks. The remaining setters are the options of the QC command and only matter when a size is set. */
SMDL_API smdl_status smdl_model_set_anim_block_size(smdl_model *model, int size);
SMDL_API smdl_status smdl_model_set_anim_block_no_stall(smdl_model *model, int enabled);

typedef enum smdl_anim_block_resolution {
    SMDL_ANIM_BLOCK_DEFAULT = 0,
    SMDL_ANIM_BLOCK_HIGH_RES = 1,
    SMDL_ANIM_BLOCK_LOW_RES = 2
} smdl_anim_block_resolution;
SMDL_API smdl_status smdl_model_set_anim_block_resolution(smdl_model *model, smdl_anim_block_resolution resolution);
/* numframes: zero frames kept in the main file, 1..4 (the compile clamps like the QC command) */
SMDL_API smdl_status smdl_model_set_anim_block_num_frames(smdl_model *model, int frames);
SMDL_API smdl_status smdl_model_set_anim_block_cache_high_res(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_anim_block_pos_delta(smdl_model *model, float delta);

/* ------------------------------------------------------------------------------------------------------------ */
/* Compile options (the QC commands and command-line switches that change the output) */

/* $zbrush, $preservetriangleorder (-preservetriangleorder), $cliptotextures */
SMDL_API smdl_status smdl_model_set_zbrush(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_preserve_triangle_order(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_clip_to_textures(smdl_model *model, int enabled);

/* $maxverts: limit of vertices per model piece and the size the pieces are clamped to. Both are clamped to
 * 1024..MAXSTUDIOVERTS; clamp_target 0 = half the limit at most. */
SMDL_API smdl_status smdl_model_set_max_vertices(smdl_model *model, int limit, int clamp_target);

/* $flexoptimize / -flexopt, -flexopt_min_static: auto, on or off, and the minimum number of static vertices */
typedef enum smdl_flex_optimize_mode {
    SMDL_FLEX_OPTIMIZE_AUTO = 0,
    SMDL_FLEX_OPTIMIZE_ON = 1,
    SMDL_FLEX_OPTIMIZE_OFF = 2
} smdl_flex_optimize_mode;
SMDL_API smdl_status smdl_model_set_flex_optimize(smdl_model *model, smdl_flex_optimize_mode mode);
SMDL_API smdl_status smdl_model_set_flex_optimize_min_static(smdl_model *model, int min_static_vertices);

/* -a <degrees>: the angle under which vertices at the same position merge their normals (default 2) */
SMDL_API smdl_status smdl_model_set_normal_blend_angle(smdl_model *model, float degrees);

/* -nowarnings: no warnings in the log; -maxwarnings <n>: stop printing after n warnings (0 = none, -1 = all) */
SMDL_API smdl_status smdl_model_set_no_warnings(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_max_warnings(smdl_model *model, int count);

/* -fitweights [<fitting poses>,<test poses>] (experimental): vertices with more than 3 bone influences get weights
 * fitted over sample poses instead of the 3 largest rescaled, where that is better over the source. Pose counts
 * 1..4096, 0 = the default (24). */
SMDL_API smdl_status smdl_model_set_fit_weights(smdl_model *model, int enabled, int fitting_poses, int test_poses);

/* -striplods: LODs other than the root and the shadow LOD are dropped from the compile */
SMDL_API smdl_status smdl_model_set_strip_lods(smdl_model *model, int enabled);

/* -preview: builds the model in preview mode (triangle order is kept, the file is flagged as a preview build) */
SMDL_API smdl_status smdl_model_set_build_preview(smdl_model *model, int enabled);

SMDL_API smdl_status smdl_model_set_dump_perf(smdl_model *model, int enabled);

#ifdef __cplusplus
}
#endif

#endif

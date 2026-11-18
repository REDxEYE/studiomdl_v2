/* studiomdl C API: LODs and the shadow LOD ($lod, $shadowlod and the options of their blocks).
 *
 * The model itself is LOD 0. smdl_model_add_lod adds LOD 1, 2, ... in call order (at most 7: MAX_NUM_LODS is 8
 * including LOD 0); a shadow LOD is added last (smdl_model_add_shadow_lod, at most one) and nothing can be added after
 * it. A LOD lists what it changes compared with LOD 0:
 *   - replace a mesh's geometry (`replacemodel`): the replacement is a geometry of its own, built from the same
 *     data as a mesh geometry (smdl_geometry_*), over the model's skeleton, or an existing geometry of the model;
 *   - remove a mesh (`removemodel`): the LOD does not draw it;
 *   - replace a bone (`replacebone`) or collapse a bone subtree (`bonetreecollapse`): vertices weighted to the bone
 *     use another one in this LOD;
 *   - replace a material (`replacematerial`), remove a material (`removemesh`: the LOD drops every mesh that uses the
 *     material);
 *   - facial animation on/off (`facial` / `nofacial`; a shadow LOD starts without and cannot enable it),
 *     `use_shadowlod_materials` (shadow LOD only).
 * The compile applies the options of one LOD in the order listed (replacemodel first), not in call order; every
 * equivalent QC lists them in that order. A mesh is replaced or removed at most once per LOD.
 * Meshes are matched by name with everything from the last '.' dropped, as studiomdl matches the source names of a
 * replacemodel; two meshes whose names are equal that way cannot be replaced or removed (the compile reports it).
 * -striplods (smdl_model_set_strip_lods) drops every LOD but the root at compile time (the shadow LOD too, though the
 * model keeps its shadow LOD flag, as with the QC); the geometries of the dropped LODs are not built.
 * $minlod / $allowrootlods are model settings (smdl_model_set_min_lod, smdl_model_set_allow_root_lods).
 */
#ifndef STUDIOMDL_C_SMDL_LOD_H
#define STUDIOMDL_C_SMDL_LOD_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* $lod <switch_distance> { ... }: switch_distance >= 0 (negative values are reserved for the shadow LOD).
 * SMDL_ERROR_LIMIT when the model has 7 LODs already, SMDL_ERROR_INVALID_ARGUMENT after the shadow LOD. */
SMDL_API smdl_status smdl_model_add_lod(smdl_model *model, float switch_distance, smdl_lod **out_lod);

/* $shadowlod { ... }: the last LOD (SMDL_ERROR_DUPLICATE_NAME when there is one already). */
SMDL_API smdl_status smdl_model_add_shadow_lod(smdl_model *model, smdl_lod **out_lod);

/* Number of LODs added to the model (LOD 0 is not counted, the shadow LOD is). */
SMDL_API uint32_t smdl_model_lod_count(const smdl_model *model);

SMDL_API int smdl_lod_is_shadow(const smdl_lod *lod);

/* replacemodel <mesh> <replacement> [reverse]: the LOD draws a new geometry instead of the mesh. out_geometry is
 * owned by the model; fill it like a mesh geometry (smdl_geometry_set_positions, ...). `reverse` is
 * smdl_geometry_set_flip_triangles on it. */
SMDL_API smdl_status smdl_lod_replace_mesh(smdl_lod *lod, smdl_mesh *mesh, smdl_geometry **out_geometry);

/* The same with an existing geometry of this model (for example one shared by several LODs; its source is built
 * once). */
SMDL_API smdl_status smdl_lod_replace_mesh_geometry(smdl_lod *lod, smdl_mesh *mesh, smdl_geometry *geometry);

/* removemodel <mesh>: the LOD does not draw the mesh. */
SMDL_API smdl_status smdl_lod_remove_mesh(smdl_lod *lod, smdl_mesh *mesh);

/* replacebone <from> <to>: vertices weighted to `from` are weighted to `to` in this LOD. */
SMDL_API smdl_status smdl_lod_replace_bone(smdl_lod *lod, smdl_bone_id from, smdl_bone_id to);

/* bonetreecollapse <bone>: `bone` and its whole subtree are replaced by `bone`. */
SMDL_API smdl_status smdl_lod_collapse_bone_tree(smdl_lod *lod, smdl_bone_id bone);

/* replacematerial <from> <to>: material names as in the geometries ($cdmaterials relative). `to` joins the model's
 * material list. */
SMDL_API smdl_status smdl_lod_replace_material(smdl_lod *lod, const char *from, const char *to);

/* removemesh <material>: the LOD drops every mesh that uses the material (the name is compared by its base name). */
SMDL_API smdl_status smdl_lod_remove_material(smdl_lod *lod, const char *material);

/* facial / nofacial. A shadow LOD cannot enable facial animation (SMDL_ERROR_INVALID_ARGUMENT). */
SMDL_API smdl_status smdl_lod_set_facial_animation(smdl_lod *lod, int enabled);

/* use_shadowlod_materials: shadow LOD only (SMDL_ERROR_INVALID_ARGUMENT for a regular LOD). */
SMDL_API smdl_status smdl_lod_set_use_shadow_lod_materials(smdl_lod *lod, int enabled);

#ifdef __cplusplus
}
#endif

#endif

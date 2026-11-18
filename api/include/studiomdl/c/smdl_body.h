/* studiomdl C API: body groups, meshes and presets.
 *
 * A body group ($bodygroup) is a named list of choices; each choice is a mesh or a blank (nothing drawn). A model
 * with a single mesh per group uses smdl_model_create_mesh ($body). The model's bodygroup value selects one choice
 * per group (the stride of group k is the product of the choice counts of the groups before it).
 * Groups appear in the model in creation order; so do the meshes of a group.
 * A mesh owns its geometry (smdl_mesh_get_geometry). The mesh name is the name studiomdl writes for the model
 * (mstudiomodel_t::name; the "studio" file name of a QC), and is unique in the model.
 */
#ifndef STUDIOMDL_C_SMDL_BODY_H
#define STUDIOMDL_C_SMDL_BODY_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* $body <name>: creates a body group `name` with exactly one choice, a mesh also called `name`.
 * SMDL_ERROR_DUPLICATE_NAME if a group or a mesh of that name exists. */
SMDL_API smdl_status smdl_model_create_mesh(smdl_model *model, const char *name, smdl_mesh **out_mesh);

/* $bodygroup <name>: creates an empty body group. Group names are unique (ignoring case). */
SMDL_API smdl_status smdl_model_add_bodygroup(smdl_model *model, const char *name, smdl_bodygroup **out_group);

/* Adds a mesh as the next choice of the group. The name is unique among all meshes of the model (ignoring case),
 * non-empty and shorter than 64 bytes (it is stored in a 64 byte field). */
SMDL_API smdl_status smdl_bodygroup_create_mesh(smdl_bodygroup *group, const char *name, smdl_mesh **out_mesh);

/* Adds a blank choice (nothing is drawn) to the group: `blank` of $bodygroup, $appendblankbodygroup. */
SMDL_API smdl_status smdl_bodygroup_add_blank(smdl_bodygroup *group);

SMDL_API smdl_status smdl_mesh_get_geometry(smdl_mesh *mesh, smdl_geometry **out_geometry);

/* $appendsource: adds more geometry to the mesh. The new geometry is merged into the mesh's source after it is built
 * (studiomdl merges the faces into one mesh that uses the first material of the source, as it does for
 * $appendsource; use it for static props). Configure it through the returned handle. */
SMDL_API smdl_status smdl_mesh_append_geometry(smdl_mesh *mesh, smdl_geometry **out_geometry);

/* The mesh name (valid for the life of the model), "" for a null handle. */
SMDL_API const char *smdl_mesh_name(const smdl_mesh *mesh);

SMDL_API smdl_status smdl_model_find_mesh(const smdl_model *model, const char *name, smdl_mesh **out_mesh);
SMDL_API smdl_status smdl_model_find_bodygroup(const smdl_model *model, const char *name,
                                               smdl_bodygroup **out_group);

SMDL_API const char *smdl_bodygroup_name(const smdl_bodygroup *group);

/* Number of choices (meshes and blanks) of the group. */
SMDL_API uint32_t smdl_bodygroup_choice_count(const smdl_bodygroup *group);

SMDL_API uint32_t smdl_model_bodygroup_count(const smdl_model *model);

/* $bodygrouppreset <name> { <group> <value> ... }: a named bodygroup value + mask over `count` groups.
 * groups[i] must belong to this model, values[i] is the choice index in that group (checked by the compile against
 * the final choice count, so groups may still grow after this call). Names are unique (case-sensitive, as in the
 * QC command). */
SMDL_API smdl_status smdl_model_add_bodygroup_preset(smdl_model *model, const char *name,
                                                     const smdl_bodygroup *const *groups, const uint32_t *values,
                                                     size_t count);

#ifdef __cplusplus
}
#endif

#endif

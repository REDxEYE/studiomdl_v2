/* studiomdl C API: the collision model ($collisionmodel / $collisionjoints and every command of their block).
 *
 * A model has at most one collision model (smdl_model_get_collision creates it on first use; a model whose collision
 * model is never given a geometry, convex source, command or text has none and writes no .phy). Its source is a geometry:
 * the collision model's own (smdl_collision_get_geometry) or any geometry of the model (smdl_collision_set_geometry,
 * for example the geometry of a mesh: the compile builds its own copy of the source, as the QC loads the SMD again).
 * Fill it like a mesh geometry (smdl_geometry_*) over the model's skeleton; every triangle belongs to the bone its
 * vertices are weighted to (no weights = bone 0), the materials are irrelevant and are not added to the model. Without
 * a geometry the root collision is empty (`$collisionmodel blank`) and convex sources provide the shapes.
 * Joint names are the bones of the model skeleton, given by id.
 *
 * The commands act in call order, like the lines of the QC block: a `jointmerge` after a `jointskip` sees the skip.
 * Commands with no geometry and no convex source fail the compile ("collision model has no geometry").
 * studiomdl loads no surface property files (LoadSurfacePropsAll is disabled), so a compile with or without a game
 * directory gives the collision model the default surface properties; the model's $surfaceprop / $contents are still
 * written to the .mdl.
 *
 * smdl_collision_set_jointed(1) = $collisionjoints (a constraint system with one body per joint), 0 = $collisionmodel
 * (one body, the default). $rollingDrag is accepted by studiomdl and does nothing; smdl_collision_set_rolling_drag
 * exists for completeness and does nothing either.
 */
#ifndef STUDIOMDL_C_SMDL_COLLISION_H
#define STUDIOMDL_C_SMDL_COLLISION_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum smdl_collision_joint_type {
    SMDL_COLLISION_JOINT_FREE = 0,  /* `free`: -360..360 degrees with the friction */
    SMDL_COLLISION_JOINT_FIXED = 1, /* `fixed` */
    SMDL_COLLISION_JOINT_LIMIT = 2  /* `limit <min> <max>` */
} smdl_collision_joint_type;

typedef enum smdl_collision_axis { SMDL_COLLISION_AXIS_X = 0, SMDL_COLLISION_AXIS_Y = 1, SMDL_COLLISION_AXIS_Z = 2 } smdl_collision_axis;

/* The model's collision model (always the same object). */
SMDL_API smdl_status smdl_model_get_collision(smdl_model *model, smdl_collision **out_collision);

/* -- source ------------------------------------------------------------------------------------------------------ */

/* $collisionmodel <file>: the collision model's own geometry (created by the first call, owned by the model). */
SMDL_API smdl_status smdl_collision_get_geometry(smdl_collision *collision, smdl_geometry **out_geometry);
/* ... or an existing geometry of this model (for example a mesh's). Replaces the previous choice. */
SMDL_API smdl_status smdl_collision_set_geometry(smdl_collision *collision, smdl_geometry *geometry);
/* $collisionjoints instead of $collisionmodel. */
SMDL_API smdl_status smdl_collision_set_jointed(smdl_collision *collision, int jointed);

/* The name of the collision source, as a source file path without extension ("src/phys_box" for `$collisionmodel
 * "src/phys_box.smd"`); it shows in the .phy (the name of the solid). Only for the geometry created by
 * smdl_collision_get_geometry (default "collision"), and for a convex source created by smdl_collision_add_convex_source
 * (default "collision_convex<index>"); SMDL_ERROR_INVALID_ARGUMENT for a geometry set by the caller. */
SMDL_API smdl_status smdl_collision_set_source_name(smdl_collision *collision, const char *name);
SMDL_API smdl_status smdl_collision_set_convex_source_name(smdl_collision *collision, uint32_t index, const char *name);

/* $addconvexsrc <file>: another source of convex pieces (at most 24; SMDL_ERROR_LIMIT beyond). The first form creates its
 * geometry (owned by the model, fill it like a mesh geometry), the second uses an existing geometry of this model. The
 * index (0, 1, ... in call order) names the source for the two setters below. */
SMDL_API smdl_status smdl_collision_add_convex_source(smdl_collision *collision, smdl_geometry **out_geometry,
                                                      uint32_t *out_index);
SMDL_API smdl_status smdl_collision_add_convex_source_geometry(smdl_collision *collision, smdl_geometry *geometry,
                                                               uint32_t *out_index);
SMDL_API uint32_t smdl_collision_convex_source_count(const smdl_collision *collision);
/* offset pos[...] angle[...] scale[...]: position, angles (pitch, yaw, roll in degrees: a QAngle) and a scale relative to
 * the model's $scale. Without it the source is not moved. */
SMDL_API smdl_status smdl_collision_set_convex_source_offset(smdl_collision *collision, uint32_t index,
                                                             const float position[3], const float angles[3],
                                                             float scale);
/* concave: the source's pieces are not split into convex parts. */
SMDL_API smdl_status smdl_collision_set_convex_source_concave(smdl_collision *collision, uint32_t index, int concave);

/* The .phy text section (KeyValues text: `"solid" { "index" "0" ... }` blocks), as `collisionText` of a preprocessed
 * model (there is no QC command for it). Applied after the commands. */
SMDL_API smdl_status smdl_collision_set_text(smdl_collision *collision, const char *text);

/* -- whole model (the QC commands of the block) -------------------------------------------------------------------- */

SMDL_API smdl_status smdl_collision_set_mass(smdl_collision *collision, float mass);                     /* $mass */
SMDL_API smdl_status smdl_collision_set_auto_mass(smdl_collision *collision);                            /* $automass */
SMDL_API smdl_status smdl_collision_set_inertia(smdl_collision *collision, float inertia);               /* $inertia: default of every joint */
SMDL_API smdl_status smdl_collision_set_damping(smdl_collision *collision, float damping);               /* $damping */
SMDL_API smdl_status smdl_collision_set_rot_damping(smdl_collision *collision, float rot_damping);       /* $rotdamping */
SMDL_API smdl_status smdl_collision_set_drag(smdl_collision *collision, float drag);                     /* $drag */
SMDL_API smdl_status smdl_collision_set_rolling_drag(smdl_collision *collision, float rolling_drag);     /* $rollingDrag (no effect) */
SMDL_API smdl_status smdl_collision_set_max_convex_pieces(smdl_collision *collision, int count);         /* $maxconvexpieces */
SMDL_API smdl_status smdl_collision_set_remove_2d(smdl_collision *collision);                            /* $remove2d */
SMDL_API smdl_status smdl_collision_set_concave_per_joint(smdl_collision *collision);                    /* $concaveperjoint */
SMDL_API smdl_status smdl_collision_set_weld_position(smdl_collision *collision, float epsilon);         /* $weldposition */
SMDL_API smdl_status smdl_collision_set_weld_normal(smdl_collision *collision, float epsilon);           /* $weldnormal */
SMDL_API smdl_status smdl_collision_set_concave(smdl_collision *collision);                              /* $concave */
SMDL_API smdl_status smdl_collision_set_convex_hull_count_override(smdl_collision *collision);           /* $convexhullcountoverride */
SMDL_API smdl_status smdl_collision_set_mass_center(smdl_collision *collision, const float center[3]);   /* $masscenter */
SMDL_API smdl_status smdl_collision_set_no_self_collisions(smdl_collision *collision);                   /* $noselfcollisions */
SMDL_API smdl_status smdl_collision_set_assume_world_space(smdl_collision *collision);                   /* $assumeworldspace */
SMDL_API smdl_status smdl_collision_set_root_bone(smdl_collision *collision, smdl_bone_id bone);         /* $rootbone */
/* $animatedfriction <min> <max> <time in> <time hold> <time out> */
SMDL_API smdl_status smdl_collision_set_animated_friction(smdl_collision *collision, int min_friction, int max_friction,
                                                          float time_in, float time_hold, float time_out);

/* -- joints ------------------------------------------------------------------------------------------------------ */

SMDL_API smdl_status smdl_collision_joint_skip(smdl_collision *collision, smdl_bone_id joint);           /* $jointskip */
/* $jointmerge <parent> <child>: the child's vertices are added to the parent's collision model. */
SMDL_API smdl_status smdl_collision_joint_merge(smdl_collision *collision, smdl_bone_id parent, smdl_bone_id child);
/* $jointconstrain <joint> <axis> <type> <min> <max> [<friction>]: min <= max. `friction` is the QC value (the QC default
 * is 1; the compile divides it by 5 like the QC). min/max are degrees and only matter for the limit type. */
SMDL_API smdl_status smdl_collision_joint_constrain(smdl_collision *collision, smdl_bone_id joint,
                                                    smdl_collision_axis axis, smdl_collision_joint_type type,
                                                    float min, float max, float friction);
SMDL_API smdl_status smdl_collision_joint_set_inertia(smdl_collision *collision, smdl_bone_id joint, float inertia);        /* $jointinertia */
SMDL_API smdl_status smdl_collision_joint_set_damping(smdl_collision *collision, smdl_bone_id joint, float damping);        /* $jointdamping */
SMDL_API smdl_status smdl_collision_joint_set_rot_damping(smdl_collision *collision, smdl_bone_id joint, float rot_damping); /* $jointrotdamping */
SMDL_API smdl_status smdl_collision_joint_set_mass_bias(smdl_collision *collision, smdl_bone_id joint, float mass_bias);    /* $jointmassbias */
/* $jointcollide <a> <b>: the two joints collide (the pair is added in this order). */
SMDL_API smdl_status smdl_collision_joint_collide(smdl_collision *collision, smdl_bone_id a, smdl_bone_id b);
/* $jointcollidealltoall { ... }: every ordered pair of different entries collide; at most 32 joints. */
SMDL_API smdl_status smdl_collision_joint_collide_all(smdl_collision *collision, const smdl_bone_id *joints, size_t count);
/* $jointnocollide <a> <b>: removes the pair in both directions. */
SMDL_API smdl_status smdl_collision_joint_no_collide(smdl_collision *collision, smdl_bone_id a, smdl_bone_id b);

#ifdef __cplusplus
}
#endif

#endif

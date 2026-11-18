/* studiomdl C API: bone dynamics: jiggle bones ($jigglebone) and procedural bones ($proceduralbones: quaternion
 * interpolation helpers with triggers, aim constraints, axis interpolation), typed and without a .vrd / procedural bones
 * file.
 *
 * Bones are ids of the model skeleton. Every object is created by one call and filled by setters; setters on a handle
 * of a part of the model act on the compile's data as the QC block would: the equivalent QC lists the same values.
 *
 * Jiggle bones: smdl_model_add_jiggle_bone creates the block for a bone (one per bone). A jiggle bone has up to three
 * sections: flexible (`is_flexible`), rigid (`is_rigid`) and base spring (`has_base_spring`); the parameters of a section
 * can be set once its section is enabled (the common ones, length / tip mass / constraints / friction / bounce, any
 * time); flexible and rigid cannot be combined. Values not set keep the QC defaults (length 10, stiffness 100, base
 * constraints +-100, everything else 0). Stiffness and damping values are clamped to 0..1000 by the compile, as the
 * QC does; angles are degrees.
 *
 * Procedural bones: the helpers are what a .vrd file lists. smdl_model_add_quat_interp makes a `<helper>` (bone, parent,
 * control parent, control bone) and its `<display>`, `<basepos>`, `<rotateaxis>`, `<jointorient>` and `<trigger>` lines
 * are setters / add_trigger. In a .vrd file `<basepos>`, `<rotateaxis>` and `<jointorient>` apply to the triggers that
 * follow them (and carry over from helper to helper); here a helper's values apply to all its triggers and start at
 * zero. smdl_model_add_aim_constraint_* make an `<aimconstraint>` (bone, parent, aim target: a bone or an attachment)
 * with its `<aimvector>`, `<upvector>`, `<basepos>`; the vectors are normalized. smdl_model_add_axis_interp makes a
 * `bone` entry of a procedural bones file (axis 1..3: x, y, z of the file) with up to six axis orientations
 * (position + angles); its position is `base position + position`.
 * $boneflexdriver is part of the flex API.
 */
#ifndef STUDIOMDL_C_SMDL_BONE_DYNAMICS_H
#define STUDIOMDL_C_SMDL_BONE_DYNAMICS_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct smdl_jiggle smdl_jiggle;
typedef struct smdl_quat_interp smdl_quat_interp;
typedef struct smdl_aim_constraint smdl_aim_constraint;
typedef struct smdl_axis_interp smdl_axis_interp;

/* -- $jigglebone ------------------------------------------------------------------------------------------------- */

/* One jiggle bone per bone (SMDL_ERROR_DUPLICATE_NAME for a second). */
SMDL_API smdl_status smdl_model_add_jiggle_bone(smdl_model *model, smdl_bone_id bone, smdl_jiggle **out_jiggle);
SMDL_API uint32_t smdl_model_jiggle_bone_count(const smdl_model *model);

/* Sections. is_rigid and is_flexible exclude each other (SMDL_ERROR_INVALID_ARGUMENT). Disabling a section resets
 * nothing: its parameters are ignored by the compile only when the section is off (they are not written). */
SMDL_API smdl_status smdl_jiggle_set_flexible(smdl_jiggle *jiggle, int enabled);
SMDL_API smdl_status smdl_jiggle_set_rigid(smdl_jiggle *jiggle, int enabled);
SMDL_API smdl_status smdl_jiggle_set_base_spring(smdl_jiggle *jiggle, int enabled);
/* allow_length_flex (flexible section). */
SMDL_API smdl_status smdl_jiggle_set_allow_length_flex(smdl_jiggle *jiggle, int enabled);

/* Every section */
SMDL_API smdl_status smdl_jiggle_set_length(smdl_jiggle *jiggle, float length);
SMDL_API smdl_status smdl_jiggle_set_tip_mass(smdl_jiggle *jiggle, float mass);
SMDL_API smdl_status smdl_jiggle_set_angle_constraint(smdl_jiggle *jiggle, float degrees);
SMDL_API smdl_status smdl_jiggle_set_yaw_constraint(smdl_jiggle *jiggle, float min_degrees, float max_degrees);
SMDL_API smdl_status smdl_jiggle_set_yaw_friction(smdl_jiggle *jiggle, float friction);
SMDL_API smdl_status smdl_jiggle_set_yaw_bounce(smdl_jiggle *jiggle, float bounce);
SMDL_API smdl_status smdl_jiggle_set_pitch_constraint(smdl_jiggle *jiggle, float min_degrees, float max_degrees);
SMDL_API smdl_status smdl_jiggle_set_pitch_friction(smdl_jiggle *jiggle, float friction);
SMDL_API smdl_status smdl_jiggle_set_pitch_bounce(smdl_jiggle *jiggle, float bounce);

/* is_flexible parameters (the section must be enabled) */
SMDL_API smdl_status smdl_jiggle_set_yaw_stiffness(smdl_jiggle *jiggle, float stiffness);
SMDL_API smdl_status smdl_jiggle_set_yaw_damping(smdl_jiggle *jiggle, float damping);
SMDL_API smdl_status smdl_jiggle_set_pitch_stiffness(smdl_jiggle *jiggle, float stiffness);
SMDL_API smdl_status smdl_jiggle_set_pitch_damping(smdl_jiggle *jiggle, float damping);
SMDL_API smdl_status smdl_jiggle_set_along_stiffness(smdl_jiggle *jiggle, float stiffness);
SMDL_API smdl_status smdl_jiggle_set_along_damping(smdl_jiggle *jiggle, float damping);

/* has_base_spring parameters (the section must be enabled) */
SMDL_API smdl_status smdl_jiggle_set_base_stiffness(smdl_jiggle *jiggle, float stiffness);
SMDL_API smdl_status smdl_jiggle_set_base_damping(smdl_jiggle *jiggle, float damping);
SMDL_API smdl_status smdl_jiggle_set_base_left_constraint(smdl_jiggle *jiggle, float min, float max);
SMDL_API smdl_status smdl_jiggle_set_base_left_friction(smdl_jiggle *jiggle, float friction);
SMDL_API smdl_status smdl_jiggle_set_base_up_constraint(smdl_jiggle *jiggle, float min, float max);
SMDL_API smdl_status smdl_jiggle_set_base_up_friction(smdl_jiggle *jiggle, float friction);
SMDL_API smdl_status smdl_jiggle_set_base_forward_constraint(smdl_jiggle *jiggle, float min, float max);
SMDL_API smdl_status smdl_jiggle_set_base_forward_friction(smdl_jiggle *jiggle, float friction);
SMDL_API smdl_status smdl_jiggle_set_base_mass(smdl_jiggle *jiggle, float mass);

/* -- quaternion interpolation (.vrd `<helper>`) ---------------------------------------------------------------------- */

/* `<helper> <bone> <parent> <control parent> <control>` */
SMDL_API smdl_status smdl_model_add_quat_interp(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                smdl_bone_id control_parent, smdl_bone_id control,
                                                smdl_quat_interp **out_interp);
SMDL_API uint32_t smdl_model_quat_interp_count(const smdl_model *model);
/* `<display> <size x y z> <distance>`: the percentage is distance / 100. */
SMDL_API smdl_status smdl_quat_interp_set_display(smdl_quat_interp *interp, const float size[3], float distance);
/* `<basepos>` (added to every trigger position), `<rotateaxis>` and `<jointorient>` (degrees, applied to every trigger's
 * orientation). */
SMDL_API smdl_status smdl_quat_interp_set_base_position(smdl_quat_interp *interp, const float position[3]);
SMDL_API smdl_status smdl_quat_interp_set_rotate_axis(smdl_quat_interp *interp, const float degrees[3]);
SMDL_API smdl_status smdl_quat_interp_set_joint_orient(smdl_quat_interp *interp, const float degrees[3]);
/* `<trigger> <tolerance> <trigger x y z> <angles x y z> <position x y z>`: all angles in degrees; at most 32 triggers
 * (SMDL_ERROR_LIMIT), the tolerance must be positive. */
SMDL_API smdl_status smdl_quat_interp_add_trigger(smdl_quat_interp *interp, float tolerance_degrees,
                                                  const float trigger_degrees[3], const float angles_degrees[3],
                                                  const float position[3]);

/* -- aim constraints (.vrd `<aimconstraint>`) ------------------------------------------------------------------------ */

/* The aim target is a bone, or an attachment (studiomdl looks the name up as an attachment first, so a bone is only
 * found when no attachment has its name). */
SMDL_API smdl_status smdl_model_add_aim_constraint_bone(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                        smdl_bone_id aim_bone, smdl_aim_constraint **out_aim);
SMDL_API smdl_status smdl_model_add_aim_constraint_attachment(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                              smdl_attachment_id aim_attachment,
                                                              smdl_aim_constraint **out_aim);
SMDL_API uint32_t smdl_model_aim_constraint_count(const smdl_model *model);
/* `<aimvector>`, `<upvector>` (both are required by the compile) and `<basepos>`. */
SMDL_API smdl_status smdl_aim_constraint_set_aim_vector(smdl_aim_constraint *aim, const float vector[3]);
SMDL_API smdl_status smdl_aim_constraint_set_up_vector(smdl_aim_constraint *aim, const float vector[3]);
SMDL_API smdl_status smdl_aim_constraint_set_base_position(smdl_aim_constraint *aim, const float position[3]);

/* -- axis interpolation ----------------------------------------------------------------------------------------- */

/* `bone "<bone>" "..." "<control>" "..." <axis>`: axis 1..3. */
SMDL_API smdl_status smdl_model_add_axis_interp(smdl_model *model, smdl_bone_id bone, smdl_bone_id control, int axis,
                                                smdl_axis_interp **out_interp);
SMDL_API uint32_t smdl_model_axis_interp_count(const smdl_model *model);
/* `basepos`: added to the position of every axis entry. */
SMDL_API smdl_status smdl_axis_interp_set_base_position(smdl_axis_interp *interp, const float position[3]);
/* `axis <index 0..5> <position> <angles>`: angles are a QAngle in degrees (pitch, yaw, roll). */
SMDL_API smdl_status smdl_axis_interp_set_orientation(smdl_axis_interp *interp, uint32_t index, const float position[3],
                                                      const float angles[3]);

#ifdef __cplusplus
}
#endif

#endif

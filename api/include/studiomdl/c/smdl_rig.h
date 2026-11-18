/* studiomdl C API: attachments, hitbox sets, hit groups, pose parameters, IK chains and locks, weight lists.
 *
 * Every object is referred to by the index id that its add function returns (ids count from 0 in creation order, per
 * kind), or looked up by name. Bones are ids of smdl_model_add_bone. Names are compared ignoring case.
 *
 * These objects are applied before the animations and sequences of the model (a command of an animation may name a
 * weight list or an IK chain, a sequence may name a pose parameter or an attachment); the creation order of a kind is
 * the order of the QC commands ($attachment, $poseparameter, $ikchain, $weightlist, ...).
 *
 * Call-time checks: null handles, ids out of range, duplicate names, non-finite values, name lengths and the limits
 * of the format (24 pose parameters, 16 IK chains, 16 autoplay locks, 128 weight lists, 256 bones per weight list).
 * The rest is reported by the compile, naming the object.
 */
#ifndef STUDIOMDL_C_SMDL_RIG_H
#define STUDIOMDL_C_SMDL_RIG_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------------------------ */
/* Attachments ($attachment, $redefineattachment) */

/* A new attachment on `bone` at `position` (units, before the model's $scale). name: unique (ignoring case), non-empty,
 * shorter than 128 bytes. The orientation starts as the identity; the functions below describe the attachment's
 * current definition (its latest revision), in the order of the QC options. */
SMDL_API smdl_status smdl_model_add_attachment(smdl_model *model, const char *name, smdl_bone_id bone,
                                               const float position[3], smdl_attachment_id *out_id);
/* $redefineattachment: starts a new revision of an existing attachment with a new bone and position; the compiler
 * re-populates the attachment from it (flags the attachment already has stay set, like in the QC). The orientation
 * functions below act on the new revision. */
SMDL_API smdl_status smdl_model_redefine_attachment(smdl_model *model, smdl_attachment_id attachment,
                                                    smdl_bone_id bone, const float position[3]);
SMDL_API smdl_status smdl_model_find_attachment(const smdl_model *model, const char *name,
                                                smdl_attachment_id *out_id);
SMDL_API uint32_t smdl_model_attachment_count(const smdl_model *model);

/* Orientation steps: each one replaces the orientation set by the step before it (the QC `absolute rotate ...`
 * rotates, `rotate ... absolute` is absolute). Angles are the QC's degrees (pitch, yaw, roll), euler the radians of an
 * SMD, quaternions x, y, z, w. */
SMDL_API smdl_status smdl_model_set_attachment_absolute(smdl_model *model, smdl_attachment_id attachment);  /* absolute */
SMDL_API smdl_status smdl_model_set_attachment_angles(smdl_model *model, smdl_attachment_id attachment,
                                                      const float angles[3]);                               /* rotate */
SMDL_API smdl_status smdl_model_set_attachment_euler(smdl_model *model, smdl_attachment_id attachment,
                                                     const float euler[3]);
SMDL_API smdl_status smdl_model_set_attachment_quaternion(smdl_model *model, smdl_attachment_id attachment,
                                                          const float rotation[4]);
/* x_and_z_axes: the matrix from an x axis and a z axis (both non-zero) */
SMDL_API smdl_status smdl_model_set_attachment_axes(smdl_model *model, smdl_attachment_id attachment,
                                                    const float x_axis[3], const float z_axis[3]);
SMDL_API smdl_status smdl_model_set_attachment_rigid(smdl_model *model, smdl_attachment_id attachment, int enabled);
SMDL_API smdl_status smdl_model_set_attachment_world_align(smdl_model *model, smdl_attachment_id attachment,
                                                           int enabled);                                    /* world_align */

/* ------------------------------------------------------------------------------------------------------------ */
/* Hitbox sets and hit groups ($hboxset, $hbox, $hgroup) */

/* name: unique (ignoring case), non-empty, shorter than 64 bytes. A model without hitbox sets gets the compiler's own
 * (from the bounding box); to name the default set "default" add it here. */
SMDL_API smdl_status smdl_model_add_hitbox_set(smdl_model *model, const char *name, smdl_hitbox_set_id *out_id);
SMDL_API smdl_status smdl_model_find_hitbox_set(const smdl_model *model, const char *name, smdl_hitbox_set_id *out_id);
SMDL_API uint32_t smdl_model_hitbox_set_count(const smdl_model *model);
/* $hbox: a box of hit group `group` on `bone`, corners in bone space (units, before $scale). out_index = the index of
 * the box inside its set. */
SMDL_API smdl_status smdl_model_add_hitbox(smdl_model *model, smdl_hitbox_set_id set, int group, smdl_bone_id bone,
                                           const float mins[3], const float maxs[3], uint32_t *out_index);
/* angles (degrees, the QC `$hbox ... <angles>`), capsule radius (>= 0 makes the box a capsule), box name */
SMDL_API smdl_status smdl_model_set_hitbox_angles(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox,
                                                  const float angles[3]);
SMDL_API smdl_status smdl_model_set_hitbox_capsule_radius(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox,
                                                          float radius);
SMDL_API smdl_status smdl_model_set_hitbox_name(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox,
                                                const char *name);
/* $hgroup: names `bone` as the bone of hit group `group` */
SMDL_API smdl_status smdl_model_add_hit_group(smdl_model *model, int group, smdl_bone_id bone);

/* ------------------------------------------------------------------------------------------------------------ */
/* Pose parameters ($poseparameter). A sequence's blend widens the range to cover its own range. */

typedef enum smdl_pose_loop_mode {
    SMDL_POSE_LOOP_NONE = 0,
    SMDL_POSE_LOOP_WRAP = 1,  /* wrap: the loop range is max - min */
    SMDL_POSE_LOOP_RANGE = 2  /* loop <range> */
} smdl_pose_loop_mode;

/* name: unique, non-empty, shorter than 128 bytes; min <= max */
SMDL_API smdl_status smdl_model_add_pose_parameter(smdl_model *model, const char *name, float min, float max,
                                                   smdl_pose_param_id *out_id);
/* The last call wins. range is only used with SMDL_POSE_LOOP_RANGE. */
SMDL_API smdl_status smdl_model_set_pose_parameter_loop(smdl_model *model, smdl_pose_param_id pose_parameter,
                                                        smdl_pose_loop_mode mode, float range);
SMDL_API smdl_status smdl_model_find_pose_parameter(const smdl_model *model, const char *name,
                                                    smdl_pose_param_id *out_id);
SMDL_API uint32_t smdl_model_pose_parameter_count(const smdl_model *model);

/* ------------------------------------------------------------------------------------------------------------ */
/* IK chains ($ikchain) and autoplay locks ($ikautoplaylock) */

/* A chain ending at `end_bone` (its two parents are the knee and the hip). name: unique, non-empty, shorter than 128
 * bytes. Defaults: axis Z, value 0, height 18, floor 0, pad 0. */
SMDL_API smdl_status smdl_model_add_ik_chain(smdl_model *model, const char *name, smdl_bone_id end_bone,
                                             smdl_ik_chain_id *out_id);
/* The motion type (one of SMDL_MOTION_X.. SMDL_MOTION_LZR of smdl_animation.h) and value the QC writes after the end
 * bone ("Z -1"). */
SMDL_API smdl_status smdl_model_set_ik_chain_axis(smdl_model *model, smdl_ik_chain_id chain, uint32_t motion,
                                                  float value);
SMDL_API smdl_status smdl_model_set_ik_chain_height(smdl_model *model, smdl_ik_chain_id chain, float height);
SMDL_API smdl_status smdl_model_set_ik_chain_floor(smdl_model *model, smdl_ik_chain_id chain, float floor);
SMDL_API smdl_status smdl_model_set_ik_chain_pad(smdl_model *model, smdl_ik_chain_id chain, float diameter);
SMDL_API smdl_status smdl_model_set_ik_chain_knee(smdl_model *model, smdl_ik_chain_id chain, const float direction[3]);
SMDL_API smdl_status smdl_model_set_ik_chain_center(smdl_model *model, smdl_ik_chain_id chain, const float center[3]);
SMDL_API smdl_status smdl_model_find_ik_chain(const smdl_model *model, const char *name, smdl_ik_chain_id *out_id);
SMDL_API uint32_t smdl_model_ik_chain_count(const smdl_model *model);

/* $ikautoplaylock: the chain is locked in every sequence that doesn't override it. out_index = the lock's index. */
SMDL_API smdl_status smdl_model_add_ik_autoplay_lock(smdl_model *model, smdl_ik_chain_id chain, float position_weight,
                                                     float local_rotation_weight, uint32_t *out_index);

/* ------------------------------------------------------------------------------------------------------------ */
/* Weight lists ($weightlist, $defaultweightlist). List 0 is the default list; the lists created here are 1, 2, ... */

#define SMDL_DEFAULT_WEIGHT_LIST ((smdl_weight_list_id)0)

/* name: unique, non-empty, shorter than 128 bytes */
SMDL_API smdl_status smdl_model_add_weight_list(smdl_model *model, const char *name, smdl_weight_list_id *out_id);
/* The weights of `bone` in the list: rotation weight and position weight (the QC's `"bone" 1 posweight 0.5`; without
 * posweight the QC uses the rotation weight). A position weight above 0 with a zero rotation weight is a compile
 * error. Entries keep the order of their first call; a second call for the same bone replaces its weights. */
SMDL_API smdl_status smdl_model_set_weight_list_bone(smdl_model *model, smdl_weight_list_id list, smdl_bone_id bone,
                                                     float weight, float position_weight);
SMDL_API smdl_status smdl_model_find_weight_list(const smdl_model *model, const char *name,
                                                 smdl_weight_list_id *out_id);
SMDL_API uint32_t smdl_model_weight_list_count(const smdl_model *model); /* including the default list */

#ifdef __cplusplus
}
#endif

#endif

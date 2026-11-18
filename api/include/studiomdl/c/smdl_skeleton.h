/* studiomdl C API: the model skeleton (bones, rest pose) and the per-bone options.
 *
 * One skeleton per model. Bones are referred to by smdl_bone_id (their index, in creation order); a parent must be
 * created before its children, so parent < child always holds (the order of an SMD "nodes" block).
 * A geometry may override the rest pose of single bones (smdl_geometry_set_rest_pose*).
 */
#ifndef STUDIOMDL_C_SMDL_SKELETON_H
#define STUDIOMDL_C_SMDL_SKELETON_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Adds a bone. name: unique in the model (compared ignoring case, like studiomdl does), non-empty, shorter than
 * 128 bytes. parent: SMDL_NO_BONE for a root, else an existing bone id. position: rest position local to the parent
 * (Source units). rotation: rest rotation local to the parent as a quaternion (x, y, z, w; normalized when it is not
 * of unit length); the euler form takes the angles in radians (x, y, z, the RadianEuler of SMD files, so
 * SMD data round-trips bit-exactly). Both forms store the same data: a quaternion is converted to the euler
 * angles the way the DMX loader converts bone transforms. At most 1024 bones.
 * Returns SMDL_ERROR_INVALID_ARGUMENT (null/empty name, non-finite value, zero quaternion),
 * SMDL_ERROR_OUT_OF_RANGE (unknown parent), SMDL_ERROR_DUPLICATE_NAME, SMDL_ERROR_LIMIT. out_id may be NULL. */
SMDL_API smdl_status smdl_model_add_bone(smdl_model *model, const char *name, smdl_bone_id parent,
                                         const float position[3], const float rotation[4], smdl_bone_id *out_id);
SMDL_API smdl_status smdl_model_add_bone_euler(smdl_model *model, const char *name, smdl_bone_id parent,
                                               const float position[3], const float euler[3], smdl_bone_id *out_id);

/* Looks a bone up by name (ignoring case). SMDL_ERROR_NOT_FOUND when there is none. */
SMDL_API smdl_status smdl_model_find_bone(const smdl_model *model, const char *name, smdl_bone_id *out_id);

SMDL_API uint32_t smdl_model_bone_count(const smdl_model *model);

/* ------------------------------------------------------------------------------------------------------------ */
/* Bone options and bone processing options.
 *
 * Bones are referred to by smdl_bone_id; an id that is not a bone of the model fails the call with
 * SMDL_ERROR_OUT_OF_RANGE (the message names the id and the bone count). studiomdl's option tables are name
 * based, so the compile resolves the ids to the bones' names; options naming a bone the compiled model does not
 * keep (no geometry or animation uses it and it is collapsed) are reported by the compile exactly as the QC
 * command would ("unknown bone ... in $limitrotation", ...).
 * The option setters of a bone can be called in any order. Every `enabled` is a C boolean (0/1); calling a setter
 * again replaces the earlier value of the same bone (the QC commands accumulate, the result is the same). Rotations
 * of calls with an _euler form are x, y, z radians like the add_bone euler form; the quaternion forms (x, y, z, w)
 * are converted the same way add_bone does. */

/* $root: the bone whose motion is the root motion (motion extraction, sequence origin). SMDL_NO_BONE clears it. */
SMDL_API smdl_status smdl_model_set_root_bone(smdl_model *model, smdl_bone_id bone);

/* $definebone: always keeps the bone in the compiled model with the given pose, even when no geometry or
 * animation uses it, and exempts it from bone collapsing. `parent` is SMDL_NO_BONE or a bone id (it should be
 * defined before `bone`: studiomdl looks the parent up among the bones it already has). position/rotation: the
 * pose local to the parent. Fails with SMDL_ERROR_DUPLICATE_NAME when the bone is already defined, and with
 * SMDL_ERROR_LIMIT beyond 1024 defined bones. The model skeleton (smdl_model_add_bone) already carries rest poses;
 * what $definebone adds is the explicit parent and pose that replace the geometry's pose of that bone (while it is
 * locked, see below), the keep/no-collapse behavior and the realign pose. Defined bones keep the order of the
 * calls (it is the order of the bones in the compiled model). */
SMDL_API smdl_status smdl_model_define_bone(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                            const float position[3], const float rotation[4]);
SMDL_API smdl_status smdl_model_define_bone_euler(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                  const float position[3], const float euler[3]);

/* locked/unlocked of a defined bone. Locked (the default unless smdl_model_set_unlock_define_bones): the defined
 * pose is installed before any geometry is processed, so it replaces the pose of the geometry. Unlocked: the pose
 * is only used when no geometry has the bone; a geometry's own pose wins. */
typedef enum smdl_define_bone_lock {
    SMDL_DEFINE_BONE_LOCK_DEFAULT = 0,  /* locked, or unlocked after smdl_model_set_unlock_define_bones(1) */
    SMDL_DEFINE_BONE_LOCKED = 1,
    SMDL_DEFINE_BONE_UNLOCKED = 2
} smdl_define_bone_lock;

/* SMDL_ERROR_NOT_FOUND when `bone` has not been defined with smdl_model_define_bone */
SMDL_API smdl_status smdl_model_set_define_bone_lock(smdl_model *model, smdl_bone_id bone, smdl_define_bone_lock lock);

/* The optional realign pose of a defined bone (the second position/angles group of $definebone): marks the bone
 * pre-aligned, its children are expressed relative to this pose and it is not realigned by $realignbones.
 * SMDL_ERROR_NOT_FOUND when `bone` has not been defined. */
SMDL_API smdl_status smdl_model_set_define_bone_realign(smdl_model *model, smdl_bone_id bone, const float position[3],
                                                        const float rotation[4]);
SMDL_API smdl_status smdl_model_set_define_bone_realign_euler(smdl_model *model, smdl_bone_id bone,
                                                              const float position[3], const float euler[3]);

/* $unlockdefinebones (and the -overridedefinebones command line switch): defined bones are unlocked unless one
 * says otherwise (smdl_model_set_define_bone_lock). Affects every bone defined in the model. */
SMDL_API smdl_status smdl_model_set_unlock_define_bones(smdl_model *model, int enabled);

/* $lockbonelengths: keep the bone lengths (positions) of the defined bones whatever the animation does */
SMDL_API smdl_status smdl_model_set_lock_bone_lengths(smdl_model *model, int enabled);

/* $realignbones: rotate each bone that has exactly one child (and is not pre-aligned) so that its X axis points at
 * that child, when the child is not already on its +X axis. */
SMDL_API smdl_status smdl_model_set_realign_bones(smdl_model *model, int enabled);

/* $forcerealign: concatenates the rotation to the bone's pose, after $realignbones. Calling it again for the same
 * bone composes the rotations in call order, like repeating the QC command. At most 256 in a model. */
SMDL_API smdl_status smdl_model_add_force_realign(smdl_model *model, smdl_bone_id bone, const float rotation[4]);
SMDL_API smdl_status smdl_model_add_force_realign_euler(smdl_model *model, smdl_bone_id bone, const float euler[3]);

/* $collapsebones / $collapsebonesaggressive: bones that nothing uses (no vertex, animation, attachment, IK chain,
 * procedural bone or bone merge) are removed and their vertices go to the parent. Aggressive: bones are filtered by
 * their own use flags instead of the use flags of their descendants. Aggressive implies collapsing. Defined bones
 * ($definebone) are never collapsed. */
SMDL_API smdl_status smdl_model_set_collapse_bones(smdl_model *model, int enabled);
SMDL_API smdl_status smdl_model_set_collapse_bones_aggressive(smdl_model *model, int enabled);

/* $alwayscollapse: collapse this bone whenever possible. Enabling it for a bone turns collapsing on. */
SMDL_API smdl_status smdl_model_set_always_collapse(smdl_model *model, smdl_bone_id bone, int enabled);

/* $controller: a bone controller (a runtime value that moves or rotates a bone). input: 0-3 user controllers,
 * SMDL_BONE_CONTROLLER_MOUTH (4) the mouth. type: axis of the motion. start/end: values at the ends of the
 * input range (degrees for rotation, units for translation); a rotation whose start and end are the same angle
 * wraps around the shortest way. The controller's index (the order of the calls, from 0) is returned through
 * out_index (may be NULL). Fails with SMDL_ERROR_INVALID_ARGUMENT (input > 4, bad type, non-finite value) and
 * SMDL_ERROR_LIMIT (more than 1024 controllers). A bone has at most one controller per axis (the compile keeps
 * the last). */
#define SMDL_BONE_CONTROLLER_MOUTH 4u
typedef enum smdl_bone_controller_type {
    SMDL_BONE_CONTROLLER_X = 0,
    SMDL_BONE_CONTROLLER_Y = 1,
    SMDL_BONE_CONTROLLER_Z = 2,
    SMDL_BONE_CONTROLLER_XR = 3, /* rotation about X */
    SMDL_BONE_CONTROLLER_YR = 4,
    SMDL_BONE_CONTROLLER_ZR = 5
} smdl_bone_controller_type;

SMDL_API smdl_status smdl_model_add_bone_controller(smdl_model *model, uint32_t input, smdl_bone_id bone,
                                                    smdl_bone_controller_type type, float start, float end,
                                                    uint32_t *out_index);

/* $screenalign: the bone always faces the screen: sphere (free) or cylinder (constrained by the bone's own axis).
 * SMDL_SCREEN_ALIGN_NONE removes the option. */
typedef enum smdl_screen_align_mode {
    SMDL_SCREEN_ALIGN_NONE = 0,
    SMDL_SCREEN_ALIGN_SPHERE = 1,
    SMDL_SCREEN_ALIGN_CYLINDER = 2
} smdl_screen_align_mode;

SMDL_API smdl_status smdl_model_set_screen_align(smdl_model *model, smdl_bone_id bone, smdl_screen_align_mode mode);

/* $worldalign: the bone is rigidly aligned to the world (it still translates) */
SMDL_API smdl_status smdl_model_set_world_align(smdl_model *model, smdl_bone_id bone, int enabled);

/* $bonemerge: the bone (and its parents) stay available for bone merging by other models */
SMDL_API smdl_status smdl_model_set_bone_merge(smdl_model *model, smdl_bone_id bone, int enabled);

/* $bonealwayssetup: the bone is always set up at runtime, even when no vertex or attachment uses it */
SMDL_API smdl_status smdl_model_set_bone_always_setup(smdl_model *model, smdl_bone_id bone, int enabled);

/* $limitrotation: lets the bone rotate more than 180 but less than 360 degrees by aligning it to the middle of
 * its range of motion. With sequence_count 0 the middle is computed over every animation of the model. With
 * sequences (names of sequences of this model, copied; they are not looked up) studiomdl keeps the bone's rest
 * rotation as the alignment: the per-sequence ranges of the QC command are not implemented by the compiler, which
 * only checks that the list is not empty. Replaces an earlier setting of the bone. */
SMDL_API smdl_status smdl_model_set_limit_rotation(smdl_model *model, smdl_bone_id bone,
                                                   const char *const *sequences, size_t sequence_count);

/* $bonesaveframe: which components of the bone are stored for the zero frame (the single frame that animations
 * keep per bone besides the per-frame data). flags: a combination of SMDL_BONE_SAVE_*; 0 lists the bone without
 * saving anything. Once any bone is listed the compiler no longer picks the bones automatically (root bones and
 * bones with a long enough position range), only the listed ones are saved. Replaces an earlier setting of the
 * bone. */
typedef enum smdl_bone_save_frame_flags {
    SMDL_BONE_SAVE_POSITION = 1,
    SMDL_BONE_SAVE_ROTATION = 2,
    SMDL_BONE_SAVE_ROTATION64 = 4
} smdl_bone_save_frame_flags;

SMDL_API smdl_status smdl_model_set_bone_save_frame(smdl_model *model, smdl_bone_id bone, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif

/* studiomdl C API: animations (the $animation QC command): frames, options, commands, IK rules.
 *
 * An animation is the data of one animation SMD: `frame_count` frames of local bone poses at `fps`. Bone poses are
 * given per bone (smdl_animation_set_bone_frames*); a bone without data keeps its rest pose in every frame (the pose
 * of the model skeleton, or the per-animation override of smdl_animation_set_rest_pose*). Positions are in Source
 * units (the model's $scale applies, like the positions of an SMD), rotations are quaternions (x, y, z, w) or the
 * RadianEuler angles of an SMD in radians (the _euler forms).
 *
 * Options and commands map one to one onto the QC tokens of $animation. The commands run in the order they were
 * added (QC: the order of the tokens), motion extraction flags (LX, LY, ...) and options are order independent.
 * Animations are applied in creation order; an animation a command refers to (subtract, align, match, ...) must have
 * been created BEFORE the animation that refers to it (the compiler processes animations in that order: a command
 * whose reference comes later is a compile error naming the animation). To get the usual animation name of a
 * sequence made from a single SMD, name the animation "@<sequence name>" (what a $sequence with an SMD file creates);
 * an animation and a sequence must not have the same name.
 *
 * Call-time checks: null handles, ids and handles of another model, non-finite values, wrong array lengths, frames
 * outside the animation (SMDL_ERROR_OUT_OF_RANGE: a frame of a command is checked against the frame count of the
 * animation it belongs to, a frame of the reference animation against the reference's), bones out of range, limits
 * (at most 3000 animations, 5000 frames, 64 commands per animation, 64 IK rules). The rest is reported by the
 * compile, naming the animation.
 *
 * $append / $continue are further calls on the same animation (and sequence); $prepend is
 * smdl_animation_move_command (move the command that was just added to the front).
 */
#ifndef STUDIOMDL_C_SMDL_ANIMATION_H
#define STUDIOMDL_C_SMDL_ANIMATION_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct smdl_ik_rule smdl_ik_rule;

/* Motion types: the controls of motion extraction (X Y Z XR YR ZR LX LY LZ LXR LYR LZR LM LQ) and of the align /
 * walk commands. A combination (bitwise or) of these. */
#define SMDL_MOTION_X 0x00000001u
#define SMDL_MOTION_Y 0x00000002u
#define SMDL_MOTION_Z 0x00000004u
#define SMDL_MOTION_XR 0x00000008u
#define SMDL_MOTION_YR 0x00000010u
#define SMDL_MOTION_ZR 0x00000020u
#define SMDL_MOTION_LX 0x00000040u
#define SMDL_MOTION_LY 0x00000080u
#define SMDL_MOTION_LZ 0x00000100u
#define SMDL_MOTION_LXR 0x00000200u
#define SMDL_MOTION_LYR 0x00000400u
#define SMDL_MOTION_LZR 0x00000800u
#define SMDL_MOTION_LINEAR 0x00001000u    /* LM */
#define SMDL_MOTION_QUADRATIC 0x00002000u /* LQ */

/* ------------------------------------------------------------------------------------------------------------ */
/* Creation */

/* name: unique among animations (compared ignoring case) and not the name of a sequence, non-empty, shorter than 128
 * bytes. fps > 0. frame_count 1..5000. */
SMDL_API smdl_status smdl_model_create_animation(smdl_model *model, const char *name, float fps, uint32_t frame_count,
                                                 smdl_animation **out_animation);
SMDL_API smdl_status smdl_model_find_animation(const smdl_model *model, const char *name, smdl_animation **out_animation);
SMDL_API uint32_t smdl_model_animation_count(const smdl_model *model);
SMDL_API smdl_status smdl_model_get_animation(const smdl_model *model, uint32_t index, smdl_animation **out_animation);

SMDL_API const char *smdl_animation_get_name(const smdl_animation *animation);
SMDL_API float smdl_animation_get_fps(const smdl_animation *animation);
SMDL_API uint32_t smdl_animation_get_frame_count(const smdl_animation *animation);

/* ------------------------------------------------------------------------------------------------------------ */
/* Frames */

/* The pose of `bone` in every frame. positions: frame_count vectors (x, y, z), rotations: frame_count quaternions
 * (x, y, z, w) (smdl_animation_set_bone_frames) or frame_count euler triples in radians (..._euler). Either array
 * may be NULL with count 0: that part keeps the rest pose. Counts must be 0 or frame_count; the arrays follow the
 * (data, count, stride_bytes) convention. Calling it again for the same bone replaces its data. */
SMDL_API smdl_status smdl_animation_set_bone_frames(smdl_animation *animation, smdl_bone_id bone,
                                                    const float *positions, size_t position_count,
                                                    size_t position_stride, const float *rotations,
                                                    size_t rotation_count, size_t rotation_stride);
SMDL_API smdl_status smdl_animation_set_bone_frames_euler(smdl_animation *animation, smdl_bone_id bone,
                                                          const float *positions, size_t position_count,
                                                          size_t position_stride, const float *eulers,
                                                          size_t euler_count, size_t euler_stride);

/* The rest pose of `bone` for this animation: used for the frames of a bone without data (or without positions or
 * rotations) instead of the pose of the model skeleton. */
SMDL_API smdl_status smdl_animation_set_rest_pose(smdl_animation *animation, smdl_bone_id bone,
                                                  const float position[3], const float rotation[4]);
SMDL_API smdl_status smdl_animation_set_rest_pose_euler(smdl_animation *animation, smdl_bone_id bone,
                                                        const float position[3], const float euler[3]);

/* ------------------------------------------------------------------------------------------------------------ */
/* Options ($animation tokens). `enabled` arguments are C booleans. */

SMDL_API smdl_status smdl_animation_set_fps(smdl_animation *animation, float fps);                       /* fps */
SMDL_API smdl_status smdl_animation_set_origin(smdl_animation *animation, const float origin[3]);       /* origin */
/* rotate <yaw>: degrees; like the QC option it adds the 90 degrees of the default up axis */
SMDL_API smdl_status smdl_animation_set_rotate(smdl_animation *animation, float degrees);
/* angles <x> <y> <z>: degrees (the z angle gets the same +90 as rotate) */
SMDL_API smdl_status smdl_animation_set_angles(smdl_animation *animation, float x, float y, float z);
SMDL_API smdl_status smdl_animation_set_scale(smdl_animation *animation, float scale);                  /* scale */
SMDL_API smdl_status smdl_animation_set_loop(smdl_animation *animation, int enabled);                   /* loop */
SMDL_API smdl_status smdl_animation_set_no_force_loop(smdl_animation *animation, int enabled);          /* noforceloop */
/* startloop <frame> / percentstartloop <percent>: loop restart; they exclude each other and imply loop */
SMDL_API smdl_status smdl_animation_set_start_loop(smdl_animation *animation, int frame);
SMDL_API smdl_status smdl_animation_set_percent_start_loop(smdl_animation *animation, float percent);
SMDL_API smdl_status smdl_animation_set_fudge_loop(smdl_animation *animation, int enabled);             /* fudgeloop */
SMDL_API smdl_status smdl_animation_set_snap(smdl_animation *animation, int enabled);                   /* snap */
SMDL_API smdl_status smdl_animation_set_post(smdl_animation *animation, int enabled);                   /* post */
SMDL_API smdl_status smdl_animation_set_auto_ik(smdl_animation *animation, int enabled);                /* autoik / noautoik */
SMDL_API smdl_status smdl_animation_set_motion_rollback(smdl_animation *animation, float rollback);     /* motionrollback */
SMDL_API smdl_status smdl_animation_set_no_anim_block(smdl_animation *animation, int enabled);          /* noanimblock */
SMDL_API smdl_status smdl_animation_set_no_anim_block_stall(smdl_animation *animation, int enabled);    /* noanimblockstall */
SMDL_API smdl_status smdl_animation_set_no_stall_frames(smdl_animation *animation, float frames);       /* nostallframes */
/* frame <first> <last> (inclusive, clamped to the animation's frames) / framestart <first> (to the last frame) */
SMDL_API smdl_status smdl_animation_set_frame_range(smdl_animation *animation, int first_frame, int last_frame);
SMDL_API smdl_status smdl_animation_set_frame_start(smdl_animation *animation, int first_frame);
/* blockname <name>: the name of the animation's block of frames ("BindPose" by default, what an SMD calls it). Only
 * useful to say which block a frame range refers to; it must be a valid name (non-empty). */
SMDL_API smdl_status smdl_animation_set_block_name(smdl_animation *animation, const char *name);
/* Motion extraction: a combination of SMDL_MOTION_* (the QC tokens X Y Z XR YR ZR LX LY LZ LXR LYR LZR LM LQ) */
SMDL_API smdl_status smdl_animation_set_motion_extraction(smdl_animation *animation, uint32_t motion_flags);

/* ------------------------------------------------------------------------------------------------------------ */
/* Commands (ParseCmdlistToken). Every function appends one command. `reference` animations must belong to the same
 * model. SMDL_ERROR_LIMIT when the animation already has 64 commands. */

SMDL_API smdl_status smdl_animation_add_fixup_loop(smdl_animation *animation, int first_frame, int last_frame); /* fixuploop */
/* weightlist <name>: the bone weights of the animation from here on (id of smdl_model_add_weight_list) */
SMDL_API smdl_status smdl_animation_add_weight_list(smdl_animation *animation, smdl_weight_list_id weight_list);
/* subtract (post = 1) / presubtract (post = 0): subtract frame `frame` of `reference` (a delta animation) */
SMDL_API smdl_status smdl_animation_add_subtract(smdl_animation *animation, const smdl_animation *reference, int frame,
                                                 int post);
/* align / alignto: align the root motion (motion_flags, e.g. X|Y) of frame dest_frame with frame src_frame of
 * `reference`. alignto = SMDL_MOTION_X | SMDL_MOTION_Y at frame 0 of both. */
SMDL_API smdl_status smdl_animation_add_align(smdl_animation *animation, const smdl_animation *reference,
                                              uint32_t motion_flags, int src_frame, int dest_frame);
SMDL_API smdl_status smdl_animation_add_align_to(smdl_animation *animation, const smdl_animation *reference);
/* alignbone / alignboneto: the same for a bone other than the root */
SMDL_API smdl_status smdl_animation_add_align_bone(smdl_animation *animation, smdl_bone_id bone,
                                                   const smdl_animation *reference, uint32_t motion_flags,
                                                   int src_frame, int dest_frame);
SMDL_API smdl_status smdl_animation_add_align_bone_to(smdl_animation *animation, smdl_bone_id bone,
                                                      const smdl_animation *reference);
SMDL_API smdl_status smdl_animation_add_match(smdl_animation *animation, const smdl_animation *reference);  /* match */
/* matchblend: match frame dest_frame to frame src_frame of reference, blending in from dest_pre to dest_post */
SMDL_API smdl_status smdl_animation_add_match_blend(smdl_animation *animation, const smdl_animation *reference,
                                                    int src_frame, int dest_frame, int dest_pre, int dest_post);
/* worldspaceblend / worldspaceblendloop (start_frame) */
SMDL_API smdl_status smdl_animation_add_world_space_blend(smdl_animation *animation, const smdl_animation *reference);
SMDL_API smdl_status smdl_animation_add_world_space_blend_loop(smdl_animation *animation,
                                                               const smdl_animation *reference, int start_frame);
SMDL_API smdl_status smdl_animation_add_rotate_to(smdl_animation *animation, float degrees);               /* rotateto */

/* ikrule / ikfixup: see smdl_ik_rule below. `chain` is an id of smdl_model_add_ik_chain. The rule stays valid until
 * the model is destroyed; its parameters can be set after the call (the command only records the rule). */
typedef enum smdl_ik_rule_type {
    SMDL_IK_RULE_AUTOSTEPS = 0,  /* autosteps <end frame> <bone>: a ground rule over the whole animation */
    SMDL_IK_RULE_TOUCH = 1,      /* touch <bone> */
    SMDL_IK_RULE_FOOTSTEP = 2,
    SMDL_IK_RULE_ATTACHMENT = 3, /* attachment <name> */
    SMDL_IK_RULE_RELEASE = 4,
    SMDL_IK_RULE_UNLATCH = 5
} smdl_ik_rule_type;

SMDL_API smdl_status smdl_animation_add_ik_rule(smdl_animation *animation, smdl_ik_chain_id chain,
                                                smdl_ik_rule_type type, smdl_ik_rule **out_rule);
SMDL_API smdl_status smdl_animation_add_ik_fixup(smdl_animation *animation, smdl_ik_chain_id chain,
                                                 smdl_ik_rule_type type, smdl_ik_rule **out_rule);

/* walkframe: motion over the first end_frame frames (motion_flags). walkalign / walkalignto: align that motion with
 * reference frame ref_frame (walkalignto: ref_frame 0, src_frame = end_frame). */
SMDL_API smdl_status smdl_animation_add_walk_frame(smdl_animation *animation, int end_frame, uint32_t motion_flags);
SMDL_API smdl_status smdl_animation_add_walk_align(smdl_animation *animation, int end_frame,
                                                   const smdl_animation *reference, uint32_t motion_flags,
                                                   int ref_frame, int src_frame);
SMDL_API smdl_status smdl_animation_add_walk_align_to(smdl_animation *animation, int end_frame,
                                                      const smdl_animation *reference, uint32_t motion_flags);
SMDL_API smdl_status smdl_animation_add_derivative(smdl_animation *animation, float scale);           /* derivative */
SMDL_API smdl_status smdl_animation_add_no_animation(smdl_animation *animation);                      /* noanimation */
SMDL_API smdl_status smdl_animation_add_no_animation_keep_duration(smdl_animation *animation);        /* noanim_keepduration */
SMDL_API smdl_status smdl_animation_add_linear_delta(smdl_animation *animation);                      /* lineardelta */
SMDL_API smdl_status smdl_animation_add_spline_delta(smdl_animation *animation);                      /* splinedelta */
SMDL_API smdl_status smdl_animation_add_compress(smdl_animation *animation, int frames);              /* compress */
SMDL_API smdl_status smdl_animation_add_num_frames(smdl_animation *animation, int frames);            /* numframes */
SMDL_API smdl_status smdl_animation_add_counter_rotate(smdl_animation *animation, smdl_bone_id bone);  /* counterrotate */
/* counterrotateto <pitch> <yaw> <roll> <bone>: degrees */
SMDL_API smdl_status smdl_animation_add_counter_rotate_to(smdl_animation *animation, smdl_bone_id bone, float pitch,
                                                          float yaw, float roll);
/* localhierarchy <bone> <parent> [range <start> <peak> <tail> <end>]: parent SMDL_NO_BONE for none; frames,
 * -1 = unset (".." in the QC). Without a range pass -1 for all four. */
SMDL_API smdl_status smdl_animation_add_local_hierarchy(smdl_animation *animation, smdl_bone_id bone,
                                                        smdl_bone_id parent, int start, int peak, int tail, int end);
/* forceboneposrot <bone> [pos x y z] [rot x y z [local]]: position (units) and/or rotation (degrees, the QC
 * angles); pass NULL to leave one out */
SMDL_API smdl_status smdl_animation_add_force_bone_pos_rot(smdl_animation *animation, smdl_bone_id bone,
                                                           const float position[3], const float angles[3],
                                                           int rotation_is_local);
/* bonedriver <bone> [axis x|y|z] [value v] [range ...]: axis 0..2; with use_range = 0 the value applies to every
 * frame, else it is blended in with start/peak/tail/end */
SMDL_API smdl_status smdl_animation_add_bone_driver(smdl_animation *animation, smdl_bone_id bone, int axis,
                                                    float value, int use_range, int start, int peak, int tail,
                                                    int end);
SMDL_API smdl_status smdl_animation_add_reverse(smdl_animation *animation);                           /* reverse */
SMDL_API smdl_status smdl_animation_add_append_animation(smdl_animation *animation, const smdl_animation *reference); /* appendanim */

SMDL_API uint32_t smdl_animation_command_count(const smdl_animation *animation);
/* $prepend: moves the command at index `from` to index `to` (the commands in between shift by one) */
SMDL_API smdl_status smdl_animation_move_command(smdl_animation *animation, uint32_t from, uint32_t to);

/* ------------------------------------------------------------------------------------------------------------ */
/* IK rules (Option_IKRule). Frames are animation frames; -1 stands for ".." (unset) in ranges. Everything not set
 * keeps the default of the rule type (the chain's height, floor and pad for ground rules). */

SMDL_API smdl_status smdl_ik_rule_set_bone(smdl_ik_rule *rule, smdl_bone_id bone);         /* touch, autosteps */
SMDL_API smdl_status smdl_ik_rule_set_attachment(smdl_ik_rule *rule, smdl_attachment_id attachment); /* attachment */
SMDL_API smdl_status smdl_ik_rule_set_autosteps_end(smdl_ik_rule *rule, int end_frame);    /* autosteps <end> */
SMDL_API smdl_status smdl_ik_rule_set_range(smdl_ik_rule *rule, int start, int peak, int tail, int end); /* range */
SMDL_API smdl_status smdl_ik_rule_set_height(smdl_ik_rule *rule, float height);            /* height */
SMDL_API smdl_status smdl_ik_rule_set_target(smdl_ik_rule *rule, int slot);                /* target <slot> */
SMDL_API smdl_status smdl_ik_rule_set_floor(smdl_ik_rule *rule, float floor);              /* floor */
SMDL_API smdl_status smdl_ik_rule_set_pad(smdl_ik_rule *rule, float diameter);             /* pad: the radius is half */
SMDL_API smdl_status smdl_ik_rule_set_radius(smdl_ik_rule *rule, float radius);            /* radius */
SMDL_API smdl_status smdl_ik_rule_set_contact(smdl_ik_rule *rule, int contact);            /* contact */
SMDL_API smdl_status smdl_ik_rule_set_use_sequence(smdl_ik_rule *rule, int enabled);       /* usesequence */
SMDL_API smdl_status smdl_ik_rule_set_use_source(smdl_ik_rule *rule, int enabled);         /* usesource */
SMDL_API smdl_status smdl_ik_rule_set_fake_origin(smdl_ik_rule *rule, const float origin[3]);   /* fakeorigin */
/* fakerotate: quaternion / euler radians / the QC's degrees (pitch, yaw, roll) */
SMDL_API smdl_status smdl_ik_rule_set_fake_rotation(smdl_ik_rule *rule, const float rotation[4]);
SMDL_API smdl_status smdl_ik_rule_set_fake_rotation_euler(smdl_ik_rule *rule, const float euler[3]);
SMDL_API smdl_status smdl_ik_rule_set_fake_angles(smdl_ik_rule *rule, const float angles[3]);

#ifdef __cplusplus
}
#endif

#endif

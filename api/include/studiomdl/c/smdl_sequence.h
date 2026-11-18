/* studiomdl C API: sequences (the $sequence QC command): animations or blend grids, activities, events, layers,
 * transitions, keyvalues.
 *
 * A sequence plays one animation (smdl_sequence_add_animation once) or a grid of blend animations (width x height
 * animations, the first axis varying fastest, like the QC's list of animations with `blendwidth`). The options that the
 * QC accepts after the animation of a $sequence and that belong to the animation (fps, loop, LX, commands, ...) are set
 * on the animation itself (smdl_animation.h): a sequence is looping when its animations are. Animations are created
 * before the sequences that use them; sequences are applied in creation order after the animations.
 *
 * The same calls give $append and $continue (more of the same calls on the sequence) and, with
 * smdl_animation_move_command, $prepend. Sequences are objects of the model from creation: $declaresequence is not
 * needed, a layer or a transition may name a sequence created later.
 *
 * Call-time checks: null handles, handles and ids of another model, unknown ids, non-finite values, name lengths
 * (shorter than 128 bytes, event options shorter than 64) and limits (1524 sequences, 1024 events, 1024 animation tags,
 * 128 activity modifiers, 64 layers, 64 IK locks, 2 blend axes). The rest (a sequence without animations, a
 * non-square blend grid, an unknown activity name, ...) is reported by the compile, naming the sequence.
 */
#ifndef STUDIOMDL_C_SMDL_SEQUENCE_H
#define STUDIOMDL_C_SMDL_SEQUENCE_H

#include "smdl_common.h"
#include "smdl_animation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------------------------ */
/* Creation */

/* name: unique among sequences (compared ignoring case) and not the name of an animation, non-empty, shorter than 128
 * bytes. */
SMDL_API smdl_status smdl_model_create_sequence(smdl_model *model, const char *name, smdl_sequence **out_sequence);
SMDL_API smdl_status smdl_model_find_sequence(const smdl_model *model, const char *name, smdl_sequence **out_sequence);
SMDL_API uint32_t smdl_model_sequence_count(const smdl_model *model);
SMDL_API smdl_status smdl_model_get_sequence(const smdl_model *model, uint32_t index, smdl_sequence **out_sequence);

SMDL_API const char *smdl_sequence_get_name(const smdl_sequence *sequence);

/* ------------------------------------------------------------------------------------------------------------ */
/* Animations and blends */

/* The next animation of the sequence (the blend animations in the order of the QC list). */
SMDL_API smdl_status smdl_sequence_add_animation(smdl_sequence *sequence, const smdl_animation *animation);
/* blendwidth: the size of the first axis of the grid; the animation count must be a multiple of it. Without it the
 * sequence has 1-3 blends in a row or a square grid. */
SMDL_API smdl_status smdl_sequence_set_blend_width(smdl_sequence *sequence, uint32_t width);
/* blendref / blendcomp / blendcenter: the animations that make the blend relative (reference pose, compare pose,
 * center) */
SMDL_API smdl_status smdl_sequence_set_blend_reference(smdl_sequence *sequence, const smdl_animation *animation);
SMDL_API smdl_status smdl_sequence_set_blend_compare(smdl_sequence *sequence, const smdl_animation *animation);
SMDL_API smdl_status smdl_sequence_set_blend_center(smdl_sequence *sequence, const smdl_animation *animation);
/* blend <pose parameter> <start> <end>: drives a blend axis of the grid. The first call is the first axis, the second
 * call the second (at most two axes together with calc_blend); it widens the pose parameter's range to cover
 * start..end. */
SMDL_API smdl_status smdl_sequence_add_blend(smdl_sequence *sequence, smdl_pose_param_id pose_parameter, float start,
                                             float end);
/* calcblend <pose parameter> <attachment> <control>: the pose parameter is calculated from the motion of the
 * attachment; control = one SMDL_MOTION_* flag */
SMDL_API smdl_status smdl_sequence_add_calc_blend(smdl_sequence *sequence, smdl_pose_param_id pose_parameter,
                                                  smdl_attachment_id attachment, uint32_t control);
/* posecycle: the cycle of the sequence is taken from a pose parameter */
SMDL_API smdl_status smdl_sequence_set_pose_cycle(smdl_sequence *sequence, smdl_pose_param_id pose_parameter);

/* ------------------------------------------------------------------------------------------------------------ */
/* Activity, events, tags */

/* activity <name> <weight>: weight is an integer > 0 (a zero weight is a compile error). With the compile's list of
 * allowed activity names (smdl_model_add_allowed_activity_name, if any) the name must be one of them. */
SMDL_API smdl_status smdl_sequence_set_activity(smdl_sequence *sequence, const char *name, int weight);
/* activitymodifier <name> */
SMDL_API smdl_status smdl_sequence_add_activity_modifier(smdl_sequence *sequence, const char *name);
/* event <name> <frame> [options]: frame of the sequence's first animation (>= 0); name is an event name or number
 * (AE_..., "5004"), options may be NULL */
SMDL_API smdl_status smdl_sequence_add_event(smdl_sequence *sequence, const char *name, int frame, const char *options);
/* The same placed by cycle (0..1 over the first animation): converted to the nearest frame of the sequence's first
 * animation when the model is compiled (cycle * (frames - 1), rounded). */
SMDL_API smdl_status smdl_sequence_add_event_cycle(smdl_sequence *sequence, const char *name, float cycle,
                                                   const char *options);
/* animtag <name> <cycle> */
SMDL_API smdl_status smdl_sequence_add_anim_tag(smdl_sequence *sequence, const char *name, float cycle);

/* ------------------------------------------------------------------------------------------------------------ */
/* Timing and flags */

SMDL_API smdl_status smdl_sequence_set_fade_in(smdl_sequence *sequence, float seconds);       /* fadein, default $defaultfadein */
SMDL_API smdl_status smdl_sequence_set_fade_out(smdl_sequence *sequence, float seconds);      /* fadeout, default $defaultfadeout */
SMDL_API smdl_status smdl_sequence_set_exit_phase(smdl_sequence *sequence, float phase);      /* exitphase */

/* Sequence flags (the QC tokens of the same name; delta, worldspace and worldrelative also set post). */
#define SMDL_SEQUENCE_SNAP 0x00000001u           /* snap */
#define SMDL_SEQUENCE_DELTA 0x00000002u          /* delta: Delta | Post */
#define SMDL_SEQUENCE_PRE_DELTA 0x00000004u      /* predelta: Delta */
#define SMDL_SEQUENCE_WORLD_SPACE 0x00000008u    /* worldspace: World | Post */
#define SMDL_SEQUENCE_WORLD_RELATIVE 0x00000010u /* worldrelative: WorldAndRelative | Post */
#define SMDL_SEQUENCE_POST 0x00000020u           /* post */
#define SMDL_SEQUENCE_AUTOPLAY 0x00000040u       /* autoplay */
#define SMDL_SEQUENCE_REALTIME 0x00000080u       /* realtime */
#define SMDL_SEQUENCE_HIDDEN 0x00000100u         /* hidden */
/* Sets or clears every flag in `flags` (a combination of SMDL_SEQUENCE_*; an unknown bit is an error). */
SMDL_API smdl_status smdl_sequence_set_flags(smdl_sequence *sequence, uint32_t flags, int enabled);
/* rootdriver <bone> */
SMDL_API smdl_status smdl_sequence_set_root_driver(smdl_sequence *sequence, smdl_bone_id bone);

/* ------------------------------------------------------------------------------------------------------------ */
/* Transition graph */

SMDL_API smdl_status smdl_sequence_set_node(smdl_sequence *sequence, const char *node);   /* node <name> */
/* transition <from> <to> (reverse = 0) / rtransition (reverse = 1) */
SMDL_API smdl_status smdl_sequence_set_transition(smdl_sequence *sequence, const char *from, const char *to,
                                                  int reverse);

/* ------------------------------------------------------------------------------------------------------------ */
/* Layers and IK locks. Layers are applied in the order they were added (addlayer / blendlayer mixed). */

/* addlayer <sequence> [local] */
SMDL_API smdl_status smdl_sequence_add_layer(smdl_sequence *sequence, const smdl_sequence *layer, int local);

/* blendlayer flags */
#define SMDL_LAYER_XFADE 0x00000001u    /* xfade */
#define SMDL_LAYER_SPLINE 0x00000002u   /* spline */
#define SMDL_LAYER_NO_BLEND 0x00000004u /* noblend */
#define SMDL_LAYER_LOCAL 0x00000008u    /* local */
#define SMDL_LAYER_POSE 0x00000010u     /* poseparameter <name>: blend with a pose parameter (needs pose_parameter) */
/* blendlayer <sequence> <start> <peak> <tail> <end> [flags]: frames of this sequence's first animation.
 * pose_parameter is only used with SMDL_LAYER_POSE (else pass 0). */
SMDL_API smdl_status smdl_sequence_add_blend_layer(smdl_sequence *sequence, const smdl_sequence *layer, float start,
                                                   float peak, float tail, float end, uint32_t flags,
                                                   smdl_pose_param_id pose_parameter);
/* iklock <chain> <position weight> <local rotation weight> */
SMDL_API smdl_status smdl_sequence_add_ik_lock(smdl_sequence *sequence, smdl_ik_chain_id chain, float position_weight,
                                               float local_rotation_weight);

/* ------------------------------------------------------------------------------------------------------------ */
/* Keyvalues */

/* keyvalues { <text> }: the text between the braces, parsed like the QC block (a syntax error fails the call with
 * SMDL_ERROR_SYNTAX); calls append. */
SMDL_API smdl_status smdl_sequence_add_keyvalues(smdl_sequence *sequence, const char *text);

#ifdef __cplusplus
}
#endif

#endif

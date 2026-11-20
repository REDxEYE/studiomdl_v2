/* studiomdl C API: flex controllers, flexes, rules, local vars, eyeballs, eyelids, mouths, bone flex drivers.
 *
 * Flexes are made from the shapes of a mesh's geometry (smdl_shapes.h). The mesh calls (smdl_mesh_add_flex, eyeballs,
 * eyelids, sphere normals, ...) are the options of the `$model` block; the compile applies them in call order, then the
 * model-wide parts in the order local variables, mouths, controllers, rules, bone flex drivers. Flex indices (the order
 * of the flexes in the MDL) therefore are: the flexes of every mesh in mesh order and call order, then the local
 * variables, then the mouths' flexes. A QC that must give the same MDL puts its `localvar`, `mouth`, `flexcontroller`
 * and `%rule` lines at the end of the last `$model` block (after the eyelids), `localvar` before `mouth`, and
 * `noautodmxrules` before any `flexcontroller`.
 *
 * Names (flexes, controllers) are compared case-insensitively like the QC does. A name is known to the model as soon
 * as the call that defines it returned: flexes by smdl_mesh_add_flex/_pair (a pair defines `<name>R` and `<name>L`),
 * smdl_mesh_add_eyelid (`<name>`, `<name>_lowerer`, `<name>_neutral`, `<name>_raiser`), smdl_mesh_add_dmx_eyelid
 * (`upper_right`, `upper_right_lowerer`, ... and the same with `_left`; `lower_` for a lower lid),
 * smdl_model_add_local_var and smdl_model_set_mouth. Rules refer to names defined before the rule is added.
 *
 * Rule expressions are the right-hand side of a QC `%flex = <expression>` line: numbers, controller names, `%flex`
 * (the value of a flex or local variable), `+ - * /`, unary minus, parentheses, `min(a, b)` and `max(a, b)`. They are
 * parsed by studiomdl's own parser when the rule is added: SMDL_ERROR_SYNTAX for a malformed expression (the message
 * names the problem and quotes the expression), SMDL_ERROR_NOT_FOUND for an unknown controller or flex. The whole
 * expression must be on one line without comments (`;`, `#`, `//`, `/ *`).
 *
 * Eyeballs are per mesh (at most 4): index 0, 1, ... in call order. Eyelids refer to them by that index.
 * The `$model` option `attachment` does nothing in studiomdl (attachments are smdl_model_add_attachment).
 */
#ifndef STUDIOMDL_C_SMDL_FLEX_H
#define STUDIOMDL_C_SMDL_FLEX_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum smdl_bone_flex_component {
    SMDL_BONE_FLEX_TX = 0, /* translate X */
    SMDL_BONE_FLEX_TY = 1,
    SMDL_BONE_FLEX_TZ = 2
} smdl_bone_flex_component;

typedef enum smdl_eyelid_kind {
    SMDL_EYELID_UPPER = 0, /* drives the upper lid of its eyeballs */
    SMDL_EYELID_LOWER = 1
} smdl_eyelid_kind;

typedef enum smdl_eyelid_state {
    SMDL_EYELID_LOWERER = 0,
    SMDL_EYELID_NEUTRAL = 1,
    SMDL_EYELID_RAISER = 2
} smdl_eyelid_state;

/* ---------------------------------------------------------------------------------------------------------------
 * Model-wide
 * ------------------------------------------------------------------------------------------------------------- */

/* `flexcontroller <group> range <min> <max> <name>`: a slider. `group` is the controller's type (QC: "default",
 * "eyes", ...). Names are unique (SMDL_ERROR_DUPLICATE_NAME); SMDL_ERROR_LIMIT at 128 controllers or a name of 128
 * characters or more. */
SMDL_API smdl_status smdl_model_add_flex_controller(smdl_model *model, const char *group, const char *name, float min,
                                                    float max, smdl_flex_controller_id *out_id);
SMDL_API smdl_status smdl_model_find_flex_controller(const smdl_model *model, const char *name,
                                                     smdl_flex_controller_id *out_id);
SMDL_API uint32_t smdl_model_flex_controller_count(const smdl_model *model);

/* `localvar <name>`: a flex without vertex data that rules can read (`%name`). A name that exists already returns that
 * flex's id. */
SMDL_API smdl_status smdl_model_add_local_var(smdl_model *model, const char *name, smdl_flex_id *out_id);

/* Flexes known to the model, ids in order of definition (see the header comment). */
SMDL_API smdl_status smdl_model_find_flex(const smdl_model *model, const char *name, smdl_flex_id *out_id);
SMDL_API uint32_t smdl_model_flex_count(const smdl_model *model);

/* `%<flex> = <expression>`: SMDL_ERROR_NOT_FOUND when `flex` is not a flex of the model yet,
 * SMDL_ERROR_DUPLICATE_NAME when it has a rule already, SMDL_ERROR_SYNTAX / SMDL_ERROR_NOT_FOUND for the
 * expression (see the header comment), SMDL_ERROR_LIMIT beyond 1024 rules. */
SMDL_API smdl_status smdl_model_add_flex_rule(smdl_model *model, const char *flex, const char *expression);
/* Same, the target flex by id (smdl_model_find_flex, smdl_mesh_get_flex_ids, smdl_model_add_local_var). */
SMDL_API smdl_status smdl_model_add_flex_rule_id(smdl_model *model, smdl_flex_id flex, const char *expression);

/* `mouth <index> <flex> <bone> <x> <y> <z>`: the flex (defined when it does not exist) opens the mouth by the
 * bone's direction `forward` (in the bone's space). Setting an index again replaces it. index < 64. */
SMDL_API smdl_status smdl_model_set_mouth(smdl_model *model, uint32_t index, const char *flex, smdl_bone_id bone,
                                          const float forward[3]);

/* `$boneflexdriver <bone> <tx|ty|tz> <controller> <min> <max>`: the bone's translation follows the controller.
 * The same bone and controller again replace the earlier call. */
SMDL_API smdl_status smdl_model_add_bone_flex_driver(smdl_model *model, smdl_bone_id bone,
                                                     smdl_bone_flex_component component,
                                                     smdl_flex_controller_id controller, float min, float max);

/* ---------------------------------------------------------------------------------------------------------------
 * Per mesh (the options of the `$model` block). `shape` arguments are indices of the mesh geometry's shapes
 * (smdl_geometry_add_shape); SMDL_NO_ID is the base shape where noted. The geometry must have the shapes before.
 * ------------------------------------------------------------------------------------------------------------- */

/* `eyeball <name> <bone> <x> <y> <z> <material> <diameter> <z angle> <iris material> <pupil scale>`: position in model
 * space, material = the mesh material of the eyeball (a material of the geometry). At most 4 per mesh. */
SMDL_API smdl_status smdl_mesh_add_eyeball(smdl_mesh *mesh, const char *name, smdl_bone_id bone,
                                           const float position[3], const char *material, float diameter,
                                           float z_angle_degrees, float pupil_scale, uint32_t *out_eyeball);

/* `flex <name> frame <n>`: a flex from a shape (SMDL_NO_ID: the base shape). Defaults: position 1, decay 1, no split.
 * Several consecutive calls with the same name make one multi-position flex (set the positions). out_flex_key (may be
 * NULL) is the key index in this mesh, for the setters below. SMDL_ERROR_LIMIT beyond 512 flex keys in the model
 * or 1024 flexes. */
SMDL_API smdl_status smdl_mesh_add_flex(smdl_mesh *mesh, const char *name, uint32_t shape, uint32_t *out_flex_key);

/* `flexpair <name> <split> frame <n>`: the same, as the stereo pair `<name>R` / `<name>L` split at x = `split`
 * (non-zero). */
SMDL_API smdl_status smdl_mesh_add_flex_pair(smdl_mesh *mesh, const char *name, float split, uint32_t shape,
                                             uint32_t *out_flex_key);

SMDL_API smdl_status smdl_mesh_set_flex_position(smdl_mesh *mesh, uint32_t flex_key, float position); /* `position` */
SMDL_API smdl_status smdl_mesh_set_flex_decay(smdl_mesh *mesh, uint32_t flex_key, float decay);       /* `decay` */
/* `split` (replaces the split of a pair) */
SMDL_API smdl_status smdl_mesh_set_flex_split(smdl_mesh *mesh, uint32_t flex_key, float split);
/* The flexes a flex key defines: a `flex` defines one (*out_left = SMDL_NO_ID); a `flexpair` defines two, the right
 * half `<name>R` in *out_flex and the left half `<name>L` in *out_left. Either out pointer may be NULL. */
SMDL_API smdl_status smdl_mesh_get_flex_ids(const smdl_mesh *mesh, uint32_t flex_key, smdl_flex_id *out_flex,
                                            smdl_flex_id *out_left);

/* `defaultflex frame <n>`: the model's default pose is this shape (SMDL_NO_ID: the base shape). Once per model
 * (SMDL_ERROR_DUPLICATE_NAME). */
SMDL_API smdl_status smdl_mesh_set_default_flex(smdl_mesh *mesh, uint32_t shape);

/* `eyelid <name> <vta> lowerer <frame> <target> neutral <frame> <target> raiser <frame> <target> [split <s>] [eyeball
 * <name>]`: the three states are set with smdl_mesh_set_eyelid_state (all required). The eyelid drives the upper or
 * the lower lid of its eyeball (see smdl_mesh_set_eyelid_eyeball). A QC equivalent names it so that the name starts
 * with `u` (upper) or `l` (lower). */
SMDL_API smdl_status smdl_mesh_add_eyelid(smdl_mesh *mesh, const char *name, smdl_eyelid_kind kind,
                                          uint32_t *out_eyelid);
/* shape: SMDL_NO_ID = the base shape (QC frame 0); target: the lid's height, within +-radius of the eyeball. */
SMDL_API smdl_status smdl_mesh_set_eyelid_state(smdl_mesh *mesh, uint32_t eyelid, smdl_eyelid_state state,
                                                uint32_t shape, float target);
SMDL_API smdl_status smdl_mesh_set_eyelid_split(smdl_mesh *mesh, uint32_t eyelid, float split);
/* `eyeball <name>`: only this eyeball (index of smdl_mesh_add_eyeball); SMDL_NO_ID = every eyeball (the default). */
SMDL_API smdl_status smdl_mesh_set_eyelid_eyeball(smdl_mesh *mesh, uint32_t eyelid, uint32_t eyeball);

/* `dmxeyelid <upper|lower> <dmx> lowerer <state> <target> neutral ... raiser ... righteyeball <name> lefteyeball
 * <name>`: the three states are shapes (all required, no base shape) built as DMX delta states of the mesh's own
 * source. The flexes are `upper_right` etc. (see the header comment); the right and left eyeballs get the right and
 * the left flex. */
SMDL_API smdl_status smdl_mesh_add_dmx_eyelid(smdl_mesh *mesh, smdl_eyelid_kind kind, uint32_t right_eyeball,
                                              uint32_t left_eyeball, uint32_t *out_dmx_eyelid);
SMDL_API smdl_status smdl_mesh_set_dmx_eyelid_state(smdl_mesh *mesh, uint32_t dmx_eyelid, smdl_eyelid_state state,
                                                    uint32_t shape, float target);

/* `spherenormals <material> <x> <y> <z>`: the normals of the vertices of that material point away from the position
 * (model space). */
SMDL_API smdl_status smdl_mesh_add_sphere_normals(smdl_mesh *mesh, const char *material, const float position[3]);

/* `noautodmxrules`: no automatic controllers and rules for the DMX flexes of the source. Clears the controllers
 * defined before it is applied (mesh calls run before the model's controllers). */
SMDL_API smdl_status smdl_mesh_set_no_auto_dmx_rules(smdl_mesh *mesh, int enabled);

/* ---------------------------------------------------------------------------------------------------------------
 * DMX combination operator (the DmeCombinationOperator of a DMX model file) and flex rules of a mesh
 *
 * What the DMX loader builds from the delta states of the mesh and the combination operator, built from the shapes of
 * the mesh's geometry (smdl_shapes.h): the first call below gives the mesh a combination operator. The compile then
 * does exactly what loading a DMX file with the same data does, before any other option of the mesh is applied:
 *   - every shape of the geometry is a delta state: a flex named like the shape, `<name>R` and `<name>L` when the
 *     shape involves a stereo raw control (see below). The shapes are new-style vertex animations of the mesh's source,
 *     weighted left/right by the geometry's balance (smdl_geometry_set_balance);
 *   - a control is a flex controller `name`, driven by one or more raw controls: 1 raw control = slider 0..1, 2 =
 *     slider -1..1 (the first raw control is the negative side), 3 or more = an n-way slider -1..1 plus a
 *     `multi_<name>` controller; stereo controls make `right_<name>` and `left_<name>` instead of `<name>`;
 *   - a shape named `a_b_c` is the corrective shape of the combination of the raw controls a, b and c (matched
 *     case-insensitively): its flex follows the product of them. A single raw control's name makes a plain flex.
 *     A shape whose name has a part that is no raw control gets no rule (the flex exists, nothing drives it);
 *   - a domination rule {dominators} / {suppressed} is given to the DMX tools' combination logic as is: the compiled
 *     rule of a combined shape gets a domination operation over the dominators' values (see
 *     CDmeCombinationOperator::FindDominators in libs/movieobjects, whose selection of the affected shapes is the DMX
 *     loader's and is reused unchanged);
 *   - the flex rules of these flexes are the ones the DMX loader generates for the controls (they are not generated
 *     with smdl_mesh_set_no_auto_dmx_rules), and the controllers are added in the order of the controls, before the
 *     controllers of the model-wide parts.
 * Raw control and control names must be unique (case-insensitive), a raw control belongs to one control. The flexes of
 * a combination are not in the model's flex list (smdl_model_find_flex) and have no ids: name them in expressions.
 * Wrinkle scales of the DMX controls are not supported; give the shapes wrinkle values (smdl_geometry_set_shape_wrinkle).
 *
 * "Advanced flex" (DmeFlexRules, the DMX route's flex rule text): as soon as one of the smdl_mesh_add_combination_
 * expression / _pass_through / _local_var calls is made, the combination operator targets flex rules instead of
 * the corrective shapes, exactly like the DMX loader: the controls only add their flex controllers (range from
 * smdl_mesh_set_combination_control_range, default 0..1, type "default"; stereo, eyelid, raw controls, dominators
 * and corrective shape names are not used for rules), then each rule in call order defines its flex (a pass-through
 * `x` reads the controller `x`) and parses its expression with the rules' parser (names resolve against the flexes
 * and controllers defined before: controllers of all controls and rules before it, flexes of earlier meshes and
 * rules). The shapes still become flexes (stereo-ness decided by the controls) after that.
 * ------------------------------------------------------------------------------------------------------------- */

/* A control with `raw_control_count` raw controls (none empty). The calls need no shapes yet. A control without raw
 * controls only adds its flex controller for "advanced flex" rules (the DMX loader accepts it only when every target
 * is a DmeFlexRules); the compile reports it when the mesh has no flex rules. out_control (may
 * be NULL) is the index of the control in this mesh. SMDL_ERROR_DUPLICATE_NAME for a name or raw control used
 * already, SMDL_ERROR_LIMIT for names of 122 characters or more. */
SMDL_API smdl_status smdl_mesh_add_combination_control(smdl_mesh *mesh, const char *name,
                                                       const char *const *raw_controls, size_t raw_control_count,
                                                       int stereo, uint32_t *out_control);

/* Makes the control an eyelid control (needs exactly two raw controls: the lower lid's, then the upper lid's); the
 * controllers `blink` and `multi_<name>` are added and the flex controller `eyes_up_down_flex` (NULL or "" =
 * "eyes_updown") makes the lids follow the eyes. */
SMDL_API smdl_status smdl_mesh_set_combination_control_eyelid(smdl_mesh *mesh, uint32_t control,
                                                              const char *eyes_up_down_flex);

/* The range of the control's flex controller (DMX flexMin / flexMax, default 0..1). Only used by "advanced flex"
 * (see above); the other controllers get their range from the number of raw controls. */
SMDL_API smdl_status smdl_mesh_set_combination_control_range(smdl_mesh *mesh, uint32_t control, float min, float max);

/* A domination rule: both lists name raw controls (checked by the compile) and must not be empty. */
SMDL_API smdl_status smdl_mesh_add_combination_dominator(smdl_mesh *mesh, const char *const *dominators,
                                                         size_t dominator_count, const char *const *suppressed,
                                                         size_t suppressed_count);

/* Flex rules of "advanced flex". The expression is the right-hand side of a QC `%flex = <expression>` (see the
 * header comment): SMDL_ERROR_SYNTAX for a malformed one (names are resolved by the compile),
 * SMDL_ERROR_DUPLICATE_NAME for a flex that has a rule (or a local variable) in this mesh already; a local variable and
 * the expression that gives it its value may share a name. */
SMDL_API smdl_status smdl_mesh_add_combination_expression(smdl_mesh *mesh, const char *flex, const char *expression);
/* The flex's rule is the controller of the same name. */
SMDL_API smdl_status smdl_mesh_add_combination_pass_through(smdl_mesh *mesh, const char *flex);
/* A flex without vertex data (`localvar`). */
SMDL_API smdl_status smdl_mesh_add_combination_local_var(smdl_mesh *mesh, const char *name);

/* ---------------------------------------------------------------------------------------------------------------
 * Vertex cache animation (`$model` option `vcafile`)
 * ------------------------------------------------------------------------------------------------------------- */

/* `vcafile <vta>`: the shapes (indices of the mesh geometry's shapes, strided like the other arrays; at least one) are
 * the frames 1, 2, ... of a vertex cache animation whose frame 0 is the base pose. The compile makes the flexes `default`
 * (the base pose) and `f0`, `f1`, ... (one per shape), one n-way flex controller `name` (range -1..1, with its
 * `multi_<name>` controller; the QC takes the name from the VTA file's base name) that plays the frames as it
 * goes from -1 to 1, and the rule of every frame flex: a vertex animation driven by one slider. Like the QC option it
 * must be the only flex option of the model: SMDL_ERROR_INVALID_ARGUMENT when the mesh has any other flex item or
 * combination, or already has a vertex cache animation; the compile reports flexes of other meshes. The shapes
 * must be listed in the order of the frames. */
SMDL_API smdl_status smdl_mesh_add_vertex_cache_animation(smdl_mesh *mesh, const char *name, const uint32_t *shapes,
                                                          size_t shape_count, size_t shapes_stride_bytes);

#ifdef __cplusplus
}
#endif

#endif

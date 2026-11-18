// Apply functions for the flex QC commands ($model options flexcontroller, flex, flexpair, defaultflex, localvar, %rule,
// eyeball, eyelid, dmxeyelid, mouth, spherenormals, noautodmxrules, and $boneflexdriver), shared by the QC parsers
// (studiomdl_commands.cpp) and the builder API front end (api/src/apply_flex.cpp): the parser turns tokens into typed
// parameters, these apply them to StudioMdlState.
//
// Errors the QC parsers reported with qc.Error are returned as the message text ("" = success, the text ends with a
// line end like the original messages): the parser reports it through qc.Error, the builder through MdlError. Errors
// that were MdlError already (too many flex descs) throw as before.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <string_view>

#include "studiomdl/studiomdl_global_state.hpp"

class ScriptTokenizer;

// ---------------------------------------------------------------------------------------------------------------
// Flex rules: `%<flex> = <expression>`
// ---------------------------------------------------------------------------------------------------------------

// How the expression parser resolves names: the index of a flex (`%name` in an expression, and the target of the rule)
// or of a flex controller, -1 when unknown. The QC path resolves against ctx.flex; the builder's call-time check
// against the names of its description (there is no StudioMdlState then).
struct FlexRuleLookup {
    std::function<int(std::string_view)> flex;
    std::function<int(std::string_view)> controller;
};

// Parses `= <expression>` from the tokenizer (the first token is the `=`, skipped) into `rule` (rule.flex = flexdesc,
// the operators in postfix order). flexName is the flex's name as the table stores it (messages). Syntax and lookup
// errors go to qc.Error. The expression runs to the end of the line; a trailing backslash continues it on the next.
void ParseFlexRule(ScriptTokenizer &qc, std::string_view flexName, int flexdesc, const FlexRuleLookup &lookup,
                   s_flexrule_t &rule);

// ---------------------------------------------------------------------------------------------------------------
// Controllers, local variables, mouths
// ---------------------------------------------------------------------------------------------------------------

// `flexcontroller <type> range <min> <max> <name>`: one controller
std::string ApplyFlexController(StudioMdlState &ctx, std::string_view type, std::string_view name, float min,
                                float max);

// `localvar <name>`: a flex (flexdesc) without vertex data that rules can use
void ApplyLocalVar(StudioMdlState &ctx, std::string_view name);

// `mouth <index> <flex> <bone> <x> <y> <z>`; the bone is looked up when the model is simplified
void ApplyMouth(StudioMdlState &ctx, size_t index, std::string_view flex, std::string_view bone,
                const Vector &forward);

// ---------------------------------------------------------------------------------------------------------------
// Flexes (vertex animation keys)
// ---------------------------------------------------------------------------------------------------------------

// `flex <name> frame <n> position <p> split <s> decay <d>` / `flexpair <name> <split> ...` / `defaultflex frame <n>`
struct FlexKeyParams {
    std::string_view name;     // the flexdesc; stereo: <name>R and <name>L
    bool stereo = false;       // flexpair
    float split = 0.0f;        // `split`; for a flexpair the value given after the name unless `split` follows
    int frame = 0;             // the frame of the vertex animation (old style: the VTA frame)
    float position = 1.0f;     // target1
    float decay = 1.0f;
};

// Adds the key for vertex animation `animation` of `source` (old style: the first animation of the VTA source, empty
// when it has none). Consecutive keys of one name are chained into a multi-position flex like the QC parser did.
// Returns the error text.
std::string ApplyFlexKey(StudioMdlState &ctx, const FlexKeyParams &params, s_source_t *source, int imodel);

// `defaultflex`: a key named "default" that is also the model's default pose (ctx.flex.defaultflexkey)
std::string ApplyDefaultFlexKey(StudioMdlState &ctx, const FlexKeyParams &params, s_source_t *source, int imodel);

// ---------------------------------------------------------------------------------------------------------------
// Eyeballs and eyelids
// ---------------------------------------------------------------------------------------------------------------

// `eyeball <name> <bone> <x> <y> <z> <material> <diameter> <z angle> <iris material (unused)> <pupil scale>`
struct EyeballParams {
    std::string_view name;
    std::string_view bone;      // a bone of the model's source
    Vector position;            // model space
    std::string_view material;  // a material of the model's source: the mesh of the eyeball
    float diameter = 0.0f;
    float zAngleDegrees = 0.0f;
    float pupilScale = 1.0f;
};

std::string ApplyEyeball(StudioMdlState &ctx, s_model_t *pmodel, const EyeballParams &params);

// Which lid of an eyeball an eyelid drives: the QC derives it from the first letter of the eyelid's name
enum class EyelidKind { Upper, Lower, Other };

// The flexdesc an eyelid creates with every call: the base `type` and `type_lowerer` etc. (QC: in the order the
// options appear)
int AddEyelidFlexdesc(StudioMdlState &ctx, std::string_view name);

// `eyelid <type> <vta> lowerer <frame> <target> neutral ... raiser ... [split <s>] [eyeball <name>]`. The flexdescs are
// added by AddEyelidFlexdesc before (baseDesc for `type`, the others for type_lowerer / type_neutral / type_raiser).
// Index 0 = lowerer, 1 = neutral, 2 = raiser.
struct EyelidParams {
    std::string_view type;
    EyelidKind kind = EyelidKind::Other;
    int baseDesc = 0;
    std::array<int, 3> desc{};
    std::array<int, 3> frame{};
    std::array<float, 3> target{};
    float split = 0.0f;
    std::string_view eyeball; // empty: every eyeball of the model
};

std::string ApplyEyelid(StudioMdlState &ctx, int imodel, const EyelidParams &params, s_source_t *source);

// `dmxeyelid <upper|lower> <dmx> lowerer <animation> <target> neutral ... raiser ... righteyeball <name> lefteyeball
// <name>`: the three states are new-style (delta state) vertex animations of `source`, by name.
struct DmxEyelidParams {
    bool upper = true;
    std::array<std::string_view, 3> animation{}; // lowerer, neutral, raiser: names of animations of the source
    std::array<float, 3> target{};
    std::string_view rightEyeball;
    std::string_view leftEyeball;
};

std::string ApplyDmxEyelid(StudioMdlState &ctx, int imodel, const DmxEyelidParams &params, s_source_t *source);

// The new-style (delta state) vertex animation of that name in the source, null when there is none
const s_sourceanim_t *FindNewStyleVertexAnim(const s_source_t *source, std::string_view name);

// ---------------------------------------------------------------------------------------------------------------
// Source modifiers
// ---------------------------------------------------------------------------------------------------------------

// `spherenormals <material> <x> <y> <z>`: the normals of the vertices of the material point away from the position
std::string ApplySphereNormals(StudioMdlState &ctx, s_source_t *source, std::string_view material, const Vector &pos);

// `noautodmxrules`: no automatic controllers/rules for the DMX flexes of the source (also clears the controllers
// defined so far)
void ApplyNoAutoDmxRules(StudioMdlState &ctx, s_source_t *source);

// ---------------------------------------------------------------------------------------------------------------
// $boneflexdriver <bone> <tx|ty|tz> <controller> <min> <max>
// ---------------------------------------------------------------------------------------------------------------

// component: STUDIO_BONE_FLEX_TX / TY / TZ
std::string ApplyBoneFlexDriver(StudioMdlState &ctx, std::string_view bone, int component, std::string_view controller,
                                float min, float max);

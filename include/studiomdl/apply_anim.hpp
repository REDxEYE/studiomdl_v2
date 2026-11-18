// Apply functions for the animation family of QC commands ($animation options and commands, $sequence options,
// $poseparameter, $ikchain, $ikautoplaylock, $attachment, $hbox/$hboxset/$hgroup, $weightlist), shared by the QC
// parsers (studiomdl_commands.cpp) and the builder API front end (api/src/apply_{animation,sequence,rig}.cpp): the
// parser turns tokens into typed parameters, these apply them to StudioMdlState (or to the animation / sequence being
// built). Bones, attachments and the like are referred to by name; they are looked up when the model is simplified,
// so an unknown bone is reported there exactly as for the QC commands. Errors are MdlError.
//
// The pointers inside s_animcmd_t / s_weightlist_t (bone names, rules) point into ctx.arena, which lives as long as
// the compile.
#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "studiomdl/studiomdl_global_state.hpp"

// A NUL-terminated copy of `name` that lives as long as the compile (stable address)
const char *InternName(StudioMdlState &ctx, std::string_view name);

class ScriptTokenizer;

// While alive, the errors of the apply functions that the QC commands always reported with the script position
// (limits, duplicate names, activity weight, frame range, ...) go through that tokenizer: "file(line): - message".
// ParseScript puts one around every command; the builder front end has none and reports with MdlError.
class ScopedQcPosition {
public:
    explicit ScopedQcPosition(const ScriptTokenizer &qc);
    ~ScopedQcPosition();
    ScopedQcPosition(const ScopedQcPosition &) = delete;
    ScopedQcPosition &operator=(const ScopedQcPosition &) = delete;

private:
    const ScriptTokenizer *m_Previous;
};

// ---------------------------------------------------------------------------------------------------------------
// $poseparameter <name> [<min> [<max>]] [wrap | loop <range>]
// An existing parameter of that name is updated (the QC creates parameters on first use, also from $sequence blend).
struct PoseParameterParams {
    std::string_view name;
    std::optional<float> min;
    std::optional<float> max;
    enum class Loop { None, Wrap, Value } loop = Loop::None; // the last of wrap / loop <range> wins
    float loopValue = 0.0f;
};

// Returns the parameter's index
int ApplyPoseParameter(StudioMdlState &ctx, const PoseParameterParams &params);

// ---------------------------------------------------------------------------------------------------------------
// $ikchain <name> <end bone> [<axis> <value>] [height <h>] [pad <diameter>] [floor <f>] [knee <x> <y> <z>] [center <x> <y> <z>]
struct IkChainParams {
    std::string_view name;
    std::string_view boneName;
    StudioMotionFlags axis = StudioMotionFlags::Z;
    float value = 0.0f;
    float height = 18.0f;
    float floor = 0.0f;
    float radius = 0.0f; // `pad` is the diameter: the parser halves it
    std::optional<Vector> knee;   // link[0].kneeDir
    std::optional<Vector> center;
};

// Index of the chain named `name` (ignoring case), -1 if there is none
int FindIkChain(const StudioMdlState &ctx, std::string_view name);

// False (after printing "duplicate ikchain ... ignored", unless -quiet) when a chain of that name already exists:
// the later definition is ignored.
bool ApplyIkChain(StudioMdlState &ctx, const IkChainParams &params);

// $ikautoplaylock <chain> <position weight> <local rotation weight>
void ApplyIkAutoplayLock(StudioMdlState &ctx, std::string_view chain, float positionWeight, float localQWeight);

// ---------------------------------------------------------------------------------------------------------------
// $attachment / $redefineattachment <name> <bone> <x> <y> <z> [absolute] [rigid] [world_align] [rotate <x> <y> <z>]
// [x_and_z_axes <x axis> <z axis>]. The local orientation steps run in order, each replaces the matrix of the one
// before it (the QC options act in token order: `absolute rotate ...` rotates, `rotate ... absolute` is absolute).
struct AttachmentLocal {
    enum class Kind { Absolute, Angles, RadianEuler, Quaternion, XZAxes } kind = Kind::Angles;
    QAngle angles{0, 0, 0};              // degrees (the QC `rotate`)
    ::RadianEuler euler{0, 0, 0};        // radians
    Quaternion quaternion{0, 0, 0, 1};
    Vector xAxis{1, 0, 0};
    Vector zAxis{0, 0, 1};
};

struct AttachmentParams {
    std::string_view name;
    std::string_view boneName;
    Vector position{0, 0, 0}; // before $scale
    bool rigid = false;
    bool worldAlign = false;
    std::vector<AttachmentLocal> locals;
};

// redefine: the index of the attachment to re-populate (flags are added to the ones it has, like the QC command);
// otherwise a new attachment is appended
void ApplyAttachment(StudioMdlState &ctx, const AttachmentParams &params, std::optional<size_t> redefine = std::nullopt);

// ---------------------------------------------------------------------------------------------------------------
// $hboxset <name>, $hbox <group> <bone> <min> <max> [<angles> [<capsule radius> [<name>]]], $hgroup <group> <bone>
void ApplyHitboxSet(StudioMdlState &ctx, std::string_view name);

struct HitboxParams {
    int group = 0;
    std::string_view boneName;
    Vector mins{0, 0, 0}; // before $scale
    Vector maxs{0, 0, 0};
    QAngle angles{0, 0, 0}; // degrees
    float capsuleRadius = -1.0f; // < 0: no capsule
    std::string_view name;       // hitbox name, may be empty
};

// Adds the hitbox to the last set; creates the "default" set first when there is none
void ApplyHitbox(StudioMdlState &ctx, const HitboxParams &params);
void ApplyHitGroup(StudioMdlState &ctx, int group, std::string_view boneName);

// ---------------------------------------------------------------------------------------------------------------
// $weightlist <name> { <bone> <weight> [posweight <weight>] ... }, $defaultweightlist { ... }
struct WeightListEntry {
    std::string_view bone;
    float weight = 0.0f;
    float positionWeight = 0.0f; // the QC default is `weight`
};

void ApplyWeightList(StudioMdlState &ctx, std::string_view name, std::span<const WeightListEntry> entries);
void ApplyDefaultWeightList(StudioMdlState &ctx, std::span<const WeightListEntry> entries);

// ---------------------------------------------------------------------------------------------------------------
// $animation <name> <file>: the allocation of the entry (index, name, file). The caller checked for a duplicate name.
s_animation_t *ApplyAnimation(StudioMdlState &ctx, std::string_view name, std::string_view filename);

// ... and its source (QC: Load_Source, which runs after the entry exists; builder: the animation's own frames): the
// defaults from the compile's $upaxis/$origin, 30 fps and the frame range of the source's first animation
void ApplyAnimationSource(StudioMdlState &ctx, s_animation_t *panim, s_source_t *source);

// End of an $animation block: numframes from the frame range
void FinishAnimation(s_animation_t *panim);

// $animation / $sequence options (each one is a token of the QC). The QC applies them in token order.
void ApplyAnimationFps(s_animation_t *panim, float fps);
void ApplyAnimationOrigin(s_animation_t *panim, float x, float y, float z);
void ApplyAnimationRotate(s_animation_t *panim, float degrees);                  // `rotate`: the yaw, +90 like the QC
void ApplyAnimationAngles(s_animation_t *panim, float x, float y, float z);      // `angles`, degrees
void ApplyAnimationScale(s_animation_t *panim, float scale);
void ApplyAnimationLoop(s_animation_t *panim);
void ApplyAnimationNoForceLoop(s_animation_t *panim);
void ApplyAnimationStartLoop(s_animation_t *panim, int frame);
void ApplyAnimationPercentStartLoop(s_animation_t *panim, float percent);
void ApplyAnimationFudgeLoop(s_animation_t *panim);
void ApplyAnimationSnap(s_animation_t *panim);
void ApplyAnimationPost(s_animation_t *panim);
void ApplyAnimationNoAutoIk(s_animation_t *panim, bool noAutoIk);
void ApplyAnimationMotionRollback(s_animation_t *panim, float rollback);
void ApplyAnimationDisableAnimBlocks(s_animation_t *panim);
void ApplyAnimationFirstSectionLocal(s_animation_t *panim);
void ApplyAnimationNoStallFrames(s_animation_t *panim, float frames);
void ApplyAnimationMotion(s_animation_t *panim, StudioMotionFlags flags);       // LX LY ... (added to the others)
// `frame <start> <end>` / `framestart <start>`: clamps to the frames of the source animation
void ApplyAnimationFrameRange(StudioMdlState &ctx, s_animation_t *panim, int startFrame, int endFrame,
                              bool useSourceEndFrame);
// `blockname <name>`: the animation is the named block of its source
void ApplyAnimationBlockName(StudioMdlState &ctx, s_animation_t *panim, std::string_view name);

// ---------------------------------------------------------------------------------------------------------------
// Animation commands ($animation / $cmdlist tokens): each appends one s_animcmd_t. `cmds` is panim->cmds. Referenced
// animations must already be resolved (they are processed before the animation that uses them).
using AnimCommands = s_cmdlist_t::AnimCmdVector;

void AddCmdFixupLoop(AnimCommands &cmds, int startFrame, int endFrame);
void AddCmdWeights(AnimCommands &cmds, int weightListIndex);
void AddCmdSubtract(AnimCommands &cmds, s_animation_t *reference, int frame, bool post); // post: `subtract`, else `presubtract`
void AddCmdAlign(StudioMdlState &ctx, AnimCommands &cmds, s_animation_t *reference, StudioMotionFlags motion,
                 int srcFrame, int destFrame, std::string_view bone); // bone empty: the root bone
void AddCmdMatch(AnimCommands &cmds, s_animation_t *reference);
void AddCmdMatchBlend(AnimCommands &cmds, s_animation_t *reference, int srcFrame, int destFrame, int destPre, int destPost);
void AddCmdWorldSpaceBlend(AnimCommands &cmds, s_animation_t *reference, int startFrame, bool loops);
void AddCmdRotateTo(AnimCommands &cmds, float angle);
void AddCmdIkRule(AnimCommands &cmds, CmdType_e type, s_ikrule_t *rule); // CMD_IKRULE or CMD_IKFIXUP
void AddCmdWalkFrame(AnimCommands &cmds, int endFrame, StudioMotionFlags motion);
void AddCmdWalkAlign(AnimCommands &cmds, int endFrame, s_animation_t *reference, StudioMotionFlags motion, int refFrame,
                     int srcFrame);
void AddCmdDerivative(AnimCommands &cmds, float scale);
void AddCmdNoAnimation(AnimCommands &cmds, bool keepDuration);
void AddCmdLinearDelta(AnimCommands &cmds, bool spline);
void AddCmdCompress(AnimCommands &cmds, int frames);
void AddCmdNumFrames(AnimCommands &cmds, int frames);
void AddCmdCounterRotate(StudioMdlState &ctx, AnimCommands &cmds, std::string_view bone,
                         const std::optional<std::array<float, 3>> &targetAngles);
void AddCmdLocalHierarchy(StudioMdlState &ctx, AnimCommands &cmds, std::string_view bone, std::string_view parent,
                          int start, int peak, int tail, int end); // parent empty: no parent; -1 = unset
void AddCmdForceBonePosRot(StudioMdlState &ctx, AnimCommands &cmds, std::string_view bone,
                           const std::optional<std::array<float, 3>> &position,
                           const std::optional<std::array<float, 3>> &rotationAngles, bool rotationIsLocal);
struct BoneDriverRange {
    int start = 0, peak = 0, tail = 0, end = 0;
};
void AddCmdBoneDriver(StudioMdlState &ctx, AnimCommands &cmds, std::string_view bone, int axis, float value,
                      const std::optional<BoneDriverRange> &range); // range unset: the value for every frame
void AddCmdReverse(AnimCommands &cmds);
void AddCmdAppendAnim(AnimCommands &cmds, s_animation_t *reference);

// $prepend: moves the command at `from` to `to` (the others shift)
void MoveAnimationCommand(s_animation_t *panim, size_t from, size_t to);

// ---------------------------------------------------------------------------------------------------------------
// IK rules (`ikrule` / `ikfixup` commands). The rule starts from InitIkRule (the chain's defaults for the type), the
// parser or the builder then overrides fields.
enum class IkRuleKind { None, AutoSteps, Touch, Footstep, Attachment, Release, Unlatch }; // None: the QC accepts an unknown type

struct IkRuleInit {
    int chain = 0; // index into ctx.anim.ikchain
    IkRuleKind kind = IkRuleKind::Footstep;
    std::string_view bone;       // Touch, AutoSteps
    std::string_view attachment; // Attachment
    int autoStepsEnd = 0;        // AutoSteps
};

// Allocates the rule in ctx.arena and fills type, defaults and the name fields
s_ikrule_t *InitIkRule(StudioMdlState &ctx, const IkRuleInit &init);

// ---------------------------------------------------------------------------------------------------------------
// $sequence options. `pseq` is the sequence being filled.
void ApplySequenceActivity(StudioMdlState &ctx, s_sequence_t *pseq, std::string_view name, int weight);
void ApplySequenceActivityModifier(s_sequence_t *pseq, std::string_view name); // lower-cased
void ApplySequenceEvent(s_sequence_t *pseq, std::string_view name, int frame, std::string_view options);
void ApplySequenceAnimTag(s_sequence_t *pseq, std::string_view name, float cycle);
// `blend`: the first call is the first axis, the second call the second; widens the parameter's range
void ApplySequenceBlend(StudioMdlState &ctx, s_sequence_t *pseq, int poseParameter, float start, float end);
// `calcblend`: control is the motion type of the attachment that drives the parameter
void ApplySequenceCalcBlend(s_sequence_t *pseq, int poseParameter, int attachment, StudioMotionFlags control);
void ApplySequenceNode(StudioMdlState &ctx, s_sequence_t *pseq, std::string_view node);
void ApplySequenceTransition(StudioMdlState &ctx, s_sequence_t *pseq, std::string_view from, std::string_view to,
                             bool reverse);
void ApplySequenceRootDriver(s_sequence_t *pseq, std::string_view bone);
void ApplySequencePoseCycle(s_sequence_t *pseq, int poseParameter);
void ApplySequenceIkLock(s_sequence_t *pseq, std::string_view chain, float positionWeight, float localQWeight);
// addlayer: `local` marks a layer that uses the sequence's own context
void ApplySequenceAddLayer(s_sequence_t *pseq, std::string_view layer, bool local);
struct BlendLayerParams {
    std::string_view layer;
    float start = 0, peak = 0, tail = 0, end = 0;
    bool xfade = false, spline = false, noBlend = false, local = false;
    std::optional<int> poseParameter; // blend with this pose parameter instead of the parent cycle
};
void ApplySequenceBlendLayer(s_sequence_t *pseq, const BlendLayerParams &params);

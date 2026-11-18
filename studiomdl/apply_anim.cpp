#include "studiomdl/apply_anim.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include "common/scriplib.h"
#include "studiomdl_errors.h"
#include "tier1_v2/string_utils.hpp"

const char *InternName(StudioMdlState &ctx, const std::string_view name) {
    return ctx.arena.CopyString(name);
}

namespace {
    // The QC command being applied (ScopedQcPosition), if any
    thread_local const ScriptTokenizer *g_CurrentQc = nullptr;

    // The errors the QC commands always reported with the script position: through the tokenizer while a QC command
    // is applied, MdlError (no position) for the builder
    template<class... Args>
    [[noreturn]] void QcError(std::format_string<Args...> fmt, Args &&...args) {
        if (g_CurrentQc) {
            g_CurrentQc->Error(fmt, std::forward<Args>(args)...);
        }
        MdlError(fmt, std::forward<Args>(args)...);
    }
}

ScopedQcPosition::ScopedQcPosition(const ScriptTokenizer &qc) : m_Previous(g_CurrentQc) {
    g_CurrentQc = &qc;
}

ScopedQcPosition::~ScopedQcPosition() {
    g_CurrentQc = m_Previous;
}

// ---------------------------------------------------------------------------------------------------------------
// Pose parameters, IK chains
// ---------------------------------------------------------------------------------------------------------------

int ApplyPoseParameter(StudioMdlState &ctx, const PoseParameterParams &params) {
    if (std::ssize(ctx.anim.pose) >= MAXSTUDIOPOSEPARAM) {
        QcError("too many pose parameters (max {})\n", MAXSTUDIOPOSEPARAM);
    }

    const std::string name(params.name);
    const int i = LookupPoseParameter(ctx, name.c_str());
    s_poseparameter_t &pose = ctx.anim.pose[i];
    pose.name = params.name;

    if (params.min) {
        pose.min = *params.min;
    }
    if (params.max) {
        pose.max = *params.max;
    }

    switch (params.loop) {
        case PoseParameterParams::Loop::None:
            break;
        case PoseParameterParams::Loop::Wrap:
            pose.flags |= StudioSequenceFlags::Looping;
            pose.loop = pose.max - pose.min;
            break;
        case PoseParameterParams::Loop::Value:
            pose.flags |= StudioSequenceFlags::Looping;
            pose.loop = params.loopValue;
            break;
    }
    return i;
}

int FindIkChain(const StudioMdlState &ctx, const std::string_view name) {
    for (int i = 0; i < std::ssize(ctx.anim.ikchain); i++) {
        if (ctx.anim.ikchain[i].name.iequals(name)) {
            return i;
        }
    }
    return -1;
}

bool ApplyIkChain(StudioMdlState &ctx, const IkChainParams &params) {
    if (FindIkChain(ctx, params.name) >= 0) {
        if (!ctx.options.quiet) {
            MdlPrint("duplicate ikchain \"{}\" ignored\n", params.name);
        }
        return false;
    }
    if (ctx.anim.ikchain.full()) {
        MdlError("too many ik chains (max {})\n", ctx.anim.ikchain.capacity());
    }

    s_ikchain_t &ikchain = ctx.anim.ikchain.emplace_back();
    ikchain.name = params.name;
    ikchain.bonename = params.boneName;
    ikchain.axis = params.axis;
    ikchain.value = params.value;
    ikchain.height = params.height;
    ikchain.floor = params.floor;
    ikchain.radius = params.radius;
    if (params.knee) {
        ikchain.link[0].kneeDir = *params.knee;
    }
    if (params.center) {
        ikchain.center = *params.center;
    }
    return true;
}

void ApplyIkAutoplayLock(StudioMdlState &ctx, const std::string_view chain, const float positionWeight,
                         const float localQWeight) {
    if (ctx.anim.ikautoplaylock.full()) {
        MdlError("too many ik autoplay locks (max {})\n", ctx.anim.ikautoplaylock.capacity());
    }
    s_iklock_t &lock = ctx.anim.ikautoplaylock.emplace_back();
    lock.name = chain;
    lock.flPosWeight = positionWeight;
    lock.flLocalQWeight = localQWeight;
}

// ---------------------------------------------------------------------------------------------------------------
// Attachments, hitboxes
// ---------------------------------------------------------------------------------------------------------------

void ApplyAttachment(StudioMdlState &ctx, const AttachmentParams &params, const std::optional<size_t> redefine) {
    s_attachment_t *attachment;
    if (!redefine) {
        if (ctx.bones.attachments.full()) {
            QcError("Too many attachments\n");
        }
        attachment = &ctx.bones.attachments.emplace_back();
    }
    else {
        attachment = &ctx.bones.attachments[*redefine];
    }

    attachment->name = params.name;
    attachment->bonename = params.boneName;

    Vector position = params.position;
    scale_vertex(ctx, position);

    // identity matrix
    AngleMatrix(QAngle(0, 0, 0), attachment->local);

    for (const AttachmentLocal &step: params.locals) {
        switch (step.kind) {
            case AttachmentLocal::Kind::Absolute:
                attachment->type |= AttachmentType::Absolute;
                AngleIMatrix(ctx.script.defaultrotation, attachment->local);
                break;
            case AttachmentLocal::Kind::Angles:
                AngleMatrix(step.angles, attachment->local);
                break;
            case AttachmentLocal::Kind::RadianEuler:
                AngleMatrix(step.euler, vec3_origin, attachment->local);
                break;
            case AttachmentLocal::Kind::Quaternion:
                QuaternionMatrix(step.quaternion, vec3_origin, attachment->local);
                break;
            case AttachmentLocal::Kind::XZAxes: {
                Vector xaxis = step.xAxis;
                Vector zaxis = step.zAxis;
                Vector yaxis;
                VectorNormalize(xaxis);
                VectorMA(zaxis, -DotProduct(zaxis, xaxis), xaxis, zaxis);
                VectorNormalize(zaxis);
                CrossProduct(zaxis, xaxis, yaxis);
                MatrixSetColumn(xaxis, 0, attachment->local);
                MatrixSetColumn(yaxis, 1, attachment->local);
                MatrixSetColumn(zaxis, 2, attachment->local);
                MatrixSetColumn(vec3_origin, 3, attachment->local);
                break;
            }
        }
    }
    if (params.rigid) {
        attachment->type |= AttachmentType::Rigid;
    }
    if (params.worldAlign) {
        attachment->flags |= AttachmentFlags::WorldAlign;
    }

    attachment->local[0][3] = position.x;
    attachment->local[1][3] = position.y;
    attachment->local[2][3] = position.z;
}

void ApplyHitboxSet(StudioMdlState &ctx, const std::string_view name) {
    s_hitboxset &set = ctx.bones.hitboxsets.emplace_back();
    set.hitboxsetname = name;
}

void ApplyHitbox(StudioMdlState &ctx, const HitboxParams &params) {
    if (ctx.bones.hitboxsets.empty()) {
        // fill in name if it wasn't specified in the .qc
        ApplyHitboxSet(ctx, "default");
    }

    // Last one
    s_hitboxset &set = ctx.bones.hitboxsets.back();
    if (set.hitboxes.full()) {
        MdlError("too many hitboxes in set '{}' (max {})\n", set.hitboxsetname, set.hitboxes.capacity());
    }

    s_bbox_t &hitbox = set.hitboxes.emplace_back(s_bbox_t{.group = params.group});
    hitbox.name = params.boneName; // the bone
    hitbox.bmin = params.mins;
    hitbox.bmax = params.maxs;
    hitbox.angOffsetOrientation = params.angles;
    hitbox.flCapsuleRadius = params.capsuleRadius;

    // Scale hitboxes
    scale_vertex(ctx, hitbox.bmin);
    scale_vertex(ctx, hitbox.bmax);

    hitbox.hitboxname = params.name;
}

void ApplyHitGroup(StudioMdlState &ctx, const int group, const std::string_view boneName) {
    s_hitgroup_t &hitgroup = ctx.bones.hitgroups.emplace_back();
    hitgroup.group = group;
    hitgroup.name = boneName;
}

// ---------------------------------------------------------------------------------------------------------------
// Weight lists
// ---------------------------------------------------------------------------------------------------------------

namespace {
    void FillWeightList(StudioMdlState &ctx, s_weightlist_t &list, const std::span<const WeightListEntry> entries) {
        list.numbones = 0;
        for (const WeightListEntry &entry: entries) {
            const int i = list.numbones++;
            if (i >= MAXWEIGHTSPERLIST) {
                QcError("Too many bones ({}) in weightlist '{}'\n", i, list.name);
            }
            list.bonename[i] = const_cast<char *>(InternName(ctx, entry.bone));
            list.boneweight[i] = entry.weight;
            list.boneposweight[i] = entry.positionWeight;
            if (entry.weight == 0 && entry.positionWeight > 0) {
                QcError("Non-zero Position weight with zero Rotation weight not allowed '{}:{} {:f} {:f}'\n",
                        list.name, entry.bone, entry.weight, entry.positionWeight);
            }
        }
    }
}

void ApplyWeightList(StudioMdlState &ctx, const std::string_view name, const std::span<const WeightListEntry> entries) {
    if (ctx.anim.weightlist.size() >= MAXWEIGHTLISTS) {
        QcError("Too many weightlist commands ({})\n", MAXWEIGHTLISTS);
    }

    // [0] is the default weightlist
    for (size_t i = 1; i < ctx.anim.weightlist.size(); i++) {
        if (ctx.anim.weightlist[i].name.iequals(name)) {
            QcError("Duplicate weightlist '{}'\n", name);
        }
    }

    s_weightlist_t &weightlist = ctx.anim.weightlist.emplace_back();
    weightlist.name = name;
    FillWeightList(ctx, weightlist, entries);
}

void ApplyDefaultWeightList(StudioMdlState &ctx, const std::span<const WeightListEntry> entries) {
    FillWeightList(ctx, ctx.anim.weightlist[0], entries);
}

// ---------------------------------------------------------------------------------------------------------------
// $animation
// ---------------------------------------------------------------------------------------------------------------

s_animation_t *ApplyAnimation(StudioMdlState &ctx, const std::string_view name, const std::string_view filename) {
    if (ctx.anim.animations.full()) {
        MdlError("Too many animations ({} max)\n", MAXSTUDIOANIMS);
    }

    // allocate animation entry
    s_animation_t *panim = ctx.anim.animations.emplace_back(ctx.arena.Create<s_animation_t>());

    panim->index = ctx.anim.animations.size() - 1;
    panim->name = name;
    panim->filename = filename;
    return panim;
}

void ApplyAnimationSource(StudioMdlState &ctx, s_animation_t *panim, s_source_t *source) {
    panim->source = source;
    if (!panim->source->m_Animations.empty()) {
        const s_sourceanim_t *pSourceAnim = &panim->source->m_Animations[0];
        panim->startframe = pSourceAnim->startframe;
        panim->endframe = pSourceAnim->endframe;
        panim->animationname = pSourceAnim->animationname;
    }
    else {
        panim->startframe = 0;
        panim->endframe = 0;
        panim->animationname = "";
    }
    VectorCopy(ctx.script.defaultadjust, panim->adjust);
    panim->rotation = ctx.script.defaultrotation;
    panim->scale = 1.0f;
    panim->fps = 30.0;
    panim->motionrollback = ctx.options.defaultMotionRollback;
}

void FinishAnimation(s_animation_t *panim) {
    panim->numframes = panim->endframe - panim->startframe + 1;
}

void ApplyAnimationFps(s_animation_t *panim, const float fps) {
    panim->fps = fps;
}

void ApplyAnimationOrigin(s_animation_t *panim, const float x, const float y, const float z) {
    panim->adjust.x = x;
    panim->adjust.y = y;
    panim->adjust.z = z;
}

void ApplyAnimationRotate(s_animation_t *panim, const float degrees) {
    // FIXME: broken for Maya
    panim->rotation.z = DEG2RAD(degrees + 90);
}

void ApplyAnimationAngles(s_animation_t *panim, const float x, const float y, const float z) {
    panim->rotation.x = DEG2RAD(x);
    panim->rotation.y = DEG2RAD(y);
    panim->rotation.z = DEG2RAD(z + 90.0f);
}

void ApplyAnimationScale(s_animation_t *panim, const float scale) {
    panim->scale = scale;
}

void ApplyAnimationLoop(s_animation_t *panim) {
    panim->flags |= StudioSequenceFlags::Looping;
}

void ApplyAnimationNoForceLoop(s_animation_t *panim) {
    panim->flags |= StudioSequenceFlags::NoForceLoop;
}

void ApplyAnimationStartLoop(s_animation_t *panim, const int frame) {
    panim->looprestart = frame;
    if (panim->looprestartpercent != 0) {
        QcError("Can't specify startloop for animation {}, percentstartloop already specified.\n", panim->name);
    }
    panim->flags |= StudioSequenceFlags::Looping;
}

void ApplyAnimationPercentStartLoop(s_animation_t *panim, const float percent) {
    panim->looprestartpercent = percent;
    if (panim->looprestart != 0) {
        QcError("Can't specify percentstartloop for animation {}, looprestart already specified.\n", panim->name);
    }
    panim->flags |= StudioSequenceFlags::Looping;
}

void ApplyAnimationFudgeLoop(s_animation_t *panim) {
    panim->fudgeloop = true;
    panim->flags |= StudioSequenceFlags::Looping;
}

void ApplyAnimationSnap(s_animation_t *panim) {
    panim->flags |= StudioSequenceFlags::Snap;
}

void ApplyAnimationPost(s_animation_t *panim) {
    panim->flags |= StudioSequenceFlags::Post;
}

void ApplyAnimationNoAutoIk(s_animation_t *panim, const bool noAutoIk) {
    panim->noAutoIK = noAutoIk;
}

void ApplyAnimationMotionRollback(s_animation_t *panim, const float rollback) {
    panim->motionrollback = rollback;
}

void ApplyAnimationDisableAnimBlocks(s_animation_t *panim) {
    panim->disableAnimblocks = true;
}

void ApplyAnimationFirstSectionLocal(s_animation_t *panim) {
    panim->isFirstSectionLocal = true;
}

void ApplyAnimationNoStallFrames(s_animation_t *panim, const float frames) {
    panim->numNostallFrames = frames;
}

void ApplyAnimationMotion(s_animation_t *panim, const StudioMotionFlags flags) {
    panim->motiontype |= flags;
}

void ApplyAnimationFrameRange(StudioMdlState &ctx, s_animation_t *panim, const int startFrame, const int endFrame,
                              const bool useSourceEndFrame) {
    panim->startframe = startFrame;
    if (!useSourceEndFrame) {
        panim->endframe = endFrame;
    }

    // NOTE: This always affects the first source anim read in
    const s_sourceanim_t *pSourceAnim = FindSourceAnim(panim->source, panim->animationname.view());
    if (pSourceAnim) {
        if (panim->startframe < pSourceAnim->startframe) {
            panim->startframe = pSourceAnim->startframe;
        }

        if (panim->endframe > pSourceAnim->endframe || useSourceEndFrame) {
            panim->endframe = pSourceAnim->endframe;
        }
    }

    if (!ctx.options.createMakefile && panim->endframe < panim->startframe) {
        QcError("end frame before start frame in {}\n", panim->name);
    }

    panim->numframes = panim->endframe - panim->startframe + 1;
}

void ApplyAnimationBlockName(StudioMdlState &ctx, s_animation_t *panim, const std::string_view name) {
    const s_sourceanim_t *pSourceAnim = FindSourceAnim(panim->source, name);

    // NOTE: This always affects the first source anim read in
    if (!pSourceAnim) {
        QcError("Requested unknown animation block name {}\n", name);
    }
    panim->startframe = pSourceAnim->startframe;
    panim->endframe = pSourceAnim->endframe;

    if (!ctx.options.createMakefile && panim->endframe < panim->startframe) {
        QcError("end frame before start frame in {}\n", panim->name);
    }

    panim->numframes = panim->endframe - panim->startframe + 1;
    panim->animationname = name;
}

// ---------------------------------------------------------------------------------------------------------------
// Animation commands
// ---------------------------------------------------------------------------------------------------------------

namespace {
    s_animcmd_t &NewCommand(AnimCommands &cmds, const CmdType_e type) {
        if (cmds.full()) {
            MdlError("too many animation commands (max {})\n", cmds.capacity());
        }
        s_animcmd_t &cmd = cmds.emplace_back();
        cmd.cmd = type;
        return cmd;
    }

    char *NameOrNull(StudioMdlState &ctx, const std::string_view name) {
        return name.empty() ? nullptr : const_cast<char *>(InternName(ctx, name));
    }
}

void AddCmdFixupLoop(AnimCommands &cmds, const int startFrame, const int endFrame) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_FIXUP);
    cmd.u.fixuploop.start = startFrame;
    cmd.u.fixuploop.end = endFrame;
}

void AddCmdWeights(AnimCommands &cmds, const int weightListIndex) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_WEIGHTS);
    cmd.u.weightlist.index = weightListIndex;
}

void AddCmdSubtract(AnimCommands &cmds, s_animation_t *reference, const int frame, const bool post) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_SUBTRACT);
    cmd.u.subtract.ref = reference;
    cmd.u.subtract.frame = frame;
    if (post) {
        cmd.u.subtract.flags |= StudioSequenceFlags::Post;
    }
}

void AddCmdAlign(StudioMdlState &ctx, AnimCommands &cmds, s_animation_t *reference, const StudioMotionFlags motion,
                 const int srcFrame, const int destFrame, const std::string_view bone) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_AO);
    cmd.u.ao.pBonename = NameOrNull(ctx, bone);
    cmd.u.ao.ref = reference;
    cmd.u.ao.motiontype = motion;
    cmd.u.ao.srcframe = srcFrame;
    cmd.u.ao.destframe = destFrame;
}

void AddCmdMatch(AnimCommands &cmds, s_animation_t *reference) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_MATCH);
    cmd.u.match.ref = reference;
}

void AddCmdMatchBlend(AnimCommands &cmds, s_animation_t *reference, const int srcFrame, const int destFrame,
                      const int destPre, const int destPost) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_MATCHBLEND);
    cmd.u.match.ref = reference;
    cmd.u.match.srcframe = srcFrame;
    cmd.u.match.destframe = destFrame;
    cmd.u.match.destpre = destPre;
    cmd.u.match.destpost = destPost;
}

void AddCmdWorldSpaceBlend(AnimCommands &cmds, s_animation_t *reference, const int startFrame, const bool loops) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_WORLDSPACEBLEND);
    cmd.u.world.ref = reference;
    cmd.u.world.startframe = startFrame;
    cmd.u.world.loops = loops;
}

void AddCmdRotateTo(AnimCommands &cmds, const float angle) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_ANGLE);
    cmd.u.angle.angle = angle;
}

void AddCmdIkRule(AnimCommands &cmds, const CmdType_e type, s_ikrule_t *rule) {
    s_animcmd_t &cmd = NewCommand(cmds, type);
    cmd.u.ikrule.pRule = rule; // the ikfixup member is the same pointer
}

void AddCmdWalkFrame(AnimCommands &cmds, const int endFrame, const StudioMotionFlags motion) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_MOTION);
    cmd.u.motion.iEndFrame = endFrame;
    cmd.u.motion.motiontype = motion;
}

void AddCmdWalkAlign(AnimCommands &cmds, const int endFrame, s_animation_t *reference, const StudioMotionFlags motion,
                     const int refFrame, const int srcFrame) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_REFMOTION);
    cmd.u.motion.iEndFrame = endFrame;
    cmd.u.motion.pRefAnim = reference;
    cmd.u.motion.motiontype = motion;
    cmd.u.motion.iRefFrame = refFrame;
    cmd.u.motion.iSrcFrame = srcFrame;
}

void AddCmdDerivative(AnimCommands &cmds, const float scale) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_DERIVATIVE);
    cmd.u.derivative.scale = scale;
}

void AddCmdNoAnimation(AnimCommands &cmds, const bool keepDuration) {
    NewCommand(cmds, keepDuration ? CMD_NOANIM_KEEPDURATION : CMD_NOANIMATION);
}

void AddCmdLinearDelta(AnimCommands &cmds, const bool spline) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_LINEARDELTA);
    cmd.u.linear.flags |= AutoLayerFlags::Post;
    if (spline) {
        cmd.u.linear.flags |= AutoLayerFlags::Spline;
    }
}

void AddCmdCompress(AnimCommands &cmds, const int frames) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_COMPRESS);
    cmd.u.compress.frames = frames;
}

void AddCmdNumFrames(AnimCommands &cmds, const int frames) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_NUMFRAMES);
    cmd.u.numframes.frames = frames;
}

void AddCmdCounterRotate(StudioMdlState &ctx, AnimCommands &cmds, const std::string_view bone,
                         const std::optional<std::array<float, 3>> &targetAngles) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_COUNTERROTATE);
    cmd.u.counterrotate.pBonename = const_cast<char *>(InternName(ctx, bone));
    if (targetAngles) {
        cmd.u.counterrotate.bHasTarget = true;
        for (int i = 0; i < 3; i++) {
            cmd.u.counterrotate.targetAngle[i] = (*targetAngles)[i];
        }
    }
}

void AddCmdLocalHierarchy(StudioMdlState &ctx, AnimCommands &cmds, const std::string_view bone,
                          const std::string_view parent, const int start, const int peak, const int tail,
                          const int end) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_LOCALHIERARCHY);
    cmd.u.localhierarchy.pBonename = const_cast<char *>(InternName(ctx, bone));
    cmd.u.localhierarchy.pParentname = const_cast<char *>(InternName(ctx, parent));
    cmd.u.localhierarchy.start = start;
    cmd.u.localhierarchy.peak = peak;
    cmd.u.localhierarchy.tail = tail;
    cmd.u.localhierarchy.end = end;
}

void AddCmdForceBonePosRot(StudioMdlState &ctx, AnimCommands &cmds, const std::string_view bone,
                           const std::optional<std::array<float, 3>> &position,
                           const std::optional<std::array<float, 3>> &rotationAngles, const bool rotationIsLocal) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_FORCEBONEPOSROT);
    cmd.u.forceboneposrot.pBonename = const_cast<char *>(InternName(ctx, bone));
    if (position) {
        cmd.u.forceboneposrot.bDoPos = true;
        for (int i = 0; i < 3; i++) {
            cmd.u.forceboneposrot.pos[i] = (*position)[i];
        }
    }
    if (rotationAngles) {
        cmd.u.forceboneposrot.bDoRot = true;
        for (int i = 0; i < 3; i++) {
            cmd.u.forceboneposrot.rot[i] = (*rotationAngles)[i];
        }
        cmd.u.forceboneposrot.bRotIsLocal = rotationIsLocal;
    }
}

void AddCmdBoneDriver(StudioMdlState &ctx, AnimCommands &cmds, const std::string_view bone, const int axis,
                      const float value, const std::optional<BoneDriverRange> &range) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_BONEDRIVER);
    cmd.u.bonedriver.pBonename = const_cast<char *>(InternName(ctx, bone));
    cmd.u.bonedriver.iAxis = axis;
    cmd.u.bonedriver.value = value;
    cmd.u.bonedriver.all = !range;
    if (range) {
        cmd.u.bonedriver.start = range->start;
        cmd.u.bonedriver.peak = range->peak;
        cmd.u.bonedriver.tail = range->tail;
        cmd.u.bonedriver.end = range->end;
    }
}

void AddCmdReverse(AnimCommands &cmds) {
    NewCommand(cmds, CMD_REVERSE);
}

void AddCmdAppendAnim(AnimCommands &cmds, s_animation_t *reference) {
    s_animcmd_t &cmd = NewCommand(cmds, CMD_APPENDANIM);
    cmd.u.appendanim.ref = reference;
}

void MoveAnimationCommand(s_animation_t *panim, const size_t from, const size_t to) {
    auto &cmds = panim->cmds;
    if (from >= cmds.size() || to >= cmds.size()) {
        MdlError("animation '{}': cannot move command {} to {} ({} commands)\n", panim->name, from, to, cmds.size());
    }
    const s_animcmd_t moved = cmds[from];
    if (from < to) {
        for (size_t i = from; i < to; i++) {
            cmds[i] = cmds[i + 1];
        }
    }
    else {
        for (size_t i = from; i > to; i--) {
            cmds[i] = cmds[i - 1];
        }
    }
    cmds[to] = moved;
}

// ---------------------------------------------------------------------------------------------------------------
// IK rules
// ---------------------------------------------------------------------------------------------------------------

s_ikrule_t *InitIkRule(StudioMdlState &ctx, const IkRuleInit &init) {
    s_ikrule_t *rule = ctx.arena.Create<s_ikrule_t>();
    const s_ikchain_t &chain = ctx.anim.ikchain[init.chain];

    rule->chain = init.chain;
    // default slot
    rule->slot = init.chain;

    switch (init.kind) {
        case IkRuleKind::None:
            break;
        case IkRuleKind::AutoSteps:
            rule->end = init.autoStepsEnd;
            rule->bonename = init.bone;
            rule->type = IkRuleType::Ground;
            rule->height = chain.height;
            rule->floor = chain.floor;
            rule->radius = chain.radius;
            rule->start = -2;
            rule->peak = -1;
            rule->tail = -1;
            break;
        case IkRuleKind::Touch:
            rule->type = IkRuleType::Self;
            rule->bonename = init.bone;
            break;
        case IkRuleKind::Footstep:
            rule->type = IkRuleType::Ground;
            rule->height = chain.height;
            rule->floor = chain.floor;
            rule->radius = chain.radius;
            break;
        case IkRuleKind::Attachment:
            rule->type = IkRuleType::Attachment;
            rule->attachment = init.attachment;
            break;
        case IkRuleKind::Release:
            rule->type = IkRuleType::Release;
            break;
        case IkRuleKind::Unlatch:
            rule->type = IkRuleType::Unlatch;
            break;
    }

    rule->contact = -1;
    return rule;
}

// ---------------------------------------------------------------------------------------------------------------
// Sequences
// ---------------------------------------------------------------------------------------------------------------

void ApplySequenceActivity(StudioMdlState &ctx, s_sequence_t *pseq, const std::string_view name, const int weight) {
    pseq->activityname = name;

    if (!ctx.anim.AllowedActivityNames.empty()) {
        if (std::ranges::find(ctx.anim.AllowedActivityNames, name) == ctx.anim.AllowedActivityNames.end()) {
            QcError("Unknown sequence activity \"{}\" in \"{}\".\n", name, pseq->name);
        }
    }

    pseq->actweight = weight;
    if (pseq->actweight == 0) {
        QcError("Activity {} has a zero weight (weights must be integers > 0)\n", pseq->activityname);
    }
}

void ApplySequenceActivityModifier(s_sequence_t *pseq, const std::string_view name) {
    if (pseq->numactivitymodifiers >= MAXSTUDIOACTIVITYMODIFIERS) {
        MdlError("sequence \"{}\": too many activity modifiers (max {})\n", pseq->name, MAXSTUDIOACTIVITYMODIFIERS);
    }
    std::string lower(name);
    vstd::make_lower_ascii(lower);
    pseq->activitymodifier[pseq->numactivitymodifiers++].name = std::string_view(lower);
}

void ApplySequenceEvent(s_sequence_t *pseq, const std::string_view name, const int frame,
                        const std::string_view options) {
    if (pseq->events.full()) {
        QcError("too many events\n");
    }
    s_event_t &event = pseq->events.emplace_back(s_event_t{.eventname = name});
    event.frame = frame;
    event.options = options;
}

void ApplySequenceAnimTag(s_sequence_t *pseq, const std::string_view name, const float cycle) {
    if (pseq->animtags.full()) {
        QcError("too many animtags\n");
    }
    s_animtag_t &animtag = pseq->animtags.emplace_back(s_animtag_t{.tagname = name});
    animtag.cycle = cycle;
}

void ApplySequenceBlend(StudioMdlState &ctx, s_sequence_t *pseq, const int poseParameter, const float start,
                        const float end) {
    int i = 0;
    if (pseq->paramindex[0] != -1) {
        i = 1;
    }

    pseq->paramindex[i] = poseParameter;
    pseq->paramattachment[i] = -1;
    pseq->paramstart[i] = start;
    pseq->paramend[i] = end;

    s_poseparameter_t &pose = ctx.anim.pose[poseParameter];
    pose.min = std::min(pose.min, start);
    pose.min = std::min(pose.min, end);
    pose.max = std::max(pose.max, start);
    pose.max = std::max(pose.max, end);
}

void ApplySequenceCalcBlend(s_sequence_t *pseq, const int poseParameter, const int attachment,
                            const StudioMotionFlags control) {
    int i = 0;
    if (pseq->paramindex[0] != -1) {
        i = 1;
    }

    pseq->paramindex[i] = poseParameter;
    pseq->paramattachment[i] = attachment;
    pseq->paramcontrol[i] = control;
}

void ApplySequenceNode(StudioMdlState &ctx, s_sequence_t *pseq, const std::string_view node) {
    pseq->entrynode = pseq->exitnode = LookupXNode(ctx, std::string(node).c_str());
}

void ApplySequenceTransition(StudioMdlState &ctx, s_sequence_t *pseq, const std::string_view from,
                             const std::string_view to, const bool reverse) {
    pseq->entrynode = LookupXNode(ctx, std::string(from).c_str());
    pseq->exitnode = LookupXNode(ctx, std::string(to).c_str());
    if (reverse) {
        pseq->nodeflags |= 1;
    }
}

void ApplySequenceRootDriver(s_sequence_t *pseq, const std::string_view bone) {
    pseq->flags |= StudioSequenceFlags::RootXform;
    pseq->rootDriverBoneName = bone;
}

void ApplySequencePoseCycle(s_sequence_t *pseq, const int poseParameter) {
    pseq->flags |= StudioSequenceFlags::CyclePose;
    pseq->cycleposeindex = poseParameter;
}

void ApplySequenceIkLock(s_sequence_t *pseq, const std::string_view chain, const float positionWeight,
                         const float localQWeight) {
    if (pseq->iklocks.full()) {
        MdlError("sequence \"{}\": too many iklocks (max {})\n", pseq->name, pseq->iklocks.capacity());
    }
    s_iklock_t &iklock = pseq->iklocks.emplace_back(s_iklock_t{.name = chain});
    iklock.flPosWeight = positionWeight;
    iklock.flLocalQWeight = localQWeight;
}

void ApplySequenceAddLayer(s_sequence_t *pseq, const std::string_view layer, const bool local) {
    if (pseq->autolayers.full()) {
        MdlError("sequence \"{}\": too many layers (max {})\n", pseq->name, pseq->autolayers.capacity());
    }
    s_autolayer_t &autoLayer = pseq->autolayers.emplace_back(s_autolayer_t{.name = layer});
    if (local) {
        autoLayer.flags |= AutoLayerFlags::Local;
        pseq->flags |= StudioSequenceFlags::Local;
    }
}

void ApplySequenceBlendLayer(s_sequence_t *pseq, const BlendLayerParams &params) {
    if (pseq->autolayers.full()) {
        MdlError("sequence \"{}\": too many layers (max {})\n", pseq->name, pseq->autolayers.capacity());
    }
    s_autolayer_t &autoLayer = pseq->autolayers.emplace_back();
    autoLayer.flags = AutoLayerFlags::None;
    autoLayer.name = params.layer;
    autoLayer.start = params.start;
    autoLayer.peak = params.peak;
    autoLayer.tail = params.tail;
    autoLayer.end = params.end;
    if (params.xfade) {
        autoLayer.flags |= AutoLayerFlags::XFade;
    }
    if (params.spline) {
        autoLayer.flags |= AutoLayerFlags::Spline;
    }
    if (params.noBlend) {
        autoLayer.flags |= AutoLayerFlags::NoBlend;
    }
    if (params.poseParameter) {
        autoLayer.flags |= AutoLayerFlags::Pose;
        autoLayer.pose = *params.poseParameter;
    }
    if (params.local) {
        autoLayer.flags |= AutoLayerFlags::Local;
        pseq->flags |= StudioSequenceFlags::Local;
    }
}

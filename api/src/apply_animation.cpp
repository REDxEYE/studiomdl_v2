// Builder front end, Anim part: animations. Each animation gets its own source (what Load_Source makes from an animation
// SMD): the model skeleton as nodes and one "BindPose" block with a frame per animation frame, filled from the rest pose
// and overridden by the bone data of the animation. The entry, options and commands then go through the same apply
// functions as a $animation command (studiomdl/apply_anim.cpp). Animations are applied in creation order; a command's
// reference animation must have been created before, because the compile resolves it when the command is added.
#include "apply.hpp"

#include <format>

#include "studiomdl/apply_anim.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"

namespace {
    // The compile's $scale applies to animation positions (SetSourceAnimBone scales with currentscale)
    class ScopedDefaultScale {
    public:
        explicit ScopedDefaultScale(StudioMdlState &ctx) : m_Ctx(ctx), m_Previous(ctx.script.currentscale) {
            ctx.script.currentscale = ctx.script.defaultscale;
        }
        ~ScopedDefaultScale() { m_Ctx.script.currentscale = m_Previous; }
        ScopedDefaultScale(const ScopedDefaultScale &) = delete;
        ScopedDefaultScale &operator=(const ScopedDefaultScale &) = delete;

    private:
        StudioMdlState &m_Ctx;
        float m_Previous;
    };

    // The animation's source: skeleton nodes, then the "BindPose" frames
    s_source_t *BuildAnimationSource(StudioMdlState &ctx, const ApplyState &state, const smdl_animation &animation,
                                     const std::string &filename) {
        const std::vector<BoneDesc> &bones = state.model.skeleton.bones;
        if (bones.empty())
            MdlError("the model has no bones (smdl_model_add_bone)");
        if (ctx.anim.sources.size() >= ctx.anim.sources.capacity())
            MdlError("too many sources, max {}", ctx.anim.sources.capacity());

        ScopedDefaultScale scale(ctx);

        s_source_t *source = ctx.arena.Create<s_source_t>();
        ctx.anim.sources.emplace_back(source);
        source->adjust = ctx.script.defaultadjust;
        source->scale = 1.0f;
        source->rotation = ctx.script.defaultrotation;
        source->filename = filename;
        source->version = 1;
        source->numbones = static_cast<int>(bones.size());

        InitSourceNodes(source->localBone);
        for (size_t i = 0; i < bones.size(); ++i) {
            SetSourceNode(source->localBone, static_cast<int>(i), bones[i].name,
                          bones[i].parent == SMDL_NO_BONE ? -1 : static_cast<int>(bones[i].parent));
        }

        // The pose of each bone without frame data: the animation's rest override, else the model skeleton
        std::vector<Vector> restPositions(bones.size());
        std::vector<RadianEuler> restRotations(bones.size());
        for (size_t i = 0; i < bones.size(); ++i) {
            restPositions[i] = bones[i].position;
            restRotations[i] = bones[i].rotation;
        }
        for (const RestPoseOverride &rest: animation.restOverrides) {
            restPositions[rest.bone] = rest.position;
            restRotations[rest.bone] = rest.rotation;
        }

        s_sourceanim_t *sourceAnim = FindOrAddSourceAnim(source, "BindPose");
        sourceAnim->startframe = -1;
        for (uint32_t frame = 0; frame < animation.frameCount; ++frame) {
            int relativeFrame = 0;
            if (!SetSourceAnimTime(ctx, source, sourceAnim, static_cast<int>(frame), relativeFrame))
                MdlError("frame {} is out of order", frame);
            for (size_t i = 0; i < bones.size(); ++i) {
                Vector position = restPositions[i];
                RadianEuler rotation = restRotations[i];
                if (const auto data = animation.bones.find(static_cast<smdl_bone_id>(i)); data != animation.bones.end()) {
                    if (!data->second.positions.empty())
                        position = data->second.positions[frame];
                    if (!data->second.rotations.empty())
                        rotation = data->second.rotations[frame];
                }
                SetSourceAnimBone(ctx, sourceAnim, relativeFrame, static_cast<int>(i), position, rotation);
            }
        }
        const int missing = FinishSourceAnim(source, sourceAnim, "BindPose");
        if (missing >= 0)
            MdlError("the animation is missing frame {}", missing);
        return source;
    }

    const std::string &BoneName(const ApplyState &state, smdl_bone_id bone) {
        return state.model.skeleton.bones[bone].name;
    }

    // The compile's entry of a reference animation; it must exist already
    s_animation_t *ResolveReference(StudioMdlState &ctx, const smdl_animation &animation, const smdl_animation *reference,
                                    std::string_view command) {
        s_animation_t *resolved = nullptr;
        if (reference->index < animation.index)
            resolved = LookupAnimation(ctx, reference->name.c_str());
        if (!resolved)
            MdlError("{} refers to animation '{}', which is created after it (create the referenced animation first)",
                     command, reference->name);
        return resolved;
    }

    IkRuleKind RuleKind(smdl_ik_rule_type type) {
        switch (type) {
            case SMDL_IK_RULE_AUTOSTEPS: return IkRuleKind::AutoSteps;
            case SMDL_IK_RULE_TOUCH: return IkRuleKind::Touch;
            case SMDL_IK_RULE_FOOTSTEP: return IkRuleKind::Footstep;
            case SMDL_IK_RULE_ATTACHMENT: return IkRuleKind::Attachment;
            case SMDL_IK_RULE_RELEASE: return IkRuleKind::Release;
            case SMDL_IK_RULE_UNLATCH: return IkRuleKind::Unlatch;
        }
        return IkRuleKind::None;
    }

    s_ikrule_t *MakeIkRule(StudioMdlState &ctx, const ApplyState &state, const smdl_ik_rule &desc) {
        const RigDesc &rig = state.model.rig;
        IkRuleInit init;
        init.chain = FindIkChain(ctx, rig.ikChains[desc.chain].name);
        if (init.chain < 0)
            MdlError("unknown IK chain '{}' in ik rule", rig.ikChains[desc.chain].name);
        init.kind = RuleKind(desc.type);
        if (desc.type == SMDL_IK_RULE_AUTOSTEPS || desc.type == SMDL_IK_RULE_TOUCH) {
            if (!desc.bone)
                MdlError("the IK rule needs a bone (smdl_ik_rule_set_bone)");
            init.bone = BoneName(state, *desc.bone);
        }
        if (desc.type == SMDL_IK_RULE_ATTACHMENT) {
            if (!desc.attachment)
                MdlError("the IK rule needs an attachment (smdl_ik_rule_set_attachment)");
            init.attachment = rig.attachments[*desc.attachment].name;
        }
        init.autoStepsEnd = desc.autoStepsEnd;

        s_ikrule_t *rule = InitIkRule(ctx, init);
        if (desc.height)
            rule->height = *desc.height;
        if (desc.target)
            rule->slot = *desc.target;
        if (desc.range) {
            rule->start = (*desc.range)[0];
            rule->peak = (*desc.range)[1];
            rule->tail = (*desc.range)[2];
            rule->end = (*desc.range)[3];
        }
        if (desc.floor)
            rule->floor = *desc.floor;
        if (desc.radius)
            rule->radius = *desc.radius;
        if (desc.contact)
            rule->contact = *desc.contact;
        switch (desc.useMode) {
            case 1:
                rule->usesequence = true;
                rule->usesource = false;
                break;
            case 2:
                rule->usesequence = false;
                rule->usesource = true;
                break;
            case 3:
                rule->usesequence = false;
                rule->usesource = false;
                break;
            default:
                break;
        }
        if (desc.fakeOrigin) {
            rule->pos = *desc.fakeOrigin;
            rule->bone = -1;
        }
        if (desc.fakeRotation) {
            rule->q = *desc.fakeRotation;
            rule->bone = -1;
        }
        return rule;
    }

    void ApplyCommands(StudioMdlState &ctx, const ApplyState &state, const smdl_animation &animation,
                       s_animation_t *panim) {
        using Type = AnimationCommandDesc::Type;
        AnimCommands &cmds = panim->cmds;
        for (const AnimationCommandDesc &command: animation.commands) {
            const auto motion = static_cast<StudioMotionFlags>(command.motion);
            const std::string_view none;
            switch (command.type) {
                case Type::FixupLoop:
                    AddCmdFixupLoop(cmds, command.a, command.b);
                    break;
                case Type::Weights:
                    AddCmdWeights(cmds, static_cast<int>(command.weightList));
                    break;
                case Type::Subtract:
                    AddCmdSubtract(cmds, ResolveReference(ctx, animation, command.reference, "subtract"), command.a,
                                   command.flag);
                    break;
                case Type::Align:
                    AddCmdAlign(ctx, cmds, ResolveReference(ctx, animation, command.reference, "align"), motion,
                                command.a, command.b,
                                command.bone == SMDL_NO_BONE ? none : std::string_view(BoneName(state, command.bone)));
                    break;
                case Type::Match:
                    AddCmdMatch(cmds, ResolveReference(ctx, animation, command.reference, "match"));
                    break;
                case Type::MatchBlend:
                    AddCmdMatchBlend(cmds, ResolveReference(ctx, animation, command.reference, "matchblend"),
                                     command.a, command.b, command.c, command.d);
                    break;
                case Type::WorldSpaceBlend:
                    AddCmdWorldSpaceBlend(cmds, ResolveReference(ctx, animation, command.reference, "worldspaceblend"),
                                          command.a, command.flag);
                    break;
                case Type::RotateTo:
                    AddCmdRotateTo(cmds, command.value);
                    break;
                case Type::IkRule:
                    AddCmdIkRule(cmds, command.flag ? CMD_IKFIXUP : CMD_IKRULE, MakeIkRule(ctx, state, *command.rule));
                    break;
                case Type::WalkFrame:
                    AddCmdWalkFrame(cmds, command.a, motion);
                    break;
                case Type::WalkAlign:
                    AddCmdWalkAlign(cmds, command.a, ResolveReference(ctx, animation, command.reference, "walkalign"),
                                    motion, command.b, command.c);
                    break;
                case Type::Derivative:
                    AddCmdDerivative(cmds, command.value);
                    break;
                case Type::NoAnimation:
                    AddCmdNoAnimation(cmds, false);
                    break;
                case Type::NoAnimationKeepDuration:
                    AddCmdNoAnimation(cmds, true);
                    break;
                case Type::LinearDelta:
                    AddCmdLinearDelta(cmds, false);
                    break;
                case Type::SplineDelta:
                    AddCmdLinearDelta(cmds, true);
                    break;
                case Type::Compress:
                    AddCmdCompress(cmds, command.a);
                    break;
                case Type::NumFrames:
                    AddCmdNumFrames(cmds, command.a);
                    break;
                case Type::CounterRotate:
                    AddCmdCounterRotate(ctx, cmds, BoneName(state, command.bone), command.vectorA);
                    break;
                case Type::LocalHierarchy:
                    AddCmdLocalHierarchy(ctx, cmds, BoneName(state, command.bone),
                                         command.parent == SMDL_NO_BONE ? none
                                                                        : std::string_view(BoneName(state, command.parent)),
                                         command.a, command.b, command.c, command.d);
                    break;
                case Type::ForceBonePosRot:
                    AddCmdForceBonePosRot(ctx, cmds, BoneName(state, command.bone), command.vectorA, command.vectorB,
                                          command.flag);
                    break;
                case Type::BoneDriver: {
                    std::optional<BoneDriverRange> range;
                    if (command.flag)
                        range = BoneDriverRange{command.b, command.c, command.d, command.e};
                    AddCmdBoneDriver(ctx, cmds, BoneName(state, command.bone), command.a, command.value, range);
                    break;
                }
                case Type::Reverse:
                    AddCmdReverse(cmds);
                    break;
                case Type::AppendAnimation:
                    AddCmdAppendAnim(cmds, ResolveReference(ctx, animation, command.reference, "appendanim"));
                    break;
            }
        }
    }

    void ApplyOptions(StudioMdlState &ctx, const smdl_animation &animation, s_animation_t *panim) {
        const AnimationOptionsDesc &options = animation.options;
        ApplyAnimationFps(panim, animation.fps);
        if (options.origin)
            ApplyAnimationOrigin(panim, options.origin->x, options.origin->y, options.origin->z);
        for (const AnimationOptionsDesc::RotationStep &step: options.rotation) {
            if (step.angles)
                ApplyAnimationAngles(panim, step.x, step.y, step.z);
            else
                ApplyAnimationRotate(panim, step.z);
        }
        if (options.scale)
            ApplyAnimationScale(panim, *options.scale);
        if (options.loop)
            ApplyAnimationLoop(panim);
        if (options.noForceLoop)
            ApplyAnimationNoForceLoop(panim);
        if (options.startLoop)
            ApplyAnimationStartLoop(panim, *options.startLoop);
        if (options.percentStartLoop)
            ApplyAnimationPercentStartLoop(panim, *options.percentStartLoop);
        if (options.fudgeLoop)
            ApplyAnimationFudgeLoop(panim);
        if (options.snap)
            ApplyAnimationSnap(panim);
        if (options.post)
            ApplyAnimationPost(panim);
        if (options.autoIk)
            ApplyAnimationNoAutoIk(panim, !*options.autoIk);
        if (options.motionRollback)
            ApplyAnimationMotionRollback(panim, *options.motionRollback);
        if (options.noAnimBlock)
            ApplyAnimationDisableAnimBlocks(panim);
        if (options.noAnimBlockStall)
            ApplyAnimationFirstSectionLocal(panim);
        if (options.noStallFrames)
            ApplyAnimationNoStallFrames(panim, *options.noStallFrames);
        if (options.blockName)
            ApplyAnimationBlockName(ctx, panim, *options.blockName);
        if (options.frameRange)
            ApplyAnimationFrameRange(ctx, panim, options.frameRange->first, options.frameRange->last,
                                     options.frameRange->toEnd);
        if (options.motion != 0)
            ApplyAnimationMotion(panim, static_cast<StudioMotionFlags>(options.motion));
    }
}

void ApplyAnimations(StudioMdlState &ctx, ApplyState &state) {
    for (const std::unique_ptr<smdl_animation> &entry: state.model.animations.list) {
        const smdl_animation &animation = *entry;
        ScopedApplyObject object(state, std::format("animation '{}'", animation.name));

        const std::string filename = std::format("animation '{}'", animation.name);
        s_animation_t *panim = ApplyAnimation(ctx, animation.name, filename);
        ApplyAnimationSource(ctx, panim, BuildAnimationSource(ctx, state, animation, filename));
        ApplyOptions(ctx, animation, panim);
        ApplyCommands(ctx, state, animation, panim);
        FinishAnimation(panim);
    }
}

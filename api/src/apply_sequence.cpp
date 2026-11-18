// Builder front end, Anim part: sequences. Each one is allocated by ProcessCmdSequence, filled through the apply
// functions of the $sequence options (studiomdl/apply_anim.cpp) and completed by ProcessSequence, the code the QC path
// ends a $sequence with. The animations were applied before (ApplyAnimations), so their entries are looked up by name.
#include "apply.hpp"

#include <cmath>
#include <format>

#include "studiomdl/apply_anim.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_commands.h"
#include "studiomdl_errors.h"

namespace {
    s_animation_t *FindApplied(StudioMdlState &ctx, const smdl_animation &animation) {
        s_animation_t *panim = LookupAnimation(ctx, animation.name.c_str());
        if (!panim)
            MdlError("animation '{}' was not applied", animation.name);
        return panim;
    }

    // The pose parameter by name (ApplyRig created them in id order)
    int PoseIndex(StudioMdlState &ctx, const ApplyState &state, smdl_pose_param_id pose) {
        return LookupPoseParameter(ctx, state.model.rig.poseParameters[pose].name.c_str());
    }

    // The event frame of a cycle placed on the first animation (the writer converts frame / (frames - 1) back)
    int CycleToFrame(float cycle, const s_animation_t *first) {
        const int lastFrame = first->numframes - 1;
        if (lastFrame <= 0)
            return 0;
        return static_cast<int>(std::lround(cycle * static_cast<float>(lastFrame)));
    }

    void ApplySequenceOptions(StudioMdlState &ctx, const ApplyState &state, const smdl_sequence &sequence,
                              s_sequence_t *pseq, const s_animation_t *first) {
        const RigDesc &rig = state.model.rig;

        if (sequence.activity)
            ApplySequenceActivity(ctx, pseq, *sequence.activity, sequence.activityWeight);
        for (const std::string &modifier: sequence.activityModifiers)
            ApplySequenceActivityModifier(pseq, modifier);
        for (const smdl_sequence::Event &event: sequence.events) {
            const int frame = event.byCycle ? CycleToFrame(event.cycle, first) : event.frame;
            ApplySequenceEvent(pseq, event.name, frame, event.options);
        }
        for (const smdl_sequence::AnimTag &tag: sequence.animTags)
            ApplySequenceAnimTag(pseq, tag.name, tag.cycle);

        if (sequence.blendWidth != 0)
            pseq->groupsize[0] = static_cast<int>(sequence.blendWidth);
        for (const smdl_sequence::Blend &blend: sequence.blends) {
            const int pose = PoseIndex(ctx, state, blend.pose);
            if (!blend.calc) {
                ApplySequenceBlend(ctx, pseq, pose, blend.start, blend.end);
                continue;
            }
            const int attachment = LookupAttachment(ctx, rig.attachments[blend.attachment].name.c_str());
            if (attachment == -1)
                MdlError("Unknown calcblend attachment \"{}\"\n", rig.attachments[blend.attachment].name);
            ApplySequenceCalcBlend(pseq, pose, attachment, static_cast<StudioMotionFlags>(blend.control));
        }
        if (sequence.blendReference)
            pseq->paramanim = FindApplied(ctx, *sequence.blendReference);
        if (sequence.blendCompare)
            pseq->paramcompanim = FindApplied(ctx, *sequence.blendCompare);
        if (sequence.blendCenter)
            pseq->paramcenter = FindApplied(ctx, *sequence.blendCenter);
        if (sequence.poseCycle)
            ApplySequencePoseCycle(pseq, PoseIndex(ctx, state, *sequence.poseCycle));

        if (sequence.node) {
            if (sequence.node->isNode)
                ApplySequenceNode(ctx, pseq, sequence.node->from);
            else
                ApplySequenceTransition(ctx, pseq, sequence.node->from, sequence.node->to, sequence.node->reverse);
        }
        if (sequence.exitPhase)
            pseq->exitphase = *sequence.exitPhase;
        if (sequence.fadeIn)
            pseq->fadeintime = *sequence.fadeIn;
        if (sequence.fadeOut)
            pseq->fadeouttime = *sequence.fadeOut;

        const uint32_t flags = sequence.flags;
        if (flags & SMDL_SEQUENCE_SNAP)
            pseq->flags |= StudioSequenceFlags::Snap;
        if (flags & SMDL_SEQUENCE_DELTA)
            pseq->flags |= StudioSequenceFlags::Delta | StudioSequenceFlags::Post;
        if (flags & SMDL_SEQUENCE_PRE_DELTA)
            pseq->flags |= StudioSequenceFlags::Delta;
        if (flags & SMDL_SEQUENCE_WORLD_SPACE)
            pseq->flags |= StudioSequenceFlags::World | StudioSequenceFlags::Post;
        if (flags & SMDL_SEQUENCE_WORLD_RELATIVE)
            pseq->flags |= StudioSequenceFlags::WorldAndRelative | StudioSequenceFlags::Post;
        if (flags & SMDL_SEQUENCE_POST)
            pseq->flags |= StudioSequenceFlags::Post;
        if (flags & SMDL_SEQUENCE_AUTOPLAY)
            pseq->flags |= StudioSequenceFlags::Autoplay;
        if (flags & SMDL_SEQUENCE_REALTIME)
            pseq->flags |= StudioSequenceFlags::Realtime;
        if (flags & SMDL_SEQUENCE_HIDDEN)
            pseq->flags |= StudioSequenceFlags::Hidden;
        if (sequence.rootDriver)
            ApplySequenceRootDriver(pseq, state.model.skeleton.bones[*sequence.rootDriver].name);

        for (const smdl_sequence::Layer &layer: sequence.layers) {
            if (!layer.isBlend) {
                ApplySequenceAddLayer(pseq, layer.layer->name, layer.local);
                continue;
            }
            BlendLayerParams params;
            params.layer = layer.layer->name;
            params.start = layer.start;
            params.peak = layer.peak;
            params.tail = layer.tail;
            params.end = layer.end;
            params.xfade = (layer.flags & SMDL_LAYER_XFADE) != 0;
            params.spline = (layer.flags & SMDL_LAYER_SPLINE) != 0;
            params.noBlend = (layer.flags & SMDL_LAYER_NO_BLEND) != 0;
            params.local = (layer.flags & SMDL_LAYER_LOCAL) != 0;
            if (layer.flags & SMDL_LAYER_POSE)
                params.poseParameter = PoseIndex(ctx, state, layer.pose);
            ApplySequenceBlendLayer(pseq, params);
        }
        for (const smdl_sequence::IkLock &lock: sequence.ikLocks)
            ApplySequenceIkLock(pseq, rig.ikChains[lock.chain].name, lock.positionWeight, lock.localQWeight);
        if (!sequence.keyValues.empty())
            pseq->KeyValue += sequence.keyValues;
    }
}

void ApplySequences(StudioMdlState &ctx, ApplyState &state) {
    for (const std::unique_ptr<smdl_sequence> &entry: state.model.sequences.list) {
        const smdl_sequence &sequence = *entry;
        ScopedApplyObject object(state, std::format("sequence '{}'", sequence.name));

        s_sequence_t *pseq = ProcessCmdSequence(ctx, sequence.name.c_str());
        if (!pseq)
            MdlError("the name of sequence '{}' is taken by a declared animation", sequence.name);

        std::vector<s_animation_t *> animations;
        animations.reserve(sequence.animations.size());
        for (const smdl_animation *animation: sequence.animations)
            animations.push_back(FindApplied(ctx, *animation));
        if (animations.empty())
            ProcessSequence(pseq, 0, nullptr, false); // reports `sequence "x": no animations found`

        // the QC's `hack to allow animation commands to refer to same sequence`
        pseq->panim[0][0] = animations[0];

        ApplySequenceOptions(ctx, state, sequence, pseq, animations[0]);
        ProcessSequence(pseq, static_cast<int>(animations.size()), animations.data(), false);
    }
}

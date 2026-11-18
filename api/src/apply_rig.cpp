// Builder front end, Anim part: attachments, hitbox sets, hit groups, pose parameters, IK chains and autoplay locks,
// weight lists. Each object goes through the apply function its QC command uses (studiomdl/apply_anim.cpp). Bones are
// passed by name, taken from the model skeleton; the compile looks them up when the model is simplified, so an unknown
// bone is reported exactly as for the QC.
#include "apply.hpp"

#include <format>

#include "studiomdl/apply_anim.hpp"
#include "studiomdl/studiomdl_global_state.hpp"

namespace {
    const std::string &BoneName(const ApplyState &state, smdl_bone_id bone) {
        return state.model.skeleton.bones[bone].name;
    }

    AttachmentParams MakeAttachment(const ApplyState &state, const AttachmentDesc &desc,
                                    const AttachmentRevisionDesc &revision) {
        AttachmentParams params;
        params.name = desc.name;
        params.boneName = BoneName(state, revision.bone);
        params.position = revision.position;
        params.rigid = revision.rigid;
        params.worldAlign = revision.worldAlign;
        for (const AttachmentRevisionDesc::Step &step: revision.steps) {
            AttachmentLocal local;
            switch (step.kind) {
                case AttachmentRevisionDesc::StepKind::Absolute:
                    local.kind = AttachmentLocal::Kind::Absolute;
                    break;
                case AttachmentRevisionDesc::StepKind::Angles:
                    local.kind = AttachmentLocal::Kind::Angles;
                    local.angles = step.angles;
                    break;
                case AttachmentRevisionDesc::StepKind::Euler:
                    local.kind = AttachmentLocal::Kind::RadianEuler;
                    local.euler = step.euler;
                    break;
                case AttachmentRevisionDesc::StepKind::Quaternion:
                    local.kind = AttachmentLocal::Kind::Quaternion;
                    local.quaternion = step.quaternion;
                    break;
                case AttachmentRevisionDesc::StepKind::Axes:
                    local.kind = AttachmentLocal::Kind::XZAxes;
                    local.xAxis = step.xAxis;
                    local.zAxis = step.zAxis;
                    break;
            }
            params.locals.push_back(local);
        }
        return params;
    }

    std::vector<WeightListEntry> MakeEntries(const ApplyState &state, const WeightListDesc &list) {
        std::vector<WeightListEntry> entries;
        entries.reserve(list.entries.size());
        for (const WeightEntryDesc &entry: list.entries)
            entries.push_back({BoneName(state, entry.bone), entry.weight, entry.positionWeight});
        return entries;
    }
}

void ApplyRig(StudioMdlState &ctx, ApplyState &state) {
    const RigDesc &rig = state.model.rig;

    for (const AttachmentDesc &attachment: rig.attachments) {
        ScopedApplyObject object(state, std::format("attachment '{}'", attachment.name));
        ApplyAttachment(ctx, MakeAttachment(state, attachment, attachment.revisions.front()));
        const size_t index = ctx.bones.attachments.size() - 1;
        for (size_t i = 1; i < attachment.revisions.size(); ++i)
            ApplyAttachment(ctx, MakeAttachment(state, attachment, attachment.revisions[i]), index);
    }

    for (const HitGroupDesc &group: rig.hitGroups) {
        ScopedApplyObject object(state, std::format("hit group {}", group.group));
        ApplyHitGroup(ctx, group.group, BoneName(state, group.bone));
    }

    for (const HitboxSetDesc &set: rig.hitboxSets) {
        ScopedApplyObject object(state, std::format("hitbox set '{}'", set.name));
        ApplyHitboxSet(ctx, set.name);
        for (const HitboxDesc &hitbox: set.hitboxes) {
            HitboxParams params;
            params.group = hitbox.group;
            params.boneName = BoneName(state, hitbox.bone);
            params.mins = hitbox.mins;
            params.maxs = hitbox.maxs;
            params.angles = hitbox.angles;
            params.capsuleRadius = hitbox.capsuleRadius;
            params.name = hitbox.name;
            ApplyHitbox(ctx, params);
        }
    }

    for (const PoseParameterDesc &pose: rig.poseParameters) {
        ScopedApplyObject object(state, std::format("pose parameter '{}'", pose.name));
        PoseParameterParams params;
        params.name = pose.name;
        params.min = pose.min;
        params.max = pose.max;
        switch (pose.loopMode) {
            case SMDL_POSE_LOOP_NONE:
                break;
            case SMDL_POSE_LOOP_WRAP:
                params.loop = PoseParameterParams::Loop::Wrap;
                break;
            case SMDL_POSE_LOOP_RANGE:
                params.loop = PoseParameterParams::Loop::Value;
                params.loopValue = pose.loopRange;
                break;
        }
        ApplyPoseParameter(ctx, params);
    }

    for (const IkChainDesc &chain: rig.ikChains) {
        ScopedApplyObject object(state, std::format("IK chain '{}'", chain.name));
        IkChainParams params;
        params.name = chain.name;
        params.boneName = BoneName(state, chain.endBone);
        params.axis = static_cast<StudioMotionFlags>(chain.axis);
        params.value = chain.value;
        params.height = chain.height;
        params.floor = chain.floor;
        params.radius = chain.radius;
        params.knee = chain.knee;
        params.center = chain.center;
        ApplyIkChain(ctx, params);
    }

    for (const IkLockDesc &lock: rig.ikLocks) {
        const IkChainDesc &chain = rig.ikChains[lock.chain];
        ScopedApplyObject object(state, std::format("IK autoplay lock '{}'", chain.name));
        ApplyIkAutoplayLock(ctx, chain.name, lock.positionWeight, lock.localQWeight);
    }

    // list 0 is the default list: a model that never fills it keeps the compile's empty list
    if (!rig.weightLists.front().entries.empty()) {
        ScopedApplyObject object(state, "default weight list");
        const std::vector<WeightListEntry> entries = MakeEntries(state, rig.weightLists.front());
        ApplyDefaultWeightList(ctx, entries);
    }
    for (size_t i = 1; i < rig.weightLists.size(); ++i) {
        const WeightListDesc &list = rig.weightLists[i];
        ScopedApplyObject object(state, std::format("weight list '{}'", list.name));
        const std::vector<WeightListEntry> entries = MakeEntries(state, list);
        ApplyWeightList(ctx, list.name, entries);
    }
}

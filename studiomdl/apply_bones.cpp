#include "studiomdl/apply_bones.hpp"

#include <cstring>

#include "studiomdl_errors.h"

namespace {
    // The bone name tables store names in fixed buffers; the QC parsers refuse longer tokens, so do the same here.
    void CheckNameFits(std::string_view command, std::string_view name, size_t capacity) {
        if (name.size() >= capacity) {
            MdlError("{}: bone name '{}' is too long ({} characters, the maximum is {})\n", command, name, name.size(),
                     capacity - 1);
        }
    }

    template<size_t N>
    void CopyName(char (&out)[N], std::string_view command, std::string_view name) {
        CheckNameFits(command, name, N);
        std::memcpy(out, name.data(), name.size());
        out[name.size()] = '\0';
    }

    void CopyName(StudioName &out, std::string_view command, std::string_view name) {
        CheckNameFits(command, name, MAXSTUDIONAME);
        out = name;
    }
}

void ApplyRootBone(StudioMdlState &ctx, const std::string_view bone) {
    CopyName(ctx.script.rootname, "$root", bone);
}

void ApplyBoneController(StudioMdlState &ctx, const int inputField, const std::string_view bone,
                         const StudioMotionFlags type, const float start, const float end) {
    s_bonecontroller_t controller;
    controller.inputfield = inputField;
    CopyName(controller.name, "$controller", bone);
    controller.type = type;
    controller.start = start;
    controller.end = end;

    if (HasFlag(controller.type, StudioMotionFlags::XR | StudioMotionFlags::YR | StudioMotionFlags::ZR)) {
        if (((int) (controller.start + 360) % 360) ==
            ((int) (controller.end + 360) % 360)) {
            controller.type |= StudioMotionFlags::RLoop;
        }
    }
    ctx.bones.bonecontrollers.emplace_back(controller);
}

void ApplyScreenAlign(StudioMdlState &ctx, const std::string_view bone, const BoneFlags flags) {
    s_screenalignedbone_t &entry = ctx.bones.screenalignedbone.emplace_back();
    CopyName(entry.name, "$screenalign", bone);
    entry.flags = flags;
}

void ApplyWorldAlign(StudioMdlState &ctx, const std::string_view bone) {
    s_worldalignedbone_t &entry = ctx.bones.worldalignedbone.emplace_back();
    CopyName(entry.name, "$worldalign", bone);
    entry.flags = BoneFlags::WorldAlign;
}

void ApplyBoneMerge(StudioMdlState &ctx, const std::string_view bone) {
    ctx.bones.bone_merge.emplace_back();
    CopyName(ctx.bones.bone_merge.back().bonename, "$bonemerge", bone);
}

void ApplyBoneAlwaysSetup(StudioMdlState &ctx, const std::string_view bone) {
    ctx.bones.bone_always_setup.emplace_back();
    CopyName(ctx.bones.bone_always_setup.back().bonename, "$bonealwayssetup", bone);
}

void ApplyLimitRotation(StudioMdlState &ctx, const std::string_view bone, const std::span<const std::string> sequences) {
    s_limitrotation_t &limit = ctx.bones.limitrotation.emplace_back();
    CopyName(limit.name, "$limitrotation", bone);
    limit.sequencenames.assign(sequences.begin(), sequences.end());
}

void ApplyBoneSaveFrame(StudioMdlState &ctx, const std::string_view bone, const bool position, const bool rotation,
                        const bool rotation64) {
    s_bonesaveframe_t entry;
    CopyName(entry.name, "$bonesaveframe", bone);
    entry.bSavePos = position;
    entry.bSaveRot = rotation;
    entry.bSaveRot64 = rotation64;
    ctx.bones.bonesaveframe.push_back(entry);
}

void ApplyRealignBones(StudioMdlState &ctx) {
    ctx.options.realignbones = true;
}

void ApplyLockBoneLengths(StudioMdlState &ctx) {
    ctx.options.lockBoneLengths = true;
}

void ApplyUnlockDefineBones(StudioMdlState &ctx) {
    ctx.options.defineBonesLockedByDefault = false;
}

void ApplyForceRealign(StudioMdlState &ctx, const std::string_view bone, const RadianEuler &rotation) {
    s_forcedrealign_t &realign = ctx.bones.forcedrealign.emplace_back();
    CopyName(realign.name, "$forcerealign", bone);
    realign.rot = rotation;
}

void ApplyCollapseBones(StudioMdlState &ctx) {
    ctx.options.collapse_bones = true;
}

void ApplyCollapseBonesAggressive(StudioMdlState &ctx) {
    ctx.options.collapse_bones = true;
    ctx.options.collapse_bones_aggressive = true;
}

void ApplyAlwaysCollapse(StudioMdlState &ctx, const std::string_view bone) {
    ctx.options.collapse_bones = true;
    ctx.bones.collapse.emplace_back(bone);
}

void ApplyDefineBone(StudioMdlState &ctx, const DefineBoneParams &params) {
    s_importbone_t &bone = ctx.bones.importbone.emplace_back();
    CopyName(bone.name, "$definebone", params.name);
    CopyName(bone.parent, "$definebone", params.parent);

    bone.bUnlocked = params.unlocked.value_or(!ctx.options.defineBonesLockedByDefault);
    bone.rawLocal = params.rawLocal;

    if (params.realign) {
        bone.bPreAligned = true;
        bone.srcRealign = *params.realign;
    }
    else {
        SetIdentityMatrix(bone.srcRealign);
    }
}

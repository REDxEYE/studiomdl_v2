// Builder front end, Core part: per-bone options and bone processing options. Everything here is the parse-time part
// of the QC commands (the tables SimplifyModel reads later); bones are passed by name, taken from the model skeleton.
// The shared apply functions are in studiomdl/apply_bones.cpp (the QC parsers call the same ones).
#include "apply.hpp"

#include "studiomdl/apply_bones.hpp"
#include "studiomdl/studiomdl_global_state.hpp"

namespace {
    // bone name + object path context for errors
    const std::string &BoneName(const ApplyState &state, smdl_bone_id bone) { return state.model.skeleton.bones[bone].name; }

    std::string BoneObject(const ApplyState &state, smdl_bone_id bone) {
        return std::format("bone '{}'", BoneName(state, bone));
    }

    StudioMotionFlags ControllerType(smdl_bone_controller_type type) {
        switch (type) {
            case SMDL_BONE_CONTROLLER_X: return StudioMotionFlags::X;
            case SMDL_BONE_CONTROLLER_Y: return StudioMotionFlags::Y;
            case SMDL_BONE_CONTROLLER_Z: return StudioMotionFlags::Z;
            case SMDL_BONE_CONTROLLER_XR: return StudioMotionFlags::XR;
            case SMDL_BONE_CONTROLLER_YR: return StudioMotionFlags::YR;
            case SMDL_BONE_CONTROLLER_ZR: return StudioMotionFlags::ZR;
        }
        return StudioMotionFlags::Invalid; // rejected when the controller is added
    }
}

void ApplySkeleton(StudioMdlState &ctx, ApplyState &state) {
    const BoneOptionsDesc &options = state.model.boneOptions;

    // $unlockdefinebones changes the default of every $definebone that follows it
    if (options.unlockDefineBones)
        ApplyUnlockDefineBones(ctx);
    if (options.realignBones)
        ApplyRealignBones(ctx);
    if (options.lockBoneLengths)
        ApplyLockBoneLengths(ctx);
    if (options.collapseBones)
        ApplyCollapseBones(ctx);
    if (options.collapseBonesAggressive)
        ApplyCollapseBonesAggressive(ctx);

    if (options.rootBone) {
        ScopedApplyObject object(state, BoneObject(state, *options.rootBone));
        ApplyRootBone(ctx, BoneName(state, *options.rootBone));
    }

    for (const DefineBoneDesc &define: options.defineBones) {
        ScopedApplyObject object(state, BoneObject(state, define.bone));
        DefineBoneParams params;
        params.name = BoneName(state, define.bone);
        if (define.parent != SMDL_NO_BONE)
            params.parent = BoneName(state, define.parent);
        if (define.lock == SMDL_DEFINE_BONE_LOCKED)
            params.unlocked = false;
        else if (define.lock == SMDL_DEFINE_BONE_UNLOCKED)
            params.unlocked = true;
        AngleMatrix(define.rotation, define.position, params.rawLocal);
        if (define.realign)
            AngleMatrix(define.realign->rotation, define.realign->position, params.realign.emplace());
        ApplyDefineBone(ctx, params);
    }

    for (const ForcedRealignDesc &realign: options.forcedRealigns) {
        ScopedApplyObject object(state, BoneObject(state, realign.bone));
        ApplyForceRealign(ctx, BoneName(state, realign.bone), realign.rotation);
    }

    for (const smdl_bone_id bone: options.alwaysCollapse) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyAlwaysCollapse(ctx, BoneName(state, bone));
    }

    for (const BoneControllerDesc &controller: options.controllers) {
        ScopedApplyObject object(state, BoneObject(state, controller.bone));
        ApplyBoneController(ctx, static_cast<int>(controller.input), BoneName(state, controller.bone),
                            ControllerType(controller.type), controller.start, controller.end);
    }

    for (const auto &[bone, mode]: options.screenAlign) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyScreenAlign(ctx, BoneName(state, bone),
                         mode == SMDL_SCREEN_ALIGN_CYLINDER ? BoneFlags::ScreenAlignCylinder : BoneFlags::ScreenAlignSphere);
    }

    for (const smdl_bone_id bone: options.worldAlign) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyWorldAlign(ctx, BoneName(state, bone));
    }

    for (const smdl_bone_id bone: options.boneMerge) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyBoneMerge(ctx, BoneName(state, bone));
    }

    for (const smdl_bone_id bone: options.boneAlwaysSetup) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyBoneAlwaysSetup(ctx, BoneName(state, bone));
    }

    for (const auto &[bone, sequences]: options.limitRotation) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyLimitRotation(ctx, BoneName(state, bone), sequences);
    }

    for (const auto &[bone, flags]: options.saveFrame) {
        ScopedApplyObject object(state, BoneObject(state, bone));
        ApplyBoneSaveFrame(ctx, BoneName(state, bone), (flags & SMDL_BONE_SAVE_POSITION) != 0,
                           (flags & SMDL_BONE_SAVE_ROTATION) != 0, (flags & SMDL_BONE_SAVE_ROTATION64) != 0);
    }
}

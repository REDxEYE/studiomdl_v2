// smdl_skeleton.h: per-bone options and bone processing options (store only; the compile applies them in
// ApplySkeleton, apply_skeleton.cpp)
#include "studiomdl/c/smdl_skeleton.h"
#include "studiomdl/studiomdl.hpp"

#include <cstring>

#include "api_util.hpp"
#include "core_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    constexpr size_t kMaxDefineBones = MAXSTUDIOSRCBONES; // StudioMdlBones::importbone / bonecontrollers capacity
    constexpr size_t kMaxForcedRealigns = 256;            // MAXSTUDIOBONES

    // Range check of a bone id; returns the failing status or SMDL_OK
    smdl_status CheckBone(const smdl_model &model, smdl_bone_id bone, const char *what) {
        if (!model.skeleton.valid(bone))
            return Fail(&model, SMDL_ERROR_OUT_OF_RANGE, "{}: bone id {} is not a bone of the model ({} bones)", what,
                        bone, model.skeleton.bones.size());
        return SMDL_OK;
    }

    // parent of a defined bone: SMDL_NO_BONE or a bone
    smdl_status CheckParent(const smdl_model &model, smdl_bone_id parent, const char *what) {
        if (parent == SMDL_NO_BONE)
            return SMDL_OK;
        return CheckBone(model, parent, what);
    }

    smdl_status ReadPosition(const smdl_model &model, const float position[3], const char *what, Vector &out) {
        if (!position || !AllFinite(position, 3))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: position is null or not finite", what);
        out = ToVector(position);
        return SMDL_OK;
    }

    smdl_status ReadEuler(const smdl_model &model, const float euler[3], const char *what, RadianEuler &out) {
        if (!euler || !AllFinite(euler, 3))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: euler angles are null or not finite", what);
        out = RadianEuler(euler[0], euler[1], euler[2]);
        return SMDL_OK;
    }

    smdl_status ReadQuaternion(const smdl_model &model, const float quaternion[4], const char *what,
                               RadianEuler &out) {
        if (!quaternion || !AllFinite(quaternion, 4))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: rotation is null or not finite", what);
        if (!QuaternionToRadianEuler(quaternion, Vector(0, 0, 0), out))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: rotation is a zero quaternion", what);
        return SMDL_OK;
    }

    template<class Set>
    void SetMember(Set &set, smdl_bone_id bone, int enabled) {
        if (enabled)
            set.insert(bone);
        else
            set.erase(bone);
    }

    // Shared by the quaternion and euler forms (and by the C API for null vectors); throw smdl::Error on failure
    void DefineBone(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent, const float position[3],
                    const float *rotation, bool euler) {
        constexpr const char *what = "define bone";
        RunBody(model, [&] {
            if (const auto status = CheckBone(*model, bone, what))
                return status;
            if (const auto status = CheckParent(*model, parent, what))
                return status;
            DefineBoneDesc desc;
            desc.bone = bone;
            desc.parent = parent;
            if (const auto status = ReadPosition(*model, position, what, desc.position))
                return status;
            if (const auto status = euler ? ReadEuler(*model, rotation, what, desc.rotation)
                                          : ReadQuaternion(*model, rotation, what, desc.rotation))
                return status;
            auto &list = model->boneOptions.defineBones;
            for (const DefineBoneDesc &existing: list) {
                if (existing.bone == bone)
                    return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "define bone: bone '{}' is already defined",
                                model->skeleton.bones[bone].name);
            }
            if (list.size() >= kMaxDefineBones)
                return Fail(model, SMDL_ERROR_LIMIT, "define bone: more than {} defined bones", kMaxDefineBones);
            list.push_back(desc);
            return Ok(model);
        });
    }

    void SetDefineBoneRealign(smdl_model *model, smdl_bone_id bone, const float position[3], const float *rotation,
                              bool euler) {
        constexpr const char *what = "define bone realign";
        RunBody(model, [&] {
            if (const auto status = CheckBone(*model, bone, what))
                return status;
            DefineBoneDesc::Realign realign;
            if (const auto status = ReadPosition(*model, position, what, realign.position))
                return status;
            if (const auto status = euler ? ReadEuler(*model, rotation, what, realign.rotation)
                                          : ReadQuaternion(*model, rotation, what, realign.rotation))
                return status;
            for (DefineBoneDesc &existing: model->boneOptions.defineBones) {
                if (existing.bone == bone) {
                    existing.realign = realign;
                    return Ok(model);
                }
            }
            return Fail(model, SMDL_ERROR_NOT_FOUND, "{}: bone '{}' has not been defined", what,
                        model->skeleton.bones[bone].name);
        });
    }

    void AddForceRealign(smdl_model *model, smdl_bone_id bone, const float *rotation, bool euler) {
        constexpr const char *what = "force realign";
        RunBody(model, [&] {
            if (const auto status = CheckBone(*model, bone, what))
                return status;
            ForcedRealignDesc desc;
            desc.bone = bone;
            if (const auto status = euler ? ReadEuler(*model, rotation, what, desc.rotation)
                                          : ReadQuaternion(*model, rotation, what, desc.rotation))
                return status;
            auto &list = model->boneOptions.forcedRealigns;
            if (list.size() >= kMaxForcedRealigns)
                return Fail(model, SMDL_ERROR_LIMIT, "{}: more than {} realigns", what, kMaxForcedRealigns);
            list.push_back(desc);
            return Ok(model);
        });
    }

    // set/clear of a per-bone boolean option
    template<class Member>
    void SetBoneFlag(smdl_model *model, smdl_bone_id bone, int enabled, const char *what, Member member) {
        RunBody(model, [&] {
            if (const auto status = CheckBone(*model, bone, what))
                return status;
            SetMember(model->boneOptions.*member, bone, enabled);
            return Ok(model);
        });
    }

    // set/clear of a model-wide boolean option
    void SetModelFlag(smdl_model *model, bool BoneOptionsDesc::*member, int enabled) {
        RunBody(model, [&] {
            model->boneOptions.*member = enabled != 0;
            return Ok(model);
        });
    }

    // $limitrotation with C arrays (also used by the C API for invalid names)
    void LimitRotation(smdl_model *model, smdl_bone_id bone, const char *const *sequences, size_t sequence_count) {
        RunBody(model, [&] {
            constexpr const char *what = "limit rotation";
            if (const auto status = CheckBone(*model, bone, what))
                return status;
            if (sequence_count && !sequences)
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: sequence names are null", what);
            std::vector<std::string> names;
            names.reserve(sequence_count);
            for (size_t i = 0; i < sequence_count; ++i) {
                if (!ValidName(sequences[i]))
                    return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: sequence name {} is null or empty", what, i);
                names.emplace_back(sequences[i]);
            }
            model->boneOptions.limitRotation[bone] = std::move(names);
            return Ok(model);
        });
    }
}

namespace smdl {
namespace detail {

void ModelSkeletonApi::setRootBone(BoneId bone) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (bone == SMDL_NO_BONE) {
            model->boneOptions.rootBone.reset();
            return Ok(model);
        }
        if (const auto status = CheckBone(*model, bone, "root bone"))
            return status;
        model->boneOptions.rootBone = bone;
        return Ok(model);
    });
}

void ModelSkeletonApi::defineBone(BoneId bone, BoneId parent, const Vec3 &position, const Quat &rotation) {
    DefineBone(model(), bone, parent, position.data(), rotation.data(), false);
}

void ModelSkeletonApi::defineBoneEuler(BoneId bone, BoneId parent, const Vec3 &position, const Vec3 &euler) {
    DefineBone(model(), bone, parent, position.data(), euler.data(), true);
}

void ModelSkeletonApi::setDefineBoneLock(BoneId bone, smdl_define_bone_lock lock) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (const auto status = CheckBone(*model, bone, "define bone lock"))
            return status;
        if (lock != SMDL_DEFINE_BONE_LOCK_DEFAULT && lock != SMDL_DEFINE_BONE_LOCKED && lock != SMDL_DEFINE_BONE_UNLOCKED)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "define bone lock: unknown mode {}", static_cast<int>(lock));
        for (DefineBoneDesc &existing: model->boneOptions.defineBones) {
            if (existing.bone == bone) {
                existing.lock = lock;
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "define bone lock: bone '{}' has not been defined",
                    model->skeleton.bones[bone].name);
    });
}

void ModelSkeletonApi::setDefineBoneRealign(BoneId bone, const Vec3 &position, const Quat &rotation) {
    SetDefineBoneRealign(model(), bone, position.data(), rotation.data(), false);
}

void ModelSkeletonApi::setDefineBoneRealignEuler(BoneId bone, const Vec3 &position, const Vec3 &euler) {
    SetDefineBoneRealign(model(), bone, position.data(), euler.data(), true);
}

void ModelSkeletonApi::setUnlockDefineBones(bool enabled) {
    SetModelFlag(model(), &BoneOptionsDesc::unlockDefineBones, enabled);
}

void ModelSkeletonApi::setLockBoneLengths(bool enabled) {
    SetModelFlag(model(), &BoneOptionsDesc::lockBoneLengths, enabled);
}

void ModelSkeletonApi::setRealignBones(bool enabled) {
    SetModelFlag(model(), &BoneOptionsDesc::realignBones, enabled);
}

void ModelSkeletonApi::addForceRealign(BoneId bone, const Quat &rotation) {
    AddForceRealign(model(), bone, rotation.data(), false);
}

void ModelSkeletonApi::addForceRealignEuler(BoneId bone, const Vec3 &euler) {
    AddForceRealign(model(), bone, euler.data(), true);
}

void ModelSkeletonApi::setCollapseBones(bool enabled) {
    SetModelFlag(model(), &BoneOptionsDesc::collapseBones, enabled);
}

void ModelSkeletonApi::setCollapseBonesAggressive(bool enabled) {
    SetModelFlag(model(), &BoneOptionsDesc::collapseBonesAggressive, enabled);
}

void ModelSkeletonApi::setAlwaysCollapse(BoneId bone, bool enabled) {
    SetBoneFlag(model(), bone, enabled, "always collapse", &BoneOptionsDesc::alwaysCollapse);
}

uint32_t ModelSkeletonApi::addBoneController(uint32_t input, BoneId bone, smdl_bone_controller_type type, float start,
                                             float end) {
    smdl_model *model = this->model();
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_index = &result;
    RunBody(model, [&] {
        constexpr const char *what = "bone controller";
        if (const auto status = CheckBone(*model, bone, what))
            return status;
        if (input > SMDL_BONE_CONTROLLER_MOUTH)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: input {} is not 0-3 or the mouth (4)", what, input);
        if (type < SMDL_BONE_CONTROLLER_X || type > SMDL_BONE_CONTROLLER_ZR)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: unknown type {}", what, static_cast<int>(type));
        if (!std::isfinite(start) || !std::isfinite(end))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: start or end is not finite", what);
        auto &list = model->boneOptions.controllers;
        if (list.size() >= kMaxDefineBones)
            return Fail(model, SMDL_ERROR_LIMIT, "{}: more than {} controllers", what, kMaxDefineBones);
        list.push_back({input, bone, type, start, end});
        if (out_index)
            *out_index = static_cast<uint32_t>(list.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelSkeletonApi::setScreenAlign(BoneId bone, smdl_screen_align_mode mode) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (const auto status = CheckBone(*model, bone, "screen align"))
            return status;
        if (mode == SMDL_SCREEN_ALIGN_NONE)
            model->boneOptions.screenAlign.erase(bone);
        else if (mode == SMDL_SCREEN_ALIGN_SPHERE || mode == SMDL_SCREEN_ALIGN_CYLINDER)
            model->boneOptions.screenAlign[bone] = mode;
        else
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "screen align: unknown mode {}", static_cast<int>(mode));
        return Ok(model);
    });
}

void ModelSkeletonApi::setWorldAlign(BoneId bone, bool enabled) {
    SetBoneFlag(model(), bone, enabled, "world align", &BoneOptionsDesc::worldAlign);
}

void ModelSkeletonApi::setBoneMerge(BoneId bone, bool enabled) {
    SetBoneFlag(model(), bone, enabled, "bone merge", &BoneOptionsDesc::boneMerge);
}

void ModelSkeletonApi::setBoneAlwaysSetup(BoneId bone, bool enabled) {
    SetBoneFlag(model(), bone, enabled, "bone always setup", &BoneOptionsDesc::boneAlwaysSetup);
}

void ModelSkeletonApi::setLimitRotation(BoneId bone, std::span<const std::string> sequenceNames) {
    std::vector<const char *> names;
    names.reserve(sequenceNames.size());
    for (const std::string &name: sequenceNames)
        names.push_back(name.c_str());
    LimitRotation(model(), bone, names.data(), names.size());
}

void ModelSkeletonApi::setBoneSaveFrame(BoneId bone, uint32_t flags) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (const auto status = CheckBone(*model, bone, "bone save frame"))
            return status;
        constexpr uint32_t all = SMDL_BONE_SAVE_POSITION | SMDL_BONE_SAVE_ROTATION | SMDL_BONE_SAVE_ROTATION64;
        if (flags & ~all)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone save frame: unknown flags 0x{:x}", flags & ~all);
        model->boneOptions.saveFrame[bone] = flags;
        return Ok(model);
    });
}

} // namespace detail
} // namespace smdl

// The C API: null checks (and null vectors, reported by the shared helpers in the old order), then the C++ method
extern "C" {

SMDL_API smdl_status smdl_model_set_root_bone(smdl_model *model, smdl_bone_id bone) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setRootBone(bone); });
}

SMDL_API smdl_status smdl_model_define_bone(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                            const float position[3], const float rotation[4]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        if (position && rotation)
            smdl::detail::BorrowModel(model).defineBone(bone, parent, {position[0], position[1], position[2]},
                                                        {rotation[0], rotation[1], rotation[2], rotation[3]});
        else
            DefineBone(model, bone, parent, position, rotation, false);
    });
}

SMDL_API smdl_status smdl_model_define_bone_euler(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                  const float position[3], const float euler[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        if (position && euler)
            smdl::detail::BorrowModel(model).defineBoneEuler(bone, parent, {position[0], position[1], position[2]},
                                                             {euler[0], euler[1], euler[2]});
        else
            DefineBone(model, bone, parent, position, euler, true);
    });
}

SMDL_API smdl_status smdl_model_set_define_bone_lock(smdl_model *model, smdl_bone_id bone, smdl_define_bone_lock lock) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setDefineBoneLock(bone, lock); });
}

SMDL_API smdl_status smdl_model_set_define_bone_realign(smdl_model *model, smdl_bone_id bone, const float position[3],
                                                        const float rotation[4]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        if (position && rotation)
            smdl::detail::BorrowModel(model).setDefineBoneRealign(
                bone, {position[0], position[1], position[2]}, {rotation[0], rotation[1], rotation[2], rotation[3]});
        else
            SetDefineBoneRealign(model, bone, position, rotation, false);
    });
}

SMDL_API smdl_status smdl_model_set_define_bone_realign_euler(smdl_model *model, smdl_bone_id bone,
                                                              const float position[3], const float euler[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        if (position && euler)
            smdl::detail::BorrowModel(model).setDefineBoneRealignEuler(
                bone, {position[0], position[1], position[2]}, {euler[0], euler[1], euler[2]});
        else
            SetDefineBoneRealign(model, bone, position, euler, true);
    });
}

SMDL_API smdl_status smdl_model_set_unlock_define_bones(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setUnlockDefineBones(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_lock_bone_lengths(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setLockBoneLengths(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_realign_bones(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setRealignBones(enabled != 0); });
}

SMDL_API smdl_status smdl_model_add_force_realign(smdl_model *model, smdl_bone_id bone, const float rotation[4]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        if (rotation)
            smdl::detail::BorrowModel(model).addForceRealign(bone, {rotation[0], rotation[1], rotation[2], rotation[3]});
        else
            AddForceRealign(model, bone, rotation, false);
    });
}

SMDL_API smdl_status smdl_model_add_force_realign_euler(smdl_model *model, smdl_bone_id bone, const float euler[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        if (euler)
            smdl::detail::BorrowModel(model).addForceRealignEuler(bone, {euler[0], euler[1], euler[2]});
        else
            AddForceRealign(model, bone, euler, true);
    });
}

SMDL_API smdl_status smdl_model_set_collapse_bones(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCollapseBones(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_collapse_bones_aggressive(smdl_model *model, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setCollapseBonesAggressive(enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_always_collapse(smdl_model *model, smdl_bone_id bone, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAlwaysCollapse(bone, enabled != 0); });
}

SMDL_API smdl_status smdl_model_add_bone_controller(smdl_model *model, uint32_t input, smdl_bone_id bone,
                                                    smdl_bone_controller_type type, float start, float end,
                                                    uint32_t *out_index) {
    if (out_index)
        *out_index = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const uint32_t index = smdl::detail::BorrowModel(model).addBoneController(input, bone, type, start, end);
        if (out_index)
            *out_index = index;
    });
}

SMDL_API smdl_status smdl_model_set_screen_align(smdl_model *model, smdl_bone_id bone, smdl_screen_align_mode mode) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setScreenAlign(bone, mode); });
}

SMDL_API smdl_status smdl_model_set_world_align(smdl_model *model, smdl_bone_id bone, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setWorldAlign(bone, enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_bone_merge(smdl_model *model, smdl_bone_id bone, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBoneMerge(bone, enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_bone_always_setup(smdl_model *model, smdl_bone_id bone, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBoneAlwaysSetup(bone, enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_limit_rotation(smdl_model *model, smdl_bone_id bone,
                                                   const char *const *sequences, size_t sequence_count) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        bool valid = !sequence_count || sequences;
        for (size_t i = 0; valid && i < sequence_count; ++i)
            valid = ValidName(sequences[i]);
        if (!valid) {
            // reported by LimitRotation after the bone check, as before
            LimitRotation(model, bone, sequences, sequence_count);
            return;
        }
        std::vector<std::string> names;
        names.reserve(sequence_count);
        for (size_t i = 0; i < sequence_count; ++i)
            names.emplace_back(sequences[i]);
        smdl::detail::BorrowModel(model).setLimitRotation(bone, names);
    });
}

SMDL_API smdl_status smdl_model_set_bone_save_frame(smdl_model *model, smdl_bone_id bone, uint32_t flags) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setBoneSaveFrame(bone, flags); });
}

} // extern "C"

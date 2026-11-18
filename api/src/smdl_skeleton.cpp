// smdl_skeleton.h: bones of the model skeleton
#include "studiomdl/c/smdl_skeleton.h"
#include "studiomdl/studiomdl.hpp"

#include "core_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    // Shared by the quaternion and euler forms; throws smdl::Error on failure
    smdl_bone_id AddBone(smdl_model *model, const char *name, smdl_bone_id parent, const float position[3],
                         const float *rotation, bool rotationIsQuaternion) {
        smdl_bone_id result = SMDL_NO_BONE;
        smdl_bone_id *out_id = &result;
        RunBody(model, [&] {
            if (!ValidName(name))
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone name is null or empty");
            if (std::string_view(name).size() >= MAXSTUDIONAME)
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone name '{}' is too long (max {} bytes)", name,
                            MAXSTUDIONAME - 1);
            if (!position || !rotation)
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone '{}': position or rotation is null", name);
            if (!AllFinite(position, 3) || !AllFinite(rotation, rotationIsQuaternion ? 4 : 3))
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone '{}': position or rotation is not finite", name);

            SkeletonDesc &skeleton = model->skeleton;
            if (skeleton.bones.size() >= MAXSTUDIOSRCBONES)
                return Fail(model, SMDL_ERROR_LIMIT, "bone '{}': a model has at most {} bones", name, MAXSTUDIOSRCBONES);
            if (parent != SMDL_NO_BONE && !skeleton.valid(parent))
                return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "bone '{}': parent {} is out of range ({} bones)", name,
                            parent, skeleton.bones.size());
            for (const BoneDesc &bone: skeleton.bones) {
                if (IEquals(bone.name, name))
                    return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "bone '{}' already exists (as '{}')", name, bone.name);
            }

            BoneDesc bone;
            bone.name = name;
            bone.parent = parent;
            bone.position = ToVector(position);
            if (rotationIsQuaternion) {
                if (!QuaternionToRadianEuler(rotation, bone.position, bone.rotation))
                    return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone '{}': the rotation quaternion has zero length",
                                name);
            }
            else {
                bone.rotation = RadianEuler(rotation[0], rotation[1], rotation[2]);
            }
            skeleton.bones.push_back(std::move(bone));
            if (out_id)
                *out_id = static_cast<smdl_bone_id>(skeleton.bones.size() - 1);
            return Ok(model);
        });
        return result;
    }
}

namespace smdl {
namespace detail {

BoneId ModelSkeletonApi::addBone(std::string_view nameView, BoneId parent, const Vec3 &position,
                                 const Quat &rotation) {
    const std::string nameStorage(nameView);
    return AddBone(model(), nameStorage.c_str(), parent, position.data(), rotation.data(), true);
}

BoneId ModelSkeletonApi::addBoneEuler(std::string_view nameView, BoneId parent, const Vec3 &position,
                                      const Vec3 &euler) {
    const std::string nameStorage(nameView);
    return AddBone(model(), nameStorage.c_str(), parent, position.data(), euler.data(), false);
}

BoneId ModelSkeletonApi::findBone(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_bone_id result = SMDL_NO_BONE;
    smdl_bone_id *out_id = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bone name is null or empty");
        for (size_t i = 0; i < model->skeleton.bones.size(); ++i) {
            if (IEquals(model->skeleton.bones[i].name, name)) {
                if (out_id)
                    *out_id = static_cast<smdl_bone_id>(i);
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "no bone named '{}'", name);
    });
    return result;
}

uint32_t ModelSkeletonApi::boneCount() const { return static_cast<uint32_t>(model()->skeleton.bones.size()); }

} // namespace detail
} // namespace smdl

extern "C" {

SMDL_API smdl_status smdl_model_add_bone(smdl_model *model, const char *name, smdl_bone_id parent,
                                         const float position[3], const float rotation[4], smdl_bone_id *out_id) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (out_id)
        *out_id = SMDL_NO_BONE;
    return CallCpp(model, [&] {
        // a null vector is reported by AddBone after the name checks, as before
        const smdl_bone_id id = position && rotation
                                    ? smdl::detail::BorrowModel(model).addBone(
                                          name ? name : "", parent, {position[0], position[1], position[2]},
                                          {rotation[0], rotation[1], rotation[2], rotation[3]})
                                    : AddBone(model, name, parent, position, rotation, true);
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_add_bone_euler(smdl_model *model, const char *name, smdl_bone_id parent,
                                               const float position[3], const float euler[3], smdl_bone_id *out_id) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (out_id)
        *out_id = SMDL_NO_BONE;
    return CallCpp(model, [&] {
        const smdl_bone_id id = position && euler
                                    ? smdl::detail::BorrowModel(model).addBoneEuler(
                                          name ? name : "", parent, {position[0], position[1], position[2]},
                                          {euler[0], euler[1], euler[2]})
                                    : AddBone(model, name, parent, position, euler, false);
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_find_bone(const smdl_model *model, const char *name, smdl_bone_id *out_id) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (out_id)
        *out_id = SMDL_NO_BONE;
    return CallCpp(model, [&] {
        const smdl_bone_id id = smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findBone(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_bone_count(const smdl_model *model) {
    return model ? smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).boneCount() : 0;
}

} // extern "C"

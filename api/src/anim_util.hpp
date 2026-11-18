// Call-time helpers of the Anim part (smdl_animation/sequence/rig.cpp)
#pragma once

#include <string>
#include <string_view>

#include "core_util.hpp"

namespace smdl_api {

// SMDL_MOTION_X .. SMDL_MOTION_QUADRATIC
inline constexpr uint32_t kMotionMask = 0x00003FFFu;

// "" when `name` is a valid object name (non-empty, shorter than maxLengthExclusive bytes), else the message
inline std::string BadObjectName(const char *name, std::string_view kind, size_t maxLengthExclusive) {
    if (!ValidName(name))
        return std::string(kind) + " name is null or empty";
    if (std::string_view(name).size() >= maxLengthExclusive)
        return std::format("{} name '{}' is too long (max {} bytes)", kind, name, maxLengthExclusive - 1);
    return {};
}

// A bone id of the model's skeleton (NO_BONE is not accepted)
inline smdl_status CheckBone(const smdl_model *model, smdl_bone_id bone, std::string_view owner, std::string_view name) {
    if (!model->skeleton.valid(bone))
        return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "{} '{}': bone {} is out of range ({} bones)", owner, name, bone,
                    model->skeleton.bones.size());
    return SMDL_OK;
}

inline const std::string &BoneName(const smdl_model &model, smdl_bone_id bone) { return model.skeleton.bones[bone].name; }

} // namespace smdl_api

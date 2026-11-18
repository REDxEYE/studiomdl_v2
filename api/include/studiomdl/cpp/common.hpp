// studiomdl C++ API: shared pieces (C++20). The API is implemented in the library (api/src); the C API wraps it.
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../c/smdl_common.h"
#include "../c/smdl_model.h"

namespace smdl {

using BoneId = smdl_bone_id;
using MaterialId = smdl_material_id;
using PoseParamId = smdl_pose_param_id;
using FlexControllerId = smdl_flex_controller_id;
using FlexId = smdl_flex_id;
using IkChainId = smdl_ik_chain_id;
using WeightListId = smdl_weight_list_id;
using AttachmentId = smdl_attachment_id;
using HitboxSetId = smdl_hitbox_set_id;

inline constexpr BoneId NoBone = SMDL_NO_BONE;
inline constexpr uint32_t NoId = SMDL_NO_ID;

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Quat = std::array<float, 4>; // x, y, z, w

enum class Severity : int { Message = SMDL_SEVERITY_MESSAGE, Warning = SMDL_SEVERITY_WARNING, Error = SMDL_SEVERITY_ERROR };

class Error : public std::runtime_error {
public:
    Error(smdl_status status, const std::string &message, std::string object = {})
        : std::runtime_error(message), m_Status(status), m_Object(std::move(object)) {}

    smdl_status status() const { return m_Status; }
    // The object being compiled when a compile failed ("bodygroup 'lid' / mesh 'a'"), else empty
    const std::string &object() const { return m_Object; }

private:
    smdl_status m_Status;
    std::string m_Object;
};

namespace detail {
    // Element stride of a span for the pointer + count + stride array convention
    template<class T>
    constexpr size_t stride_of() { return sizeof(T); }
}

} // namespace smdl

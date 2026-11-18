// smdl_bone_dynamics.h: jiggle bones and procedural bones (store only; the compile applies them in
// ApplyBoneDynamics, apply_bone_dynamics.cpp)
#include "studiomdl/c/smdl_bone_dynamics.h"
#include "studiomdl/studiomdl.hpp"

#include <cmath>

#include "core_util.hpp"

using namespace smdl_api;

namespace {
    smdl_status CheckBone(const smdl_model &model, smdl_bone_id bone, const char *what) {
        if (!model.skeleton.valid(bone))
            return Fail(&model, SMDL_ERROR_OUT_OF_RANGE, "{}: bone id {} is not a bone of the model ({} bones)", what,
                        bone, model.skeleton.bones.size());
        return SMDL_OK;
    }

    smdl_status CheckFinite(const smdl_model &model, std::initializer_list<float> values, const char *what) {
        for (const float value: values) {
            if (!std::isfinite(value))
                return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: value is not a finite number", what);
        }
        return SMDL_OK;
    }

    smdl_status CheckVector(const smdl_model &model, const float *v, const char *what) {
        if (!v || !AllFinite(v, 3))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: vector is null or not finite", what);
        return SMDL_OK;
    }

    // jiggle setter on a section parameter: `member` is a float member of smdl_jiggle
    void SetJiggle(smdl_jiggle *jiggle, float smdl_jiggle::*member, float value, const char *what,
                   bool smdl_jiggle::*section = nullptr) {
        smdl_model *model = jiggle->model;
        RunBody(model, [&] {
            if (const auto status = CheckFinite(*model, {value}, what))
                return status;
            if (section && !(jiggle->*section))
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "jiggle bone: {} needs its section enabled first", what);
            jiggle->*member = value;
            return Ok(model);
        });
    }

    void SetJigglePair(smdl_jiggle *jiggle, std::pair<float, float> smdl_jiggle::*member, float a, float b,
                       const char *what, bool smdl_jiggle::*section = nullptr) {
        smdl_model *model = jiggle->model;
        RunBody(model, [&] {
            if (const auto status = CheckFinite(*model, {a, b}, what))
                return status;
            if (section && !(jiggle->*section))
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "jiggle bone: {} needs its section enabled first", what);
            jiggle->*member = {a, b};
            return Ok(model);
        });
    }

    template<class T>
    smdl_status Create(smdl_model *model, std::vector<std::unique_ptr<T>> &list, T *&out, const char *what,
                       auto &&fill) {
        auto object = std::make_unique<T>();
        object->model = model;
        if (const auto status = fill(*object))
            return status;
        out = list.emplace_back(std::move(object)).get();
        return Ok(model);
    }

    smdl_aim_constraint *AddAim(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent, bool attachment, uint32_t aim) {
        smdl_aim_constraint *result = nullptr;
        smdl_aim_constraint **out_aim = &result;
        RunBody(model, [&] {
            if (const auto status = CheckBone(*model, bone, "aim constraint bone"))
                return status;
            if (const auto status = CheckBone(*model, parent, "aim constraint parent"))
                return status;
            if (attachment) {
                if (aim >= model->rig.attachments.size())
                    return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "aim constraint: attachment id {} does not exist ({} attachments)",
                                aim, model->rig.attachments.size());
            } else if (const auto status = CheckBone(*model, aim, "aim constraint target")) {
                return status;
            }
            smdl_aim_constraint *constraint = nullptr;
            const auto status = Create(model, model->boneDynamics.aimConstraints, constraint, "aim constraint",
                                       [&](smdl_aim_constraint &a) {
                                           a.bone = bone;
                                           a.parent = parent;
                                           a.aimIsAttachment = attachment;
                                           a.aim = aim;
                                           return SMDL_OK;
                                       });
            if (status == SMDL_OK && out_aim)
                *out_aim = constraint;
            return status;
        });
        return result;
    }
}


namespace smdl {

// -- JiggleBone ---------------------------------------------------------------------------------------------------

const JiggleBone &JiggleBone::setFlexible(bool enabled) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    RunBody(model, [&] {
        if (enabled && jiggle->rigid)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "jiggle bone: is_flexible and is_rigid cannot be combined");
        jiggle->flexible = enabled != 0;
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setRigid(bool enabled) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    RunBody(model, [&] {
        if (enabled && jiggle->flexible)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "jiggle bone: is_flexible and is_rigid cannot be combined");
        jiggle->rigid = enabled != 0;
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setBaseSpring(bool enabled) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    RunBody(model, [&] {
        jiggle->baseSpring = enabled != 0;
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setAllowLengthFlex(bool enabled) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    RunBody(model, [&] {
        if (enabled && !jiggle->flexible)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "jiggle bone: allow_length_flex needs is_flexible first");
        jiggle->allowLengthFlex = enabled != 0;
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setLength(float length) const {
    SetJiggle(handle(), &smdl_jiggle::length, length, "length");
    return *this;
}

const JiggleBone &JiggleBone::setTipMass(float mass) const {
    SetJiggle(handle(), &smdl_jiggle::tipMass, mass, "tip mass");
    return *this;
}

const JiggleBone &JiggleBone::setAngleConstraint(float degrees) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    RunBody(model, [&] {
        if (const auto status = CheckFinite(*model, {degrees}, "angle constraint"))
            return status;
        jiggle->angleConstraint = degrees;
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setYawConstraint(float minDegrees, float maxDegrees) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    const float min_degrees = minDegrees;
    const float max_degrees = maxDegrees;
    RunBody(model, [&] {
        if (const auto status = CheckFinite(*model, {min_degrees, max_degrees}, "yaw constraint"))
            return status;
        jiggle->yawConstraint = std::make_pair(min_degrees, max_degrees);
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setPitchConstraint(float minDegrees, float maxDegrees) const {
    smdl_jiggle *jiggle = handle();
    smdl_model *model = jiggle->model;
    const float min_degrees = minDegrees;
    const float max_degrees = maxDegrees;
    RunBody(model, [&] {
        if (const auto status = CheckFinite(*model, {min_degrees, max_degrees}, "pitch constraint"))
            return status;
        jiggle->pitchConstraint = std::make_pair(min_degrees, max_degrees);
        return Ok(model);
    });
    return *this;
}

const JiggleBone &JiggleBone::setYawFriction(float v) const {
    SetJiggle(handle(), &smdl_jiggle::yawFriction, v, "yaw friction");
    return *this;
}

const JiggleBone &JiggleBone::setYawBounce(float v) const {
    SetJiggle(handle(), &smdl_jiggle::yawBounce, v, "yaw bounce");
    return *this;
}

const JiggleBone &JiggleBone::setPitchFriction(float v) const {
    SetJiggle(handle(), &smdl_jiggle::pitchFriction, v, "pitch friction");
    return *this;
}

const JiggleBone &JiggleBone::setPitchBounce(float v) const {
    SetJiggle(handle(), &smdl_jiggle::pitchBounce, v, "pitch bounce");
    return *this;
}

const JiggleBone &JiggleBone::setYawStiffness(float v) const {
    SetJiggle(handle(), &smdl_jiggle::yawStiffness, v, "yaw stiffness", &smdl_jiggle::flexible);
    return *this;
}

const JiggleBone &JiggleBone::setYawDamping(float v) const {
    SetJiggle(handle(), &smdl_jiggle::yawDamping, v, "yaw damping", &smdl_jiggle::flexible);
    return *this;
}

const JiggleBone &JiggleBone::setPitchStiffness(float v) const {
    SetJiggle(handle(), &smdl_jiggle::pitchStiffness, v, "pitch stiffness", &smdl_jiggle::flexible);
    return *this;
}

const JiggleBone &JiggleBone::setPitchDamping(float v) const {
    SetJiggle(handle(), &smdl_jiggle::pitchDamping, v, "pitch damping", &smdl_jiggle::flexible);
    return *this;
}

const JiggleBone &JiggleBone::setAlongStiffness(float v) const {
    SetJiggle(handle(), &smdl_jiggle::alongStiffness, v, "along stiffness", &smdl_jiggle::flexible);
    return *this;
}

const JiggleBone &JiggleBone::setAlongDamping(float v) const {
    SetJiggle(handle(), &smdl_jiggle::alongDamping, v, "along damping", &smdl_jiggle::flexible);
    return *this;
}

const JiggleBone &JiggleBone::setBaseStiffness(float v) const {
    SetJiggle(handle(), &smdl_jiggle::baseStiffness, v, "base stiffness", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseDamping(float v) const {
    SetJiggle(handle(), &smdl_jiggle::baseDamping, v, "base damping", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseLeftConstraint(float min, float max) const {
    SetJigglePair(handle(), &smdl_jiggle::baseLeft, min, max, "base left constraint", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseLeftFriction(float v) const {
    SetJiggle(handle(), &smdl_jiggle::baseLeftFriction, v, "base left friction", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseUpConstraint(float min, float max) const {
    SetJigglePair(handle(), &smdl_jiggle::baseUp, min, max, "base up constraint", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseUpFriction(float v) const {
    SetJiggle(handle(), &smdl_jiggle::baseUpFriction, v, "base up friction", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseForwardConstraint(float min, float max) const {
    SetJigglePair(handle(), &smdl_jiggle::baseForward, min, max, "base forward constraint", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseForwardFriction(float v) const {
    SetJiggle(handle(), &smdl_jiggle::baseForwardFriction, v, "base forward friction", &smdl_jiggle::baseSpring);
    return *this;
}

const JiggleBone &JiggleBone::setBaseMass(float v) const {
    SetJiggle(handle(), &smdl_jiggle::baseMass, v, "base mass", &smdl_jiggle::baseSpring);
    return *this;
}

// -- QuatInterp ---------------------------------------------------------------------------------------------------

const QuatInterp &QuatInterp::setDisplay(const Vec3 &sizeVec, float distance) const {
    smdl_quat_interp *interp = handle();
    smdl_model *model = interp->model;
    const float *size = sizeVec.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, size, "helper display size"))
            return status;
        if (const auto status = CheckFinite(*model, {distance}, "helper display distance"))
            return status;
        interp->hasDisplay = true;
        interp->displaySize = ToVector(size);
        interp->displayDistance = distance;
        return Ok(model);
    });
    return *this;
}

const QuatInterp &QuatInterp::setBasePosition(const Vec3 &vector) const {
    smdl_quat_interp *interp = handle();
    smdl_model *model = interp->model;
    const float *v = vector.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, v, "helper base position"))
            return status;
        interp->basePosition = ToVector(v);
        return Ok(model);
    });
    return *this;
}

const QuatInterp &QuatInterp::setRotateAxis(const Vec3 &vector) const {
    smdl_quat_interp *interp = handle();
    smdl_model *model = interp->model;
    const float *v = vector.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, v, "helper rotate axis"))
            return status;
        interp->rotateAxis = ToVector(v);
        return Ok(model);
    });
    return *this;
}

const QuatInterp &QuatInterp::setJointOrient(const Vec3 &vector) const {
    smdl_quat_interp *interp = handle();
    smdl_model *model = interp->model;
    const float *v = vector.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, v, "helper joint orient"))
            return status;
        interp->jointOrient = ToVector(v);
        return Ok(model);
    });
    return *this;
}

const QuatInterp &QuatInterp::addTrigger(float toleranceDegrees, const Vec3 &triggerVec, const Vec3 &anglesVec,
                                         const Vec3 &positionVec) const {
    smdl_quat_interp *interp = handle();
    smdl_model *model = interp->model;
    const float tolerance_degrees = toleranceDegrees;
    const float *trigger_degrees = triggerVec.data();
    const float *angles_degrees = anglesVec.data();
    const float *position = positionVec.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, trigger_degrees, "trigger angles"))
            return status;
        if (const auto status = CheckVector(*model, angles_degrees, "trigger orientation"))
            return status;
        if (const auto status = CheckVector(*model, position, "trigger position"))
            return status;
        if (!(tolerance_degrees > 0.0f) || !std::isfinite(tolerance_degrees))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "trigger tolerance {} must be positive", tolerance_degrees);
        if (interp->triggers.size() >= kMaxQuatInterpTriggers)
            return Fail(model, SMDL_ERROR_LIMIT, "a helper has at most {} triggers", kMaxQuatInterpTriggers);
        smdl_quat_interp::Trigger &trigger = interp->triggers.emplace_back();
        trigger.toleranceDegrees = tolerance_degrees;
        trigger.triggerDegrees = ToVector(trigger_degrees);
        trigger.anglesDegrees = ToVector(angles_degrees);
        trigger.position = ToVector(position);
        return Ok(model);
    });
    return *this;
}

// -- AimConstraint ------------------------------------------------------------------------------------------------

const AimConstraint &AimConstraint::setAimVector(const Vec3 &vector) const {
    smdl_aim_constraint *aim = handle();
    smdl_model *model = aim->model;
    const float *v = vector.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, v, "aim vector"))
            return status;
        aim->aimVector = ToVector(v);
        return Ok(model);
    });
    return *this;
}

const AimConstraint &AimConstraint::setUpVector(const Vec3 &vector) const {
    smdl_aim_constraint *aim = handle();
    smdl_model *model = aim->model;
    const float *v = vector.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, v, "up vector"))
            return status;
        aim->upVector = ToVector(v);
        return Ok(model);
    });
    return *this;
}

const AimConstraint &AimConstraint::setBasePosition(const Vec3 &vector) const {
    smdl_aim_constraint *aim = handle();
    smdl_model *model = aim->model;
    const float *v = vector.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, v, "aim base position"))
            return status;
        aim->basePosition = ToVector(v);
        return Ok(model);
    });
    return *this;
}

// -- AxisInterp ---------------------------------------------------------------------------------------------------

const AxisInterp &AxisInterp::setBasePosition(const Vec3 &positionVec) const {
    smdl_axis_interp *interp = handle();
    smdl_model *model = interp->model;
    const float *position = positionVec.data();
    RunBody(model, [&] {
        if (const auto status = CheckVector(*model, position, "axis interpolation base position"))
            return status;
        interp->basePosition = ToVector(position);
        return Ok(model);
    });
    return *this;
}

const AxisInterp &AxisInterp::setOrientation(uint32_t index, const Vec3 &positionVec, const Vec3 &anglesVec) const {
    smdl_axis_interp *interp = handle();
    smdl_model *model = interp->model;
    const float *position = positionVec.data();
    const float *angles = anglesVec.data();
    RunBody(model, [&] {
        if (index >= interp->orientations.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "axis interpolation: orientation index {} is not 0..5", index);
        if (const auto status = CheckVector(*model, position, "axis orientation position"))
            return status;
        if (const auto status = CheckVector(*model, angles, "axis orientation angles"))
            return status;
        auto &orientation = interp->orientations[index];
        orientation.set = true;
        orientation.position = ToVector(position);
        orientation.angles = QAngle(angles[0], angles[1], angles[2]);
        return Ok(model);
    });
    return *this;
}

// -- Model --------------------------------------------------------------------------------------------------------

namespace detail {

JiggleBone ModelBoneDynamicsApi::addJiggleBone(BoneId boneId) {
    smdl_model *model = this->model();
    const smdl_bone_id bone = boneId;
    smdl_jiggle *result = nullptr;
    smdl_jiggle **out_jiggle = &result;
    RunBody(model, [&] {
        if (const auto status = CheckBone(*model, bone, "jiggle bone"))
            return status;
        for (const auto &other: model->boneDynamics.jiggles) {
            if (other->bone == bone)
                return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "bone '{}' has a jiggle bone already",
                            model->skeleton.bones[bone].name);
        }
        smdl_jiggle *jiggle = nullptr;
        const auto status = Create(model, model->boneDynamics.jiggles, jiggle, "jiggle bone", [&](smdl_jiggle &j) {
            j.bone = bone;
            return SMDL_OK;
        });
        if (status == SMDL_OK && out_jiggle)
            *out_jiggle = jiggle;
        return status;
    });
    return JiggleBone(result, model);
}

uint32_t ModelBoneDynamicsApi::jiggleBoneCount() const {
    return static_cast<uint32_t>(model()->boneDynamics.jiggles.size());
}

QuatInterp ModelBoneDynamicsApi::addQuatInterp(BoneId boneId, BoneId parentId, BoneId controlParentId,
                                               BoneId controlId) {
    smdl_model *model = this->model();
    const smdl_bone_id bone = boneId;
    const smdl_bone_id parent = parentId;
    const smdl_bone_id control_parent = controlParentId;
    const smdl_bone_id control = controlId;
    smdl_quat_interp *result = nullptr;
    smdl_quat_interp **out_interp = &result;
    RunBody(model, [&] {
        for (const auto &[id, what]: {std::pair{bone, "helper bone"}, std::pair{parent, "helper parent"},
                                      std::pair{control_parent, "helper control parent"},
                                      std::pair{control, "helper control"}}) {
            if (const auto status = CheckBone(*model, id, what))
                return status;
        }
        smdl_quat_interp *interp = nullptr;
        const auto status = Create(model, model->boneDynamics.quatInterps, interp, "helper", [&](smdl_quat_interp &q) {
            q.bone = bone;
            q.parent = parent;
            q.controlParent = control_parent;
            q.control = control;
            return SMDL_OK;
        });
        if (status == SMDL_OK && out_interp)
            *out_interp = interp;
        return status;
    });
    return {result, model};
}

AimConstraint ModelBoneDynamicsApi::addAimConstraintToBone(BoneId bone, BoneId parent, BoneId aimBone) {
    smdl_model *model = this->model();
    return {AddAim(model, bone, parent, false, aimBone), model};
}

AimConstraint ModelBoneDynamicsApi::addAimConstraintToAttachment(BoneId bone, BoneId parent,
                                                                 AttachmentId aimAttachment) {
    smdl_model *model = this->model();
    return {AddAim(model, bone, parent, true, aimAttachment), model};
}

AxisInterp ModelBoneDynamicsApi::addAxisInterp(BoneId boneId, BoneId controlId, int axis) {
    smdl_model *model = this->model();
    const smdl_bone_id bone = boneId;
    const smdl_bone_id control = controlId;
    smdl_axis_interp *result = nullptr;
    smdl_axis_interp **out_interp = &result;
    RunBody(model, [&] {
        if (const auto status = CheckBone(*model, bone, "axis interpolation bone"))
            return status;
        if (const auto status = CheckBone(*model, control, "axis interpolation control"))
            return status;
        if (axis < 1 || axis > 3)
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "axis interpolation: axis {} is not 1, 2 or 3", axis);
        smdl_axis_interp *interp = nullptr;
        const auto status = Create(model, model->boneDynamics.axisInterps, interp, "axis interpolation",
                                   [&](smdl_axis_interp &a) {
                                       a.bone = bone;
                                       a.control = control;
                                       a.axis = axis;
                                       return SMDL_OK;
                                   });
        if (status == SMDL_OK && out_interp)
            *out_interp = interp;
        return status;
    });
    return {result, model};
}

} // namespace detail
} // namespace smdl

namespace {
    smdl::JiggleBone View(smdl_jiggle *j) { return {j, j->model}; }
    smdl::QuatInterp View(smdl_quat_interp *q) { return {q, q->model}; }
    smdl::AimConstraint View(smdl_aim_constraint *a) { return {a, a->model}; }
    smdl::AxisInterp View(smdl_axis_interp *a) { return {a, a->model}; }

    smdl::Vec3 ToVec3(const float *v) { return {v[0], v[1], v[2]}; }
}

// The C API: null-pointer checks (they need C), then the C++ method
extern "C" {

// -- jiggle -----------------------------------------------------------------------------------------------------

SMDL_API smdl_status smdl_model_add_jiggle_bone(smdl_model *model, smdl_bone_id bone, smdl_jiggle **out_jiggle) {
    if (out_jiggle)
        *out_jiggle = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::JiggleBone jiggle = smdl::detail::BorrowModel(model).addJiggleBone(bone);
        if (out_jiggle)
            *out_jiggle = jiggle.handle();
    });
}

SMDL_API uint32_t smdl_model_jiggle_bone_count(const smdl_model *model) {
    return model ? smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).jiggleBoneCount() : 0;
}

SMDL_API smdl_status smdl_jiggle_set_flexible(smdl_jiggle *j, int enabled) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setFlexible(enabled != 0); });
}

SMDL_API smdl_status smdl_jiggle_set_rigid(smdl_jiggle *j, int enabled) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setRigid(enabled != 0); });
}

SMDL_API smdl_status smdl_jiggle_set_base_spring(smdl_jiggle *j, int enabled) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseSpring(enabled != 0); });
}

SMDL_API smdl_status smdl_jiggle_set_allow_length_flex(smdl_jiggle *j, int enabled) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setAllowLengthFlex(enabled != 0); });
}

SMDL_API smdl_status smdl_jiggle_set_length(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setLength(v); });
}

SMDL_API smdl_status smdl_jiggle_set_tip_mass(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setTipMass(v); });
}

SMDL_API smdl_status smdl_jiggle_set_angle_constraint(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setAngleConstraint(v); });
}

SMDL_API smdl_status smdl_jiggle_set_yaw_friction(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setYawFriction(v); });
}

SMDL_API smdl_status smdl_jiggle_set_yaw_bounce(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setYawBounce(v); });
}

SMDL_API smdl_status smdl_jiggle_set_pitch_friction(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setPitchFriction(v); });
}

SMDL_API smdl_status smdl_jiggle_set_pitch_bounce(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setPitchBounce(v); });
}

SMDL_API smdl_status smdl_jiggle_set_yaw_stiffness(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setYawStiffness(v); });
}

SMDL_API smdl_status smdl_jiggle_set_yaw_damping(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setYawDamping(v); });
}

SMDL_API smdl_status smdl_jiggle_set_pitch_stiffness(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setPitchStiffness(v); });
}

SMDL_API smdl_status smdl_jiggle_set_pitch_damping(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setPitchDamping(v); });
}

SMDL_API smdl_status smdl_jiggle_set_along_stiffness(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setAlongStiffness(v); });
}

SMDL_API smdl_status smdl_jiggle_set_along_damping(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setAlongDamping(v); });
}

SMDL_API smdl_status smdl_jiggle_set_base_stiffness(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseStiffness(v); });
}

SMDL_API smdl_status smdl_jiggle_set_base_damping(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseDamping(v); });
}

SMDL_API smdl_status smdl_jiggle_set_base_left_friction(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseLeftFriction(v); });
}

SMDL_API smdl_status smdl_jiggle_set_base_up_friction(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseUpFriction(v); });
}

SMDL_API smdl_status smdl_jiggle_set_base_forward_friction(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseForwardFriction(v); });
}

SMDL_API smdl_status smdl_jiggle_set_base_mass(smdl_jiggle *j, float v) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseMass(v); });
}

SMDL_API smdl_status smdl_jiggle_set_yaw_constraint(smdl_jiggle *j, float a, float b) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setYawConstraint(a, b); });
}

SMDL_API smdl_status smdl_jiggle_set_pitch_constraint(smdl_jiggle *j, float a, float b) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setPitchConstraint(a, b); });
}

SMDL_API smdl_status smdl_jiggle_set_base_left_constraint(smdl_jiggle *j, float a, float b) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseLeftConstraint(a, b); });
}

SMDL_API smdl_status smdl_jiggle_set_base_up_constraint(smdl_jiggle *j, float a, float b) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseUpConstraint(a, b); });
}

SMDL_API smdl_status smdl_jiggle_set_base_forward_constraint(smdl_jiggle *j, float a, float b) {
    if (!j)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(j->model, [&] { View(j).setBaseForwardConstraint(a, b); });
}

// -- quaternion interpolation -----------------------------------------------------------------------------------

SMDL_API smdl_status smdl_model_add_quat_interp(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                smdl_bone_id control_parent, smdl_bone_id control,
                                                smdl_quat_interp **out_interp) {
    if (out_interp)
        *out_interp = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::QuatInterp interp =
            smdl::detail::BorrowModel(model).addQuatInterp(bone, parent, control_parent, control);
        if (out_interp)
            *out_interp = interp.handle();
    });
}

SMDL_API uint32_t smdl_model_quat_interp_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->boneDynamics.quatInterps.size()) : 0;
}

SMDL_API smdl_status smdl_quat_interp_set_display(smdl_quat_interp *interp, const float size[3], float distance) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*interp->model, size, "helper display size"))
        return status;
    return CallCpp(interp->model, [&] { View(interp).setDisplay(ToVec3(size), distance); });
}

SMDL_API smdl_status smdl_quat_interp_set_base_position(smdl_quat_interp *interp, const float v[3]) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*interp->model, v, "helper base position"))
        return status;
    return CallCpp(interp->model, [&] { View(interp).setBasePosition(ToVec3(v)); });
}

SMDL_API smdl_status smdl_quat_interp_set_rotate_axis(smdl_quat_interp *interp, const float v[3]) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*interp->model, v, "helper rotate axis"))
        return status;
    return CallCpp(interp->model, [&] { View(interp).setRotateAxis(ToVec3(v)); });
}

SMDL_API smdl_status smdl_quat_interp_set_joint_orient(smdl_quat_interp *interp, const float v[3]) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*interp->model, v, "helper joint orient"))
        return status;
    return CallCpp(interp->model, [&] { View(interp).setJointOrient(ToVec3(v)); });
}

SMDL_API smdl_status smdl_quat_interp_add_trigger(smdl_quat_interp *interp, float tolerance_degrees,
                                                  const float trigger_degrees[3], const float angles_degrees[3],
                                                  const float position[3]) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    smdl_model *model = interp->model;
    if (const auto status = CheckVector(*model, trigger_degrees, "trigger angles"))
        return status;
    if (const auto status = CheckVector(*model, angles_degrees, "trigger orientation"))
        return status;
    if (const auto status = CheckVector(*model, position, "trigger position"))
        return status;
    return CallCpp(model, [&] {
        View(interp).addTrigger(tolerance_degrees, ToVec3(trigger_degrees), ToVec3(angles_degrees), ToVec3(position));
    });
}

// -- aim constraints ----------------------------------------------------------------------------------------------

SMDL_API smdl_status smdl_model_add_aim_constraint_bone(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                        smdl_bone_id aim_bone, smdl_aim_constraint **out_aim) {
    if (out_aim)
        *out_aim = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::AimConstraint aim =
            smdl::detail::BorrowModel(model).addAimConstraintToBone(bone, parent, aim_bone);
        if (out_aim)
            *out_aim = aim.handle();
    });
}

SMDL_API smdl_status smdl_model_add_aim_constraint_attachment(smdl_model *model, smdl_bone_id bone, smdl_bone_id parent,
                                                              smdl_attachment_id aim_attachment,
                                                              smdl_aim_constraint **out_aim) {
    if (out_aim)
        *out_aim = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::AimConstraint aim =
            smdl::detail::BorrowModel(model).addAimConstraintToAttachment(bone, parent, aim_attachment);
        if (out_aim)
            *out_aim = aim.handle();
    });
}

SMDL_API uint32_t smdl_model_aim_constraint_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->boneDynamics.aimConstraints.size()) : 0;
}

SMDL_API smdl_status smdl_aim_constraint_set_aim_vector(smdl_aim_constraint *aim, const float v[3]) {
    if (!aim)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*aim->model, v, "aim vector"))
        return status;
    return CallCpp(aim->model, [&] { View(aim).setAimVector(ToVec3(v)); });
}

SMDL_API smdl_status smdl_aim_constraint_set_up_vector(smdl_aim_constraint *aim, const float v[3]) {
    if (!aim)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*aim->model, v, "up vector"))
        return status;
    return CallCpp(aim->model, [&] { View(aim).setUpVector(ToVec3(v)); });
}

SMDL_API smdl_status smdl_aim_constraint_set_base_position(smdl_aim_constraint *aim, const float v[3]) {
    if (!aim)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*aim->model, v, "aim base position"))
        return status;
    return CallCpp(aim->model, [&] { View(aim).setBasePosition(ToVec3(v)); });
}

// -- axis interpolation -------------------------------------------------------------------------------------------

SMDL_API smdl_status smdl_model_add_axis_interp(smdl_model *model, smdl_bone_id bone, smdl_bone_id control, int axis,
                                                smdl_axis_interp **out_interp) {
    if (out_interp)
        *out_interp = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::AxisInterp interp = smdl::detail::BorrowModel(model).addAxisInterp(bone, control, axis);
        if (out_interp)
            *out_interp = interp.handle();
    });
}

SMDL_API uint32_t smdl_model_axis_interp_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->boneDynamics.axisInterps.size()) : 0;
}

SMDL_API smdl_status smdl_axis_interp_set_base_position(smdl_axis_interp *interp, const float position[3]) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (const auto status = CheckVector(*interp->model, position, "axis interpolation base position"))
        return status;
    return CallCpp(interp->model, [&] { View(interp).setBasePosition(ToVec3(position)); });
}

SMDL_API smdl_status smdl_axis_interp_set_orientation(smdl_axis_interp *interp, uint32_t index, const float position[3],
                                                      const float angles[3]) {
    if (!interp)
        return SMDL_ERROR_INVALID_ARGUMENT;
    smdl_model *model = interp->model;
    if (index < interp->orientations.size()) {
        if (const auto status = CheckVector(*model, position, "axis orientation position"))
            return status;
        if (const auto status = CheckVector(*model, angles, "axis orientation angles"))
            return status;
    } else {
        return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "axis interpolation: orientation index {} is not 0..5", index);
    }
    return CallCpp(model, [&] { View(interp).setOrientation(index, ToVec3(position), ToVec3(angles)); });
}

} // extern "C"

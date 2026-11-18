// studiomdl C++ wrapper: smdl_bone_dynamics.h
#pragma once

#include <utility>

#include "common.hpp"
#include "../c/smdl_bone_dynamics.h"

namespace smdl {

// A jiggle bone ($jigglebone). Non-owning view, valid as long as its model lives. See smdl_bone_dynamics.h.
class JiggleBone {
public:
    JiggleBone(smdl_jiggle *jiggle, smdl_model *model) : m_Jiggle(jiggle), m_Model(model) {}

    smdl_jiggle *handle() const { return m_Jiggle; }
    smdl_model *model() const { return m_Model; }

    // sections
    const JiggleBone &setFlexible(bool enabled = true) const;
    const JiggleBone &setRigid(bool enabled = true) const;
    const JiggleBone &setBaseSpring(bool enabled = true) const;
    const JiggleBone &setAllowLengthFlex(bool enabled = true) const;

    // every section
    const JiggleBone &setLength(float length) const;
    const JiggleBone &setTipMass(float mass) const;
    const JiggleBone &setAngleConstraint(float degrees) const;
    const JiggleBone &setYawConstraint(float minDegrees, float maxDegrees) const;
    const JiggleBone &setYawFriction(float friction) const;
    const JiggleBone &setYawBounce(float bounce) const;
    const JiggleBone &setPitchConstraint(float minDegrees, float maxDegrees) const;
    const JiggleBone &setPitchFriction(float friction) const;
    const JiggleBone &setPitchBounce(float bounce) const;

    // flexible (clamped to 0..1000 by the compile)
    const JiggleBone &setYawStiffness(float v) const;
    const JiggleBone &setYawDamping(float v) const;
    const JiggleBone &setPitchStiffness(float v) const;
    const JiggleBone &setPitchDamping(float v) const;
    const JiggleBone &setAlongStiffness(float v) const;
    const JiggleBone &setAlongDamping(float v) const;

    // base spring
    const JiggleBone &setBaseStiffness(float v) const;
    const JiggleBone &setBaseDamping(float v) const;
    const JiggleBone &setBaseLeftConstraint(float min, float max) const;
    const JiggleBone &setBaseLeftFriction(float v) const;
    const JiggleBone &setBaseUpConstraint(float min, float max) const;
    const JiggleBone &setBaseUpFriction(float v) const;
    const JiggleBone &setBaseForwardConstraint(float min, float max) const;
    const JiggleBone &setBaseForwardFriction(float v) const;
    const JiggleBone &setBaseMass(float v) const;

private:
    smdl_jiggle *m_Jiggle;
    smdl_model *m_Model;
};

// A quaternion interpolation helper (.vrd `<helper>`)
class QuatInterp {
public:
    QuatInterp(smdl_quat_interp *interp, smdl_model *model) : m_Interp(interp), m_Model(model) {}

    smdl_quat_interp *handle() const { return m_Interp; }
    smdl_model *model() const { return m_Model; }

    const QuatInterp &setDisplay(const Vec3 &size, float distance) const;
    const QuatInterp &setBasePosition(const Vec3 &position) const;
    const QuatInterp &setRotateAxis(const Vec3 &degrees) const;
    const QuatInterp &setJointOrient(const Vec3 &degrees) const;
    const QuatInterp &addTrigger(float toleranceDegrees, const Vec3 &triggerDegrees, const Vec3 &anglesDegrees,
                                 const Vec3 &position) const;

private:
    smdl_quat_interp *m_Interp;
    smdl_model *m_Model;
};

// An aim constraint (.vrd `<aimconstraint>`)
class AimConstraint {
public:
    AimConstraint(smdl_aim_constraint *aim, smdl_model *model) : m_Aim(aim), m_Model(model) {}

    smdl_aim_constraint *handle() const { return m_Aim; }
    smdl_model *model() const { return m_Model; }

    const AimConstraint &setAimVector(const Vec3 &vector) const;
    const AimConstraint &setUpVector(const Vec3 &vector) const;
    const AimConstraint &setBasePosition(const Vec3 &position) const;

private:
    smdl_aim_constraint *m_Aim;
    smdl_model *m_Model;
};

// An axis interpolation bone (procedural bones file `bone` entry)
class AxisInterp {
public:
    AxisInterp(smdl_axis_interp *interp, smdl_model *model) : m_Interp(interp), m_Model(model) {}

    smdl_axis_interp *handle() const { return m_Interp; }
    smdl_model *model() const { return m_Model; }

    const AxisInterp &setBasePosition(const Vec3 &position) const;
    // index 0..5; angles are a QAngle in degrees (pitch, yaw, roll)
    const AxisInterp &setOrientation(uint32_t index, const Vec3 &position, const Vec3 &anglesDegrees) const;

private:
    smdl_axis_interp *m_Interp;
    smdl_model *m_Model;
};

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
    class ModelBoneDynamicsApi {
    public:
        // $jigglebone (one per bone)
        JiggleBone addJiggleBone(BoneId bone);
        uint32_t jiggleBoneCount() const;

        // .vrd `<helper> <bone> <parent> <control parent> <control>`
        QuatInterp addQuatInterp(BoneId bone, BoneId parent, BoneId controlParent, BoneId control);

        // .vrd `<aimconstraint>` with a bone or an attachment as the target
        AimConstraint addAimConstraintToBone(BoneId bone, BoneId parent, BoneId aimBone);
        AimConstraint addAimConstraintToAttachment(BoneId bone, BoneId parent, AttachmentId aimAttachment);

        // procedural bones file `bone` entry; axis 1..3
        AxisInterp addAxisInterp(BoneId bone, BoneId control, int axis);

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

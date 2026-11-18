#include "studiomdl/apply_bone_dynamics.hpp"

#include <cstring>

#include "tier0/dbg.h"
#include "mathlib/mathlib.h"
#include "studio.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_log.hpp"

namespace {
    float ClampStiffness(const float value) {
        constexpr float minStiffness = 0.0f;
        constexpr float maxStiffness = 1000.0f;
        return clamp(value, minStiffness, maxStiffness);
    }
}

s_jigglebone_t &ApplyJiggleBegin(StudioMdlState &ctx, const std::string_view bone) {
    s_jigglebone_t *jiggle = &ctx.bones.jigglebones.emplace_back();
    jiggle->bonename = bone;

    // default values
    memset(&jiggle->data, 0, sizeof(mstudiojigglebone_t));
    jiggle->data.length = 10.0f;
    jiggle->data.yawStiffness = 100.0f;
    jiggle->data.pitchStiffness = 100.0f;
    jiggle->data.alongStiffness = 100.0f;
    jiggle->data.baseStiffness = 100.0f;
    jiggle->data.baseMinUp = -100.0f;
    jiggle->data.baseMaxUp = 100.0f;
    jiggle->data.baseMinLeft = -100.0f;
    jiggle->data.baseMaxLeft = 100.0f;
    jiggle->data.baseMinForward = -100.0f;
    jiggle->data.baseMaxForward = 100.0f;
    return *jiggle;
}

void ApplyJiggleEnd(const StudioMdlState &ctx, const s_jigglebone_t &jiggle) {
    if (!ctx.options.quiet)
        Msg("Marking bone {} as a jiggle bone\n", jiggle.bonename.c_str());
}

void ApplyJiggleFlexible(s_jigglebone_t &jiggle) {
    jiggle.data.flags |= (JiggleBoneFlags::IsFlexible | JiggleBoneFlags::HasLengthConstraint);
}

void ApplyJiggleRigid(s_jigglebone_t &jiggle) {
    jiggle.data.flags |= (JiggleBoneFlags::IsRigid | JiggleBoneFlags::HasLengthConstraint);
}

void ApplyJiggleBaseSpring(s_jigglebone_t &jiggle) {
    jiggle.data.flags |= JiggleBoneFlags::HasBaseSpring;
}

void ApplyJiggleAllowLengthFlex(s_jigglebone_t &jiggle) {
    jiggle.data.flags &= ~JiggleBoneFlags::HasLengthConstraint;
}

void ApplyJiggleTipMass(s_jigglebone_t &jiggle, const float mass) { jiggle.data.tipMass = mass; }

void ApplyJiggleLength(s_jigglebone_t &jiggle, const float length) { jiggle.data.length = length; }

void ApplyJiggleAngleConstraint(s_jigglebone_t &jiggle, const float degrees) {
    jiggle.data.flags |= JiggleBoneFlags::HasAngleConstraint;
    jiggle.data.angleLimit = degrees * M_PI / 180.0f;
}

void ApplyJiggleYawConstraint(s_jigglebone_t &jiggle, const float minDegrees, const float maxDegrees) {
    jiggle.data.flags |= JiggleBoneFlags::HasYawConstraint;
    jiggle.data.minYaw = minDegrees * M_PI / 180.0f;
    jiggle.data.maxYaw = maxDegrees * M_PI / 180.0f;
}

void ApplyJiggleYawFriction(s_jigglebone_t &jiggle, const float friction) { jiggle.data.yawFriction = friction; }

void ApplyJiggleYawBounce(s_jigglebone_t &jiggle, const float bounce) { jiggle.data.yawBounce = bounce; }

void ApplyJigglePitchConstraint(s_jigglebone_t &jiggle, const float minDegrees, const float maxDegrees) {
    jiggle.data.flags |= JiggleBoneFlags::HasPitchConstraint;
    jiggle.data.minPitch = minDegrees * M_PI / 180.0f;
    jiggle.data.maxPitch = maxDegrees * M_PI / 180.0f;
}

void ApplyJigglePitchFriction(s_jigglebone_t &jiggle, const float friction) { jiggle.data.pitchFriction = friction; }

void ApplyJigglePitchBounce(s_jigglebone_t &jiggle, const float bounce) { jiggle.data.pitchBounce = bounce; }

void ApplyJiggleYawStiffness(s_jigglebone_t &jiggle, const float stiffness) {
    jiggle.data.yawStiffness = ClampStiffness(stiffness);
}

void ApplyJiggleYawDamping(s_jigglebone_t &jiggle, const float damping) {
    jiggle.data.yawDamping = ClampStiffness(damping);
}

void ApplyJigglePitchStiffness(s_jigglebone_t &jiggle, const float stiffness) {
    jiggle.data.pitchStiffness = ClampStiffness(stiffness);
}

void ApplyJigglePitchDamping(s_jigglebone_t &jiggle, const float damping) {
    jiggle.data.pitchDamping = ClampStiffness(damping);
}

void ApplyJiggleAlongStiffness(s_jigglebone_t &jiggle, const float stiffness) {
    jiggle.data.alongStiffness = ClampStiffness(stiffness);
}

void ApplyJiggleAlongDamping(s_jigglebone_t &jiggle, const float damping) {
    jiggle.data.alongDamping = ClampStiffness(damping);
}

void ApplyJiggleBaseStiffness(s_jigglebone_t &jiggle, const float stiffness) {
    jiggle.data.baseStiffness = ClampStiffness(stiffness);
}

void ApplyJiggleBaseDamping(s_jigglebone_t &jiggle, const float damping) {
    jiggle.data.baseDamping = ClampStiffness(damping);
}

void ApplyJiggleBaseLeftConstraint(s_jigglebone_t &jiggle, const float min, const float max) {
    jiggle.data.baseMinLeft = min;
    jiggle.data.baseMaxLeft = max;
}

void ApplyJiggleBaseLeftFriction(s_jigglebone_t &jiggle, const float friction) {
    jiggle.data.baseLeftFriction = friction;
}

void ApplyJiggleBaseUpConstraint(s_jigglebone_t &jiggle, const float min, const float max) {
    jiggle.data.baseMinUp = min;
    jiggle.data.baseMaxUp = max;
}

void ApplyJiggleBaseUpFriction(s_jigglebone_t &jiggle, const float friction) {
    jiggle.data.baseUpFriction = friction;
}

void ApplyJiggleBaseForwardConstraint(s_jigglebone_t &jiggle, const float min, const float max) {
    jiggle.data.baseMinForward = min;
    jiggle.data.baseMaxForward = max;
}

void ApplyJiggleBaseForwardFriction(s_jigglebone_t &jiggle, const float friction) {
    jiggle.data.baseForwardFriction = friction;
}

void ApplyJiggleBaseMass(s_jigglebone_t &jiggle, const float mass) { jiggle.data.baseMass = mass; }

s_quatinterpbone_t &ApplyQuatInterpBegin(StudioMdlState &ctx, const std::string_view bone,
                                         const std::string_view parent, const std::string_view controlParent,
                                         const std::string_view control) {
    // () zero-fills; {} would leave the Vector members uninitialized
    s_quatinterpbone_t &helper = ctx.bones.quatinterpbones.emplace_back(s_quatinterpbone_t());
    helper.bonename = bone;
    helper.parentname = parent;
    helper.controlparentname = controlParent;
    helper.controlname = control;
    return helper;
}

void ApplyQuatInterpDisplay(s_quatinterpbone_t &helper, const Vector &size, const float distance) {
    helper.percentage = distance / 100.0;
    helper.size = size;
}

bool ApplyQuatInterpTrigger(s_quatinterpbone_t &helper, const Vector &basePosition, const Vector &rotateAxisDegrees,
                            const Vector &jointOrientDegrees, const float toleranceDegrees,
                            const Vector &triggerDegrees, const Vector &anglesDegrees, const Vector &position) {
    if (helper.numtriggers >= static_cast<int>(helper.trigger.size()))
        return false;

    const RadianEuler rotateaxis(DEG2RAD(rotateAxisDegrees.x), DEG2RAD(rotateAxisDegrees.y),
                                 DEG2RAD(rotateAxisDegrees.z));
    const RadianEuler jointorient(DEG2RAD(jointOrientDegrees.x), DEG2RAD(jointOrientDegrees.y),
                                  DEG2RAD(jointOrientDegrees.z));
    const RadianEuler trigger(DEG2RAD(triggerDegrees.x), DEG2RAD(triggerDegrees.y), DEG2RAD(triggerDegrees.z));
    const RadianEuler ang(DEG2RAD(anglesDegrees.x), DEG2RAD(anglesDegrees.y), DEG2RAD(anglesDegrees.z));

    Quaternion q;
    AngleQuaternion(ang, q);

    if (rotateaxis.x != 0.0 || rotateaxis.y != 0.0 || rotateaxis.z != 0.0) {
        Quaternion q1;
        Quaternion q2;
        AngleQuaternion(rotateaxis, q1);
        QuaternionMult(q1, q, q2);
        q = q2;
    }

    if (jointorient.x != 0.0 || jointorient.y != 0.0 || jointorient.z != 0.0) {
        Quaternion q1;
        Quaternion q2;
        AngleQuaternion(jointorient, q1);
        QuaternionMult(q, q1, q2);
        q = q2;
    }

    const int j = helper.numtriggers++;
    helper.tolerance[j] = DEG2RAD(toleranceDegrees);
    AngleQuaternion(trigger, helper.trigger[j]);
    VectorAdd(basePosition, position, helper.pos[j]);
    helper.quat[j] = q;
    return true;
}

s_aimatbone_t &ApplyAimAtBegin(StudioMdlState &ctx, const std::string_view bone, const std::string_view parent,
                               const std::string_view aim) {
    // () zero-fills; {} would leave the Vector members uninitialized
    s_aimatbone_t &aimAt = ctx.bones.aimatbones.emplace_back(s_aimatbone_t());
    aimAt.bonename = bone;
    aimAt.parentname = parent;
    aimAt.aimname = aim;
    return aimAt;
}

void ApplyAimAtAimVector(s_aimatbone_t &aimAt, const Vector &vector) {
    // Make sure these are unit length on read
    Vector normalized = vector;
    VectorNormalize(normalized);
    aimAt.aimvector = normalized;
}

void ApplyAimAtUpVector(s_aimatbone_t &aimAt, const Vector &vector) {
    // Make sure these are unit length on read
    Vector normalized = vector;
    VectorNormalize(normalized);
    aimAt.upvector = normalized;
}

void ApplyAimAtBasePosition(s_aimatbone_t &aimAt, const Vector &position) { aimAt.basepos = position; }

s_axisinterpbone_t &ApplyAxisInterpBegin(StudioMdlState &ctx, const std::string_view bone,
                                         const std::string_view control, const int axis) {
    // () zero-fills; {} would leave the Vector members uninitialized
    s_axisinterpbone_t &interp = ctx.bones.axisinterpbones.emplace_back(s_axisinterpbone_t());
    interp.bonename = bone;
    interp.controlname = control;
    interp.axis = axis - 1; // MAX uses 1..3, engine 0..2
    return interp;
}

bool ApplyAxisInterpAxis(s_axisinterpbone_t &interp, const int index, const Vector &basePosition,
                         const Vector &position, const QAngle &angles) {
    if (index < 0 || index >= static_cast<int>(interp.pos.size()))
        return false;
    VectorAdd(basePosition, position, interp.pos[index]);
    AngleQuaternion(angles, interp.quat[index]);
    return true;
}

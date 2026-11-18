// Builder front end, LodPhys part: jiggle bones and procedural bones. What `$jigglebone` and the entries of
// `$proceduralbones` do, through the shared apply functions of studiomdl/apply_bone_dynamics.cpp (the QC parsers call
// the same ones). Bones are named by the model skeleton; they are resolved when the model is simplified.
#include <format>

#include "apply.hpp"
#include "studiomdl/apply_bone_dynamics.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"

namespace {
    void ApplyJiggle(StudioMdlState &ctx, ApplyState &state, const smdl_jiggle &desc) {
        const std::string &boneName = state.model.skeleton.bones[desc.bone].name;
        ScopedApplyObject object(state, std::format("jiggle bone '{}'", boneName));

        s_jigglebone_t &jiggle = ApplyJiggleBegin(ctx, boneName);
        if (desc.flexible) {
            ApplyJiggleFlexible(jiggle);
            if (desc.allowLengthFlex)
                ApplyJiggleAllowLengthFlex(jiggle);
        }
        if (desc.rigid)
            ApplyJiggleRigid(jiggle);
        if (desc.baseSpring)
            ApplyJiggleBaseSpring(jiggle);

        ApplyJiggleLength(jiggle, desc.length);
        ApplyJiggleTipMass(jiggle, desc.tipMass);
        if (desc.angleConstraint)
            ApplyJiggleAngleConstraint(jiggle, *desc.angleConstraint);
        if (desc.yawConstraint)
            ApplyJiggleYawConstraint(jiggle, desc.yawConstraint->first, desc.yawConstraint->second);
        ApplyJiggleYawFriction(jiggle, desc.yawFriction);
        ApplyJiggleYawBounce(jiggle, desc.yawBounce);
        if (desc.pitchConstraint)
            ApplyJigglePitchConstraint(jiggle, desc.pitchConstraint->first, desc.pitchConstraint->second);
        ApplyJigglePitchFriction(jiggle, desc.pitchFriction);
        ApplyJigglePitchBounce(jiggle, desc.pitchBounce);

        if (desc.flexible) {
            ApplyJiggleYawStiffness(jiggle, desc.yawStiffness);
            ApplyJiggleYawDamping(jiggle, desc.yawDamping);
            ApplyJigglePitchStiffness(jiggle, desc.pitchStiffness);
            ApplyJigglePitchDamping(jiggle, desc.pitchDamping);
            ApplyJiggleAlongStiffness(jiggle, desc.alongStiffness);
            ApplyJiggleAlongDamping(jiggle, desc.alongDamping);
        }

        if (desc.baseSpring) {
            ApplyJiggleBaseStiffness(jiggle, desc.baseStiffness);
            ApplyJiggleBaseDamping(jiggle, desc.baseDamping);
            ApplyJiggleBaseLeftConstraint(jiggle, desc.baseLeft.first, desc.baseLeft.second);
            ApplyJiggleBaseLeftFriction(jiggle, desc.baseLeftFriction);
            ApplyJiggleBaseUpConstraint(jiggle, desc.baseUp.first, desc.baseUp.second);
            ApplyJiggleBaseUpFriction(jiggle, desc.baseUpFriction);
            ApplyJiggleBaseForwardConstraint(jiggle, desc.baseForward.first, desc.baseForward.second);
            ApplyJiggleBaseForwardFriction(jiggle, desc.baseForwardFriction);
            ApplyJiggleBaseMass(jiggle, desc.baseMass);
        }

        ApplyJiggleEnd(ctx, jiggle);
    }

    void ApplyQuatInterp(StudioMdlState &ctx, ApplyState &state, const smdl_quat_interp &desc) {
        const auto &bones = state.model.skeleton.bones;
        ScopedApplyObject object(state, std::format("procedural helper '{}'", bones[desc.bone].name));

        s_quatinterpbone_t &helper = ApplyQuatInterpBegin(ctx, bones[desc.bone].name, bones[desc.parent].name,
                                                          bones[desc.controlParent].name, bones[desc.control].name);
        if (desc.hasDisplay)
            ApplyQuatInterpDisplay(helper, desc.displaySize, desc.displayDistance);
        for (const smdl_quat_interp::Trigger &trigger: desc.triggers) {
            if (!ApplyQuatInterpTrigger(helper, desc.basePosition, desc.rotateAxis, desc.jointOrient,
                                        trigger.toleranceDegrees, trigger.triggerDegrees, trigger.anglesDegrees,
                                        trigger.position))
                MdlError("too many triggers (at most {})", kMaxQuatInterpTriggers);
        }
    }

    void ApplyAimConstraint(StudioMdlState &ctx, ApplyState &state, const smdl_aim_constraint &desc) {
        const auto &bones = state.model.skeleton.bones;
        ScopedApplyObject object(state, std::format("aim constraint '{}'", bones[desc.bone].name));

        const std::string &aimName = desc.aimIsAttachment ? state.model.rig.attachments[desc.aim].name
                                                          : bones[desc.aim].name;
        s_aimatbone_t &aimAt = ApplyAimAtBegin(ctx, bones[desc.bone].name, bones[desc.parent].name, aimName);
        if (desc.aimVector)
            ApplyAimAtAimVector(aimAt, *desc.aimVector);
        if (desc.upVector)
            ApplyAimAtUpVector(aimAt, *desc.upVector);
        ApplyAimAtBasePosition(aimAt, desc.basePosition);
    }

    void ApplyAxisInterp(StudioMdlState &ctx, ApplyState &state, const smdl_axis_interp &desc) {
        const auto &bones = state.model.skeleton.bones;
        ScopedApplyObject object(state, std::format("axis interpolation '{}'", bones[desc.bone].name));

        s_axisinterpbone_t &interp = ApplyAxisInterpBegin(ctx, bones[desc.bone].name, bones[desc.control].name, desc.axis);
        for (size_t i = 0; i < desc.orientations.size(); ++i) {
            const auto &orientation = desc.orientations[i];
            if (orientation.set)
                ApplyAxisInterpAxis(interp, static_cast<int>(i), desc.basePosition, orientation.position,
                                    orientation.angles);
        }
    }
}

void ApplyBoneDynamics(StudioMdlState &ctx, ApplyState &state) {
    const BoneDynamicsDesc &dynamics = state.model.boneDynamics;
    for (const auto &jiggle: dynamics.jiggles)
        ApplyJiggle(ctx, state, *jiggle);
    for (const auto &helper: dynamics.quatInterps)
        ApplyQuatInterp(ctx, state, *helper);
    for (const auto &aim: dynamics.aimConstraints)
        ApplyAimConstraint(ctx, state, *aim);
    for (const auto &interp: dynamics.axisInterps)
        ApplyAxisInterp(ctx, state, *interp);
}

// Helpers shared by the Anim part tests: building animations from the SMD fixtures.
#pragma once

#include "harness.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace animtest {

enum class RotationForm { Euler, Quaternion };

// The quaternion of an SMD RadianEuler (x = roll, y = pitch, z = yaw), the math of mathlib's AngleQuaternion
inline smdl::Quat EulerToQuaternion(const smdl::Vec3 &euler) {
    const float sr = std::sin(euler[0] * 0.5f), cr = std::cos(euler[0] * 0.5f);
    const float sp = std::sin(euler[1] * 0.5f), cp = std::cos(euler[1] * 0.5f);
    const float sy = std::sin(euler[2] * 0.5f), cy = std::cos(euler[2] * 0.5f);
    return {sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy};
}

// An animation of the model with the frames of an animation SMD (node names must exist in the model, which the
// fixtures' shared biped skeleton guarantees). Options and commands are the caller's.
inline smdl::Animation AddAnimationFromSmd(smdl::Model &model, const std::string &name, const char *smdPath, float fps,
                                           RotationForm form = RotationForm::Euler) {
    const SmdFile smd = ReadSmd(FixturePath(smdPath));
    smdl::Animation animation = model.createAnimation(name, fps, static_cast<uint32_t>(smd.frames.size()));
    for (size_t node = 0; node < smd.nodes.size(); ++node) {
        std::vector<smdl::Vec3> positions;
        std::vector<smdl::Vec3> eulers;
        std::vector<smdl::Quat> quaternions;
        for (const SmdFrame &frame: smd.frames) {
            positions.push_back(frame.bones[node].position);
            eulers.push_back(frame.bones[node].euler);
            quaternions.push_back(EulerToQuaternion(frame.bones[node].euler));
        }
        const smdl::BoneId bone = model.findBone(smd.nodes[node].name);
        if (form == RotationForm::Euler)
            animation.setBoneFramesEuler(bone, positions, eulers);
        else
            animation.setBoneFrames(bone, positions, quaternions);
    }
    return animation;
}

} // namespace animtest

// studiomdl C++ wrapper: smdl_animation.h
#pragma once

#include <optional>

#include "common.hpp"
#include "../c/smdl_animation.h"

namespace smdl {

// Motion types (motion extraction, align / walk commands, IK chain axes): combine with |
namespace Motion {
    inline constexpr uint32_t X = SMDL_MOTION_X;
    inline constexpr uint32_t Y = SMDL_MOTION_Y;
    inline constexpr uint32_t Z = SMDL_MOTION_Z;
    inline constexpr uint32_t XR = SMDL_MOTION_XR;
    inline constexpr uint32_t YR = SMDL_MOTION_YR;
    inline constexpr uint32_t ZR = SMDL_MOTION_ZR;
    inline constexpr uint32_t LX = SMDL_MOTION_LX;
    inline constexpr uint32_t LY = SMDL_MOTION_LY;
    inline constexpr uint32_t LZ = SMDL_MOTION_LZ;
    inline constexpr uint32_t LXR = SMDL_MOTION_LXR;
    inline constexpr uint32_t LYR = SMDL_MOTION_LYR;
    inline constexpr uint32_t LZR = SMDL_MOTION_LZR;
    inline constexpr uint32_t Linear = SMDL_MOTION_LINEAR;
    inline constexpr uint32_t Quadratic = SMDL_MOTION_QUADRATIC;
}

using IkRuleType = smdl_ik_rule_type;

// The rule of an ikrule / ikfixup command: set its parameters after the command is added. Non-owning view, valid as
// long as its model lives. See smdl_animation.h.
class IkRule {
public:
    IkRule(smdl_ik_rule *rule, smdl_model *model) : m_Rule(rule), m_Model(model) {}

    smdl_ik_rule *handle() const { return m_Rule; }
    smdl_model *model() const { return m_Model; }

    IkRule &setBone(BoneId bone);
    IkRule &setAttachment(AttachmentId attachment);
    IkRule &setAutoStepsEnd(int endFrame);
    // Frames; -1 stands for ".." (unset)
    IkRule &setRange(int start, int peak, int tail, int end);
    IkRule &setHeight(float height);
    IkRule &setTarget(int slot);
    IkRule &setFloor(float floor);
    // The QC's pad is a diameter (the radius is half)
    IkRule &setPad(float diameter);
    IkRule &setRadius(float radius);
    IkRule &setContact(int contact);
    IkRule &setUseSequence(bool enabled = true);
    IkRule &setUseSource(bool enabled = true);
    IkRule &setFakeOrigin(const Vec3 &origin);
    IkRule &setFakeRotation(const Quat &rotation);
    IkRule &setFakeRotationEuler(const Vec3 &euler);
    // The QC's fakerotate: degrees (pitch, yaw, roll)
    IkRule &setFakeAngles(const Vec3 &angles);

private:
    smdl_ik_rule *m_Rule;
    smdl_model *m_Model;
};

// An animation: frames of bone poses, options and commands. Non-owning view, valid as long as its model lives. The
// setters return *this so calls can be chained. See smdl_animation.h.
class Animation {
public:
    Animation(smdl_animation *animation, smdl_model *model) : m_Animation(animation), m_Model(model) {}

    smdl_animation *handle() const { return m_Animation; }
    smdl_model *model() const { return m_Model; }

    std::string_view name() const;
    float fps() const;
    uint32_t frameCount() const;

    // ---- frames ----

    // Either span may be empty: that part keeps the rest pose; otherwise frameCount() entries
    Animation &setBoneFrames(BoneId bone, std::span<const Vec3> positions, std::span<const Quat> rotations) {
        return setBoneFrames(bone, floats(positions.data()), positions.size(), 0, floats(rotations.data()),
                             rotations.size(), 0);
    }
    // Pointer + count + stride in bytes (0 = packed), like the C API
    Animation &setBoneFrames(BoneId bone, const float *positions, size_t positionCount, size_t positionStrideBytes,
                             const float *rotations, size_t rotationCount, size_t rotationStrideBytes);
    // Rotations as SMD euler angles (radians x, y, z)
    Animation &setBoneFramesEuler(BoneId bone, std::span<const Vec3> positions, std::span<const Vec3> eulers) {
        return setBoneFramesEuler(bone, floats(positions.data()), positions.size(), 0, floats(eulers.data()),
                                  eulers.size(), 0);
    }
    Animation &setBoneFramesEuler(BoneId bone, const float *positions, size_t positionCount,
                                  size_t positionStrideBytes, const float *eulers, size_t eulerCount,
                                  size_t eulerStrideBytes);
    Animation &setRestPose(BoneId bone, const Vec3 &position, const Quat &rotation);
    Animation &setRestPoseEuler(BoneId bone, const Vec3 &position, const Vec3 &euler);

    // ---- options ($animation tokens) ----

    Animation &setFps(float fps);
    Animation &setOrigin(const Vec3 &origin);
    Animation &setRotate(float degrees);
    Animation &setAngles(float x, float y, float z);
    Animation &setScale(float scale);
    Animation &setLoop(bool enabled = true);
    Animation &setNoForceLoop(bool enabled = true);
    Animation &setStartLoop(int frame);
    Animation &setPercentStartLoop(float percent);
    Animation &setFudgeLoop(bool enabled = true);
    Animation &setSnap(bool enabled = true);
    Animation &setPost(bool enabled = true);
    Animation &setAutoIk(bool enabled);
    Animation &setMotionRollback(float rollback);
    Animation &setNoAnimBlock(bool enabled = true);
    Animation &setNoAnimBlockStall(bool enabled = true);
    Animation &setNoStallFrames(float frames);
    Animation &setFrameRange(int firstFrame, int lastFrame);
    Animation &setFrameStart(int firstFrame);
    Animation &setBlockName(std::string_view name);
    // A combination of the Motion:: flags (the QC tokens X Y Z XR YR ZR LX LY LZ LXR LYR LZR LM LQ)
    Animation &setMotionExtraction(uint32_t motion);

    // ---- commands, in the order they are added ----

    Animation &addFixupLoop(int firstFrame, int lastFrame);
    Animation &addWeightList(WeightListId weightList);
    // post: `subtract`, else `presubtract`
    Animation &addSubtract(const Animation &reference, int frame, bool post = true);
    Animation &addAlign(const Animation &reference, uint32_t motion, int srcFrame, int destFrame);
    Animation &addAlignTo(const Animation &reference);
    Animation &addAlignBone(BoneId bone, const Animation &reference, uint32_t motion, int srcFrame, int destFrame);
    Animation &addAlignBoneTo(BoneId bone, const Animation &reference);
    Animation &addMatch(const Animation &reference);
    Animation &addMatchBlend(const Animation &reference, int srcFrame, int destFrame, int destPre, int destPost);
    Animation &addWorldSpaceBlend(const Animation &reference);
    Animation &addWorldSpaceBlendLoop(const Animation &reference, int startFrame);
    Animation &addRotateTo(float degrees);

    // ikrule / ikfixup: returns the rule to set its parameters on
    IkRule addIkRule(IkChainId chain, IkRuleType type);
    IkRule addIkFixup(IkChainId chain, IkRuleType type);

    Animation &addWalkFrame(int endFrame, uint32_t motion);
    Animation &addWalkAlign(int endFrame, const Animation &reference, uint32_t motion, int refFrame, int srcFrame);
    Animation &addWalkAlignTo(int endFrame, const Animation &reference, uint32_t motion);
    Animation &addDerivative(float scale);
    Animation &addNoAnimation();
    Animation &addNoAnimationKeepDuration();
    Animation &addLinearDelta();
    Animation &addSplineDelta();
    Animation &addCompress(int frames);
    Animation &addNumFrames(int frames);
    Animation &addCounterRotate(BoneId bone);
    // Degrees
    Animation &addCounterRotateTo(BoneId bone, float pitch, float yaw, float roll);
    // parent: NoBone for none; frames -1 = unset (no range: leave all four at -1)
    Animation &addLocalHierarchy(BoneId bone, BoneId parent, int start = -1, int peak = -1, int tail = -1, int end = -1);
    // Position in units, angles in degrees; nullopt leaves one out
    Animation &addForceBonePosRot(BoneId bone, const std::optional<Vec3> &position, const std::optional<Vec3> &angles,
                                  bool rotationIsLocal = false);
    // axis 0..2; without a range the value applies to every frame
    Animation &addBoneDriver(BoneId bone, int axis, float value);
    Animation &addBoneDriver(BoneId bone, int axis, float value, int start, int peak, int tail, int end);
    Animation &addReverse();
    Animation &addAppendAnimation(const Animation &reference);

    uint32_t commandCount() const;
    // $prepend: moves the command at `from` to `to`
    Animation &moveCommand(uint32_t from, uint32_t to);

private:
    static const float *floats(const void *data) { return static_cast<const float *>(data); }

    smdl_animation *m_Animation;
    smdl_model *m_Model;
};

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
    class ModelAnimationApi {
    public:
        // name: unique among animations and sequences; "@<sequence>" is the name of a sequence's own animation (QC
        // implied animation). Animations are applied in creation order: create the ones a command refers to first.
        Animation createAnimation(std::string_view name, float fps, uint32_t frameCount);

        // Throws smdl::Error (SMDL_ERROR_NOT_FOUND) for an unknown name
        Animation findAnimation(std::string_view name) const;

        uint32_t animationCount() const;

        Animation animation(uint32_t index) const;

    private:
        smdl_model *model() const; // api/src/smdl_model.cpp
    };
}

} // namespace smdl

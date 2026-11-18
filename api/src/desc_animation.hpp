// Description of animations (smdl_animation.h); defines struct smdl_animation. Owned by the Anim part.
#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "desc_shared.hpp"
#include "studiomdl/c/smdl_animation.h"

struct smdl_animation;

// An ikrule / ikfixup command's rule (Option_IKRule): the type and chain are fixed at creation, everything else is
// optional and overrides the defaults InitIkRule derives from the chain.
struct smdl_ik_rule {
    smdl_model *model = nullptr;
    const smdl_animation *animation = nullptr;
    smdl_ik_chain_id chain = 0;
    smdl_ik_rule_type type = SMDL_IK_RULE_FOOTSTEP;
    bool isFixup = false;

    std::optional<smdl_bone_id> bone;             // touch, autosteps
    std::optional<smdl_attachment_id> attachment; // attachment
    int autoStepsEnd = 0;
    std::optional<std::array<int, 4>> range;      // start, peak, tail, end (-1 = unset)
    std::optional<float> height;
    std::optional<int> target;
    std::optional<float> floor;
    std::optional<float> radius;
    std::optional<int> contact;
    int useMode = 0;                              // 0 untouched, 1 usesequence, 2 usesource, 3 neither
    std::optional<Vector> fakeOrigin;
    std::optional<Quaternion> fakeRotation;
};

// One animation command, in call order. Field use per type is documented where ApplyAnimations reads it.
struct AnimationCommandDesc {
    enum class Type {
        FixupLoop, Weights, Subtract, Align, Match, MatchBlend, WorldSpaceBlend, RotateTo, IkRule, WalkFrame,
        WalkAlign, Derivative, NoAnimation, NoAnimationKeepDuration, LinearDelta, SplineDelta, Compress, NumFrames,
        CounterRotate, LocalHierarchy, ForceBonePosRot, BoneDriver, Reverse, AppendAnimation
    } type = Type::Reverse;

    const smdl_animation *reference = nullptr;
    smdl_ik_rule *rule = nullptr;
    uint32_t motion = 0;
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    float value = 0.0f;
    bool flag = false;
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_id parent = SMDL_NO_BONE;
    smdl_weight_list_id weightList = 0;
    std::optional<std::array<float, 3>> vectorA;
    std::optional<std::array<float, 3>> vectorB;
};

struct AnimationBoneFrames {
    std::vector<Vector> positions;       // frameCount entries, or empty (rest pose)
    std::vector<RadianEuler> rotations;  // frameCount entries, or empty (rest pose)
};

struct AnimationOptionsDesc {
    struct RotationStep {
        bool angles = false; // true: `angles x y z`, false: `rotate yaw`
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    struct FrameRange {
        int first = 0;
        int last = 0;
        bool toEnd = false; // framestart
    };

    std::optional<float> fps;
    std::optional<Vector> origin;
    std::vector<RotationStep> rotation;
    std::optional<float> scale;
    bool loop = false;
    bool noForceLoop = false;
    std::optional<int> startLoop;
    std::optional<float> percentStartLoop;
    bool fudgeLoop = false;
    bool snap = false;
    bool post = false;
    std::optional<bool> autoIk;
    std::optional<float> motionRollback;
    bool noAnimBlock = false;
    bool noAnimBlockStall = false;
    std::optional<float> noStallFrames;
    std::optional<std::string> blockName;
    std::optional<FrameRange> frameRange;
    uint32_t motion = 0;
};

struct smdl_animation {
    smdl_model *model = nullptr;
    uint32_t index = 0; // creation order
    std::string name;
    float fps = 30.0f;
    uint32_t frameCount = 1;

    std::map<smdl_bone_id, AnimationBoneFrames> bones;
    std::vector<RestPoseOverride> restOverrides;

    AnimationOptionsDesc options;
    std::vector<AnimationCommandDesc> commands;
    std::deque<std::unique_ptr<smdl_ik_rule>> ikRules; // owners of the rules the commands point to
};

struct AnimationsDesc {
    std::deque<std::unique_ptr<smdl_animation>> list; // creation order = apply order
};

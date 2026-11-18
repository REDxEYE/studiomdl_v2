// Description of attachments, hitboxes, pose params, IK chains, weight lists (smdl_rig.h). Owned by the Anim part.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "studiomdl/c/smdl_rig.h"
#include "desc_shared.hpp"

// One definition of an attachment ($attachment or $redefineattachment)
struct AttachmentRevisionDesc {
    enum class StepKind { Absolute, Angles, Euler, Quaternion, Axes };
    struct Step {
        StepKind kind = StepKind::Angles;
        QAngle angles{0, 0, 0};          // degrees
        RadianEuler euler{0, 0, 0};
        Quaternion quaternion{0, 0, 0, 1};
        Vector xAxis{1, 0, 0};
        Vector zAxis{0, 0, 1};
    };

    smdl_bone_id bone = SMDL_NO_BONE;
    Vector position{0, 0, 0};
    std::vector<Step> steps; // in call order
    bool rigid = false;
    bool worldAlign = false;
};

struct AttachmentDesc {
    std::string name;
    std::vector<AttachmentRevisionDesc> revisions; // [0] = $attachment, the others $redefineattachment
};

struct HitboxDesc {
    int group = 0;
    smdl_bone_id bone = SMDL_NO_BONE;
    Vector mins{0, 0, 0};
    Vector maxs{0, 0, 0};
    QAngle angles{0, 0, 0};
    float capsuleRadius = -1.0f;
    std::string name;
};

struct HitboxSetDesc {
    std::string name;
    std::vector<HitboxDesc> hitboxes;
};

struct HitGroupDesc {
    int group = 0;
    smdl_bone_id bone = SMDL_NO_BONE;
};

struct PoseParameterDesc {
    std::string name;
    float min = 0.0f;
    float max = 0.0f;
    smdl_pose_loop_mode loopMode = SMDL_POSE_LOOP_NONE;
    float loopRange = 0.0f;
};

struct IkChainDesc {
    std::string name;
    smdl_bone_id endBone = SMDL_NO_BONE;
    uint32_t axis = 0x4; // SMDL_MOTION_Z
    float value = 0.0f;
    float height = 18.0f;
    float floor = 0.0f;
    float radius = 0.0f; // pad / 2
    std::optional<Vector> knee;
    std::optional<Vector> center;
};

struct IkLockDesc {
    smdl_ik_chain_id chain = 0;
    float positionWeight = 0.0f;
    float localQWeight = 0.0f;
};

struct WeightEntryDesc {
    smdl_bone_id bone = SMDL_NO_BONE;
    float weight = 0.0f;
    float positionWeight = 0.0f;
};

struct WeightListDesc {
    std::string name; // empty for the default list
    std::vector<WeightEntryDesc> entries;
};

struct RigDesc {
    std::vector<AttachmentDesc> attachments;
    std::vector<HitboxSetDesc> hitboxSets;
    std::vector<HitGroupDesc> hitGroups;
    std::vector<PoseParameterDesc> poseParameters;
    std::vector<IkChainDesc> ikChains;
    std::vector<IkLockDesc> ikLocks;
    std::vector<WeightListDesc> weightLists{WeightListDesc{}}; // [0] = the default list
};

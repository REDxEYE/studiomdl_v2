// Description of bone dynamics (smdl_bone_dynamics.h): jiggle bones and procedural bones (quaternion interpolation
// helpers, aim constraints, axis interpolation). Owned by the LodPhys part.
//
// Bones are model skeleton ids. Jiggle bones keep every QC parameter with the QC's default; a section (is_flexible,
// is_rigid, has_base_spring) is a flag, and the parameters of a section can only be set once its flag is. The compile
// (ApplyBoneDynamics, apply_bone_dynamics.cpp) applies jiggle bones, then quaternion helpers (with their triggers), aim
// constraints and axis interpolation bones, each in creation order.
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "desc_shared.hpp"

struct smdl_jiggle {
    smdl_model *model = nullptr;
    smdl_bone_id bone = SMDL_NO_BONE;

    bool flexible = false;        // is_flexible
    bool rigid = false;           // is_rigid
    bool baseSpring = false;      // has_base_spring
    bool allowLengthFlex = false; // allow_length_flex (flexible)

    // every section
    float length = 10.0f;
    float tipMass = 0.0f;
    std::optional<float> angleConstraint;                  // degrees
    std::optional<std::pair<float, float>> yawConstraint;  // degrees
    float yawFriction = 0.0f;
    float yawBounce = 0.0f;
    std::optional<std::pair<float, float>> pitchConstraint; // degrees
    float pitchFriction = 0.0f;
    float pitchBounce = 0.0f;

    // flexible
    float yawStiffness = 100.0f;
    float yawDamping = 0.0f;
    float pitchStiffness = 100.0f;
    float pitchDamping = 0.0f;
    float alongStiffness = 100.0f;
    float alongDamping = 0.0f;

    // base spring
    float baseStiffness = 100.0f;
    float baseDamping = 0.0f;
    std::pair<float, float> baseLeft{-100.0f, 100.0f};
    float baseLeftFriction = 0.0f;
    std::pair<float, float> baseUp{-100.0f, 100.0f};
    float baseUpFriction = 0.0f;
    std::pair<float, float> baseForward{-100.0f, 100.0f};
    float baseForwardFriction = 0.0f;
    float baseMass = 0.0f;
};

inline constexpr size_t kMaxQuatInterpTriggers = 32;

// A `<helper>` of a .vrd file
struct smdl_quat_interp {
    smdl_model *model = nullptr;
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_id parent = SMDL_NO_BONE;
    smdl_bone_id controlParent = SMDL_NO_BONE;
    smdl_bone_id control = SMDL_NO_BONE;

    bool hasDisplay = false;      // `<display>`: size and percentage (distance / 100)
    Vector displaySize{0, 0, 0};
    float displayDistance = 0.0f;
    Vector basePosition{0, 0, 0}; // `<basepos>`
    Vector rotateAxis{0, 0, 0};   // `<rotateaxis>`, degrees
    Vector jointOrient{0, 0, 0};  // `<jointorient>`, degrees

    struct Trigger {
        float toleranceDegrees = 0.0f;
        Vector triggerDegrees{0, 0, 0};
        Vector anglesDegrees{0, 0, 0};
        Vector position{0, 0, 0};
    };
    std::vector<Trigger> triggers;
};

// An `<aimconstraint>` of a .vrd file
struct smdl_aim_constraint {
    smdl_model *model = nullptr;
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_id parent = SMDL_NO_BONE;
    bool aimIsAttachment = false;
    uint32_t aim = 0; // bone id or attachment id
    std::optional<Vector> aimVector;
    std::optional<Vector> upVector;
    Vector basePosition{0, 0, 0};
};

// A `bone` entry of a procedural bones file (axis interpolation)
struct smdl_axis_interp {
    smdl_model *model = nullptr;
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_id control = SMDL_NO_BONE;
    int axis = 1; // 1..3 as the file writes it
    Vector basePosition{0, 0, 0};
    struct Orientation {
        bool set = false;
        Vector position{0, 0, 0};
        QAngle angles{0, 0, 0}; // degrees (pitch, yaw, roll)
    };
    std::array<Orientation, 6> orientations;
};

struct BoneDynamicsDesc {
    std::vector<std::unique_ptr<smdl_jiggle>> jiggles;
    std::vector<std::unique_ptr<smdl_quat_interp>> quatInterps;
    std::vector<std::unique_ptr<smdl_aim_constraint>> aimConstraints;
    std::vector<std::unique_ptr<smdl_axis_interp>> axisInterps;
};

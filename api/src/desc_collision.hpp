// Description of the collision model (smdl_collision.h); defines struct smdl_collision. Owned by the LodPhys part.
//
// The collision model is what `$collisionmodel` / `$collisionjoints` and the commands of their block describe. The
// commands are kept in call order (`ops`) and replayed in that order by ApplyCollision (apply_collision.cpp), like the
// lines of the QC block. The geometry is the source file of the command (or none: `$collisionmodel blank` with convex
// sources). Joints are bones of the model skeleton, by id; the collision geometry's own skeleton is the model skeleton.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "desc_shared.hpp"

struct CollisionOp {
    enum class Kind {
        Mass,                    // value[0]
        AutoMass,
        Inertia,                 // value[0]
        Damping,                 // value[0]
        RotDamping,              // value[0]
        Drag,                    // value[0]
        RollingDrag,             // value[0]: a documented no-op in the QC
        MaxConvexPieces,         // integer[0]
        Remove2D,
        ConcavePerJoint,
        WeldPosition,            // value[0]
        WeldNormal,              // value[0]
        Concave,
        ConvexHullCountOverride,
        MassCenter,              // vector
        NoSelfCollisions,
        AssumeWorldSpace,
        RootBone,                // bone
        AnimatedFriction,        // integer[0..1] min/max friction, value[0..2] time in/hold/out
        JointSkip,               // bone
        JointMerge,              // bone = parent, bone2 = child
        JointConstrain,          // bone, integer[0] axis (0..2), integer[1] type (CollisionJointType), value[0..2] min/max/friction
        JointInertia,            // bone, value[0]
        JointDamping,            // bone, value[0]
        JointRotDamping,         // bone, value[0]
        JointMassBias,           // bone, value[0]
        JointCollide,            // bone, bone2
        JointCollideGroup,       // bones (every ordered pair of different entries)
        JointNoCollide,          // bone, bone2
        ConvexSource,            // index into smdl_collision::convexSources
    };

    Kind kind = Kind::AutoMass;
    float value[3] = {0, 0, 0};
    int integer[2] = {0, 0};
    Vector vector{0, 0, 0};
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_id bone2 = SMDL_NO_BONE;
    std::vector<smdl_bone_id> bones;
    uint32_t index = 0;
};

// `$addconvexsrc <file> [offset pos[...] angle[...] scale[...]] [concave]`
struct CollisionConvexSource {
    smdl_geometry *geometry = nullptr;
    bool ownsGeometry = false; // created by smdl_collision_add_convex_source (its source name can be set)
    bool hasOffset = false;
    Vector position{0, 0, 0};
    QAngle angles{0, 0, 0};   // degrees (pitch, yaw, roll)
    float scale = 1.0f;       // relative to the model's $scale
    bool concave = false;
};

inline constexpr size_t kMaxCollisionConvexSources = 24;  // MAX_EXTRA_COLLISION_MODELS
inline constexpr size_t kMaxCollisionGroupJoints = 32;    // $jointcollidealltoall

struct smdl_collision {
    smdl_model *model = nullptr;
    smdl_geometry *geometry = nullptr; // root collision source; null = blank (needs convex sources)
    bool ownsGeometry = false;         // created by smdl_collision_get_geometry (its source name can be set)
    bool jointed = false;              // $collisionjoints (a constraint system) instead of $collisionmodel
    std::vector<CollisionOp> ops;
    std::vector<CollisionConvexSource> convexSources;
    std::string text;                  // the .phy text section (KeyValues text), applied last
    bool hasText = false;

    // The model has a collision model at all
    bool present() const { return geometry || !convexSources.empty() || !ops.empty() || hasText; }
};

struct CollisionDesc {
    std::unique_ptr<smdl_collision> collision; // created by the first smdl_model_get_collision
};

// studiomdl C++ wrapper: smdl_collision.h
#pragma once

#include <span>

#include "common.hpp"
#include "geometry.hpp"
#include "body.hpp"
#include "../c/smdl_collision.h"

namespace smdl {

enum class JointType : int {
    Free = SMDL_COLLISION_JOINT_FREE,
    Fixed = SMDL_COLLISION_JOINT_FIXED,
    Limit = SMDL_COLLISION_JOINT_LIMIT,
};

enum class CollisionAxis : int { X = SMDL_COLLISION_AXIS_X, Y = SMDL_COLLISION_AXIS_Y, Z = SMDL_COLLISION_AXIS_Z };

// The model's collision model ($collisionmodel / $collisionjoints and the commands of their block). Non-owning view,
// valid as long as its model lives. The commands act in call order. See smdl_collision.h.
class Collision {
public:
    Collision(smdl_collision *collision, smdl_model *model) : m_Collision(collision), m_Model(model) {}

    smdl_collision *handle() const { return m_Collision; }
    smdl_model *model() const { return m_Model; }

    // -- source -----------------------------------------------------------------------------------------------------

    // The collision model's own geometry (created on first use): fill it like a mesh geometry
    Geometry geometry() const;
    // ... or an existing geometry of the model, for example a mesh's
    const Collision &setGeometry(const Geometry &geometry) const;
    // $collisionjoints (true) instead of $collisionmodel
    const Collision &setJointed(bool jointed) const;

    // The name of the collision source (a path without extension, e.g. "src/phys_box"); it shows in the .phy
    const Collision &setSourceName(std::string_view name) const;
    const Collision &setConvexSourceName(uint32_t index, std::string_view name) const;

    // $addconvexsrc: a new geometry for another source of convex pieces (at most 24); `index` names it for the
    // offset / concave setters
    Geometry addConvexSource(uint32_t *index = nullptr) const;
    uint32_t addConvexSource(const Geometry &geometry) const;
    uint32_t convexSourceCount() const;
    // offset pos[...] angle[...] scale[...]: angles are a QAngle in degrees (pitch, yaw, roll)
    const Collision &setConvexSourceOffset(uint32_t index, const Vec3 &position, const Vec3 &anglesDegrees,
                                           float scale = 1.0f) const;
    const Collision &setConvexSourceConcave(uint32_t index, bool concave = true) const;

    // The .phy text section (KeyValues text), applied after the commands
    const Collision &setText(std::string_view text) const;

    // -- whole model ------------------------------------------------------------------------------------------------

    const Collision &setMass(float mass) const;
    const Collision &setAutoMass() const;
    const Collision &setInertia(float inertia) const;
    const Collision &setDamping(float damping) const;
    const Collision &setRotDamping(float rotDamping) const;
    const Collision &setDrag(float drag) const;
    // $rollingDrag: no effect in studiomdl
    const Collision &setRollingDrag(float rollingDrag) const;
    const Collision &setMaxConvexPieces(int count) const;
    const Collision &setRemove2D() const;
    const Collision &setConcavePerJoint() const;
    const Collision &setWeldPosition(float epsilon) const;
    const Collision &setWeldNormal(float epsilon) const;
    const Collision &setConcave() const;
    const Collision &setConvexHullCountOverride() const;
    const Collision &setMassCenter(const Vec3 &center) const;
    const Collision &setNoSelfCollisions() const;
    const Collision &setAssumeWorldSpace() const;
    const Collision &setRootBone(BoneId bone) const;
    const Collision &setAnimatedFriction(int minFriction, int maxFriction, float timeIn, float timeHold,
                                         float timeOut) const;

    // -- joints -----------------------------------------------------------------------------------------------------

    const Collision &jointSkip(BoneId joint) const;
    // the child's vertices are added to the parent's collision model
    const Collision &jointMerge(BoneId parent, BoneId child) const;
    // min <= max (degrees, for Limit); friction is the QC value (the QC default is 1)
    const Collision &jointConstrain(BoneId joint, CollisionAxis axis, JointType type, float min, float max,
                                    float friction = 1.0f) const;
    const Collision &jointSetInertia(BoneId joint, float inertia) const;
    const Collision &jointSetDamping(BoneId joint, float damping) const;
    const Collision &jointSetRotDamping(BoneId joint, float rotDamping) const;
    const Collision &jointSetMassBias(BoneId joint, float massBias) const;
    const Collision &jointCollide(BoneId a, BoneId b) const;
    // every ordered pair of different entries collide (at most 32 joints)
    const Collision &jointCollideAll(std::span<const BoneId> joints) const;
    const Collision &jointNoCollide(BoneId a, BoneId b) const;

private:
    smdl_collision *m_Collision;
    smdl_model *m_Model;
};

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
        class ModelCollisionApi {
    public:
        // The model's collision model (the same object every time; a model whose collision model is never filled has
        // none)
        Collision collision();

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

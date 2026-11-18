// smdl_collision.h: the collision model (store only; the compile applies it in ApplyCollision, apply_collision.cpp)
#include "studiomdl/c/smdl_collision.h"
#include "studiomdl/studiomdl.hpp"

#include <cmath>
#include <cstring>

#include "core_util.hpp"

using namespace smdl_api;

namespace {
    using Kind = CollisionOp::Kind;

    smdl_status CheckBone(const smdl_collision &collision, smdl_bone_id bone, const char *what) {
        const smdl_model &model = *collision.model;
        if (!model.skeleton.valid(bone))
            return Fail(&model, SMDL_ERROR_OUT_OF_RANGE, "collision: {}: bone id {} is not a bone of the model ({} bones)",
                        what, bone, model.skeleton.bones.size());
        return SMDL_OK;
    }

    smdl_status CheckFinite(const smdl_collision &collision, std::initializer_list<float> values, const char *what) {
        for (const float value: values) {
            if (!std::isfinite(value))
                return Fail(collision.model, SMDL_ERROR_INVALID_ARGUMENT, "collision: {} is not a finite number", what);
        }
        return SMDL_OK;
    }

    CollisionOp &Add(smdl_collision &collision, Kind kind) {
        CollisionOp &op = collision.ops.emplace_back();
        op.kind = kind;
        return op;
    }

    // One float parameter; throws smdl::Error on failure
    template<class Check>
    void AddValue(smdl_collision *collision, Kind kind, float value, const char *what, Check &&check) {
        smdl_model *model = collision->model;
        RunBody(model, [&] {
            if (const auto status = CheckFinite(*collision, {value}, what))
                return status;
            if (const auto status = check(value))
                return status;
            Add(*collision, kind).value[0] = value;
            return Ok(model);
        });
    }

    void AddValue(smdl_collision *collision, Kind kind, float value, const char *what) {
        AddValue(collision, kind, value, what, [](float) { return SMDL_OK; });
    }

    void AddFlag(smdl_collision *collision, Kind kind) {
        smdl_model *model = collision->model;
        RunBody(model, [&] {
            Add(*collision, kind);
            return Ok(model);
        });
    }

    void AddJointValue(smdl_collision *collision, Kind kind, smdl_bone_id joint, float value, const char *what) {
        smdl_model *model = collision->model;
        RunBody(model, [&] {
            if (const auto status = CheckBone(*collision, joint, what))
                return status;
            if (const auto status = CheckFinite(*collision, {value}, what))
                return status;
            CollisionOp &op = Add(*collision, kind);
            op.bone = joint;
            op.value[0] = value;
            return Ok(model);
        });
    }

    void AddJointPair(smdl_collision *collision, Kind kind, smdl_bone_id a, smdl_bone_id b, const char *what) {
        smdl_model *model = collision->model;
        RunBody(model, [&] {
            if (const auto status = CheckBone(*collision, a, what))
                return status;
            if (const auto status = CheckBone(*collision, b, what))
                return status;
            CollisionOp &op = Add(*collision, kind);
            op.bone = a;
            op.bone2 = b;
            return Ok(model);
        });
    }
    smdl_status AddConvex(smdl_collision &collision, smdl_geometry *geometry, uint32_t *out_index) {
        smdl_model *model = collision.model;
        if (collision.convexSources.size() >= kMaxCollisionConvexSources)
            return Fail(model, SMDL_ERROR_LIMIT, "collision: at most {} convex sources", kMaxCollisionConvexSources);
        const uint32_t index = static_cast<uint32_t>(collision.convexSources.size());
        collision.convexSources.push_back({geometry});
        Add(collision, Kind::ConvexSource).index = index;
        if (out_index)
            *out_index = index;
        return Ok(model);
    }

    smdl_status CheckConvexIndex(const smdl_collision &collision, uint32_t index) {
        if (index >= collision.convexSources.size())
            return Fail(collision.model, SMDL_ERROR_OUT_OF_RANGE, "collision: convex source {} does not exist ({} sources)",
                        index, collision.convexSources.size());
        return SMDL_OK;
    }
}
namespace smdl {
namespace detail {

Collision ModelCollisionApi::collision() {
    smdl_model *model = this->model();
    smdl_collision *result = nullptr;
    smdl_collision **out_collision = &result;
    RunBody(model, [&] {
        auto &collision = model->collision.collision;
        if (!collision) {
            collision = std::make_unique<smdl_collision>();
            collision->model = model;
        }
        if (out_collision)
            *out_collision = collision.get();
        return Ok(model);
    });
    return Collision(result, model);
}

} // namespace detail

Geometry Collision::geometry() const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    smdl_geometry *result = nullptr;
    smdl_geometry **out_geometry = &result;
    RunBody(model, [&] {
        if (!collision->geometry) {
            collision->geometry = NewGeometry(*model, "collision model");
            collision->geometry->sourceName = "collision";
            collision->ownsGeometry = true;
        }
        if (out_geometry)
            *out_geometry = collision->geometry;
        return Ok(model);
    });
    return Geometry(result, model);
}

const Collision &Collision::setGeometry(const Geometry &geometryView) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    smdl_geometry *geometry = geometryView.handle();
    RunBody(model, [&] {
        if (!geometry || geometry->model != model)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: geometry is null or not a geometry of this model");
        collision->geometry = geometry;
        collision->ownsGeometry = false;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setJointed(bool jointedValue) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const int jointed = jointedValue;
    RunBody(model, [&] {
        collision->jointed = jointed != 0;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setSourceName(std::string_view nameView) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: source name is null or empty");
        if (!collision->geometry || !collision->ownsGeometry)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "collision: the source name can only be set on the geometry from smdl_collision_get_geometry");
        collision->geometry->sourceName = name;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setConvexSourceName(uint32_t index, std::string_view nameView) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    RunBody(model, [&] {
        if (const auto status = CheckConvexIndex(*collision, index))
            return status;
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: source name is null or empty");
        CollisionConvexSource &source = collision->convexSources[index];
        if (!source.ownsGeometry)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "collision: the source name can only be set on a convex source created by smdl_collision_add_convex_source");
        source.geometry->sourceName = name;
        return Ok(model);
    });
    return *this;
}

Geometry Collision::addConvexSource(uint32_t *index) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    smdl_geometry *result = nullptr;
    smdl_geometry **out_geometry = &result;
    uint32_t *out_index = index;
    RunBody(model, [&] {
        if (collision->convexSources.size() >= kMaxCollisionConvexSources)
            return Fail(model, SMDL_ERROR_LIMIT, "collision: at most {} convex sources", kMaxCollisionConvexSources);
        smdl_geometry *geometry =
                NewGeometry(*model, std::format("collision convex source {}", collision->convexSources.size()));
        geometry->sourceName = std::format("collision_convex{}", collision->convexSources.size());
        if (const auto status = AddConvex(*collision, geometry, out_index))
            return status;
        collision->convexSources.back().ownsGeometry = true;
        if (out_geometry)
            *out_geometry = geometry;
        return SMDL_OK;
    });
    return Geometry(result, model);
}

uint32_t Collision::addConvexSource(const Geometry &geometryView) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    smdl_geometry *geometry = geometryView.handle();
    uint32_t result = 0;
    uint32_t *out_index = &result;
    RunBody(model, [&] {
        if (!geometry || geometry->model != model)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "collision: convex source geometry is null or not a geometry of this model");
        return AddConvex(*collision, geometry, out_index);
    });
    return result;
}

uint32_t Collision::convexSourceCount() const { return static_cast<uint32_t>(handle()->convexSources.size()); }

const Collision &Collision::setConvexSourceOffset(uint32_t index, const Vec3 &positionValue,
                                                  const Vec3 &anglesValue, float scale) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const float *position = positionValue.data();
    const float *angles = anglesValue.data();
    RunBody(model, [&] {
        if (!position || !angles)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: convex source offset position or angles is null");
        if (const auto status = CheckConvexIndex(*collision, index))
            return status;
        if (!AllFinite(position, 3) || !AllFinite(angles, 3) || !std::isfinite(scale))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: convex source offset is not finite");
        CollisionConvexSource &source = collision->convexSources[index];
        source.hasOffset = true;
        source.position = ToVector(position);
        source.angles = QAngle(angles[0], angles[1], angles[2]);
        source.scale = scale;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setConvexSourceConcave(uint32_t index, bool concaveValue) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const int concave = concaveValue;
    RunBody(model, [&] {
        if (const auto status = CheckConvexIndex(*collision, index))
            return status;
        collision->convexSources[index].concave = concave != 0;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setText(std::string_view textView) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const std::string textStorage(textView);
    const char *text = textStorage.c_str();
    RunBody(model, [&] {
        if (!text)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: text is null");
        collision->text = text;
        collision->hasText = true;
        return Ok(model);
    });
    return *this;
}

// -- whole model ------------------------------------------------------------------------------------------------

const Collision &Collision::setMass(float mass) const {
    AddValue(handle(), Kind::Mass, mass, "mass");
    return *this;
}
const Collision &Collision::setAutoMass() const {
    AddFlag(handle(), Kind::AutoMass);
    return *this;
}
const Collision &Collision::setInertia(float inertia) const {
    AddValue(handle(), Kind::Inertia, inertia, "inertia");
    return *this;
}
const Collision &Collision::setDamping(float damping) const {
    AddValue(handle(), Kind::Damping, damping, "damping");
    return *this;
}
const Collision &Collision::setRotDamping(float rot_damping) const {
    AddValue(handle(), Kind::RotDamping, rot_damping, "rotational damping");
    return *this;
}
const Collision &Collision::setDrag(float drag) const {
    AddValue(handle(), Kind::Drag, drag, "drag");
    return *this;
}
const Collision &Collision::setRollingDrag(float rolling_drag) const {
    AddValue(handle(), Kind::RollingDrag, rolling_drag, "rolling drag");
    return *this;
}

const Collision &Collision::setMaxConvexPieces(int count) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    RunBody(model, [&] {
        if (count < 0)
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "collision: max convex pieces {} is negative", count);
        Add(*collision, Kind::MaxConvexPieces).integer[0] = count;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setRemove2D() const {
    AddFlag(handle(), Kind::Remove2D);
    return *this;
}
const Collision &Collision::setConcavePerJoint() const {
    AddFlag(handle(), Kind::ConcavePerJoint);
    return *this;
}
const Collision &Collision::setWeldPosition(float epsilon) const {
    AddValue(handle(), Kind::WeldPosition, epsilon, "weld position epsilon");
    return *this;
}
const Collision &Collision::setWeldNormal(float epsilon) const {
    AddValue(handle(), Kind::WeldNormal, epsilon, "weld normal epsilon");
    return *this;
}
const Collision &Collision::setConcave() const {
    AddFlag(handle(), Kind::Concave);
    return *this;
}
const Collision &Collision::setConvexHullCountOverride() const {
    AddFlag(handle(), Kind::ConvexHullCountOverride);
    return *this;
}

const Collision &Collision::setMassCenter(const Vec3 &centerValue) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const float *center = centerValue.data();
    RunBody(model, [&] {
        if (!center || !AllFinite(center, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: mass center is null or not finite");
        Add(*collision, Kind::MassCenter).vector = ToVector(center);
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setNoSelfCollisions() const {
    AddFlag(handle(), Kind::NoSelfCollisions);
    return *this;
}
const Collision &Collision::setAssumeWorldSpace() const {
    AddFlag(handle(), Kind::AssumeWorldSpace);
    return *this;
}

const Collision &Collision::setRootBone(BoneId bone) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    RunBody(model, [&] {
        if (const auto status = CheckBone(*collision, bone, "root bone"))
            return status;
        Add(*collision, Kind::RootBone).bone = bone;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::setAnimatedFriction(int min_friction, int max_friction, float time_in, float time_hold,
                                                float time_out) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    RunBody(model, [&] {
        if (const auto status = CheckFinite(*collision, {time_in, time_hold, time_out}, "animated friction time"))
            return status;
        CollisionOp &op = Add(*collision, Kind::AnimatedFriction);
        op.integer[0] = min_friction;
        op.integer[1] = max_friction;
        op.value[0] = time_in;
        op.value[1] = time_hold;
        op.value[2] = time_out;
        return Ok(model);
    });
    return *this;
}

// -- joints -----------------------------------------------------------------------------------------------------

const Collision &Collision::jointSkip(BoneId joint) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    RunBody(model, [&] {
        if (const auto status = CheckBone(*collision, joint, "joint skip"))
            return status;
        Add(*collision, Kind::JointSkip).bone = joint;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::jointMerge(BoneId parent, BoneId child) const {
    AddJointPair(handle(), Kind::JointMerge, parent, child, "joint merge");
    return *this;
}

const Collision &Collision::jointConstrain(BoneId joint, CollisionAxis axisValue, JointType typeValue, float min,
                                           float max, float friction) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const smdl_collision_axis axis = static_cast<smdl_collision_axis>(axisValue);
    const smdl_collision_joint_type type = static_cast<smdl_collision_joint_type>(typeValue);
    RunBody(model, [&] {
        if (const auto status = CheckBone(*collision, joint, "joint constraint"))
            return status;
        if (axis < SMDL_COLLISION_AXIS_X || axis > SMDL_COLLISION_AXIS_Z)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: joint constraint axis {} is not 0, 1 or 2",
                        static_cast<int>(axis));
        if (type < SMDL_COLLISION_JOINT_FREE || type > SMDL_COLLISION_JOINT_LIMIT)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: joint constraint type {} is not valid",
                        static_cast<int>(type));
        if (const auto status = CheckFinite(*collision, {min, max, friction}, "joint constraint value"))
            return status;
        if (min > max)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: joint constraint minimum {} is above the maximum {}",
                        min, max);
        CollisionOp &op = Add(*collision, Kind::JointConstrain);
        op.bone = joint;
        op.integer[0] = axis;
        op.integer[1] = type;
        op.value[0] = min;
        op.value[1] = max;
        op.value[2] = friction;
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::jointSetInertia(BoneId joint, float inertia) const {
    AddJointValue(handle(), Kind::JointInertia, joint, inertia, "joint inertia");
    return *this;
}
const Collision &Collision::jointSetDamping(BoneId joint, float damping) const {
    AddJointValue(handle(), Kind::JointDamping, joint, damping, "joint damping");
    return *this;
}
const Collision &Collision::jointSetRotDamping(BoneId joint, float rot_damping) const {
    AddJointValue(handle(), Kind::JointRotDamping, joint, rot_damping, "joint rotational damping");
    return *this;
}
const Collision &Collision::jointSetMassBias(BoneId joint, float mass_bias) const {
    AddJointValue(handle(), Kind::JointMassBias, joint, mass_bias, "joint mass bias");
    return *this;
}

const Collision &Collision::jointCollide(BoneId a, BoneId b) const {
    AddJointPair(handle(), Kind::JointCollide, a, b, "joint collide");
    return *this;
}

const Collision &Collision::jointCollideAll(std::span<const BoneId> jointsView) const {
    smdl_collision *collision = handle();
    smdl_model *model = collision->model;
    const smdl_bone_id *joints = jointsView.data();
    size_t count = jointsView.size();
    RunBody(model, [&] {
        if (!joints && count)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "collision: joint list is null");
        if (count > kMaxCollisionGroupJoints)
            return Fail(model, SMDL_ERROR_LIMIT, "collision: at most {} joints in a collide-all group",
                        kMaxCollisionGroupJoints);
        for (size_t i = 0; i < count; ++i) {
            if (const auto status = CheckBone(*collision, joints[i], "joint collide all"))
                return status;
        }
        Add(*collision, Kind::JointCollideGroup).bones.assign(joints, joints + count);
        return Ok(model);
    });
    return *this;
}

const Collision &Collision::jointNoCollide(BoneId a, BoneId b) const {
    AddJointPair(handle(), Kind::JointNoCollide, a, b, "joint no collide");
    return *this;
}

} // namespace smdl

namespace {
    smdl::Collision View(smdl_collision *c) { return smdl::Collision(c, c->model); }

    // The null-handle check of the C functions that act on a collision
    template<class F>
    smdl_status OnCollision(smdl_collision *c, F &&body) {
        if (!c)
            return SMDL_ERROR_INVALID_ARGUMENT;
        return CallCpp(c->model, [&] { body(View(c)); });
    }
}

extern "C" {

SMDL_API smdl_status smdl_model_get_collision(smdl_model *model, smdl_collision **out_collision) {
    if (out_collision)
        *out_collision = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Collision collision = smdl::detail::BorrowModel(model).collision();
        if (out_collision)
            *out_collision = collision.handle();
    });
}

SMDL_API smdl_status smdl_collision_get_geometry(smdl_collision *collision, smdl_geometry **out_geometry) {
    if (out_geometry)
        *out_geometry = nullptr;
    return OnCollision(collision, [&](const smdl::Collision &c) {
        const smdl::Geometry geometry = c.geometry();
        if (out_geometry)
            *out_geometry = geometry.handle();
    });
}

SMDL_API smdl_status smdl_collision_set_geometry(smdl_collision *collision, smdl_geometry *geometry) {
    return OnCollision(collision, [&](const smdl::Collision &c) {
        c.setGeometry(smdl::Geometry(geometry, collision->model));
    });
}

SMDL_API smdl_status smdl_collision_set_jointed(smdl_collision *collision, int jointed) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setJointed(jointed != 0); });
}

SMDL_API smdl_status smdl_collision_set_source_name(smdl_collision *collision, const char *name) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setSourceName(name ? name : ""); });
}

SMDL_API smdl_status smdl_collision_set_convex_source_name(smdl_collision *collision, uint32_t index, const char *name) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setConvexSourceName(index, name ? name : ""); });
}

SMDL_API smdl_status smdl_collision_add_convex_source(smdl_collision *collision, smdl_geometry **out_geometry,
                                                      uint32_t *out_index) {
    if (out_geometry)
        *out_geometry = nullptr;
    return OnCollision(collision, [&](const smdl::Collision &c) {
        const smdl::Geometry geometry = c.addConvexSource(out_index);
        if (out_geometry)
            *out_geometry = geometry.handle();
    });
}

SMDL_API smdl_status smdl_collision_add_convex_source_geometry(smdl_collision *collision, smdl_geometry *geometry,
                                                               uint32_t *out_index) {
    return OnCollision(collision, [&](const smdl::Collision &c) {
        const uint32_t index = c.addConvexSource(smdl::Geometry(geometry, collision->model));
        if (out_index)
            *out_index = index;
    });
}

SMDL_API uint32_t smdl_collision_convex_source_count(const smdl_collision *collision) {
    return collision ? View(const_cast<smdl_collision *>(collision)).convexSourceCount() : 0;
}

SMDL_API smdl_status smdl_collision_set_convex_source_offset(smdl_collision *collision, uint32_t index,
                                                             const float position[3], const float angles[3],
                                                             float scale) {
    if (!collision)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position || !angles)
        return Fail(collision->model, SMDL_ERROR_INVALID_ARGUMENT,
                    "collision: convex source offset position or angles is null");
    return OnCollision(collision, [&](const smdl::Collision &c) {
        c.setConvexSourceOffset(index, {position[0], position[1], position[2]}, {angles[0], angles[1], angles[2]},
                                scale);
    });
}

SMDL_API smdl_status smdl_collision_set_convex_source_concave(smdl_collision *collision, uint32_t index, int concave) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setConvexSourceConcave(index, concave != 0); });
}

SMDL_API smdl_status smdl_collision_set_text(smdl_collision *collision, const char *text) {
    if (!collision)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!text)
        return Fail(collision->model, SMDL_ERROR_INVALID_ARGUMENT, "collision: text is null");
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setText(text); });
}

// -- whole model ------------------------------------------------------------------------------------------------

SMDL_API smdl_status smdl_collision_set_mass(smdl_collision *c, float mass) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setMass(mass); });
}
SMDL_API smdl_status smdl_collision_set_auto_mass(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setAutoMass(); });
}
SMDL_API smdl_status smdl_collision_set_inertia(smdl_collision *c, float inertia) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setInertia(inertia); });
}
SMDL_API smdl_status smdl_collision_set_damping(smdl_collision *c, float damping) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setDamping(damping); });
}
SMDL_API smdl_status smdl_collision_set_rot_damping(smdl_collision *c, float rot_damping) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setRotDamping(rot_damping); });
}
SMDL_API smdl_status smdl_collision_set_drag(smdl_collision *c, float drag) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setDrag(drag); });
}
SMDL_API smdl_status smdl_collision_set_rolling_drag(smdl_collision *c, float rolling_drag) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setRollingDrag(rolling_drag); });
}

SMDL_API smdl_status smdl_collision_set_max_convex_pieces(smdl_collision *collision, int count) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setMaxConvexPieces(count); });
}

SMDL_API smdl_status smdl_collision_set_remove_2d(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setRemove2D(); });
}
SMDL_API smdl_status smdl_collision_set_concave_per_joint(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setConcavePerJoint(); });
}
SMDL_API smdl_status smdl_collision_set_weld_position(smdl_collision *c, float epsilon) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setWeldPosition(epsilon); });
}
SMDL_API smdl_status smdl_collision_set_weld_normal(smdl_collision *c, float epsilon) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setWeldNormal(epsilon); });
}
SMDL_API smdl_status smdl_collision_set_concave(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setConcave(); });
}
SMDL_API smdl_status smdl_collision_set_convex_hull_count_override(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setConvexHullCountOverride(); });
}

SMDL_API smdl_status smdl_collision_set_mass_center(smdl_collision *collision, const float center[3]) {
    if (!collision)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!center)
        return Fail(collision->model, SMDL_ERROR_INVALID_ARGUMENT, "collision: mass center is null or not finite");
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setMassCenter({center[0], center[1], center[2]}); });
}

SMDL_API smdl_status smdl_collision_set_no_self_collisions(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setNoSelfCollisions(); });
}
SMDL_API smdl_status smdl_collision_set_assume_world_space(smdl_collision *c) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.setAssumeWorldSpace(); });
}

SMDL_API smdl_status smdl_collision_set_root_bone(smdl_collision *collision, smdl_bone_id bone) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.setRootBone(bone); });
}

SMDL_API smdl_status smdl_collision_set_animated_friction(smdl_collision *collision, int min_friction, int max_friction,
                                                          float time_in, float time_hold, float time_out) {
    return OnCollision(collision, [&](const smdl::Collision &c) {
        c.setAnimatedFriction(min_friction, max_friction, time_in, time_hold, time_out);
    });
}

// -- joints -----------------------------------------------------------------------------------------------------

SMDL_API smdl_status smdl_collision_joint_skip(smdl_collision *collision, smdl_bone_id joint) {
    return OnCollision(collision, [&](const smdl::Collision &c) { c.jointSkip(joint); });
}

SMDL_API smdl_status smdl_collision_joint_merge(smdl_collision *c, smdl_bone_id parent, smdl_bone_id child) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointMerge(parent, child); });
}

SMDL_API smdl_status smdl_collision_joint_constrain(smdl_collision *collision, smdl_bone_id joint,
                                                    smdl_collision_axis axis, smdl_collision_joint_type type,
                                                    float min, float max, float friction) {
    return OnCollision(collision, [&](const smdl::Collision &c) {
        c.jointConstrain(joint, static_cast<smdl::CollisionAxis>(axis), static_cast<smdl::JointType>(type), min, max,
                         friction);
    });
}

SMDL_API smdl_status smdl_collision_joint_set_inertia(smdl_collision *c, smdl_bone_id joint, float inertia) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointSetInertia(joint, inertia); });
}
SMDL_API smdl_status smdl_collision_joint_set_damping(smdl_collision *c, smdl_bone_id joint, float damping) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointSetDamping(joint, damping); });
}
SMDL_API smdl_status smdl_collision_joint_set_rot_damping(smdl_collision *c, smdl_bone_id joint, float rot_damping) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointSetRotDamping(joint, rot_damping); });
}
SMDL_API smdl_status smdl_collision_joint_set_mass_bias(smdl_collision *c, smdl_bone_id joint, float mass_bias) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointSetMassBias(joint, mass_bias); });
}

SMDL_API smdl_status smdl_collision_joint_collide(smdl_collision *c, smdl_bone_id a, smdl_bone_id b) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointCollide(a, b); });
}

SMDL_API smdl_status smdl_collision_joint_collide_all(smdl_collision *collision, const smdl_bone_id *joints,
                                                      size_t count) {
    if (!collision)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!joints && count)
        return Fail(collision->model, SMDL_ERROR_INVALID_ARGUMENT, "collision: joint list is null");
    return OnCollision(collision, [&](const smdl::Collision &c) {
        c.jointCollideAll(std::span<const smdl::BoneId>(joints, count));
    });
}

SMDL_API smdl_status smdl_collision_joint_no_collide(smdl_collision *c, smdl_bone_id a, smdl_bone_id b) {
    return OnCollision(c, [&](const smdl::Collision &v) { v.jointNoCollide(a, b); });
}

} // extern "C"

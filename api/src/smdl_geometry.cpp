// smdl::Geometry (cpp/geometry.hpp): geometry data of a mesh. The setters copy and cross-check; BuildSourceFromGeometry
// (apply_geometry.cpp) turns the data into a source at compile. The C API (smdl_geometry.h) wraps the C++ methods.
#include "studiomdl/c/smdl_geometry.h"
#include "studiomdl/studiomdl.hpp"

#include <optional>

#include "core_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    struct Problem {
        smdl_status status;
        std::string message;
    };

    // What can be decided about the data set so far (arrays that are not set yet are not checked)
    std::optional<Problem> Inconsistency(const smdl_geometry &g) {
        const size_t vertexCount = g.positions.size();
        auto length = [](std::string what, size_t size, size_t expected) -> std::optional<Problem> {
            if (size == expected)
                return std::nullopt;
            return Problem{SMDL_ERROR_INVALID_ARGUMENT, std::format("{} has {} entries, expected {}", what, size, expected)};
        };
        if (vertexCount) {
            if (!g.normals.empty())
                if (auto p = length("normals", g.normals.size(), vertexCount))
                    return p;
            if (!g.tangents.empty())
                if (auto p = length("tangents", g.tangents.size(), vertexCount))
                    return p;
            for (size_t set = 0; set < kMaxUvSets; ++set) {
                if (!g.uvs[set].empty())
                    if (auto p = length(std::format("UV set {}", set), g.uvs[set].size(), vertexCount))
                        return p;
            }
            if (g.influencesPerVertex)
                if (auto p = length("weights", g.weightBones.size() / g.influencesPerVertex, vertexCount))
                    return p;
            for (uint32_t index: g.indices) {
                if (index >= vertexCount)
                    return Problem{SMDL_ERROR_OUT_OF_RANGE,
                                   std::format("triangle index {} is out of range ({} vertices)", index, vertexCount)};
            }
            for (uint32_t index: g.quadIndices) {
                if (index >= vertexCount)
                    return Problem{SMDL_ERROR_OUT_OF_RANGE,
                                   std::format("quad index {} is out of range ({} vertices)", index, vertexCount)};
            }
        }
        if (!g.triangleMaterials.empty())
            if (auto p = length("triangle materials", g.triangleMaterials.size(), g.indices.size() / 3))
                return p;
        if (!g.quadMaterials.empty())
            if (auto p = length("quad materials", g.quadMaterials.size(), g.quadIndices.size() / 4))
                return p;
        if (!g.indices.empty()) {
            if (!g.cornerNormals.empty())
                if (auto p = length("corner normals", g.cornerNormals.size(), g.indices.size()))
                    return p;
            for (size_t set = 0; set < kMaxUvSets; ++set) {
                if (!g.cornerUvs[set].empty())
                    if (auto p = length(std::format("corner UV set {}", set), g.cornerUvs[set].size(), g.indices.size()))
                        return p;
            }
        }
        return std::nullopt;
    }

    // Swaps `value` into `member`, undoes it when the geometry becomes inconsistent
    template<class T>
    smdl_status TrySet(smdl_geometry &g, std::string_view what, T &member, T &&value) {
        std::swap(member, value);
        if (auto problem = Inconsistency(g)) {
            std::swap(member, value);
            return Fail(g.model, problem->status, "{}: {}: {}", g.owner, what, problem->message);
        }
        return Ok(g.model);
    }

    bool ReadVectors(smdl_geometry &g, const float *data, size_t count, size_t stride, std::string_view what,
                     std::vector<Vector> &out, smdl_status &status) {
        std::vector<float> floats;
        if (!ReadFloats(data, count, stride, 3, floats, g.model, std::format("{}: {}", g.owner, what), status))
            return false;
        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
            out.emplace_back(floats[i * 3], floats[i * 3 + 1], floats[i * 3 + 2]);
        return true;
    }

    bool ReadUvs(smdl_geometry &g, const float *data, size_t count, size_t stride, std::string_view what,
                 std::vector<Vector2D> &out, smdl_status &status) {
        std::vector<float> floats;
        if (!ReadFloats(data, count, stride, 2, floats, g.model, std::format("{}: {}", g.owner, what), status))
            return false;
        out.reserve(count);
        for (size_t i = 0; i < count; ++i)
            out.emplace_back(floats[i * 2], floats[i * 2 + 1]);
        return true;
    }

    // Shared by triangles and quads
    smdl_status SetPolygons(smdl_geometry *g, const uint32_t *indices, size_t count, size_t stride, size_t corners,
                            const smdl_material_id *materials, size_t materialsStride, bool quads) {
        const char *what = quads ? "quads" : "triangles";
        std::vector<uint32_t> newIndices;
        std::vector<smdl_material_id> newMaterials;
        smdl_status status = SMDL_OK;
        if (!ReadUints(indices, count, stride, corners, newIndices, g->model, std::format("{}: {}", g->owner, what),
                       status))
            return status;
        if (materials) {
            if (!ReadUints(materials, count, materialsStride, 1, newMaterials, g->model,
                           std::format("{}: {} materials", g->owner, what), status))
                return status;
            for (smdl_material_id id: newMaterials) {
                if (id >= g->materials.size())
                    return Fail(g->model, SMDL_ERROR_OUT_OF_RANGE, "{}: {}: material id {} is out of range ({} materials)",
                                g->owner, what, id, g->materials.size());
            }
        }
        else if (count && g->materials.empty()) {
            return Fail(g->model, SMDL_ERROR_OUT_OF_RANGE,
                        "{}: {}: no material ids given and the geometry has no material (smdl_geometry_add_material first)",
                        g->owner, what);
        }

        // both arrays are replaced together: a failure of the second check leaves both unchanged
        auto &memberIndices = quads ? g->quadIndices : g->indices;
        auto &memberMaterials = quads ? g->quadMaterials : g->triangleMaterials;
        std::swap(memberIndices, newIndices);
        std::swap(memberMaterials, newMaterials);
        if (auto problem = Inconsistency(*g)) {
            std::swap(memberIndices, newIndices);
            std::swap(memberMaterials, newMaterials);
            return Fail(g->model, problem->status, "{}: {}: {}", g->owner, what, problem->message);
        }
        return Ok(g->model);
    }

    smdl_status SetRestPose(smdl_geometry *g, smdl_bone_id bone, const float position[3], const float *rotation,
                            bool quaternion) {
        if (!position || !rotation)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: rest pose position or rotation is null", g->owner);
        if (bone >= g->model->skeleton.bones.size())
            return Fail(g->model, SMDL_ERROR_OUT_OF_RANGE, "{}: rest pose: bone {} is out of range ({} bones)",
                        g->owner, bone, g->model->skeleton.bones.size());
        if (!AllFinite(position, 3) || !AllFinite(rotation, quaternion ? 4 : 3))
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: rest pose of bone {} is not finite", g->owner, bone);
        RestPoseOverride pose{bone, ToVector(position), RadianEuler(0, 0, 0)};
        if (quaternion) {
            if (!QuaternionToRadianEuler(rotation, pose.position, pose.rotation))
                return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT,
                            "{}: rest pose of bone {}: the quaternion has zero length", g->owner, bone);
        }
        else {
            pose.rotation = RadianEuler(rotation[0], rotation[1], rotation[2]);
        }
        for (RestPoseOverride &existing: g->restOverrides) {
            if (existing.bone == bone) {
                existing = pose;
                return Ok(g->model);
            }
        }
        g->restOverrides.push_back(pose);
        return Ok(g->model);
    }
}

namespace smdl {

void Geometry::setPositions(const float *xyz, size_t count, size_t stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        if (count > MAXSTUDIOSRCVERTS)
            return Fail(g->model, SMDL_ERROR_LIMIT, "{}: {} vertices (max {})", g->owner, count, MAXSTUDIOSRCVERTS);
        std::vector<Vector> data;
        smdl_status status = SMDL_OK;
        if (!ReadVectors(*g, xyz, count, stride, "positions", data, status))
            return status;
        return TrySet(*g, "positions", g->positions, std::move(data));
    });
}

void Geometry::setNormals(const float *xyz, size_t count, size_t stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        std::vector<Vector> data;
        smdl_status status = SMDL_OK;
        if (!ReadVectors(*g, xyz, count, stride, "normals", data, status))
            return status;
        return TrySet(*g, "normals", g->normals, std::move(data));
    });
}

void Geometry::setTangents(const float *xyzw, size_t count, size_t stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        std::vector<float> floats;
        smdl_status status = SMDL_OK;
        if (!ReadFloats(xyzw, count, stride, 4, floats, g->model, std::format("{}: tangents", g->owner), status))
            return status;
        std::vector<Vector4D> data;
        data.reserve(count);
        for (size_t i = 0; i < count; ++i)
            data.emplace_back(floats[i * 4], floats[i * 4 + 1], floats[i * 4 + 2], floats[i * 4 + 3]);
        return TrySet(*g, "tangents", g->tangents, std::move(data));
    });
}

void Geometry::setUVs(uint32_t set, const float *uv, size_t count, size_t stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        if (set >= kMaxUvSets)
            return Fail(g->model, SMDL_ERROR_OUT_OF_RANGE, "{}: UV set {} is out of range (max {})", g->owner, set,
                        kMaxUvSets - 1);
        std::vector<Vector2D> data;
        smdl_status status = SMDL_OK;
        if (!ReadUvs(*g, uv, count, stride, std::format("UV set {}", set), data, status))
            return status;
        return TrySet(*g, std::format("UV set {}", set), g->uvs[set], std::move(data));
    });
}

void Geometry::setWeights(uint32_t influences_per_vertex, const BoneId *bones, size_t bones_stride,
                          const float *weights, size_t weights_stride, size_t vertex_count) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        if (influences_per_vertex == 0 || influences_per_vertex > MAXSTUDIOSRCBONES)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: weights: influences per vertex must be 1..{}, got {}",
                        g->owner, MAXSTUDIOSRCBONES, influences_per_vertex);
        std::vector<uint32_t> newBones;
        std::vector<float> newWeights;
        smdl_status status = SMDL_OK;
        if (!ReadUints(bones, vertex_count, bones_stride, influences_per_vertex, newBones, g->model,
                       std::format("{}: weight bones", g->owner), status))
            return status;
        if (!ReadFloats(weights, vertex_count, weights_stride, influences_per_vertex, newWeights, g->model,
                        std::format("{}: weights", g->owner), status))
            return status;
        const size_t boneCount = g->model->skeleton.bones.size();
        for (size_t i = 0; i < newBones.size(); ++i) {
            if (newBones[i] >= boneCount)
                return Fail(g->model, SMDL_ERROR_OUT_OF_RANGE,
                            "{}: weights: vertex {} influence {}: bone {} is out of range ({} bones)", g->owner,
                            i / influences_per_vertex, i % influences_per_vertex, newBones[i], boneCount);
            if (newWeights[i] < 0.0f)
                return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: weights: vertex {} influence {}: negative weight",
                            g->owner, i / influences_per_vertex, i % influences_per_vertex);
        }
        std::swap(g->influencesPerVertex, influences_per_vertex);
        std::swap(g->weightBones, newBones);
        std::swap(g->weights, newWeights);
        if (auto problem = Inconsistency(*g)) {
            std::swap(g->influencesPerVertex, influences_per_vertex);
            std::swap(g->weightBones, newBones);
            std::swap(g->weights, newWeights);
            return Fail(g->model, problem->status, "{}: weights: {}", g->owner, problem->message);
        }
        return Ok(g->model);
    });
}

MaterialId Geometry::addMaterial(std::string_view name) {
    smdl_geometry *g = handle();
    MaterialId result = NoId;
    RunBody(g->model, [&] {
        if (name.empty())
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: material name is null or empty", g->owner);
        if (name.size() >= MAXSTUDIONAME)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: material name '{}' is too long (max {} bytes)",
                        g->owner, name, MAXSTUDIONAME - 1);
        for (const std::string &existing: g->materials) {
            if (IEquals(existing, name))
                return Fail(g->model, SMDL_ERROR_DUPLICATE_NAME, "{}: material '{}' already exists", g->owner, name);
        }
        g->materials.emplace_back(name);
        result = static_cast<MaterialId>(g->materials.size() - 1);
        return Ok(g->model);
    });
    return result;
}

void Geometry::setTriangles(const uint32_t *indices, size_t triangle_count, size_t stride, const MaterialId *materials,
                            size_t materials_stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] { return SetPolygons(g, indices, triangle_count, stride, 3, materials, materials_stride, false); });
}

void Geometry::setQuads(const uint32_t *indices, size_t quad_count, size_t stride, const MaterialId *materials,
                        size_t materials_stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] { return SetPolygons(g, indices, quad_count, stride, 4, materials, materials_stride, true); });
}

void Geometry::setCornerNormals(const float *xyz, size_t corner_count, size_t stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        std::vector<Vector> data;
        smdl_status status = SMDL_OK;
        if (!ReadVectors(*g, xyz, corner_count, stride, "corner normals", data, status))
            return status;
        return TrySet(*g, "corner normals", g->cornerNormals, std::move(data));
    });
}

void Geometry::setCornerUVs(uint32_t set, const float *uv, size_t corner_count, size_t stride) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        if (set >= kMaxUvSets)
            return Fail(g->model, SMDL_ERROR_OUT_OF_RANGE, "{}: UV set {} is out of range (max {})", g->owner, set,
                        kMaxUvSets - 1);
        std::vector<Vector2D> data;
        smdl_status status = SMDL_OK;
        if (!ReadUvs(*g, uv, corner_count, stride, std::format("corner UV set {}", set), data, status))
            return status;
        return TrySet(*g, std::format("corner UV set {}", set), g->cornerUvs[set], std::move(data));
    });
}

void Geometry::setRestPose(BoneId bone, const Vec3 &position, const Quat &rotation) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] { return SetRestPose(g, bone, position.data(), rotation.data(), true); });
}

void Geometry::setRestPoseEuler(BoneId bone, const Vec3 &position, const Vec3 &euler) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] { return SetRestPose(g, bone, position.data(), euler.data(), false); });
}

void Geometry::setFlipTriangles(bool flip) {
    smdl_geometry *g = handle();
    g->flipTriangles = flip;
}

void Geometry::setScale(float scale) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        if (!std::isfinite(scale) || scale < 0.0f)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: scale must be finite and >= 0 (0 = model scale)",
                        g->owner);
        if (g->appended)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT,
                        "{}: an appended geometry has no scale of its own, use setAppendOffset", g->owner);
        g->scale = scale;
        return Ok(g->model);
    });
}

void Geometry::setAppendOffset(const Vec3 &position, const Vec3 &anglesDegrees, float scale) {
    smdl_geometry *g = handle();
    RunBody(g->model, [&] {
        if (!g->appended)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT,
                        "{}: only geometry created by Mesh::appendGeometry has an append offset", g->owner);
        if (!AllFinite(position.data(), 3) || !AllFinite(anglesDegrees.data(), 3) || !std::isfinite(scale) || scale == 0.0f)
            return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: append offset needs finite values and a non-zero scale",
                        g->owner);
        g->hasAppendOffset = true;
        g->appendPosition = ToVector(position.data());
        g->appendAngles = ToVector(anglesDegrees.data());
        g->appendScale = scale;
        return Ok(g->model);
    });
}

uint32_t Geometry::vertexCount() const { return static_cast<uint32_t>(handle()->positions.size()); }
uint32_t Geometry::triangleCount() const { return static_cast<uint32_t>(handle()->indices.size() / 3); }
uint32_t Geometry::quadCount() const { return static_cast<uint32_t>(handle()->quadIndices.size() / 4); }
uint32_t Geometry::materialCount() const { return static_cast<uint32_t>(handle()->materials.size()); }

} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
namespace {
    smdl::Geometry View(smdl_geometry *g) { return smdl::Geometry(g, g->model); }
}

extern "C" {

SMDL_API smdl_status smdl_geometry_set_positions(smdl_geometry *g, const float *xyz, size_t count, size_t stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setPositions(xyz, count, stride); });
}

SMDL_API smdl_status smdl_geometry_set_normals(smdl_geometry *g, const float *xyz, size_t count, size_t stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setNormals(xyz, count, stride); });
}

SMDL_API smdl_status smdl_geometry_set_tangents(smdl_geometry *g, const float *xyzw, size_t count, size_t stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setTangents(xyzw, count, stride); });
}

SMDL_API smdl_status smdl_geometry_set_uvs(smdl_geometry *g, uint32_t set, const float *uv, size_t count,
                                           size_t stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setUVs(set, uv, count, stride); });
}

SMDL_API smdl_status smdl_geometry_set_weights(smdl_geometry *g, uint32_t influences_per_vertex,
                                               const smdl_bone_id *bones, size_t bones_stride, const float *weights,
                                               size_t weights_stride, size_t vertex_count) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] {
        View(g).setWeights(influences_per_vertex, bones, bones_stride, weights, weights_stride, vertex_count);
    });
}

SMDL_API smdl_status smdl_geometry_add_material(smdl_geometry *g, const char *name, smdl_material_id *out_id) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (out_id)
        *out_id = SMDL_NO_ID;
    return CallCpp(g->model, [&] {
        const smdl_material_id id = View(g).addMaterial(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_geometry_set_triangles(smdl_geometry *g, const uint32_t *indices, size_t triangle_count,
                                                 size_t stride, const smdl_material_id *materials,
                                                 size_t materials_stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setTriangles(indices, triangle_count, stride, materials, materials_stride); });
}

SMDL_API smdl_status smdl_geometry_set_quads(smdl_geometry *g, const uint32_t *indices, size_t quad_count,
                                             size_t stride, const smdl_material_id *materials,
                                             size_t materials_stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setQuads(indices, quad_count, stride, materials, materials_stride); });
}

SMDL_API smdl_status smdl_geometry_set_corner_normals(smdl_geometry *g, const float *xyz, size_t corner_count,
                                                      size_t stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setCornerNormals(xyz, corner_count, stride); });
}

SMDL_API smdl_status smdl_geometry_set_corner_uvs(smdl_geometry *g, uint32_t set, const float *uv,
                                                  size_t corner_count, size_t stride) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setCornerUVs(set, uv, corner_count, stride); });
}

SMDL_API smdl_status smdl_geometry_set_rest_pose(smdl_geometry *g, smdl_bone_id bone, const float position[3],
                                                 const float rotation[4]) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position || !rotation)
        return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: rest pose position or rotation is null", g->owner);
    return CallCpp(g->model, [&] {
        View(g).setRestPose(bone, {position[0], position[1], position[2]},
                            {rotation[0], rotation[1], rotation[2], rotation[3]});
    });
}

SMDL_API smdl_status smdl_geometry_set_rest_pose_euler(smdl_geometry *g, smdl_bone_id bone, const float position[3],
                                                       const float euler[3]) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position || !euler)
        return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: rest pose position or rotation is null", g->owner);
    return CallCpp(g->model, [&] {
        View(g).setRestPoseEuler(bone, {position[0], position[1], position[2]}, {euler[0], euler[1], euler[2]});
    });
}

SMDL_API smdl_status smdl_geometry_set_flip_triangles(smdl_geometry *g, int flip) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setFlipTriangles(flip != 0); });
}

SMDL_API smdl_status smdl_geometry_set_scale(smdl_geometry *g, float scale) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(g->model, [&] { View(g).setScale(scale); });
}

SMDL_API smdl_status smdl_geometry_set_append_offset(smdl_geometry *g, const float position[3], const float angles[3],
                                                     float scale) {
    if (!g)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position || !angles)
        return Fail(g->model, SMDL_ERROR_INVALID_ARGUMENT, "{}: append offset needs finite values and a non-zero scale",
                    g->owner);
    return CallCpp(g->model, [&] {
        View(g).setAppendOffset({position[0], position[1], position[2]}, {angles[0], angles[1], angles[2]}, scale);
    });
}

SMDL_API uint32_t smdl_geometry_vertex_count(const smdl_geometry *g) {
    return g ? View(const_cast<smdl_geometry *>(g)).vertexCount() : 0;
}

SMDL_API uint32_t smdl_geometry_triangle_count(const smdl_geometry *g) {
    return g ? View(const_cast<smdl_geometry *>(g)).triangleCount() : 0;
}

SMDL_API uint32_t smdl_geometry_quad_count(const smdl_geometry *g) {
    return g ? View(const_cast<smdl_geometry *>(g)).quadCount() : 0;
}

SMDL_API uint32_t smdl_geometry_material_count(const smdl_geometry *g) {
    return g ? View(const_cast<smdl_geometry *>(g)).materialCount() : 0;
}

} // extern "C"

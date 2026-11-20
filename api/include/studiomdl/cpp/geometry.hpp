// studiomdl C++ API: geometry (implemented in api/src/smdl_geometry.cpp; smdl_geometry.h is the C form)
#pragma once

#include "common.hpp"
#include "shapes.hpp"
#include "../c/smdl_geometry.h"

// Defines smdl::Geometry : public detail::GeometryShapesApi (shapes.hpp) with handle() and model().
namespace smdl {

// A non-owning view of a geometry (the source data of one mesh); valid as long as its model lives. See
// smdl_geometry.h for the conventions (SMD winding and UVs, required normals, array cross-checks).
class Geometry : public detail::GeometryShapesApi {
public:
    Geometry(smdl_geometry *geometry, smdl_model *model) : m_Geometry(geometry), m_Model(model) {}

    smdl_geometry *handle() const { return m_Geometry; }
    smdl_model *model() const { return m_Model; }

    // Vertex data: spans of Vec3/Vec2/Vec4, or pointer + count + stride in bytes (0 = packed) like the C API.
    void setPositions(std::span<const Vec3> xyz) { setPositions(floats(xyz.data()), xyz.size(), 0); }
    void setPositions(const float *xyz, size_t count, size_t strideBytes = 0);

    void setNormals(std::span<const Vec3> xyz) { setNormals(floats(xyz.data()), xyz.size(), 0); }
    void setNormals(const float *xyz, size_t count, size_t strideBytes = 0);

    // x, y, z, w (w = handedness)
    void setTangents(std::span<const Vec4> xyzw) { setTangents(floats(xyzw.data()), xyzw.size(), 0); }
    void setTangents(const float *xyzw, size_t count, size_t strideBytes = 0);

    void setUVs(uint32_t set, std::span<const Vec2> uv) { setUVs(set, floats(uv.data()), uv.size(), 0); }
    void setUVs(uint32_t set, const float *uv, size_t count, size_t strideBytes = 0);

    // bones/weights: influencesPerVertex (1..MaxInfluences) entries per vertex, rows of the same length (padding
    // weight 0).
    void setWeights(uint32_t influencesPerVertex, std::span<const BoneId> bones, std::span<const float> weights) {
        if (influencesPerVertex == 0 || bones.size() % influencesPerVertex != 0 || bones.size() != weights.size())
            throw Error(SMDL_ERROR_INVALID_ARGUMENT, "setWeights: bones and weights must hold influencesPerVertex entries per vertex");
        setWeights(influencesPerVertex, bones.data(), 0, weights.data(), 0, bones.size() / influencesPerVertex);
    }
    void setWeights(uint32_t influencesPerVertex, const BoneId *bones, size_t bonesStrideBytes, const float *weights,
                    size_t weightsStrideBytes, size_t vertexCount);

    // Material names are unique in a geometry; ids are 0, 1, 2...
    MaterialId addMaterial(std::string_view name);

    // indices: 3 per triangle; materials: one id per triangle, or empty = material 0 for every triangle
    void setTriangles(std::span<const uint32_t> indices, std::span<const MaterialId> materials = {}) {
        const size_t count = polygonCount(indices.size(), 3, materials.size(), "setTriangles");
        setTriangles(indices.data(), count, 0, materials.empty() ? nullptr : materials.data(), 0);
    }
    void setTriangles(const uint32_t *indices, size_t triangleCount, size_t strideBytes, const MaterialId *materials,
                      size_t materialsStrideBytes);

    // $subd models: 4 indices per quad
    void setQuads(std::span<const uint32_t> indices, std::span<const MaterialId> materials = {}) {
        const size_t count = polygonCount(indices.size(), 4, materials.size(), "setQuads");
        setQuads(indices.data(), count, 0, materials.empty() ? nullptr : materials.data(), 0);
    }
    void setQuads(const uint32_t *indices, size_t quadCount, size_t strideBytes, const MaterialId *materials,
                  size_t materialsStrideBytes);

    // Per triangle corner (3 per triangle, in triangle order); they replace the indexed normals/UVs
    void setCornerNormals(std::span<const Vec3> xyz) { setCornerNormals(floats(xyz.data()), xyz.size(), 0); }
    void setCornerNormals(const float *xyz, size_t cornerCount, size_t strideBytes = 0);

    void setCornerUVs(uint32_t set, std::span<const Vec2> uv) { setCornerUVs(set, floats(uv.data()), uv.size(), 0); }
    void setCornerUVs(uint32_t set, const float *uv, size_t cornerCount, size_t strideBytes = 0);

    // Rest pose of a bone as this geometry sees it (replaces the model skeleton's for this geometry)
    void setRestPose(BoneId bone, const Vec3 &position, const Quat &rotation);
    void setRestPoseEuler(BoneId bone, const Vec3 &position, const Vec3 &euler);

    void setFlipTriangles(bool flip);

    // Material names relative to materials/ like those of a DMX file (see smdl_geometry.h)
    void setRelativeMaterials(bool relative);

    // File name of the source built from this geometry, for messages and the log (see smdl_geometry.h)
    void setSourceName(std::string_view name);

    // 0 = the model scale
    void setScale(float scale);

    // Only for geometry created by Mesh::appendGeometry ($appendsource offset): angles in degrees (pitch, yaw, roll)
    void setAppendOffset(const Vec3 &position, const Vec3 &anglesDegrees, float scale = 1.0f);

    uint32_t vertexCount() const;
    uint32_t triangleCount() const;
    uint32_t quadCount() const;
    uint32_t materialCount() const;

private:
    template<class T>
    static const float *floats(const T *data) { return reinterpret_cast<const float *>(data); }

    static size_t polygonCount(size_t indexCount, size_t corners, size_t materialCount, const char *what) {
        if (indexCount % corners != 0)
            throw Error(SMDL_ERROR_INVALID_ARGUMENT, std::string(what) + ": the index count is not a multiple of " + std::to_string(corners));
        if (materialCount != 0 && materialCount != indexCount / corners)
            throw Error(SMDL_ERROR_INVALID_ARGUMENT, std::string(what) + ": needs one material id per polygon (or none)");
        return indexCount / corners;
    }

    smdl_geometry *m_Geometry;
    smdl_model *m_Model;
};

} // namespace smdl

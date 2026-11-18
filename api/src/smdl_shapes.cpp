// smdl_shapes.h: shapes of a geometry (store and validate; BuildShapeAnimations / the flex apply code use them)
#include "studiomdl/c/smdl_shapes.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>

#include "api_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    // frame 0 is the base pose
    constexpr size_t kMaxShapes = MAXSTUDIOANIMFRAMES - 1;

    GeometryShapeDesc *FindShape(smdl_geometry &geometry, uint32_t shape, smdl_status &status) {
        if (shape >= geometry.shapes.size()) {
            status = Fail(geometry.model, SMDL_ERROR_OUT_OF_RANGE, "{}: shape {} does not exist ({} shapes)",
                          geometry.owner, shape, geometry.shapes.size());
            return nullptr;
        }
        return &geometry.shapes[shape];
    }
}

namespace smdl {

namespace detail {

ShapeId GeometryShapesApi::addShape(std::string_view nameView, const uint32_t *vertices, size_t count,
                                    size_t vertices_stride_bytes, const float *position_deltas,
                                    size_t position_deltas_stride_bytes) const {
    smdl_geometry *geometry = handle();
    smdl_model *model = geometry->model;
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_shape = &result;
    RunBody(model, [&] {
        const std::string &who = geometry->owner;
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: shape name is null or empty", who);
        if (geometry->positions.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "{}: shape '{}': set the positions first (smdl_geometry_set_positions)", who, name);
        for (const auto &shape: geometry->shapes) {
            if (shape.name == name)
                return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "{}: shape '{}' exists already", who, name);
        }
        if (geometry->shapes.size() >= kMaxShapes)
            return Fail(model, SMDL_ERROR_LIMIT, "{}: too many shapes (at most {})", who, kMaxShapes);

        smdl_status status = SMDL_OK;
        std::vector<uint32_t> indices;
        if (!ReadUints(vertices, count, vertices_stride_bytes, 1, indices, model, "shape vertices", status))
            return status;
        std::vector<float> deltas;
        if (!ReadFloats(position_deltas, count, position_deltas_stride_bytes, 3, deltas, model, "shape position deltas",
                        status))
            return status;

        for (size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= geometry->positions.size())
                return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "{}: shape '{}': vertex index {} (entry {}) is not a vertex ({} vertices)",
                            who, name, indices[i], i, geometry->positions.size());
        }
        std::vector<uint32_t> sorted = indices;
        std::ranges::sort(sorted);
        if (const auto duplicate = std::ranges::adjacent_find(sorted); duplicate != sorted.end())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: shape '{}': vertex {} is listed twice", who, name,
                        *duplicate);

        GeometryShapeDesc &shape = geometry->shapes.emplace_back();
        shape.name = name;
        shape.vertices = std::move(indices);
        shape.positionDeltas.reserve(shape.vertices.size());
        for (size_t i = 0; i < shape.vertices.size(); ++i)
            shape.positionDeltas.emplace_back(deltas[i * 3], deltas[i * 3 + 1], deltas[i * 3 + 2]);
        if (out_shape)
            *out_shape = static_cast<uint32_t>(geometry->shapes.size() - 1);
        return Ok(model);
    });
    return result;
}

void GeometryShapesApi::setShapeNormalDeltas(ShapeId shape, const float *xyz, size_t count, size_t stride_bytes) const {
    smdl_geometry *geometry = handle();
    smdl_model *model = geometry->model;
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        GeometryShapeDesc *desc = FindShape(*geometry, shape, status);
        if (!desc)
            return status;
        if (count != desc->vertices.size())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: shape '{}': {} normal deltas for {} vertices",
                        geometry->owner, desc->name, count, desc->vertices.size());
        std::vector<float> values;
        if (!ReadFloats(xyz, count, stride_bytes, 3, values, model, "shape normal deltas", status))
            return status;
        desc->normalDeltas.clear();
        for (size_t i = 0; i < count; ++i)
            desc->normalDeltas.emplace_back(values[i * 3], values[i * 3 + 1], values[i * 3 + 2]);
        return Ok(model);
    });
}

void GeometryShapesApi::setShapeWrinkle(ShapeId shape, const float *values, size_t count, size_t stride_bytes) const {
    smdl_geometry *geometry = handle();
    smdl_model *model = geometry->model;
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        GeometryShapeDesc *desc = FindShape(*geometry, shape, status);
        if (!desc)
            return status;
        if (count != desc->vertices.size())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: shape '{}': {} wrinkle values for {} vertices",
                        geometry->owner, desc->name, count, desc->vertices.size());
        std::vector<float> read;
        if (!ReadFloats(values, count, stride_bytes, 1, read, model, "shape wrinkle values", status))
            return status;
        desc->wrinkle = std::move(read);
        return Ok(model);
    });
}

uint32_t GeometryShapesApi::shapeCount() const { return static_cast<uint32_t>(handle()->shapes.size()); }

ShapeId GeometryShapesApi::findShape(std::string_view nameView) const {
    smdl_geometry *geometry = handle();
    smdl_model *model = geometry->model;
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_shape = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: shape name is null or empty", geometry->owner);
        for (size_t i = 0; i < geometry->shapes.size(); ++i) {
            if (geometry->shapes[i].name == name) {
                if (out_shape)
                    *out_shape = static_cast<uint32_t>(i);
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "{}: no shape named '{}'", geometry->owner, name);
    });
    return result;
}

} // namespace detail

} // namespace smdl

// The C API: null checks and out-parameters, then the C++ method
namespace {
    smdl::Geometry View(smdl_geometry *g) { return smdl::Geometry(g, g->model); }
}

extern "C" {

SMDL_API smdl_status smdl_geometry_add_shape(smdl_geometry *geometry, const char *name, const uint32_t *vertices,
                                             size_t count, size_t vertices_stride_bytes,
                                             const float *position_deltas, size_t position_deltas_stride_bytes,
                                             uint32_t *out_shape) {
    if (out_shape)
        *out_shape = SMDL_NO_ID;
    if (!geometry)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(geometry->model, [&] {
        const uint32_t shape = View(geometry).addShape(name ? name : "", vertices, count, vertices_stride_bytes,
                                                       position_deltas, position_deltas_stride_bytes);
        if (out_shape)
            *out_shape = shape;
    });
}

SMDL_API smdl_status smdl_geometry_set_shape_normal_deltas(smdl_geometry *geometry, uint32_t shape, const float *xyz,
                                                           size_t count, size_t stride_bytes) {
    if (!geometry)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(geometry->model, [&] { View(geometry).setShapeNormalDeltas(shape, xyz, count, stride_bytes); });
}

SMDL_API smdl_status smdl_geometry_set_shape_wrinkle(smdl_geometry *geometry, uint32_t shape, const float *values,
                                                     size_t count, size_t stride_bytes) {
    if (!geometry)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(geometry->model, [&] { View(geometry).setShapeWrinkle(shape, values, count, stride_bytes); });
}

SMDL_API uint32_t smdl_geometry_shape_count(const smdl_geometry *geometry) {
    return geometry ? static_cast<uint32_t>(geometry->shapes.size()) : 0;
}

SMDL_API smdl_status smdl_geometry_find_shape(const smdl_geometry *geometry, const char *name, uint32_t *out_shape) {
    if (out_shape)
        *out_shape = SMDL_NO_ID;
    if (!geometry)
        return SMDL_ERROR_INVALID_ARGUMENT;
    smdl_geometry *g = const_cast<smdl_geometry *>(geometry);
    return CallCpp(g->model, [&] {
        const uint32_t shape = View(g).findShape(name ? name : "");
        if (out_shape)
            *out_shape = shape;
    });
}

} // extern "C"

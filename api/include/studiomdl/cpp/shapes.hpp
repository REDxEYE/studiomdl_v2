// studiomdl C++ wrapper: smdl_shapes.h
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "common.hpp"
#include "../c/smdl_shapes.h"

// GeometryShapesApi is a base of smdl::Geometry (geometry.hpp): handle() is its geometry, ownerModel() its model.
namespace smdl {

// Index of a shape of a geometry (the order of addShape calls). NoId is the base shape where a call accepts it.
using ShapeId = uint32_t;

namespace detail {
        class GeometryShapesApi {
    public:
        // A shape: sparse vertex indices with one position delta each (see smdl_shapes.h)
        ShapeId addShape(std::string_view name, std::span<const uint32_t> vertices, std::span<const Vec3> positionDeltas) const {
            if (vertices.size() != positionDeltas.size())
                throw Error(SMDL_ERROR_INVALID_ARGUMENT, "addShape: " + std::to_string(vertices.size()) + " vertices but " +
                                                             std::to_string(positionDeltas.size()) + " position deltas");
            return addShape(name, vertices.data(), vertices.size(), 0,
                            positionDeltas.empty() ? nullptr : positionDeltas.front().data(), stride_of<Vec3>());
        }

        // Pointer + count + stride in bytes (0 = packed) like the C API
        ShapeId addShape(std::string_view name, const uint32_t *vertices, size_t count, size_t verticesStride,
                         const float *positionDeltas, size_t positionDeltasStride) const;

        void setShapeNormalDeltas(ShapeId shape, std::span<const Vec3> normalDeltas) const {
            setShapeNormalDeltas(shape, normalDeltas.empty() ? nullptr : normalDeltas.front().data(), normalDeltas.size(),
                                 stride_of<Vec3>());
        }

        void setShapeNormalDeltas(ShapeId shape, const float *xyz, size_t count, size_t stride) const;

        void setShapeWrinkle(ShapeId shape, std::span<const float> wrinkle) const {
            setShapeWrinkle(shape, wrinkle.data(), wrinkle.size(), 0);
        }

        void setShapeWrinkle(ShapeId shape, const float *values, size_t count, size_t stride) const;

        uint32_t shapeCount() const;

        // Throws Error(SMDL_ERROR_NOT_FOUND) when there is no such shape
        ShapeId findShape(std::string_view name) const;

    private:
        smdl_geometry *handle() const; // api/src/cpp_api.cpp
        smdl_model *ownerModel() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

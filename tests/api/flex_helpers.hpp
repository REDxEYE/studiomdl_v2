// Helpers of the Flex part tests: shapes from the frames of a fixture VTA.
#pragma once

#include "harness.hpp"

#include <string>
#include <vector>

namespace flextest {

// One shape per VTA frame 1..K (named "frame<k>", shape index k - 1): the geometry vertices every changed VTA vertex
// drives (MapVtaToGeometry: the mapping the compiler builds) with the position and normal delta of that frame.
// `mode` must be the one the geometry was filled with.
inline void AddVtaShapes(smdl::Geometry geometry, const SmdFile &smd, VertexMode mode, const VtaFile &vta) {
    const GeometryData data = BuildGeometryData(smd, mode);
    const VtaVertexMap map = MapVtaToGeometry(vta, data.positions, data.normals);
    Require(map.unmatched == 0, "every VTA vertex matches a geometry vertex");
    for (const VtaFrame &frame: vta.frames) {
        if (frame.frame == 0)
            continue;
        std::vector<uint32_t> vertices;
        std::vector<smdl::Vec3> positionDeltas;
        std::vector<smdl::Vec3> normalDeltas;
        for (const VtaDelta &delta: VtaFrameDeltas(vta, frame.frame)) {
            for (const uint32_t vertex: map.geometryVertices[static_cast<size_t>(delta.vertex)]) {
                vertices.push_back(vertex);
                positionDeltas.push_back(delta.positionDelta);
                normalDeltas.push_back(delta.normalDelta);
            }
        }
        const smdl::ShapeId shape = geometry.addShape("frame" + std::to_string(frame.frame), vertices, positionDeltas);
        Require(shape == static_cast<smdl::ShapeId>(frame.frame - 1), "shape index = frame - 1");
        geometry.setShapeNormalDeltas(shape, normalDeltas);
    }
}

} // namespace flextest

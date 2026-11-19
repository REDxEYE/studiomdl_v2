// The SMD reference host: feeds the builder API from an SmdFile (semantics: harness.hpp)
#include "harness.hpp"

#include <format>
#include <map>

namespace {
    bool IsNotFound(const smdl::Error &error) { return error.status() == SMDL_ERROR_NOT_FOUND; }

    // The model's bone of an SMD node (by name)
    smdl::BoneId BoneOfNode(smdl::Model &model, const SmdFile &smd, uint32_t node) {
        if (node >= smd.nodes.size())
            throw TestFailure(std::format("{}: node {} does not exist", smd.path.string(), node));
        return model.findBone(smd.nodes[node].name);
    }
} // namespace

void AddSkeleton(smdl::Model &model, const SmdFile &smd) {
    if (smd.frames.empty() && !smd.nodes.empty())
        throw TestFailure(std::format("{}: no skeleton section: no rest pose for the bones", smd.path.string()));
    for (size_t i = 0; i < smd.nodes.size(); ++i) {
        const SmdNode &node = smd.nodes[i];
        try {
            model.findBone(node.name);
            continue; // already there (another source of the same skeleton)
        } catch (const smdl::Error &error) {
            if (!IsNotFound(error))
                throw;
        }
        const SmdBonePose &pose = smd.frames.front().bones[i];
        const smdl::BoneId parent = node.parent < 0 ? smdl::NoBone : model.findBone(smd.nodes[node.parent].name);
        model.addBoneEuler(node.name, parent, pose.position, pose.euler);
    }
}

void FillGeometry(smdl::Model &model, smdl::Geometry geometry, const SmdFile &smd, VertexMode mode) {
    const GeometryData data = BuildGeometryData(smd, mode);

    // The materials are known before the triangles; their ids are 0, 1, 2... in call order (smdl_geometry.h)
    std::vector<smdl::MaterialId> materialIds;
    for (const std::string &name: data.materials)
        materialIds.push_back(geometry.addMaterial(name));
    std::vector<smdl::MaterialId> triangleMaterials;
    triangleMaterials.reserve(data.triangleMaterials.size());
    for (const uint32_t material: data.triangleMaterials)
        triangleMaterials.push_back(materialIds[material]);

    std::vector<smdl::BoneId> bones;
    bones.reserve(data.weightNodes.size());
    for (const uint32_t node: data.weightNodes)
        bones.push_back(BoneOfNode(model, smd, node));

    // positions first, then the per-vertex arrays, the triangles, the per-corner arrays (smdl_geometry.h)
    geometry.setPositions(data.positions);
    geometry.setNormals(data.normals);
    for (size_t set = 0; set < data.uvs.size(); ++set)
        geometry.setUVs(static_cast<uint32_t>(set), data.uvs[set]);
    geometry.setWeights(data.influencesPerVertex, bones, data.weights);
    geometry.setTriangles(data.indices, triangleMaterials);
    if (mode == VertexMode::Corners) {
        geometry.setCornerNormals(data.cornerNormals);
        for (size_t set = 0; set < data.cornerUvs.size(); ++set)
            geometry.setCornerUVs(static_cast<uint32_t>(set), data.cornerUvs[set]);
    }
}

void SetGeometryRestPose(smdl::Model &model, smdl::Geometry geometry, const SmdFile &smd) {
    if (smd.frames.empty() && !smd.nodes.empty())
        throw TestFailure(std::format("{}: no skeleton section", smd.path.string()));
    for (size_t i = 0; i < smd.nodes.size(); ++i) {
        const SmdBonePose &pose = smd.frames.front().bones[i];
        geometry.setRestPoseEuler(BoneOfNode(model, smd, static_cast<uint32_t>(i)), pose.position, pose.euler);
    }
}

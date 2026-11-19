// Tests of the SMD reference host (smd_host.cpp) against the real C++ API: the state of the geometry handles after
// the host filled them (the byte-equivalence tests with the QC compiles live in the parts' test files)
#include "harness.hpp"

namespace {
    struct Built {
        SmdFile smd;
        GeometryData data;
    };

    Built Fill(smdl::Model &model, std::string_view smdPath, std::string_view meshName, VertexMode mode) {
        Built built{ReadSmd(FixturePath(smdPath)), {}};
        built.data = BuildGeometryData(built.smd, mode);
        AddSkeleton(model, built.smd);
        smdl::Mesh mesh = model.createMesh(meshName);
        FillGeometry(model, mesh.geometry(), built.smd, mode);
        return built;
    }
} // namespace

SMDL_TEST(harness_smd_host_prop_static_indexed) {
    smdl::Model model("api/prop_static");
    const Built built = Fill(model, "src/crate.smd", "crate", VertexMode::Indexed);
    const smdl::Geometry geometry = model.findMesh("crate").geometry();

    Require(model.boneCount() == built.smd.nodes.size() && model.boneCount() == 1, "one bone (root)");
    Require(model.findBone("root") == 0, "bone id of node 0");
    Require(!built.smd.triangles.empty(), "the crate has triangles");
    Require(geometry.triangleCount() == built.smd.triangles.size(), "triangle count");
    Require(geometry.vertexCount() == 3 * built.smd.triangles.size(), "Indexed: one vertex per corner");
    Require(geometry.materialCount() == 1, "prop_static has one material");
    Require(geometry.quadCount() == 0, "no quads");
    Require(built.data.cornerNormals.empty(), "Indexed mode sends no corner arrays");
}

SMDL_TEST(harness_smd_host_prop_static_corners) {
    smdl::Model model("api/prop_static");
    const Built built = Fill(model, "src/crate.smd", "crate", VertexMode::Corners);
    const smdl::Geometry geometry = model.findMesh("crate").geometry();

    // Pooled by (position, weights): the 6 faces of the box share 8 corner positions
    Require(geometry.vertexCount() == 8, "Corners: pooled positions of the box");
    Require(geometry.vertexCount() == built.data.positions.size(), "vertex count = pooled positions");
    Require(geometry.triangleCount() == built.smd.triangles.size(), "triangle count");
    Require(built.data.cornerNormals.size() == 3 * built.smd.triangles.size(), "one corner normal per corner");
    Require(built.data.indices.size() == 3 * built.smd.triangles.size(), "index count");
}

SMDL_TEST(harness_smd_host_skeleton_shared) {
    // Several sources of one skeleton: the second AddSkeleton adds nothing; bone ids follow the node order
    smdl::Model model("api/skinned");
    const SmdFile smd = ReadSmd(FixturePath("src/chain_ref.smd"));
    AddSkeleton(model, smd);
    Require(model.boneCount() == smd.nodes.size(), "bone per node");
    AddSkeleton(model, smd);
    Require(model.boneCount() == smd.nodes.size(), "second AddSkeleton adds nothing");
    for (size_t i = 0; i < smd.nodes.size(); ++i)
        Require(model.findBone(smd.nodes[i].name) == i, "bone id = node index");
    RequireThrows<smdl::Error>([&] { model.findBone("no_such_bone"); }, "unknown bone");
}

SMDL_TEST(harness_smd_host_skinned_weights) {
    smdl::Model model("api/skinned");
    const Built built = Fill(model, "src/chain_ref.smd", "tube", VertexMode::Corners);
    const smdl::Geometry geometry = model.findMesh("tube").geometry();
    Require(geometry.vertexCount() == built.data.positions.size(), "pooled vertices");
    Require(built.data.influencesPerVertex >= 2, "the tube mesh has multi-weight vertices");
    Require(built.data.weights.size() == built.data.positions.size() * built.data.influencesPerVertex,
            "a weight row per vertex");
    Require(geometry.materialCount() == built.data.materials.size(), "materials");
}

SMDL_TEST(harness_smd_host_multi_material) {
    smdl::Model model("api/prop_multi");
    const Built built = Fill(model, "src/house.smd", "house", VertexMode::Indexed);
    const smdl::Geometry geometry = model.findMesh("house").geometry();
    Require(built.data.materials.size() == 3, "prop_multi has three materials");
    Require(geometry.materialCount() == 3, "geometry materials");
    Require(geometry.triangleCount() == built.smd.triangles.size(), "triangle count");
}

SMDL_TEST(harness_smd_host_rest_pose) {
    smdl::Model model("api/skinned");
    const SmdFile smd = ReadSmd(FixturePath("src/chain_ref.smd"));
    AddSkeleton(model, smd);
    smdl::Mesh mesh = model.createMesh("tube");
    SetGeometryRestPose(model, mesh.geometry(), smd); // accepted for every node
}

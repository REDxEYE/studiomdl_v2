// Tests of the harness itself: reference compiles, output comparison, SMD/VTA reader, vertex pooling
#include "harness.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

namespace {
    void WriteText(const std::filesystem::path &path, std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    bool Contains(const std::string &text, std::string_view part) { return text.find(part) != std::string::npos; }

    // Two skinned triangles over three bones; the second shares two corners (position + weights) with the first
    constexpr const char *kPoolSmd = R"(version 1
nodes
0 "root" -1
1 "arm" 0
end
skeleton
time 0
0 0 0 0 0 0 0
1 1.5 0 2 0 1.570796 0
end
triangles
mat_a
0 0 0 0 0 0 1 0 0
0 1 0 0 0 0 1 1 0
1 0 1 0 0 0 1 0 1 2 0 0.75 1 0.25
mat_a
0 0 0 0 0 0 1 0 0
1 0 1 0 0 0 1 0.5 0.5 2 0 0.75 1 0.25
0 1 1 0 0 0 1 1 1
mat_b
0 0 0 0 0 0 1 0 0
0 1 0 0 0 0 1 1 0
0 1 1 0 0 0 1 1 1
end
)";
} // namespace

SMDL_TEST(harness_tempdir) {
    std::filesystem::path first, second;
    {
        TempDir a("one"), b("one");
        first = a.path();
        second = b.path();
        Require(first != second, "temp dirs are unique even with the same tag");
        Require(std::filesystem::is_directory(first) && std::filesystem::is_empty(first), "fresh empty directory");
        WriteText(first / "x" / "y.txt", "z");
    }
    Require(!std::filesystem::exists(first) && !std::filesystem::exists(second), "removed on destruction");
}

SMDL_TEST(harness_reference_compile_twice) {
    const auto qc = FixturePath("prop_static.qc");
    const Outputs first = CompileQcReference(qc);
    const Outputs second = CompileQcReference(qc);
    Require(first.contains("models/api/prop_static.mdl"), "the .mdl is among the outputs");
    Require(first.contains("models/api/prop_static.vvd"), "the .vvd is among the outputs");
    Require(std::ranges::any_of(first, [](const auto &entry) { return entry.first.ends_with(".vtx"); }),
            "a .vtx is among the outputs");
    for (const auto &[name, bytes]: first)
        Require(!bytes.empty(), "output " + name + " is not empty");
    RequireSameOutputs(first, second);
}

SMDL_TEST(harness_reference_failure) {
    TempDir dir("fail");
    WriteText(dir.path() / "bad.qc", "$modelname \"api/bad.mdl\"\n$body \"x\" \"does_not_exist.smd\"\n");
    const ReferenceRun run = RunReference(dir.path() / "bad.qc");
    Require(run.exitCode != 0, "a QC with a missing source fails");
    Require(run.outputs.empty(), "a failed compile writes no model");
    Require(!run.log.empty(), "the log of the failed compile is captured");
    RequireThrows<TestFailure>([&] { CompileQcReference(dir.path() / "bad.qc"); },
                               "CompileQcReference throws when the compile fails");
}

SMDL_TEST(harness_detects_one_byte_difference) {
    const Outputs expected = CompileQcReference(FixturePath("prop_static.qc"));
    RequireSameOutputs(expected, expected);

    // flip one byte of numbones (studiohdr_t offset 156)
    Outputs changed = expected;
    std::vector<std::byte> &mdl = changed.at("models/api/prop_static.mdl");
    mdl.at(156) ^= std::byte{1};
    std::string report = DescribeOutputDifference(expected, changed);
    Require(Contains(report, "models/api/prop_static.mdl"), "report names the file: " + report);
    Require(Contains(report, "offset 156"), "report gives the offset: " + report);
    Require(Contains(report, "numbones"), "report decodes the header field: " + report);
    bool threw = false;
    try {
        RequireSameOutputs(expected, changed);
    } catch (const TestFailure &failure) {
        threw = true;
        Require(Contains(failure.what(), "test_harness.cpp:"), "failure carries the file:line");
        Require(Contains(failure.what(), "numbones"), "failure carries the report");
    }
    Require(threw, "RequireSameOutputs fails on a one-byte difference");

    // a difference in another file, a size difference, a missing and an extra file
    Outputs vvd = expected;
    vvd.at("models/api/prop_static.vvd").back() ^= std::byte{0x80};
    report = DescribeOutputDifference(expected, vvd);
    Require(Contains(report, "prop_static.vvd") && Contains(report, "first difference"), "vvd difference: " + report);

    Outputs longer = expected;
    longer.at("models/api/prop_static.mdl").push_back(std::byte{0});
    report = DescribeOutputDifference(expected, longer);
    Require(Contains(report, "prefix"), "size difference is reported: " + report);

    Outputs missing = expected;
    missing.erase("models/api/prop_static.vvd");
    missing["models/api/extra.bin"] = {};
    report = DescribeOutputDifference(expected, missing);
    Require(Contains(report, "missing in actual: models/api/prop_static.vvd"), "missing file: " + report);
    Require(Contains(report, "unexpected in actual: models/api/extra.bin"), "extra file: " + report);
    RequireThrows<TestFailure>([&] { RequireSameOutputs(expected, missing); }, "missing files fail");
}

SMDL_TEST(harness_mdl_offset_decoding) {
    const Outputs outputs = CompileQcReference(FixturePath("prop_static.qc"));
    const std::vector<std::byte> &mdl = outputs.at("models/api/prop_static.mdl");
    Require(Contains(DescribeMdlOffset(mdl, 0), "studiohdr_t.id"), "offset 0 is the id");
    Require(Contains(DescribeMdlOffset(mdl, 5), "studiohdr_t.version"), "offset 5 is in the version");
    Require(Contains(DescribeMdlOffset(mdl, 12 + 63), "studiohdr_t.name"), "end of the name");
    Require(Contains(DescribeMdlOffset(mdl, 156), "studiohdr_t.numbones"), "offset 156 is numbones");
    Require(Contains(DescribeMdlOffset(mdl, 160), "studiohdr_t.boneindex"), "offset 160 is boneindex");
    Require(Contains(DescribeMdlOffset(mdl, 407), "studiohdr_t.unused2"), "last header byte");
    // a field holding a section offset: the first byte of the bones names that section
    int32_t boneIndex = 0;
    std::memcpy(&boneIndex, mdl.data() + 160, sizeof(boneIndex));
    Require(boneIndex >= 408 && static_cast<size_t>(boneIndex) < mdl.size(), "boneindex is inside the file");
    Require(Contains(DescribeMdlOffset(mdl, static_cast<size_t>(boneIndex)), "boneindex"),
            "the first byte of the bones is in the bone section: " + DescribeMdlOffset(mdl, static_cast<size_t>(boneIndex)));
    // the header's length field is the file size
    int32_t length = 0;
    std::memcpy(&length, mdl.data() + 76, sizeof(length));
    Require(static_cast<size_t>(length) == mdl.size(), "studiohdr_t.length is the file size (field table is right)");
}

SMDL_TEST(harness_smd_reader_versions) {
    TempDir dir("smd");
    // version 3 with comments, CRLF line ends, links, extra uvs and a skeleton with an implicit second frame
    std::string text = "version 3\r\n// a comment\r\nnodes\r\n0 \"root\" -1\r\n1 \"arm\" 0\r\nend\r\nskeleton\r\ntime 5\r\n"
                       "0 0 0 0 0 0 0\r\n1 1.5 0 2 0 1.570796 0\r\ntime 6\r\n1 2 0 2 0 1.570796 0.5\r\nend\r\ntriangles\r\n"
                       "mat/with space.bmp  \r\n"
                       "1 0 0 0 0 0 1 0.25 0.75 2 0 0.75 1 0.25 2 0.5 0.125\r\n"
                       "0 1 0 0 0 0 1 1 0 0 2 0.5 0.25\r\n"
                       "0 0 1 0 0 0 1 0 1\r\n"
                       "end\r\n";
    WriteText(dir.path() / "v3.smd", text);
    const SmdFile smd = ReadSmd(dir.path() / "v3.smd");
    Require(smd.version == 3, "version");
    Require(smd.nodes.size() == 2 && smd.nodes[1].name == "arm" && smd.nodes[1].parent == 0, "nodes");
    Require(smd.frames.size() == 2, "two skeleton frames");
    Require(smd.frames[0].time == 5 && smd.frames[1].time == 6, "frame times");
    Require(smd.frames[0].bones[1].euler[1] == 1.570796f, "euler value is the exact float of the text");
    Require(smd.frames[0].bones[1].position == smdl::Vec3({1.5f, 0.0f, 2.0f}), "frame 0 position");
    Require(smd.frames[1].bones[1].euler[2] == 0.5f && smd.frames[1].bones[1].position[0] == 2.0f, "frame 1 values");
    Require(smd.frames[1].bones[0].position == smdl::Vec3({0, 0, 0}), "unlisted bone repeats the previous frame");
    Require(smd.triangles.size() == 1, "one triangle");
    const SmdTriangle &triangle = smd.triangles[0];
    Require(triangle.material == "mat/with space.bmp", "material name without the trailing blanks");
    const SmdCorner &c0 = triangle.corners[0];
    Require(c0.bone == 1 && c0.uv == smdl::Vec2({0.25f, 0.75f}), "corner bone and uv (v not inverted)");
    Require(c0.links.size() == 2 && c0.links[0].bone == 0 && c0.links[0].weight == 0.75f &&
                    c0.links[1].bone == 1 && c0.links[1].weight == 0.25f,
            "links");
    Require(c0.extras == std::vector<float>({0.5f, 0.125f}), "extra uvs of version 3");
    Require(triangle.corners[1].links.empty() && triangle.corners[1].extras == std::vector<float>({0.5f, 0.25f}),
            "no links, extras after the zero link count");
    Require(triangle.corners[2].links.empty() && triangle.corners[2].extras.empty(), "plain corner");

    // version 1 and 2 ignore the extras column, 2 differs only in how the compiler reads the material
    for (const int version: {1, 2}) {
        std::string old = text;
        old.replace(0, 9, "version " + std::to_string(version));
        WriteText(dir.path() / "old.smd", old);
        const SmdFile v = ReadSmd(dir.path() / "old.smd");
        Require(v.version == version, "version " + std::to_string(version));
        Require(v.triangles.at(0).corners[0].extras.empty(), "no extras before version 3");
        Require(v.triangles.at(0).corners[0].links.size() == 2, "links in every version");
    }

    WriteText(dir.path() / "bad_version.smd", "version 4\nnodes\nend\n");
    RequireThrows<TestFailure>([&] { ReadSmd(dir.path() / "bad_version.smd"); }, "version 4 is rejected");
    WriteText(dir.path() / "bad_parent.smd", "version 1\nnodes\n0 \"a\" 1\n1 \"b\" -1\nend\n");
    RequireThrows<TestFailure>([&] { ReadSmd(dir.path() / "bad_parent.smd"); }, "parent after child is rejected");
    WriteText(dir.path() / "bad_bone.smd",
              "version 1\nnodes\n0 \"a\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\ntriangles\nm\n3 0 0 0 0 0 1 0 0\n"
              "0 0 0 0 0 0 1 0 0\n0 0 0 0 0 0 1 0 0\nend\n");
    RequireThrows<TestFailure>([&] { ReadSmd(dir.path() / "bad_bone.smd"); }, "bone index out of range is rejected");
}

SMDL_TEST(harness_smd_reader_fixture) {
    const SmdFile smd = ReadSmd(FixturePath("src/crate.smd"));
    Require(!smd.nodes.empty() && !smd.frames.empty() && !smd.triangles.empty(), "crate.smd has nodes, skeleton, triangles");
    Require(smd.frames.front().bones.size() == smd.nodes.size(), "one pose per node");
}

SMDL_TEST(harness_geometry_data_indexed) {
    TempDir dir("pool");
    WriteText(dir.path() / "pool.smd", kPoolSmd);
    const SmdFile smd = ReadSmd(dir.path() / "pool.smd");
    const GeometryData data = BuildGeometryData(smd, VertexMode::Indexed);
    Require(data.positions.size() == 9 && data.normals.size() == 9, "one vertex per corner");
    Require(data.uvs.size() == 1 && data.uvs[0].size() == 9, "uv set 0 per corner");
    Require(data.indices == std::vector<uint32_t>({0, 1, 2, 3, 4, 5, 6, 7, 8}), "indices 0..3n-1 in file order");
    Require(data.materials == std::vector<std::string>({"mat_a", "mat_b"}), "materials in first-seen order");
    Require(data.triangleMaterials == std::vector<uint32_t>({0, 0, 1}), "per-triangle materials");
    Require(data.influencesPerVertex == 2, "largest link count");
    Require(data.weights.size() == 18 && data.weightNodes.size() == 18, "weights padded to influencesPerVertex");
    // corner 2 (vertex 2): links {0: .75, 1: .25}; corner 0: {0: 1} padded with bone 0 / weight 0
    Require(data.weightNodes[4] == 0 && data.weightNodes[5] == 1 && data.weights[4] == 0.75f && data.weights[5] == 0.25f,
            "linked corner");
    Require(data.weightNodes[0] == 0 && data.weightNodes[1] == 0 && data.weights[0] == 1.0f && data.weights[1] == 0.0f,
            "single-bone corner padded with zero weight");
    Require(data.cornerNormals.empty() && data.cornerUvs.empty(), "no corner arrays in Indexed mode");
}

SMDL_TEST(harness_geometry_data_corners) {
    TempDir dir("pool");
    WriteText(dir.path() / "pool.smd", kPoolSmd);
    const SmdFile smd = ReadSmd(dir.path() / "pool.smd");
    const GeometryData data = BuildGeometryData(smd, VertexMode::Corners);
    // corners in file order: (000|b0) (100|b0) (010|b0 .75,b1 .25) | (000|b0) (010|same) (110|b0) | (000|b0) (100|b0) (110|b0)
    // -> pool [(000|b0), (100|b0), (010|links), (110|b0)], indices 0 1 2 | 0 2 3 | 0 1 3
    Require(data.cornerNormals.size() == 9 && data.cornerUvs.size() == 1 && data.cornerUvs[0].size() == 9,
            "corner arrays have 3 entries per triangle");
    Require(data.indices.size() == 9, "3 indices per triangle");
    // corner 0, 3 and 6 are the same position and weights -> the same vertex, the first one seen
    Require(data.indices[0] == 0 && data.indices[3] == 0 && data.indices[6] == 0, "equal corners share a vertex");
    // corner 2 (links) and corner 4 (same position, same links) share a vertex
    Require(data.indices[2] == data.indices[4], "equal position+weights share a vertex");
    // corner 1 and 7 are (100|b0); corner 5 and 8 are (110|b0)
    Require(data.indices[1] == data.indices[7], "corner 1 and 7 are equal");
    Require(data.indices[5] == data.indices[8], "corner 5 and 8 are equal");
    // the pool order is the first-seen order
    Require(data.indices[0] == 0 && data.indices[1] == 1 && data.indices[2] == 2, "pool in first-seen order");
    Require(data.positions.size() == 4, "four distinct (position, weights) pairs");
    // the pooled vertex has the normal/uv of the first corner that created it, corners keep their own
    Require(data.uvs[0][2] == smdl::Vec2({0.0f, 1.0f}), "pooled uv is the first corner's");
    Require(data.cornerUvs[0][4] == smdl::Vec2({0.5f, 0.5f}), "corner 4 keeps its own uv");
    Require(data.cornerUvs[0][2] == smdl::Vec2({0.0f, 1.0f}), "corner 2 uv");

    // same position, different weights: not pooled
    std::string text = kPoolSmd;
    const size_t at = text.find("1 0 1 0 0 0 1 0.5 0.5 2 0 0.75 1 0.25");
    Require(at != std::string::npos, "test data");
    text.replace(at, std::string("1 0 1 0 0 0 1 0.5 0.5 2 0 0.75 1 0.25").size(), "1 0 1 0 0 0 1 0.5 0.5 2 0 0.5 1 0.5");
    WriteText(dir.path() / "pool2.smd", text);
    const GeometryData other = BuildGeometryData(ReadSmd(dir.path() / "pool2.smd"), VertexMode::Corners);
    Require(other.positions.size() == 5 && other.indices[2] != other.indices[4], "different weights are different vertices");
}

SMDL_TEST(harness_vta_reader_and_mapping) {
    TempDir dir("vta");
    // four vertices in a row; frame 1 moves vertices 1 and 2, frame 2 vertex 3
    WriteText(dir.path() / "a.vta", R"(version 1
nodes
0 "root" -1
end
skeleton
time 0
time 1 # a
time 2 # b
end
vertexanimation
time 0
0 0 0 0 0 0 1
1 1 0 0 0 0 1
2 2 0 0 0 0 1
3 3 0 0 0 0 1
time 1
1 1 0 0.5 0 0 1
2 2 0 0.25 0 0 1
time 2
3 3.5 0 0 0 0.1 1
end
)");
    const VtaFile vta = ReadVta(dir.path() / "a.vta");
    Require(vta.frames.size() == 3 && vta.vertexCount == 4, "frames and vertex count");
    Require(vta.findFrame(1) && vta.findFrame(1)->vertices.size() == 2 && !vta.findFrame(3), "findFrame");
    const auto deltas = VtaFrameDeltas(vta, 1);
    Require(deltas.size() == 2 && deltas[0].vertex == 1 && deltas[0].positionDelta == smdl::Vec3({0, 0, 0.5f}) &&
                    deltas[1].positionDelta[2] == 0.25f,
            "frame deltas against frame 0");
    Require(VtaFrameDeltas(vta, 2)[0].normalDelta == smdl::Vec3({0, 0.1f, 0}), "normal deltas");

    // geometry: the 4 VTA positions, vertex 1 duplicated (two geometry vertices), one geometry vertex far away
    const std::vector<smdl::Vec3> positions{{0, 0, 0}, {1, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {9, 9, 9}};
    const std::vector<smdl::Vec3> normals(positions.size(), smdl::Vec3{0, 0, 1});
    const VtaVertexMap map = MapVtaToGeometry(vta, positions, normals);
    Require(map.geometryVertices.size() == 4, "one entry per VTA vertex");
    Require(map.geometryVertices[0] == std::vector<uint32_t>({0}), "vertex 0 -> geometry 0");
    Require(map.geometryVertices[1] == std::vector<uint32_t>({1, 2}), "vertex 1 drives both duplicates");
    Require(map.geometryVertices[2] == std::vector<uint32_t>({3}) && map.geometryVertices[3] == std::vector<uint32_t>({4}),
            "vertices 2 and 3");
    Require(map.unmatched == 0, "every VTA vertex found its geometry vertex");

    // a VTA vertex without a geometry vertex nearby is unmatched
    const std::vector<smdl::Vec3> fewer{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    const VtaVertexMap partial = MapVtaToGeometry(vta, fewer, std::vector<smdl::Vec3>(3, smdl::Vec3{0, 0, 1}));
    Require(partial.unmatched == 1 && partial.geometryVertices[3].empty(), "unmatched VTA vertex");

    // the fixture VTA reads
    if (std::filesystem::exists(std::filesystem::path(SMDL_TEST_FIXTURES_DIR) / "src" / "face.vta")) {
        const VtaFile face = ReadVta(FixturePath("src/face.vta"));
        Require(face.frames.size() >= 4 && face.findFrame(0), "face.vta has frame 0 and the flex frames");
    }
}

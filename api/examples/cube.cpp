// Builds a 16-unit cube with a bone on each corner (every vertex weighted to its corner bone and the root), two flexes
// driven by controllers, and an idle sequence, then writes models/examples/cube.* under the given directory.
//
// Built as the CMake target `smdl_example_cube` (top-level builds), or by hand:
//   g++ -std=c++20 -Iapi/include api/examples/cube.cpp -L<build> -lstudiomdl -Wl,-rpath,<build> -o cube
//   ./cube /tmp/out
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <studiomdl/studiomdl.hpp>

int main(int argc, char **argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    try {
        smdl::Model mdl("examples/cube");
        mdl.addMaterialPath("models/examples/");

        // Skeleton: a root plus one bone per corner, placed at the corner (rest pose is local to the parent)
        constexpr std::array<smdl::Vec3, 8> corners{{
            {-8, -8, 0}, {8, -8, 0}, {8, 8, 0}, {-8, 8, 0},     // bottom
            {-8, -8, 16}, {8, -8, 16}, {8, 8, 16}, {-8, 8, 16}, // top
        }};
        constexpr smdl::Quat identity{0, 0, 0, 1};
        const smdl::BoneId root = mdl.addBone("root", smdl::NoBone, {0, 0, 0}, identity);
        std::array<smdl::BoneId, 8> cornerBones{};
        for (size_t i = 0; i < corners.size(); ++i)
            cornerBones[i] = mdl.addBone("corner" + std::to_string(i), root, corners[i], identity);

        // Mesh: 8 shared vertices (positions, weights and shapes are per vertex), 6 quad faces as 12 triangles. A
        // vertex sits on 3 faces with different UVs and normals, so those are given per triangle corner: the compiler
        // splits a vertex wherever its corners differ, like the SMD loader does.
        smdl::Mesh body = mdl.createMesh("cube");
        smdl::Geometry geo = body.geometry();
        geo.setPositions(corners);

        // Two influences per vertex: 70% its corner bone, 30% the root
        std::vector<smdl::BoneId> weightBones;
        std::vector<float> weights;
        for (size_t i = 0; i < corners.size(); ++i) {
            weightBones.insert(weightBones.end(), {cornerBones[i], root});
            weights.insert(weights.end(), {0.7f, 0.3f});
        }
        geo.setWeights(2, weightBones, weights);

        const smdl::MaterialId wood = geo.addMaterial("cube_wood");
        // Each face as a quad a, b, c, d (counter-clockwise seen from outside) -> triangles (a, b, c) and (a, c, d)
        const std::array<std::array<uint32_t, 4>, 6> faces{{
            {0, 3, 2, 1}, // bottom
            {4, 5, 6, 7}, // top
            {0, 1, 5, 4}, // -y
            {1, 2, 6, 5}, // +x
            {2, 3, 7, 6}, // +y
            {3, 0, 4, 7}, // -x
        }};
        const std::array<smdl::Vec2, 4> quadUV{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        std::vector<uint32_t> indices;
        std::vector<smdl::Vec2> cornerUVs;
        std::vector<smdl::Vec3> cornerNormals;
        for (const auto &f: faces) {
            // flat face normal: (b - a) x (d - a), normalized
            const smdl::Vec3 &a = corners[f[0]], &b = corners[f[1]], &d = corners[f[3]];
            const smdl::Vec3 e1{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2{d[0] - a[0], d[1] - a[1], d[2] - a[2]};
            smdl::Vec3 n{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (float &c: n)
                c /= len;
            for (const int q: {0, 1, 2, 0, 2, 3}) {
                indices.push_back(f[q]);
                cornerUVs.push_back(quadUV[q]);
                cornerNormals.push_back(n);
            }
        }
        geo.setTriangles(indices, std::vector<smdl::MaterialId>(indices.size() / 3, wood));
        geo.setCornerUVs(0, cornerUVs);
        geo.setCornerNormals(cornerNormals);

        // Shapes (vertex animation): sparse vertex indices + position deltas
        const smdl::ShapeId raiseTop = geo.addShape("raise_top", std::vector<uint32_t>{4, 5, 6, 7},
                                                    std::vector<smdl::Vec3>(4, smdl::Vec3{0, 0, 8}));
        const smdl::ShapeId pinch = geo.addShape("pinch", std::vector<uint32_t>{0, 1, 2, 3, 4, 5, 6, 7},
                                                 std::vector<smdl::Vec3>{{4, 4, 0}, {-4, 4, 0}, {-4, -4, 0}, {4, -4, 0},
                                                                         {4, 4, 0}, {-4, 4, 0}, {-4, -4, 0}, {4, -4, 0}});

        // Flexes on the mesh from those shapes, controllers on the model, rules connecting them. addFlex returns the
        // mesh's flex key (for setFlexPosition/Decay/Split); flexId(key) is the model-wide flex a rule targets.
        const smdl::FlexKeyId raiseKey = body.addFlex("raise_top", raiseTop);
        const smdl::FlexKeyId pinchKey = body.addFlex("pinch", pinch);
        mdl.addFlexController("cube", "raise", 0.0f, 1.0f);
        mdl.addFlexController("cube", "squeeze", 0.0f, 1.0f);
        mdl.addFlexRule(body.flexId(raiseKey), "raise");
        mdl.addFlexRule(body.flexId(pinchKey), "min(squeeze * 2, 1)");

        // A model needs at least one sequence: a one-frame idle in the rest pose
        smdl::Animation idle = mdl.createAnimation("idle_anim", 30.0f, 1);
        idle.setLoop();
        mdl.createSequence("idle").addAnimation(idle);

        // Compile in memory (no game directory needed), print the messages, write the files
        smdl::Result result = mdl.compile({}, [](smdl::Severity severity, std::string_view text) {
            if (severity != smdl::Severity::Message)
                std::fwrite(text.data(), 1, text.size(), stderr);
        });
        for (const smdl::OutputFile &file: result.files())
            std::printf("%.*s: %zu bytes\n", static_cast<int>(file.relativePath.size()), file.relativePath.data(),
                        file.data.size());
        result.saveTo(outDir); // <outDir>/models/examples/cube.{mdl,vvd,dx90.vtx}
        std::printf("%d warning(s)\n", result.warningCount());
    }
    catch (const smdl::Error &e) {
        std::fprintf(stderr, "error: %s%s%s\n", e.what(), e.object().empty() ? "" : " in ", e.object().c_str());
        return 1;
    }
    return 0;
}

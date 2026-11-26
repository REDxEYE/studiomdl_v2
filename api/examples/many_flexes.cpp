// Builds a square grid with N flexes (default 160), each driven by its own controller, and writes
// models/examples/many_flexes_<N>.* under the given directory (e.g. an SFM game directory such as game/usermod).
//
// Built as the CMake target `smdl_example_many_flexes` (top-level builds): smdl_example_many_flexes <outDir> [N]
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

#include <studiomdl/studiomdl.hpp>

int main(int argc, char **argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const uint32_t flexCount = argc > 2 ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 160;
    const uint32_t side = std::max<uint32_t>(2, static_cast<uint32_t>(std::ceil(std::sqrt(double(flexCount))))),
                   quads = side - 1;
    constexpr float spacing = 4.0f;
    try {
        smdl::Model mdl(std::format("examples/many_flexes_{}", flexCount));
        mdl.addMaterialPath("models/examples/");
        const smdl::BoneId root = mdl.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});

        smdl::Mesh body = mdl.createMesh("grid");
        smdl::Geometry geo = body.geometry();
        std::vector<smdl::Vec3> positions, normals;
        std::vector<smdl::Vec2> uvs;
        for (uint32_t y = 0; y < side; ++y) {
            for (uint32_t x = 0; x < side; ++x) {
                positions.push_back({(x - quads / 2.0f) * spacing, (y - quads / 2.0f) * spacing, 0});
                normals.push_back({0, 0, 1});
                uvs.push_back({float(x) / quads, float(y) / quads});
            }
        }
        geo.setPositions(positions);
        geo.setNormals(normals);
        geo.setUVs(0, uvs);
        geo.setWeights(1, std::vector<smdl::BoneId>(positions.size(), root), std::vector<float>(positions.size(), 1.0f));

        std::vector<uint32_t> indices;
        for (uint32_t y = 0; y < quads; ++y) {
            for (uint32_t x = 0; x < quads; ++x) {
                const uint32_t a = y * side + x, b = a + 1, c = a + side + 1, d = a + side;
                indices.insert(indices.end(), {a, b, c, a, c, d});
            }
        }
        const smdl::MaterialId material = geo.addMaterial("many_flexes");
        geo.setTriangles(indices, std::vector<smdl::MaterialId>(indices.size() / 3, material));

        // Flex i raises vertex i; controller f<i> drives it one to one
        for (uint32_t i = 0; i < flexCount; ++i) {
            const std::string name = std::format("f{:03}", i);
            const smdl::ShapeId shape = geo.addShape(name, std::vector<uint32_t>{i}, std::vector<smdl::Vec3>{{0, 0, 8}});
            const smdl::FlexKeyId key = body.addFlex(name, shape);
            mdl.addFlexController("grid", name, 0.0f, 1.0f);
            mdl.addFlexRule(body.flexId(key), name);
        }

        smdl::Animation idle = mdl.createAnimation("idle_anim", 30.0f, 1);
        idle.setLoop();
        mdl.createSequence("idle").addAnimation(idle);

        smdl::Result result = mdl.compile({}, [](smdl::Severity severity, std::string_view text) {
            if (severity != smdl::Severity::Message)
                std::fwrite(text.data(), 1, text.size(), stderr);
        });
        result.saveTo(outDir);
        for (const smdl::OutputFile &file: result.files())
            std::printf("%s/%.*s: %zu bytes\n", outDir.c_str(), static_cast<int>(file.relativePath.size()),
                        file.relativePath.data(), file.data.size());
        std::printf("%u flexes, %u controllers, %d warning(s)\n", mdl.flexCount(), flexCount, result.warningCount());
    }
    catch (const smdl::Error &e) {
        std::fprintf(stderr, "error: %s%s%s\n", e.what(), e.object().empty() ? "" : " in ", e.object().c_str());
        return 1;
    }
    return 0;
}

// Consumer host: studiomdl::static linked into a C++20 executable (tests/consumer/CMakeLists.txt), only the C++ wrapper
// and <studiomdl/studiomdl.hpp>. Builds the same cube static prop as tests/api_c/c_host.c, compiles it, saves it.
//
// usage: consumer_host <output directory> [reference directory]
// With a reference directory (the output of the shared-library C host) the files must be byte-identical: the static
// link must behave exactly like the shared library.
#include <studiomdl/studiomdl.hpp>
#include <algorithm>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr std::array<smdl::Vec3, 8> kPositions = {{
    {-8, -8, 0}, {8, -8, 0}, {8, 8, 0}, {-8, 8, 0}, {-8, -8, 16}, {8, -8, 16}, {8, 8, 16}, {-8, 8, 16},
}};
constexpr float kN = 0.57735f;
constexpr std::array<smdl::Vec3, 8> kNormals = {{
    {-kN, -kN, -kN}, {kN, -kN, -kN}, {kN, kN, -kN}, {-kN, kN, -kN}, {-kN, -kN, kN}, {kN, -kN, kN}, {kN, kN, kN}, {-kN, kN, kN},
}};
constexpr std::array<smdl::Vec2, 8> kUvs = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}}};
constexpr std::array<uint32_t, 36> kTriangles = {
    0, 3, 2, 0, 2, 1, /* bottom */ 4, 5, 6, 4, 6, 7, /* top */ 0, 1, 5, 0, 5, 4, /* -y */
    1, 2, 6, 1, 6, 5, /* +x */ 2, 3, 7, 2, 7, 6, /* +y */ 3, 0, 4, 3, 4, 7, /* -x */
};

std::vector<char> ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int Fail(const std::string &what) {
    std::fprintf(stderr, "consumer_host: %s\n", what.c_str());
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2)
        return Fail("usage: consumer_host <output directory> [reference directory]");
    const std::filesystem::path outDir = argv[1];

    try {
        smdl::Model model("api_c/crate");
        model.addMaterialPath("models/api_c");
        model.setSurfaceProp("wood");
        model.setStaticProp(true);
        model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});

        smdl::Geometry geometry = model.createMesh("crate").geometry();
        geometry.setPositions(kPositions);
        geometry.setNormals(kNormals);
        geometry.setUVs(0, kUvs);
        const smdl::MaterialId wood = geometry.addMaterial("crate_wood");
        const std::vector<smdl::MaterialId> materials(kTriangles.size() / 3, wood);
        geometry.setTriangles(kTriangles, materials);

        int logMessages = 0;
        const smdl::Result result = model.compile({}, [&](smdl::Severity, std::string_view) { ++logMessages; });
        if (!result.ok())
            return Fail("compile failed: " + std::string(result.errorMessage()));
        const auto files = result.files();
        if (files.size() < 3)
            return Fail("expected at least 3 output files, got " + std::to_string(files.size()));
        if (logMessages == 0)
            return Fail("the log callback received nothing");

        result.saveTo(outDir.string());

        if (argc > 2) {
            const std::filesystem::path refDir = argv[2];
            size_t refCount = 0;
            for (const auto &entry : std::filesystem::recursive_directory_iterator(refDir))
                refCount += entry.is_regular_file();
            if (refCount != files.size())
                return Fail("reference has " + std::to_string(refCount) + " files, this build " + std::to_string(files.size()));
            for (const smdl::OutputFile &file : files) {
                const auto expected = ReadFile(refDir / std::filesystem::path(std::string(file.relativePath)));
                const auto saved = ReadFile(outDir / std::filesystem::path(std::string(file.relativePath)));
                const bool sameAsMemory =
                    saved.size() == file.data.size() && std::equal(saved.begin(), saved.end(), reinterpret_cast<const char *>(file.data.data()));
                if (!sameAsMemory)
                    return Fail("saved file differs from the result: " + std::string(file.relativePath));
                if (expected.empty() || expected != saved)
                    return Fail("differs from the shared library output: " + std::string(file.relativePath));
            }
        }
        std::printf("consumer_host: ok (%zu files, %d log messages)\n", files.size(), logMessages);
        return 0;
    } catch (const smdl::Error &error) {
        return Fail(std::string("smdl::Error: ") + error.what() + " [" + error.object() + "]");
    }
}

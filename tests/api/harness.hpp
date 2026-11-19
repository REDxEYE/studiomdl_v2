// Test harness of the studiomdl builder API (STUDIOMDL_API_PLAN.md): test registry, reference compiles of the QC
// fixtures, byte comparison of outputs, and an SMD/VTA reference reader that drives the C++ API.
//
// Writing a test (tests/api/test_<part>.cpp):
//
//     #include "harness.hpp"
//
//     SMDL_TEST(core_prop_static) {                    // name = <part>_<what>; the runner filters on it
//         const SmdFile smd = ReadSmd(FixturePath("src/crate.smd"));
//         smdl::Model model("api/prop_static");
//         AddSkeleton(model, smd);
//         auto mesh = model.createMesh("crate");
//         FillGeometry(model, mesh.geometry(), smd, VertexMode::Indexed);
//         ...
//         RequireSameOutputs(CompileQcReference(FixturePath("prop_static.qc")), ResultFiles(model.compile()));
//     }
//
// Running: `smdl_api_tests [pattern...]` (names matched case-sensitively with `*` and `?`, e.g. `core_*`; a leading
// `-` excludes: `-flex_*`), `--list` prints the names. ctest runs the whole suite as the test `api`. Tests run in
// name order in one process, one at a time; every test must leave nothing behind (use TempDir) and not depend on
// another test. A test fails by throwing: Require()/Fail() throw TestFailure with the failing file:line, an
// unexpected smdl::Error or std::exception fails the test with its message.
// Library code must not print to stdout (its text belongs to the compile's log callback): the runner captures fd 1
// while a test runs (POSIX) and prints `[ NOTE ] <test> wrote N byte(s) to stdout` after it; with the environment
// variable SMDL_TEST_STRICT_STDOUT set such a test fails.
// Debugging a mismatch: with the environment variable SMDL_TEST_DUMP=<dir> RequireSameOutputs() saves both sides as
// <dir>/expected/models/... and <dir>/actual/models/... (diff them with the tools of tests/golden/mdl_struct.py).
//
// ---------------------------------------------------------------------------------------------------------------
// Fixtures (tests/api/fixtures, see its README.md for the list of QCs, what each covers and which files it writes)
// ---------------------------------------------------------------------------------------------------------------
// Reference outputs: CompileQcReference() runs the studiomdl_v2 binary built next to the tests (path baked in at
// build time) with `-game <temp game dir>` in the QC's directory (the QC is passed by file name, sources are
// relative to it), with the gameinfo.txt of tests/golden/run_golden.py. The result keys are "models/api/x.mdl" etc.
// (the same keys ResultFiles() gives for an smdl::Result), so Outputs of both sides compare directly.
//
// ---------------------------------------------------------------------------------------------------------------
// SMD reader semantics (ReadSmd): what studiomdl's SMD loader (Load_SMD in v1support.cpp / Grab_Nodes /
// Grab_Animation in studiomdl.cpp) sees, with the same sscanf formats:
//  * `version` 1..3 is stored in SmdFile::version. Version 3 corner lines can carry extra UV pairs (SmdCorner::extras,
//    at most 14 floats = UV sets 1..7). Version 2 marks the material names as relative texture paths in the
//    compiler (LookupTexture(.., version == 2)); the builder API has no flag for it, so material names are passed
//    verbatim and the fixtures use version 1.
//  * `nodes`: SmdFile::nodes indexed by the node index (indices must be contiguous, parent < index like
//    Build_Reference demands). The bone ids of a Model fed by AddSkeleton() are the node indices (in node order).
//  * `skeleton`: SmdFile::frames[t] holds one pose per node (SmdBonePose: position, RadianEuler x/y/z exactly the
//    float parsed by `%f`; no clip_rotations, no scale). A frame repeats the previous frame's values for bones it
//    does not list, like the loader. frames[0] is the reference pose (`time` of the first block = startFrame).
//  * `triangles`: SmdFile::triangles in file order, each with the material line (trailing non-graphic characters
//    stripped) and three SmdCorner in file order: bone, position, normal, uv (exactly the file's u and v, NOT
//    flipped), the weight links (`numlinks bone weight...`: weights parsed with atof then rounded to float like the
//    loader, links empty = the corner is bound 1.0 to its bone) and the version 3 extras. Nothing is dropped:
//    null-texture and degenerate triangles stay (the builder's replay skips them like Grab_Triangles does).
//    A `vertexanimation` section inside an SMD is skipped.
//
// How the builder is expected to replay the data FillGeometry() hands over (the contract of Core's geometry):
//  * The triangle list is the SMD's, in file order; indices of triangle t are its corners in FILE order. The builder
//    replays each triangle like ParseFaceData does for a triangle read from an SMD: corner lines 0,1,2 in order, the
//    UV's v is inverted (t[1] = 1 - v), weights go through SortAndBalanceBones, vertices unify through the same
//    lookup (material, normal, position, uv, weights, extra uvs), and the face is stored as (a,b,c) =
//    (corner0, corner2, corner1) unless setFlipTriangles. So an Indexed or Corners geometry of an SMD must compile
//    to the same bytes as that SMD in a QC.
//  * UVs are the file's values; both Indexed and Corners modes pass them unflipped.
//  * Materials: addMaterial in first-seen order of the triangles; per-triangle material ids.
//  * Weights: `influencesPerVertex` = the largest link count in the file (1 when no corner has links); a vertex
//    with fewer links is padded with its first bone at weight 0.0 (padding never changes the result: zero weights
//    are dropped, duplicate bones merge). A corner without links is {its bone, 1.0}.
//  * VertexMode::Indexed: one vertex per corner (vertex i = corner i, indices 0..3n-1); studiomdl merges equal ones.
//    No corner arrays.
//  * VertexMode::Corners: positions + weights pooled (a vertex per distinct (position, effective weight list), pool
//    order = first seen while walking the corners in file order); normals/UVs are corner arrays (3 per triangle)
//    through setCornerNormals/setCornerUVs; the pooled vertex's own normal/uv (setNormals/setUVs) are those of
//    the first corner that created it. The triangle indices point into the pool.
//  * Bone ids are looked up by name in the model (the source's node -> Model bone), so any SMD whose node names
//    exist in the model works (LOD/bodygroup SMDs); `SetGeometryRestPose` adds the SMD's frame-0 pose as the
//    geometry's rest pose overrides.
//
// VTA reader (ReadVta) and the vertex mapping (MapVtaToGeometry): see below.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <studiomdl/studiomdl.hpp>

// ---------------------------------------------------------------------------------------------------------------
// Test registry and assertions
// ---------------------------------------------------------------------------------------------------------------

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TestCase {
    std::string name;
    void (*function)();
    const char *file;
    int line;
};

// Name-ordered at run time (static initialisation order across translation units is unspecified)
std::vector<TestCase> &TestRegistry();

struct TestRegistrar {
    TestRegistrar(const char *name, void (*function)(), const char *file, int line);
};

#define SMDL_TEST(name)                                                                                         \
    static void smdl_test_##name();                                                                             \
    static const TestRegistrar smdl_test_registrar_##name(#name, &smdl_test_##name, __FILE__, __LINE__);        \
    static void smdl_test_##name()

// Throws TestFailure("<file>:<line>: <what>").
void Require(bool condition, std::string_view what, std::source_location where = std::source_location::current());
[[noreturn]] void Fail(std::string_view what, std::source_location where = std::source_location::current());

// Runs function; fails unless it throws Exception (the whole expression of a test body fits in a lambda).
template<class Exception = std::exception, class Function>
void RequireThrows(Function &&function, std::string_view what,
                   std::source_location where = std::source_location::current()) {
    try {
        function();
    } catch (const Exception &) {
        return;
    } catch (...) {
        Fail(std::string("expected exception of another type: ") + std::string(what), where);
    }
    Fail(std::string("expected an exception: ") + std::string(what), where);
}

// Runs the registered tests whose name matches the patterns (main.cpp). Returns the process exit code.
int RunTests(const std::vector<std::string> &patterns);

// ---------------------------------------------------------------------------------------------------------------
// Files, temp dirs, reference compiles, output comparison
// ---------------------------------------------------------------------------------------------------------------

using Outputs = std::map<std::string, std::vector<std::byte>>; // "models/api/x.mdl" -> bytes

// tests/api/fixtures/<rel>; fails the test when it doesn't exist
std::filesystem::path FixturePath(std::string_view rel);

// A fresh empty directory under the system temp dir, removed (recursively) on destruction. Setting the environment
// variable SMDL_TEST_KEEP keeps them (debugging). Each instance is unique per process and call: tests are
// deterministic and independent of each other.
class TempDir {
public:
    explicit TempDir(std::string_view tag = "test");
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    ~TempDir();

    const std::filesystem::path &path() const { return m_Path; }
    operator const std::filesystem::path &() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

// The files under <dir>/models, keyed "models/..." (what Result::saveTo/save and the studiomdl CLI write)
Outputs ReadModelsDir(const std::filesystem::path &dir);

// The files of a compile result, keyed by OutputFile::relativePath
Outputs ResultFiles(const smdl::Result &result);

struct ReferenceRun {
    int exitCode = 0;
    std::string log; // stdout+stderr of studiomdl_v2
    Outputs outputs; // what it wrote under <game>/models
};

// Runs `studiomdl_v2 -game <temp game dir> <extraArgs...> <qc file name>` with the QC's directory as working
// directory; never throws for a failing compile (exit code and log are returned).
ReferenceRun RunReference(const std::filesystem::path &qc, const std::vector<std::string> &extraArgs = {});

// RunReference that requires exit code 0 (the log is part of the failure message) and returns the outputs.
Outputs CompileQcReference(const std::filesystem::path &qc, std::vector<std::string> extraArgs = {});

// The text of the `qc_path` key value a reference compile of `qc` writes into the .mdl (the QC's absolute path: the
// temp game dir has no content mirror). A builder model compared byte for byte calls model.setQcPath() with it
// (a model built in code has no QC and writes no entry). POSIX paths.
std::string QcPathEntry(const std::filesystem::path &qc);

// Byte comparison of two output sets. Fails with the first difference: file, offset, sizes and, for .mdl files,
// the studiohdr_t field or section the offset falls in.
void RequireSameOutputs(const Outputs &expected, const Outputs &actual,
                        std::source_location where = std::source_location::current());

// "" when equal, else the report RequireSameOutputs fails with
std::string DescribeOutputDifference(const Outputs &expected, const Outputs &actual);

// "studiohdr_t.numbones (header +0x9c)" / "after the start of the 'seq' section (localseqindex = 0x1a0, +0x24)"
std::string DescribeMdlOffset(std::span<const std::byte> mdl, size_t offset);

// ---------------------------------------------------------------------------------------------------------------
// SMD / VTA reference reader (semantics: top of this file)
// ---------------------------------------------------------------------------------------------------------------

struct SmdNode {
    std::string name;
    int parent = -1;
};

struct SmdBonePose {
    smdl::Vec3 position{};
    smdl::Vec3 euler{}; // RadianEuler x, y, z as in the file
};

struct SmdFrame {
    int time = 0;                  // the `time` value in the file
    std::vector<SmdBonePose> bones; // one per node
};

struct SmdLink {
    int bone = 0;
    float weight = 0;
};

struct SmdCorner {
    int bone = 0;
    smdl::Vec3 position{};
    smdl::Vec3 normal{};
    smdl::Vec2 uv{}; // as in the file (v not inverted)
    std::vector<SmdLink> links;  // empty: bound to `bone` with weight 1
    std::vector<float> extras;   // version 3: extra UV pairs u0 v0 u1 v1 ...
};

struct SmdTriangle {
    std::string material;
    std::array<SmdCorner, 3> corners;
};

struct SmdFile {
    std::filesystem::path path;
    int version = 1;
    std::vector<SmdNode> nodes;
    std::vector<SmdFrame> frames; // skeleton frames; frames[0] = reference pose
    std::vector<SmdTriangle> triangles;
};

SmdFile ReadSmd(const std::filesystem::path &path);

// The model's bones from the SMD's nodes and frame 0 (euler form, addBoneEuler). Nodes whose name already exists in
// the model are skipped, so several SMDs sharing a skeleton (or extending it) can be added one after the other.
void AddSkeleton(smdl::Model &model, const SmdFile &smd);

enum class VertexMode { Indexed, Corners };

// The arrays FillGeometry hands to the API (no API needed to build or inspect them: tests of the reader use this)
struct GeometryData {
    std::vector<std::string> materials;       // first-seen order
    std::vector<smdl::Vec3> positions;        // per vertex
    std::vector<smdl::Vec3> normals;          // per vertex
    std::vector<std::vector<smdl::Vec2>> uvs; // uvs[set][vertex]; set 0 = u,v, 1.. = the version 3 extras
    uint32_t influencesPerVertex = 1;
    std::vector<uint32_t> weightNodes;        // vertex * influencesPerVertex entries: SMD node indices
    std::vector<float> weights;               // same layout
    std::vector<uint32_t> indices;            // 3 per triangle
    std::vector<uint32_t> triangleMaterials;  // per triangle: index into materials
    // Corners mode only (3 per triangle, file order)
    std::vector<smdl::Vec3> cornerNormals;
    std::vector<std::vector<smdl::Vec2>> cornerUvs; // [set][corner]
};

GeometryData BuildGeometryData(const SmdFile &smd, VertexMode mode);

// Sets positions/normals/uvs/weights/materials/triangles (+ corner normals/uvs in Corners mode) of the geometry.
void FillGeometry(smdl::Model &model, smdl::Geometry geometry, const SmdFile &smd, VertexMode mode);

// The SMD's frame-0 pose of every node as the geometry's rest pose (setRestPoseEuler), for sources whose bind pose
// differs from the model skeleton.
void SetGeometryRestPose(smdl::Model &model, smdl::Geometry geometry, const SmdFile &smd);

// ---- VTA (old-style vertex animation, Load_VTA / Grab_Vertexanimation) ----

struct VtaVertex {
    int index = 0; // VTA vertex number
    smdl::Vec3 position{};
    smdl::Vec3 normal{};
};

struct VtaFrame {
    int time = 0;  // `time` in the file's vertexanimation block
    int frame = 0; // time - first skeleton time: the number QC `flex ... frame N` / `defaultflex frame N` uses
    std::vector<VtaVertex> vertices; // frame 0 lists every vertex, the others only the changed ones
};

struct VtaFile {
    std::filesystem::path path;
    std::vector<SmdNode> nodes;
    std::vector<SmdFrame> skeleton;
    std::vector<VtaFrame> frames; // in file order
    int vertexCount = 0;          // highest vertex index + 1 over all frames

    const VtaFrame *findFrame(int frame) const; // by VtaFrame::frame; null when absent
};

VtaFile ReadVta(const std::filesystem::path &path);

// A shape frame as sparse deltas against the base frame (frame 0): what the compiler subtracts
// (`vanim.pos - base[vertex].pos`) for flex frame `frame`.
struct VtaDelta {
    int vertex = 0; // VTA vertex number
    smdl::Vec3 positionDelta{};
    smdl::Vec3 normalDelta{};
};

std::vector<VtaDelta> VtaFrameDeltas(const VtaFile &vta, int frame);

// Which geometry vertices each VTA vertex drives, the way the compiler maps vertex animations to model vertices
// (RemapVertexAnimations -> BuildModelToVAnimMap + BuildVAnimMap in simplify.cpp):
//  * VTA vertices j are processed in ascending order, located by their frame-0 position/normal; the candidates are
//    the geometry vertices k at squared distance <= radius^2 of that position, where the radius starts at
//    MAX_VANIM_DIST (0.3873), becomes 95% of itself after every vertex (not below 0.001 * MAX_VANIM_DIST) and doubles
//    (up to MAX_VANIM_DIST) while a search finds nothing;
//  * a candidate k is assigned to j when its distance is smaller than the best distance k has had so far, or equal
//    with a larger dot product of the geometry vertex normal and the VTA frame-0 normal (so ties of both keep the
//    lowest j);
//  * geometryVertices[j] = the geometry vertices assigned to j, ascending (several when the compiler duplicated the
//    vertex along material/normal/uv boundaries). `unmatched` counts the VTA vertices whose nearest candidate is
//    farther than sqrt(0.01) (or that have none): the compiler's "unmatched vertex anims" warning counter.
// positions/normals are the geometry's vertices (GeometryData::positions/normals): in Indexed mode the corners, in
// Corners mode the pool (equal positions of different corners are ONE vertex there).
struct VtaVertexMap {
    std::vector<std::vector<uint32_t>> geometryVertices; // [VTA vertex number] -> geometry vertex numbers
    int unmatched = 0;
};

VtaVertexMap MapVtaToGeometry(const VtaFile &vta, std::span<const smdl::Vec3> positions,
                              std::span<const smdl::Vec3> normals);

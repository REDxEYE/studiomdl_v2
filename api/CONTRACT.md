# studiomdl builder API — shared contract for all implementation agents

Repo: /mnt/new_stuff/Stuff_copy/projects/cpp/studiomdl_v2 (C++23, Valve studiomdl port). This file is in the repo
(`api/CONTRACT.md`) so it survives sessions; the integration owner keeps it current. Read first:
- `STUDIOMDL_API_PLAN.md` (the design; "Decisions" are the user's and binding),
- `AGENT_NOTES.md` sections 1-2 (build, CRLF rules: keep each file's line endings, check with `grep -c $'\r' file`),
  33-34 (library split, side effects),
- `api/src/apply.hpp`, `api/src/desc_shared.hpp`, `api/src/model.hpp`, `api/src/api_util.hpp`, `api/src/compile.cpp`,
  `api/include/studiomdl/c/smdl_common.h`, `api/include/studiomdl/cpp/{common,model,result}.hpp`.

## Layout (already exists; you fill your part's files, create new ones freely)
- C headers: `api/include/studiomdl/c/smdl_<part>.h` (one per part). Umbrella: `studiomdl_api.h`.
- C++ API (primary, since 2026-11-18): `api/include/studiomdl/cpp/<part>.hpp` declares the classes; the methods are
  implemented in `api/src/smdl_<part>.cpp`. `smdl::Model` (cpp/model.hpp) derives from `detail::Model<Part>Api` of
  every part (plain classes; their private `model()` accessor is defined in api/src/smdl_model.cpp); `smdl::Geometry`
  derives from `detail::GeometryShapesApi`, `smdl::Mesh` from `detail::MeshFlexApi`. Views (Geometry, Mesh, Animation,
  ...) hold (handle, model); Model and QcCompile own their handle (borrowed, non-owning instances for the C wrappers:
  `detail::BorrowModel` / `detail::BorrowQcCompile`). Only inline convenience overloads stay in headers.
- Implementation pattern (see `api/FLIP_BRIEF.md` and api/src/smdl_geometry.cpp, smdl_body.cpp): a method runs its
  status-style body with `RunBody(model, [&] { ... return Fail(...) / Ok(...); })`, which throws `smdl::Error`; the
  `extern "C"` function does its null/out-parameter checks and calls the method through `CallCpp(model, ...)`, which
  maps the exception back to a status and the model's last error. `api/src/desc_<part>.hpp` holds the description
  structs, `api/src/apply_<part>.cpp` the Apply* functions.
- CMake globs `api/src/*.cpp` and `tests/api/**`: never edit CMakeLists for new files, just re-run cmake.
- Shared files owned by the integration owner (do NOT edit; ask via your final report if you need a change):
  `api/src/{apply.hpp,model.hpp,api_util.hpp,compile.cpp,smdl_model.cpp,desc_shared.hpp}` except where the header
  comments give your part ownership of specific fields, `api/include/studiomdl/c/{smdl_common,smdl_model,smdl_compile}.h`,
  `api/include/studiomdl/cpp/{common,model,result}.hpp`, umbrella headers, CMake files.

## ABI rules (STUDIOMDL_API_PLAN.md "C ABI conventions")
- Prefix `smdl_<object>_<verb>` (`smdl_model_add_bone`, `smdl_geometry_set_uvs`), `SMDL_API` on every function,
  C-compatible header (compile it as C11 too), `extern "C"` blocks.
- Ids are the typedefs in smdl_common.h; out-params last (`smdl_bone_id *out_id`); handles owned by the model.
- Arrays `(const T *data, size_t count, size_t stride_bytes)`, stride 0 = packed; copied during the call
  (`smdl_api::ReadFloats/ReadUints`). Vectors `const float[3]`, quaternions `const float[4]` (x,y,z,w).
- Rotations: every call taking a rotation has a quaternion form and an `_euler` form (x,y,z radians, SMD/RadianEuler
  convention). Descriptions store RadianEuler (see desc_shared.hpp) so SMD-sourced data round-trips bit-exactly;
  convert quaternions with the same math the DMX loader uses (dmxsupport.cpp) when the call is made.
- Validate cheaply at call time (null, range, duplicate names, MAXSTUDIO* limits from studiomdl_constants.hpp /
  studio.h), store, return. Everything else is reported by the compile: from Apply code call MdlError (it always
  throws MdlCompileError) inside a `ScopedApplyObject`.
- No struct passed by value; options with many fields = one setter per field; enums with explicit values.

## Error and memory rules (STUDIOMDL_API_PLAN.md phase 0; user decision: exceptions everywhere, no hooks/mode flag)
- Errors are C++ exceptions: `MdlError`/`MdlErrorAt`/tokenizer/cmdlib `Error` throw `MdlCompileError`
  (`studiomdl/compile_error.hpp`; always, from Apply code too), tier0 fatal paths (`Error()`, LR_ABORT,
  `Plat_ExitProcess`) throw `Plat_ExitException` (`tier0/platform.h`), allocation failure is `std::bad_alloc`. Never
  call `exit()`/`_exit()` in code a compile reaches. Only entry points catch (CLI `RunStudioMdl`, dmxconvert `main`,
  repeat test, the C API compile driver); no exception crosses the C boundary (every extern "C" body through
  `smdl_api::Guard`). A failed compile still returns an `smdl_result` (status, `error_message`, `error_object`,
  the log messages), sets `smdl_model_last_error`, and writes nothing to stdout.
- An error abandons the compile; the process must stay reusable. Write exception-safe code: RAII for every
  allocation that doesn't outlive its scope (`std::vector`, `std::string`, `std::unique_ptr` with the right deleter;
  no malloc/calloc/strdup/new[] paired with free/delete in the same function), no MdlError from destructors or
  through C callbacks (qsort comparators, qhull).
- Compile-lifetime objects (sources, models, animations, the arrays and strings their structs point to) are NOT
  `c_allocate`/`calloc`/`strdup`: allocate them from `ctx.arena` (`CompileArena`, `studiomdl/compile_arena.hpp`):
  `Create<T>()` (zero-filled + value-initialized, destroyed in reverse order), `CreateArray<T>(n)` (zero-filled, only
  freed), `CopyString(text)`, `Adopt(std::unique_ptr<T>)` for an object that may still be dropped on an error path.
  Everything lives until `StudioMdlState` goes; replacing a pointer in a struct leaves the old block in the arena
  (never `free` it). Scratch datamodel elements that belong to no file are `DmElementPtr<E>` /
  `MakeScratchElement<E>(name)` (`datamodel/dmelement.h`), never a bare `CreateElement` + manual destroy.
- New code you write (API and studiomdl/ refactors) follows these rules; don't add raw owning pointers.

## Apply layer rules
- The builder must produce byte-identical output to the equivalent QC. Reuse studiomdl's own code paths: existing
  `Process*` functions, the loader helpers, and where QC logic is inline in a `Cmd_*`/`Option_*` parser, split it
  into a callable apply function that both the parser and your Apply code call (put new apply functions in
  `studiomdl/apply_<area>.cpp` + header under `include/studiomdl/` or next to the parser; keep the parser's
  behaviour byte-identical). Never copy-paste command logic.
- Geometry → source (Core): replay every triangle corner through the same lookups the SMD loader uses
  (`ParseFaceData` in v1support.cpp: vertex/normal/texcoord unification, v_list, texture overrides, null-texture
  skipping, degenerate removal, winding) by refactoring that code into a shared function, then
  `BuildIndividualMeshes`. Indexed input is expanded to corners in triangle order.
- Ordering: the driver calls parts in the order in apply.hpp; the equivalent QC for tests must issue commands in a
  compatible order.

## Concurrency rules (several agents work at once)
- Other agents edit files in `studiomdl/` concurrently. Always re-read a file region right before editing it; keep
  edits small and local; never reformat, never rewrite an existing file with `write`, never revert others' changes.
  Never `git checkout/reset/stash`.
- Build in your OWN build dir: `cmake -S . -B /tmp/v2build_<YourName> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  then `ninja -C /tmp/v2build_<YourName> <targets>`. Use it for compile checks while working (single-file:
  `ninja -C <dir> <path/to/obj>` or the compile_commands.json of your dir). Don't touch /tmp/v2build or
  /tmp/v2ref (the integration owner's).
- `tests/clamp_split` doesn't compile (known, pre-existing): build explicit targets
  (`studiomdl_v2 studiomdl_shared studiomdl_repeat_test smdl_api_tests dmxconvert`), not `all`.

## Verification each agent must do before finishing
- Your targets build with GCC; your headers compile as C11 (`gcc -std=c11 -Wall -Wextra -pedantic`) and C++20.
- CLI unchanged: `tests/tools/compare.sh /tmp/v2ref/studiomdl_v2 /tmp/v2build_<YourName>/studiomdl_v2` reports
  0 differences (84 files) and `ctest -R "^(golden|repeat_process)$"` passes in your build dir.
- Equivalence tests for your features in `tests/api/` (harness: `tests/api/harness.hpp`, see below) pass.

## Test harness contract (tests/api/, built as `smdl_api_tests`, linked against studiomdl_shared, ctest `api`)
```cpp
#include "harness.hpp"
SMDL_TEST(core_prop_static) { ... }               // self-registering test; failures throw
void Require(bool cond, std::string_view what);   // throws TestFailure with location
using Outputs = std::map<std::string, std::vector<std::byte>>;  // "models/x.mdl" -> bytes
std::filesystem::path FixturePath(std::string_view rel);        // tests/api/fixtures/<rel>
Outputs CompileQcReference(const std::filesystem::path &qc, std::vector<std::string> extraArgs = {});
                                                  // runs the studiomdl_v2 next to the test binary in a temp game dir
Outputs ResultFiles(const smdl::Result &result);
void RequireSameOutputs(const Outputs &expected, const Outputs &actual); // byte compare, reports first difference
// reference host: SMD/VTA reader driving the C++ API
struct SmdFile { ... };                            // nodes, skeleton frames (euler), triangles (per-corner vertices)
SmdFile ReadSmd(const std::filesystem::path &path);
void AddSkeleton(smdl::Model &model, const SmdFile &smd);            // bones from nodes + frame 0 (euler form)
enum class VertexMode { Indexed, Corners };       // Indexed: one vertex per corner; Corners: pooled positions+weights
void FillGeometry(smdl::Model &model, smdl::Geometry geometry, const SmdFile &smd, VertexMode mode);
```
Fixtures for API tests live in `tests/api/fixtures/` (SMD/VTA sources + QCs; generator script allowed).
Each part adds `tests/api/test_<part>.cpp` files.
Equivalence with a QC: the QC compile writes its own path into the mdl keyvalues (`qc_path`), the builder writes
none by default. Call `model.setQcPath(QcPathEntry(FixturePath(qc)))` (`smdl_model_set_qc_path`) before compiling;
`RequireSameAsQc` in `tests/api/core_helpers.hpp` does it. Degree-valued QC inputs (e.g. $definebone angles) match
the API's radians only for angles that survive `RAD2DEG(DEG2RAD(d))` (15/30/90/12 do, 5/10/20 don't).
`SMDL_TEST_DUMP=<dir>` saves expected/actual outputs on a mismatch.

## Core C API (owned by CoreBuilder; other parts code against these names now)
```c
/* smdl_skeleton.h */
smdl_status smdl_model_add_bone(smdl_model *, const char *name, smdl_bone_id parent, const float position[3],
                                const float rotation[4], smdl_bone_id *out_id);
smdl_status smdl_model_add_bone_euler(smdl_model *, const char *name, smdl_bone_id parent, const float position[3],
                                      const float euler[3], smdl_bone_id *out_id);
smdl_status smdl_model_find_bone(const smdl_model *, const char *name, smdl_bone_id *out_id);
uint32_t    smdl_model_bone_count(const smdl_model *);
/* smdl_body.h */
smdl_status smdl_model_create_mesh(smdl_model *, const char *name, smdl_mesh **out_mesh);   /* $body: one-choice group */
smdl_status smdl_model_add_bodygroup(smdl_model *, const char *name, smdl_bodygroup **out_group);
smdl_status smdl_bodygroup_create_mesh(smdl_bodygroup *, const char *name, smdl_mesh **out_mesh);
smdl_status smdl_bodygroup_add_blank(smdl_bodygroup *);
smdl_status smdl_mesh_get_geometry(smdl_mesh *, smdl_geometry **out_geometry);
smdl_status smdl_model_find_mesh(const smdl_model *, const char *name, smdl_mesh **out_mesh);
/* smdl_geometry.h */
smdl_status smdl_geometry_set_positions(smdl_geometry *, const float *xyz, size_t count, size_t stride);
smdl_status smdl_geometry_set_normals(smdl_geometry *, const float *xyz, size_t count, size_t stride);
smdl_status smdl_geometry_set_tangents(smdl_geometry *, const float *xyzw, size_t count, size_t stride);
smdl_status smdl_geometry_set_uvs(smdl_geometry *, uint32_t set, const float *uv, size_t count, size_t stride);
smdl_status smdl_geometry_set_weights(smdl_geometry *, uint32_t influences_per_vertex,
                                      const smdl_bone_id *bones, size_t bones_stride,
                                      const float *weights, size_t weights_stride, size_t vertex_count);
smdl_status smdl_geometry_add_material(smdl_geometry *, const char *name, smdl_material_id *out_id);
smdl_status smdl_geometry_set_triangles(smdl_geometry *, const uint32_t *indices, size_t triangle_count,
                                        size_t stride, const smdl_material_id *materials, size_t materials_stride);
smdl_status smdl_geometry_set_quads(smdl_geometry *, const uint32_t *indices, size_t quad_count, size_t stride,
                                    const smdl_material_id *materials, size_t materials_stride);
smdl_status smdl_geometry_set_corner_normals(smdl_geometry *, const float *xyz, size_t corner_count, size_t stride);
smdl_status smdl_geometry_set_corner_uvs(smdl_geometry *, uint32_t set, const float *uv, size_t corner_count,
                                         size_t stride);
smdl_status smdl_geometry_set_rest_pose(smdl_geometry *, smdl_bone_id bone, const float position[3],
                                        const float rotation[4]);
smdl_status smdl_geometry_set_rest_pose_euler(smdl_geometry *, smdl_bone_id bone, const float position[3],
                                              const float euler[3]);
smdl_status smdl_geometry_set_flip_triangles(smdl_geometry *, int flip);
smdl_status smdl_geometry_set_scale(smdl_geometry *, float scale);
uint32_t    smdl_geometry_vertex_count(const smdl_geometry *);
/* smdl_globals.h (excerpt; Core adds every other model-wide setting) */
smdl_status smdl_model_add_material_path(smdl_model *, const char *path);   /* $cdmaterials */
smdl_status smdl_model_set_static_prop(smdl_model *, int enabled);
smdl_status smdl_model_set_surfaceprop(smdl_model *, const char *name);
```
C++ (Core): `smdl::Geometry` (handle(), model(), setPositions(std::span<const Vec3>) / pointer+count+stride
overloads, setNormals, setTangents, setUVs(set, ...), setWeights(influencesPerVertex, std::span<const BoneId>,
std::span<const float>), addMaterial(name) -> MaterialId, setTriangles(std::span<const uint32_t> indices,
std::span<const MaterialId> materials), setQuads, setCornerNormals, setCornerUVs, setRestPose(bone, Vec3, Quat),
setRestPoseEuler(bone, Vec3, Vec3), setFlipTriangles(bool), setScale(float), vertexCount()),
`smdl::Mesh` (handle(), model(), name(), geometry()), `smdl::BodyGroup` (createMesh(name), addBlank()),
Model mixins: `addBone(name, parent, Vec3, Quat) -> BoneId`, `addBoneEuler(name, parent, Vec3, Vec3)`,
`findBone(name) -> BoneId` (throws NOT_FOUND), `boneCount()`, `createMesh(name) -> Mesh`,
`addBodyGroup(name) -> BodyGroup`, `findMesh(name)`, `addMaterialPath`, `setStaticProp`, `setSurfaceProp`.

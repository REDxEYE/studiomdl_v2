# Flip brief: move one API part from "C holds the logic" to "C++ holds the logic, C wraps it"

Self-contained instructions for one mechanical conversion. Do not read other docs; do not redesign anything.
Repo: /mnt/new_stuff/Stuff_copy/projects/cpp/studiomdl_v2. You are given ONE part: a C++ header
`api/include/studiomdl/cpp/<part>.hpp` and its implementation file(s) `api/src/smdl_<part>.cpp`. Edit only those.

## Finished examples (copy their style exactly)
- View class (an object owned by the model, like Geometry): `api/include/studiomdl/cpp/geometry.hpp` +
  `api/src/smdl_geometry.cpp`.
- Model base class (methods called on smdl::Model, like ModelBodyApi) and small views (Mesh, BodyGroup):
  `api/include/studiomdl/cpp/body.hpp` + `api/src/smdl_body.cpp`.
Open them first and compare with your files.

## Today (before)
- The header's methods are inline and call a C function: `detail::check(smdl_xxx(handle(), ...), model());`
- `api/src/smdl_<part>.cpp` defines that C function: null checks, then
  `return Guard(model, [&] { ...body using Fail(...)/Ok(...)... });`

## After
1. Header: every method whose body calls an `smdl_...` C function becomes a declaration only (keep its comment,
   name, parameters, default arguments, `const`). Inline convenience overloads that only forward to another
   method of the same class (span overloads, initializer_list overloads, argument pre-checks that throw `Error`)
   stay inline, but must call the C++ method, not the C function. Nothing in the header may call an `smdl_...`
   function any more (except `smdl_status_string`).
2. Implementation file, at the top: add `#include "studiomdl/studiomdl.hpp"` after the C header include.
3. Implementation file: for each such method, write its definition inside `namespace smdl {` (model base classes
   inside `namespace smdl { namespace detail {`), BEFORE the `extern "C" {` block. The body is the old `Guard`
   lambda body, copied VERBATIM, run with `RunBody(model, [&] { <old body> });`. To keep it verbatim, first declare
   locals with the exact names the old body uses:
   - the handle: view class `smdl_xxx *g = handle();` (use the old parameter name, e.g. `g`, `animation`);
     model base class `smdl_model *model = this->model();`. If the old body uses `model` and you are in a view
     class, add `smdl_model *model = <handle>->model;`.
   - string parameters: the C++ parameter is `std::string_view nameView`; add
     `const std::string nameStorage(nameView); const char *name = nameStorage.c_str();` (same pattern for every
     string parameter, keeping the old name).
   - out-parameters become the return value: `smdl_bone_id result = SMDL_NO_ID; smdl_bone_id *out_id = &result;`
     before `RunBody`, `return result;` after (handles: `smdl_mesh *result = nullptr; smdl_mesh **out_mesh =
     &result;` then `return Mesh(result, model);`).
   - arrays given as C++ spans in the header but as pointer + count in the old body: declare locals with the old
     names (`const float *xyz = span.data(); size_t count = span.size();`).
   - vector/quaternion parameters (`const Vec3 &position`): `const float *position_c = position.data();`, or
     use `position.data()` where the old body used the pointer.
   - Getters that only read a field (`count()`, `name()`) are one line: `return static_cast<uint32_t>(handle()->x.size());`
4. Implementation file: rewrite each C function in the `extern "C"` block as: keep its null-handle check and its
   `*out = ...` reset lines, then `return CallCpp(<model pointer>, [&] { <call the C++ method>; });`
   - view class: construct a view: `smdl::Geometry(g, g->model).setPositions(xyz, count, stride);`
     (see the `View()` helper in smdl_geometry.cpp; you may add the same helper).
   - model base class: `smdl::detail::BorrowModel(model).createMesh(name ? name : "");` (for a `const smdl_model *`
     use `const_cast<smdl_model *>(model)`).
   - C strings: pass `name ? name : ""`. Out-parameters: assign from the returned value inside the lambda
     (`if (out_id) *out_id = id;`, handles: `*out_mesh = mesh.handle();`).
   - C pointer arguments that C++ takes by reference (`const float position[3]` -> `Vec3`): check for null in
     the C function first with the same `Fail(...)` message the old body used, then build the value:
     `{position[0], position[1], position[2]}`.
   - Functions that return a value directly (counts, names): call the C++ getter, or keep reading the field.
5. Keep every error message, status code and check order EXACTLY as before. Don't touch helper functions in the
   anonymous namespace unless one did its own null check + `Guard` (then remove only that wrapper, see
   `SetRestPose` in smdl_geometry.cpp). Don't rename anything public. Don't edit other files.

## Helpers you use (api/src/api_util.hpp, already included via core_util.hpp or api_util.hpp)
- `Fail(model, status, fmt, ...)` records the message and returns status (unchanged).
- `RunBody(model, lambda)`: runs a body that returns smdl_status; throws smdl::Error on failure.
- `CallCpp(model, lambda)`: C entry point; catches smdl::Error and returns its status.
- `smdl::detail::BorrowModel(smdl_model *)`: a non-owning smdl::Model for calling model methods from C.

## Check your work
Build only your object file (other agents build theirs at the same time):
```
cmake -S . -B /tmp/v2build_flip_<part> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
ninja -C /tmp/v2build_flip_<part> api/CMakeFiles/studiomdl_api.dir/src/smdl_<part>.cpp.o
```
Then confirm no `smdl_` C function is called from your header:
`grep -n "smdl_[a-z_]*(" api/include/studiomdl/cpp/<part>.hpp` (only `smdl_status_string` may appear).
Report: the files you changed, the number of methods converted, anything you could not convert mechanically.

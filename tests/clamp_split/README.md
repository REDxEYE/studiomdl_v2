# ClampMaxVerticesPerModel test (`ctest -R clamp_split`)

The build extracts the real `ClampMaxVerticesPerModel` + `CClampedSource` code from `studiomdl_app.cpp` / `studiomdl.cpp` / `studiomdl.h`
(`extract.cmake`) and compiles it against the small shims in `shim.h`. If a marker line used by `extract.cmake` is renamed, the build of
`clamp_split_test` fails with a message naming it.

* **Differential test**: `thongs_faces.bin` is the face index array (81408 faces, 47659 vertices) captured from the Source Filmmaker
  `studiomdl.exe` just before it splits the model. The pieces must have exactly the same vertex/face counts as the compiled
  studiomdl produces (21843/31152, 21845/33609, 12171/16647), for material index 0 *and* 2 (the original SFM crashes with 2).
  It only runs when the working directory is this folder (ctest sets it).
* **Random multi-material test**: material indices that are not 0..nummeshes-1, quads, flex animations. Checks that every face stays in
  its material, `meshindex`/`texmap`/`nummeshes` are preserved in every piece, mesh vertex ranges are disjoint, the vertex animations are
  remapped to the right vertices, empty flex keys are culled and controllers are not registered again for split pieces.
* **The same with flex optimization active**: pieces never mix faces that touch a flexed vertex with static faces, pieces with flexed faces
  come first, and static pieces carry no flex keys.

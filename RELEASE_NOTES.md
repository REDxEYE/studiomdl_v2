# studiomdl_v2 — Release Notes

A cross-platform (Linux, Windows, macOS x86_64/arm64) 64-bit port of Valve's Source Filmmaker `studiomdl`, plus
`dmxconvert`.

## New features
- **`.mpp` compile (DMX model description):** pass an `.mpp` instead of a QC; Valve's `mdlcompile` loaders are restored.
  Binary and keyvalues2 both load. The format is documented in `MPP_FORMAT.md`.
- **Flex optimization** (`-flexopt auto|on|off`, `-flexopt_min_static N`, QC `$flexoptimize`): moves faces no flex touches
  into separate static meshes, keeping models under the engine's 65,535-vertex flex cache limit. Without it, flexes move
  vertices of other meshes. Prints a budget check and before/after statistics.
- **Weight fitting** (`-fitweights`, experimental): reduces each vertex to the maximum number of bone influences with a
  least-squares fit over sample poses, instead of dropping the smallest weights. Works for every source format (SMD,
  DMX, VRM, builder API) on up to 16 influences per vertex.
  Indexed builder, DMX and VRM sources fit each input vertex once before splitting material, UV and normal seams.
- **UTF-8 paths everywhere:** non-ASCII (e.g. Cyrillic) QC, source and output paths work on Windows and Linux.
- **Rig constraints in DMX:** point, orient, aim, parent and twist constraints, including targets under `DmeRig`/`DmeRigHandle`.
- **No external DLLs:** a single static executable, with no Steam or game binaries required.

## Raised limits
- Flex controllers: 128 (`MAXSTUDIOFLEXCTRL`)
- Materials/skins: 128 (`MAXSTUDIOSKINS`, was 32)
- Flexed vertices per model: 65,534 (`MAXSTUDIOFLEXVERTS`)

## Fixes vs SFM's `studiomdl.exe`
- Explicit DMX flex rules report the controller that exceeds the limit instead of an internal vector overflow.
- **Model splitting (vertex clamp):** no crash for materials other than index 0; faces keep their material; flex
  controllers and rules are no longer duplicated per piece; flex keys that move nothing in a piece are culled.
  Pieces are named `<model>_clamped<N>`.
- Hitboxes: the last hitbox is no longer written into every slot; auto-generated hitboxes have no garbage orientation.
- VTA `flexfile` was ignored; DMX animations overflowed the heap (missing root bone slot); `-t` texture replacement
  read out of bounds; `$collisionprecision` had no effect; DMX `$eyelid` didn't find its model.
- **Bones without vertices are kept when something names them:** jiggle bones, procedural bones (`$proceduralbones`
  helpers, their parents and control bones, axis interpolation and aim bones), rig constraint bones, `$controller`
  bones and explicit `$hbox` bones. SFM's compiler dropped them (an unweighted helper silently lost its rule, an
  explicit hitbox on one was a "cannot find bone" error); a bone nothing refers to is still removed. `$alwayssetup`
  bones are kept too.
- Builder API: a DMX combination control without raw controls is accepted for "advanced flex" rules (a controller
  that only feeds expressions, as the DMX loader allows); without rules the compile reports it.
- Builder API: shapes of a combination operator (DMX delta states) follow the geometry's vertices like the DMX loader
  follows position indices, so separate vertices at one position (closed lips) can move apart; they were a
  "different position deltas" compile error.
- Builder API: `smdl_geometry_set_source_name` names the source in the compile's messages and log as given
  (`body.blend`) instead of `<mesh>.smd`.
- Builder API: `smdl_geometry_set_indexed_corners` opts indexed meshes into unifying only corners of the same input
  vertex, splitting at material/normal/UV seams without scanning by position. Defaults to SMD-style welding for QC
  byte-equivalence. On glamchica, direct `Model.compile` fell from 5.75 s to 0.74 s.
- `.mpp` fixes: `mdlPath` prefix handling, `castTextureShadows`, `noForcedFade` spelling, missing `motionControl` no longer crashes.
- Output is deterministic (no runtime pointers or uninitialized bytes in the files). The MDL checksum is correct on 64-bit.

## dmxconvert vs SFM's `dmxconvert.exe`
- Same command line (`-i`, `-ie`, `-o`, `-oe`, `-of`). DMX, particle, actbusy and commentary conversions produce the same
  output as SFM's tool, and its binary files load in SFM unchanged. Element ids are real UUIDs on every platform.
- Fixed: importing `.vmt`/`.vmf`/actbusy/commentary files crashed. VMT export keeps proxy names and parameters, writes
  fallback block names and keeps the key order of the source file (SFM's export loses proxy parameters).

## Performance
- The VTX vertex-cache optimization uses meshoptimizer instead of NvTriStrip: a large model now compiles in ~1.3 s instead of ~6.5 s,
  and vertex cache efficiency is better.

## Under the hood
- Physics (`$collisionmodel`/`$collisionjoints`) via a slim built-in `vphys_lite`.
- Messages use `std::format`; one structured global state; std containers instead of fixed C arrays; scoped enums for studio flags.
- Removed: threading, Xbox/PS3 code, Perforce, material system, rendering, the MDL/data cache, the Dme undo system,
  and SFM-only Dme classes (animation sets, film clips, tracks, cameras, game models, and others). DMX files containing
  removed types still load.


## Known issues
- Old-style VTA flexes are not carried into split pieces.
- Builder API: DMX features for hosts that replace the DMX route: per-mesh combination operators (controls with stereo / n-way / eyelid kinds, corrective shapes `a_b_c`, dominators, "advanced flex" expression rules), per-vertex stereo balance (`Geometry::setBalance`), `vcafile` vertex cache animations (`Mesh::addVertexCacheAnimation`) and `Geometry::setRelativeMaterials`; byte-identical to the compile of the equivalent DMX/QC.

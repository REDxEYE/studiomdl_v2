# Qt HLMV port status

Every control from upstream HLMV exists in the Qt UI with its original id and position. This list
tracks what each one does today. **Stub** = laid out, accepts input, no effect. **Partial** = works
for some cases. Upstream behaviour lives in `upstream/mdlviewer.cpp` (menus) and
`upstream/controlpanel.cpp` (panel), keyed by the `IDC_*` id.

Backend each item needs: **R** renderer (`sfm_gl_probe/viewport.cpp`), **A** animation/sequence
playback, **M** model data (`mdl_mesh.cpp`), **P** physics (.phy), **S** sound, **C** compile.

## Menus (mdlviewer.cpp)

| Item | Status | Needs | Notes |
|---|---|---|---|
| File → Load Model..., Refresh (F5), Recent Models, Exit | Done | | |
| File → Recompile | Stub | C | Run studiomdl on the model's QC, then refresh |
| File → Run .mvscript... / Save session as .mvscript... | Stub | | Upstream script format in `MDLViewer::RunScript`/`SaveScript` |
| File → Load Background/Ground Texture, Unload Ground Texture | Disabled | | Disabled upstream too |
| Options → Background Color... | Done | | |
| Options → Light Color..., Ambient Color... | Done | | |
| Options → Ground Color... | Partial | R | Stored and saved; no ground plane to draw |
| Options → Secondary Lights | Stub | R | Toggles the check only; needs the extra HLMV lights |
| Options → Center View | Partial | | Resets our orbit camera, not HLMV's model transform |
| Options → Center Verts | Done | | Recenter on vertex bounds without changing orbit angle or zoom |
| Options → Viewmodel Mode | Stub | R | Viewmodel camera/FOV preset |
| Options → Make Screenshot... | Done | | Saves the GL framebuffer as BMP |
| View → File Associations... | Disabled | | Disabled upstream too |
| View → Show Activities / Show hidden / Show sequence numbers / Sort sequences | Stub | A | Checks toggle; must change the sequence lists' labels and order |
| View → Show orbit circle / Enable orbit yaw | Stub | R | Upstream camera modes |
| View → DotA View Mode | Stub | R | Camera preset |
| Help → Goto Homepage..., About... | Done | | |
| Ctrl-key accelerators | Partial | | Ctrl+W draws the wireframe overlay; most other accelerators toggle stub controls |
| Ctrl+Up / Ctrl+Down (tessellation) | Stub | R | No tessellation |

## Viewport

| Item | Status | Needs | Notes |
|---|---|---|---|
| Left drag orbit, middle/right drag pan, wheel zoom, Ctrl+left drag light | Done | | Controls differ from HLMV (HLMV rotates the model, right-drag zooms, Shift pans) |
| Drag-and-drop of .mdl / .qc onto the viewport | Stub | | Upstream loads models and adds QCs to the Compile tab |
| Frame slider (bottom) | Stub | A | Updates its label only |

## Render tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Render mode: Wireframe, Textured | Done | | |
| Render mode: Smoothshaded, BoneWeights, BadVertexData, UV Chart, Co-LocatedVerts | Stub | R | Draw textured |
| Specular | Done | | |
| Ground, Background, Shadow, Movement | Stub | R, A | |
| Hit Boxes, Seq. Boxes, HitBox Set/HitBox | Stub | R, M | Hitbox data is read for counts only |
| Wireframe overlay, Show Origin Axis + Axis Len | Done | | Overlay draws mesh edges over filled modes; axis shows model-space X/Y/Z in red/green/blue |
| Bones, Attachments, Normals, Tangents | Stub | R | Debug overlays |
| Physics Model + highlight bone | Stub | P, R | |
| Normal Mapping, Displacement, Software Skin, Enable Overbrightening | Stub | R | |
| Enable IK, Head Turn, Illum. Position, Play Sounds | Stub | A, S | |
| FOV | Stub | R | Our camera is orthographic |
| Inspect bone weights | Stub | R | Index list is filled |
| Included Models | Stub | M | Disabled until a model has includes |

## Sequence tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Sequence lists (5 layers) | Partial | A | Filled with sequence names; selection has no effect |
| Layer weights, frame radio buttons, sequence filters | Stub | A | |
| Pose parameters | Stub | A, M | Hidden until pose parameters are read |
| Speed scale, FPS label, blend controls, activity modifiers, animate weapons | Stub | A | |

## Model tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Model info labels | Done | | HW bones shows total bones |
| Bodypart / submodel, skin, LOD choice | Partial | M, R | Lists filled; renderer always draws bodygroup 0, skin 0, LOD 0 |
| Bone controllers | Stub | A | |
| Bodygroup presets, Roll bodygroups | Stub | M | |
| Auto LOD, LOD switch, LOD metric | Stub | R | |
| Materials used, VMTs Loaded, Shader | Partial | | Lists filled; selection, Explore to material and shader name are not wired |

## Flex tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Flex sliders and controller choices, Reset/Random/Zero/One | Done | | CPU flex; wrinkle maps ignored |

## Physics, Cloth tabs

| Item | Status | Needs | Notes |
|---|---|---|---|
| Physics bone, constraints, mass, materials, Generate QC | Stub | P | Slider labels update |
| Cloth (softbody) controls | Stub | P | |

## Bones, Attachments tabs

| Item | Status | Needs | Notes |
|---|---|---|---|
| Bone list | Partial | | Filled; selection does nothing |
| Highlight, names, surface prop, hitbox editing, .HBX load/save, Generate QC | Stub | R, M | |
| Attachment and bone lists | Partial | | Filled; selection does nothing |
| Translation/rotation fields, nudge buttons, QC string | Stub | M | Needs attachment transforms |

## IKRule, Events tabs

| Item | Status | Needs | Notes |
|---|---|---|---|
| IKRule QC string builder | Done | | Range/contact "now" buttons need A |
| Event QC string builder | Partial | S | Sound list is empty (no soundscripts loaded) |
| Event history | Stub | A | |

## Materials, Submodels, Compile tabs

| Item | Status | Needs | Notes |
|---|---|---|---|
| Material list | Partial | | Filled; parameter list and editors not wired |
| Material parameter editing, Replace VMT, Copy to clipboard | Stub | | Needs material parameter access in the renderer |
| Submodels (merged models) | Stub | M, R | |
| Compile: recent QC list, Recompile, logs, one-click toggle | Partial | C | Toggle label works only |

## Suggested order

1. Model tab choices: bodygroups, skins, LOD (data already read; renderer support only).
2. Remaining debug overlays: bones, attachments, normals/tangents, hitboxes.
3. Ground and background, FOV with a perspective camera, HLMV-style mouse controls.
4. Sequence playback (frame slider, layers, speed, pose parameters), then IK/head turn.
5. Materials tab editing, Compile tab with studiomdl, drag-and-drop.
6. Physics and cloth (.phy).

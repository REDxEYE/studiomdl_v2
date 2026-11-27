# Qt HLMV port status

All control-panel tabs use native Qt layouts, with scrollbars when needed. Controls keep their
original `IDC_*` IDs. **Stub** = laid out, accepts input, no effect. **Partial** = works for some
cases. Upstream behaviour lives in `upstream/mdlviewer.cpp` (menus) and
`upstream/controlpanel.cpp` (panel).

Start `hlmv` without arguments to choose the Source Filmmaker installation (or its `game` or
`platform/shaders/fxc` directory). HLMV remembers the selection and prompts again if it becomes invalid.
An explicit shader directory still overrides the saved location for that launch.

Backend each item needs: **R** renderer (`render/viewport.cpp`), **A** animation/sequence
playback, **M** model data (`render/mdl_mesh.cpp`), **P** physics (.phy), **S** sound, **C** compile.

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
| View → Show Activities / Show hidden / Show sequence numbers / Sort sequences | Done | | Rebuild the sequence lists, keeping the selected sequences and weights |
| View → Show orbit circle / Enable orbit yaw | Stub | R | Upstream camera modes |
| View → DotA View Mode | Stub | R | Camera preset |
| Help → Goto Homepage..., About... | Done | | |
| Ctrl-key accelerators | Partial | | Ctrl+W draws the wireframe overlay; most other accelerators toggle stub controls |
| Ctrl+Up / Ctrl+Down (tessellation) | Stub | R | No tessellation |

## Viewport

| Item | Status | Needs | Notes |
|---|---|---|---|
| Left drag orbit, middle/right drag pan, wheel zoom, Ctrl+left drag light | Done | | Controls differ from HLMV (HLMV rotates the model, right-drag zooms, Shift pans) |
| Perspective camera | Done | | Wheel zoom dollies toward the orbit target |
| Drag-and-drop of .mdl onto the viewport | Done | | The empty viewer starts with an OpenGL surface so the first drop does not switch the window's composition mode. |
| Drag-and-drop of .qc onto the viewport | Stub | C | Upstream adds QCs to the Compile tab |
| Frame slider (bottom) | Done | | Follows the layer picked by the Sequence tab's radio buttons; dragging sets the frame and stops playback, as upstream |
| Animation playback | Done | | Engine bone setup (CS:GO IBoneSetup/CIKContext, procedural and jiggle bones, $includemodel and .ani blocks) with CPU skinning; clock clamped to 100 fps like upstream |
| VertexLitGeneric / UnlitGeneric / Phong materials | Partial | | SFM vertexlit_and_unlit_generic_*30 and phong_*30 with $detail (+scale, blend mode/factor, tint), $envmap (+tint, mask, contrast, saturation), $selfillum (+tint, mask, fresnel), $halflambert, Phong $lightwarptexture, $phongwarptexture, $rimlight, $phongalbedotint; dx90 fallback blocks applied; animated VTFs show frame 0. Not done: bumped non-Phong variant ($bumpmap/$lightwarptexture without $phong), flashlight, material proxies, $envmapfresnel on non-Phong |
| VMT loading and render state | Done | | SDK filesystem (gameinfo.txt search paths via filesystem_init, case-insensitive lookups); `patch` include/insert/replace chains and dx90 fallback blocks as cmaterial.cpp; blending from EvaluateBlendRequirements ($alpha, $vertexalpha, $translucent/$alphatest with base alpha, $additive) without depth writes, alpha test (reference 0.7 by default), backface culling with $nocull, $decal, $ignorez; translucent meshes drawn after opaque ones in mesh order (studiorender's two passes). Not done: material proxies, `replacements` files |
| Teeth materials | Done | | SFM teeth_*30 / teeth_bump_*30 lit by studiorender's mouth 0 ($forward from its bone, $illumfactor from its flex), so teeth stay dark until the mouth flex opens |
| Eyes / EyeRefract materials | Done | | SFM eyes_*30 / eye_refract_*30 shaders with eyeball tracking toward the HLMV view target; Render tab's Enable Eye Look (from SFM hlmv) aims them at the camera; Eyes glint, flashlight and intro/cloak passes not rendered |

## Render tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Render mode: Wireframe, Textured | Done | | |
| Render mode: Smoothshaded, BoneWeights, BadVertexData, UV Chart, Co-LocatedVerts | Stub | R | Draw textured |
| Specular | Done | | |
| Ground, Background, Shadow, Movement | Stub | R, A | |
| Hit Boxes | Done | | Current Bones-tab hitbox set as wire boxes in HLMV's group colors; capsules draw as grown bounds |
| Seq. Boxes | Stub | A | Needs sequence selection |
| Wireframe overlay, Show Origin Axis + Axis Len | Done | | Overlay draws mesh edges over filled modes; axis shows model-space X/Y/Z in red/green/blue |
| Bones, Attachments | Done | | Current pose; cyan parent links and RGB axes scaled by Axis Len, drawn through the mesh like HLMV |
| Normals, Tangents | Done | | Blue normals; red tangents and green bitangents; length is Axis Len / 10 |
| Bone names, highlight bone | Done | | Driven by the Bones tab, as upstream |
| Physics Model + highlight bone | Stub | P, R | |
| Normal Mapping, Displacement, Software Skin, Enable Overbrightening | Stub | R | |
| Enable IK | Done | | Ground targets drop onto their floor height, as upstream with no world |
| Head Turn, Illum. Position, Play Sounds | Stub | A, S | |
| FOV | Done | | Horizontal degrees like HLMV; values outside (0, 180) are ignored |
| Inspect bone weights | Stub | R | Index list is filled |
| Included Models | Stub | M | Disabled until a model has includes |

## Sequence tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Sequence lists (5 layers), layer weights, frame radio buttons, sequence filters | Done | | Port of initSequenceChoices/setSequence/setOverlaySequence |
| Pose parameters | Done | | Up to 8 sliders with value fields; ranges from the model |
| Speed scale, FPS label, ground speed, blend sequence changes/time/Blend Now, activity modifiers | Done | | Timing and activity controls share rows to fit alongside all eight pose controls. |
| Animate weapons | Stub | M | Needs merged models |

## Model tab

| Item | Status | Needs | Notes |
|---|---|---|---|
| Model info labels | Done | | HW bones shows total bones |
| Bodypart / submodel picker, Roll bodygroups | Done | | Body index label follows the selection |
| Skin choice | Done | | Every family's materials load with the model |
| LOD choice | Partial | M, R | List filled; renderer always draws LOD 0 |
| Bone controllers | Done | | Controller list and slider, as upstream initBoneControllers |
| Bodygroup presets | Stub | M | Preset data is not read |
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
| Bone list, Highlight, Names | Done | | Names draw only while bones are shown or highlighted, as upstream |
| Surface prop, Apply to Children, Generate QC | Done | | Surface prop list is "default" plus the model's own props (no vphysics surfaceproperties) |
| Hitbox sets (choose, add, delete, rename), hitbox list, highlight, group/name, add/delete, Autogenerate | Done | | Edits live in the viewer and feed Generate QC |
| Edit Hitbox drag modes, Show Default Pose, .HBX load/save | Stub | R, P | Need a viewport drag widget, physics, and the datamodel in hlmv |
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

1. Model tab: LOD choice (renderer support only).
2. Remaining debug overlays: sequence boxes; hitbox drag editing and .HBX files.
3. Ground and background, HLMV-style mouse controls.
4. Head turn, event history, sequence boxes, Movement (root motion) display.
5. Materials tab editing, Compile tab with studiomdl, .qc drag-and-drop.
6. Physics and cloth (.phy).

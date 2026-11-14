# `.mpp`: DMX model description (QC replacement) — writer reference

Status 2026-11-05. Describes what `studiomdl_v2` reads from an `.mpp` (`studiomdl/mppsupport.cpp`, `LoadPreprocessedFile`;
collision part in `studiomdl/collisionmodel.cpp` under `MDLCOMPILE`). Element classes live in `include/mdlobjects/*.h` /
`libs/mdlobjects/*.cpp` (attribute names come from each class's `OnConstruction`). Valve's original reader is
`utils/studiomdl/dmxsupport.cpp:4864` in the CS:GO SDK; Valve's only writer is SFM's Python
`sdktools/python/global/lib/site-packages/modelpipeline/asset.py` (`Asset.assemble()`).

Compile: `studiomdl_v2 -game <gamedir> path/to/model.mpp` (the `.mpp` extension selects this path; no QC is read).

Verified (2026-11-05): an `.mpp` built from `tests/golden/fixtures/src` (Body + 1 LOD, hat bodygroup with blank, one
animation sequence) gives the same MDL structure and byte-identical VVD/VTX (checksum excepted) as the equivalent QC,
on Linux and on the Windows build under Wine. The example generator is at the end of this file.

## 1. File

- A normal DMX file. `studiomdl_v2` reads it with `RestoreFromFile` and does not check the format name.
  - Valve's pipeline wrote **keyvalues2**, format name `dmx`; the SDK also registers `mp_preprocess` (ext `mpp`, keyvalues2, v1).
  - Binary (tested: encoding `binary` 2, format `model` 1) and keyvalues2 (tested: encoding `keyvalues2` 1) both load.
    Blender Source Tools' `datamodel.py` writes invalid keyvalues2 for elements with an empty name (`"name" string`
    with no value); write `"name" "string" ""` instead or use binary. v2 prints the parse error as a WARNING but then
    goes on with the partly read file (an `.mpp` then fails later with `0003: MPP has no body groups ...`).
- Everything is **embedded**: the model/animation/collision DMX contents (their `model`, `skeleton`,
  `combinationOperator`, `animationList` elements with all children) are copied into the `.mpp` tree. Nothing is
  referenced by external path except `includeModelList` (`.mdl` files) and the LOD `path` name (see 4.3).
- **Element IDs must be unique in the file.** Blender Source Tools derives IDs from names, so two exported DMX files
  usually share IDs (e.g. both have an element "Rig"). Give every copied element a fresh UUID when merging files.
- **Element types matter.** The loader casts by type name (`CastElement`, `GetValueElement<CDmeX>`); an element of the
  wrong type (e.g. plain `DmElement`) reads as missing. Use exactly the `Dme*` type names below. The root's type does
  not matter (Valve: `DmElement` named `root`; `DmeAssetRoot` also works).
- **Attribute names and types matter.** Names are case-sensitive. Use the listed DMX types; a value with another type
  reads as the default.
- **Element names carry meaning** (sequence names, bone names, bodygroup names, ...). Each table says where.
- Attributes not listed are ignored. Missing attributes take the listed default.
- Some child elements are created automatically by the C++ constructor but **not** when a file is read
  (`InitAndCreate` is skipped while unserializing): `DmeSequence.motionControl`, `DmeSequenceBase.activity`,
  `DmeIkRule.range`, `DmeAnimCmdAlign/WalkFrame.motionControl`, `DmeSequenceCalcBlend.motionControl`. Write them when
  their values matter; when absent v2 uses "no flags"/"no activity"/"no range".

DMX type names used below: `string`, `bool`, `int`, `float`, `vector3`, `qangle`, `quaternion`, `color`, `time`,
`element`, `element_array`, `string_array`.

## 2. Tree overview

```
root (DmElement "root")
├─ mdlPath                "models/props/crate.mdl"           (output name)
├─ staticProp, skinnedLODs, ambientBoost, opacity, ...        (model flags, §3)
├─ bodyGroupList  : DmeBodyGroupList                          ($bodygroup/$model/$body, $lod)
│   └─ bodyGroupList[] : DmeBodyGroup "<bodygroup name>"
│       └─ bodyPartList[] : DmeLODList "<model name>"  | DmeBlankBodyPart "blank"
│           ├─ lodList[] : DmeLOD "<source name>"
│           │     path, switchMetric, noFlex, isShadowLOD
│           │     model : DmeModel        ┐ copied from a model .dmx root
│           │     skeleton : DmeModel     │ ("model", "skeleton", "combinationOperator")
│           │     combinationOperator     ┘
│           └─ eyeballList[] : DmeEyeball                     ($model { eyeball ... })
│   boneMergeList, boneKeepList : string_array                 ($bonemerge)
├─ eyeballGlobals : DmeEyeballGlobals                          ($eyeposition, $maxeyedeflection)
├─ materialGroupList : DmeMaterialGroupList                    ($texturegroup skins)
├─ collisionModel : DmeCollisionModel | DmeCollisionJoints     ($collisionmodel / $collisionjoints)
│     model, skeleton (copied from the physics .dmx), jointList[] : DmeCollisionJoint ...
├─ collisionText : string                                      ($collisiontext)
├─ sequenceList : DmeSequenceList                              ($sequence/$animation, $ikchain)
│   ├─ ikChainList[] : DmeIkChain
│   └─ sequences[] : DmeSequence "<name>" | DmeMultiSequence "<name>"
│         skeleton : DmeModel, animationList : DmeAnimationList   (copied from an animation .dmx)
├─ boneMaskList : DmeBoneMaskList                              ($weightlist)
├─ poseParameterList : DmePoseParameterList                    ($poseparameter)
├─ animBlockSize : DmeAnimBlockSize                            ($animblocksize)
├─ includeModelList : DmeIncludeModelList                      ($includemodel)
├─ defineBoneList : DmeDefineBoneList                          ($definebone)
├─ hitboxSetList : DmeHitboxSetList                            ($hboxset/$hbox)
├─ boneFlexDriverList : DmeBoneFlexDriverList                  ($boneflexdriver)
├─ bbox : DmeBBox                                              ($bbox)
└─ keyValues : string                                          ($keyvalues)
```

Processing order (`LoadPreprocessedRoot`): `mdlPath` → body groups (LODs, eyeballs) → collision → collision text →
material groups → bone merge/keep → *(static props stop here)* → bone masks → pose parameters → anim block size →
sequences → include models → define bones → hitboxes → bone flex drivers → key values → global flags.
At least one body group or one sequence is required (`0003: MPP has no body groups and no animations`).

## 3. Root attributes

| attribute | type | default | meaning / QC equivalent |
|---|---|---|---|
| `mdlPath` | string | — | `$modelname`. A leading `models/` (or `models\`) is stripped; relative to `<game>/models/`. If absent, the name is the `.mpp`'s directory under `<game>/models` (a trailing `preprocess`/`.preprocess` directory is skipped), mapped from content to game; e.g. `<game>/models/props/crate/preprocess/crate.mpp` → `props/crate.mdl`. |
| `staticProp` | bool | false | `$staticprop`. Requires a `bodyGroupList`; after body groups/collision/materials nothing else is read, and a single `BindPose` sequence is generated. |
| `skinnedLODs` | bool | false | `$skinnedLODs`. |
| `bodyGroupList` | element `DmeBodyGroupList` | — | geometry, §4. |
| `eyeballGlobals` | element `DmeEyeballGlobals` | — | §4.5. |
| `collisionModel` | element `DmeCollisionModel`/`DmeCollisionJoints` | — | §5. Only read when `bodyGroupList` exists. |
| `collisionText` | string | — | `$collisiontext` body: keyvalues blocks (`name { "key" "value" ... }`) copied into the `.phy` text section. |
| `materialGroupList` | element `DmeMaterialGroupList` | — | §4.6. |
| `sequenceList` | element `DmeSequenceList` | — | §6. |
| `boneMaskList` | element `DmeBoneMaskList` | — | §7.1. |
| `poseParameterList` | element `DmePoseParameterList` | — | §7.2. |
| `animBlockSize` | element `DmeAnimBlockSize` | — | §7.3. |
| `includeModelList` | element `DmeIncludeModelList` | — | §7.4. |
| `defineBoneList` | element `DmeDefineBoneList` | — | §7.5. |
| `hitboxSetList` | element `DmeHitboxSetList` | — | §7.6. Must be typed `DmeHitboxSetList` (Valve's Python wrote a plain `DmElement`, which the reader can't use). |
| `boneFlexDriverList` | element `DmeBoneFlexDriverList` | — | §7.7. |
| `keyValues` | string | — | `$keyvalues`: text placed inside `mdlkeyvalue { ... }`. |
| `bbox` | element `DmeBBox` | — | `$bbox`: `minBounds`/`maxBounds` (vector3), rotated by the up-axis rotation. |
| `opacity` | int | absent | 0 = auto, 1 = `$opaque`, 2 = `$mostlyopaque`. |
| `illuminationPosition` | vector3 | absent | `$illumposition` (rotated by the up-axis rotation). |
| `ambientBoost` | bool | false | `$ambientboost`. |
| `subdivisionSurface` | bool | false | `$subd` model flag. |
| `doNotCastShadows` | bool | false | `$donotcastshadows`. |
| `castTextureShadows` | bool | false | `$casttextureshadows` (v2 fix: Valve set the do-not-cast flag here). |
| `noForcedFade` / `noForcedFace` | bool | false | `$noforcedfade`. Both spellings accepted (`DmeAssetRoot` declares `noForcedFace`). |
| `contentsDescription` | string | absent | default `$contents`, see §8. |

Not read (Valve pipeline bookkeeping, safe to omit): `surfaceProperty` on the root (use `collisionModel.surfaceProperty`),
`relatedAssets`, `preAssembleCmds`, `postAssembleCmds`, `notes`, `nameAtCreationTime`, `dynamicLightingOrigin`, `makefile`
(printed only).

No `.mpp` equivalent: `$cdmaterials` (see §4.6 note), `$scale`, `$origin` (per sequence only), `$attachment` (attachments
come from `DmeAttachment` shapes inside the embedded models), `$surfaceprop` (except through collision/hitboxes),
`$jigglebone` (embedded `DmeJiggleBone` joints in the skeleton are read as in plain DMX).

## 4. Geometry

### 4.1 `DmeBodyGroupList`
| attribute | type | meaning |
|---|---|---|
| `bodyGroupList` | element_array `DmeBodyGroup` | one entry per bodypart, in order. |
| `boneMergeList` | string_array | `$bonemerge` bone names. |
| `boneKeepList` | string_array | bones always kept; treated like `boneMergeList`. |

### 4.2 `DmeBodyGroup` — element name = bodypart name (`$bodygroup "<name>"` / `$model "<name>"`)
| attribute | type | meaning |
|---|---|---|
| `bodyPartList` | element_array of `DmeLODList` or `DmeBlankBodyPart` | submodels in order; `DmeBlankBodyPart` (or a `DmeLODList` with no LODs) = `blank`. |

### 4.3 `DmeLODList` — element name = model name (`s_model_t` name)

The "main" body part (its source drives the `BindPose` sequence and static-prop setup) is the body part named `default`
in the body group named `default`, else the first non-empty `DmeLODList` of that group (or of the first group).

| attribute | type | meaning |
|---|---|---|
| `lodList` | element_array `DmeLOD` | LOD 0 is the one with the lowest `switchMetric` (0); others become `$lod` entries. Sorted by `switchMetric`, shadow LOD last. |
| `eyeballList` | element_array `DmeEyeball` | §4.5. |

`DmeLOD`:
| attribute | type | default | meaning |
|---|---|---|---|
| `path` | string | "" | **Source name of this LOD; must be set, unique in the file, and equal to the LOD element's name without extension** (see rules). The source becomes `<path>.dmx` internally. |
| `model` | element `DmeModel` | — | the model DMX root's `model` (meshes, joints, base states, `qcModelElements` with `DmeEyeball`/`DmeEyelid`/`DmeMouth`). Copy with all children. |
| `skeleton` | element `DmeDag` (usually the same `DmeModel`) | — | the model DMX root's `skeleton`. A LOD needs `model` or `skeleton`. |
| `combinationOperator` | element `DmeCombinationOperator` | — | the model DMX root's `combinationOperator` (flex controllers/rules); omit if none. |
| `switchMetric` | float | 0 | `$lod <distance>`; 0 for LOD 0; must be ≥ 0. |
| `noFlex` | bool | false | `$lod { nofacial }`. |
| `isShadowLOD` | bool | false | `$shadowlod`; at most one per file. |

LOD rules (verified): `$lod` model replacement matches the root LOD's element name (extension stripped) against the
root LOD's `path`. With Valve's convention (name = data path, `path` empty) LOD 1+ geometry was silently wrong.
Use `name == path` (e.g. `DmeLOD "Body"` with `path "Body"`, `DmeLOD "Body_lod1"` with `path "Body_lod1"`).
The up axis comes from the root LOD's `model.upAxis` ("Y" → rotate to Z-up, as in plain DMX); the first LOD or sequence
with a model decides it for the whole file.

### 4.4 `DmeBlankBodyPart` — no attributes (`blank`).

### 4.5 Eyes
`DmeEyeball` (in `DmeLODList.eyeballList`; element name = eyeball name). The material must be a mesh material of the
model, the parent bone a bone of the model, otherwise the eyeball is skipped with a warning. `eyes_updown` /
`eyes_rightleft` flex controllers are added if missing.
| attribute | type | default | meaning (`$model { eyeball }`) |
|---|---|---|---|
| `parentBoneName` | string | — | bone the eye is attached to |
| `materialName` | string | — | eye material (name as in the mesh; a `/` marks it as a relative path) |
| `position` | vector3 | 0 | eye center in model space (transformed into bone space) |
| `radius` | float | 0.5 | eyeball radius |
| `angle` | float | 2.0 | yaw angle in degrees (z offset = tan(angle)) |
| `irisScale` | float | 1.0 | iris scale (stored inverted) |

(Valve's Python used `parentName`/`diameter`/`pupilScale` but never wrote eyes into the `.mpp`; use the names above.)

`DmeEyeballGlobals` (root `eyeballGlobals`): `eyePosition` vector3 (`$eyeposition`, rotated by the up-axis rotation),
`maximumEyeDeflection` float degrees, default 90 (`$maxeyedeflection`).

Eyelids and mouths come from `DmeModel.qcModelElements` inside the embedded model, exactly as for plain DMX.

### 4.6 `DmeMaterialGroupList` — `$texturegroup` (one texture group)
| attribute | type | meaning |
|---|---|---|
| `materialGroups` | element_array `DmeMaterialGroup` | skin families; group 0 = default skin. |

`DmeMaterialGroup`: `materialList` string_array — material names, column-aligned across groups (entry *i* of group *n*
replaces entry *i* of group 0). Names are looked up as relative paths. Fewer entries than materials in later groups → warning.

Material search path: there is no `$cdmaterials`. Material names containing `/` are relative to `materials/`; otherwise
the directory is taken from the `.mpp` path from its `models` component on, or `models/<model name>/` (`SetSkinValues`).

## 5. Collision (`collisionModel`)

`DmeCollisionModel` (rigid, `$collisionmodel`) or `DmeCollisionJoints` (ragdoll, `$collisionjoints`; same attributes plus §5.2).
| attribute | type | default | QC |
|---|---|---|---|
| `model` | element `DmeModel` | — | required; the physics DMX root's `model` |
| `skeleton` | element `DmeDag` | — | required; the physics DMX root's `skeleton` |
| `surfaceProperty` | string | "default" | `$surfaceprop` (sets the model default) |
| `automaticMassComputation` | bool | true | `$automass`; when false `mass` is used |
| `mass` | float | 0 | `$mass` |
| `inertia` | float | 1 | `$inertia` |
| `damping` | float | 0 | `$damping` |
| `rotationalDamping` | float | 0 | `$rotdamping` |
| `drag` | float | -1 | `$drag` |
| `maxConvexPieces` | int | 40 | `$maxconvexpieces` |
| `remove2d` | bool | false | `$remove2d` |
| `weldPositionTolerance` | float | 0 | `$weldposition` |
| `weldNormalTolerance` | float | 0.999 | `$weldnormal` |
| `concave` | bool | false | `$concave` |
| `forceMassCenter` / `massCenter` | bool / vector3 | false / 0 | `$masscenter` |
| `assumeWorldSpace` | bool | false | `$assumeworldspace` |

### 5.2 `DmeCollisionJoints` extra attributes (ignored for static props)
| attribute | type | default | QC |
|---|---|---|---|
| `jointList` | element_array `DmeCollisionJoint` | — | per-bone settings; element name = bone name |
| `rootBone` | string | — | `$rootbone` |
| `jointSkipList` | string_array | — | `$jointskip` |
| `concavePerJoint` | bool | false | `$concaveperjoint` |
| `selfCollisions` | bool | true | false = `$noselfcollisions` |
| `animatedFriction` | element `DmeJointAnimatedFriction` | — | `$animatedfriction`: `minFriction` int, `maxFriction` int, `timeIn`/`timeHold`/`timeOut` time |
| `boneFollower` | bool | false | not read |

`DmeCollisionJoint` (name = bone): `massBias` float 1 (`$jointmassbias`), `damping` float 0 (`$jointdamping`),
`rotationalDamping` float 0 (`$jointrotdamping`), `inertia` float 1 (`$jointinertia`), `constrainX`/`constrainY`/`constrainZ`
element `DmeJointConstrain` (`$jointconstrain`), `jointMergeList` string_array (`$jointmerge`), `jointCollideList`
string_array (`$jointcollide`).

`DmeJointConstrain`: `type` int (0 free, 1 fixed, 2 limit; default 1), `minAngle` float, `maxAngle` float, `friction` float.

## 6. Sequences (`sequenceList`)

`DmeSequenceList`: `sequences` element_array of `DmeSequence`/`DmeMultiSequence`; `ikChainList` element_array `DmeIkChain`.

A `BindPose` sequence is always created first from the main body part's bind pose (Valve behaviour; QC compiles don't
have it). Simple sequences are processed before multi sequences, and a simple sequence referenced by another one
(align/subtract/layer) before the referrer. Duplicate sequence names are an error; references between sequences are by element
(the target's name is looked up).

### 6.1 Attributes of both kinds (`DmeSequenceBase`) — element name = sequence name (extension stripped)
| attribute | type | default | QC `$sequence` option |
|---|---|---|---|
| `activity` | element `DmeSequenceActivity` | — | `activity`: element name = activity name (`ACT_...`), `weight` int (default 1), `modifierList` string_array (`activitymodifier`) |
| `loop` | bool | false | `loop` |
| `snap` | bool | false | `snap` |
| `post` | bool | false | `post` |
| `hidden` | bool | false | `hidden` |
| `delta` | bool | false | `delta` (also sets post) |
| `worldSpace` | bool | false | `worldspace` (also sets post) |
| `preDelta` | bool | false | `predelta` |
| `autoPlay` | bool | false | `autoplay` |
| `realtime` | bool | false | `realtime` |
| `fadein` / `fadeout` | float | 0.2 | `fadein` / `fadeout` |
| `entryNode` / `exitNode` / `reverseNodeTransition` | string / string / bool | — | `node` (entry only), `transition` (both), `rtransition` (both + reverse) |
| `animationEventList` | element_array `DmeAnimationEvent` | — | `{ event }`: element name = event name/number, `frame` int, `dataString` string (options) |
| `layerList` | element_array `DmeSequenceAddLayer`/`DmeSequenceBlendLayer` | — | `addlayer` / `blendlayer`, §6.4 |
| `ikLockList` | element_array `DmeIkLock` | — | `iklock`: `ikChain` element `DmeIkChain`, `lockPosition` float 0–1, `lockRotation` float 0–1 |
| `keyValues` | string | — | sequence `keyvalues` text |

### 6.2 `DmeSequence` (one animation)
| attribute | type | default | meaning |
|---|---|---|---|
| `skeleton` | element `DmeModel` | — | **required**; the animation DMX root's `skeleton` (its `upAxis` may set the file's up axis) |
| `animationList` | element `DmeAnimationList` | — | the animation DMX root's `animationList`; absent = single-frame sequence from the skeleton pose |
| `fps` | float | 30 | `fps` |
| `origin` | vector3 | 0 | `origin` |
| `scale` | float | 1 | `scale` |
| `startLoop` | int | 0 | `startloop` |
| `forceLoop` | bool | false | false sets `STUDIO_NOFORCELOOP` |
| `autoIk` | bool | false | `autoik` (false = `noautoik`) |
| `motionRollback` | float | 0.3 | `motionrollback` |
| `animBlocks` | bool | true | false = `noanimblock` |
| `animBlockStall` | bool | true | false = `noanimblockstall` |
| `motionControl` | element `DmeMotionControl` | — | motion extraction flags (`LX LY ...`), §6.5 |
| `animationCommandList` | element_array `DmeAnimCmd*` | — | `$animation` commands, §6.3 |
| `ikRuleList` | element_array `DmeIk*Rule` | — | `ikrule`, §6.6 |
| `boneMask` | element `DmeBoneMask` | — | not read |

### 6.3 Animation commands (`animationCommandList`)
| type | attributes | QC |
|---|---|---|
| `DmeAnimCmdFixupLoop` | `startFrame` int, `endFrame` int | `fixuploop` |
| `DmeAnimCmdWeightList` | `weightListName` string (a `boneMaskList` entry) | `weightlist` |
| `DmeAnimCmdSubtract` | `animation` element (sequence), `frame` int | `subtract` |
| `DmeAnimCmdPreSubtract` | as Subtract | `presubtract` |
| `DmeAnimCmdAlign` | `animation` element (sequence), `boneName` string (empty = whole), `sourceFrame` int, `destinationFrame` int, `motionControl` element | `alignto`/`alignbone` |
| `DmeAnimCmdRotateTo` | `angle` float | `rotateto` |
| `DmeAnimCmdWalkFrame` | `endFrame` int, `motionControl` element | `walkframe` |
| `DmeAnimCmdDerivative` | `scale` float (1) | `derivative` |
| `DmeAnimCmdLinearDelta` | — | `lineardelta` |
| `DmeAnimCmdSplineDelta` | — | `splinedelta` (v2 and Valve map it to the linear-delta command) |
| `DmeAnimCmdCompress` | `skipFrames` int | `compress` |
| `DmeAnimCmdNumFrames` | `frames` int | `numframes` |
| `DmeAnimCmdLocalHierarchy` | `boneName`, `parentBoneName` string, `startFrame`/`peakFrame`/`tailFrame`/`endFrame` float | `localhierarchy` |
| `DmeAnimCmdNoAnimation` | — | `noanimation` |

Subtract/PreSubtract/LinearDelta/SplineDelta also mark the sequence as delta.

### 6.4 Layers
`DmeSequenceAddLayer`: `animation` element (sequence) — `addlayer`.
`DmeSequenceBlendLayer`: `animation`, `startFrame`/`peakFrame`/`tailFrame`/`endFrame` float, `spline`, `crossfade`,
`noBlend`, `local` bool, `poseParameterName` string — `blendlayer`.

### 6.5 `DmeMotionControl`
bools `X Y Z XR YR ZR LX LY LZ LXR LYR LZR LM` → the matching `STUDIO_*` motion flags (QC `X`, `LX`, ... options).

### 6.6 IK
`DmeIkChain` (in `ikChainList`; element name = chain name, `$ikchain`): `endJoint` string, `height` float 18, `pad` float
(radius = pad/2), `floor` float, `knee` vector3, `center` vector3.

IK rules (`ikRuleList`), all with `ikChain` element (required, must also be in `ikChainList`), `range` element
`DmeIkRange` (`startFrame`, `maxStartFrame`, `maxEndFrame`, `endFrame` int → start/peak/tail/end) and `useType` int
(0 none, 1 sequence (default), 2 source):
| type | extra attributes | QC `ikrule` |
|---|---|---|
| `DmeIkTouchRule` | `boneName` string | `touch` |
| `DmeIkFootstepRule` | `height`, `floor`, `pad` float, `contact` int (defaults from the chain) | `footstep` |
| `DmeIkAttachmentRule` | `attachmentName` string, `fallbackBone` string, `fallbackPosition` vector3, `fallbackRotation` quaternion (`radius` exists but is not read) | `attachment` |
| `DmeIkReleaseRule` | — | `release` |

### 6.7 `DmeMultiSequence` (blends)
| attribute | type | default | meaning |
|---|---|---|---|
| `sequenceList` | element_array `DmeSequence` | — | blended animations (must be sequences in this file) |
| `blendWidth` | int | 0 | `blendwidth` |
| `blendList` | element_array `DmeSequenceBlend`/`DmeSequenceCalcBlend` | — | up to 2 blend axes |
| `blendRef` / `blendComp` / `blendCenter` | element `DmeSequence` | — | `blendref` / `blendcomp` / `blendcenter` |

`DmeSequenceBlend`: `poseParameterName` string, `paramStart`, `paramEnd` float — `blend`.
`DmeSequenceCalcBlend`: `poseParameterName`, `attachmentName` string, `motionControl` element — `calcblend`.

## 7. Other model data

### 7.1 `DmeBoneMaskList` — `$weightlist`
`boneMaskList` element_array `DmeBoneMask`; `defaultBoneMask` element (one of them; becomes `$defaultweightlist`).
`DmeBoneMask` (name = weight list name): `boneWeightList` element_array `DmeBoneWeight` (name = bone name, `weight`
float; optional `positionWeight` float for a separate position weight).

### 7.2 `DmePoseParameterList` — `$poseparameter`
`poseParameterList` element_array `DmePoseParameter` (name = pose parameter): `min`, `max` float, `wrap` bool,
`loop` bool + `loopRange` float.

### 7.3 `DmeAnimBlockSize` — `$animblocksize`
`size` int (values < 1024 are KB), `stall` bool (default true; false = `nostall`), `storageType` int (0 low-res default,
1 high-res).

### 7.4 `DmeIncludeModelList` — `$includemodel`
`includeModels` string_array of model paths, relative to `models/` (e.g. `"humans/male_shared.mdl"`).

### 7.5 `DmeDefineBoneList` — `$definebone`
`defineBones` element_array `DmeDefineBone` (name = bone): `parent` string, `translation` vector3, `rotation` qangle,
`realignTranslation` vector3, `realignRotation` qangle (non-zero realign marks the bone pre-aligned),
`contentsDescription` string (per-bone `$jointcontents`, §8).

### 7.6 `DmeHitboxSetList` — `$hboxset` / `$hbox`
`hitboxSetList` element_array `DmeHitboxSet` (name = set name, e.g. `default`): `hitboxList` element_array `DmeHitbox`
(name = hitbox name): `boneName` string, `groupId` int, `minBounds`/`maxBounds` vector3, `surfaceProperty` string
(per-bone `$jointsurfaceprop`; conflicting values warn).

### 7.7 `DmeBoneFlexDriverList` — `$boneflexdriver`
`boneFlexDriverList` element_array `DmeBoneFlexDriver` (`boneName` string, `controlList` element_array
`DmeBoneFlexDriverControl`: `flexControllerName` string, `boneComponent` int 0–2 = tx/ty/tz, `min` float 0, `max` float 1).
Only one list per model.

## 8. `contentsDescription` values
Underscore-separated, order-insensitive, case-insensitive: `notsolid`, `solid`, `monster`, `ladder`, `grate` and the
combinations `solid_monster`, `solid_ladder`, `solid_monster_ladder`, `grate_monster`, `grate_ladder`,
`grate_monster_ladder`. Root: model default contents; `DmeDefineBone`: per bone.

## 9. Example writer (Python, Blender Source Tools `datamodel.py`)

Builds the verified test model from `tests/golden/fixtures/src` (binary DMX, fresh IDs, `name == path` LODs).

```python
import sys, uuid
sys.path.insert(0, "<blender addons>/io_scene_valvesource")
import datamodel

SRC = "tests/golden/fixtures/src/"
def load(name):                       # source DMX root; fresh IDs so files can be merged
    src = datamodel.load(SRC + name)
    for e in src.elements:
        e._id = uuid.uuid4()
    return src.root

body, body_lod1, hat, rig = load("Body.dmx"), load("Body_lod1.dmx"), load("Hat.dmx"), load("anims/Rig.dmx")

dm = datamodel.DataModel("model", 1)
dm.allow_random_ids = True
root = dm.add_element("root", id=None)
root["mdlPath"] = "models/golden/mpp_character.mdl"

def lod(name, src, metric):
    e = dm.add_element(name, "DmeLOD", id=None)
    e["path"] = name                  # == element name: needed for $lod replacement
    e["model"], e["skeleton"] = src["model"], src["skeleton"]
    if "combinationOperator" in src:
        e["combinationOperator"] = src["combinationOperator"]
    e["switchMetric"] = metric
    return e

def lod_list(name, lods):
    e = dm.add_element(name, "DmeLODList", id=None)
    e["lodList"] = datamodel.make_array(lods, datamodel.Element)
    e["eyeballList"] = datamodel.make_array([], datamodel.Element)
    return e

def group(name, parts):
    e = dm.add_element(name, "DmeBodyGroup", id=None)
    e["bodyPartList"] = datamodel.make_array(parts, datamodel.Element)
    return e

bgl = dm.add_element("bodyGroupList", "DmeBodyGroupList", id=None)
bgl["bodyGroupList"] = datamodel.make_array([
    group("Body", [lod_list("Body", [lod("Body", body, 0.0), lod("Body_lod1", body_lod1, 20.0)])]),
    group("hat", [dm.add_element("blank", "DmeBlankBodyPart", id=None), lod_list("Hat", [lod("Hat", hat, 0.0)])]),
], datamodel.Element)
root["bodyGroupList"] = bgl

seq = dm.add_element("wave", "DmeSequence", id=None)
seq["skeleton"], seq["animationList"] = rig["skeleton"], rig["animationList"]
seq["fps"], seq["loop"] = 30.0, True
sl = dm.add_element("sequenceList", "DmeSequenceList", id=None)
sl["sequences"] = datamodel.make_array([seq], datamodel.Element)
root["sequenceList"] = sl

dm.root = root
dm.write("mpp_character.mpp", "binary", 2)
```

Equivalent QC:
```
$modelname "golden/mpp_character.mdl"
$model "Body" "Body.dmx"
$lod 20 { replacemodel "Body" "Body_lod1.dmx" }
$bodygroup "hat" { blank studio "Hat.dmx" }
$sequence "wave" "anims/Rig.dmx" fps 30 loop
```

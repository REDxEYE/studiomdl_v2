// Model description objects shared by several parts. Owned by the integration owner; the owning part may add
// fields to its own structs (noted below), every other part only reads the fields declared here.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "mathlib/vector4d.h"
#include "mathlib/mathlib.h"
#include "studiomdl/c/smdl_common.h"

// ---------------------------------------------------------------------------------------------------------------
// Skeleton (model-wide). Per-bone options live in the Core part's BoneOptionsDesc, jiggle/procedural in
// BoneDynamicsDesc. Rotations are stored as RadianEuler, the representation s_bone_t / SMD files use, so a host
// passing SMD euler angles (the *_euler API variants) gets bit-identical results; quaternion inputs are converted
// once, when the call is made.
struct BoneDesc {
    std::string name;
    smdl_bone_id parent = SMDL_NO_BONE;
    Vector position{0, 0, 0};          // rest pose, local to the parent
    RadianEuler rotation{0, 0, 0};
};

struct SkeletonDesc {
    std::vector<BoneDesc> bones;

    std::optional<smdl_bone_id> find(std::string_view name) const; // exact (case-sensitive) name
    bool valid(smdl_bone_id id) const { return id < bones.size(); }
};

// ---------------------------------------------------------------------------------------------------------------
// Geometry: the source data of one mesh, LOD or collision model (what an SMD/DMX file holds). Core owns the
// vertex/triangle fields and may add more; Flex owns `shapes`.
inline constexpr size_t kMaxUvSets = 8;

struct GeometryShapeDesc {
    std::string name;
    std::vector<uint32_t> vertices;      // indices into positions (sparse)
    std::vector<Vector> positionDeltas;  // one per entry of `vertices`
    std::vector<Vector> normalDeltas;    // empty, or one per entry
    std::vector<float> wrinkle;          // empty, or one per entry
};

struct RestPoseOverride {
    smdl_bone_id bone;
    Vector position;
    RadianEuler rotation;
};

struct smdl_geometry {
    smdl_model *model = nullptr;
    std::string owner; // what it belongs to, for messages ("mesh 'crate'", "lod 1 / mesh 'crate'", "collision")
    // Name of the source built from this geometry (BuildSourceFromGeometry): "<sourceName>.smd" is the source's file
    // name, the name sequences and LODs look it up by. Empty = derived from `owner`. Meshes set it to the mesh name.
    std::string sourceName;
    // Source file name for messages and the log, used as it is (smdl_geometry_set_source_name); empty = derived above
    std::string sourceFile;
    bool indexedCorners = false; // opt-in: keep geometry vertex identities instead of SMD position-based welding

    // indexed vertices (all arrays have positions.size() entries, or are empty when optional)
    std::vector<Vector> positions;
    std::vector<Vector> normals;
    std::vector<Vector4D> tangents;                        // optional, w = handedness
    std::array<std::vector<Vector2D>, kMaxUvSets> uvs;     // optional per set
    uint32_t influencesPerVertex = 0;                      // weightBones/weights hold positions.size() * this
    std::vector<smdl_bone_id> weightBones;
    std::vector<float> weights;

    // triangles
    std::vector<uint32_t> indices;                        // 3 per triangle
    std::vector<smdl_material_id> triangleMaterials;      // 1 per triangle
    std::vector<std::string> materials;                   // smdl_material_id -> material name

    // quads for $subd models (4 indices per quad), optional
    std::vector<uint32_t> quadIndices;
    std::vector<smdl_material_id> quadMaterials;

    // per-corner overrides (3 per triangle, empty when unused)
    std::vector<Vector> cornerNormals;
    std::array<std::vector<Vector2D>, kMaxUvSets> cornerUvs;

    std::vector<RestPoseOverride> restOverrides;           // bones whose bind pose differs in this geometry

    std::vector<GeometryShapeDesc> shapes;                 // Flex part
    std::vector<float> balance;                            // Flex part: stereo balance per vertex (DmeVertexData "balance"), empty = none

    bool flipTriangles = false;
    bool relativeMaterials = false;                        // material names are relative to materials/ (DMX materials, SMD version 2)
    float scale = 0.0f;                                    // 0 = the model's $scale (like a `studio` without `scale`)

    // $appendsource: set on the geometries created by smdl_mesh_append_geometry
    bool appended = false;
    bool hasAppendOffset = false;
    Vector appendPosition{0, 0, 0};
    Vector appendAngles{0, 0, 0};                          // QAngle degrees (pitch, yaw, roll)
    float appendScale = 1.0f;
};

// ---------------------------------------------------------------------------------------------------------------
// Body groups and meshes. Core owns these structs and may add fields.
struct smdl_mesh {
    smdl_model *model = nullptr;
    std::string name;
    smdl_bodygroup *group = nullptr;
    smdl_geometry *geometry = nullptr; // always set: created with the mesh
    std::vector<smdl_geometry *> appended; // $appendsource geometries, merged into this mesh in order
};

struct smdl_bodygroup {
    smdl_model *model = nullptr;
    std::string name;
    std::vector<smdl_mesh *> choices; // nullptr = a blank choice
};

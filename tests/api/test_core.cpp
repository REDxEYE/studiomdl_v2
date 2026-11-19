// Core part tests: builder output must be byte-identical to the equivalent QC compile (fixtures core_*.qc), plus the
// call-time checks and the errors of the compile. The QCs name the meshes by their studio path ("src/crate"), so the
// builder's meshes carry the same name; the groups are named like the $body / $bodygroup.
#include "core_helpers.hpp"

#include <cmath>
#include <cstring>
#include <numbers>

using namespace coretest;

// ---------------------------------------------------------------------------------------------------------------
// Equivalence with the QC compile
// ---------------------------------------------------------------------------------------------------------------

// A static prop without a sequence of its own gets the BindPose sequence (the rule of ApplyFinish)
SMDL_TEST(core_prop_static_indexed) {
    smdl::Model model("api/core_prop_static");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
    RequireSameAsQc(model, "core_prop_static.qc");
}

SMDL_TEST(core_prop_static_corners) {
    smdl::Model model("api/core_prop_static");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    AddBody(model, "crate", "src/crate", kCrate, VertexMode::Corners);
    RequireSameAsQc(model, "core_prop_static.qc");
}

SMDL_TEST(core_prop_multi_indexed) {
    smdl::Model model("api/core_prop_multi");
    BasicGlobals(model, "wood");
    NoSequences(model);
    AddBody(model, "house", "src/house", "src/house.smd", VertexMode::Indexed);
    RequireSameAsQc(model, "core_prop_multi.qc");
}

SMDL_TEST(core_prop_multi_corners) {
    smdl::Model model("api/core_prop_multi");
    BasicGlobals(model, "wood");
    NoSequences(model);
    AddBody(model, "house", "src/house", "src/house.smd", VertexMode::Corners);
    RequireSameAsQc(model, "core_prop_multi.qc");
}

SMDL_TEST(core_skinned_indexed) {
    smdl::Model model("api/core_skinned");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    AddBody(model, "tube", "src/chain_ref", "src/chain_ref.smd", VertexMode::Indexed);
    RequireSameAsQc(model, "core_skinned.qc");
}

SMDL_TEST(core_skinned_corners) {
    smdl::Model model("api/core_skinned");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    AddBody(model, "tube", "src/chain_ref", "src/chain_ref.smd", VertexMode::Corners);
    RequireSameAsQc(model, "core_skinned.qc");
}

// Up to 4 influences per vertex with real tails; with 100 poses the fit beats the plain trim on this mesh, so the
// fitted weights are what both sides write
static void BuildFitWeights(smdl::Model &model, VertexMode mode, bool fit) {
    BasicGlobals(model, "flesh");
    NoSequences(model);
    AddBody(model, "tube", "src/fit_ref", "src/fit_ref.smd", mode);
    if (fit)
        model.setFitWeights(true, 100, 100);
}

SMDL_TEST(core_fitweights_indexed) {
    smdl::Model model("api/core_fitweights");
    BuildFitWeights(model, VertexMode::Indexed, true);
    RequireSameAsQc(model, "core_fitweights.qc", {"-fitweights", "100"});
}

SMDL_TEST(core_fitweights_corners) {
    smdl::Model model("api/core_fitweights");
    BuildFitWeights(model, VertexMode::Corners, true);
    RequireSameAsQc(model, "core_fitweights.qc", {"-fitweights", "100"});
}

// The weights in the .vvd are the fit, not the trim the loaders store
SMDL_TEST(core_fitweights_changes_weights) {
    smdl::Model trimmed("api/core_fitweights");
    BuildFitWeights(trimmed, VertexMode::Indexed, false);
    RequireSameAsQc(trimmed, "core_fitweights.qc");
    smdl::Model fitted("api/core_fitweights");
    BuildFitWeights(fitted, VertexMode::Indexed, true);
    const Outputs a = ResultFiles(trimmed.compile()), b = ResultFiles(fitted.compile());
    Require(a.at("models/api/core_fitweights.vvd") != b.at("models/api/core_fitweights.vvd"),
            "-fitweights leaves the .vvd weights unchanged");
}

// Two $bodygroups (studio + studio, studio + blank), presets and a 2-family $texturegroup, $body first
static void BuildBodyGroups(smdl::Model &model, VertexMode mode) {
    BasicGlobals(model, "flesh");
    NoSequences(model);
    AddBody(model, "base", "src/bg_base", "src/bg_base.smd", mode);

    smdl::BodyGroup hat = model.addBodyGroup("hat");
    AddMesh(model, hat, "src/bg_hat", "src/bg_hat.smd", mode);
    AddMesh(model, hat, "src/bg_hat_tall", "src/bg_hat_tall.smd", mode);

    smdl::BodyGroup gloves = model.addBodyGroup("gloves");
    AddMesh(model, gloves, "src/bg_gloves", "src/bg_gloves.smd", mode);
    gloves.addBlank();

    model.addSkinFamily({"bg_body", "bg_cloth", "bg_glove"});
    model.addSkinFamily({"bg_body_alt", "bg_cloth_alt", "bg_glove_alt"});

    model.addBodyGroupPreset("formal", {{hat, 1}, {gloves, 0}});
    model.addBodyGroupPreset("casual", {{hat, 0}, {gloves, 1}});
}

SMDL_TEST(core_bodygroups_indexed) {
    smdl::Model model("api/core_bodygroups");
    BuildBodyGroups(model, VertexMode::Indexed);
    RequireSameAsQc(model, "core_bodygroups.qc");
}

SMDL_TEST(core_bodygroups_corners) {
    smdl::Model model("api/core_bodygroups");
    BuildBodyGroups(model, VertexMode::Corners);
    RequireSameAsQc(model, "core_bodygroups.qc");
}

// A second source whose bind pose differs from the model skeleton's: the geometry carries its own rest pose
SMDL_TEST(core_rest_pose_override) {
    smdl::Model model("api/core_restpose");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    AddBody(model, "tube", "src/chain_ref", "src/chain_ref.smd", VertexMode::Indexed);

    const SmdFile rest = ReadSmd(FixturePath("src/core_chain_rest.smd"));
    smdl::BodyGroup alt = model.addBodyGroup("alt");
    smdl::Mesh mesh = alt.createMesh("src/core_chain_rest");
    FillGeometry(model, mesh.geometry(), rest, VertexMode::Indexed);
    SetGeometryRestPose(model, mesh.geometry(), rest);
    RequireSameAsQc(model, "core_restpose.qc");
}

// Without the override the geometry uses the model skeleton's pose and the output differs from the QC: the override
// is what makes the second source keep its own bind pose
SMDL_TEST(core_rest_pose_override_is_needed) {
    smdl::Model model("api/core_restpose");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    AddBody(model, "tube", "src/chain_ref", "src/chain_ref.smd", VertexMode::Indexed);
    smdl::BodyGroup alt = model.addBodyGroup("alt");
    AddMesh(model, alt, "src/core_chain_rest", "src/core_chain_rest.smd", VertexMode::Indexed);
    const Outputs reference = CompileQcReference(FixturePath("core_restpose.qc"));
    Require(!DescribeOutputDifference(reference, ResultFiles(model.compile())).empty(),
            "a geometry without its rest pose must not compile like the QC source with another bind pose");
}

// The options of studio: reverse and scale
SMDL_TEST(core_flip_and_scale) {
    smdl::Model model("api/core_options");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    smdl::Mesh mesh = AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
    mesh.geometry().setFlipTriangles(true);
    mesh.geometry().setScale(2.0f);
    RequireSameAsQc(model, "core_options.qc");
}

// $appendsource <file> "offset pos[...] angle[...] scale[...]"
SMDL_TEST(core_append_source) {
    smdl::Model model("api/core_appendsource");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    smdl::Mesh mesh = AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
    smdl::Geometry appended = mesh.appendGeometry();
    FillGeometry(model, appended, ReadSmd(FixturePath(kCrate)), VertexMode::Indexed);
    appended.setAppendOffset({20.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 90.0f}, 1.0f);
    RequireSameAsQc(model, "core_appendsource.qc");
}

// $overridematerial after the body
SMDL_TEST(core_override_material) {
    smdl::Model model("api/core_override");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
    model.overrideMaterial("crate_override");
    RequireSameAsQc(model, "core_override.qc");
}

// -t <replacement> of the command line
SMDL_TEST(core_texture_replacement) {
    smdl::Model model("api/core_prop_static");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    model.addTextureReplacement("crate_replaced");
    AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
    RequireSameAsQc(model, "core_prop_static.qc", {"-t", "crate_replaced"});
}

// Model globals: surfaceprop, contents, flags, eye position, cdmaterials, keyvalues ... in the order the QC states them
SMDL_TEST(core_globals) {
    smdl::Model model("api/core_globals");
    model.setInternalName("api/core_globals_internal");
    model.addMaterialPath("models/api");
    model.addMaterialPath("models/api/extra");
    model.setScale(1.5f);
    model.setOrigin({1.0f, 2.0f, 3.0f});
    model.setOriginAngle(10.0f);
    model.setSurfaceProp("metal");
    model.setContents(smdl::ContentsGrate | smdl::ContentsLadder);
    model.setAmbientBoost(true);
    model.setNoForcedFade(true);
    model.setDoNotCastShadows(true);
    model.setCastTextureShadows(true);
    model.setConstantDirectionalLight(true, 0.5f);
    model.setObsolete(true);
    model.setMostlyOpaque(true);
    model.setEyePosition({1.0f, 2.0f, 3.0f});
    model.setIllumPosition({4.0f, 5.0f, 6.0f});
    model.setMaxEyeDeflection(40.0f);
    model.setGamma(2.0f);
    model.setBBox({-10.0f, -10.0f, 0.0f}, {10.0f, 10.0f, 20.0f});
    model.setCBox({-5.0f, -5.0f, 0.0f}, {5.0f, 5.0f, 10.0f});
    model.setBBoxOnlyVerts(true);
    model.setSkipBoneInBBox(true);
    model.setCollisionPrecision(0.5f);
    model.addKeyValues("prop_data { base Wooden.Large }");
    model.addKeyValues("second { key value }");
    model.setPreserveTriangleOrder(true);
    model.setClipToTextures(true);
    model.setDefaultFadeInTime(0.2f);
    model.setDefaultFadeOutTime(0.3f);
    model.setMotionRollback(0.5f);
    model.setClampWorldspace({-1000.0f, -1000.0f, -1000.0f}, {1000.0f, 1000.0f, 1000.0f});
    model.setSectionFrames(20, 10);
    model.addAllowedActivityName("ACT_CORE_TEST");
    NoSequences(model);
    const smdl::Mesh mesh = AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);

    // per bone values need the bone: $jointsurfaceprop / $jointcontents of the root bone
    model.setBoneSurfaceProp(model.findBone("root"), "glass");
    model.setBoneContents(model.findBone("root"), smdl::ContentsMonster);
    (void) mesh;
    RequireSameAsQc(model, "core_globals.qc");
}

// A QC and the builder with the same flags: a flag turned off again leaves the default
SMDL_TEST(core_flag_off_is_default) {
    smdl::Model model("api/core_prop_static");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    model.setOpaque(true);
    model.setOpaque(false);
    model.setObsolete(true);
    model.setObsolete(false);
    AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
    RequireSameAsQc(model, "core_prop_static.qc");
}

// ---------------------------------------------------------------------------------------------------------------
// Rotations: quaternion and euler input of one model
// ---------------------------------------------------------------------------------------------------------------

namespace {
    constexpr float kPi = std::numbers::pi_v<float>;

    // The crate on a 3 bone skeleton whose rotations convert exactly between the two forms: identity and a half turn
    // about Z. Bones are added in the euler form (`euler`) or the quaternion form.
    smdl::Model BuildRotated(bool euler) {
        smdl::Model model("api/core_rotated");
        BasicGlobals(model, "wood");
        NoSequences(model);
        const smdl::BoneId root = euler ? model.addBoneEuler("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0})
                                        : model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});
        const smdl::BoneId arm = euler ? model.addBoneEuler("arm", root, {0, 0, 8}, {0, 0, kPi})
                                       : model.addBone("arm", root, {0, 0, 8}, {0, 0, 1, 0});
        euler ? model.addBoneEuler("hand", arm, {0, 0, 8}, {0, 0, 0}) : model.addBone("hand", arm, {0, 0, 8}, {0, 0, 0, 1});

        const SmdFile smd = ReadSmd(FixturePath(kCrate));
        smdl::BodyGroup group = model.addBodyGroup("crate");
        smdl::Mesh mesh = group.createMesh("crate");
        FillGeometry(model, mesh.geometry(), smd, VertexMode::Indexed);
        // the crate is rigid to bone 0 (the skeleton of its SMD has the single bone "root")
        return model;
    }
}

SMDL_TEST(core_bone_quaternion_matches_euler) {
    const smdl::Model euler = BuildRotated(true);
    const smdl::Model quaternion = BuildRotated(false);
    RequireSameOutputs(ResultFiles(euler.compile()), ResultFiles(quaternion.compile()));
}

SMDL_TEST(core_rest_pose_quaternion_matches_euler) {
    auto build = [](bool euler) {
        smdl::Model model("api/core_rotated");
        BasicGlobals(model, "wood");
        NoSequences(model);
        const smdl::BoneId root = model.addBoneEuler("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0});
        const smdl::BoneId arm = model.addBoneEuler("arm", root, {0, 0, 8}, {0, 0, 0});
        const SmdFile smd = ReadSmd(FixturePath(kCrate));
        smdl::BodyGroup group = model.addBodyGroup("crate");
        smdl::Mesh mesh = group.createMesh("crate");
        FillGeometry(model, mesh.geometry(), smd, VertexMode::Indexed);
        if (euler)
            mesh.geometry().setRestPoseEuler(arm, {1, 2, 3}, {0, 0, kPi});
        else
            mesh.geometry().setRestPose(arm, {1, 2, 3}, {0, 0, 1, 0});
        return model;
    };
    const smdl::Model euler = build(true);
    const smdl::Model quaternion = build(false);
    RequireSameOutputs(ResultFiles(euler.compile()), ResultFiles(quaternion.compile()));
}

// ---------------------------------------------------------------------------------------------------------------
// Call-time checks: rejected when the call is made, with an explanation in the error
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(core_bone_checks) {
    smdl::Model model("api/core_checks");
    const smdl::BoneId root = model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});

    smdl::Error error = CaptureError([&] { model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1}); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME && Contains(error.what(), "root"), "duplicate bone name");

    error = CaptureError([&] { model.addBone("ROOT", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1}); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME, "bone names compare ignoring case");

    error = CaptureError([&] { model.addBone("child", 7, {0, 0, 0}, {0, 0, 0, 1}); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "parent 7"), "unknown parent");

    error = CaptureError([&] { model.addBone("zero", root, {0, 0, 0}, {0, 0, 0, 0}); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "zero length"), "zero quaternion");

    error = CaptureError([&] { model.addBone("nan", root, {std::nanf(""), 0, 0}, {0, 0, 0, 1}); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT, "non-finite position");

    error = CaptureError([&] { model.findBone("nothing"); });
    Require(error.status() == SMDL_ERROR_NOT_FOUND, "find bone");
    Require(model.boneCount() == 1, "failed calls add nothing");
}

SMDL_TEST(core_geometry_checks) {
    smdl::Model model("api/core_checks");
    const smdl::BoneId root = model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});
    smdl::Geometry geometry = model.createMesh("m").geometry();

    const std::vector<smdl::Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    geometry.setPositions(positions);
    Require(geometry.vertexCount() == 3, "positions stored");

    // arrays that contradict the vertex count
    smdl::Error error = CaptureError([&] { geometry.setNormals(std::vector<smdl::Vec3>(2)); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "normals"), "normals count");

    error = CaptureError([&] { geometry.setUVs(8, std::vector<smdl::Vec2>(3)); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "UV set 8"), "uv set range");

    // weights: bone out of range, negative weight
    error = CaptureError([&] {
        const std::vector<smdl::BoneId> bones = {root, root, 5};
        const std::vector<float> weights = {1, 1, 1};
        geometry.setWeights(1, bones, weights);
    });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "bone 5"), "weight bone range");

    error = CaptureError([&] {
        const std::vector<smdl::BoneId> bones = {root, root, root};
        const std::vector<float> weights = {1, -1, 1};
        geometry.setWeights(1, bones, weights);
    });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "negative"), "negative weight");

    const smdl::MaterialId material = geometry.addMaterial("m");
    error = CaptureError([&] { geometry.addMaterial("M"); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME, "duplicate material name (ignoring case)");

    // triangle index and material id range
    error = CaptureError([&] { geometry.setTriangles(std::vector<uint32_t>{0, 1, 3}, std::vector<smdl::MaterialId>{material}); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "triangle index 3"), "triangle index");

    error = CaptureError([&] { geometry.setTriangles(std::vector<uint32_t>{0, 1, 2}, std::vector<smdl::MaterialId>{4}); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "material id 4"), "material id");

    error = CaptureError([&] { geometry.setTriangles(std::vector<uint32_t>{0, 1, 2, 0}); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT, "index count not a multiple of 3");

    // a failed call leaves the data as it was
    Require(geometry.triangleCount() == 0, "failed setTriangles stored nothing");
    geometry.setTriangles(std::vector<uint32_t>{0, 1, 2}, std::vector<smdl::MaterialId>{material});
    Require(geometry.triangleCount() == 1, "valid triangle stored");

    // per-corner arrays must match the triangle count
    error = CaptureError([&] { geometry.setCornerNormals(std::vector<smdl::Vec3>(4)); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "corner normals"), "corner normal count");

    // rest pose of an unknown bone, negative scale
    error = CaptureError([&] { geometry.setRestPoseEuler(9, {0, 0, 0}, {0, 0, 0}); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "bone 9"), "rest pose bone");

    error = CaptureError([&] { geometry.setScale(-1.0f); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT, "negative scale");
}

SMDL_TEST(core_body_checks) {
    smdl::Model model("api/core_checks");
    smdl::Mesh mesh = model.createMesh("a");
    Require(mesh.name() == "a", "mesh name");

    smdl::Error error = CaptureError([&] { model.createMesh("a"); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME && Contains(error.what(), "'a'"), "duplicate mesh name");

    error = CaptureError([&] { model.addBodyGroup("a"); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME, "$body made a group named like the mesh");

    smdl::BodyGroup group = model.addBodyGroup("g");
    error = CaptureError([&] { group.createMesh("a"); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME, "mesh names are unique in the model");

    error = CaptureError([&] { model.findMesh("none"); });
    Require(error.status() == SMDL_ERROR_NOT_FOUND, "find mesh");

    // quads of a geometry without vertices
    error = CaptureError([&] { mesh.geometry().setQuads(std::vector<uint32_t>{0, 1, 2, 3}); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE || error.status() == SMDL_ERROR_INVALID_ARGUMENT,
            "quads need material ids");
}

SMDL_TEST(core_globals_checks) {
    smdl::Model model("api/core_checks");
    const smdl::BoneId root = model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});

    smdl::Error error = CaptureError([&] { model.setScale(0.0f); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "scale"), "scale 0");

    error = CaptureError([&] { model.setContents(1u << 20); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "contents"), "unknown contents bits");

    error = CaptureError([&] { model.setBoneSurfaceProp(root + 3, "wood"); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "bone"), "bone surfaceprop");

    error = CaptureError([&] { model.setBoneContents(root + 3, smdl::ContentsSolid); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE, "bone contents");

    error = CaptureError([&] { model.addKeyValues("prop_data { base"); });
    Require(error.status() == SMDL_ERROR_SYNTAX, "unbalanced keyvalues");

    error = CaptureError([&] { model.setConstantDirectionalLight(true, 2.0f); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT, "directional light range");

    error = CaptureError([&] { model.setIllumPosition({0, 0, 0}, 9); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE, "illum attachment bone");

    for (int i = 0; i < 16; ++i)
        model.addMaterialPath("models/api" + std::to_string(i));
    error = CaptureError([&] { model.addMaterialPath("one/too/many"); });
    Require(error.status() == SMDL_ERROR_LIMIT && Contains(error.what(), "16"), "17th material path");

    error = CaptureError([&] { model.addSkinFamily({"a", "b"}); model.addSkinFamily({"a"}); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "default skin family"),
            "skin family length");

    error = CaptureError([&] { model.addSkipTransition({"only_one"}); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT, "skip transition needs two nodes");
}

// ---------------------------------------------------------------------------------------------------------------
// Compile errors name the object
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(core_compile_error_names_mesh) {
    smdl::Model model("api/core_checks");
    BasicGlobals(model, "wood");
    NoSequences(model);
    model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});
    smdl::BodyGroup group = model.addBodyGroup("lid");
    smdl::Geometry geometry = group.createMesh("lid_closed").geometry();
    const smdl::MaterialId material = geometry.addMaterial("lid");
    geometry.setPositions(std::vector<smdl::Vec3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
    geometry.setTriangles(std::vector<uint32_t>{0, 1, 2}, std::vector<smdl::MaterialId>{material});
    // no normals

    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(error.status() == SMDL_ERROR_COMPILE, "compile error status");
    Require(error.object() == "bodygroup 'lid' / mesh 'lid_closed'", "object of the failure: " + error.object());
    Require(Contains(error.what(), "normals"), "message names the problem: " + std::string(error.what()));
}

SMDL_TEST(core_compile_error_names_preset) {
    smdl::Model model("api/core_checks");
    BasicGlobals(model, "wood");
    NoSequences(model);
    const SmdFile smd = ReadSmd(FixturePath(kCrate));
    AddSkeleton(model, smd);
    smdl::BodyGroup group = model.addBodyGroup("g");
    smdl::Mesh mesh = group.createMesh("m");
    FillGeometry(model, mesh.geometry(), smd, VertexMode::Indexed);
    model.addBodyGroupPreset("bad", {{group, 5}}); // the group has one choice

    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(error.status() == SMDL_ERROR_COMPILE, "compile error status");
    Require(error.object() == "bodygroup preset 'bad'", "object of the failure: " + error.object());
}

SMDL_TEST(core_compile_error_empty_group) {
    smdl::Model model("api/core_checks");
    BasicGlobals(model, "wood");
    NoSequences(model);
    model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});
    model.addBodyGroup("empty");
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(error.object() == "bodygroup 'empty'", "object of the failure: " + error.object());
}

SMDL_TEST(core_compile_error_no_sequences) {
    // a model without sequences and without an included model is the compiler's own error
    smdl::Model model("api/core_checks");
    BasicGlobals(model, "wood");
    AddBody(model, "crate", "crate", kCrate, VertexMode::Indexed);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(error.status() == SMDL_ERROR_COMPILE && Contains(error.what(), "no sequences"),
            "no sequences: " + std::string(error.what()));
}

// A compile that failed leaves the process and the model reusable: fix the model and compile again
SMDL_TEST(core_compile_after_error) {
    smdl::Model model("api/core_prop_static");
    BasicGlobals(model, "wood");
    model.setStaticProp(true);
    smdl::BodyGroup group = model.addBodyGroup("crate");
    smdl::Mesh mesh = group.createMesh("src/crate");
    const SmdFile smd = ReadSmd(FixturePath(kCrate));
    AddSkeleton(model, smd);
    mesh.geometry().setPositions(std::vector<smdl::Vec3>{{0, 0, 0}});
    RequireThrows<smdl::Error>([&] { model.compile(); }, "geometry without triangles");

    smdl::Model good("api/core_prop_static");
    BasicGlobals(good, "wood");
    good.setStaticProp(true);
    AddBody(good, "crate", "src/crate", kCrate, VertexMode::Indexed);
    RequireSameAsQc(good, "core_prop_static.qc");
}

// ---------------------------------------------------------------------------------------------------------------
// User tangents
// ---------------------------------------------------------------------------------------------------------------

namespace {
    // The .vvd of the compile: header (id, version, checksum, numLODs, numLODVertexes[8], numFixups, fixupTableStart,
    // vertexDataStart, tangentDataStart), then 48 byte vertices (bone weights 16, position, normal, uv) and 16 byte
    // tangents (x, y, z, w), in LOD 0 order
    struct VvdVertex {
        smdl::Vec3 position, normal;
        smdl::Vec4 tangent;
    };

    std::vector<VvdVertex> ReadVvd(const std::vector<std::byte> &bytes) {
        auto read = [&](size_t offset, void *out, size_t size) {
            Require(offset + size <= bytes.size(), "vvd read inside the file");
            std::memcpy(out, bytes.data() + offset, size);
        };
        int32_t count = 0, vertexStart = 0, tangentStart = 0, fixups = 0;
        read(16, &count, 4);
        read(48, &fixups, 4);
        read(56, &vertexStart, 4);
        read(60, &tangentStart, 4);
        Require(fixups == 0, "single LOD vvd without fixups");
        std::vector<VvdVertex> vertices(static_cast<size_t>(count));
        for (size_t i = 0; i < vertices.size(); ++i) {
            read(static_cast<size_t>(vertexStart) + i * 48 + 16, vertices[i].position.data(), 12);
            read(static_cast<size_t>(vertexStart) + i * 48 + 28, vertices[i].normal.data(), 12);
            read(static_cast<size_t>(tangentStart) + i * 16, vertices[i].tangent.data(), 16);
        }
        return vertices;
    }

    // The tangent given for a vertex: a unit vector that depends on the position, w from the normal
    smdl::Vec4 UserTangent(const smdl::Vec3 &position, const smdl::Vec3 &normal) {
        const smdl::Vec3 v = {position[0] + 1.0f, position[1] - 2.0f, position[2] + 3.0f};
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return {v[0] / length, v[1] / length, v[2] / length, (normal[0] + normal[1] + normal[2] >= 0.0f) ? 1.0f : -1.0f};
    }

    bool Near(const smdl::Vec3 &a, const smdl::Vec3 &b) {
        return std::fabs(a[0] - b[0]) < 1e-4f && std::fabs(a[1] - b[1]) < 1e-4f && std::fabs(a[2] - b[2]) < 1e-4f;
    }

    // The source transform of a Z up model: a quarter turn about Z, (x, y, z) -> (-y, x, z)
    smdl::Vec3 QuarterTurn(const smdl::Vec3 &v) { return {-v[1], v[0], v[2]}; }
}

// smdl_geometry_set_tangents: the .vvd carries the given tangent of each vertex (rotated with the model like the
// normal) instead of the ones the compiler calculates from the texture coordinates
SMDL_TEST(core_user_tangents) {
    const SmdFile smd = ReadSmd(FixturePath(kCrate));
    const GeometryData data = BuildGeometryData(smd, VertexMode::Indexed);

    auto compile = [&](bool tangents) {
        smdl::Model model("api/core_prop_static");
        BasicGlobals(model, "wood");
        model.setStaticProp(true);
        smdl::Mesh mesh = AddBody(model, "crate", "src/crate", kCrate, VertexMode::Indexed);
        if (tangents) {
            std::vector<smdl::Vec4> given(data.positions.size());
            for (size_t i = 0; i < given.size(); ++i)
                given[i] = UserTangent(data.positions[i], data.normals[i]);
            mesh.geometry().setTangents(given);
        }
        return ResultFiles(model.compile());
    };

    const Outputs calculated = compile(false);
    const Outputs given = compile(true);
    const std::string vvd = "models/api/core_prop_static.vvd";
    Require(calculated.at(vvd) != given.at(vvd), "the user tangents change the .vvd");

    const std::vector<VvdVertex> vertices = ReadVvd(given.at(vvd));
    Require(vertices.size() == 24, "the 12 triangles of the crate unify to 24 vertices");
    for (const VvdVertex &vertex: vertices) {
        // find the source vertex of this one: same position and normal after the quarter turn
        const smdl::Vec4 *expected = nullptr;
        smdl::Vec4 candidate{};
        for (size_t i = 0; i < data.positions.size(); ++i) {
            if (Near(QuarterTurn(data.positions[i]), vertex.position) && Near(QuarterTurn(data.normals[i]), vertex.normal)) {
                candidate = UserTangent(data.positions[i], data.normals[i]);
                expected = &candidate;
                break;
            }
        }
        Require(expected != nullptr, "every vertex of the .vvd comes from a vertex of the geometry");
        const smdl::Vec3 turned = QuarterTurn({candidate[0], candidate[1], candidate[2]});
        Require(Near(turned, {vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]}) && vertex.tangent[3] == candidate[3],
                "the tangent of the vertex is the one that was given");
    }
}

// A tangent array has one entry per vertex
SMDL_TEST(core_user_tangents_checks) {
    smdl::Model model("api/core_checks");
    model.addBone("root", smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});
    smdl::Geometry geometry = model.createMesh("m").geometry();
    geometry.setPositions(std::vector<smdl::Vec3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
    const smdl::Error error = CaptureError([&] { geometry.setTangents(std::vector<smdl::Vec4>(2)); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT && Contains(error.what(), "tangents"), "tangent count");
}

// LodPhys part tests, LODs: builder output must be byte-identical to the QC compile of the equivalent fixture
// (lod.qc, lod_options.qc, lod_models.qc), plus the call-time checks and the compile errors. The QCs name the meshes by
// their SMD path ("src/lod_hi.smd"), so the builder's meshes carry the same name (the .mdl stores it); LODs match their
// replacements by the name without the extension.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

using namespace coretest;
using namespace animtest;

namespace {
    constexpr const char *kHi = "src/lod_hi.smd";
    constexpr const char *kMid = "src/lod_mid.smd";
    constexpr const char *kLow = "src/lod_low.smd";
    constexpr const char *kShadow = "src/lod_shadow.smd";

    // $body "lod_body" "src/lod_hi.smd" and the QC's $sequence "idle" "src/lod_hi.smd" fps 30
    smdl::Mesh BuildLodBase(smdl::Model &model) {
        BasicGlobals(model, "metal");
        smdl::Mesh hi = AddBody(model, "lod_body", "src/lod_hi.smd", kHi, VertexMode::Indexed);
        return hi;
    }

    void AddIdleSequence(smdl::Model &model) {
        smdl::Animation idle = AddAnimationFromSmd(model, "@idle", kHi, 30.0f);
        model.createSequence("idle").addAnimation(idle);
    }

    // The geometry of a LOD source SMD over the model's skeleton
    smdl::Geometry ReplaceWith(smdl::Model &model, const smdl::Lod &lod, const smdl::Mesh &mesh, const char *smdPath,
                               VertexMode mode = VertexMode::Indexed) {
        smdl::Geometry geometry = lod.replaceMesh(mesh);
        FillGeometry(model, geometry, ReadSmd(FixturePath(smdPath)), mode);
        return geometry;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Equivalence with the QC compile
// ---------------------------------------------------------------------------------------------------------------

// replacemodel in two LODs, removemesh (the material is not in the second LOD's source), $shadowlod
static void BuildLodFixture(smdl::Model &model, VertexMode mode) {
    smdl::Mesh hi = BuildLodBase(model);

    smdl::Lod lod1 = model.addLod(20.0f);
    ReplaceWith(model, lod1, hi, kMid, mode);

    smdl::Lod lod2 = model.addLod(50.0f);
    ReplaceWith(model, lod2, hi, kLow, mode);
    lod2.removeMaterial("lod_trim");

    smdl::Lod shadow = model.addShadowLod();
    ReplaceWith(model, shadow, hi, kShadow, mode);

    AddIdleSequence(model);
}

SMDL_TEST(lod_equiv_fixture) {
    smdl::Model model("api/lod");
    BuildLodFixture(model, VertexMode::Indexed);
    Require(model.lodCount() == 3, "three LODs, the shadow LOD included");
    RequireSameAsQc(model, "lod.qc");
}

SMDL_TEST(lod_equiv_fixture_corners) {
    smdl::Model model("api/lod");
    BuildLodFixture(model, VertexMode::Corners);
    RequireSameAsQc(model, "lod.qc");
}

// replacebone, bonetreecollapse, replacematerial, removemesh with the mesh present in the LOD source (HEAD's QC compile
// fails on that: the LOD vertex maps were left uninitialized), reverse, nofacial / facial, use_shadowlod_materials
SMDL_TEST(lod_equiv_options) {
    smdl::Model model("api/lod_options");
    smdl::Mesh hi = BuildLodBase(model);

    smdl::Lod lod1 = model.addLod(15.0f);
    smdl::Geometry mid = ReplaceWith(model, lod1, hi, kMid);
    mid.setFlipTriangles(true);
    lod1.replaceBone(model.findBone("tip"), model.findBone("mid"));
    lod1.replaceMaterial("lod_skin", "lod_skin_far");
    lod1.removeMaterial("lod_trim");
    lod1.setFacialAnimation(false);

    smdl::Lod lod2 = model.addLod(40.0f);
    ReplaceWith(model, lod2, hi, kLow);
    lod2.collapseBoneTree(model.findBone("mid"));
    lod2.setFacialAnimation(true);

    smdl::Lod shadow = model.addShadowLod();
    ReplaceWith(model, shadow, hi, kShadow);
    shadow.setUseShadowLodMaterials(true);

    AddIdleSequence(model);
    RequireSameAsQc(model, "lod_options.qc");
}

// removemodel: the LOD stops drawing a body
SMDL_TEST(lod_equiv_remove_model) {
    smdl::Model model("api/lod_models");
    smdl::Mesh hi = BuildLodBase(model);
    smdl::BodyGroup extraGroup = model.addBodyGroup("lod_extra");
    smdl::Mesh extra = AddMesh(model, extraGroup, "src/lod_low.smd", kLow, VertexMode::Indexed, false);

    smdl::Lod lod1 = model.addLod(25.0f);
    ReplaceWith(model, lod1, hi, kMid);
    lod1.removeMesh(extra);

    AddIdleSequence(model);
    RequireSameAsQc(model, "lod_models.qc");
}

// An existing geometry shared by two LODs: its source is built once and both LODs use it, like a QC naming the same SMD
// twice (a second load is another source with the same content, so the files are the same)
SMDL_TEST(lod_shared_geometry_matches_separate_geometries) {
    const auto build = [](bool shared) {
        smdl::Model model("api/lod_shared");
        smdl::Mesh hi = BuildLodBase(model);
        smdl::Lod lod1 = model.addLod(20.0f);
        smdl::Geometry mid = ReplaceWith(model, lod1, hi, kMid);
        smdl::Lod lod2 = model.addLod(40.0f);
        if (shared)
            lod2.replaceMesh(hi, mid);
        else
            ReplaceWith(model, lod2, hi, kMid);
        AddIdleSequence(model);
        return ResultFiles(model.compile());
    };
    RequireSameOutputs(build(false), build(true));
}

// -striplods: the LODs after the root are not built (the QC forgets them, the shadow LOD too, but the model keeps the
// shadow LOD flag)
SMDL_TEST(lod_equiv_strip_lods) {
    smdl::Model model("api/lod");
    BuildLodFixture(model, VertexMode::Indexed);
    model.setStripLods(true);
    RequireSameAsQc(model, "lod.qc", {"-striplods"});
}

// ---------------------------------------------------------------------------------------------------------------
// Call-time checks and compile errors
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(lod_call_checks) {
    smdl::Model model("api/lod_checks");
    smdl::Mesh hi = BuildLodBase(model);

    CaptureError([&] { model.addLod(-1.0f); });

    smdl::Lod lod = model.addLod(10.0f);
    Require(!lod.isShadow(), "regular LOD");
    ReplaceWith(model, lod, hi, kMid);
    CaptureError([&] { lod.removeMesh(hi); });      // already replaced
    CaptureError([&] { lod.replaceMesh(hi); });
    CaptureError([&] { lod.replaceBone(model.findBone("tip"), static_cast<smdl::BoneId>(99)); });
    CaptureError([&] { lod.collapseBoneTree(static_cast<smdl::BoneId>(99)); });
    CaptureError([&] { lod.replaceMaterial("", "x"); });
    CaptureError([&] { lod.removeMaterial(""); });
    CaptureError([&] { lod.setUseShadowLodMaterials(true); }); // shadow LOD only

    smdl::Lod shadow = model.addShadowLod();
    Require(shadow.isShadow(), "shadow LOD");
    CaptureError([&] { shadow.setFacialAnimation(true); });
    CaptureError([&] { model.addShadowLod(); });
    CaptureError([&] { model.addLod(30.0f); }); // nothing after the shadow LOD
    Require(model.lodCount() == 2, "LOD count");
}

SMDL_TEST(lod_call_check_limit) {
    smdl::Model model("api/lod_limit");
    BuildLodBase(model);
    for (int i = 0; i < 7; ++i)
        model.addLod(static_cast<float>(10 + i));
    CaptureError([&] { model.addLod(100.0f); });
}

// Two meshes whose names are equal up to the last '.' cannot be told apart by studiomdl's replacemodel
SMDL_TEST(lod_compile_error_ambiguous_mesh_name) {
    smdl::Model model("api/lod_ambiguous");
    BasicGlobals(model, "metal");
    smdl::Mesh a = AddBody(model, "a", "src/lod_hi.a", kHi, VertexMode::Indexed);
    smdl::BodyGroup groupB = model.addBodyGroup("b");
    AddMesh(model, groupB, "src/lod_hi.b", kHi, VertexMode::Indexed, false);
    smdl::Lod lod = model.addLod(20.0f);
    ReplaceWith(model, lod, a, kMid);
    AddIdleSequence(model);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(Contains(error.what(), "same name"), error.what());
}

// LodPhys part tests, collision model: builder output must be byte-identical to the QC compile of the equivalent fixture
// (collision_*.qc), plus the call-time checks and the compile errors. The QCs name the meshes by their SMD path, so the
// builder's meshes carry the same name; the collision geometries are filled from the physics SMDs.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

#include <algorithm>
#include <cstring>

using namespace coretest;
using namespace animtest;

namespace {
    constexpr const char *kPhysBox = "src/phys_box.smd";
    constexpr const char *kRagdollPhys = "src/ragdoll_phys.smd";

    // $body "crate" "src/crate.smd" + $sequence "idle" "src/crate.smd" fps 30
    smdl::Mesh BuildCrate(smdl::Model &model) {
        BasicGlobals(model, "wood");
        smdl::Mesh crate = AddBody(model, "crate", "src/crate.smd", kCrate, VertexMode::Indexed);
        smdl::Animation idle = AddAnimationFromSmd(model, "@idle", kCrate, 30.0f);
        model.createSequence("idle").addAnimation(idle);
        return crate;
    }

    // $body "ragdoll" "src/ragdoll_ref.smd" + the looping idle
    void BuildRagdoll(smdl::Model &model) {
        BasicGlobals(model, "flesh");
        AddBody(model, "ragdoll", "src/ragdoll_ref.smd", "src/ragdoll_ref.smd", VertexMode::Indexed);
        smdl::Animation idle = AddAnimationFromSmd(model, "@idle", "src/anims/ragdoll_idle.smd", 30.0f);
        idle.setLoop();
        model.createSequence("idle").addAnimation(idle);
    }

    void FillPhys(smdl::Model &model, const smdl::Geometry &geometry, const char *smdPath,
                  VertexMode mode = VertexMode::Corners) {
        FillGeometry(model, geometry, ReadSmd(FixturePath(smdPath)), mode);
    }

    // The collision source named like the QC's file ("src/phys_box.smd" -> "src/phys_box")
    void FillSource(smdl::Model &model, const smdl::Collision &collision, const char *smdPath,
                    VertexMode mode = VertexMode::Corners) {
        std::string name = smdPath;
        name.resize(name.size() - 4);
        const smdl::Geometry geometry = collision.geometry();
        collision.setSourceName(name);
        FillPhys(model, geometry, smdPath, mode);
    }

    // The $collisionjoints block of the ragdoll fixtures
    smdl::Collision RagdollCollision(smdl::Model &model, VertexMode mode = VertexMode::Corners) {
        smdl::Collision collision = model.collision();
        collision.setJointed(true);
        FillSource(model, collision, kRagdollPhys, mode);
        return collision;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Equivalence with the QC compile
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(collision_equiv_single) {
    smdl::Model model("api/collision_single");
    BuildCrate(model);
    smdl::Collision collision = model.collision();
    FillSource(model, collision, kPhysBox);
    collision.setMass(50.0f).setInertia(3.0f).setDamping(0.1f).setRotDamping(1.5f);
    RequireSameAsQc(model, "collision_single.qc");
}

SMDL_TEST(collision_equiv_single_indexed_vertices) {
    smdl::Model model("api/collision_single");
    BuildCrate(model);
    smdl::Collision collision = model.collision();
    FillSource(model, collision, kPhysBox, VertexMode::Indexed);
    collision.setMass(50.0f).setInertia(3.0f).setDamping(0.1f).setRotDamping(1.5f);
    RequireSameAsQc(model, "collision_single.qc");
}

// every whole-model option ($rollingDrag does nothing in the QC)
SMDL_TEST(collision_equiv_options) {
    smdl::Model model("api/collision_options");
    BuildCrate(model);
    smdl::Collision collision = model.collision();
    FillSource(model, collision, kPhysBox);
    collision.setMass(25.0f)
            .setInertia(2.5f)
            .setDamping(0.2f)
            .setRotDamping(0.7f)
            .setDrag(0.3f)
            .setRollingDrag(0.5f)
            .setMaxConvexPieces(4)
            .setRemove2D()
            .setWeldPosition(0.02f)
            .setWeldNormal(0.95f)
            .setConcave()
            .setConvexHullCountOverride()
            .setMassCenter({0.0f, 0.0f, 8.0f})
            .setAssumeWorldSpace();
    RequireSameAsQc(model, "collision_options.qc");
}

SMDL_TEST(collision_equiv_auto_mass) {
    smdl::Model model("api/collision_automass");
    BuildCrate(model);
    smdl::Collision collision = model.collision();
    FillSource(model, collision, kPhysBox);
    collision.setAutoMass();
    RequireSameAsQc(model, "collision_automass.qc");
}

// $collisionmodel blank + $addconvexsrc (plain; offset + concave)
SMDL_TEST(collision_equiv_convex_sources) {
    smdl::Model model("api/collision_convex");
    BuildCrate(model);
    smdl::Collision collision = model.collision();
    collision.setMass(30.0f);
    uint32_t first = 99;
    FillPhys(model, collision.addConvexSource(&first), kPhysBox);
    collision.setConvexSourceName(first, "src/phys_box");
    Require(first == 0, "first convex source index");
    uint32_t second = 99;
    FillPhys(model, collision.addConvexSource(&second), kPhysBox);
    collision.setConvexSourceName(second, "src/phys_box");
    Require(second == 1, "second convex source index");
    Require(collision.convexSourceCount() == 2, "two convex sources");
    collision.setConvexSourceOffset(second, {0.0f, 0.0f, 20.0f}, {0.0f, 45.0f, 0.0f}, 0.5f);
    collision.setConvexSourceConcave(second);
    RequireSameAsQc(model, "collision_convex.qc");
}

// the collision source is a mesh's geometry: the compile builds its own copy of the source
SMDL_TEST(collision_equiv_mesh_geometry) {
    smdl::Model model("api/collision_mesh");
    smdl::Mesh crate = BuildCrate(model);
    model.collision().setGeometry(crate.geometry()).setMass(50.0f);
    RequireSameAsQc(model, "collision_mesh.qc");
}

SMDL_TEST(collision_equiv_joints) {
    smdl::Model model("api/collision_joints");
    BuildRagdoll(model);
    smdl::Collision collision = RagdollCollision(model);
    const smdl::BoneId pelvis = model.findBone("pelvis");
    const smdl::BoneId spine = model.findBone("spine");
    const smdl::BoneId skull = model.findBone("skull");
    collision.setMass(40.0f).setRootBone(pelvis);
    collision.jointConstrain(spine, smdl::CollisionAxis::X, smdl::JointType::Limit, -20.0f, 20.0f, 0.1f);
    collision.jointConstrain(spine, smdl::CollisionAxis::Y, smdl::JointType::Limit, -30.0f, 30.0f, 0.1f);
    collision.jointConstrain(spine, smdl::CollisionAxis::Z, smdl::JointType::Fixed, 0.0f, 0.0f, 0.0f);
    collision.jointConstrain(skull, smdl::CollisionAxis::X, smdl::JointType::Limit, -40.0f, 40.0f, 0.2f);
    collision.jointConstrain(skull, smdl::CollisionAxis::Y, smdl::JointType::Free, 0.0f, 0.0f, 0.2f);
    collision.jointConstrain(skull, smdl::CollisionAxis::Z, smdl::JointType::Limit, -45.0f, 45.0f, 0.2f);
    collision.jointSetMassBias(skull, 1.5f).jointSetDamping(spine, 0.2f);
    RequireSameAsQc(model, "collision_joints.qc");
}

// every joint command that keeps the three joints: properties, collide pairs, all-to-all, no collide, animated friction
SMDL_TEST(collision_equiv_joints_full) {
    smdl::Model model("api/collision_joints_full");
    BuildRagdoll(model);
    smdl::Collision collision = RagdollCollision(model);
    const smdl::BoneId pelvis = model.findBone("pelvis");
    const smdl::BoneId spine = model.findBone("spine");
    const smdl::BoneId skull = model.findBone("skull");
    collision.setAutoMass()
            .setInertia(4.0f)
            .setDamping(0.1f)
            .setRotDamping(0.2f)
            .setDrag(0.05f)
            .setMaxConvexPieces(8)
            .setWeldPosition(0.03f)
            .setConcavePerJoint()
            .setConvexHullCountOverride()
            .setNoSelfCollisions()
            .setRootBone(pelvis)
            .setAnimatedFriction(10, 100, 0.5f, 1.0f, 0.5f);
    collision.jointSetInertia(spine, 2.0f)
            .jointSetDamping(skull, 0.3f)
            .jointSetRotDamping(spine, 0.4f)
            .jointSetMassBias(pelvis, 2.0f);
    collision.jointConstrain(spine, smdl::CollisionAxis::X, smdl::JointType::Limit, -20.0f, 20.0f, 0.1f);
    collision.jointConstrain(spine, smdl::CollisionAxis::Y, smdl::JointType::Free, 0.0f, 0.0f, 0.2f);
    collision.jointConstrain(spine, smdl::CollisionAxis::Z, smdl::JointType::Fixed, 0.0f, 0.0f, 0.0f);
    collision.jointConstrain(skull, smdl::CollisionAxis::X, smdl::JointType::Limit, -10.0f, 10.0f);
    collision.jointCollide(pelvis, skull);
    const std::array<smdl::BoneId, 3> all = {pelvis, spine, skull};
    collision.jointCollideAll(all);
    collision.jointNoCollide(spine, skull);
    RequireSameAsQc(model, "collision_joints_full.qc");
}

SMDL_TEST(collision_equiv_joint_merge) {
    smdl::Model model("api/collision_joints_merge");
    BuildRagdoll(model);
    smdl::Collision collision = RagdollCollision(model);
    collision.setMass(40.0f);
    collision.jointMerge(model.findBone("spine"), model.findBone("skull"));
    collision.jointConstrain(model.findBone("spine"), smdl::CollisionAxis::X, smdl::JointType::Limit, -20.0f, 20.0f, 0.1f);
    RequireSameAsQc(model, "collision_joints_merge.qc");
}

SMDL_TEST(collision_equiv_joint_skip) {
    smdl::Model model("api/collision_joints_skip");
    BuildRagdoll(model);
    smdl::Collision collision = RagdollCollision(model);
    collision.setMass(40.0f);
    collision.jointSkip(model.findBone("skull"));
    collision.jointConstrain(model.findBone("spine"), smdl::CollisionAxis::X, smdl::JointType::Limit, -20.0f, 20.0f, 0.1f);
    RequireSameAsQc(model, "collision_joints_skip.qc");
}

// The collision text (.mpp `collisionText`) is written into the .phy
SMDL_TEST(collision_text_is_written_to_phy) {
    const auto build = [](bool withText) {
        smdl::Model model("api/collision_text");
        BuildCrate(model);
        smdl::Collision collision = model.collision();
        FillSource(model, collision, kPhysBox);
        collision.setMass(50.0f);
        if (withText)
            collision.setText("solid { \"index\" \"0\" \"name\" \"crate_body\" \"surfaceprop\" \"wood\" }");
        return ResultFiles(model.compile());
    };
    const Outputs plain = build(false);
    const Outputs text = build(true);
    const auto &plainPhy = plain.at("models/api/collision_text.phy");
    const auto &textPhy = text.at("models/api/collision_text.phy");
    Require(textPhy.size() > plainPhy.size(), "the text makes the .phy larger");
    const std::string needle = "crate_body";
    const bool found = std::search(textPhy.begin(), textPhy.end(), needle.begin(), needle.end(),
                                   [](std::byte a, char b) { return static_cast<char>(a) == b; }) != textPhy.end();
    Require(found, "the .phy contains the collision text");
}

// A model whose collision model was never filled has none
SMDL_TEST(collision_untouched_writes_no_phy) {
    smdl::Model model("api/collision_none");
    BuildCrate(model);
    model.collision();
    const Outputs files = ResultFiles(model.compile());
    Require(!files.contains("models/api/collision_none.phy"), "no .phy");
}

// ---------------------------------------------------------------------------------------------------------------
// Call-time checks and compile errors
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(collision_call_checks) {
    smdl::Model model("api/collision_checks");
    BuildRagdoll(model);
    smdl::Collision collision = model.collision();
    const smdl::BoneId spine = model.findBone("spine");
    const smdl::BoneId noBone = 99;

    CaptureError([&] { collision.setRootBone(noBone); });
    CaptureError([&] { collision.jointSkip(noBone); });
    CaptureError([&] { collision.jointMerge(spine, noBone); });
    CaptureError([&] { collision.jointCollide(noBone, spine); });
    CaptureError([&] { collision.jointNoCollide(spine, noBone); });
    CaptureError([&] { collision.jointSetMassBias(noBone, 1.0f); });
    CaptureError([&] {
        collision.jointConstrain(spine, smdl::CollisionAxis::X, smdl::JointType::Limit, 20.0f, -20.0f);
    });
    CaptureError([&] {
        collision.jointConstrain(spine, static_cast<smdl::CollisionAxis>(3), smdl::JointType::Limit, -1.0f, 1.0f);
    });
    CaptureError([&] {
        collision.jointConstrain(spine, smdl::CollisionAxis::X, static_cast<smdl::JointType>(7), -1.0f, 1.0f);
    });
    CaptureError([&] { collision.setMass(std::nanf("")); });
    CaptureError([&] { collision.setMaxConvexPieces(-1); });
    CaptureError([&] { collision.setConvexSourceConcave(0); }); // no such source
    CaptureError([&] { collision.setConvexSourceOffset(0, {0, 0, 0}, {0, 0, 0}); });

    std::vector<smdl::BoneId> many(33, spine);
    CaptureError([&] { collision.jointCollideAll(many); });

    // at most 24 convex sources
    for (int i = 0; i < 24; ++i)
        collision.addConvexSource();
    CaptureError([&] { collision.addConvexSource(); });
    Require(collision.convexSourceCount() == 24, "24 convex sources");
}

SMDL_TEST(collision_geometry_of_another_model_is_rejected) {
    smdl::Model model("api/collision_foreign");
    smdl::Model other("api/collision_foreign_other");
    smdl::Mesh mesh = BuildCrate(other);
    CaptureError([&] { model.collision().setGeometry(mesh.geometry()); });
}

SMDL_TEST(collision_compile_error_commands_without_geometry) {
    smdl::Model model("api/collision_nogeometry");
    BuildCrate(model);
    model.collision().setMass(10.0f);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(Contains(error.what(), "no geometry"), error.what());
}

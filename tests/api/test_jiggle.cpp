// LodPhys part tests, bone dynamics: jiggle bones must be byte-identical to the QC compile of jiggle.qc, plus the
// call-time checks.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

#include <algorithm>
#include <cstring>

using namespace coretest;
using namespace animtest;

SMDL_TEST(jiggle_equiv_fixture) {
    smdl::Model model("api/jiggle");
    BasicGlobals(model, "flesh");
    AddBody(model, "tube", "src/chain_ref.smd", "src/chain_ref.smd", VertexMode::Indexed);

    const smdl::JiggleBone tip = model.addJiggleBone(model.findBone("tip"));
    tip.setFlexible()
            .setLength(6.0f)
            .setTipMass(40.0f)
            .setPitchStiffness(100.0f)
            .setPitchDamping(7.0f)
            .setYawStiffness(100.0f)
            .setYawDamping(7.0f)
            .setAllowLengthFlex()
            .setAlongStiffness(100.0f)
            .setAlongDamping(0.0f)
            .setAngleConstraint(30.0f);
    tip.setBaseSpring()
            .setBaseStiffness(100.0f)
            .setBaseDamping(5.0f)
            .setBaseLeftConstraint(-10.0f, 10.0f)
            .setBaseUpConstraint(-10.0f, 10.0f)
            .setBaseForwardConstraint(-10.0f, 10.0f)
            .setBaseMass(5.0f);

    const smdl::JiggleBone tipEnd = model.addJiggleBone(model.findBone("tip_end"));
    tipEnd.setRigid()
            .setLength(4.0f)
            .setTipMass(10.0f)
            .setYawConstraint(-20.0f, 20.0f)
            .setYawFriction(0.3f)
            .setYawBounce(0.1f)
            .setPitchConstraint(-25.0f, 25.0f)
            .setPitchFriction(0.3f)
            .setPitchBounce(0.1f);

    smdl::Animation idle = AddAnimationFromSmd(model, "@idle", "src/chain_ref.smd", 30.0f);
    smdl::Animation wiggle = AddAnimationFromSmd(model, "@wiggle", "src/anims/chain_idle.smd", 30.0f);
    wiggle.setLoop();
    model.createSequence("idle").addAnimation(idle);
    model.createSequence("wiggle").addAnimation(wiggle);
    RequireSameAsQc(model, "jiggle.qc");
}

SMDL_TEST(jiggle_call_checks) {
    smdl::Model model("api/jiggle_checks");
    BasicGlobals(model, "flesh");
    AddBody(model, "tube", "src/chain_ref.smd", "src/chain_ref.smd", VertexMode::Indexed);
    const smdl::BoneId tip = model.findBone("tip");

    CaptureError([&] { model.addJiggleBone(99); });
    const smdl::JiggleBone jiggle = model.addJiggleBone(tip);
    CaptureError([&] { model.addJiggleBone(tip); }); // one per bone
    CaptureError([&] { jiggle.setYawStiffness(1.0f); }); // needs is_flexible
    CaptureError([&] { jiggle.setBaseMass(1.0f); });     // needs has_base_spring
    CaptureError([&] { jiggle.setAllowLengthFlex(); });  // needs is_flexible
    jiggle.setRigid();
    CaptureError([&] { jiggle.setFlexible(); });         // not with is_rigid
    CaptureError([&] { jiggle.setLength(std::nanf("")); });
    Require(model.jiggleBoneCount() == 1, "one jiggle bone");
}

namespace {
    // Bone names of a compiled MDL (studiohdr_t numbones/boneindex at 156/160, mstudiobone_t is 216 bytes with the
    // name offset first)
    std::vector<std::string> MdlBoneNames(const std::vector<std::byte> &mdl) {
        auto read = [&](size_t offset) {
            int32_t value;
            std::memcpy(&value, mdl.data() + offset, sizeof(value));
            return value;
        };
        std::vector<std::string> names;
        const int32_t count = read(156), index = read(160);
        for (int32_t i = 0; i < count; ++i) {
            const size_t bone = static_cast<size_t>(index) + 216u * static_cast<size_t>(i);
            names.emplace_back(reinterpret_cast<const char *>(mdl.data() + bone + read(bone)));
        }
        return names;
    }
}

// Bones without vertices that other parts name are kept: jiggle bones, procedural bones and their control, explicit
// hitboxes and bone controllers. A bone nothing names still goes.
SMDL_TEST(referenced_bones_without_vertices_are_kept) {
    smdl::Model model("api/kept_bones");
    BasicGlobals(model, "flesh");
    AddBody(model, "tube", "src/chain_ref.smd", "src/chain_ref.smd", VertexMode::Indexed);
    const smdl::BoneId tip = model.findBone("tip");
    auto loose = [&](const char *name) { return model.addBone(name, tip, {0, 0, 2}, {0, 0, 0, 1}); };
    model.addJiggleBone(loose("loose_jiggle")).setRigid().setLength(4.0f);
    const smdl::BoneId control = loose("loose_control");
    model.addQuatInterp(loose("loose_helper"), tip, tip, control).addTrigger(90.0f, {0, 0, 0}, {0, 0, 0}, {0, 0, 0});
    model.addHitbox(model.addHitboxSet("default"), 0, loose("loose_hitbox"), {-1, -1, -1}, {1, 1, 1});
    model.addBoneController(0, loose("loose_controller"), SMDL_BONE_CONTROLLER_ZR, -90.0f, 90.0f);
    loose("loose_unused");
    smdl::Animation idle = AddAnimationFromSmd(model, "@idle", "src/chain_ref.smd", 30.0f);
    model.createSequence("idle").addAnimation(idle);

    const Outputs outputs = ResultFiles(model.compile());
    const std::vector<std::string> names = MdlBoneNames(outputs.at("models/api/kept_bones.mdl"));
    for (const char *name: {"loose_jiggle", "loose_control", "loose_helper", "loose_hitbox", "loose_controller"})
        Require(std::ranges::find(names, name) != names.end(), std::string(name) + " is kept");
    Require(std::ranges::find(names, "loose_unused") == names.end(), "an unreferenced bone is still removed");
}

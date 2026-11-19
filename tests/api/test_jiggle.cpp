// LodPhys part tests, bone dynamics: jiggle bones must be byte-identical to the QC compile of jiggle.qc, plus the
// call-time checks.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

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

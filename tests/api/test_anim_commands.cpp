// Anim part tests, equivalence for the rarely used features: every animation option and command, IK rules, weight
// lists with a default list, and the sequence options (anim_cmds.qc, anim_seq.qc). The builder calls are the QC's tokens
// in the same order; animation options that the QC accepts in any order are applied by the builder in a fixed order
// (blockname before frame / framestart), so the fixture lists them in that order.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

using namespace coretest;
using namespace animtest;
namespace M = smdl::Motion;

SMDL_TEST(anim_equiv_commands) {
    smdl::Model model("api/anim_cmds");
    BasicGlobals(model, "flesh");
    AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);
    const auto bone = [&](std::string_view name) { return model.findBone(name); };

    const smdl::WeightListId upper = model.addWeightList("upper");
    model.setWeightListBone(upper, bone("root"), 0.0f);
    model.setWeightListBone(upper, bone("spine"), 1.0f, 0.5f);
    model.setWeightListBone(upper, bone("head"), 1.0f);
    model.setWeightListBone(smdl::DefaultWeightList, bone("root"), 1.0f);
    model.setWeightListBone(smdl::DefaultWeightList, bone("pelvis"), 1.0f);

    model.addAttachment("muzzle", bone("r_arm"), {0, 0, 10});
    const smdl::IkChainId leftLeg = model.addIkChain("l_leg", bone("l_foot"));
    model.setIkChainKnee(leftLeg, {1, 0, 0});
    model.setIkChainHeight(leftLeg, 18);
    model.setIkChainFloor(leftLeg, 0);
    model.setIkChainPad(leftLeg, 8);
    const smdl::IkChainId rightLeg = model.addIkChain("r_leg", bone("r_foot"));
    model.setIkChainKnee(rightLeg, {1, 0, 0});
    model.setIkChainCenter(rightLeg, {0, 0, 0});

    smdl::Animation refPose = AddAnimationFromSmd(model, "ref_pose", "src/biped.smd", 30);

    smdl::Animation idle = AddAnimationFromSmd(model, "a_idle", "src/anims/biped_idle.smd", 24);
    idle.setLoop().addFixupLoop(8, 9).setAutoIk(false);

    smdl::Animation walk = AddAnimationFromSmd(model, "a_walk", "src/anims/biped_walk.smd", 30);
    walk.setLoop()
            .setMotionExtraction(M::LX | M::LY)
            .setOrigin({1, 2, 3})
            .setRotate(90)
            .setScale(1.5f)
            .setMotionRollback(0.5f)
            .addWalkFrame(12, M::X | M::Y)
            .setPost();

    smdl::Animation wave = AddAnimationFromSmd(model, "a_wave", "src/anims/biped_wave.smd", 30);
    wave.addSubtract(refPose, 0, true).addWeightList(upper).setFrameRange(2, 12);

    smdl::Animation aligned = AddAnimationFromSmd(model, "a_aligned", "src/anims/lean_center.smd", 30);
    aligned.addAlignTo(walk).addAlignBone(bone("pelvis"), walk, M::X | M::Y, 0, 5).setSnap();

    smdl::Animation ik = AddAnimationFromSmd(model, "a_ik", "src/anims/lean_left.smd", 30);
    ik.setStartLoop(3).setNoStallFrames(4).setNoAnimBlock().setNoAnimBlockStall().setAngles(10, 20, 30);

    smdl::Animation ikRules = AddAnimationFromSmd(model, "a_ik_rules", "src/anims/lean_right.smd", 30);
    ikRules.setPercentStartLoop(0.25f).setFudgeLoop().setBlockName("BindPose").setFrameStart(1);
    ikRules.addIkRule(leftLeg, SMDL_IK_RULE_FOOTSTEP)
            .setRange(0, 3, 7, 11)
            .setFloor(1)
            .setHeight(10)
            .setPad(6)
            .setContact(2)
            .setTarget(1)
            .setUseSequence();
    ikRules.addIkRule(rightLeg, SMDL_IK_RULE_TOUCH)
            .setBone(bone("l_foot"))
            .setRange(0, 2, 4, 9)
            .setRadius(3)
            .setUseSource();
    ikRules.addIkRule(leftLeg, SMDL_IK_RULE_RELEASE).setRange(4, 5, 6, 7);
    ikRules.addIkFixup(rightLeg, SMDL_IK_RULE_ATTACHMENT)
            .setAttachment(model.findAttachment("muzzle"))
            .setRange(0, 1, 2, 3)
            .setFakeOrigin({1, 2, 3})
            .setFakeAngles({10, 20, 30});
    ikRules.addIkRule(rightLeg, SMDL_IK_RULE_UNLATCH).setRange(1, 2, 3, 4);

    smdl::Animation bones = AddAnimationFromSmd(model, "a_bones", "src/anims/lean_center.smd", 30);
    bones.addRotateTo(45)
            .addCounterRotate(bone("head"))
            .addCounterRotateTo(bone("spine"), 0, 90, 0)
            .addForceBonePosRot(bone("head"), smdl::Vec3{0, 0, 1}, smdl::Vec3{0, 30, 0}, true)
            .addBoneDriver(bone("head"), 1, 1.0f, 0, 1, 2, 3)
            .addBoneDriver(bone("spine"), 0, 1.0f)
            .addLocalHierarchy(bone("l_arm"), bone("spine"), 1, 2, 3, 4)
            .addLocalHierarchy(bone("r_arm"), bone("spine"));

    smdl::Animation misc = AddAnimationFromSmd(model, "a_misc", "src/anims/lean_left.smd", 30);
    misc.addDerivative(2).addLinearDelta();

    smdl::Animation spline = AddAnimationFromSmd(model, "a_spline", "src/anims/lean_right.smd", 30);
    spline.addSplineDelta().addNumFrames(6).addReverse();

    smdl::Animation appended = AddAnimationFromSmd(model, "a_appended", "src/anims/lean_center.smd", 30);
    appended.addAppendAnimation(idle)
            .addMatchBlend(walk, 0, 0, 0, 3)
            .addMatch(walk)
            .addWorldSpaceBlend(walk)
            .addWorldSpaceBlendLoop(walk, 2)
            .addWalkAlignTo(6, walk, M::LX)
            .addWalkAlign(8, walk, M::LX | M::LY, 0, 4)
            .addNoAnimation()
            .addNoAnimationKeepDuration();

    model.createSequence("idle").addAnimation(idle);
    model.createSequence("walk").addAnimation(walk);
    model.createSequence("wave").addAnimation(wave);
    model.createSequence("aligned").addAnimation(aligned);
    model.createSequence("ik").addAnimation(ik);
    model.createSequence("ik_rules").addAnimation(ikRules);
    model.createSequence("bones").addAnimation(bones);
    model.createSequence("misc").addAnimation(misc);
    model.createSequence("spline").addAnimation(spline);
    model.createSequence("appended").addAnimation(appended);
    model.createSequence("ref").addAnimation(refPose);

    RequireSameAsQc(model, "anim_cmds.qc");
}

SMDL_TEST(anim_equiv_sequence_options) {
    smdl::Model model("api/anim_seq");
    BasicGlobals(model, "flesh");
    AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);
    const auto bone = [&](std::string_view name) { return model.findBone(name); };

    const smdl::PoseParamId moveX = model.addPoseParameter("move_x", -1, 1);
    const smdl::PoseParamId turn = model.addPoseParameter("turn", -180, 180);
    model.setPoseParameterLoop(turn, 360);
    const smdl::PoseParamId aim = model.addPoseParameter("aim", 0, 90);
    model.setPoseParameterWrap(aim);
    model.addAttachment("muzzle", bone("r_arm"), {0, 0, 10});
    const smdl::IkChainId leg = model.addIkChain("l_leg", bone("l_foot"));
    model.setIkChainKnee(leg, {1, 0, 0});
    model.setIkChainHeight(leg, 18);
    model.setIkChainFloor(leg, 0);
    model.setIkChainPad(leg, 8);

    const auto loopAnimation = [&](const char *name, const char *smd) {
        smdl::Animation animation = AddAnimationFromSmd(model, name, smd, 30);
        animation.setLoop();
        return animation;
    };
    smdl::Animation base = loopAnimation("base", "src/anims/lean_center.smd");
    smdl::Animation g0 = loopAnimation("g0", "src/anims/lean_left.smd");
    smdl::Animation g1 = loopAnimation("g1", "src/anims/lean_right.smd");
    smdl::Animation g2 = loopAnimation("g2", "src/anims/lean_center.smd");
    smdl::Animation g3 = loopAnimation("g3", "src/anims/lean_left.smd");
    smdl::Animation single = loopAnimation("single", "src/anims/biped_walk.smd");

    smdl::Sequence walk = model.createSequence("walk");
    walk.addAnimation(single)
            .setActivity("ACT_WALK", 2)
            .addActivityModifier("crouch")
            .addActivityModifier("sneak")
            .addEvent("AE_FOOTSTEP", 4, "left")
            .addEvent("5004", 10)
            .addAnimTag("step", 0.5f)
            .setFadeIn(0.3f)
            .setFadeOut(0.4f)
            .setNode("stand")
            .setExitPhase(0.5f)
            .setFlags(smdl::SequenceFlag::Hidden | smdl::SequenceFlag::Realtime | smdl::SequenceFlag::Snap |
                      smdl::SequenceFlag::Post)
            .addIkLock(leg, 1, 0.5f)
            .addKeyValues(R"("bounds" { "min" "0 0 0" "max" "1 1 1" })");

    model.createSequence("grid")
            .addAnimation(g0)
            .addAnimation(g1)
            .addAnimation(g2)
            .addAnimation(g3)
            .setBlendWidth(2)
            .addBlend(moveX, -1, 1)
            .addBlend(turn, -180, 180)
            .setBlendReference(base)
            .setBlendCompare(base)
            .setBlendCenter(g2);

    model.createSequence("calc")
            .addAnimation(g0)
            .addAnimation(g1)
            .addAnimation(g2)
            .setBlendWidth(3)
            .addBlend(aim, 0, 90)
            .setPoseCycle(moveX);

    smdl::Sequence trans = model.createSequence("trans");
    trans.addAnimation(base).setTransition("stand", "crouch").setFlags(smdl::SequenceFlag::Autoplay);
    model.createSequence("rtrans")
            .addAnimation(base)
            .setTransition("crouch", "stand", true)
            .setFlags(smdl::SequenceFlag::Delta);
    model.createSequence("world").addAnimation(base).setFlags(smdl::SequenceFlag::WorldSpace).setRootDriver(bone("root"));
    model.createSequence("worldrel")
            .addAnimation(base)
            .setFlags(smdl::SequenceFlag::WorldRelative | smdl::SequenceFlag::PreDelta)
            .addLayer(walk, true)
            .addLayer(trans)
            .addBlendLayer(walk, 0, 2, 6, 8,
                           smdl::LayerFlag::Spline | smdl::LayerFlag::NoBlend | smdl::LayerFlag::Local |
                                   smdl::LayerFlag::Pose,
                           moveX)
            .addBlendLayer(trans, 1, 2, 3, 4);

    RequireSameAsQc(model, "anim_seq.qc");
}

// $append / $continue are further calls on the same sequence / animation; $prepend moves the last command first
SMDL_TEST(anim_append_and_prepend_match_the_qc_calls) {
    const auto build = [](bool oneShot) {
        smdl::Model model("api/anims");
        BasicGlobals(model, "flesh");
        AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);
        smdl::Animation idle = AddAnimationFromSmd(model, "a_idle", "src/anims/biped_idle.smd", 15);
        smdl::Sequence sequence = model.createSequence("idle");
        sequence.addAnimation(idle);
        if (oneShot) {
            idle.addReverse().addFixupLoop(1, 2); // $prepend of a fixuploop after a reverse
            idle.moveCommand(1, 0);
            sequence.setFadeIn(0.5f).addEvent("AE_X", 3);
        }
        else {
            idle.addFixupLoop(1, 2);
            idle.addReverse(); // the command order the move above produces: fixuploop, reverse
            sequence.addEvent("AE_X", 3);
            sequence.setFadeIn(0.5f);
        }
        return ResultFiles(model.compile());
    };
    RequireSameOutputs(build(false), build(true));
}

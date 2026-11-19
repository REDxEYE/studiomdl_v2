// Anim part tests, equivalence with the QC compile: animations, sequences (blends, events, activities, layers), pose
// parameters, attachments, hitboxes, IK chains, weight lists. The fixture QCs (anims.qc, blend.qc, rig.qc) create the
// animations of their sequences with explicit `$animation "@name"` commands, in the order the builder applies them
// (all animations in creation order, then the sequences), so the sources and entries appear in the same order.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

using namespace coretest;
using namespace animtest;

// ---------------------------------------------------------------------------------------------------------------
// anims.qc: $animation + $sequence pairs, motion extraction
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(anim_equiv_anims) {
    smdl::Model model("api/anims");
    BasicGlobals(model, "flesh");
    AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);

    smdl::Animation idle = AddAnimationFromSmd(model, "a_idle", "src/anims/biped_idle.smd", 15.0f);
    idle.setLoop();
    smdl::Animation walk = AddAnimationFromSmd(model, "@walk", "src/anims/biped_walk.smd", 30.0f);
    walk.setLoop().setMotionExtraction(smdl::Motion::LX | smdl::Motion::LY);
    smdl::Animation ref = AddAnimationFromSmd(model, "@ref", "src/biped.smd", 30.0f);

    model.createSequence("idle")
            .addAnimation(idle)
            .setFadeIn(0.2f)
            .setFadeOut(0.3f)
            .setFlags(smdl::SequenceFlag::Snap);
    model.createSequence("walk").addAnimation(walk);
    model.createSequence("ref").addAnimation(ref);

    RequireSameAsQc(model, "anims.qc");
}

// Quaternion frames are converted to the euler angles an SMD holds (the DMX loader's math), which is not exact to the
// last bit: the compile must give the same files with the same sizes, differing only in a few bytes of quantized
// animation values and the checksum derived from them.
SMDL_TEST(anim_quaternion_frames_match_euler_frames) {
    const auto build = [](RotationForm form) {
        smdl::Model model("api/anims");
        BasicGlobals(model, "flesh");
        AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);
        smdl::Animation idle = AddAnimationFromSmd(model, "a_idle", "src/anims/biped_idle.smd", 15.0f, form);
        idle.setLoop();
        smdl::Animation walk = AddAnimationFromSmd(model, "@walk", "src/anims/biped_walk.smd", 30.0f, form);
        walk.setLoop().setMotionExtraction(smdl::Motion::LX | smdl::Motion::LY);
        smdl::Animation ref = AddAnimationFromSmd(model, "@ref", "src/biped.smd", 30.0f, form);
        model.createSequence("idle").addAnimation(idle);
        model.createSequence("walk").addAnimation(walk);
        model.createSequence("ref").addAnimation(ref);
        return ResultFiles(model.compile());
    };
    const Outputs euler = build(RotationForm::Euler);
    const Outputs quaternion = build(RotationForm::Quaternion);
    Require(euler.size() == quaternion.size(), "same files");
    for (const auto &[name, bytes]: euler) {
        const auto other = quaternion.find(name);
        Require(other != quaternion.end(), "file " + name);
        Require(other->second.size() == bytes.size(), "size of " + name);
        size_t different = 0;
        for (size_t i = 0; i < bytes.size(); ++i)
            different += bytes[i] != other->second[i];
        Require(different * 100 <= bytes.size(), "more than 1% of " + name + " differs");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// blend.qc: pose parameter, three-way blend with events and an activity
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(anim_equiv_blend) {
    smdl::Model model("api/blend");
    BasicGlobals(model, "flesh");
    AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);

    const smdl::PoseParamId moveX = model.addPoseParameter("move_x", -1.0f, 1.0f);

    smdl::Animation left = AddAnimationFromSmd(model, "a_left", "src/anims/lean_left.smd", 30.0f);
    left.setLoop();
    smdl::Animation center = AddAnimationFromSmd(model, "a_center", "src/anims/lean_center.smd", 30.0f);
    center.setLoop();
    smdl::Animation right = AddAnimationFromSmd(model, "a_right", "src/anims/lean_right.smd", 30.0f);
    right.setLoop();
    smdl::Animation idleAnim = AddAnimationFromSmd(model, "@idle", "src/biped.smd", 30.0f);

    model.createSequence("lean")
            .addAnimation(left)
            .addAnimation(center)
            .addAnimation(right)
            .setBlendWidth(3)
            .addBlend(moveX, -1.0f, 1.0f)
            .setActivity("ACT_WALK", 1)
            .addEvent("AE_FOOTSTEP", 4, "left")
            .addEvent("AE_FOOTSTEP", 10, "right")
            .setFadeIn(0.1f);
    model.createSequence("idle").addAnimation(idleAnim);

    RequireSameAsQc(model, "blend.qc");
}

// An event placed by cycle is the event of the nearest frame of the first animation: frame 4 of 12 frames is cycle
// 4 / 11
SMDL_TEST(anim_equiv_blend_event_cycle) {
    smdl::Model model("api/blend");
    BasicGlobals(model, "flesh");
    AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);

    const smdl::PoseParamId moveX = model.addPoseParameter("move_x", -1.0f, 1.0f);

    smdl::Animation left = AddAnimationFromSmd(model, "a_left", "src/anims/lean_left.smd", 30.0f);
    left.setLoop();
    smdl::Animation center = AddAnimationFromSmd(model, "a_center", "src/anims/lean_center.smd", 30.0f);
    center.setLoop();
    smdl::Animation right = AddAnimationFromSmd(model, "a_right", "src/anims/lean_right.smd", 30.0f);
    right.setLoop();
    smdl::Animation idleAnim = AddAnimationFromSmd(model, "@idle", "src/biped.smd", 30.0f);

    model.createSequence("lean")
            .addAnimation(left)
            .addAnimation(center)
            .addAnimation(right)
            .setBlendWidth(3)
            .addBlend(moveX, -1.0f, 1.0f)
            .setActivity("ACT_WALK", 1)
            .addEventCycle("AE_FOOTSTEP", 4.0f / 11.0f, "left")
            .addEventCycle("AE_FOOTSTEP", 10.0f / 11.0f, "right")
            .setFadeIn(0.1f);
    model.createSequence("idle").addAnimation(idleAnim);

    RequireSameAsQc(model, "blend.qc");
}

// ---------------------------------------------------------------------------------------------------------------
// rig.qc: attachments, hit groups, hitbox sets, bone merge, weight list, layered sequence, IK chains and locks
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(anim_equiv_rig) {
    smdl::Model model("api/rig");
    BasicGlobals(model, "flesh");
    AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);

    const auto bone = [&](std::string_view name) { return model.findBone(name); };

    const smdl::AttachmentId headTop = model.addAttachment("head_top", bone("head"), {0, 0, 12});
    model.setAttachmentAngles(headTop, {0, 90, 0});
    const smdl::AttachmentId eyePos = model.addAttachment("eye_pos", bone("head"), {4, 0, 2});
    model.setAttachmentAxes(eyePos, {1, 0, 0}, {0, 0, 1});
    const smdl::AttachmentId handAbs = model.addAttachment("hand_abs", bone("l_arm"), {0, 0, 14});
    model.setAttachmentAbsolute(handAbs);
    const smdl::AttachmentId rootRigid = model.addAttachment("root_rigid", bone("root"), {0, 0, 0});
    model.setAttachmentRigid(rootRigid);
    const smdl::AttachmentId footWa = model.addAttachment("foot_wa", bone("l_foot"), {0, 0, 2});
    model.setAttachmentAngles(footWa, {0, 0, 45});
    model.setAttachmentWorldAlign(footWa);

    model.addHitGroup(1, bone("head"));
    model.addHitGroup(2, bone("spine"));
    model.addHitGroup(3, bone("pelvis"));
    model.addHitGroup(4, bone("l_arm"));
    model.addHitGroup(5, bone("r_arm"));
    model.addHitGroup(6, bone("l_thigh"));
    model.addHitGroup(7, bone("r_thigh"));

    const smdl_hitbox_set_id standing = model.addHitboxSet("default");
    model.addHitbox(standing, 1, bone("head"), {-5, -5, 0}, {5, 5, 10});
    const uint32_t chest = model.addHitbox(standing, 2, bone("spine"), {-7, -7, 0}, {7, 7, 18});
    model.setHitboxAngles(standing, chest, {0, 10, 0});
    model.setHitboxName(standing, chest, "chest_box");
    model.addHitbox(standing, 3, bone("pelvis"), {-7, -7, -2}, {7, 7, 10});
    model.addHitbox(standing, 4, bone("l_arm"), {-2.5f, -2.5f, 0}, {2.5f, 2.5f, 14});
    model.addHitbox(standing, 5, bone("r_arm"), {-2.5f, -2.5f, 0}, {2.5f, 2.5f, 14});
    model.addHitbox(standing, 6, bone("l_thigh"), {-3, -3, 0}, {3, 3, 16});
    model.addHitbox(standing, 7, bone("r_thigh"), {-3, -3, 0}, {3, 3, 16});

    const smdl_hitbox_set_id crouch = model.addHitboxSet("crouch");
    model.addHitbox(crouch, 1, bone("head"), {-5, -5, 0}, {5, 5, 10});
    model.addHitbox(crouch, 3, bone("pelvis"), {-7, -7, -2}, {7, 7, 10});

    model.setBoneMerge(bone("head"), true);

    const smdl::WeightListId upper = model.addWeightList("upperbody");
    model.setWeightListBone(upper, bone("root"), 0.0f);
    model.setWeightListBone(upper, bone("pelvis"), 0.0f);
    model.setWeightListBone(upper, bone("spine"), 1.0f);
    model.setWeightListBone(upper, bone("head"), 1.0f);
    model.setWeightListBone(upper, bone("l_arm"), 1.0f);
    model.setWeightListBone(upper, bone("r_arm"), 1.0f);

    smdl::Animation idleAnim = AddAnimationFromSmd(model, "@idle", "src/biped.smd", 30.0f);
    smdl::Animation wave = AddAnimationFromSmd(model, "@wave", "src/anims/biped_wave.smd", 30.0f);
    wave.addWeightList(upper);
    smdl::Animation waveLayered = AddAnimationFromSmd(model, "@wave_layered", "src/anims/biped_wave.smd", 30.0f);
    waveLayered.addWeightList(upper);

    smdl::Sequence idle = model.createSequence("idle");
    idle.addAnimation(idleAnim);
    smdl::Sequence waveSequence = model.createSequence("wave");
    waveSequence.addAnimation(wave);
    smdl::Sequence layered = model.createSequence("wave_layered");
    layered.addAnimation(waveLayered).addLayer(idle).addBlendLayer(waveSequence, 0, 2, 10, 14, smdl::LayerFlag::XFade);

    const smdl::IkChainId left = model.addIkChain("l_leg", bone("l_foot"));
    model.setIkChainKnee(left, {1, 0, 0});
    model.setIkChainHeight(left, 18);
    model.setIkChainFloor(left, 0);
    model.setIkChainPad(left, 8);
    const smdl::IkChainId right = model.addIkChain("r_leg", bone("r_foot"));
    model.setIkChainKnee(right, {1, 0, 0});
    model.setIkChainCenter(right, {0, 0, 0});
    model.addIkAutoplayLock(left, 1.0f, 0.1f);
    model.addIkAutoplayLock(right, 0.9f, 0.2f);

    RequireSameAsQc(model, "rig.qc");
}

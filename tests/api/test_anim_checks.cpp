// Anim part tests: call-time validation (the call that is wrong fails, with the status that tells why) and compile
// errors (the failure names the animation, sequence or rig object it is about).
#include "core_helpers.hpp"

#include "anim_helpers.hpp"

#include <limits>

using namespace coretest;
using namespace animtest;

namespace {
    // A biped model with its body, ready for animations
    void BiPed(smdl::Model &model) {
        BasicGlobals(model, "flesh");
        AddBody(model, "biped", "src/biped.smd", "src/biped.smd", VertexMode::Indexed);
    }

    smdl_status StatusOf(const std::function<void()> &call) { return CaptureError(call).status(); }

    // The model's compile failure
    smdl::Error CompileError(smdl::Model &model) {
        return CaptureError([&] { (void) model.compile(); });
    }

    // A sequence for the rig tests of the compile errors
    void OneAnimationSequence(smdl::Model &model, const char *name) {
        smdl::Animation animation = model.createAnimation(std::string("@") + name, 30.0f, 1);
        model.createSequence(name).addAnimation(animation);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Call-time checks
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(anim_check_animation_creation) {
    smdl::Model model("api/checks");
    BiPed(model);
    Require(StatusOf([&] { model.createAnimation("", 30, 1); }) == SMDL_ERROR_INVALID_ARGUMENT, "empty name");
    Require(StatusOf([&] { model.createAnimation(std::string(200, 'a'), 30, 1); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "long name");
    Require(StatusOf([&] { model.createAnimation("a", 0.0f, 1); }) == SMDL_ERROR_INVALID_ARGUMENT, "fps 0");
    Require(StatusOf([&] { model.createAnimation("a", std::nanf(""), 1); }) == SMDL_ERROR_INVALID_ARGUMENT, "fps NaN");
    Require(StatusOf([&] { model.createAnimation("a", 30, 0); }) == SMDL_ERROR_LIMIT, "0 frames");
    Require(StatusOf([&] { model.createAnimation("a", 30, 5001); }) == SMDL_ERROR_LIMIT, "5001 frames");

    smdl::Animation first = model.createAnimation("Walk", 30, 4);
    Require(first.name() == "Walk" && first.frameCount() == 4 && first.fps() == 30.0f, "getters");
    Require(StatusOf([&] { model.createAnimation("walk", 30, 4); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate (case)");
    model.createSequence("run");
    Require(StatusOf([&] { model.createAnimation("RUN", 30, 4); }) == SMDL_ERROR_DUPLICATE_NAME,
            "animation named like a sequence");
    Require(StatusOf([&] { model.createSequence("walk"); }) == SMDL_ERROR_DUPLICATE_NAME,
            "sequence named like an animation");
    Require(StatusOf([&] { model.createSequence("RUN"); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate sequence");

    Require(model.findAnimation("WALK").handle() == first.handle(), "find ignores case");
    Require(StatusOf([&] { model.findAnimation("nothing"); }) == SMDL_ERROR_NOT_FOUND, "find unknown");
    Require(model.animationCount() == 1 && model.animation(0).handle() == first.handle(), "get by index");
    Require(StatusOf([&] { model.animation(1); }) == SMDL_ERROR_OUT_OF_RANGE, "index out of range");
    Require(model.sequenceCount() == 1 && model.findSequence("Run").name() == "run", "sequence lookup");
}

SMDL_TEST(anim_check_frames) {
    smdl::Model model("api/checks");
    BiPed(model);
    smdl::Animation animation = model.createAnimation("a", 30, 3);
    const std::vector<smdl::Vec3> positions(3, smdl::Vec3{1, 2, 3});
    const std::vector<smdl::Quat> rotations(3, smdl::Quat{0, 0, 0, 1});
    const smdl::BoneId root = model.findBone("root");

    animation.setBoneFrames(root, positions, rotations);
    animation.setBoneFrames(root, positions, {}); // positions only: the rotations keep the rest pose
    animation.setBoneFrames(root, {}, {});        // no data: removes the bone's frames

    Require(StatusOf([&] { animation.setBoneFrames(model.boneCount() + 5, positions, rotations); }) ==
                    SMDL_ERROR_OUT_OF_RANGE,
            "unknown bone");
    Require(StatusOf([&] { animation.setBoneFrames(smdl::NoBone, positions, rotations); }) == SMDL_ERROR_OUT_OF_RANGE,
            "no bone");
    const std::vector<smdl::Vec3> two(2, smdl::Vec3{});
    Require(StatusOf([&] { animation.setBoneFrames(root, two, rotations); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "two positions for three frames");
    const std::vector<smdl::Quat> shortRotations(2, smdl::Quat{0, 0, 0, 1});
    Require(StatusOf([&] { animation.setBoneFrames(root, positions, shortRotations); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "two rotations for three frames");
    std::vector<smdl::Vec3> bad = positions;
    bad[1][0] = std::numeric_limits<float>::infinity();
    Require(StatusOf([&] { animation.setBoneFrames(root, bad, rotations); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "infinite position");
    const std::vector<smdl::Quat> zero(3, smdl::Quat{0, 0, 0, 0});
    Require(StatusOf([&] { animation.setBoneFrames(root, positions, zero); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "zero quaternion");
    Require(StatusOf([&] { animation.setRestPose(model.boneCount(), {0, 0, 0}, {0, 0, 0, 1}); }) ==
                    SMDL_ERROR_OUT_OF_RANGE,
            "rest pose of an unknown bone");
}

SMDL_TEST(anim_check_options_and_commands) {
    smdl::Model model("api/checks");
    BiPed(model);
    smdl::Animation animation = model.createAnimation("a", 30, 10);
    smdl::Animation other = model.createAnimation("b", 30, 4);
    const smdl::BoneId root = model.findBone("root");

    Require(StatusOf([&] { animation.setFps(0.0f); }) == SMDL_ERROR_INVALID_ARGUMENT, "fps 0");
    Require(StatusOf([&] { animation.setScale(std::nanf("")); }) == SMDL_ERROR_INVALID_ARGUMENT, "scale NaN");
    Require(StatusOf([&] { animation.setStartLoop(10); }) == SMDL_ERROR_OUT_OF_RANGE, "start loop frame 10 of 10");
    Require(StatusOf([&] { animation.setFrameRange(-1, 4); }) == SMDL_ERROR_OUT_OF_RANGE, "negative frame range");
    Require(StatusOf([&] { animation.setMotionExtraction(0x80000000u); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "unknown motion bit");
    Require(StatusOf([&] { animation.setBlockName(""); }) == SMDL_ERROR_INVALID_ARGUMENT, "empty block name");

    Require(StatusOf([&] { animation.addFixupLoop(0, 10); }) == SMDL_ERROR_OUT_OF_RANGE, "frame 10 of 10");
    Require(StatusOf([&] { animation.addFixupLoop(-1, 3); }) == SMDL_ERROR_OUT_OF_RANGE, "frame -1");
    Require(StatusOf([&] { animation.addSubtract(other, 4); }) == SMDL_ERROR_OUT_OF_RANGE,
            "subtract frame 4 of the 4 frames of the reference");
    Require(StatusOf([&] { animation.addAlign(other, 0, 0, 0); }) == SMDL_ERROR_INVALID_ARGUMENT, "align without motion");
    Require(StatusOf([&] { animation.addAlign(other, smdl::Motion::X, 0, 10); }) == SMDL_ERROR_OUT_OF_RANGE,
            "destination frame");
    Require(StatusOf([&] { animation.addWeightList(5); }) == SMDL_ERROR_OUT_OF_RANGE, "unknown weight list");
    Require(StatusOf([&] { animation.addCounterRotate(model.boneCount()); }) == SMDL_ERROR_OUT_OF_RANGE, "bad bone");
    Require(StatusOf([&] { animation.addBoneDriver(root, 3, 1.0f); }) == SMDL_ERROR_INVALID_ARGUMENT, "driver axis 3");
    Require(StatusOf([&] { animation.addNumFrames(0); }) == SMDL_ERROR_OUT_OF_RANGE, "numframes 0");
    Require(StatusOf([&] { animation.addIkRule(0, SMDL_IK_RULE_FOOTSTEP); }) == SMDL_ERROR_OUT_OF_RANGE, "no ik chains");
    Require(animation.commandCount() == 0, "failed calls added nothing");

    // valid commands, the default weight list (0) exists
    animation.addWeightList(smdl::DefaultWeightList).addReverse();
    Require(animation.commandCount() == 2, "two commands");
    Require(StatusOf([&] { animation.moveCommand(0, 2); }) == SMDL_ERROR_OUT_OF_RANGE, "move to 2");
    animation.moveCommand(1, 0);

    // limits: 64 commands
    smdl::Animation busy = model.createAnimation("busy", 30, 2);
    for (int i = 0; i < 64; ++i)
        busy.addReverse();
    Require(StatusOf([&] { busy.addReverse(); }) == SMDL_ERROR_LIMIT, "65th command");
    Require(StatusOf([&] { busy.addMatch(other); }) == SMDL_ERROR_LIMIT, "65th command (match)");
}

SMDL_TEST(anim_check_other_model) {
    smdl::Model model("api/checks");
    BiPed(model);
    smdl::Model other("api/checks_other");
    BiPed(other);
    smdl::Animation own = model.createAnimation("a", 30, 4);
    smdl::Animation foreign = other.createAnimation("a", 30, 4);
    smdl::Sequence sequence = model.createSequence("s");
    smdl::Sequence foreignSequence = other.createSequence("s");

    Require(StatusOf([&] { own.addMatch(foreign); }) == SMDL_ERROR_INVALID_ARGUMENT, "command with a foreign animation");
    Require(StatusOf([&] { sequence.addAnimation(foreign); }) == SMDL_ERROR_INVALID_ARGUMENT, "foreign blend animation");
    Require(StatusOf([&] { sequence.setBlendReference(foreign); }) == SMDL_ERROR_INVALID_ARGUMENT, "foreign blend ref");
    Require(StatusOf([&] { sequence.addLayer(foreignSequence); }) == SMDL_ERROR_INVALID_ARGUMENT, "foreign layer");
    Require(StatusOf([&] { sequence.addBlendLayer(foreignSequence, 0, 1, 2, 3); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "foreign blend layer");
}

SMDL_TEST(anim_check_sequence) {
    smdl::Model model("api/checks");
    BiPed(model);
    const smdl::PoseParamId pose = model.addPoseParameter("p", 0, 1);
    smdl::Animation animation = model.createAnimation("a", 30, 4);
    smdl::Sequence sequence = model.createSequence("s");
    sequence.addAnimation(animation);

    Require(StatusOf([&] { sequence.addBlend(pose + 1, 0, 1); }) == SMDL_ERROR_OUT_OF_RANGE, "unknown pose parameter");
    sequence.addBlend(pose, 0, 1).addBlend(pose, 0, 2);
    Require(StatusOf([&] { sequence.addBlend(pose, 0, 3); }) == SMDL_ERROR_LIMIT, "third blend axis");
    Require(StatusOf([&] { sequence.addCalcBlend(pose, 0, smdl::Motion::X); }) == SMDL_ERROR_OUT_OF_RANGE,
            "calcblend with an unknown attachment");
    Require(StatusOf([&] { sequence.setPoseCycle(9); }) == SMDL_ERROR_OUT_OF_RANGE, "pose cycle");
    Require(StatusOf([&] { sequence.setBlendWidth(0); }) == SMDL_ERROR_INVALID_ARGUMENT, "blend width 0");
    Require(StatusOf([&] { sequence.setFadeIn(std::nanf("")); }) == SMDL_ERROR_INVALID_ARGUMENT, "fade NaN");
    Require(StatusOf([&] { sequence.setFlags(0x80000000u); }) == SMDL_ERROR_INVALID_ARGUMENT, "unknown flag");
    Require(StatusOf([&] { sequence.setRootDriver(model.boneCount()); }) == SMDL_ERROR_OUT_OF_RANGE, "root driver bone");
    Require(StatusOf([&] { sequence.setActivity("", 1); }) == SMDL_ERROR_INVALID_ARGUMENT, "empty activity");
    Require(StatusOf([&] { sequence.addEvent("AE_X", -1); }) == SMDL_ERROR_OUT_OF_RANGE, "negative event frame");
    Require(StatusOf([&] { sequence.addEvent("AE_X", 1, std::string(64, 'x')); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "event options of 64 bytes");
    Require(StatusOf([&] { sequence.addEventCycle("AE_X", std::nanf("")); }) == SMDL_ERROR_OUT_OF_RANGE, "cycle NaN");
    Require(StatusOf([&] { sequence.addIkLock(0, 1, 1); }) == SMDL_ERROR_OUT_OF_RANGE, "unknown ik chain");
    Require(StatusOf([&] { sequence.addKeyValues("unbalanced { \"a\" \"b\" "); }) == SMDL_ERROR_SYNTAX, "keyvalues syntax");
    sequence.addKeyValues("mdlkey { \"a\" \"b\" }");
}

SMDL_TEST(anim_check_rig) {
    smdl::Model model("api/checks");
    BiPed(model);
    const smdl::BoneId head = model.findBone("head");
    const smdl::BoneId bad = model.boneCount();

    const smdl::AttachmentId attachment = model.addAttachment("a", head, {0, 0, 0});
    Require(attachment == 0 && model.attachmentCount() == 1, "attachment id");
    Require(StatusOf([&] { model.addAttachment("A", head, {0, 0, 0}); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate");
    Require(StatusOf([&] { model.addAttachment("b", bad, {0, 0, 0}); }) == SMDL_ERROR_OUT_OF_RANGE, "bad bone");
    Require(StatusOf([&] { model.addAttachment("c", head, {std::nanf(""), 0, 0}); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "NaN position");
    Require(StatusOf([&] { model.setAttachmentAngles(7, {0, 0, 0}); }) == SMDL_ERROR_OUT_OF_RANGE, "attachment id");
    Require(StatusOf([&] { model.setAttachmentAxes(attachment, {0, 0, 0}, {0, 0, 1}); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "zero axis");
    Require(StatusOf([&] { model.setAttachmentQuaternion(attachment, {0, 0, 0, 0}); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "zero quaternion");
    Require(model.findAttachment("A") == attachment, "find attachment");
    Require(StatusOf([&] { model.findAttachment("z"); }) == SMDL_ERROR_NOT_FOUND, "find unknown attachment");
    model.redefineAttachment(attachment, head, {1, 2, 3});

    const smdl_hitbox_set_id set = model.addHitboxSet("default");
    Require(StatusOf([&] { model.addHitboxSet("DEFAULT"); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate hitbox set");
    Require(StatusOf([&] { model.addHitbox(set + 1, 0, head, {0, 0, 0}, {1, 1, 1}); }) == SMDL_ERROR_OUT_OF_RANGE,
            "hitbox set id");
    Require(StatusOf([&] { model.addHitbox(set, 0, bad, {0, 0, 0}, {1, 1, 1}); }) == SMDL_ERROR_OUT_OF_RANGE, "hitbox bone");
    const uint32_t box = model.addHitbox(set, 0, head, {0, 0, 0}, {1, 1, 1});
    Require(StatusOf([&] { model.setHitboxName(set, box + 1, "x"); }) == SMDL_ERROR_OUT_OF_RANGE, "hitbox index");
    Require(StatusOf([&] { model.addHitGroup(1, bad); }) == SMDL_ERROR_OUT_OF_RANGE, "hit group bone");

    const smdl::PoseParamId pose = model.addPoseParameter("p", 0, 1);
    Require(StatusOf([&] { model.addPoseParameter("p", 0, 1); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate pose");
    Require(StatusOf([&] { model.addPoseParameter("q", 2, 1); }) == SMDL_ERROR_INVALID_ARGUMENT, "min above max");
    Require(StatusOf([&] { model.setPoseParameterLoop(pose + 1, 1.0f); }) == SMDL_ERROR_OUT_OF_RANGE, "pose id");
    model.setPoseParameterWrap(pose);
    for (int i = 1; i < 24; ++i)
        model.addPoseParameter("p" + std::to_string(i), 0, 1);
    Require(StatusOf([&] { model.addPoseParameter("one_too_many", 0, 1); }) == SMDL_ERROR_LIMIT, "25th pose parameter");

    const smdl::IkChainId chain = model.addIkChain("leg", head);
    Require(StatusOf([&] { model.addIkChain("LEG", head); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate chain");
    Require(StatusOf([&] { model.setIkChainAxis(chain, smdl::Motion::X | smdl::Motion::Y, 1.0f); }) ==
                    SMDL_ERROR_INVALID_ARGUMENT,
            "two axes");
    model.setIkChainAxis(chain, smdl::Motion::Z, -1.0f);
    Require(StatusOf([&] { model.addIkAutoplayLock(chain + 1, 1, 1); }) == SMDL_ERROR_OUT_OF_RANGE, "lock chain");
    Require(model.addIkAutoplayLock(chain, 1, 1) == 0, "lock index");

    const smdl::WeightListId list = model.addWeightList("w");
    Require(list == 1 && model.weightListCount() == 2, "weight list ids start after the default list");
    Require(StatusOf([&] { model.addWeightList("W"); }) == SMDL_ERROR_DUPLICATE_NAME, "duplicate weight list");
    Require(StatusOf([&] { model.setWeightListBone(list, bad, 1.0f); }) == SMDL_ERROR_OUT_OF_RANGE, "weight bone");
    Require(StatusOf([&] { model.setWeightListBone(list + 1, head, 1.0f); }) == SMDL_ERROR_OUT_OF_RANGE, "list id");
    model.setWeightListBone(smdl::DefaultWeightList, head, 1.0f);
    Require(model.findWeightList("W") == list, "find weight list");
    Require(StatusOf([&] { model.findWeightList("default"); }) == SMDL_ERROR_NOT_FOUND, "the default list has no name");
}

// ---------------------------------------------------------------------------------------------------------------
// Compile errors name the object
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(anim_error_sequence_without_animation) {
    smdl::Model model("api/errors");
    BiPed(model);
    OneAnimationSequence(model, "fine");
    model.createSequence("empty");
    const smdl::Error error = CompileError(model);
    Require(error.status() == SMDL_ERROR_COMPILE, "compile error");
    Require(Contains(error.object(), "sequence 'empty'"), "names the sequence: " + error.object());
    Require(Contains(error.what(), "no animations"), std::string("message: ") + error.what());
}

SMDL_TEST(anim_error_non_square_blend) {
    smdl::Model model("api/errors");
    BiPed(model);
    const smdl::PoseParamId pose = model.addPoseParameter("p", 0, 1);
    smdl::Sequence sequence = model.createSequence("grid");
    for (int i = 0; i < 5; ++i)
        sequence.addAnimation(model.createAnimation("g" + std::to_string(i), 30, 1));
    sequence.addBlend(pose, 0, 1);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "sequence 'grid'"), "names the sequence: " + error.object());
    Require(Contains(error.what(), "non-square"), std::string("message: ") + error.what());
}

SMDL_TEST(anim_error_zero_activity_weight) {
    smdl::Model model("api/errors");
    BiPed(model);
    OneAnimationSequence(model, "walk");
    model.findSequence("walk").setActivity("ACT_WALK", 0);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "sequence 'walk'"), "names the sequence: " + error.object());
    Require(Contains(error.what(), "zero weight"), std::string("message: ") + error.what());
}

SMDL_TEST(anim_error_reference_created_later) {
    smdl::Model model("api/errors");
    BiPed(model);
    smdl::Animation first = model.createAnimation("first", 30, 2);
    smdl::Animation second = model.createAnimation("second", 30, 2);
    first.addMatch(second); // the compile adds `first` before `second` exists
    model.createSequence("s").addAnimation(first);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "animation 'first'"), "names the animation: " + error.object());
    Require(Contains(error.what(), "second"), std::string("message: ") + error.what());
}

SMDL_TEST(anim_error_start_loop_conflict) {
    smdl::Model model("api/errors");
    BiPed(model);
    smdl::Animation animation = model.createAnimation("looper", 30, 8);
    animation.setStartLoop(2).setPercentStartLoop(0.5f);
    model.createSequence("s").addAnimation(animation);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "animation 'looper'"), "names the animation: " + error.object());
}

SMDL_TEST(anim_error_frame_range) {
    smdl::Model model("api/errors");
    BiPed(model);
    smdl::Animation animation = model.createAnimation("range", 30, 8);
    animation.setFrameRange(6, 3);
    model.createSequence("s").addAnimation(animation);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "animation 'range'"), "names the animation: " + error.object());
    Require(Contains(error.what(), "end frame before start frame"), std::string("message: ") + error.what());
}

SMDL_TEST(anim_error_unknown_block_name) {
    smdl::Model model("api/errors");
    BiPed(model);
    smdl::Animation animation = model.createAnimation("blocks", 30, 2);
    animation.setBlockName("nothere");
    model.createSequence("s").addAnimation(animation);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "animation 'blocks'"), "names the animation: " + error.object());
}

SMDL_TEST(anim_error_ik_rule_without_bone) {
    smdl::Model model("api/errors");
    BiPed(model);
    const smdl::IkChainId chain = model.addIkChain("leg", model.findBone("l_foot"));
    smdl::Animation animation = model.createAnimation("touching", 30, 2);
    animation.addIkRule(chain, SMDL_IK_RULE_TOUCH); // `touch` needs smdl_ik_rule_set_bone
    model.createSequence("s").addAnimation(animation);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "animation 'touching'"), "names the animation: " + error.object());
}

SMDL_TEST(anim_error_weight_list) {
    smdl::Model model("api/errors");
    BiPed(model);
    const smdl::WeightListId list = model.addWeightList("broken");
    model.setWeightListBone(list, model.findBone("head"), 0.0f, 1.0f); // position weight without rotation weight
    OneAnimationSequence(model, "s");
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "weight list 'broken'"), "names the weight list: " + error.object());
}

SMDL_TEST(anim_error_unknown_activity) {
    smdl::Model model("api/errors");
    BiPed(model);
    model.addAllowedActivityName("ACT_ALLOWED");
    OneAnimationSequence(model, "s");
    model.findSequence("s").setActivity("ACT_NOT_ALLOWED", 1);
    const smdl::Error error = CompileError(model);
    Require(Contains(error.object(), "sequence 's'"), "names the sequence: " + error.object());
    Require(Contains(error.what(), "ACT_NOT_ALLOWED"), std::string("message: ") + error.what());
}

// The first failing object is reported even when later objects are fine, and a model whose objects are all valid
// compiles again after an error was fixed (the process stays reusable)
SMDL_TEST(anim_error_then_success) {
    smdl::Model model("api/errors");
    BiPed(model);
    smdl::Sequence empty = model.createSequence("empty");
    Require(CompileError(model).status() == SMDL_ERROR_COMPILE, "first compile fails");
    empty.addAnimation(model.createAnimation("@empty", 30, 1));
    const smdl::Result result = model.compile();
    Require(!ResultFiles(result).empty(), "second compile works");
}

// The QC path reports the checks of the shared apply functions with the position in the script
SMDL_TEST(anim_error_qc_keeps_script_position) {
    const ReferenceRun run = RunReference(FixturePath("anim_error_activity.qc"));
    Require(run.exitCode != 0, "the QC fails");
    Require(Contains(run.log, "anim_error_activity.qc("), "file and line in the log: " + run.log);
    Require(Contains(run.log, "zero weight"), "message in the log: " + run.log);
}

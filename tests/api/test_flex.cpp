// Flex part tests: equivalence with the QC compile (flex.qc, flex_more.qc), call-time validation, compile errors that
// name the object, DMX style eyelids. The shapes are the frames of the fixture VTAs (flex_helpers.hpp); the QCs put the
// model-wide lines (localvar, mouth, flexcontroller, %rules) at the end of the last $model block, which is where the
// builder applies them.
#include "core_helpers.hpp"

#include "anim_helpers.hpp"
#include "flex_helpers.hpp"

using namespace coretest;
using namespace flextest;

namespace {
    struct Meshes {
        smdl::Mesh face;
        smdl::Mesh eyes;
    };

    // face.smd / eyes.smd as two bodygroups named like the QC's $model names, shapes from the VTAs
    Meshes AddFaceAndEyes(smdl::Model &model) {
        smdl::BodyGroup faceGroup = model.addBodyGroup("face");
        const SmdFile faceSmd = ReadSmd(FixturePath("src/face.smd"));
        smdl::Mesh face = AddMesh(model, faceGroup, "src/face.smd", "src/face.smd", VertexMode::Corners);
        AddVtaShapes(face.geometry(), faceSmd, VertexMode::Corners, ReadVta(FixturePath("src/face.vta")));

        smdl::BodyGroup eyesGroup = model.addBodyGroup("eyes");
        const SmdFile eyesSmd = ReadSmd(FixturePath("src/eyes.smd"));
        smdl::Mesh eyes = AddMesh(model, eyesGroup, "src/eyes.smd", "src/eyes.smd", VertexMode::Corners);
        AddVtaShapes(eyes.geometry(), eyesSmd, VertexMode::Corners, ReadVta(FixturePath("src/eyes.vta")));
        return {face, eyes};
    }

    void AddFaceIdle(smdl::Model &model) {
        smdl::Animation idle = animtest::AddAnimationFromSmd(model, "a_idle", "src/anims/face_idle.smd", 30.0f);
        idle.setLoop();
        model.createSequence("idle").addAnimation(idle);
    }

    void AddRightLeftEyeballs(smdl::Model &model, const smdl::Mesh &eyes) {
        eyes.addEyeball("right_eye", model.findBone("r_eye"), {2.5f, 4.0f, 1.5f}, "eyeball_r", 2.4f, 4.0f, 0.9f);
        eyes.addEyeball("left_eye", model.findBone("l_eye"), {-2.5f, 4.0f, 1.5f}, "eyeball_l", 2.4f, 4.0f, 0.9f);
    }

    // A face and eyes model with flex-free commands only: the base of the validation tests
    smdl::Model FaceModel() {
        smdl::Model model("api/flex");
        BasicGlobals(model, "flesh");
        return model;
    }

    smdl_status StatusOf(const std::function<void()> &call) { return CaptureError(call).status(); }
}

// ---------------------------------------------------------------------------------------------------------------
// Equivalence with the QC compile
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(flex_equiv_flex) {
    smdl::Model model("api/flex");
    BasicGlobals(model, "flesh");
    const Meshes meshes = AddFaceAndEyes(model);

    // $model "face": defaultflex frame 0, flex blink / smile (pair) / jaw_open from frames 1, 2, 3
    meshes.face.setDefaultFlex(smdl::NoId);
    meshes.face.addFlex("blink", 0);
    meshes.face.addFlexPair("smile", 0.4f, 1);
    meshes.face.addFlex("jaw_open", 2);

    // $model "eyes": eyeballs, eyelids from frames 1..4
    AddRightLeftEyeballs(model, meshes.eyes);
    const smdl::EyelidId right = meshes.eyes.addEyelid("upper_right", smdl::EyelidKind::Upper);
    meshes.eyes.setEyelidState(right, smdl::EyelidState::Lowerer, 0, -0.8f);
    meshes.eyes.setEyelidState(right, smdl::EyelidState::Neutral, smdl::NoId, 0.0f);
    meshes.eyes.setEyelidState(right, smdl::EyelidState::Raiser, 1, 0.5f);
    meshes.eyes.setEyelidSplit(right, 0.1f);
    meshes.eyes.setEyelidEyeball(right, 0);
    const smdl::EyelidId left = meshes.eyes.addEyelid("upper_left", smdl::EyelidKind::Upper);
    meshes.eyes.setEyelidState(left, smdl::EyelidState::Lowerer, 2, -0.8f);
    meshes.eyes.setEyelidState(left, smdl::EyelidState::Neutral, smdl::NoId, 0.0f);
    meshes.eyes.setEyelidState(left, smdl::EyelidState::Raiser, 3, 0.5f);
    meshes.eyes.setEyelidSplit(left, 0.1f);
    meshes.eyes.setEyelidEyeball(left, 1);

    // model-wide: localvar, mouth, controllers, rules
    model.addLocalVar("smile_amount");
    model.setMouth(0, "mouth_open", model.findBone("jaw"), {0.0f, 1.0f, 0.0f});
    for (const char *name: {"blink_ctl", "smile_ctl", "jaw_ctl"})
        model.addFlexController("default", name, 0.0f, 1.0f);
    model.addFlexController("default", "brow_ctl", -1.0f, 1.0f);
    model.addFlexRule("blink", "blink_ctl");
    model.addFlexRule("smile_amount", "smile_ctl * 0.5 + 0.25");
    model.addFlexRule("smileR", "smile_ctl");
    model.addFlexRule("smileL", "smile_ctl * 0.5 + %smile_amount * 0.5");
    model.addFlexRule("jaw_open", "jaw_ctl + 0.25 * %blink");

    AddFaceIdle(model);
    RequireSameAsQc(model, "flex.qc");
}

SMDL_TEST(flex_equiv_flex_more) {
    smdl::Model model("api/flex_more");
    BasicGlobals(model, "flesh");
    const Meshes meshes = AddFaceAndEyes(model);

    // multi-position flex (two consecutive keys of one name), split and decay options
    const smdl::FlexKeyId squeeze1 = meshes.face.addFlex("squeeze", 0);
    meshes.face.setFlexPosition(squeeze1, 0.5f);
    meshes.face.setFlexDecay(squeeze1, 0.5f);
    meshes.face.addFlex("squeeze", 1);
    const smdl::FlexKeyId wide = meshes.face.addFlexPair("wide", 0.2f, 2);
    meshes.face.setFlexDecay(wide, 0.5f);
    const smdl::FlexKeyId narrow = meshes.face.addFlex("narrow", 2);
    meshes.face.setFlexSplit(narrow, 0.3f);
    meshes.face.setFlexPosition(narrow, 0.75f);
    meshes.face.addSphereNormals("face_skin", {0.0f, 60.0f, 0.0f});
    meshes.face.setNoAutoDmxRules();

    AddRightLeftEyeballs(model, meshes.eyes);
    const smdl::EyelidId right = meshes.eyes.addEyelid("lower_right", smdl::EyelidKind::Lower);
    meshes.eyes.setEyelidState(right, smdl::EyelidState::Lowerer, 0, -0.6f);
    meshes.eyes.setEyelidState(right, smdl::EyelidState::Neutral, smdl::NoId, 0.0f);
    meshes.eyes.setEyelidState(right, smdl::EyelidState::Raiser, 1, 0.4f);
    meshes.eyes.setEyelidEyeball(right, 0);
    const smdl::EyelidId left = meshes.eyes.addEyelid("lower_left", smdl::EyelidKind::Lower);
    meshes.eyes.setEyelidState(left, smdl::EyelidState::Lowerer, 2, -0.6f);
    meshes.eyes.setEyelidState(left, smdl::EyelidState::Neutral, smdl::NoId, 0.0f);
    meshes.eyes.setEyelidState(left, smdl::EyelidState::Raiser, 3, 0.4f);

    const smdl::FlexControllerId squeezeCtl = model.addFlexController("default", "squeeze_ctl", 0.0f, 1.0f);
    const smdl::FlexControllerId wideCtl = model.addFlexController("default", "wide_ctl", 0.0f, 1.0f);
    model.addFlexController("eyes", "look_ctl", -1.0f, 1.0f);
    model.addFlexRule("squeeze", "max(squeeze_ctl, wide_ctl * 0.5)");
    model.addFlexRule("wideR", "min(wide_ctl, 0.8) + -look_ctl");
    model.addFlexRule("wideL", "(wide_ctl - 0.1) / 2");
    model.addFlexRule("narrow", "squeeze_ctl * (1 - wide_ctl)");
    model.addBoneFlexDriver(model.findBone("jaw"), smdl::BoneFlexComponent::TZ, squeezeCtl, 0.0f, 1.5f);
    model.addBoneFlexDriver(model.findBone("jaw"), smdl::BoneFlexComponent::TX, wideCtl, -1.0f, 1.0f);

    AddFaceIdle(model);
    RequireSameAsQc(model, "flex_more.qc");
}

// ---------------------------------------------------------------------------------------------------------------
// Shapes: call-time checks
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(flex_shape_validation) {
    smdl::Model model = FaceModel();
    smdl::BodyGroup group = model.addBodyGroup("face");
    smdl::Mesh mesh = AddMesh(model, group, "src/face.smd", "src/face.smd", VertexMode::Corners);
    smdl::Geometry geometry = mesh.geometry();
    const uint32_t vertexCount = geometry.vertexCount();
    const std::vector<smdl::Vec3> deltas{{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -2.0f}};

    // positions are required first
    smdl::Geometry empty = model.addBodyGroup("empty").createMesh("empty").geometry();
    Require(StatusOf([&] { empty.addShape("s", std::vector<uint32_t>{0}, std::vector<smdl::Vec3>{deltas[0]}); }) ==
                SMDL_ERROR_INVALID_ARGUMENT,
            "no positions");

    Require(StatusOf([&] { geometry.addShape("bad", std::vector<uint32_t>{0, vertexCount}, deltas); }) ==
                SMDL_ERROR_OUT_OF_RANGE,
            "vertex index past the end");
    Require(StatusOf([&] { geometry.addShape("dup", std::vector<uint32_t>{3, 3}, deltas); }) ==
                SMDL_ERROR_INVALID_ARGUMENT,
            "vertex listed twice");
    Require(StatusOf([&] { geometry.addShape("", std::vector<uint32_t>{0}, std::vector<smdl::Vec3>{deltas[0]}); }) ==
                SMDL_ERROR_INVALID_ARGUMENT,
            "empty name");
    const std::vector<smdl::Vec3> infinite{{0.0f, 0.0f, std::numeric_limits<float>::infinity()}};
    Require(StatusOf([&] { geometry.addShape("inf", std::vector<uint32_t>{0}, infinite); }) ==
                SMDL_ERROR_INVALID_ARGUMENT,
            "non-finite delta");
    Require(geometry.shapeCount() == 0, "failed calls add nothing");

    const smdl::ShapeId shape = geometry.addShape("down", std::vector<uint32_t>{0, 1}, deltas);
    Require(shape == 0 && geometry.shapeCount() == 1, "first shape");
    Require(StatusOf([&] { geometry.addShape("down", std::vector<uint32_t>{2}, std::vector<smdl::Vec3>{deltas[0]}); }) ==
                SMDL_ERROR_DUPLICATE_NAME,
            "duplicate shape name");
    Require(geometry.findShape("down") == 0, "findShape");
    Require(StatusOf([&] { geometry.findShape("up"); }) == SMDL_ERROR_NOT_FOUND, "unknown shape");

    Require(StatusOf([&] { geometry.setShapeNormalDeltas(shape, std::vector<smdl::Vec3>{deltas[0]}); }) ==
                SMDL_ERROR_INVALID_ARGUMENT,
            "normal delta count");
    Require(StatusOf([&] { geometry.setShapeNormalDeltas(7, deltas); }) == SMDL_ERROR_OUT_OF_RANGE, "unknown shape id");
    Require(StatusOf([&] { geometry.setShapeWrinkle(shape, std::vector<float>{0.5f}); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "wrinkle count");
    geometry.setShapeNormalDeltas(shape, deltas);
    geometry.setShapeWrinkle(shape, std::vector<float>{0.5f, 1.0f});

    // flexes refer to existing shapes
    Require(StatusOf([&] { mesh.addFlex("f", 1); }) == SMDL_ERROR_OUT_OF_RANGE, "flex of a missing shape");
    mesh.addFlex("f", 0);
    mesh.addFlex("g", smdl::NoId); // the base shape
}

// ---------------------------------------------------------------------------------------------------------------
// Flex objects: call-time checks
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(flex_call_validation) {
    smdl::Model model = FaceModel();
    const Meshes meshes = AddFaceAndEyes(model);
    const smdl::Mesh &face = meshes.face;
    const smdl::Mesh &eyes = meshes.eyes;
    const smdl::BoneId jaw = model.findBone("jaw");

    // controllers
    const smdl::FlexControllerId ctl = model.addFlexController("default", "ctl", 0.0f, 1.0f);
    Require(ctl == 0 && model.flexControllerCount() == 1 && model.findFlexController("CTL") == 0, "controller ids");
    Require(StatusOf([&] { model.addFlexController("default", "Ctl", 0.0f, 1.0f); }) == SMDL_ERROR_DUPLICATE_NAME,
            "duplicate controller (case-insensitive)");
    Require(StatusOf([&] { model.addFlexController("default", "", 0.0f, 1.0f); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "empty controller name");
    Require(StatusOf([&] { model.findFlexController("nope"); }) == SMDL_ERROR_NOT_FOUND, "unknown controller");
    Require(StatusOf([&] { model.addFlexController("default", std::string(200, 'x'), 0.0f, 1.0f); }) == SMDL_ERROR_LIMIT,
            "long controller name");
    for (int i = 1; i < 128; ++i)
        model.addFlexController("default", "c" + std::to_string(i), 0.0f, 1.0f);
    Require(StatusOf([&] { model.addFlexController("default", "one_more", 0.0f, 1.0f); }) == SMDL_ERROR_LIMIT,
            "128 controllers");

    // flexes
    face.addFlex("blink", 0);
    face.addFlexPair("smile", 0.4f, 1);
    Require(model.findFlex("blink") == 0 && model.findFlex("smileR") == 1 && model.findFlex("smileL") == 2,
            "flex ids in definition order");
    Require(StatusOf([&] { face.addFlexPair("pair", 0.0f, 1); }) == SMDL_ERROR_INVALID_ARGUMENT, "pair without split");
    Require(StatusOf([&] { face.setFlexPosition(9, 1.0f); }) == SMDL_ERROR_OUT_OF_RANGE, "unknown flex key");
    Require(StatusOf([&] { face.setFlexDecay(0, std::nanf("")); }) == SMDL_ERROR_INVALID_ARGUMENT, "NaN decay");
    face.setDefaultFlex(smdl::NoId);
    Require(StatusOf([&] { face.setDefaultFlex(0); }) == SMDL_ERROR_DUPLICATE_NAME, "one default flex");

    // local variables, mouths
    const smdl::FlexId local = model.addLocalVar("amount");
    Require(model.addLocalVar("AMOUNT") == local, "an existing name returns its id");
    Require(StatusOf([&] { model.setMouth(64, "mouth", jaw, {0.0f, 1.0f, 0.0f}); }) == SMDL_ERROR_LIMIT, "mouth index");
    Require(StatusOf([&] { model.setMouth(0, "mouth", 99, {0.0f, 1.0f, 0.0f}); }) == SMDL_ERROR_OUT_OF_RANGE,
            "mouth bone");
    model.setMouth(0, "mouth", jaw, {0.0f, 1.0f, 0.0f});
    model.setMouth(0, "mouth", jaw, {0.0f, 0.0f, 1.0f}); // replaces
    Require(model.findFlex("mouth") != smdl::NoId, "a mouth defines its flex");

    // rules
    Require(StatusOf([&] { model.addFlexRule("nothing", "ctl"); }) == SMDL_ERROR_NOT_FOUND, "rule for an unknown flex");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl +"); }) == SMDL_ERROR_SYNTAX, "operator without operand");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl ctl"); }) == SMDL_ERROR_SYNTAX, "operands without operator");
    Require(StatusOf([&] { model.addFlexRule("blink", "max(ctl)"); }) == SMDL_ERROR_SYNTAX, "max without comma");
    Require(StatusOf([&] { model.addFlexRule("blink", "(ctl"); }) == SMDL_ERROR_SYNTAX, "unmatched (");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl)"); }) == SMDL_ERROR_SYNTAX, "unmatched )");
    Require(StatusOf([&] { model.addFlexRule("blink", "1.2.3"); }) == SMDL_ERROR_SYNTAX, "bad number");
    Require(StatusOf([&] { model.addFlexRule("blink", ""); }) == SMDL_ERROR_SYNTAX, "empty expression");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl\nctl"); }) == SMDL_ERROR_SYNTAX, "newline");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl // x"); }) == SMDL_ERROR_SYNTAX, "comment");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl ; x"); }) == SMDL_ERROR_SYNTAX, "semicolon");
    Require(StatusOf([&] { model.addFlexRule("blink", "unknown_ctl"); }) == SMDL_ERROR_NOT_FOUND, "unknown controller");
    Require(StatusOf([&] { model.addFlexRule("blink", "%unknown_flex"); }) == SMDL_ERROR_NOT_FOUND, "unknown flex");
    const smdl::Error syntax = CaptureError([&] { model.addFlexRule("blink", "ctl * (1 +"); });
    Require(Contains(syntax.what(), "ctl * (1 +") && Contains(syntax.what(), "blink"),
            std::string("the message quotes the expression: ") + syntax.what());
    model.addFlexRule("blink", "max(ctl, -0.5 * %amount) + 2 * (ctl - 1) / 4");
    Require(StatusOf([&] { model.addFlexRule("blink", "ctl"); }) == SMDL_ERROR_DUPLICATE_NAME, "a flex has one rule");

    // bone flex drivers
    Require(StatusOf([&] { model.addBoneFlexDriver(jaw, smdl::BoneFlexComponent::TX, 500, 0.0f, 1.0f); }) ==
                SMDL_ERROR_OUT_OF_RANGE,
            "driver controller id");
    Require(StatusOf([&] { model.addBoneFlexDriver(99, smdl::BoneFlexComponent::TX, ctl, 0.0f, 1.0f); }) ==
                SMDL_ERROR_OUT_OF_RANGE,
            "driver bone");
    model.addBoneFlexDriver(jaw, smdl::BoneFlexComponent::TY, ctl, 0.0f, 1.0f);

    // eyeballs and eyelids
    const smdl::BoneId rEye = model.findBone("r_eye");
    Require(StatusOf([&] { eyes.addEyeball("e", 99, {0, 0, 0}, "eyeball_r", 2.0f); }) == SMDL_ERROR_OUT_OF_RANGE,
            "eyeball bone");
    Require(StatusOf([&] { eyes.addEyeball("e", rEye, {0, 0, 0}, "eyeball_r", 2.0f, 0.0f, 0.0f); }) ==
                SMDL_ERROR_INVALID_ARGUMENT,
            "pupil scale 0");
    for (int i = 0; i < 4; ++i)
        eyes.addEyeball("eye" + std::to_string(i), rEye, {0, 0, 0}, "eyeball_r", 2.0f);
    Require(StatusOf([&] { eyes.addEyeball("fifth", rEye, {0, 0, 0}, "eyeball_r", 2.0f); }) == SMDL_ERROR_LIMIT,
            "at most 4 eyeballs");
    const smdl::EyelidId lid = eyes.addEyelid("lid", smdl::EyelidKind::Upper);
    Require(StatusOf([&] { eyes.setEyelidState(lid, smdl::EyelidState::Lowerer, 99, 0.0f); }) == SMDL_ERROR_OUT_OF_RANGE,
            "eyelid shape");
    Require(StatusOf([&] { eyes.setEyelidState(5, smdl::EyelidState::Lowerer, 0, 0.0f); }) == SMDL_ERROR_OUT_OF_RANGE,
            "eyelid id");
    Require(StatusOf([&] { eyes.setEyelidEyeball(lid, 9); }) == SMDL_ERROR_OUT_OF_RANGE, "eyelid eyeball");
    Require(StatusOf([&] { eyes.addDmxEyelid(smdl::EyelidKind::Upper, 0, 9); }) == SMDL_ERROR_OUT_OF_RANGE,
            "dmx eyelid eyeball");
    Require(StatusOf([&] { eyes.addDmxEyelid(smdl::EyelidKind::Upper, 1, 1); }) == SMDL_ERROR_INVALID_ARGUMENT,
            "dmx eyelid needs two eyeballs");
    const uint32_t dmx = eyes.addDmxEyelid(smdl::EyelidKind::Upper, 0, 1);
    Require(StatusOf([&] { eyes.setDmxEyelidState(dmx, smdl::EyelidState::Lowerer, smdl::NoId, 0.0f); }) ==
                SMDL_ERROR_OUT_OF_RANGE,
            "dmx eyelid states are shapes");
    Require(model.findFlex("upper_right_raiser") != smdl::NoId && model.findFlex("upper_left") != smdl::NoId,
            "dmx eyelid flexes are known");
    eyes.addSphereNormals("eyeball_r", {0, 0, 0});
    Require(StatusOf([&] { eyes.addSphereNormals("", {0, 0, 0}); }) == SMDL_ERROR_INVALID_ARGUMENT, "empty material");
}

// ---------------------------------------------------------------------------------------------------------------
// Compile errors name the object
// ---------------------------------------------------------------------------------------------------------------

SMDL_TEST(flex_compile_error_names_eyeball) {
    smdl::Model model("api/flex");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    const Meshes meshes = AddFaceAndEyes(model);
    meshes.eyes.addEyeball("odd_eye", model.findBone("r_eye"), {2.5f, 4.0f, 1.5f}, "no_such_material", 2.4f);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(error.status() == SMDL_ERROR_COMPILE, "compile status");
    Require(Contains(error.object(), "eyeball 'odd_eye'") && Contains(error.object(), "src/eyes.smd"),
            "names the eyeball and its mesh: " + error.object());
    Require(Contains(error.what(), "no_such_material"), std::string("message: ") + error.what());
}

SMDL_TEST(flex_compile_error_eyelid_incomplete) {
    smdl::Model model("api/flex");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    const Meshes meshes = AddFaceAndEyes(model);
    AddRightLeftEyeballs(model, meshes.eyes);
    const smdl::EyelidId lid = meshes.eyes.addEyelid("upper_right", smdl::EyelidKind::Upper);
    meshes.eyes.setEyelidState(lid, smdl::EyelidState::Lowerer, 0, -0.8f);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(Contains(error.object(), "eyelid 'upper_right'"), "names the eyelid: " + error.object());
    Require(Contains(error.what(), "neutral"), std::string("names the missing state: ") + error.what());
}

SMDL_TEST(flex_compile_error_eyelid_out_of_range) {
    smdl::Model model("api/flex");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    const Meshes meshes = AddFaceAndEyes(model);
    AddRightLeftEyeballs(model, meshes.eyes);
    const smdl::EyelidId lid = meshes.eyes.addEyelid("upper_right", smdl::EyelidKind::Upper);
    meshes.eyes.setEyelidState(lid, smdl::EyelidState::Lowerer, 0, -5.0f); // the eyeball's radius is 1.2
    meshes.eyes.setEyelidState(lid, smdl::EyelidState::Neutral, smdl::NoId, 0.0f);
    meshes.eyes.setEyelidState(lid, smdl::EyelidState::Raiser, 1, 0.5f);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(Contains(error.object(), "eyelid 'upper_right'"), "names the eyelid: " + error.object());
    Require(Contains(error.what(), "out of range"), std::string("message: ") + error.what());
}

SMDL_TEST(flex_compile_error_shape_conflict) {
    // two vertices at one position with different deltas
    smdl::Model model("api/flex");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    const SmdFile smd = ReadSmd(FixturePath("src/face.smd"));
    smdl::BodyGroup group = model.addBodyGroup("face");
    smdl::Mesh mesh = AddMesh(model, group, "src/face.smd", "src/face.smd", VertexMode::Indexed);
    const GeometryData data = BuildGeometryData(smd, VertexMode::Indexed);
    uint32_t first = 0, second = 0;
    bool found = false;
    for (uint32_t i = 0; i < data.positions.size() && !found; ++i) {
        for (uint32_t j = i + 1; j < data.positions.size(); ++j) {
            if (data.positions[i] == data.positions[j]) {
                first = i;
                second = j;
                found = true;
                break;
            }
        }
    }
    Require(found, "the indexed face has vertices at one position");
    const smdl::ShapeId shape = mesh.geometry().addShape("split", std::vector<uint32_t>{first, second},
                                                         std::vector<smdl::Vec3>{{0, 0, 1}, {0, 0, -1}});
    mesh.addFlex("f", shape);
    const smdl::Error error = CaptureError([&] { model.compile(); });
    Require(Contains(error.object(), "flex 'f'"), "names the flex: " + error.object());
    Require(Contains(error.what(), "split") && Contains(error.what(), "different position deltas"),
            std::string("message: ") + error.what());
}

// ---------------------------------------------------------------------------------------------------------------
// Shapes without flexes, wrinkle values, DMX style eyelids
// ---------------------------------------------------------------------------------------------------------------

namespace {
    bool HasText(const Outputs &outputs, std::string_view text) {
        for (const auto &[name, bytes]: outputs) {
            if (!name.ends_with(".mdl"))
                continue;
            const std::string_view data(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            if (data.find(text) != std::string_view::npos)
                return true;
        }
        return false;
    }
}

// A shape nothing uses adds no vertex animation source: the output equals the model without shapes
SMDL_TEST(flex_unused_shapes_change_nothing) {
    const auto build = [](bool withShapes) {
        smdl::Model model("api/flex");
        BasicGlobals(model, "flesh");
        NoSequences(model);
        smdl::BodyGroup group = model.addBodyGroup("face");
        const SmdFile smd = ReadSmd(FixturePath("src/face.smd"));
        smdl::Mesh mesh = AddMesh(model, group, "src/face.smd", "src/face.smd", VertexMode::Corners);
        if (withShapes)
            AddVtaShapes(mesh.geometry(), smd, VertexMode::Corners, ReadVta(FixturePath("src/face.vta")));
        return ResultFiles(model.compile());
    };
    RequireSameOutputs(build(false), build(true));
}

// Wrinkle values make the flex a wrinkle flex (bigger vertex animation records in the MDL)
SMDL_TEST(flex_wrinkle_shape) {
    const SmdFile smd = ReadSmd(FixturePath("src/face.smd"));
    const VtaFile vta = ReadVta(FixturePath("src/face.vta"));
    const auto build = [&](bool withWrinkle) {
        smdl::Model model("api/flex");
        BasicGlobals(model, "flesh");
        NoSequences(model);
        smdl::BodyGroup group = model.addBodyGroup("face");
        smdl::Mesh mesh = AddMesh(model, group, "src/face.smd", "src/face.smd", VertexMode::Corners);
        const GeometryData data = BuildGeometryData(smd, VertexMode::Corners);
        const VtaVertexMap map = MapVtaToGeometry(vta, data.positions, data.normals);
        std::vector<uint32_t> vertices;
        std::vector<smdl::Vec3> deltas;
        std::vector<float> wrinkle;
        for (const VtaDelta &delta: VtaFrameDeltas(vta, 1)) {
            for (const uint32_t vertex: map.geometryVertices[static_cast<size_t>(delta.vertex)]) {
                vertices.push_back(vertex);
                deltas.push_back(delta.positionDelta);
                wrinkle.push_back(0.5f);
            }
        }
        const smdl::ShapeId shape = mesh.geometry().addShape("blink", vertices, deltas);
        if (withWrinkle)
            mesh.geometry().setShapeWrinkle(shape, wrinkle);
        mesh.addFlex("blink", shape);
        return ResultFiles(model.compile());
    };
    const Outputs plain = build(false);
    const Outputs wrinkled = build(true);
    Require(wrinkled.at("models/api/flex.mdl").size() > plain.at("models/api/flex.mdl").size(),
            "wrinkle records are bigger");
}

// DMX style eyelid: the three states are delta states of the mesh's source. Smoke test (no DMX fixture with delta
// states exists to compare with): the flexes, controllers' targets and eyeball links come out and the compile adds
// no warnings about unmatched vertices.
SMDL_TEST(flex_dmx_eyelid) {
    smdl::Model model("api/flex");
    BasicGlobals(model, "flesh");
    NoSequences(model);
    const Meshes meshes = AddFaceAndEyes(model);
    AddRightLeftEyeballs(model, meshes.eyes);
    // frames 1..4 of eyes.vta: right lowerer / raiser, left lowerer / raiser; the neutral state is the right raiser
    // here only to have a third shape
    const uint32_t lid = meshes.eyes.addDmxEyelid(smdl::EyelidKind::Upper, 0, 1);
    meshes.eyes.setDmxEyelidState(lid, smdl::EyelidState::Lowerer, 0, -0.8f);
    meshes.eyes.setDmxEyelidState(lid, smdl::EyelidState::Neutral, 3, 0.0f);
    meshes.eyes.setDmxEyelidState(lid, smdl::EyelidState::Raiser, 1, 0.5f);
    const smdl::Result result = model.compile();
    Require(result.ok(), "compile ok");
    const Outputs files = ResultFiles(result);
    for (const char *name: {"upper_right", "upper_right_lowerer", "upper_right_neutral", "upper_right_raiser",
                            "upper_left", "upper_left_lowerer", "upper_left_neutral", "upper_left_raiser"})
        Require(HasText(files, name), std::string("flex ") + name + " in the MDL");
    for (const smdl::Message &message: result.messages())
        Require(!Contains(message.text, "unmatched vertex anims"), std::string(message.text));

    // a state that is not set is a compile error naming the DMX eyelid
    smdl::Model broken("api/flex");
    BasicGlobals(broken, "flesh");
    NoSequences(broken);
    const Meshes brokenMeshes = AddFaceAndEyes(broken);
    AddRightLeftEyeballs(broken, brokenMeshes.eyes);
    const uint32_t incomplete = brokenMeshes.eyes.addDmxEyelid(smdl::EyelidKind::Lower, 0, 1);
    brokenMeshes.eyes.setDmxEyelidState(incomplete, smdl::EyelidState::Lowerer, 0, -0.8f);
    const smdl::Error error = CaptureError([&] { broken.compile(); });
    Require(Contains(error.object(), "dmx eyelid 'lower'"), "names the DMX eyelid: " + error.object());
}

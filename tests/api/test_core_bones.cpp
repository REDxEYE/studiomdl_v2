// Core part tests: bone options and bone processing options. Each builder model is compared byte for byte with the QC
// compile of the fixture that lists the same commands in the order the builder applies them (api/src/apply_skeleton.cpp).
// All of them use the 12 bone biped (src/biped.smd, every bone weighted) without sequences of its own.
#include "core_helpers.hpp"

#include <numbers>

using namespace coretest;

namespace {
    constexpr const char *kBiped = "src/biped.smd";
    constexpr const char *kExtraBones = "src/core_bones_extra.smd";

    // QC angles are degrees: these radians convert back to the same degrees exactly (RAD2DEG(Rad(d)) == d for the
    // angles the fixtures use: 15, 30, 90), so the builder's euler form reproduces a QC pose bit for bit.
    constexpr float Rad(float degrees) { return degrees * (std::numbers::pi_v<float> / 180.0f); }

    struct Biped {
        smdl::Model model;
        smdl::BoneId root, pelvis, spine, head, lArm, rArm, lShin, rShin, lFoot, rFoot;
    };

    // The model of the fixtures' $body "<smd>" "<smd>" (the biped, or the extra bones model)
    Biped BuildBiped(const char *modelName, const char *smd = kBiped) {
        Biped biped{smdl::Model(modelName), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        smdl::Model &model = biped.model;
        BasicGlobals(model, "flesh");
        NoSequences(model);
        AddBody(model, "biped", smd, smd, VertexMode::Indexed);
        if (std::string_view(smd) == kBiped) {
            biped.root = model.findBone("root");
            biped.pelvis = model.findBone("pelvis");
            biped.spine = model.findBone("spine");
            biped.head = model.findBone("head");
            biped.lArm = model.findBone("l_arm");
            biped.rArm = model.findBone("r_arm");
            biped.lShin = model.findBone("l_shin");
            biped.rShin = model.findBone("r_shin");
            biped.lFoot = model.findBone("l_foot");
            biped.rFoot = model.findBone("r_foot");
        }
        return biped;
    }

    // The model equals the QC compile, and the options matter: the same model without them (and with the same
    // qc_path) compiles to something else
    void RequireOptionsSameAsQc(Biped &biped, const char *qc, const char *smd = kBiped) {
        Biped plain = BuildBiped(std::string(biped.model.name()).c_str(), smd);
        plain.model.setQcPath(QcPathEntry(FixturePath(qc)));
        const Outputs withoutOptions = ResultFiles(plain.model.compile());
        RequireSameAsQc(biped.model, qc);
        Require(!DescribeOutputDifference(withoutOptions, ResultFiles(biped.model.compile())).empty(),
                std::string("the options of ") + qc + " change the output");
    }
}

// $realignbones, $lockbonelengths, $root, four $definebone (default lock, explicit locked with a realign pose,
// explicit unlocked), $forcerealign
SMDL_TEST(core_bones_define) {
    Biped b = BuildBiped("api/core_bones_define");
    smdl::Model &model = b.model;
    model.setRealignBones(true);
    model.setLockBoneLengths(true);
    model.setRootBone(b.root);
    model.defineBoneEuler(b.root, smdl::NoBone, {0, 0, 0}, {0, 0, Rad(90)});
    model.defineBoneEuler(b.pelvis, b.root, {0, 0, 34}, {0, 0, 0});
    model.defineBoneEuler(b.spine, b.pelvis, {0, 0, 10}, {Rad(15), 0, 0});
    model.defineBoneEuler(b.head, b.spine, {0, 0, 18}, {0, Rad(-30), 0});
    model.setDefineBoneLock(b.head, SMDL_DEFINE_BONE_LOCKED);
    model.setDefineBoneRealignEuler(b.head, {0, 0, 18}, {0, Rad(-15), 0});
    model.defineBoneEuler(b.lArm, b.spine, {0, 8, 14}, {Rad(-90), 0, 0});
    model.setDefineBoneLock(b.lArm, SMDL_DEFINE_BONE_UNLOCKED);
    model.addForceRealignEuler(b.head, {Rad(15), 0, 0});
    RequireOptionsSameAsQc(b, "core_bones_define.qc");
}

// The quaternion forms give the bytes of the euler forms (identity and a half turn about Z are exact in both)
SMDL_TEST(core_bones_define_quaternion_matches_euler) {
    auto build = [&](bool euler) {
        Biped b = BuildBiped("api/core_bones_quaternion");
        smdl::Model &model = b.model;
        if (euler) {
            model.defineBoneEuler(b.root, smdl::NoBone, {0, 0, 0}, {0, 0, 0});
            model.defineBoneEuler(b.pelvis, b.root, {0, 0, 34}, {0, 0, std::numbers::pi_v<float>});
            model.addForceRealignEuler(b.pelvis, {0, 0, 0});
        }
        else {
            model.defineBone(b.root, smdl::NoBone, {0, 0, 0}, {0, 0, 0, 1});
            model.defineBone(b.pelvis, b.root, {0, 0, 34}, {0, 0, 1, 0});
            model.addForceRealign(b.pelvis, {0, 0, 0, 1});
        }
        return std::move(b.model);
    };
    smdl::Model euler = build(true);
    smdl::Model quaternion = build(false);
    RequireSameOutputs(ResultFiles(euler.compile()), ResultFiles(quaternion.compile()));
}

// $collapsebones and $alwayscollapse: for this model they change nothing (see the fixture), the output is the QC's
SMDL_TEST(core_bones_collapse) {
    Biped b = BuildBiped("api/core_bones_collapse", kExtraBones);
    smdl::Model &model = b.model;
    model.setCollapseBones(true);
    model.setAlwaysCollapse(model.findBone("side"), true);
    RequireSameAsQc(model, "core_bones_collapse.qc");
}

// $collapsebonesaggressive removes the root that has no vertex of its own
SMDL_TEST(core_bones_collapse_aggressive) {
    Biped b = BuildBiped("api/core_bones_collapse_aggressive", kExtraBones);
    b.model.setCollapseBonesAggressive(true);
    RequireOptionsSameAsQc(b, "core_bones_collapse_aggressive.qc", kExtraBones);
}

// $unlockdefinebones + $collapsebonesaggressive with defined bones (they are never collapsed)
SMDL_TEST(core_bones_aggressive) {
    Biped b = BuildBiped("api/core_bones_aggressive");
    smdl::Model &model = b.model;
    model.setUnlockDefineBones(true);
    model.setCollapseBonesAggressive(true);
    model.defineBoneEuler(b.root, smdl::NoBone, {0, 0, 0}, {0, 0, Rad(90)});
    model.setDefineBoneLock(b.root, SMDL_DEFINE_BONE_LOCKED);
    model.defineBoneEuler(b.pelvis, b.root, {0, 0, 34}, {0, 0, 0});
    model.setDefineBoneLock(b.pelvis, SMDL_DEFINE_BONE_LOCKED);
    model.defineBoneEuler(b.spine, b.pelvis, {0, 0, 10}, {Rad(30), 0, 0});
    model.setDefineBoneLock(b.spine, SMDL_DEFINE_BONE_LOCKED);
    model.defineBoneEuler(b.lArm, b.spine, {0, 8, 14}, {Rad(-90), 0, 0});
    RequireOptionsSameAsQc(b, "core_bones_aggressive.qc");
}

// $controller (user inputs and the mouth, translation and a looping rotation), $screenalign, $worldalign, $bonemerge,
// $bonealwayssetup, $limitrotation, $bonesaveframe, $jointsurfaceprop and $jointcontents
SMDL_TEST(core_bones_flags) {
    Biped b = BuildBiped("api/core_bones_flags");
    smdl::Model &model = b.model;
    model.setBoneSurfaceProp(b.head, "metal");
    model.setBoneSurfaceProp(b.lArm, "glass");
    model.setBoneContents(b.head, smdl::ContentsGrate);
    model.setBoneContents(b.rArm, smdl::ContentsMonster);

    Require(model.addBoneController(0, b.head, SMDL_BONE_CONTROLLER_YR, -30.0f, 30.0f) == 0, "first controller index");
    Require(model.addBoneController(1, b.head, SMDL_BONE_CONTROLLER_XR, -20.0f, 20.0f) == 1, "second controller index");
    model.addBoneController(SMDL_BONE_CONTROLLER_MOUTH, b.lArm, SMDL_BONE_CONTROLLER_Z, 0.0f, 4.0f);
    model.addBoneController(2, b.rArm, SMDL_BONE_CONTROLLER_ZR, 0.0f, 360.0f);
    model.setScreenAlign(b.head, SMDL_SCREEN_ALIGN_SPHERE);
    model.setScreenAlign(b.lArm, SMDL_SCREEN_ALIGN_CYLINDER);
    model.setWorldAlign(b.lFoot, true);
    model.setBoneMerge(b.spine, true);
    model.setBoneMerge(b.head, true);
    model.setBoneAlwaysSetup(b.rFoot, true);
    model.setLimitRotation(b.lShin);
    const std::vector<std::string> sequences = {"idle", "walk"};
    model.setLimitRotation(b.rShin, sequences);
    model.setBoneSaveFrame(b.root, SMDL_BONE_SAVE_POSITION | SMDL_BONE_SAVE_ROTATION);
    model.setBoneSaveFrame(b.pelvis, SMDL_BONE_SAVE_ROTATION64);
    model.setBoneSaveFrame(b.spine, 0);
    RequireOptionsSameAsQc(b, "core_bones_flags.qc");
}

// A setter turned off again leaves the bone as if it was never set (the QC has no command for it)
SMDL_TEST(core_bones_option_off_is_default) {
    Biped b = BuildBiped("api/core_bones_off");
    smdl::Model &model = b.model;
    model.setWorldAlign(b.lFoot, true);
    model.setWorldAlign(b.lFoot, false);
    model.setBoneMerge(b.head, true);
    model.setBoneMerge(b.head, false);
    model.setBoneAlwaysSetup(b.rFoot, true);
    model.setBoneAlwaysSetup(b.rFoot, false);
    model.setScreenAlign(b.head, SMDL_SCREEN_ALIGN_SPHERE);
    model.setScreenAlign(b.head, SMDL_SCREEN_ALIGN_NONE);
    model.setAlwaysCollapse(b.lFoot, true);
    model.setAlwaysCollapse(b.lFoot, false);
    model.setCollapseBones(true);
    model.setCollapseBones(false);
    smdl::Model plain = BuildBiped("api/core_bones_off").model;
    RequireSameOutputs(ResultFiles(plain.compile()), ResultFiles(model.compile()));
}

// Bone ids that are not bones of the model are rejected when the option is set
SMDL_TEST(core_bones_option_checks) {
    Biped b = BuildBiped("api/core_bones_checks");
    smdl::Model &model = b.model;
    const smdl::BoneId missing = model.boneCount() + 2;

    smdl::Error error = CaptureError([&] { model.setWorldAlign(missing, true); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE && Contains(error.what(), "bone"), "world align bone range");

    error = CaptureError([&] { model.addBoneController(5, b.head, SMDL_BONE_CONTROLLER_X, 0, 1); });
    Require(error.status() == SMDL_ERROR_INVALID_ARGUMENT, "controller input range");

    error = CaptureError([&] { model.setDefineBoneLock(b.head, SMDL_DEFINE_BONE_LOCKED); });
    Require(error.status() == SMDL_ERROR_NOT_FOUND, "lock of a bone that was not defined");

    model.defineBoneEuler(b.root, smdl::NoBone, {0, 0, 0}, {0, 0, 0});
    error = CaptureError([&] { model.defineBoneEuler(b.root, smdl::NoBone, {0, 0, 0}, {0, 0, 0}); });
    Require(error.status() == SMDL_ERROR_DUPLICATE_NAME, "a bone is defined once");

    error = CaptureError([&] { model.setBoneSaveFrame(missing, SMDL_BONE_SAVE_POSITION); });
    Require(error.status() == SMDL_ERROR_OUT_OF_RANGE, "save frame bone range");
}

// smdl::Animation, smdl::IkRule and the animation part of smdl::Model (cpp/animation.hpp): animations, their options,
// commands and IK rules. The C API (smdl_animation.h) wraps the C++ methods.
#include "studiomdl/c/smdl_animation.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>

#include "anim_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    constexpr size_t kMaxAnimationFrames = MAXSTUDIOANIMFRAMES;
    constexpr size_t kMaxAnimations = MAXSTUDIOANIMS;
    constexpr size_t kMaxCommands = MAXSTUDIOCMDS;
    constexpr size_t kMaxIkRules = MAXSTUDIOIKRULES;

    using Command = AnimationCommandDesc;

    bool AnimationNameInUse(const smdl_model &model, std::string_view name) {
        return std::ranges::any_of(model.animations.list, [&](const auto &a) { return IEquals(a->name, name); });
    }

    bool SequenceNameInUse(const smdl_model &model, std::string_view name) {
        return std::ranges::any_of(model.sequences.list, [&](const auto &s) { return IEquals(s->name, name); });
    }

    // A frame of `animation` (0 .. frame_count - 1)
    smdl_status CheckFrame(const smdl_animation &animation, int frame, std::string_view what) {
        if (frame < 0 || static_cast<int64_t>(frame) >= static_cast<int64_t>(animation.frameCount))
            return Fail(animation.model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': {} {} is out of range ({} frames)",
                        animation.name, what, frame, animation.frameCount);
        return SMDL_OK;
    }

    // A reference animation: not null, of the same model
    smdl_status CheckReference(const smdl_animation &animation, const smdl_animation *reference, std::string_view what) {
        if (!reference)
            return Fail(animation.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': {} reference animation is null",
                        animation.name, what);
        if (reference->model != animation.model)
            return Fail(animation.model, SMDL_ERROR_INVALID_ARGUMENT,
                        "animation '{}': {} reference animation '{}' belongs to another model", animation.name, what,
                        reference->name);
        return SMDL_OK;
    }

    smdl_status CheckMotion(const smdl_animation &animation, uint32_t motion, bool required, std::string_view what) {
        if ((motion & ~kMotionMask) != 0)
            return Fail(animation.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': {}: unknown SMDL_MOTION_* bits",
                        animation.name, what);
        if (required && motion == 0)
            return Fail(animation.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': {}: no motion flags given",
                        animation.name, what);
        return SMDL_OK;
    }

    smdl_status CheckBoneOf(const smdl_animation &animation, smdl_bone_id bone) {
        return CheckBone(animation.model, bone, "animation", animation.name);
    }

    // Checks the command count and appends (the command is built by the caller)
    smdl_status AddCommand(smdl_animation &animation, Command command) {
        if (animation.commands.size() >= kMaxCommands)
            return Fail(animation.model, SMDL_ERROR_LIMIT, "animation '{}' has at most {} commands", animation.name,
                        kMaxCommands);
        animation.commands.push_back(std::move(command));
        return Ok(animation.model);
    }

    smdl_status SetBoneFrames(smdl_animation &a, smdl_bone_id bone, const float *positions, size_t positionCount,
                              size_t positionStride, const float *rotations, size_t rotationCount,
                              size_t rotationStride, bool quaternions) {
        smdl_model *model = a.model;
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        if (positionCount != 0 && positionCount != a.frameCount)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "animation '{}': {} positions given for bone '{}', expected 0 or {} (one per frame)", a.name,
                        positionCount, BoneName(*model, bone), a.frameCount);
        if (rotationCount != 0 && rotationCount != a.frameCount)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "animation '{}': {} rotations given for bone '{}', expected 0 or {} (one per frame)", a.name,
                        rotationCount, BoneName(*model, bone), a.frameCount);

        smdl_status status = SMDL_OK;
        std::vector<float> positionValues;
        std::vector<float> rotationValues;
        const std::string what = std::format("animation '{}' bone '{}' positions", a.name, BoneName(*model, bone));
        if (!ReadFloats(positions, positionCount, positionStride, 3, positionValues, model, what, status))
            return status;
        const std::string rotationWhat =
                std::format("animation '{}' bone '{}' rotations", a.name, BoneName(*model, bone));
        if (!ReadFloats(rotations, rotationCount, rotationStride, quaternions ? 4 : 3, rotationValues, model,
                        rotationWhat, status))
            return status;

        AnimationBoneFrames frames;
        frames.positions.reserve(positionCount);
        for (size_t i = 0; i < positionCount; ++i)
            frames.positions.emplace_back(positionValues[i * 3], positionValues[i * 3 + 1], positionValues[i * 3 + 2]);
        frames.rotations.reserve(rotationCount);
        for (size_t i = 0; i < rotationCount; ++i) {
            RadianEuler euler;
            if (quaternions) {
                const Vector position = positionCount ? frames.positions[i] : Vector(0, 0, 0);
                if (!QuaternionToRadianEuler(&rotationValues[i * 4], position, euler))
                    return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                                "animation '{}' bone '{}': rotation {} is a zero-length quaternion", a.name,
                                BoneName(*model, bone), i);
            }
            else {
                euler = RadianEuler(rotationValues[i * 3], rotationValues[i * 3 + 1], rotationValues[i * 3 + 2]);
            }
            frames.rotations.push_back(euler);
        }

        if (frames.positions.empty() && frames.rotations.empty())
            a.bones.erase(bone);
        else
            a.bones[bone] = std::move(frames);
        return Ok(model);
    }

    smdl_status SetRestPose(smdl_animation &a, smdl_bone_id bone, const float position[3], const float *rotation,
                            bool quaternion) {
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        if (!position || !rotation || !AllFinite(position, 3) || !AllFinite(rotation, quaternion ? 4 : 3))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT,
                        "animation '{}': rest pose of bone '{}' is null or not finite", a.name,
                        BoneName(*a.model, bone));
        RestPoseOverride pose{bone, ToVector(position), RadianEuler(0, 0, 0)};
        if (quaternion) {
            if (!QuaternionToRadianEuler(rotation, pose.position, pose.rotation))
                return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT,
                            "animation '{}': rest rotation of bone '{}' is a zero-length quaternion", a.name,
                            BoneName(*a.model, bone));
        }
        else {
            pose.rotation = RadianEuler(rotation[0], rotation[1], rotation[2]);
        }
        for (RestPoseOverride &existing: a.restOverrides) {
            if (existing.bone == bone) {
                existing = pose;
                return Ok(a.model);
            }
        }
        a.restOverrides.push_back(pose);
        return Ok(a.model);
    }

    // Option setters that take one float
    template<class F>
    smdl_status SetFloatOption(smdl_animation &a, float value, std::string_view what, F &&apply) {
        if (!std::isfinite(value))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': {} is not finite", a.name, what);
        apply(a, value);
        return Ok(a.model);
    }

    template<class F>
    smdl_status SetOption(smdl_animation &a, F &&apply) {
        apply(a);
        return Ok(a.model);
    }

    smdl_status AddIkRule(smdl_animation &a, smdl_ik_chain_id chain, smdl_ik_rule_type type, bool fixup,
                          smdl_ik_rule **out_rule) {
        if (out_rule)
            *out_rule = nullptr;
        smdl_model *model = a.model;
        if (chain >= model->rig.ikChains.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': IK chain {} is out of range ({} chains)",
                        a.name, chain, model->rig.ikChains.size());
        if (type < SMDL_IK_RULE_AUTOSTEPS || type > SMDL_IK_RULE_UNLATCH)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': IK rule type {} is not valid", a.name,
                        static_cast<int>(type));
        if (a.ikRules.size() >= kMaxIkRules)
            return Fail(model, SMDL_ERROR_LIMIT, "animation '{}' has at most {} IK rules", a.name, kMaxIkRules);
        if (a.commands.size() >= kMaxCommands)
            return Fail(model, SMDL_ERROR_LIMIT, "animation '{}' has at most {} commands", a.name, kMaxCommands);

        auto rule = std::make_unique<smdl_ik_rule>();
        rule->model = model;
        rule->animation = &a;
        rule->chain = chain;
        rule->type = type;
        rule->isFixup = fixup;
        Command command;
        command.type = Command::Type::IkRule;
        command.rule = rule.get();
        command.flag = fixup;
        a.commands.push_back(std::move(command));
        if (out_rule)
            *out_rule = rule.get();
        a.ikRules.push_back(std::move(rule));
        return Ok(model);
    }

    smdl_status AlignCommand(smdl_animation &a, smdl_bone_id bone, const smdl_animation *reference, uint32_t motion,
                             int srcFrame, int destFrame, bool toVariant, bool boneVariant) {
        if (smdl_status status = CheckReference(a, reference, "align"); status != SMDL_OK)
            return status;
        if (boneVariant) {
            if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
                return status;
        }
        if (toVariant) {
            motion = SMDL_MOTION_X | SMDL_MOTION_Y;
            srcFrame = destFrame = 0;
        }
        else {
            if (smdl_status status = CheckMotion(a, motion, true, "align"); status != SMDL_OK)
                return status;
            if (smdl_status status = CheckFrame(*reference, srcFrame, "source frame"); status != SMDL_OK)
                return status;
            if (smdl_status status = CheckFrame(a, destFrame, "destination frame"); status != SMDL_OK)
                return status;
        }
        Command command;
        command.type = Command::Type::Align;
        command.reference = reference;
        command.motion = motion;
        command.a = srcFrame;
        command.b = destFrame;
        command.bone = boneVariant ? bone : SMDL_NO_BONE;
        return AddCommand(a, std::move(command));
    }

    smdl_status AddBoneDriver(smdl_animation &a, smdl_bone_id bone, int axis, float value, int use_range, int start,
                              int peak, int tail, int end) {
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        if (axis < 0 || axis > 2)
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': bonedriver axis {} must be 0..2", a.name,
                        axis);
        if (!std::isfinite(value))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': bonedriver value is not finite", a.name);
        Command command;
        command.type = Command::Type::BoneDriver;
        command.bone = bone;
        command.a = axis;
        command.value = value;
        command.flag = use_range != 0;
        command.b = start;
        command.c = peak;
        command.d = tail;
        command.e = end;
        return AddCommand(a, std::move(command));
    }

    template<class F>
    smdl_status SetRuleFloat(smdl_ik_rule &r, float value, std::string_view what, F &&apply) {
        if (!std::isfinite(value))
            return Fail(r.model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: {} is not finite", what);
        apply(r, value);
        return Ok(r.model);
    }

    // Commands that need only a type
    smdl_status SimpleCommand(smdl_animation &a, Command::Type type) {
        Command command;
        command.type = type;
        return AddCommand(a, std::move(command));
    }
}

namespace smdl {

std::string_view Animation::name() const { return handle()->name; }

float Animation::fps() const { return handle()->fps; }

uint32_t Animation::frameCount() const { return handle()->frameCount; }

// ---- frames ----

Animation &Animation::setBoneFrames(BoneId bone, const float *positions, size_t positionCount,
                                    size_t positionStrideBytes, const float *rotations, size_t rotationCount,
                                    size_t rotationStrideBytes) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetBoneFrames(*animation, bone, positions, positionCount, positionStrideBytes, rotations, rotationCount,
                             rotationStrideBytes, true);
    });
    return *this;
}

Animation &Animation::setBoneFramesEuler(BoneId bone, const float *positions, size_t positionCount,
                                         size_t positionStrideBytes, const float *eulers, size_t eulerCount,
                                         size_t eulerStrideBytes) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetBoneFrames(*animation, bone, positions, positionCount, positionStrideBytes, eulers, eulerCount,
                             eulerStrideBytes, false);
    });
    return *this;
}

Animation &Animation::setRestPose(BoneId bone, const Vec3 &positionValue, const Quat &rotationValue) {
    smdl_animation *animation = handle();
    const float *position = positionValue.data();
    const float *rotation = rotationValue.data();
    RunBody(animation->model, [&] { return SetRestPose(*animation, bone, position, rotation, true); });
    return *this;
}

Animation &Animation::setRestPoseEuler(BoneId bone, const Vec3 &positionValue, const Vec3 &eulerValue) {
    smdl_animation *animation = handle();
    const float *position = positionValue.data();
    const float *euler = eulerValue.data();
    RunBody(animation->model, [&] { return SetRestPose(*animation, bone, position, euler, false); });
    return *this;
}

// ---- options ----

Animation &Animation::setFps(float fps) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (!std::isfinite(fps) || fps <= 0.0f)
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': fps {} must be above 0", a.name, fps);
        a.fps = fps;
        a.options.fps = fps;
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setOrigin(const Vec3 &originValue) {
    smdl_animation &a = *handle();
    const float *origin = originValue.data();
    RunBody(a.model, [&] {
        if (!origin || !AllFinite(origin, 3))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': origin is null or not finite", a.name);
        a.options.origin = ToVector(origin);
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setRotate(float degrees) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetFloatOption(*animation, degrees, "rotate angle", [](smdl_animation &a, float value) {
            AnimationOptionsDesc::RotationStep step;
            step.z = value;
            a.options.rotation.push_back(step);
        });
    });
    return *this;
}

Animation &Animation::setAngles(float x, float y, float z) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': angles are not finite", a.name);
        AnimationOptionsDesc::RotationStep step;
        step.angles = true;
        step.x = x;
        step.y = y;
        step.z = z;
        a.options.rotation.push_back(step);
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setScale(float scale) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetFloatOption(*animation, scale, "scale", [](smdl_animation &a, float value) { a.options.scale = value; });
    });
    return *this;
}

Animation &Animation::setLoop(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.loop = enabled != 0; }); });
    return *this;
}

Animation &Animation::setNoForceLoop(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.noForceLoop = enabled != 0; }); });
    return *this;
}

Animation &Animation::setStartLoop(int frame) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckFrame(a, frame, "loop start frame"); status != SMDL_OK)
            return status;
        a.options.startLoop = frame;
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setPercentStartLoop(float percent) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetFloatOption(*animation, percent, "loop start percent",
                              [](smdl_animation &a, float value) { a.options.percentStartLoop = value; });
    });
    return *this;
}

Animation &Animation::setFudgeLoop(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.fudgeLoop = enabled != 0; }); });
    return *this;
}

Animation &Animation::setSnap(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.snap = enabled != 0; }); });
    return *this;
}

Animation &Animation::setPost(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.post = enabled != 0; }); });
    return *this;
}

Animation &Animation::setAutoIk(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.autoIk = enabled != 0; }); });
    return *this;
}

Animation &Animation::setMotionRollback(float rollback) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetFloatOption(*animation, rollback, "motion rollback",
                              [](smdl_animation &a, float value) { a.options.motionRollback = value; });
    });
    return *this;
}

Animation &Animation::setNoAnimBlock(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model,
            [&] { return SetOption(*animation, [&](smdl_animation &a) { a.options.noAnimBlock = enabled != 0; }); });
    return *this;
}

Animation &Animation::setNoAnimBlockStall(bool enabled) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetOption(*animation, [&](smdl_animation &a) { a.options.noAnimBlockStall = enabled != 0; });
    });
    return *this;
}

Animation &Animation::setNoStallFrames(float frames) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] {
        return SetFloatOption(*animation, frames, "stall frames",
                              [](smdl_animation &a, float value) { a.options.noStallFrames = value; });
    });
    return *this;
}

Animation &Animation::setFrameRange(int firstFrame, int lastFrame) {
    smdl_animation &a = *handle();
    const int first_frame = firstFrame;
    const int last_frame = lastFrame;
    RunBody(a.model, [&] {
        if (first_frame < 0 || last_frame < 0)
            return Fail(a.model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': frame range {}..{} is negative", a.name,
                        first_frame, last_frame);
        a.options.frameRange = AnimationOptionsDesc::FrameRange{first_frame, last_frame, false};
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setFrameStart(int firstFrame) {
    smdl_animation &a = *handle();
    const int first_frame = firstFrame;
    RunBody(a.model, [&] {
        if (first_frame < 0)
            return Fail(a.model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': first frame {} is negative", a.name,
                        first_frame);
        a.options.frameRange = AnimationOptionsDesc::FrameRange{first_frame, 0, true};
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setBlockName(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (std::string bad = BadObjectName(name, "block", MAXSTUDIONAME); !bad.empty())
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': {}", a.name, bad);
        a.options.blockName = name;
        return Ok(a.model);
    });
    return *this;
}

Animation &Animation::setMotionExtraction(uint32_t motion) {
    smdl_animation &a = *handle();
    const uint32_t motion_flags = motion;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckMotion(a, motion_flags, false, "motion extraction"); status != SMDL_OK)
            return status;
        a.options.motion = motion_flags;
        return Ok(a.model);
    });
    return *this;
}

// ---- commands ----

Animation &Animation::addFixupLoop(int firstFrame, int lastFrame) {
    smdl_animation &a = *handle();
    const int first_frame = firstFrame;
    const int last_frame = lastFrame;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckFrame(a, first_frame, "first frame"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(a, last_frame, "last frame"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::FixupLoop;
        command.a = first_frame;
        command.b = last_frame;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addWeightList(WeightListId weightList) {
    smdl_animation &a = *handle();
    const smdl_weight_list_id weight_list = weightList;
    RunBody(a.model, [&] {
        if (weight_list >= a.model->rig.weightLists.size())
            return Fail(a.model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': weight list {} is out of range ({} lists)",
                        a.name, weight_list, a.model->rig.weightLists.size());
        Command command;
        command.type = Command::Type::Weights;
        command.weightList = weight_list;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addSubtract(const Animation &referenceView, int frame, bool post) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "subtract"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(*reference, frame, "subtract frame"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::Subtract;
        command.reference = reference;
        command.a = frame;
        command.flag = post != 0;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addAlign(const Animation &referenceView, uint32_t motion, int srcFrame, int destFrame) {
    smdl_animation *animation = handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(animation->model,
            [&] { return AlignCommand(*animation, SMDL_NO_BONE, reference, motion, srcFrame, destFrame, false, false); });
    return *this;
}

Animation &Animation::addAlignTo(const Animation &referenceView) {
    smdl_animation *animation = handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(animation->model, [&] { return AlignCommand(*animation, SMDL_NO_BONE, reference, 0, 0, 0, true, false); });
    return *this;
}

Animation &Animation::addAlignBone(BoneId bone, const Animation &referenceView, uint32_t motion, int srcFrame,
                                   int destFrame) {
    smdl_animation *animation = handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(animation->model,
            [&] { return AlignCommand(*animation, bone, reference, motion, srcFrame, destFrame, false, true); });
    return *this;
}

Animation &Animation::addAlignBoneTo(BoneId bone, const Animation &referenceView) {
    smdl_animation *animation = handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(animation->model, [&] { return AlignCommand(*animation, bone, reference, 0, 0, 0, true, true); });
    return *this;
}

Animation &Animation::addMatch(const Animation &referenceView) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "match"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::Match;
        command.reference = reference;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addMatchBlend(const Animation &referenceView, int srcFrame, int destFrame, int destPre,
                                    int destPost) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    const int src_frame = srcFrame;
    const int dest_frame = destFrame;
    const int dest_pre = destPre;
    const int dest_post = destPost;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "matchblend"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(*reference, src_frame, "source frame"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(a, dest_frame, "destination frame"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::MatchBlend;
        command.reference = reference;
        command.a = src_frame;
        command.b = dest_frame;
        command.c = dest_pre;
        command.d = dest_post;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addWorldSpaceBlend(const Animation &referenceView) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "worldspaceblend"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::WorldSpaceBlend;
        command.reference = reference;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addWorldSpaceBlendLoop(const Animation &referenceView, int startFrame) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    const int start_frame = startFrame;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "worldspaceblendloop"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(a, start_frame, "start frame"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::WorldSpaceBlend;
        command.reference = reference;
        command.a = start_frame;
        command.flag = true;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addRotateTo(float degrees) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (!std::isfinite(degrees))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': rotateto angle is not finite", a.name);
        Command command;
        command.type = Command::Type::RotateTo;
        command.value = degrees;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

IkRule Animation::addIkRule(IkChainId chain, IkRuleType type) {
    smdl_animation *animation = handle();
    smdl_ik_rule *result = nullptr;
    smdl_ik_rule **out_rule = &result;
    RunBody(animation->model, [&] { return AddIkRule(*animation, chain, type, false, out_rule); });
    return IkRule(result, animation->model);
}

IkRule Animation::addIkFixup(IkChainId chain, IkRuleType type) {
    smdl_animation *animation = handle();
    smdl_ik_rule *result = nullptr;
    smdl_ik_rule **out_rule = &result;
    RunBody(animation->model, [&] { return AddIkRule(*animation, chain, type, true, out_rule); });
    return IkRule(result, animation->model);
}

Animation &Animation::addWalkFrame(int endFrame, uint32_t motion) {
    smdl_animation &a = *handle();
    const int end_frame = endFrame;
    const uint32_t motion_flags = motion;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckFrame(a, end_frame, "end frame"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckMotion(a, motion_flags, false, "walkframe"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::WalkFrame;
        command.a = end_frame;
        command.motion = motion_flags;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addWalkAlign(int endFrame, const Animation &referenceView, uint32_t motion, int refFrame,
                                   int srcFrame) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    const int end_frame = endFrame;
    const uint32_t motion_flags = motion;
    const int ref_frame = refFrame;
    const int src_frame = srcFrame;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "walkalign"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(a, end_frame, "end frame"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckMotion(a, motion_flags, true, "walkalign"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(*reference, ref_frame, "reference frame"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(a, src_frame, "source frame"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::WalkAlign;
        command.a = end_frame;
        command.reference = reference;
        command.motion = motion_flags;
        command.b = ref_frame;
        command.c = src_frame;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addWalkAlignTo(int endFrame, const Animation &referenceView, uint32_t motion) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    const int end_frame = endFrame;
    const uint32_t motion_flags = motion;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "walkalignto"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckFrame(a, end_frame, "end frame"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckMotion(a, motion_flags, false, "walkalignto"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::WalkAlign;
        command.a = end_frame;
        command.reference = reference;
        command.motion = motion_flags;
        command.b = 0;
        command.c = end_frame;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addDerivative(float scale) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (!std::isfinite(scale))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': derivative scale is not finite", a.name);
        Command command;
        command.type = Command::Type::Derivative;
        command.value = scale;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addNoAnimation() {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return SimpleCommand(*animation, Command::Type::NoAnimation); });
    return *this;
}

Animation &Animation::addNoAnimationKeepDuration() {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return SimpleCommand(*animation, Command::Type::NoAnimationKeepDuration); });
    return *this;
}

Animation &Animation::addLinearDelta() {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return SimpleCommand(*animation, Command::Type::LinearDelta); });
    return *this;
}

Animation &Animation::addSplineDelta() {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return SimpleCommand(*animation, Command::Type::SplineDelta); });
    return *this;
}

Animation &Animation::addCompress(int frames) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (frames < 0)
            return Fail(a.model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': compress frames {} is negative", a.name,
                        frames);
        Command command;
        command.type = Command::Type::Compress;
        command.a = frames;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addNumFrames(int frames) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (frames < 1 || static_cast<size_t>(frames) > kMaxAnimationFrames)
            return Fail(a.model, SMDL_ERROR_OUT_OF_RANGE, "animation '{}': numframes {} must be 1..{}", a.name, frames,
                        kMaxAnimationFrames);
        Command command;
        command.type = Command::Type::NumFrames;
        command.a = frames;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addReverse() {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return SimpleCommand(*animation, Command::Type::Reverse); });
    return *this;
}

Animation &Animation::addAppendAnimation(const Animation &referenceView) {
    smdl_animation &a = *handle();
    const smdl_animation *reference = referenceView.handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckReference(a, reference, "appendanim"); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::AppendAnimation;
        command.reference = reference;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addCounterRotate(BoneId bone) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        Command command;
        command.type = Command::Type::CounterRotate;
        command.bone = bone;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addCounterRotateTo(BoneId bone, float pitch, float yaw, float roll) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        if (!std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(roll))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': counterrotateto angles are not finite",
                        a.name);
        Command command;
        command.type = Command::Type::CounterRotate;
        command.bone = bone;
        command.vectorA = std::array<float, 3>{pitch, yaw, roll};
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addLocalHierarchy(BoneId bone, BoneId parent, int start, int peak, int tail, int end) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        if (parent != SMDL_NO_BONE) {
            if (smdl_status status = CheckBoneOf(a, parent); status != SMDL_OK)
                return status;
        }
        Command command;
        command.type = Command::Type::LocalHierarchy;
        command.bone = bone;
        command.parent = parent;
        command.a = start;
        command.b = peak;
        command.c = tail;
        command.d = end;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addForceBonePosRot(BoneId bone, const std::optional<Vec3> &positionValue,
                                         const std::optional<Vec3> &anglesValue, bool rotationIsLocal) {
    smdl_animation &a = *handle();
    const float *position = positionValue ? positionValue->data() : nullptr;
    const float *angles = anglesValue ? anglesValue->data() : nullptr;
    const int rotation_is_local = rotationIsLocal;
    RunBody(a.model, [&] {
        if (smdl_status status = CheckBoneOf(a, bone); status != SMDL_OK)
            return status;
        if ((position && !AllFinite(position, 3)) || (angles && !AllFinite(angles, 3)))
            return Fail(a.model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': forceboneposrot values are not finite",
                        a.name);
        Command command;
        command.type = Command::Type::ForceBonePosRot;
        command.bone = bone;
        if (position)
            command.vectorA = std::array<float, 3>{position[0], position[1], position[2]};
        if (angles)
            command.vectorB = std::array<float, 3>{angles[0], angles[1], angles[2]};
        command.flag = rotation_is_local != 0;
        return AddCommand(a, std::move(command));
    });
    return *this;
}

Animation &Animation::addBoneDriver(BoneId bone, int axis, float value) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return AddBoneDriver(*animation, bone, axis, value, 0, 0, 0, 0, 0); });
    return *this;
}

Animation &Animation::addBoneDriver(BoneId bone, int axis, float value, int start, int peak, int tail, int end) {
    smdl_animation *animation = handle();
    RunBody(animation->model, [&] { return AddBoneDriver(*animation, bone, axis, value, 1, start, peak, tail, end); });
    return *this;
}

uint32_t Animation::commandCount() const { return static_cast<uint32_t>(handle()->commands.size()); }

Animation &Animation::moveCommand(uint32_t from, uint32_t to) {
    smdl_animation &a = *handle();
    RunBody(a.model, [&] {
        if (from >= a.commands.size() || to >= a.commands.size())
            return Fail(a.model, SMDL_ERROR_OUT_OF_RANGE,
                        "animation '{}': cannot move command {} to {} ({} commands)", a.name, from, to,
                        a.commands.size());
        Command moved = std::move(a.commands[from]);
        a.commands.erase(a.commands.begin() + from);
        a.commands.insert(a.commands.begin() + to, std::move(moved));
        return Ok(a.model);
    });
    return *this;
}

// ---- IK rules ----

IkRule &IkRule::setBone(BoneId bone) {
    smdl_ik_rule &r = *handle();
    RunBody(r.model, [&] {
        if (!r.model->skeleton.valid(bone))
            return Fail(r.model, SMDL_ERROR_OUT_OF_RANGE, "IK rule: bone {} is out of range ({} bones)", bone,
                        r.model->skeleton.bones.size());
        r.bone = bone;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setAttachment(AttachmentId attachmentId) {
    smdl_ik_rule &r = *handle();
    const smdl_attachment_id attachment = attachmentId;
    RunBody(r.model, [&] {
        if (attachment >= r.model->rig.attachments.size())
            return Fail(r.model, SMDL_ERROR_OUT_OF_RANGE, "IK rule: attachment {} is out of range ({} attachments)",
                        attachment, r.model->rig.attachments.size());
        r.attachment = attachment;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setAutoStepsEnd(int endFrame) {
    smdl_ik_rule &r = *handle();
    const int end_frame = endFrame;
    RunBody(r.model, [&] {
        if (end_frame < 0)
            return Fail(r.model, SMDL_ERROR_OUT_OF_RANGE, "IK rule: autosteps end frame {} is negative", end_frame);
        r.autoStepsEnd = end_frame;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setRange(int start, int peak, int tail, int end) {
    smdl_ik_rule &r = *handle();
    RunBody(r.model, [&] {
        for (const int frame: {start, peak, tail, end}) {
            if (frame < -1)
                return Fail(r.model, SMDL_ERROR_OUT_OF_RANGE, "IK rule: range frame {} is below -1 (unset)", frame);
        }
        r.range = std::array<int, 4>{start, peak, tail, end};
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setHeight(float height) {
    smdl_ik_rule *rule = handle();
    RunBody(rule->model,
            [&] { return SetRuleFloat(*rule, height, "height", [](smdl_ik_rule &r, float v) { r.height = v; }); });
    return *this;
}

IkRule &IkRule::setTarget(int slot) {
    smdl_ik_rule &r = *handle();
    RunBody(r.model, [&] {
        if (slot < 0)
            return Fail(r.model, SMDL_ERROR_OUT_OF_RANGE, "IK rule: target slot {} is negative", slot);
        r.target = slot;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setFloor(float floor) {
    smdl_ik_rule *rule = handle();
    RunBody(rule->model,
            [&] { return SetRuleFloat(*rule, floor, "floor", [](smdl_ik_rule &r, float v) { r.floor = v; }); });
    return *this;
}

IkRule &IkRule::setPad(float diameter) {
    smdl_ik_rule *rule = handle();
    RunBody(rule->model, [&] {
        return SetRuleFloat(*rule, diameter, "pad", [](smdl_ik_rule &r, float v) { r.radius = v / 2.0f; });
    });
    return *this;
}

IkRule &IkRule::setRadius(float radius) {
    smdl_ik_rule *rule = handle();
    RunBody(rule->model,
            [&] { return SetRuleFloat(*rule, radius, "radius", [](smdl_ik_rule &r, float v) { r.radius = v; }); });
    return *this;
}

IkRule &IkRule::setContact(int contact) {
    smdl_ik_rule &r = *handle();
    RunBody(r.model, [&] {
        r.contact = contact;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setUseSequence(bool enabledFlag) {
    smdl_ik_rule &r = *handle();
    const int enabled = enabledFlag;
    RunBody(r.model, [&] {
        if (enabled)
            r.useMode = 1;
        else if (r.useMode == 1)
            r.useMode = 3;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setUseSource(bool enabledFlag) {
    smdl_ik_rule &r = *handle();
    const int enabled = enabledFlag;
    RunBody(r.model, [&] {
        if (enabled)
            r.useMode = 2;
        else if (r.useMode == 2)
            r.useMode = 3;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setFakeOrigin(const Vec3 &originValue) {
    smdl_ik_rule &r = *handle();
    const float *origin = originValue.data();
    RunBody(r.model, [&] {
        if (!origin || !AllFinite(origin, 3))
            return Fail(r.model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake origin is null or not finite");
        r.fakeOrigin = ToVector(origin);
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setFakeRotation(const Quat &rotationValue) {
    smdl_ik_rule &r = *handle();
    const float *rotation = rotationValue.data();
    RunBody(r.model, [&] {
        if (!rotation || !AllFinite(rotation, 4))
            return Fail(r.model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake rotation is null or not finite");
        Quaternion q(rotation[0], rotation[1], rotation[2], rotation[3]);
        const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (!(lengthSquared > 1e-12f))
            return Fail(r.model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake rotation quaternion has zero length");
        if (std::fabs(lengthSquared - 1.0f) > 1e-6f)
            QuaternionNormalize(q);
        r.fakeRotation = q;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setFakeRotationEuler(const Vec3 &eulerValue) {
    smdl_ik_rule &r = *handle();
    const float *euler = eulerValue.data();
    RunBody(r.model, [&] {
        if (!euler || !AllFinite(euler, 3))
            return Fail(r.model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake rotation is null or not finite");
        Quaternion q;
        AngleQuaternion(RadianEuler(euler[0], euler[1], euler[2]), q);
        r.fakeRotation = q;
        return Ok(r.model);
    });
    return *this;
}

IkRule &IkRule::setFakeAngles(const Vec3 &anglesValue) {
    smdl_ik_rule &r = *handle();
    const float *angles = anglesValue.data();
    RunBody(r.model, [&] {
        if (!angles || !AllFinite(angles, 3))
            return Fail(r.model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake angles are null or not finite");
        Quaternion q;
        AngleQuaternion(QAngle(angles[0], angles[1], angles[2]), q);
        r.fakeRotation = q;
        return Ok(r.model);
    });
    return *this;
}

namespace detail {

Animation ModelAnimationApi::createAnimation(std::string_view nameView, float fps, uint32_t frameCount) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    const uint32_t frame_count = frameCount;
    smdl_model *model = this->model();
    smdl_animation *result = nullptr;
    smdl_animation **out_animation = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "animation", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (!std::isfinite(fps) || fps <= 0.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': fps {} must be above 0", name, fps);
        if (frame_count == 0 || frame_count > kMaxAnimationFrames)
            return Fail(model, SMDL_ERROR_LIMIT, "animation '{}': frame count {} must be 1..{}", name, frame_count,
                        kMaxAnimationFrames);
        if (model->animations.list.size() >= kMaxAnimations)
            return Fail(model, SMDL_ERROR_LIMIT, "animation '{}': a model has at most {} animations", name,
                        kMaxAnimations);
        if (AnimationNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "animation '{}' already exists", name);
        if (SequenceNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME,
                        "animation '{}': a sequence of that name exists (animations and sequences share the names)",
                        name);

        auto animation = std::make_unique<smdl_animation>();
        animation->model = model;
        animation->index = static_cast<uint32_t>(model->animations.list.size());
        animation->name = name;
        animation->fps = fps;
        animation->frameCount = frame_count;
        smdl_animation *handle = animation.get();
        model->animations.list.push_back(std::move(animation));
        if (out_animation)
            *out_animation = handle;
        return Ok(model);
    });
    return Animation(result, model);
}

Animation ModelAnimationApi::findAnimation(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_animation *result = nullptr;
    smdl_animation **out_animation = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "animation name is null or empty");
        for (const auto &animation: model->animations.list) {
            if (IEquals(animation->name, name)) {
                if (out_animation)
                    *out_animation = animation.get();
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "no animation named '{}'", name);
    });
    return Animation(result, model);
}

uint32_t ModelAnimationApi::animationCount() const {
    return static_cast<uint32_t>(model()->animations.list.size());
}

Animation ModelAnimationApi::animation(uint32_t index) const {
    smdl_model *model = this->model();
    smdl_animation *result = nullptr;
    smdl_animation **out_animation = &result;
    RunBody(model, [&] {
        if (index >= model->animations.list.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "animation {} is out of range ({} animations)", index,
                        model->animations.list.size());
        *out_animation = model->animations.list[index].get();
        return Ok(model);
    });
    return Animation(result, model);
}

} // namespace detail
} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
namespace {
    smdl::Animation View(smdl_animation *a) { return smdl::Animation(a, a->model); }

    smdl::IkRule View(smdl_ik_rule *r) { return smdl::IkRule(r, r->model); }

    // `reference` is not null (checked by the caller)
    smdl::Animation Ref(const smdl_animation *reference) {
        return smdl::Animation(const_cast<smdl_animation *>(reference), reference->model);
    }
}

extern "C" {

// ---- creation ----

SMDL_API smdl_status smdl_model_create_animation(smdl_model *model, const char *name, float fps, uint32_t frame_count,
                                                 smdl_animation **out_animation) {
    if (out_animation)
        *out_animation = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Animation animation =
                smdl::detail::BorrowModel(model).createAnimation(name ? name : "", fps, frame_count);
        if (out_animation)
            *out_animation = animation.handle();
    });
}

SMDL_API smdl_status smdl_model_find_animation(const smdl_model *model, const char *name,
                                               smdl_animation **out_animation) {
    if (out_animation)
        *out_animation = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Animation animation =
                smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findAnimation(name ? name : "");
        if (out_animation)
            *out_animation = animation.handle();
    });
}

SMDL_API uint32_t smdl_model_animation_count(const smdl_model *model) {
    return model ? smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).animationCount() : 0;
}

SMDL_API smdl_status smdl_model_get_animation(const smdl_model *model, uint32_t index,
                                              smdl_animation **out_animation) {
    if (out_animation)
        *out_animation = nullptr;
    if (!model || !out_animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        *out_animation = smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).animation(index).handle();
    });
}

SMDL_API const char *smdl_animation_get_name(const smdl_animation *animation) {
    return animation ? animation->name.c_str() : "";
}

SMDL_API float smdl_animation_get_fps(const smdl_animation *animation) { return animation ? animation->fps : 0.0f; }

SMDL_API uint32_t smdl_animation_get_frame_count(const smdl_animation *animation) {
    return animation ? animation->frameCount : 0;
}

// ---- frames ----

SMDL_API smdl_status smdl_animation_set_bone_frames(smdl_animation *animation, smdl_bone_id bone,
                                                    const float *positions, size_t position_count,
                                                    size_t position_stride, const float *rotations,
                                                    size_t rotation_count, size_t rotation_stride) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] {
        View(animation).setBoneFrames(bone, positions, position_count, position_stride, rotations, rotation_count,
                                      rotation_stride);
    });
}

SMDL_API smdl_status smdl_animation_set_bone_frames_euler(smdl_animation *animation, smdl_bone_id bone,
                                                          const float *positions, size_t position_count,
                                                          size_t position_stride, const float *eulers,
                                                          size_t euler_count, size_t euler_stride) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] {
        View(animation).setBoneFramesEuler(bone, positions, position_count, position_stride, eulers, euler_count,
                                           euler_stride);
    });
}

SMDL_API smdl_status smdl_animation_set_rest_pose(smdl_animation *animation, smdl_bone_id bone,
                                                  const float position[3], const float rotation[4]) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position || !rotation) {
        if (smdl_status status = CheckBoneOf(*animation, bone); status != SMDL_OK)
            return status;
        return Fail(animation->model, SMDL_ERROR_INVALID_ARGUMENT,
                    "animation '{}': rest pose of bone '{}' is null or not finite", animation->name,
                    BoneName(*animation->model, bone));
    }
    return CallCpp(animation->model, [&] {
        View(animation).setRestPose(bone, {position[0], position[1], position[2]},
                                    {rotation[0], rotation[1], rotation[2], rotation[3]});
    });
}

SMDL_API smdl_status smdl_animation_set_rest_pose_euler(smdl_animation *animation, smdl_bone_id bone,
                                                        const float position[3], const float euler[3]) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position || !euler) {
        if (smdl_status status = CheckBoneOf(*animation, bone); status != SMDL_OK)
            return status;
        return Fail(animation->model, SMDL_ERROR_INVALID_ARGUMENT,
                    "animation '{}': rest pose of bone '{}' is null or not finite", animation->name,
                    BoneName(*animation->model, bone));
    }
    return CallCpp(animation->model, [&] {
        View(animation).setRestPoseEuler(bone, {position[0], position[1], position[2]},
                                         {euler[0], euler[1], euler[2]});
    });
}

// ---- options ----

SMDL_API smdl_status smdl_animation_set_fps(smdl_animation *animation, float fps) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setFps(fps); });
}

SMDL_API smdl_status smdl_animation_set_origin(smdl_animation *animation, const float origin[3]) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!origin)
        return Fail(animation->model, SMDL_ERROR_INVALID_ARGUMENT, "animation '{}': origin is null or not finite",
                    animation->name);
    return CallCpp(animation->model, [&] { View(animation).setOrigin({origin[0], origin[1], origin[2]}); });
}

SMDL_API smdl_status smdl_animation_set_rotate(smdl_animation *animation, float degrees) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setRotate(degrees); });
}

SMDL_API smdl_status smdl_animation_set_angles(smdl_animation *animation, float x, float y, float z) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setAngles(x, y, z); });
}

SMDL_API smdl_status smdl_animation_set_scale(smdl_animation *animation, float scale) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setScale(scale); });
}

SMDL_API smdl_status smdl_animation_set_loop(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setLoop(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_no_force_loop(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setNoForceLoop(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_start_loop(smdl_animation *animation, int frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setStartLoop(frame); });
}

SMDL_API smdl_status smdl_animation_set_percent_start_loop(smdl_animation *animation, float percent) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setPercentStartLoop(percent); });
}

SMDL_API smdl_status smdl_animation_set_fudge_loop(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setFudgeLoop(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_snap(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setSnap(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_post(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setPost(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_auto_ik(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setAutoIk(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_motion_rollback(smdl_animation *animation, float rollback) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setMotionRollback(rollback); });
}

SMDL_API smdl_status smdl_animation_set_no_anim_block(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setNoAnimBlock(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_no_anim_block_stall(smdl_animation *animation, int enabled) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setNoAnimBlockStall(enabled != 0); });
}

SMDL_API smdl_status smdl_animation_set_no_stall_frames(smdl_animation *animation, float frames) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setNoStallFrames(frames); });
}

SMDL_API smdl_status smdl_animation_set_frame_range(smdl_animation *animation, int first_frame, int last_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setFrameRange(first_frame, last_frame); });
}

SMDL_API smdl_status smdl_animation_set_frame_start(smdl_animation *animation, int first_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setFrameStart(first_frame); });
}

SMDL_API smdl_status smdl_animation_set_block_name(smdl_animation *animation, const char *name) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setBlockName(name ? name : ""); });
}

SMDL_API smdl_status smdl_animation_set_motion_extraction(smdl_animation *animation, uint32_t motion_flags) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).setMotionExtraction(motion_flags); });
}

// ---- commands ----

SMDL_API smdl_status smdl_animation_add_fixup_loop(smdl_animation *animation, int first_frame, int last_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addFixupLoop(first_frame, last_frame); });
}

SMDL_API smdl_status smdl_animation_add_weight_list(smdl_animation *animation, smdl_weight_list_id weight_list) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addWeightList(weight_list); });
}

SMDL_API smdl_status smdl_animation_add_subtract(smdl_animation *animation, const smdl_animation *reference, int frame,
                                                 int post) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "subtract"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addSubtract(Ref(reference), frame, post != 0); });
}

SMDL_API smdl_status smdl_animation_add_align(smdl_animation *animation, const smdl_animation *reference,
                                              uint32_t motion_flags, int src_frame, int dest_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "align"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model,
                   [&] { View(animation).addAlign(Ref(reference), motion_flags, src_frame, dest_frame); });
}

SMDL_API smdl_status smdl_animation_add_align_to(smdl_animation *animation, const smdl_animation *reference) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "align"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addAlignTo(Ref(reference)); });
}

SMDL_API smdl_status smdl_animation_add_align_bone(smdl_animation *animation, smdl_bone_id bone,
                                                   const smdl_animation *reference, uint32_t motion_flags,
                                                   int src_frame, int dest_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "align"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model,
                   [&] { View(animation).addAlignBone(bone, Ref(reference), motion_flags, src_frame, dest_frame); });
}

SMDL_API smdl_status smdl_animation_add_align_bone_to(smdl_animation *animation, smdl_bone_id bone,
                                                      const smdl_animation *reference) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "align"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addAlignBoneTo(bone, Ref(reference)); });
}

SMDL_API smdl_status smdl_animation_add_match(smdl_animation *animation, const smdl_animation *reference) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "match"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addMatch(Ref(reference)); });
}

SMDL_API smdl_status smdl_animation_add_match_blend(smdl_animation *animation, const smdl_animation *reference,
                                                    int src_frame, int dest_frame, int dest_pre, int dest_post) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "matchblend"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] {
        View(animation).addMatchBlend(Ref(reference), src_frame, dest_frame, dest_pre, dest_post);
    });
}

SMDL_API smdl_status smdl_animation_add_world_space_blend(smdl_animation *animation, const smdl_animation *reference) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "worldspaceblend"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addWorldSpaceBlend(Ref(reference)); });
}

SMDL_API smdl_status smdl_animation_add_world_space_blend_loop(smdl_animation *animation,
                                                               const smdl_animation *reference, int start_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "worldspaceblendloop"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addWorldSpaceBlendLoop(Ref(reference), start_frame); });
}

SMDL_API smdl_status smdl_animation_add_rotate_to(smdl_animation *animation, float degrees) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addRotateTo(degrees); });
}

SMDL_API smdl_status smdl_animation_add_ik_rule(smdl_animation *animation, smdl_ik_chain_id chain,
                                                smdl_ik_rule_type type, smdl_ik_rule **out_rule) {
    if (out_rule)
        *out_rule = nullptr;
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] {
        const smdl::IkRule rule = View(animation).addIkRule(chain, type);
        if (out_rule)
            *out_rule = rule.handle();
    });
}

SMDL_API smdl_status smdl_animation_add_ik_fixup(smdl_animation *animation, smdl_ik_chain_id chain,
                                                 smdl_ik_rule_type type, smdl_ik_rule **out_rule) {
    if (out_rule)
        *out_rule = nullptr;
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] {
        const smdl::IkRule rule = View(animation).addIkFixup(chain, type);
        if (out_rule)
            *out_rule = rule.handle();
    });
}

SMDL_API smdl_status smdl_animation_add_walk_frame(smdl_animation *animation, int end_frame, uint32_t motion_flags) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addWalkFrame(end_frame, motion_flags); });
}

SMDL_API smdl_status smdl_animation_add_walk_align(smdl_animation *animation, int end_frame,
                                                   const smdl_animation *reference, uint32_t motion_flags,
                                                   int ref_frame, int src_frame) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "walkalign"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] {
        View(animation).addWalkAlign(end_frame, Ref(reference), motion_flags, ref_frame, src_frame);
    });
}

SMDL_API smdl_status smdl_animation_add_walk_align_to(smdl_animation *animation, int end_frame,
                                                      const smdl_animation *reference, uint32_t motion_flags) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "walkalignto"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model,
                   [&] { View(animation).addWalkAlignTo(end_frame, Ref(reference), motion_flags); });
}

SMDL_API smdl_status smdl_animation_add_derivative(smdl_animation *animation, float scale) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addDerivative(scale); });
}

SMDL_API smdl_status smdl_animation_add_no_animation(smdl_animation *animation) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addNoAnimation(); });
}

SMDL_API smdl_status smdl_animation_add_no_animation_keep_duration(smdl_animation *animation) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addNoAnimationKeepDuration(); });
}

SMDL_API smdl_status smdl_animation_add_linear_delta(smdl_animation *animation) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addLinearDelta(); });
}

SMDL_API smdl_status smdl_animation_add_spline_delta(smdl_animation *animation) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addSplineDelta(); });
}

SMDL_API smdl_status smdl_animation_add_compress(smdl_animation *animation, int frames) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addCompress(frames); });
}

SMDL_API smdl_status smdl_animation_add_num_frames(smdl_animation *animation, int frames) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addNumFrames(frames); });
}

SMDL_API smdl_status smdl_animation_add_reverse(smdl_animation *animation) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addReverse(); });
}

SMDL_API smdl_status smdl_animation_add_append_animation(smdl_animation *animation, const smdl_animation *reference) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (smdl_status status = CheckReference(*animation, reference, "appendanim"); status != SMDL_OK)
        return status;
    return CallCpp(animation->model, [&] { View(animation).addAppendAnimation(Ref(reference)); });
}

SMDL_API smdl_status smdl_animation_add_counter_rotate(smdl_animation *animation, smdl_bone_id bone) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addCounterRotate(bone); });
}

SMDL_API smdl_status smdl_animation_add_counter_rotate_to(smdl_animation *animation, smdl_bone_id bone, float pitch,
                                                          float yaw, float roll) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).addCounterRotateTo(bone, pitch, yaw, roll); });
}

SMDL_API smdl_status smdl_animation_add_local_hierarchy(smdl_animation *animation, smdl_bone_id bone,
                                                        smdl_bone_id parent, int start, int peak, int tail, int end) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model,
                   [&] { View(animation).addLocalHierarchy(bone, parent, start, peak, tail, end); });
}

SMDL_API smdl_status smdl_animation_add_force_bone_pos_rot(smdl_animation *animation, smdl_bone_id bone,
                                                           const float position[3], const float angles[3],
                                                           int rotation_is_local) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] {
        std::optional<smdl::Vec3> positionValue;
        std::optional<smdl::Vec3> anglesValue;
        if (position)
            positionValue = smdl::Vec3{position[0], position[1], position[2]};
        if (angles)
            anglesValue = smdl::Vec3{angles[0], angles[1], angles[2]};
        View(animation).addForceBonePosRot(bone, positionValue, anglesValue, rotation_is_local != 0);
    });
}

SMDL_API smdl_status smdl_animation_add_bone_driver(smdl_animation *animation, smdl_bone_id bone, int axis,
                                                    float value, int use_range, int start, int peak, int tail,
                                                    int end) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] {
        if (use_range != 0)
            View(animation).addBoneDriver(bone, axis, value, start, peak, tail, end);
        else
            View(animation).addBoneDriver(bone, axis, value);
    });
}

SMDL_API uint32_t smdl_animation_command_count(const smdl_animation *animation) {
    return animation ? static_cast<uint32_t>(animation->commands.size()) : 0;
}

SMDL_API smdl_status smdl_animation_move_command(smdl_animation *animation, uint32_t from, uint32_t to) {
    if (!animation)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(animation->model, [&] { View(animation).moveCommand(from, to); });
}

// ---- IK rules ----

SMDL_API smdl_status smdl_ik_rule_set_bone(smdl_ik_rule *rule, smdl_bone_id bone) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setBone(bone); });
}

SMDL_API smdl_status smdl_ik_rule_set_attachment(smdl_ik_rule *rule, smdl_attachment_id attachment) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setAttachment(attachment); });
}

SMDL_API smdl_status smdl_ik_rule_set_autosteps_end(smdl_ik_rule *rule, int end_frame) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setAutoStepsEnd(end_frame); });
}

SMDL_API smdl_status smdl_ik_rule_set_range(smdl_ik_rule *rule, int start, int peak, int tail, int end) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setRange(start, peak, tail, end); });
}

SMDL_API smdl_status smdl_ik_rule_set_height(smdl_ik_rule *rule, float height) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setHeight(height); });
}

SMDL_API smdl_status smdl_ik_rule_set_target(smdl_ik_rule *rule, int slot) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setTarget(slot); });
}

SMDL_API smdl_status smdl_ik_rule_set_floor(smdl_ik_rule *rule, float floor) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setFloor(floor); });
}

SMDL_API smdl_status smdl_ik_rule_set_pad(smdl_ik_rule *rule, float diameter) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setPad(diameter); });
}

SMDL_API smdl_status smdl_ik_rule_set_radius(smdl_ik_rule *rule, float radius) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setRadius(radius); });
}

SMDL_API smdl_status smdl_ik_rule_set_contact(smdl_ik_rule *rule, int contact) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setContact(contact); });
}

SMDL_API smdl_status smdl_ik_rule_set_use_sequence(smdl_ik_rule *rule, int enabled) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setUseSequence(enabled != 0); });
}

SMDL_API smdl_status smdl_ik_rule_set_use_source(smdl_ik_rule *rule, int enabled) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(rule->model, [&] { View(rule).setUseSource(enabled != 0); });
}

SMDL_API smdl_status smdl_ik_rule_set_fake_origin(smdl_ik_rule *rule, const float origin[3]) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!origin)
        return Fail(rule->model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake origin is null or not finite");
    return CallCpp(rule->model, [&] { View(rule).setFakeOrigin({origin[0], origin[1], origin[2]}); });
}

SMDL_API smdl_status smdl_ik_rule_set_fake_rotation(smdl_ik_rule *rule, const float rotation[4]) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!rotation)
        return Fail(rule->model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake rotation is null or not finite");
    return CallCpp(rule->model, [&] {
        View(rule).setFakeRotation({rotation[0], rotation[1], rotation[2], rotation[3]});
    });
}

SMDL_API smdl_status smdl_ik_rule_set_fake_rotation_euler(smdl_ik_rule *rule, const float euler[3]) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!euler)
        return Fail(rule->model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake rotation is null or not finite");
    return CallCpp(rule->model, [&] { View(rule).setFakeRotationEuler({euler[0], euler[1], euler[2]}); });
}

SMDL_API smdl_status smdl_ik_rule_set_fake_angles(smdl_ik_rule *rule, const float angles[3]) {
    if (!rule)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!angles)
        return Fail(rule->model, SMDL_ERROR_INVALID_ARGUMENT, "IK rule: fake angles are null or not finite");
    return CallCpp(rule->model, [&] { View(rule).setFakeAngles({angles[0], angles[1], angles[2]}); });
}

} // extern "C"

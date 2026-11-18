// smdl_sequence.h: sequences
#include "studiomdl/c/smdl_sequence.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>

#include "anim_util.hpp"
#include "studiomdl/apply_globals.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    constexpr size_t kMaxSequences = MAXSTUDIOSEQUENCES;
    constexpr size_t kMaxEvents = MAXSTUDIOEVENTS;
    constexpr size_t kMaxAnimTags = MAXSTUDIOTAGS;
    constexpr size_t kMaxActivityModifiers = MAXSTUDIOACTIVITYMODIFIERS;
    constexpr size_t kMaxLayers = 64;
    constexpr size_t kMaxIkLocks = 64;
    constexpr size_t kMaxBlendAxes = 2;
    constexpr size_t kMaxEventOptions = 63; // s_event_t::options is a FixedString<64>
    constexpr uint32_t kSequenceFlagMask = SMDL_SEQUENCE_SNAP | SMDL_SEQUENCE_DELTA | SMDL_SEQUENCE_PRE_DELTA |
                                           SMDL_SEQUENCE_WORLD_SPACE | SMDL_SEQUENCE_WORLD_RELATIVE |
                                           SMDL_SEQUENCE_POST | SMDL_SEQUENCE_AUTOPLAY | SMDL_SEQUENCE_REALTIME |
                                           SMDL_SEQUENCE_HIDDEN;
    constexpr uint32_t kLayerFlagMask =
            SMDL_LAYER_XFADE | SMDL_LAYER_SPLINE | SMDL_LAYER_NO_BLEND | SMDL_LAYER_LOCAL | SMDL_LAYER_POSE;

    bool AnimationNameInUse(const smdl_model &model, std::string_view name) {
        return std::ranges::any_of(model.animations.list, [&](const auto &a) { return IEquals(a->name, name); });
    }

    bool SequenceNameInUse(const smdl_model &model, std::string_view name) {
        return std::ranges::any_of(model.sequences.list, [&](const auto &s) { return IEquals(s->name, name); });
    }


    smdl_status CheckAnimation(const smdl_sequence &sequence, const smdl_animation *animation, std::string_view what) {
        if (!animation)
            return Fail(sequence.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': {} animation is null",
                        sequence.name, what);
        if (animation->model != sequence.model)
            return Fail(sequence.model, SMDL_ERROR_INVALID_ARGUMENT,
                        "sequence '{}': {} animation '{}' belongs to another model", sequence.name, what,
                        animation->name);
        return SMDL_OK;
    }

    smdl_status CheckSequenceRef(const smdl_sequence &sequence, const smdl_sequence *other, std::string_view what) {
        if (!other)
            return Fail(sequence.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': {} sequence is null",
                        sequence.name, what);
        if (other->model != sequence.model)
            return Fail(sequence.model, SMDL_ERROR_INVALID_ARGUMENT,
                        "sequence '{}': {} sequence '{}' belongs to another model", sequence.name, what, other->name);
        return SMDL_OK;
    }

    smdl_status CheckPose(const smdl_sequence &sequence, smdl_pose_param_id pose) {
        if (pose >= sequence.model->rig.poseParameters.size())
            return Fail(sequence.model, SMDL_ERROR_OUT_OF_RANGE,
                        "sequence '{}': pose parameter {} is out of range ({} pose parameters)", sequence.name, pose,
                        sequence.model->rig.poseParameters.size());
        return SMDL_OK;
    }

    smdl_status CheckText(const smdl_sequence &sequence, const char *text, std::string_view what) {
        if (std::string bad = BadObjectName(text, what, MAXSTUDIONAME); !bad.empty())
            return Fail(sequence.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': {}", sequence.name, bad);
        return SMDL_OK;
    }

    template<class F>
    smdl_status SetFloat(smdl_sequence &s, float value, std::string_view what, F &&apply) {
        if (!std::isfinite(value))
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': {} is not finite", s.name, what);
        apply(s, value);
        return Ok(s.model);
    }

    smdl_status SetAnimationPointer(smdl_sequence &s, const smdl_animation *animation, std::string_view what,
                                    const smdl_animation *smdl_sequence::*member) {
        if (smdl_status status = CheckAnimation(s, animation, what); status != SMDL_OK)
            return status;
        s.*member = animation;
        return Ok(s.model);
    }

    smdl_status AddEvent(smdl_sequence &s, const char *name, bool byCycle, int frame, float cycle,
                         const char *options) {
        if (smdl_status status = CheckText(s, name, "event"); status != SMDL_OK)
            return status;
        if (byCycle ? !std::isfinite(cycle) : frame < 0)
            return Fail(s.model, SMDL_ERROR_OUT_OF_RANGE, "sequence '{}': event '{}' has no valid position", s.name,
                        name);
        if (options && std::string_view(options).size() > kMaxEventOptions)
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT,
                        "sequence '{}': options of event '{}' are too long (max {} bytes)", s.name, name,
                        kMaxEventOptions);
        if (s.events.size() >= kMaxEvents)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} events", s.name, kMaxEvents);
        smdl_sequence::Event event;
        event.name = name;
        event.byCycle = byCycle;
        event.frame = frame;
        event.cycle = cycle;
        if (options)
            event.options = options;
        s.events.push_back(std::move(event));
        return Ok(s.model);
    }
}


namespace smdl {

std::string_view Sequence::name() const { return handle()->name; }

// ---- animations and blends ----

Sequence &Sequence::addAnimation(const Animation &animationRef) {
    const smdl_animation *animation = animationRef.handle();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckAnimation(s, animation, "blend"); status != SMDL_OK)
            return status;
        s.animations.push_back(animation);
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::setBlendWidth(uint32_t width) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (width == 0)
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': blend width must be above 0", s.name);
        s.blendWidth = width;
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::setBlendReference(const Animation &animationRef) {
    const smdl_animation *animation = animationRef.handle();
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] {
        return SetAnimationPointer(*sequence, animation, "blend reference", &smdl_sequence::blendReference);
    });
    return *this;
}

Sequence &Sequence::setBlendCompare(const Animation &animationRef) {
    const smdl_animation *animation = animationRef.handle();
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] {
        return SetAnimationPointer(*sequence, animation, "blend compare", &smdl_sequence::blendCompare);
    });
    return *this;
}

Sequence &Sequence::setBlendCenter(const Animation &animationRef) {
    const smdl_animation *animation = animationRef.handle();
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] {
        return SetAnimationPointer(*sequence, animation, "blend center", &smdl_sequence::blendCenter);
    });
    return *this;
}

Sequence &Sequence::addBlend(PoseParamId pose_parameter, float start, float end) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckPose(s, pose_parameter); status != SMDL_OK)
            return status;
        if (!std::isfinite(start) || !std::isfinite(end))
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': blend range is not finite", s.name);
        if (s.blends.size() >= kMaxBlendAxes)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} blend axes", s.name, kMaxBlendAxes);
        smdl_sequence::Blend blend;
        blend.pose = pose_parameter;
        blend.start = start;
        blend.end = end;
        s.blends.push_back(blend);
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::addCalcBlend(PoseParamId pose_parameter, AttachmentId attachment, uint32_t control) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckPose(s, pose_parameter); status != SMDL_OK)
            return status;
        if (attachment >= s.model->rig.attachments.size())
            return Fail(s.model, SMDL_ERROR_OUT_OF_RANGE,
                        "sequence '{}': attachment {} is out of range ({} attachments)", s.name, attachment,
                        s.model->rig.attachments.size());
        if (control == 0 || (control & ~kMotionMask) != 0 || (control & (control - 1)) != 0)
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT,
                        "sequence '{}': the calcblend control must be exactly one SMDL_MOTION_* flag", s.name);
        if (s.blends.size() >= kMaxBlendAxes)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} blend axes", s.name, kMaxBlendAxes);
        smdl_sequence::Blend blend;
        blend.calc = true;
        blend.pose = pose_parameter;
        blend.attachment = attachment;
        blend.control = control;
        s.blends.push_back(blend);
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::setPoseCycle(PoseParamId pose_parameter) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckPose(s, pose_parameter); status != SMDL_OK)
            return status;
        s.poseCycle = pose_parameter;
        return Ok(s.model);
    });
    return *this;
}

// ---- activity, events, tags ----

Sequence &Sequence::setActivity(std::string_view nameView, int weight) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckText(s, name, "activity"); status != SMDL_OK)
            return status;
        s.activity = name;
        s.activityWeight = weight;
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::addActivityModifier(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckText(s, name, "activity modifier"); status != SMDL_OK)
            return status;
        if (s.activityModifiers.size() >= kMaxActivityModifiers)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} activity modifiers", s.name,
                        kMaxActivityModifiers);
        s.activityModifiers.emplace_back(name);
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::addEvent(std::string_view nameView, int frame, std::string_view optionsView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    const std::string optionsStorage(optionsView);
    const char *options = optionsView.empty() ? nullptr : optionsStorage.c_str();
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] { return AddEvent(*sequence, name, false, frame, 0.0f, options); });
    return *this;
}

Sequence &Sequence::addEventCycle(std::string_view nameView, float cycle, std::string_view optionsView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    const std::string optionsStorage(optionsView);
    const char *options = optionsView.empty() ? nullptr : optionsStorage.c_str();
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] { return AddEvent(*sequence, name, true, 0, cycle, options); });
    return *this;
}

Sequence &Sequence::addAnimTag(std::string_view nameView, float cycle) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckText(s, name, "animation tag"); status != SMDL_OK)
            return status;
        if (!std::isfinite(cycle))
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': animtag cycle is not finite", s.name);
        if (s.animTags.size() >= kMaxAnimTags)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} animation tags", s.name,
                        kMaxAnimTags);
        s.animTags.push_back({name, cycle});
        return Ok(s.model);
    });
    return *this;
}

// ---- timing and flags ----

Sequence &Sequence::setFadeIn(float seconds) {
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] {
        return SetFloat(*sequence, seconds, "fade in time", [](smdl_sequence &s, float v) { s.fadeIn = v; });
    });
    return *this;
}

Sequence &Sequence::setFadeOut(float seconds) {
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] {
        return SetFloat(*sequence, seconds, "fade out time", [](smdl_sequence &s, float v) { s.fadeOut = v; });
    });
    return *this;
}

Sequence &Sequence::setExitPhase(float phase) {
    smdl_sequence *sequence = handle();
    RunBody(sequence->model, [&] {
        return SetFloat(*sequence, phase, "exit phase", [](smdl_sequence &s, float v) { s.exitPhase = v; });
    });
    return *this;
}

Sequence &Sequence::setFlags(uint32_t flags, bool enabled) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if ((flags & ~kSequenceFlagMask) != 0)
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': unknown SMDL_SEQUENCE_* bits", s.name);
        if (enabled)
            s.flags |= flags;
        else
            s.flags &= ~flags;
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::setRootDriver(BoneId bone) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckBone(s.model, bone, "sequence", s.name); status != SMDL_OK)
            return status;
        s.rootDriver = bone;
        return Ok(s.model);
    });
    return *this;
}

// ---- transition graph ----

Sequence &Sequence::setNode(std::string_view nodeView) {
    const std::string nodeStorage(nodeView);
    const char *node = nodeStorage.c_str();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckText(s, node, "node"); status != SMDL_OK)
            return status;
        s.node = smdl_sequence::Node{true, node, node, false};
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::setTransition(std::string_view fromView, std::string_view toView, bool reverse) {
    const std::string fromStorage(fromView);
    const char *from = fromStorage.c_str();
    const std::string toStorage(toView);
    const char *to = toStorage.c_str();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckText(s, from, "transition source node"); status != SMDL_OK)
            return status;
        if (smdl_status status = CheckText(s, to, "transition destination node"); status != SMDL_OK)
            return status;
        s.node = smdl_sequence::Node{false, from, to, reverse};
        return Ok(s.model);
    });
    return *this;
}

// ---- layers and IK locks ----

Sequence &Sequence::addLayer(const Sequence &layerRef, bool local) {
    const smdl_sequence *layer = layerRef.handle();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckSequenceRef(s, layer, "layer"); status != SMDL_OK)
            return status;
        if (s.layers.size() >= kMaxLayers)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} layers", s.name, kMaxLayers);
        smdl_sequence::Layer entry;
        entry.layer = layer;
        entry.local = local;
        s.layers.push_back(entry);
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::addBlendLayer(const Sequence &layerRef, float start, float peak, float tail, float end,
                                  uint32_t flags, PoseParamId pose_parameter) {
    const smdl_sequence *layer = layerRef.handle();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (smdl_status status = CheckSequenceRef(s, layer, "blend layer"); status != SMDL_OK)
            return status;
        if ((flags & ~kLayerFlagMask) != 0)
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': unknown SMDL_LAYER_* bits", s.name);
        if (!std::isfinite(start) || !std::isfinite(peak) || !std::isfinite(tail) || !std::isfinite(end))
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': blendlayer frames are not finite",
                        s.name);
        if ((flags & SMDL_LAYER_POSE) != 0) {
            if (smdl_status status = CheckPose(s, pose_parameter); status != SMDL_OK)
                return status;
        }
        if (s.layers.size() >= kMaxLayers)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} layers", s.name, kMaxLayers);
        smdl_sequence::Layer entry;
        entry.layer = layer;
        entry.isBlend = true;
        entry.start = start;
        entry.peak = peak;
        entry.tail = tail;
        entry.end = end;
        entry.flags = flags;
        entry.local = (flags & SMDL_LAYER_LOCAL) != 0;
        entry.pose = (flags & SMDL_LAYER_POSE) != 0 ? pose_parameter : 0;
        s.layers.push_back(entry);
        return Ok(s.model);
    });
    return *this;
}

Sequence &Sequence::addIkLock(IkChainId chain, float position_weight, float local_rotation_weight) {
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (chain >= s.model->rig.ikChains.size())
            return Fail(s.model, SMDL_ERROR_OUT_OF_RANGE, "sequence '{}': IK chain {} is out of range ({} chains)",
                        s.name, chain, s.model->rig.ikChains.size());
        if (!std::isfinite(position_weight) || !std::isfinite(local_rotation_weight))
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': IK lock weights are not finite", s.name);
        if (s.ikLocks.size() >= kMaxIkLocks)
            return Fail(s.model, SMDL_ERROR_LIMIT, "sequence '{}' has at most {} IK locks", s.name, kMaxIkLocks);
        s.ikLocks.push_back({chain, position_weight, local_rotation_weight});
        return Ok(s.model);
    });
    return *this;
}

// ---- keyvalues ----

Sequence &Sequence::addKeyValues(std::string_view textView) {
    const std::string textStorage(textView);
    const char *text = textStorage.c_str();
    smdl_sequence *sequence = handle();
    smdl_sequence &s = *sequence;
    RunBody(s.model, [&] {
        if (!text)
            return Fail(s.model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': keyvalues text is null", s.name);
        std::string formatted;
        std::string error;
        if (!FormatKeyValuesText(text, formatted, error))
            return Fail(s.model, SMDL_ERROR_SYNTAX, "sequence '{}': keyvalues: {}", s.name, error);
        s.keyValues += formatted;
        return Ok(s.model);
    });
    return *this;
}

namespace detail {

Sequence ModelSequenceApi::createSequence(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_sequence *result = nullptr;
    smdl_sequence **out_sequence = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "sequence", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (model->sequences.list.size() >= kMaxSequences)
            return Fail(model, SMDL_ERROR_LIMIT, "sequence '{}': a model has at most {} sequences", name,
                        kMaxSequences);
        if (SequenceNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "sequence '{}' already exists", name);
        if (AnimationNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME,
                        "sequence '{}': an animation of that name exists (animations and sequences share the names)",
                        name);
        auto sequence = std::make_unique<smdl_sequence>();
        sequence->model = model;
        sequence->index = static_cast<uint32_t>(model->sequences.list.size());
        sequence->name = name;
        smdl_sequence *handle = sequence.get();
        model->sequences.list.push_back(std::move(sequence));
        if (out_sequence)
            *out_sequence = handle;
        return Ok(model);
    });
    return Sequence(result, model);
}

Sequence ModelSequenceApi::findSequence(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_sequence *result = nullptr;
    smdl_sequence **out_sequence = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "sequence name is null or empty");
        for (const auto &sequence: model->sequences.list) {
            if (IEquals(sequence->name, name)) {
                if (out_sequence)
                    *out_sequence = sequence.get();
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "no sequence named '{}'", name);
    });
    return Sequence(result, model);
}

uint32_t ModelSequenceApi::sequenceCount() const { return static_cast<uint32_t>(model()->sequences.list.size()); }

Sequence ModelSequenceApi::sequence(uint32_t index) const {
    smdl_model *model = this->model();
    smdl_sequence *result = nullptr;
    smdl_sequence **out_sequence = &result;
    RunBody(model, [&] {
        if (index >= model->sequences.list.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "sequence {} is out of range ({} sequences)", index,
                        model->sequences.list.size());
        *out_sequence = model->sequences.list[index].get();
        return Ok(model);
    });
    return Sequence(result, model);
}

} // namespace detail
} // namespace smdl

namespace {
    smdl::Sequence View(smdl_sequence *s) { return smdl::Sequence(s, s->model); }
}

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
extern "C" {

SMDL_API smdl_status smdl_model_create_sequence(smdl_model *model, const char *name, smdl_sequence **out_sequence) {
    if (out_sequence)
        *out_sequence = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Sequence sequence = smdl::detail::BorrowModel(model).createSequence(name ? name : "");
        if (out_sequence)
            *out_sequence = sequence.handle();
    });
}

SMDL_API smdl_status smdl_model_find_sequence(const smdl_model *model, const char *name, smdl_sequence **out_sequence) {
    if (out_sequence)
        *out_sequence = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Sequence sequence =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findSequence(name ? name : "");
        if (out_sequence)
            *out_sequence = sequence.handle();
    });
}

SMDL_API uint32_t smdl_model_sequence_count(const smdl_model *model) {
    return model ? smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).sequenceCount() : 0;
}

SMDL_API smdl_status smdl_model_get_sequence(const smdl_model *model, uint32_t index, smdl_sequence **out_sequence) {
    if (out_sequence)
        *out_sequence = nullptr;
    if (!model || !out_sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        *out_sequence = smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).sequence(index).handle();
    });
}

SMDL_API const char *smdl_sequence_get_name(const smdl_sequence *sequence) {
    return sequence ? sequence->name.c_str() : "";
}

// ---- animations and blends ----

SMDL_API smdl_status smdl_sequence_add_animation(smdl_sequence *sequence, const smdl_animation *animation) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!animation)
        return CheckAnimation(*sequence, animation, "blend");
    return CallCpp(sequence->model, [&] {
        View(sequence).addAnimation(smdl::Animation(const_cast<smdl_animation *>(animation), sequence->model));
    });
}

SMDL_API smdl_status smdl_sequence_set_blend_width(smdl_sequence *sequence, uint32_t width) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setBlendWidth(width); });
}

SMDL_API smdl_status smdl_sequence_set_blend_reference(smdl_sequence *sequence, const smdl_animation *animation) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!animation)
        return CheckAnimation(*sequence, animation, "blend reference");
    return CallCpp(sequence->model, [&] {
        View(sequence).setBlendReference(smdl::Animation(const_cast<smdl_animation *>(animation), sequence->model));
    });
}

SMDL_API smdl_status smdl_sequence_set_blend_compare(smdl_sequence *sequence, const smdl_animation *animation) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!animation)
        return CheckAnimation(*sequence, animation, "blend compare");
    return CallCpp(sequence->model, [&] {
        View(sequence).setBlendCompare(smdl::Animation(const_cast<smdl_animation *>(animation), sequence->model));
    });
}

SMDL_API smdl_status smdl_sequence_set_blend_center(smdl_sequence *sequence, const smdl_animation *animation) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!animation)
        return CheckAnimation(*sequence, animation, "blend center");
    return CallCpp(sequence->model, [&] {
        View(sequence).setBlendCenter(smdl::Animation(const_cast<smdl_animation *>(animation), sequence->model));
    });
}

SMDL_API smdl_status smdl_sequence_add_blend(smdl_sequence *sequence, smdl_pose_param_id pose_parameter, float start,
                                             float end) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).addBlend(pose_parameter, start, end); });
}

SMDL_API smdl_status smdl_sequence_add_calc_blend(smdl_sequence *sequence, smdl_pose_param_id pose_parameter,
                                                  smdl_attachment_id attachment, uint32_t control) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).addCalcBlend(pose_parameter, attachment, control); });
}

SMDL_API smdl_status smdl_sequence_set_pose_cycle(smdl_sequence *sequence, smdl_pose_param_id pose_parameter) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setPoseCycle(pose_parameter); });
}

// ---- activity, events, tags ----

SMDL_API smdl_status smdl_sequence_set_activity(smdl_sequence *sequence, const char *name, int weight) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setActivity(name ? name : "", weight); });
}

SMDL_API smdl_status smdl_sequence_add_activity_modifier(smdl_sequence *sequence, const char *name) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).addActivityModifier(name ? name : ""); });
}

SMDL_API smdl_status smdl_sequence_add_event(smdl_sequence *sequence, const char *name, int frame,
                                             const char *options) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).addEvent(name ? name : "", frame, options ? options : ""); });
}

SMDL_API smdl_status smdl_sequence_add_event_cycle(smdl_sequence *sequence, const char *name, float cycle,
                                                   const char *options) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model,
                   [&] { View(sequence).addEventCycle(name ? name : "", cycle, options ? options : ""); });
}

SMDL_API smdl_status smdl_sequence_add_anim_tag(smdl_sequence *sequence, const char *name, float cycle) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).addAnimTag(name ? name : "", cycle); });
}

// ---- timing and flags ----

SMDL_API smdl_status smdl_sequence_set_fade_in(smdl_sequence *sequence, float seconds) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setFadeIn(seconds); });
}

SMDL_API smdl_status smdl_sequence_set_fade_out(smdl_sequence *sequence, float seconds) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setFadeOut(seconds); });
}

SMDL_API smdl_status smdl_sequence_set_exit_phase(smdl_sequence *sequence, float phase) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setExitPhase(phase); });
}

SMDL_API smdl_status smdl_sequence_set_flags(smdl_sequence *sequence, uint32_t flags, int enabled) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setFlags(flags, enabled != 0); });
}

SMDL_API smdl_status smdl_sequence_set_root_driver(smdl_sequence *sequence, smdl_bone_id bone) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setRootDriver(bone); });
}

// ---- transition graph ----

SMDL_API smdl_status smdl_sequence_set_node(smdl_sequence *sequence, const char *node) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model, [&] { View(sequence).setNode(node ? node : ""); });
}

SMDL_API smdl_status smdl_sequence_set_transition(smdl_sequence *sequence, const char *from, const char *to,
                                                  int reverse) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model,
                   [&] { View(sequence).setTransition(from ? from : "", to ? to : "", reverse != 0); });
}

// ---- layers and IK locks ----

SMDL_API smdl_status smdl_sequence_add_layer(smdl_sequence *sequence, const smdl_sequence *layer, int local) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!layer)
        return CheckSequenceRef(*sequence, layer, "layer");
    return CallCpp(sequence->model, [&] {
        View(sequence).addLayer(smdl::Sequence(const_cast<smdl_sequence *>(layer), sequence->model), local != 0);
    });
}

SMDL_API smdl_status smdl_sequence_add_blend_layer(smdl_sequence *sequence, const smdl_sequence *layer, float start,
                                                   float peak, float tail, float end, uint32_t flags,
                                                   smdl_pose_param_id pose_parameter) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!layer)
        return CheckSequenceRef(*sequence, layer, "blend layer");
    return CallCpp(sequence->model, [&] {
        View(sequence).addBlendLayer(smdl::Sequence(const_cast<smdl_sequence *>(layer), sequence->model), start, peak,
                                     tail, end, flags, pose_parameter);
    });
}

SMDL_API smdl_status smdl_sequence_add_ik_lock(smdl_sequence *sequence, smdl_ik_chain_id chain, float position_weight,
                                               float local_rotation_weight) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(sequence->model,
                   [&] { View(sequence).addIkLock(chain, position_weight, local_rotation_weight); });
}

// ---- keyvalues ----

SMDL_API smdl_status smdl_sequence_add_keyvalues(smdl_sequence *sequence, const char *text) {
    if (!sequence)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!text)
        return Fail(sequence->model, SMDL_ERROR_INVALID_ARGUMENT, "sequence '{}': keyvalues text is null",
                    sequence->name);
    return CallCpp(sequence->model, [&] { View(sequence).addKeyValues(text); });
}
} // extern "C"

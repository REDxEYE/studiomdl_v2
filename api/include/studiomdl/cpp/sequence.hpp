// studiomdl C++ wrapper: smdl_sequence.h
#pragma once

#include "common.hpp"
#include "animation.hpp"
#include "../c/smdl_sequence.h"

namespace smdl {

// Sequence flags (smdl_sequence_set_flags): combine with |
namespace SequenceFlag {
    inline constexpr uint32_t Snap = SMDL_SEQUENCE_SNAP;
    inline constexpr uint32_t Delta = SMDL_SEQUENCE_DELTA;
    inline constexpr uint32_t PreDelta = SMDL_SEQUENCE_PRE_DELTA;
    inline constexpr uint32_t WorldSpace = SMDL_SEQUENCE_WORLD_SPACE;
    inline constexpr uint32_t WorldRelative = SMDL_SEQUENCE_WORLD_RELATIVE;
    inline constexpr uint32_t Post = SMDL_SEQUENCE_POST;
    inline constexpr uint32_t Autoplay = SMDL_SEQUENCE_AUTOPLAY;
    inline constexpr uint32_t Realtime = SMDL_SEQUENCE_REALTIME;
    inline constexpr uint32_t Hidden = SMDL_SEQUENCE_HIDDEN;
}

// blendlayer options: combine with |
namespace LayerFlag {
    inline constexpr uint32_t XFade = SMDL_LAYER_XFADE;
    inline constexpr uint32_t Spline = SMDL_LAYER_SPLINE;
    inline constexpr uint32_t NoBlend = SMDL_LAYER_NO_BLEND;
    inline constexpr uint32_t Local = SMDL_LAYER_LOCAL;
    inline constexpr uint32_t Pose = SMDL_LAYER_POSE;
}

// A sequence: one animation or a blend grid, with activity, events, layers, ... Non-owning view, valid as long as its
// model lives. The setters return *this so calls can be chained. See smdl_sequence.h.
class Sequence {
public:
    Sequence(smdl_sequence *sequence, smdl_model *model) : m_Sequence(sequence), m_Model(model) {}

    smdl_sequence *handle() const { return m_Sequence; }
    smdl_model *model() const { return m_Model; }

    std::string_view name() const;

    // ---- animations and blends ----

    // The next animation of the sequence (blend animations in the order of the QC list)
    Sequence &addAnimation(const Animation &animation);
    // Several animations at once, e.g. the animations of a blend grid
    Sequence &addAnimations(std::span<const Animation> animations) {
        for (const Animation &animation: animations)
            addAnimation(animation);
        return *this;
    }
    Sequence &setBlendWidth(uint32_t width);
    Sequence &setBlendReference(const Animation &animation);
    Sequence &setBlendCompare(const Animation &animation);
    Sequence &setBlendCenter(const Animation &animation);
    // blend <pose parameter> <start> <end>: the first call is the first axis, the second call the second
    Sequence &addBlend(PoseParamId poseParameter, float start, float end);
    // calcblend: control = one Motion:: flag
    Sequence &addCalcBlend(PoseParamId poseParameter, AttachmentId attachment, uint32_t control);
    Sequence &setPoseCycle(PoseParamId poseParameter);

    // ---- activity, events, tags ----

    Sequence &setActivity(std::string_view name, int weight);
    Sequence &addActivityModifier(std::string_view name);
    // frame of the first animation; options may be empty
    Sequence &addEvent(std::string_view name, int frame, std::string_view options = {});
    // cycle 0..1 over the first animation (converted to the nearest frame when the model is compiled)
    Sequence &addEventCycle(std::string_view name, float cycle, std::string_view options = {});
    Sequence &addAnimTag(std::string_view name, float cycle);

    // ---- timing and flags ----

    Sequence &setFadeIn(float seconds);
    Sequence &setFadeOut(float seconds);
    Sequence &setExitPhase(float phase);
    // flags: a combination of SequenceFlag::
    Sequence &setFlags(uint32_t flags, bool enabled = true);
    Sequence &setRootDriver(BoneId bone);

    // ---- transition graph ----

    Sequence &setNode(std::string_view node);
    Sequence &setTransition(std::string_view from, std::string_view to, bool reverse = false);

    // ---- layers and IK locks ----

    Sequence &addLayer(const Sequence &layer, bool local = false);
    // flags: a combination of LayerFlag::; poseParameter is used with LayerFlag::Pose
    Sequence &addBlendLayer(const Sequence &layer, float start, float peak, float tail, float end, uint32_t flags = 0,
                            PoseParamId poseParameter = 0);
    Sequence &addIkLock(IkChainId chain, float positionWeight, float localRotationWeight);

    // ---- keyvalues ----

    // The text between the braces of a QC keyvalues block
    Sequence &addKeyValues(std::string_view text);

private:
    smdl_sequence *m_Sequence;
    smdl_model *m_Model;
};

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
    class ModelSequenceApi {
    public:
        // name: unique among sequences and animations
        Sequence createSequence(std::string_view name);

        // Throws smdl::Error (SMDL_ERROR_NOT_FOUND) for an unknown name
        Sequence findSequence(std::string_view name) const;

        uint32_t sequenceCount() const;

        Sequence sequence(uint32_t index) const;

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

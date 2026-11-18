// Description of sequences (smdl_sequence.h); defines struct smdl_sequence. Owned by the Anim part.
#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "desc_shared.hpp"
#include "studiomdl/c/smdl_sequence.h"

struct smdl_sequence {
    struct Blend {
        bool calc = false; // calcblend (attachment + control) instead of blend (start + end)
        smdl_pose_param_id pose = 0;
        float start = 0.0f;
        float end = 0.0f;
        smdl_attachment_id attachment = 0;
        uint32_t control = 0;
    };
    struct Event {
        std::string name;
        bool byCycle = false;
        int frame = 0;
        float cycle = 0.0f;
        std::string options;
    };
    struct AnimTag {
        std::string name;
        float cycle = 0.0f;
    };
    struct Node {
        bool isNode = true; // node <name> (from = to) / transition
        std::string from;
        std::string to;
        bool reverse = false;
    };
    struct Layer {
        const smdl_sequence *layer = nullptr;
        bool isBlend = false; // blendlayer instead of addlayer
        bool local = false;
        float start = 0.0f, peak = 0.0f, tail = 0.0f, end = 0.0f;
        uint32_t flags = 0; // SMDL_LAYER_* (blendlayer)
        smdl_pose_param_id pose = 0;
    };
    struct IkLock {
        smdl_ik_chain_id chain = 0;
        float positionWeight = 0.0f;
        float localQWeight = 0.0f;
    };

    smdl_model *model = nullptr;
    uint32_t index = 0; // creation order
    std::string name;

    std::vector<const smdl_animation *> animations;
    uint32_t blendWidth = 0; // 0 = unset
    const smdl_animation *blendReference = nullptr;
    const smdl_animation *blendCompare = nullptr;
    const smdl_animation *blendCenter = nullptr;
    std::vector<Blend> blends; // at most 2
    std::optional<smdl_pose_param_id> poseCycle;

    std::optional<std::string> activity;
    int activityWeight = 0;
    std::vector<std::string> activityModifiers;
    std::vector<Event> events;
    std::vector<AnimTag> animTags;

    std::optional<float> fadeIn;
    std::optional<float> fadeOut;
    std::optional<float> exitPhase;
    uint32_t flags = 0; // SMDL_SEQUENCE_*
    std::optional<smdl_bone_id> rootDriver;
    std::optional<Node> node;

    std::vector<Layer> layers;
    std::vector<IkLock> ikLocks;
    std::string keyValues; // formatted (FormatKeyValuesText), appended per call
};

struct SequencesDesc {
    std::deque<std::unique_ptr<smdl_sequence>> list; // creation order = apply order
};

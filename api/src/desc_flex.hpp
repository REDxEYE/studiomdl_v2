// Description of flex controllers, flexes, rules, eyeballs, eyelids, mouths (smdl_flex.h). Owned by the Flex part.
//
// The model-wide lists are applied by ApplyFlex, the per-mesh items by ApplyMeshFlex in call order (apply_flex.cpp).
// `flexNames` lists every flex name the calls defined so far in order of definition (smdl_flex_id = index): the
// call-time checks of rules use it; the compile resolves names against StudioMdlState again.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "desc_shared.hpp"

struct FlexControllerDesc {
    std::string group; // the controller's type
    std::string name;
    float min = 0.0f;
    float max = 1.0f;
};

struct FlexRuleDesc {
    std::string flex;
    std::string expression;
};

struct MouthDesc {
    uint32_t index = 0;
    std::string flex;
    smdl_bone_id bone = SMDL_NO_BONE;
    Vector forward{0, 0, 0};
};

struct BoneFlexDriverDesc {
    smdl_bone_id bone = SMDL_NO_BONE;
    int component = 0; // smdl_bone_flex_component
    smdl_flex_controller_id controller = 0;
    float min = 0.0f;
    float max = 0.0f;
};

struct EyeballDesc {
    std::string name;
    smdl_bone_id bone = SMDL_NO_BONE;
    Vector position{0, 0, 0};
    std::string material;
    float diameter = 0.0f;
    float zAngleDegrees = 0.0f;
    float pupilScale = 1.0f;
};

struct FlexKeyDesc {
    std::string name;
    bool pair = false;
    float split = 0.0f;
    uint32_t shape = SMDL_NO_ID; // SMDL_NO_ID = the base shape
    float position = 1.0f;
    float decay = 1.0f;
};

struct EyelidDesc {
    std::string name;
    int kind = 0; // smdl_eyelid_kind
    std::array<bool, 3> set{};
    std::array<uint32_t, 3> shape{SMDL_NO_ID, SMDL_NO_ID, SMDL_NO_ID};
    std::array<float, 3> target{};
    float split = 0.0f;
    uint32_t eyeball = SMDL_NO_ID; // SMDL_NO_ID = every eyeball
};

struct DmxEyelidDesc {
    int kind = 0; // smdl_eyelid_kind
    uint32_t rightEyeball = 0;
    uint32_t leftEyeball = 0;
    std::array<bool, 3> set{};
    std::array<uint32_t, 3> shape{SMDL_NO_ID, SMDL_NO_ID, SMDL_NO_ID};
    std::array<float, 3> target{};
};

struct SphereNormalsDesc {
    std::string material;
    Vector position{0, 0, 0};
};

// One call of the mesh's options, in call order. `index` addresses the vector of that kind in MeshFlexDesc
// (the default flex and noautodmxrules have no payload / their own field).
struct MeshFlexItem {
    enum class Kind { Eyeball, Flex, DefaultFlex, Eyelid, DmxEyelid, SphereNormals, NoAutoDmxRules };
    Kind kind;
    uint32_t index = 0;
};

struct MeshFlexDesc {
    std::vector<MeshFlexItem> items;
    std::vector<EyeballDesc> eyeballs;
    std::vector<FlexKeyDesc> flexKeys;
    std::vector<EyelidDesc> eyelids;
    std::vector<DmxEyelidDesc> dmxEyelids;
    std::vector<SphereNormalsDesc> sphereNormals;
    uint32_t defaultShape = SMDL_NO_ID;
    bool hasDefaultFlex = false;
    bool noAutoDmxRules = false;
};

struct FlexDesc {
    std::vector<FlexControllerDesc> controllers;
    std::vector<std::string> flexNames;
    std::vector<std::string> localVars;
    std::vector<MouthDesc> mouths;
    std::vector<FlexRuleDesc> rules;
    std::vector<BoneFlexDriverDesc> boneDrivers;
    std::unordered_map<const smdl_mesh *, MeshFlexDesc> meshes;
    bool hasDefaultFlex = false; // a mesh set the default flex (once per model)

    // Case-insensitive lookups (the QC's comparison); -1 = not found
    int findFlex(std::string_view name) const;
    int findController(std::string_view name) const;
    // Defines the name when unknown; returns its id
    uint32_t defineFlex(std::string_view name);
};

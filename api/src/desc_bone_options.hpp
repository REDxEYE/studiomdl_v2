// Description of per-bone options and bone processing options (smdl_skeleton.h). Owned by the Core part.
// Bones are referred to by smdl_bone_id (index into SkeletonDesc::bones); the apply step resolves them to names
// because studiomdl's bone option tables are name based. Rotations are RadianEuler like BoneDesc.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "desc_shared.hpp"
#include "studiomdl/c/smdl_skeleton.h"

// $definebone: a bone with a fixed pose that is always stored (see smdl_model_define_bone)
struct DefineBoneDesc {
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_id parent = SMDL_NO_BONE;
    Vector position{0, 0, 0};
    RadianEuler rotation{0, 0, 0};
    smdl_define_bone_lock lock = SMDL_DEFINE_BONE_LOCK_DEFAULT;

    struct Realign {
        Vector position{0, 0, 0};
        RadianEuler rotation{0, 0, 0};
    };
    std::optional<Realign> realign; // the optional "realign pos / angles" of $definebone
};

// $forcerealign
struct ForcedRealignDesc {
    smdl_bone_id bone = SMDL_NO_BONE;
    RadianEuler rotation{0, 0, 0};
};

// $controller
struct BoneControllerDesc {
    uint32_t input = 0;
    smdl_bone_id bone = SMDL_NO_BONE;
    smdl_bone_controller_type type = SMDL_BONE_CONTROLLER_X;
    float start = 0;
    float end = 0;
};

struct BoneOptionsDesc {
    std::optional<smdl_bone_id> rootBone; // $root

    // bone processing options
    bool realignBones = false;            // $realignbones
    bool lockBoneLengths = false;         // $lockbonelengths
    bool unlockDefineBones = false;       // $unlockdefinebones
    bool collapseBones = false;           // $collapsebones
    bool collapseBonesAggressive = false; // $collapsebonesaggressive

    // lists in the order the calls were made (the order is part of the output: bone table, controller indices,
    // composition of several realigns of one bone)
    std::vector<DefineBoneDesc> defineBones;
    std::vector<ForcedRealignDesc> forcedRealigns;
    std::vector<BoneControllerDesc> controllers;

    // per bone options, one entry per bone (ordered by bone id)
    std::map<smdl_bone_id, smdl_screen_align_mode> screenAlign;   // $screenalign
    std::set<smdl_bone_id> worldAlign;                            // $worldalign
    std::set<smdl_bone_id> boneMerge;                             // $bonemerge
    std::set<smdl_bone_id> boneAlwaysSetup;                       // $bonealwayssetup
    std::set<smdl_bone_id> alwaysCollapse;                        // $alwayscollapse
    std::map<smdl_bone_id, std::vector<std::string>> limitRotation; // $limitrotation: sequence names (may be empty)
    std::map<smdl_bone_id, uint32_t> saveFrame;                   // $bonesaveframe: SMDL_BONE_SAVE_* flags
};

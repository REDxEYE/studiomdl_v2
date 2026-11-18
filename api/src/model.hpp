// struct smdl_model: the whole description of a model built through the API. Plain data: no StudioMdlState, no
// globals, no lock. Every part keeps its data in its own member (desc_<part>.hpp).
#pragma once

#include <deque>
#include <memory>
#include <string>

#include "desc_shared.hpp"
#include "desc_globals.hpp"
#include "desc_bone_options.hpp"
#include "desc_body.hpp"
#include "desc_flex.hpp"
#include "desc_rig.hpp"
#include "desc_animation.hpp"
#include "desc_sequence.hpp"
#include "desc_lod.hpp"
#include "desc_collision.hpp"
#include "desc_bone_dynamics.hpp"

struct smdl_model {
    std::string name;                 // path under models/ without extension, '/' separators
    mutable std::string lastError;    // smdl_model_last_error

    GlobalsDesc globals;
    SkeletonDesc skeleton;
    BoneOptionsDesc boneOptions;
    BodyDesc body;
    FlexDesc flex;
    RigDesc rig;
    AnimationsDesc animations;
    SequencesDesc sequences;
    LodsDesc lods;
    CollisionDesc collision;
    BoneDynamicsDesc boneDynamics;

    // Owners of the shared handle objects (stable addresses). Parts create them through NewGeometry/NewMesh/
    // NewBodyGroup (api_util.hpp); order of creation is kept.
    std::deque<std::unique_ptr<smdl_geometry>> geometries;
    std::deque<std::unique_ptr<smdl_mesh>> meshes;
    std::deque<std::unique_ptr<smdl_bodygroup>> bodygroups; // in model order ($body/$bodygroup order)
};

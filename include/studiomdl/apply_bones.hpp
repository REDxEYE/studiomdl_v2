// Apply functions for the bones QC commands, shared by the QC parsers (studiomdl_commands.cpp) and the builder API
// front end (api/src/apply_*.cpp): the parser turns tokens into typed parameters, these apply them to StudioMdlState.
//
// All of them only fill the tables SimplifyModel reads later (ctx.bones.*, ctx.options.*): bones are referred to by
// name, they are looked up (findGlobalBone) when the model is simplified, so a bone that does not exist is reported
// there, exactly as for the QC commands. Names longer than the table's storage are an error (MdlError).
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "studiomdl/studiomdl_global_state.hpp"

// $root <bone>: the bone whose motion is the root motion (ctx.script.rootname)
void ApplyRootBone(StudioMdlState &ctx, std::string_view bone);

// $controller <input> <bone> <axis> <start> <end>. inputField: 0-3 user controller, 4 mouth. type: one of
// X Y Z XR YR ZR (the others are rejected when the model is written). A rotation controller whose start and end are
// the same angle modulo 360 gets StudioMotionFlags::RLoop.
void ApplyBoneController(StudioMdlState &ctx, int inputField, std::string_view bone, StudioMotionFlags type,
                         float start, float end);

// $screenalign <bone> [sphere|cylinder]: flags is BoneFlags::ScreenAlignSphere or ScreenAlignCylinder
void ApplyScreenAlign(StudioMdlState &ctx, std::string_view bone, BoneFlags flags);

// $worldalign <bone>
void ApplyWorldAlign(StudioMdlState &ctx, std::string_view bone);

// $bonemerge <bone>
void ApplyBoneMerge(StudioMdlState &ctx, std::string_view bone);

// $bonealwayssetup <bone>
void ApplyBoneAlwaysSetup(StudioMdlState &ctx, std::string_view bone);

// $limitrotation <bone> [<sequence>...]
void ApplyLimitRotation(StudioMdlState &ctx, std::string_view bone, std::span<const std::string> sequences);

// $bonesaveframe <bone> [position] [rotation] [rotation64]
void ApplyBoneSaveFrame(StudioMdlState &ctx, std::string_view bone, bool position, bool rotation, bool rotation64);

// $realignbones, $lockbonelengths, $unlockdefinebones
void ApplyRealignBones(StudioMdlState &ctx);
void ApplyLockBoneLengths(StudioMdlState &ctx);
void ApplyUnlockDefineBones(StudioMdlState &ctx);

// $forcerealign <bone> rotate <x> <y> <z>: rotation is the RadianEuler the compiler concatenates to the bone's
// pose (the QC values are degrees)
void ApplyForceRealign(StudioMdlState &ctx, std::string_view bone, const RadianEuler &rotation);

// $collapsebones, $collapsebonesaggressive (also turns collapsing on), $alwayscollapse <bone> (also turns it on)
void ApplyCollapseBones(StudioMdlState &ctx);
void ApplyCollapseBonesAggressive(StudioMdlState &ctx);
void ApplyAlwaysCollapse(StudioMdlState &ctx, std::string_view bone);

// $definebone <name> <parent> [locked|unlocked] <pos> <angles> [<realign pos> <realign angles>]
struct DefineBoneParams {
    std::string_view name;
    std::string_view parent;       // empty = no parent
    std::optional<bool> unlocked;  // unset: the default (ctx.options.defineBonesLockedByDefault, as of this call)
    matrix3x4_t rawLocal;          // the bone's pose relative to its parent
    std::optional<matrix3x4_t> realign; // the realigned pose: marks the bone pre-aligned
};

void ApplyDefineBone(StudioMdlState &ctx, const DefineBoneParams &params);

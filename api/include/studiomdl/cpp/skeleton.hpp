// studiomdl C++ wrapper: smdl_skeleton.h
#pragma once

#include <vector>

#include "common.hpp"
#include "../c/smdl_skeleton.h"

namespace smdl {

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
        class ModelSkeletonApi {
    public:
        // Adds a bone with a quaternion rest rotation (x, y, z, w); parent = NoBone for a root.
        BoneId addBone(std::string_view name, BoneId parent, const Vec3 &position, const Quat &rotation);

        // Same with the rest rotation as SMD euler angles (radians, x, y, z)
        BoneId addBoneEuler(std::string_view name, BoneId parent, const Vec3 &position, const Vec3 &euler);

        // Throws smdl::Error (SMDL_ERROR_NOT_FOUND) for an unknown name
        BoneId findBone(std::string_view name) const;

        uint32_t boneCount() const;

        // ---- Bone options and bone processing options (see smdl_skeleton.h for the semantics) ----

        // $root; NoBone clears it
        void setRootBone(BoneId bone);

        // $definebone: the pose is local to `parent` (NoBone or a bone defined before `bone`)
        void defineBone(BoneId bone, BoneId parent, const Vec3 &position, const Quat &rotation);
        void defineBoneEuler(BoneId bone, BoneId parent, const Vec3 &position, const Vec3 &euler);
        void setDefineBoneLock(BoneId bone, smdl_define_bone_lock lock);
        void setDefineBoneRealign(BoneId bone, const Vec3 &position, const Quat &rotation);
        void setDefineBoneRealignEuler(BoneId bone, const Vec3 &position, const Vec3 &euler);
        void setUnlockDefineBones(bool enabled);
        void setLockBoneLengths(bool enabled);
        void setRealignBones(bool enabled);
        void addForceRealign(BoneId bone, const Quat &rotation);
        void addForceRealignEuler(BoneId bone, const Vec3 &euler);

        // $collapsebones, $collapsebonesaggressive, $alwayscollapse
        void setCollapseBones(bool enabled);
        void setCollapseBonesAggressive(bool enabled);
        void setAlwaysCollapse(BoneId bone, bool enabled);

        // $controller: returns the controller's index
        uint32_t addBoneController(uint32_t input, BoneId bone, smdl_bone_controller_type type, float start,
                                   float end);

        // $screenalign, $worldalign, $bonemerge, $bonealwayssetup
        void setScreenAlign(BoneId bone, smdl_screen_align_mode mode);
        void setWorldAlign(BoneId bone, bool enabled);
        void setBoneMerge(BoneId bone, bool enabled);
        void setBoneAlwaysSetup(BoneId bone, bool enabled);

        // $limitrotation: the sequence names may be empty
        void setLimitRotation(BoneId bone, std::span<const std::string> sequences = {});

        // $bonesaveframe: flags = SMDL_BONE_SAVE_POSITION | SMDL_BONE_SAVE_ROTATION | SMDL_BONE_SAVE_ROTATION64
        void setBoneSaveFrame(BoneId bone, uint32_t flags);

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

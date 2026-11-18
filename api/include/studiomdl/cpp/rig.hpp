// studiomdl C++ wrapper: smdl_rig.h
#pragma once

#include "common.hpp"
#include "../c/smdl_rig.h"

namespace smdl {

inline constexpr WeightListId DefaultWeightList = SMDL_DEFAULT_WEIGHT_LIST;

using PoseLoopMode = smdl_pose_loop_mode;

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle. See smdl_rig.h for the semantics.
        class ModelRigApi {
    public:
        // ---- attachments ($attachment / $redefineattachment) ----

        AttachmentId addAttachment(std::string_view name, BoneId bone, const Vec3 &position);
        // Starts a new revision of the attachment; the orientation setters below then describe that revision
        void redefineAttachment(AttachmentId attachment, BoneId bone, const Vec3 &position);
        // Throws smdl::Error (SMDL_ERROR_NOT_FOUND) for an unknown name
        AttachmentId findAttachment(std::string_view name) const;
        uint32_t attachmentCount() const;

        // Orientation steps, each replaces the one before it: angles in degrees (QC rotate), euler in radians
        void setAttachmentAbsolute(AttachmentId attachment);
        void setAttachmentAngles(AttachmentId attachment, const Vec3 &angles);
        void setAttachmentEuler(AttachmentId attachment, const Vec3 &euler);
        void setAttachmentQuaternion(AttachmentId attachment, const Quat &rotation);
        void setAttachmentAxes(AttachmentId attachment, const Vec3 &xAxis, const Vec3 &zAxis);
        void setAttachmentRigid(AttachmentId attachment, bool enabled = true);
        void setAttachmentWorldAlign(AttachmentId attachment, bool enabled = true);

        // ---- hitbox sets and hit groups ($hboxset / $hbox / $hgroup) ----

        HitboxSetId addHitboxSet(std::string_view name);
        HitboxSetId findHitboxSet(std::string_view name) const;
        uint32_t hitboxSetCount() const;
        // Returns the index of the box inside its set
        uint32_t addHitbox(HitboxSetId set, int group, BoneId bone, const Vec3 &mins, const Vec3 &maxs);
        void setHitboxAngles(HitboxSetId set, uint32_t hitbox, const Vec3 &angles);
        void setHitboxCapsuleRadius(HitboxSetId set, uint32_t hitbox, float radius);
        void setHitboxName(HitboxSetId set, uint32_t hitbox, std::string_view name);
        void addHitGroup(int group, BoneId bone);

        // ---- pose parameters ($poseparameter) ----

        PoseParamId addPoseParameter(std::string_view name, float min, float max);
        void setPoseParameterWrap(PoseParamId pose);
        void setPoseParameterLoop(PoseParamId pose, float range);
        void clearPoseParameterLoop(PoseParamId pose);
        PoseParamId findPoseParameter(std::string_view name) const;
        uint32_t poseParameterCount() const;

        // ---- IK chains and autoplay locks ($ikchain / $ikautoplaylock) ----

        IkChainId addIkChain(std::string_view name, BoneId endBone);
        // motion: one of the Motion:: flags (animation.hpp)
        void setIkChainAxis(IkChainId chain, uint32_t motion, float value);
        void setIkChainHeight(IkChainId chain, float height);
        void setIkChainFloor(IkChainId chain, float floor);
        // The QC's pad is a diameter
        void setIkChainPad(IkChainId chain, float diameter);
        void setIkChainKnee(IkChainId chain, const Vec3 &direction);
        void setIkChainCenter(IkChainId chain, const Vec3 &center);
        IkChainId findIkChain(std::string_view name) const;
        uint32_t ikChainCount() const;
        // Returns the index of the lock
        uint32_t addIkAutoplayLock(IkChainId chain, float positionWeight, float localRotationWeight);

        // ---- weight lists ($weightlist / $defaultweightlist; list DefaultWeightList is the default list) ----

        WeightListId addWeightList(std::string_view name);
        void setWeightListBone(WeightListId list, BoneId bone, float weight, float positionWeight);
        // The position weight equals the weight, like a QC entry without posweight
        void setWeightListBone(WeightListId list, BoneId bone, float weight) {
            setWeightListBone(list, bone, weight, weight);
        }
        WeightListId findWeightList(std::string_view name) const;
        uint32_t weightListCount() const;

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

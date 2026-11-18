// smdl_rig.h: attachments, hitbox sets, hit groups, pose parameters, IK chains and locks, weight lists
#include "studiomdl/c/smdl_rig.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>

#include "anim_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    constexpr size_t kMaxHitboxSetNameLength = 63; // MAXSTUDIOHITBOXSETNAME
    constexpr size_t kMaxPoseParameters = 24;      // MAXSTUDIOPOSEPARAM
    constexpr size_t kMaxIkChains = 16;
    constexpr size_t kMaxIkLocks = 16;
    constexpr size_t kMaxWeightLists = 128;        // MAXWEIGHTLISTS, the default list included
    constexpr size_t kMaxWeightsPerList = 256;     // MAXSTUDIOBONES
    constexpr size_t kMaxAttachments = MAXSTUDIOSRCBONES;
    constexpr size_t kMaxHitGroups = MAXSTUDIOSRCBONES;
    constexpr size_t kMaxHitboxes = MAXSTUDIOSRCBONES;

    template<class T>
    bool NameInUse(const std::vector<T> &objects, std::string_view name) {
        return std::ranges::any_of(objects, [&](const T &object) { return IEquals(object.name, name); });
    }

    template<class T>
    smdl_status FindByName(const smdl_model *model, const std::vector<T> &objects, const char *name, uint32_t first,
                           const char *kind, uint32_t *out_id) {
        if (out_id)
            *out_id = SMDL_NO_ID;
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{} name is null or empty", kind);
        for (size_t i = first; i < objects.size(); ++i) {
            if (IEquals(objects[i].name, name)) {
                if (out_id)
                    *out_id = static_cast<uint32_t>(i);
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "no {} named '{}'", kind, name);
    }

    // Attachment of `id`, or nullptr after failing
    AttachmentDesc *GetAttachment(smdl_model *model, smdl_attachment_id id, smdl_status &status) {
        if (id >= model->rig.attachments.size()) {
            status = Fail(model, SMDL_ERROR_OUT_OF_RANGE, "attachment {} is out of range ({} attachments)", id,
                          model->rig.attachments.size());
            return nullptr;
        }
        return &model->rig.attachments[id];
    }

    AttachmentRevisionDesc &LatestRevision(AttachmentDesc &attachment) { return attachment.revisions.back(); }

    HitboxDesc *GetHitbox(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox, smdl_status &status) {
        if (set >= model->rig.hitboxSets.size()) {
            status = Fail(model, SMDL_ERROR_OUT_OF_RANGE, "hitbox set {} is out of range ({} sets)", set,
                          model->rig.hitboxSets.size());
            return nullptr;
        }
        HitboxSetDesc &hitboxSet = model->rig.hitboxSets[set];
        if (hitbox >= hitboxSet.hitboxes.size()) {
            status = Fail(model, SMDL_ERROR_OUT_OF_RANGE, "hitbox {} is out of range (set '{}' has {} hitboxes)",
                          hitbox, hitboxSet.name, hitboxSet.hitboxes.size());
            return nullptr;
        }
        return &hitboxSet.hitboxes[hitbox];
    }

    IkChainDesc *GetChain(smdl_model *model, smdl_ik_chain_id id, smdl_status &status) {
        if (id >= model->rig.ikChains.size()) {
            status = Fail(model, SMDL_ERROR_OUT_OF_RANGE, "IK chain {} is out of range ({} chains)", id,
                          model->rig.ikChains.size());
            return nullptr;
        }
        return &model->rig.ikChains[id];
    }

    // Attachment orientation step: the body shared by the setters below
    smdl_status AddAttachmentOrientation(smdl_model *model, smdl_attachment_id id, AttachmentRevisionDesc::Step step,
                                         const float *values, size_t valueCount) {
        smdl_status status = SMDL_OK;
        AttachmentDesc *attachment = GetAttachment(model, id, status);
        if (!attachment)
            return status;
        if (valueCount && (!values || !AllFinite(values, valueCount)))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': rotation is null or not finite",
                        attachment->name);
        LatestRevision(*attachment).steps.push_back(step);
        return Ok(model);
    }

    smdl_status SetPoseParameterLoop(smdl_model *model, smdl_pose_param_id pose_parameter, smdl_pose_loop_mode mode,
                                     float range) {
        if (pose_parameter >= model->rig.poseParameters.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "pose parameter {} is out of range ({} pose parameters)",
                        pose_parameter, model->rig.poseParameters.size());
        if (mode != SMDL_POSE_LOOP_NONE && mode != SMDL_POSE_LOOP_WRAP && mode != SMDL_POSE_LOOP_RANGE)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "pose parameter loop mode {} is not valid",
                        static_cast<int>(mode));
        if (mode == SMDL_POSE_LOOP_RANGE && !std::isfinite(range))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "pose parameter loop range is not finite");
        PoseParameterDesc &pose = model->rig.poseParameters[pose_parameter];
        pose.loopMode = mode;
        pose.loopRange = mode == SMDL_POSE_LOOP_RANGE ? range : 0.0f;
        return Ok(model);
    }

    // The C entry points take pointers the C++ methods take by reference. For a null pointer these reproduce the
    // checks the old bodies ran before reaching the null check, then the null failure itself.
    smdl_status FailNullAttachmentValue(smdl_model *model, smdl_attachment_id id, const char *what) {
        smdl_status status = SMDL_OK;
        AttachmentDesc *attachment = GetAttachment(model, id, status);
        if (!attachment)
            return status;
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': {}", attachment->name, what);
    }

    smdl_status FailNullAttachmentPosition(smdl_model *model, const char *name, smdl_bone_id bone) {
        if (std::string bad = BadObjectName(name, "attachment", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (smdl_status status = CheckBone(model, bone, "attachment", name); status != SMDL_OK)
            return status;
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': position is null or not finite", name);
    }

    smdl_status FailNullRedefinePosition(smdl_model *model, smdl_attachment_id id, smdl_bone_id bone) {
        smdl_status status = SMDL_OK;
        AttachmentDesc *attachment = GetAttachment(model, id, status);
        if (!attachment)
            return status;
        if (status = CheckBone(model, bone, "attachment", attachment->name); status != SMDL_OK)
            return status;
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': position is null or not finite",
                    attachment->name);
    }

    smdl_status FailNullHitboxValue(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox, const char *message) {
        smdl_status status = SMDL_OK;
        if (!GetHitbox(model, set, hitbox, status))
            return status;
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", message);
    }

    smdl_status FailNullHitboxBox(smdl_model *model, smdl_hitbox_set_id set, smdl_bone_id bone) {
        if (set >= model->rig.hitboxSets.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "hitbox set {} is out of range ({} sets)", set,
                        model->rig.hitboxSets.size());
        HitboxSetDesc &hitboxSet = model->rig.hitboxSets[set];
        if (smdl_status status = CheckBone(model, bone, "hitbox set", hitboxSet.name); status != SMDL_OK)
            return status;
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "hitbox set '{}': box corners are null or not finite",
                    hitboxSet.name);
    }

    smdl_status FailNullChainValue(smdl_model *model, smdl_ik_chain_id chain, const char *what) {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': {}", desc->name, what);
    }
}

namespace smdl {
namespace detail {

// ---- attachments ----

AttachmentId ModelRigApi::addAttachment(std::string_view nameView, BoneId bone, const Vec3 &position) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    const float *position_c = position.data();
    smdl_attachment_id result = SMDL_NO_ID;
    smdl_attachment_id *out_id = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "attachment", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (smdl_status status = CheckBone(model, bone, "attachment", name); status != SMDL_OK)
            return status;
        if (!position_c || !AllFinite(position_c, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': position is null or not finite", name);
        if (model->rig.attachments.size() >= kMaxAttachments)
            return Fail(model, SMDL_ERROR_LIMIT, "attachment '{}': a model has at most {} attachments", name,
                        kMaxAttachments);
        if (NameInUse(model->rig.attachments, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "attachment '{}' already exists", name);

        AttachmentDesc attachment;
        attachment.name = name;
        AttachmentRevisionDesc &revision = attachment.revisions.emplace_back();
        revision.bone = bone;
        revision.position = ToVector(position_c);
        model->rig.attachments.push_back(std::move(attachment));
        if (out_id)
            *out_id = static_cast<smdl_attachment_id>(model->rig.attachments.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelRigApi::redefineAttachment(AttachmentId id, BoneId bone, const Vec3 &position) {
    smdl_model *model = this->model();
    const float *position_c = position.data();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        AttachmentDesc *attachment = GetAttachment(model, id, status);
        if (!attachment)
            return status;
        if (status = CheckBone(model, bone, "attachment", attachment->name); status != SMDL_OK)
            return status;
        if (!position_c || !AllFinite(position_c, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': position is null or not finite",
                        attachment->name);
        AttachmentRevisionDesc &revision = attachment->revisions.emplace_back();
        revision.bone = bone;
        revision.position = ToVector(position_c);
        return Ok(model);
    });
}

AttachmentId ModelRigApi::findAttachment(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_attachment_id result = SMDL_NO_ID;
    smdl_attachment_id *out_id = &result;
    RunBody(model, [&] { return FindByName(model, model->rig.attachments, name, 0, "attachment", out_id); });
    return result;
}

uint32_t ModelRigApi::attachmentCount() const { return static_cast<uint32_t>(model()->rig.attachments.size()); }

void ModelRigApi::setAttachmentAbsolute(AttachmentId attachment) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        AttachmentRevisionDesc::Step step;
        step.kind = AttachmentRevisionDesc::StepKind::Absolute;
        return AddAttachmentOrientation(model, attachment, step, nullptr, 0);
    });
}

void ModelRigApi::setAttachmentAngles(AttachmentId attachment, const Vec3 &angles) {
    smdl_model *model = this->model();
    const float *angles_c = angles.data();
    RunBody(model, [&] {
        AttachmentRevisionDesc::Step step;
        step.kind = AttachmentRevisionDesc::StepKind::Angles;
        if (angles_c)
            step.angles = QAngle(angles_c[0], angles_c[1], angles_c[2]);
        return AddAttachmentOrientation(model, attachment, step, angles_c, 3);
    });
}

void ModelRigApi::setAttachmentEuler(AttachmentId attachment, const Vec3 &euler) {
    smdl_model *model = this->model();
    const float *euler_c = euler.data();
    RunBody(model, [&] {
        AttachmentRevisionDesc::Step step;
        step.kind = AttachmentRevisionDesc::StepKind::Euler;
        if (euler_c)
            step.euler = RadianEuler(euler_c[0], euler_c[1], euler_c[2]);
        return AddAttachmentOrientation(model, attachment, step, euler_c, 3);
    });
}

void ModelRigApi::setAttachmentQuaternion(AttachmentId attachment, const Quat &rotationQuat) {
    smdl_model *model = this->model();
    const float *rotation = rotationQuat.data();
    RunBody(model, [&] {
        AttachmentRevisionDesc::Step step;
        step.kind = AttachmentRevisionDesc::StepKind::Quaternion;
        if (rotation) {
            step.quaternion = Quaternion(rotation[0], rotation[1], rotation[2], rotation[3]);
            const float lengthSquared = step.quaternion.x * step.quaternion.x + step.quaternion.y * step.quaternion.y +
                                        step.quaternion.z * step.quaternion.z + step.quaternion.w * step.quaternion.w;
            if (AllFinite(rotation, 4) && !(lengthSquared > 1e-12f))
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment rotation quaternion has zero length");
            if (AllFinite(rotation, 4) && std::fabs(lengthSquared - 1.0f) > 1e-6f)
                QuaternionNormalize(step.quaternion);
        }
        return AddAttachmentOrientation(model, attachment, step, rotation, 4);
    });
}

void ModelRigApi::setAttachmentAxes(AttachmentId attachment, const Vec3 &xAxis, const Vec3 &zAxis) {
    smdl_model *model = this->model();
    const float *x_axis = xAxis.data();
    const float *z_axis = zAxis.data();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        AttachmentDesc *desc = GetAttachment(model, attachment, status);
        if (!desc)
            return status;
        if (!x_axis || !z_axis || !AllFinite(x_axis, 3) || !AllFinite(z_axis, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': axes are null or not finite", desc->name);
        AttachmentRevisionDesc::Step step;
        step.kind = AttachmentRevisionDesc::StepKind::Axes;
        step.xAxis = ToVector(x_axis);
        step.zAxis = ToVector(z_axis);
        if (step.xAxis.LengthSqr() == 0.0f || step.zAxis.LengthSqr() == 0.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "attachment '{}': an axis has zero length", desc->name);
        LatestRevision(*desc).steps.push_back(step);
        return Ok(model);
    });
}

void ModelRigApi::setAttachmentRigid(AttachmentId attachment, bool enabledFlag) {
    smdl_model *model = this->model();
    const int enabled = enabledFlag;
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        AttachmentDesc *desc = GetAttachment(model, attachment, status);
        if (!desc)
            return status;
        LatestRevision(*desc).rigid = enabled != 0;
        return Ok(model);
    });
}

void ModelRigApi::setAttachmentWorldAlign(AttachmentId attachment, bool enabledFlag) {
    smdl_model *model = this->model();
    const int enabled = enabledFlag;
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        AttachmentDesc *desc = GetAttachment(model, attachment, status);
        if (!desc)
            return status;
        LatestRevision(*desc).worldAlign = enabled != 0;
        return Ok(model);
    });
}

// ---- hitbox sets and hit groups ----

HitboxSetId ModelRigApi::addHitboxSet(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_hitbox_set_id result = SMDL_NO_ID;
    smdl_hitbox_set_id *out_id = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "hitbox set", kMaxHitboxSetNameLength + 1); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (NameInUse(model->rig.hitboxSets, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "hitbox set '{}' already exists", name);
        HitboxSetDesc set;
        set.name = name;
        model->rig.hitboxSets.push_back(std::move(set));
        if (out_id)
            *out_id = static_cast<smdl_hitbox_set_id>(model->rig.hitboxSets.size() - 1);
        return Ok(model);
    });
    return result;
}

HitboxSetId ModelRigApi::findHitboxSet(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_hitbox_set_id result = SMDL_NO_ID;
    smdl_hitbox_set_id *out_id = &result;
    RunBody(model, [&] { return FindByName(model, model->rig.hitboxSets, name, 0, "hitbox set", out_id); });
    return result;
}

uint32_t ModelRigApi::hitboxSetCount() const { return static_cast<uint32_t>(model()->rig.hitboxSets.size()); }

uint32_t ModelRigApi::addHitbox(HitboxSetId set, int group, BoneId bone, const Vec3 &minsValue, const Vec3 &maxsValue) {
    smdl_model *model = this->model();
    const float *mins = minsValue.data();
    const float *maxs = maxsValue.data();
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_index = &result;
    RunBody(model, [&] {
        if (set >= model->rig.hitboxSets.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "hitbox set {} is out of range ({} sets)", set,
                        model->rig.hitboxSets.size());
        HitboxSetDesc &hitboxSet = model->rig.hitboxSets[set];
        if (smdl_status status = CheckBone(model, bone, "hitbox set", hitboxSet.name); status != SMDL_OK)
            return status;
        if (!mins || !maxs || !AllFinite(mins, 3) || !AllFinite(maxs, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "hitbox set '{}': box corners are null or not finite",
                        hitboxSet.name);
        if (hitboxSet.hitboxes.size() >= kMaxHitboxes)
            return Fail(model, SMDL_ERROR_LIMIT, "hitbox set '{}' has at most {} hitboxes", hitboxSet.name,
                        kMaxHitboxes);
        HitboxDesc &hitbox = hitboxSet.hitboxes.emplace_back();
        hitbox.group = group;
        hitbox.bone = bone;
        hitbox.mins = ToVector(mins);
        hitbox.maxs = ToVector(maxs);
        if (out_index)
            *out_index = static_cast<uint32_t>(hitboxSet.hitboxes.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelRigApi::setHitboxAngles(HitboxSetId set, uint32_t hitbox, const Vec3 &anglesValue) {
    smdl_model *model = this->model();
    const float *angles = anglesValue.data();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        HitboxDesc *desc = GetHitbox(model, set, hitbox, status);
        if (!desc)
            return status;
        if (!angles || !AllFinite(angles, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "hitbox angles are null or not finite");
        desc->angles = QAngle(angles[0], angles[1], angles[2]);
        return Ok(model);
    });
}

void ModelRigApi::setHitboxCapsuleRadius(HitboxSetId set, uint32_t hitbox, float radius) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        HitboxDesc *desc = GetHitbox(model, set, hitbox, status);
        if (!desc)
            return status;
        if (!std::isfinite(radius))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "hitbox capsule radius is not finite");
        desc->capsuleRadius = radius;
        return Ok(model);
    });
}

void ModelRigApi::setHitboxName(HitboxSetId set, uint32_t hitbox, std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        HitboxDesc *desc = GetHitbox(model, set, hitbox, status);
        if (!desc)
            return status;
        if (!name)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "hitbox name is null");
        if (std::string_view(name).size() >= MAXSTUDIONAME)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "hitbox name '{}' is too long (max {} bytes)", name,
                        MAXSTUDIONAME - 1);
        desc->name = name;
        return Ok(model);
    });
}

void ModelRigApi::addHitGroup(int group, BoneId bone) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (smdl_status status = CheckBone(model, bone, "hit group", std::to_string(group)); status != SMDL_OK)
            return status;
        if (model->rig.hitGroups.size() >= kMaxHitGroups)
            return Fail(model, SMDL_ERROR_LIMIT, "a model has at most {} hit groups", kMaxHitGroups);
        model->rig.hitGroups.push_back({group, bone});
        return Ok(model);
    });
}

// ---- pose parameters ----

PoseParamId ModelRigApi::addPoseParameter(std::string_view nameView, float min, float max) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_pose_param_id result = SMDL_NO_ID;
    smdl_pose_param_id *out_id = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "pose parameter", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (!std::isfinite(min) || !std::isfinite(max))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "pose parameter '{}': range is not finite", name);
        if (min > max)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "pose parameter '{}': min {} is above max {}", name, min,
                        max);
        if (model->rig.poseParameters.size() >= kMaxPoseParameters)
            return Fail(model, SMDL_ERROR_LIMIT, "pose parameter '{}': a model has at most {} pose parameters", name,
                        kMaxPoseParameters);
        if (NameInUse(model->rig.poseParameters, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "pose parameter '{}' already exists", name);
        PoseParameterDesc pose;
        pose.name = name;
        pose.min = min;
        pose.max = max;
        model->rig.poseParameters.push_back(std::move(pose));
        if (out_id)
            *out_id = static_cast<smdl_pose_param_id>(model->rig.poseParameters.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelRigApi::setPoseParameterWrap(PoseParamId pose) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetPoseParameterLoop(model, pose, SMDL_POSE_LOOP_WRAP, 0.0f); });
}

void ModelRigApi::setPoseParameterLoop(PoseParamId pose, float range) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetPoseParameterLoop(model, pose, SMDL_POSE_LOOP_RANGE, range); });
}

void ModelRigApi::clearPoseParameterLoop(PoseParamId pose) {
    smdl_model *model = this->model();
    RunBody(model, [&] { return SetPoseParameterLoop(model, pose, SMDL_POSE_LOOP_NONE, 0.0f); });
}

PoseParamId ModelRigApi::findPoseParameter(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_pose_param_id result = SMDL_NO_ID;
    smdl_pose_param_id *out_id = &result;
    RunBody(model, [&] { return FindByName(model, model->rig.poseParameters, name, 0, "pose parameter", out_id); });
    return result;
}

uint32_t ModelRigApi::poseParameterCount() const {
    return static_cast<uint32_t>(model()->rig.poseParameters.size());
}

// ---- IK chains and locks ----

IkChainId ModelRigApi::addIkChain(std::string_view nameView, BoneId end_bone) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_ik_chain_id result = SMDL_NO_ID;
    smdl_ik_chain_id *out_id = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "IK chain", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (smdl_status status = CheckBone(model, end_bone, "IK chain", name); status != SMDL_OK)
            return status;
        if (model->rig.ikChains.size() >= kMaxIkChains)
            return Fail(model, SMDL_ERROR_LIMIT, "IK chain '{}': a model has at most {} IK chains", name, kMaxIkChains);
        if (NameInUse(model->rig.ikChains, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "IK chain '{}' already exists", name);
        IkChainDesc chain;
        chain.name = name;
        chain.endBone = end_bone;
        model->rig.ikChains.push_back(std::move(chain));
        if (out_id)
            *out_id = static_cast<smdl_ik_chain_id>(model->rig.ikChains.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelRigApi::setIkChainAxis(IkChainId chain, uint32_t motion, float value) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (motion == 0 || (motion & ~kMotionMask) != 0 || (motion & (motion - 1)) != 0)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "IK chain '{}': the axis must be exactly one SMDL_MOTION_* flag", desc->name);
        if (!std::isfinite(value))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': value is not finite", desc->name);
        desc->axis = motion;
        desc->value = value;
        return Ok(model);
    });
}

void ModelRigApi::setIkChainHeight(IkChainId chain, float value) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (!std::isfinite(value))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': value is not finite", desc->name);
        desc->height = value;
        return Ok(model);
    });
}

void ModelRigApi::setIkChainFloor(IkChainId chain, float value) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (!std::isfinite(value))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': value is not finite", desc->name);
        desc->floor = value;
        return Ok(model);
    });
}

void ModelRigApi::setIkChainPad(IkChainId chain, float value) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (!std::isfinite(value))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': value is not finite", desc->name);
        desc->radius = value / 2.0f;
        return Ok(model);
    });
}

void ModelRigApi::setIkChainKnee(IkChainId chain, const Vec3 &directionValue) {
    smdl_model *model = this->model();
    const float *direction = directionValue.data();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (!direction || !AllFinite(direction, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': knee direction is null or not finite",
                        desc->name);
        desc->knee = ToVector(direction);
        return Ok(model);
    });
}

void ModelRigApi::setIkChainCenter(IkChainId chain, const Vec3 &centerValue) {
    smdl_model *model = this->model();
    const float *center = centerValue.data();
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (!center || !AllFinite(center, 3))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK chain '{}': center is null or not finite", desc->name);
        desc->center = ToVector(center);
        return Ok(model);
    });
}

IkChainId ModelRigApi::findIkChain(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_ik_chain_id result = SMDL_NO_ID;
    smdl_ik_chain_id *out_id = &result;
    RunBody(model, [&] { return FindByName(model, model->rig.ikChains, name, 0, "IK chain", out_id); });
    return result;
}

uint32_t ModelRigApi::ikChainCount() const { return static_cast<uint32_t>(model()->rig.ikChains.size()); }

uint32_t ModelRigApi::addIkAutoplayLock(IkChainId chain, float position_weight, float local_rotation_weight) {
    smdl_model *model = this->model();
    uint32_t result = SMDL_NO_ID;
    uint32_t *out_index = &result;
    RunBody(model, [&] {
        smdl_status status = SMDL_OK;
        IkChainDesc *desc = GetChain(model, chain, status);
        if (!desc)
            return status;
        if (!std::isfinite(position_weight) || !std::isfinite(local_rotation_weight))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "IK autoplay lock '{}': weights are not finite",
                        desc->name);
        if (model->rig.ikLocks.size() >= kMaxIkLocks)
            return Fail(model, SMDL_ERROR_LIMIT, "a model has at most {} IK autoplay locks", kMaxIkLocks);
        model->rig.ikLocks.push_back({chain, position_weight, local_rotation_weight});
        if (out_index)
            *out_index = static_cast<uint32_t>(model->rig.ikLocks.size() - 1);
        return Ok(model);
    });
    return result;
}

// ---- weight lists ----

WeightListId ModelRigApi::addWeightList(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_weight_list_id result = SMDL_NO_ID;
    smdl_weight_list_id *out_id = &result;
    RunBody(model, [&] {
        if (std::string bad = BadObjectName(name, "weight list", MAXSTUDIONAME); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (model->rig.weightLists.size() >= kMaxWeightLists)
            return Fail(model, SMDL_ERROR_LIMIT, "weight list '{}': a model has at most {} weight lists", name,
                        kMaxWeightLists - 1);
        if (NameInUse(model->rig.weightLists, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "weight list '{}' already exists", name);
        WeightListDesc list;
        list.name = name;
        model->rig.weightLists.push_back(std::move(list));
        if (out_id)
            *out_id = static_cast<smdl_weight_list_id>(model->rig.weightLists.size() - 1);
        return Ok(model);
    });
    return result;
}

void ModelRigApi::setWeightListBone(WeightListId list, BoneId bone, float weight, float position_weight) {
    smdl_model *model = this->model();
    RunBody(model, [&] {
        if (list >= model->rig.weightLists.size())
            return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "weight list {} is out of range ({} lists)", list,
                        model->rig.weightLists.size());
        WeightListDesc &desc = model->rig.weightLists[list];
        const std::string what = list == 0 ? "default" : desc.name;
        if (smdl_status status = CheckBone(model, bone, "weight list", what); status != SMDL_OK)
            return status;
        if (!std::isfinite(weight) || !std::isfinite(position_weight))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "weight list '{}': weights are not finite", what);
        for (WeightEntryDesc &entry: desc.entries) {
            if (entry.bone == bone) {
                entry.weight = weight;
                entry.positionWeight = position_weight;
                return Ok(model);
            }
        }
        if (desc.entries.size() >= kMaxWeightsPerList)
            return Fail(model, SMDL_ERROR_LIMIT, "weight list '{}' has at most {} bones", what, kMaxWeightsPerList);
        desc.entries.push_back({bone, weight, position_weight});
        return Ok(model);
    });
}

WeightListId ModelRigApi::findWeightList(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_weight_list_id result = SMDL_NO_ID;
    smdl_weight_list_id *out_id = &result;
    RunBody(model, [&] { return FindByName(model, model->rig.weightLists, name, 1, "weight list", out_id); });
    return result;
}

uint32_t ModelRigApi::weightListCount() const { return static_cast<uint32_t>(model()->rig.weightLists.size()); }

} // namespace detail
} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
extern "C" {

// ---- attachments ----

SMDL_API smdl_status smdl_model_add_attachment(smdl_model *model, const char *name, smdl_bone_id bone,
                                               const float position[3], smdl_attachment_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position)
        return Guard(model, [&] { return FailNullAttachmentPosition(model, name, bone); });
    return CallCpp(model, [&] {
        const smdl_attachment_id id = smdl::detail::BorrowModel(model).addAttachment(
            name ? name : "", bone, smdl::Vec3{position[0], position[1], position[2]});
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_redefine_attachment(smdl_model *model, smdl_attachment_id id, smdl_bone_id bone,
                                                    const float position[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!position)
        return Guard(model, [&] { return FailNullRedefinePosition(model, id, bone); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).redefineAttachment(id, bone,
                                                            smdl::Vec3{position[0], position[1], position[2]});
    });
}

SMDL_API smdl_status smdl_model_find_attachment(const smdl_model *model, const char *name, smdl_attachment_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_attachment_id id =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findAttachment(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_attachment_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->rig.attachments.size()) : 0;
}

SMDL_API smdl_status smdl_model_set_attachment_absolute(smdl_model *model, smdl_attachment_id attachment) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAttachmentAbsolute(attachment); });
}

SMDL_API smdl_status smdl_model_set_attachment_angles(smdl_model *model, smdl_attachment_id attachment,
                                                      const float angles[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!angles)
        return Guard(model, [&] { return FailNullAttachmentValue(model, attachment, "rotation is null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setAttachmentAngles(attachment, smdl::Vec3{angles[0], angles[1], angles[2]});
    });
}

SMDL_API smdl_status smdl_model_set_attachment_euler(smdl_model *model, smdl_attachment_id attachment,
                                                     const float euler[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!euler)
        return Guard(model, [&] { return FailNullAttachmentValue(model, attachment, "rotation is null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setAttachmentEuler(attachment, smdl::Vec3{euler[0], euler[1], euler[2]});
    });
}

SMDL_API smdl_status smdl_model_set_attachment_quaternion(smdl_model *model, smdl_attachment_id attachment,
                                                          const float rotation[4]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!rotation)
        return Guard(model, [&] { return FailNullAttachmentValue(model, attachment, "rotation is null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setAttachmentQuaternion(
            attachment, smdl::Quat{rotation[0], rotation[1], rotation[2], rotation[3]});
    });
}

SMDL_API smdl_status smdl_model_set_attachment_axes(smdl_model *model, smdl_attachment_id attachment,
                                                    const float x_axis[3], const float z_axis[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!x_axis || !z_axis)
        return Guard(model, [&] { return FailNullAttachmentValue(model, attachment, "axes are null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setAttachmentAxes(attachment, smdl::Vec3{x_axis[0], x_axis[1], x_axis[2]},
                                                           smdl::Vec3{z_axis[0], z_axis[1], z_axis[2]});
    });
}

SMDL_API smdl_status smdl_model_set_attachment_rigid(smdl_model *model, smdl_attachment_id attachment, int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAttachmentRigid(attachment, enabled != 0); });
}

SMDL_API smdl_status smdl_model_set_attachment_world_align(smdl_model *model, smdl_attachment_id attachment,
                                                           int enabled) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setAttachmentWorldAlign(attachment, enabled != 0); });
}

// ---- hitbox sets and hit groups ----

SMDL_API smdl_status smdl_model_add_hitbox_set(smdl_model *model, const char *name, smdl_hitbox_set_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_hitbox_set_id id = smdl::detail::BorrowModel(model).addHitboxSet(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_find_hitbox_set(const smdl_model *model, const char *name, smdl_hitbox_set_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_hitbox_set_id id =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findHitboxSet(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_hitbox_set_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->rig.hitboxSets.size()) : 0;
}

SMDL_API smdl_status smdl_model_add_hitbox(smdl_model *model, smdl_hitbox_set_id set, int group, smdl_bone_id bone,
                                           const float mins[3], const float maxs[3], uint32_t *out_index) {
    if (out_index)
        *out_index = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!mins || !maxs)
        return Guard(model, [&] { return FailNullHitboxBox(model, set, bone); });
    return CallCpp(model, [&] {
        const uint32_t index = smdl::detail::BorrowModel(model).addHitbox(
            set, group, bone, smdl::Vec3{mins[0], mins[1], mins[2]}, smdl::Vec3{maxs[0], maxs[1], maxs[2]});
        if (out_index)
            *out_index = index;
    });
}

SMDL_API smdl_status smdl_model_set_hitbox_angles(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox,
                                                  const float angles[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!angles)
        return Guard(model,
                     [&] { return FailNullHitboxValue(model, set, hitbox, "hitbox angles are null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setHitboxAngles(set, hitbox, smdl::Vec3{angles[0], angles[1], angles[2]});
    });
}

SMDL_API smdl_status smdl_model_set_hitbox_capsule_radius(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox,
                                                          float radius) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setHitboxCapsuleRadius(set, hitbox, radius); });
}

SMDL_API smdl_status smdl_model_set_hitbox_name(smdl_model *model, smdl_hitbox_set_id set, uint32_t hitbox,
                                                const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!name)
        return Guard(model, [&] { return FailNullHitboxValue(model, set, hitbox, "hitbox name is null"); });
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setHitboxName(set, hitbox, name); });
}

SMDL_API smdl_status smdl_model_add_hit_group(smdl_model *model, int group, smdl_bone_id bone) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).addHitGroup(group, bone); });
}

// ---- pose parameters ----

SMDL_API smdl_status smdl_model_add_pose_parameter(smdl_model *model, const char *name, float min, float max,
                                                   smdl_pose_param_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_pose_param_id id = smdl::detail::BorrowModel(model).addPoseParameter(name ? name : "", min, max);
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_set_pose_parameter_loop(smdl_model *model, smdl_pose_param_id pose_parameter,
                                                        smdl_pose_loop_mode mode, float range) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return Guard(model, [&] { return SetPoseParameterLoop(model, pose_parameter, mode, range); });
}

SMDL_API smdl_status smdl_model_find_pose_parameter(const smdl_model *model, const char *name,
                                                    smdl_pose_param_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_pose_param_id id =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findPoseParameter(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_pose_parameter_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->rig.poseParameters.size()) : 0;
}

// ---- IK chains and locks ----

SMDL_API smdl_status smdl_model_add_ik_chain(smdl_model *model, const char *name, smdl_bone_id end_bone,
                                             smdl_ik_chain_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_ik_chain_id id = smdl::detail::BorrowModel(model).addIkChain(name ? name : "", end_bone);
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_set_ik_chain_axis(smdl_model *model, smdl_ik_chain_id chain, uint32_t motion,
                                                  float value) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setIkChainAxis(chain, motion, value); });
}

#define SMDL_IK_CHAIN_FLOAT_SETTER(function, method)                                                                 \
    SMDL_API smdl_status function(smdl_model *model, smdl_ik_chain_id chain, float value) {                          \
        if (!model)                                                                                                  \
            return SMDL_ERROR_INVALID_ARGUMENT;                                                                      \
        return CallCpp(model, [&] { smdl::detail::BorrowModel(model).method(chain, value); });                       \
    }

SMDL_IK_CHAIN_FLOAT_SETTER(smdl_model_set_ik_chain_height, setIkChainHeight)
SMDL_IK_CHAIN_FLOAT_SETTER(smdl_model_set_ik_chain_floor, setIkChainFloor)
SMDL_IK_CHAIN_FLOAT_SETTER(smdl_model_set_ik_chain_pad, setIkChainPad)

SMDL_API smdl_status smdl_model_set_ik_chain_knee(smdl_model *model, smdl_ik_chain_id chain, const float direction[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!direction)
        return Guard(model, [&] { return FailNullChainValue(model, chain, "knee direction is null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setIkChainKnee(chain, smdl::Vec3{direction[0], direction[1], direction[2]});
    });
}

SMDL_API smdl_status smdl_model_set_ik_chain_center(smdl_model *model, smdl_ik_chain_id chain, const float center[3]) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (!center)
        return Guard(model, [&] { return FailNullChainValue(model, chain, "center is null or not finite"); });
    return CallCpp(model, [&] {
        smdl::detail::BorrowModel(model).setIkChainCenter(chain, smdl::Vec3{center[0], center[1], center[2]});
    });
}

SMDL_API smdl_status smdl_model_find_ik_chain(const smdl_model *model, const char *name, smdl_ik_chain_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_ik_chain_id id =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findIkChain(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_ik_chain_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->rig.ikChains.size()) : 0;
}

SMDL_API smdl_status smdl_model_add_ik_autoplay_lock(smdl_model *model, smdl_ik_chain_id chain, float position_weight,
                                                     float local_rotation_weight, uint32_t *out_index) {
    if (out_index)
        *out_index = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const uint32_t index =
            smdl::detail::BorrowModel(model).addIkAutoplayLock(chain, position_weight, local_rotation_weight);
        if (out_index)
            *out_index = index;
    });
}

// ---- weight lists ----

SMDL_API smdl_status smdl_model_add_weight_list(smdl_model *model, const char *name, smdl_weight_list_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_weight_list_id id = smdl::detail::BorrowModel(model).addWeightList(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API smdl_status smdl_model_set_weight_list_bone(smdl_model *model, smdl_weight_list_id list, smdl_bone_id bone,
                                                     float weight, float position_weight) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model,
                   [&] { smdl::detail::BorrowModel(model).setWeightListBone(list, bone, weight, position_weight); });
}

SMDL_API smdl_status smdl_model_find_weight_list(const smdl_model *model, const char *name,
                                                 smdl_weight_list_id *out_id) {
    if (out_id)
        *out_id = SMDL_NO_ID;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl_weight_list_id id =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findWeightList(name ? name : "");
        if (out_id)
            *out_id = id;
    });
}

SMDL_API uint32_t smdl_model_weight_list_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->rig.weightLists.size()) : 0;
}

} // extern "C"

// smdl_lod.h: LODs and the shadow LOD (store only; the compile applies them in ApplyLods, apply_lod.cpp)
#include "studiomdl/c/smdl_lod.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>
#include <cmath>

#include "core_util.hpp"
#include "studio.h"

using namespace smdl_api;

namespace {
    // MAX_NUM_LODS counts the root LOD
    constexpr size_t kMaxAddedLods = MAX_NUM_LODS - 1;

    smdl_lod *AddLod(smdl_model &model, bool shadow, float distance) {
        LodsDesc &lods = model.lods;
        auto &lod = lods.lods.emplace_back(std::make_unique<smdl_lod>());
        lod->model = &model;
        lod->isShadow = shadow;
        lod->switchDistance = distance;
        lod->facialAnimation = !shadow;
        lod->label = shadow ? "shadow lod" : std::format("lod {}", lods.lods.size());
        return lod.get();
    }

    smdl_status CheckCanAddLod(smdl_model &model) {
        if (model.lods.hasShadow())
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "the shadow LOD must be the last LOD of the model");
        if (model.lods.lods.size() >= kMaxAddedLods)
            return Fail(&model, SMDL_ERROR_LIMIT, "too many LODs (at most {} besides the root LOD)", kMaxAddedLods);
        return SMDL_OK;
    }

    bool OwnedBy(const smdl_model &model, const smdl_mesh *mesh) { return mesh && mesh->model == &model; }

    bool UsesMesh(const smdl_lod &lod, const smdl_mesh *mesh) {
        return std::ranges::any_of(lod.modelReplacements, [&](const auto &entry) { return entry.mesh == mesh; });
    }

    smdl_status CheckMeshTarget(smdl_lod &lod, const smdl_mesh *mesh) {
        smdl_model &model = *lod.model;
        if (!OwnedBy(model, mesh))
            return Fail(&model, SMDL_ERROR_INVALID_ARGUMENT, "{}: mesh is null or not a mesh of this model", lod.label);
        if (UsesMesh(lod, mesh))
            return Fail(&model, SMDL_ERROR_DUPLICATE_NAME, "{}: mesh '{}' is already replaced or removed", lod.label,
                        mesh->name);
        return SMDL_OK;
    }

    smdl_status CheckBone(const smdl_lod &lod, smdl_bone_id bone, const char *what) {
        const smdl_model &model = *lod.model;
        if (!model.skeleton.valid(bone))
            return Fail(&model, SMDL_ERROR_OUT_OF_RANGE, "{}: {}: bone id {} is not a bone of the model ({} bones)",
                        lod.label, what, bone, model.skeleton.bones.size());
        return SMDL_OK;
    }

    smdl_status CheckName(const smdl_lod &lod, const char *name, const char *what) {
        if (!ValidName(name))
            return Fail(lod.model, SMDL_ERROR_INVALID_ARGUMENT, "{}: {} is null or empty", lod.label, what);
        return SMDL_OK;
    }
}

namespace smdl {

bool Lod::isShadow() const { return handle()->isShadow; }

Geometry Lod::replaceMesh(const Mesh &meshView) const {
    smdl_lod *lod = handle();
    smdl_mesh *mesh = meshView.handle();
    smdl_model *model = lod->model;
    smdl_geometry *result = nullptr;
    smdl_geometry **out_geometry = &result;
    RunBody(model, [&] {
        if (const auto status = CheckMeshTarget(*lod, mesh))
            return status;
        smdl_geometry *geometry = NewGeometry(*model, std::format("{} / mesh '{}'", lod->label, mesh->name));
        geometry->sourceName = std::format("{}_lod{}", mesh->name, model->lods.lods.size());
        lod->modelReplacements.push_back({mesh, geometry});
        if (out_geometry)
            *out_geometry = geometry;
        return Ok(model);
    });
    return Geometry(result, this->model());
}

void Lod::replaceMesh(const Mesh &meshView, const Geometry &geometryView) const {
    smdl_lod *lod = handle();
    smdl_mesh *mesh = meshView.handle();
    smdl_geometry *geometry = geometryView.handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (const auto status = CheckMeshTarget(*lod, mesh))
            return status;
        if (!geometry || geometry->model != model)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: geometry is null or not a geometry of this model",
                        lod->label);
        lod->modelReplacements.push_back({mesh, geometry});
        return Ok(model);
    });
}

void Lod::removeMesh(const Mesh &meshView) const {
    smdl_lod *lod = handle();
    smdl_mesh *mesh = meshView.handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (const auto status = CheckMeshTarget(*lod, mesh))
            return status;
        lod->modelReplacements.push_back({mesh, nullptr});
        return Ok(model);
    });
}

void Lod::replaceBone(BoneId from, BoneId to) const {
    smdl_lod *lod = handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (const auto status = CheckBone(*lod, from, "replace bone (from)"))
            return status;
        if (const auto status = CheckBone(*lod, to, "replace bone (to)"))
            return status;
        lod->boneReplacements.emplace_back(from, to);
        return Ok(model);
    });
}

void Lod::collapseBoneTree(BoneId bone) const {
    smdl_lod *lod = handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (const auto status = CheckBone(*lod, bone, "collapse bone tree"))
            return status;
        lod->boneTreeCollapses.push_back(bone);
        return Ok(model);
    });
}

void Lod::replaceMaterial(std::string_view fromView, std::string_view toView) const {
    const std::string fromStorage(fromView);
    const char *from = fromStorage.c_str();
    const std::string toStorage(toView);
    const char *to = toStorage.c_str();
    smdl_lod *lod = handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (const auto status = CheckName(*lod, from, "replaced material name"))
            return status;
        if (const auto status = CheckName(*lod, to, "replacement material name"))
            return status;
        lod->materialReplacements.emplace_back(from, to);
        return Ok(model);
    });
}

void Lod::removeMaterial(std::string_view materialView) const {
    const std::string materialStorage(materialView);
    const char *material = materialStorage.c_str();
    smdl_lod *lod = handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (const auto status = CheckName(*lod, material, "removed material name"))
            return status;
        lod->removedMaterials.emplace_back(material);
        return Ok(model);
    });
}

void Lod::setFacialAnimation(bool enabled) const {
    smdl_lod *lod = handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (enabled && lod->isShadow)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "facial animation is not allowed for the shadow LOD");
        lod->facialAnimation = enabled != 0;
        return Ok(model);
    });
}

void Lod::setUseShadowLodMaterials(bool enabled) const {
    smdl_lod *lod = handle();
    smdl_model *model = lod->model;
    RunBody(model, [&] {
        if (enabled && !lod->isShadow)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: use_shadowlod_materials only applies to the shadow LOD",
                        lod->label);
        lod->useShadowLodMaterials = enabled != 0;
        return Ok(model);
    });
}

namespace detail {

Lod ModelLodApi::addLod(float switch_distance) {
    smdl_model *model = this->model();
    smdl_lod *result = nullptr;
    smdl_lod **out_lod = &result;
    RunBody(model, [&] {
        if (!std::isfinite(switch_distance) || switch_distance < 0.0f)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT,
                        "LOD switch distance {} is not valid (must be >= 0, negative values are reserved for the shadow LOD)",
                        switch_distance);
        if (const auto status = CheckCanAddLod(*model))
            return status;
        smdl_lod *lod = AddLod(*model, false, switch_distance);
        if (out_lod)
            *out_lod = lod;
        return Ok(model);
    });
    return Lod(result, model);
}

Lod ModelLodApi::addShadowLod() {
    smdl_model *model = this->model();
    smdl_lod *result = nullptr;
    smdl_lod **out_lod = &result;
    RunBody(model, [&] {
        if (model->lods.hasShadow())
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "the model has a shadow LOD already");
        if (const auto status = CheckCanAddLod(*model))
            return status;
        smdl_lod *lod = AddLod(*model, true, 0.0f);
        if (out_lod)
            *out_lod = lod;
        return Ok(model);
    });
    return Lod(result, model);
}

uint32_t ModelLodApi::lodCount() const { return static_cast<uint32_t>(model()->lods.lods.size()); }

} // namespace detail
} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
extern "C" {

SMDL_API smdl_status smdl_model_add_lod(smdl_model *model, float switch_distance, smdl_lod **out_lod) {
    if (out_lod)
        *out_lod = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Lod lod = smdl::detail::BorrowModel(model).addLod(switch_distance);
        if (out_lod)
            *out_lod = lod.handle();
    });
}

SMDL_API smdl_status smdl_model_add_shadow_lod(smdl_model *model, smdl_lod **out_lod) {
    if (out_lod)
        *out_lod = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Lod lod = smdl::detail::BorrowModel(model).addShadowLod();
        if (out_lod)
            *out_lod = lod.handle();
    });
}

SMDL_API uint32_t smdl_model_lod_count(const smdl_model *model) {
    return model ? static_cast<uint32_t>(model->lods.lods.size()) : 0;
}

SMDL_API int smdl_lod_is_shadow(const smdl_lod *lod) { return lod && lod->isShadow ? 1 : 0; }

SMDL_API smdl_status smdl_lod_replace_mesh(smdl_lod *lod, smdl_mesh *mesh, smdl_geometry **out_geometry) {
    if (out_geometry)
        *out_geometry = nullptr;
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] {
        const smdl::Geometry geometry = smdl::Lod(lod, lod->model).replaceMesh(smdl::Mesh(mesh, lod->model));
        if (out_geometry)
            *out_geometry = geometry.handle();
    });
}

SMDL_API smdl_status smdl_lod_replace_mesh_geometry(smdl_lod *lod, smdl_mesh *mesh, smdl_geometry *geometry) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] {
        smdl::Lod(lod, lod->model).replaceMesh(smdl::Mesh(mesh, lod->model), smdl::Geometry(geometry, lod->model));
    });
}

SMDL_API smdl_status smdl_lod_remove_mesh(smdl_lod *lod, smdl_mesh *mesh) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] { smdl::Lod(lod, lod->model).removeMesh(smdl::Mesh(mesh, lod->model)); });
}

SMDL_API smdl_status smdl_lod_replace_bone(smdl_lod *lod, smdl_bone_id from, smdl_bone_id to) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] { smdl::Lod(lod, lod->model).replaceBone(from, to); });
}

SMDL_API smdl_status smdl_lod_collapse_bone_tree(smdl_lod *lod, smdl_bone_id bone) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] { smdl::Lod(lod, lod->model).collapseBoneTree(bone); });
}

SMDL_API smdl_status smdl_lod_replace_material(smdl_lod *lod, const char *from, const char *to) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] {
        smdl::Lod(lod, lod->model).replaceMaterial(from ? from : "", to ? to : "");
    });
}

SMDL_API smdl_status smdl_lod_remove_material(smdl_lod *lod, const char *material) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] { smdl::Lod(lod, lod->model).removeMaterial(material ? material : ""); });
}

SMDL_API smdl_status smdl_lod_set_facial_animation(smdl_lod *lod, int enabled) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] { smdl::Lod(lod, lod->model).setFacialAnimation(enabled != 0); });
}

SMDL_API smdl_status smdl_lod_set_use_shadow_lod_materials(smdl_lod *lod, int enabled) {
    if (!lod)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(lod->model, [&] { smdl::Lod(lod, lod->model).setUseShadowLodMaterials(enabled != 0); });
}

} // extern "C"

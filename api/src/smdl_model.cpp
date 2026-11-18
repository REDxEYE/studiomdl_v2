// smdl_common.h / smdl_model.h: version, status strings, model lifetime, shared object creation
#include "studiomdl/c/smdl_model.h"

#include <algorithm>

#include "api_util.hpp"
#include "studiomdl/studiomdl.hpp"

using namespace smdl_api;

std::optional<smdl_bone_id> SkeletonDesc::find(std::string_view name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name)
            return static_cast<smdl_bone_id>(i);
    }
    return std::nullopt;
}

namespace smdl_api {

smdl_geometry *NewGeometry(smdl_model &model, std::string owner) {
    auto &geometry = model.geometries.emplace_back(std::make_unique<smdl_geometry>());
    geometry->model = &model;
    geometry->owner = std::move(owner);
    return geometry.get();
}

smdl_mesh *NewMesh(smdl_model &model, std::string name, smdl_bodygroup *group) {
    auto &mesh = model.meshes.emplace_back(std::make_unique<smdl_mesh>());
    mesh->model = &model;
    mesh->name = std::move(name);
    mesh->group = group;
    mesh->geometry = NewGeometry(model, std::format("mesh '{}'", mesh->name));
    return mesh.get();
}

smdl_bodygroup *NewBodyGroup(smdl_model &model, std::string name) {
    auto &group = model.bodygroups.emplace_back(std::make_unique<smdl_bodygroup>());
    group->model = &model;
    group->name = std::move(name);
    return group.get();
}

} // namespace smdl_api

// Model names are paths under models/: '/' separators, no extension, no leading separator
static std::string NormalizeModelName(std::string_view name) {
    std::string out(name);
    std::ranges::replace(out, '\\', '/');
    while (!out.empty() && out.front() == '/')
        out.erase(out.begin());
    if (out.size() > 4 && (out.ends_with(".mdl") || out.ends_with(".MDL")))
        out.resize(out.size() - 4);
    return out;
}

namespace smdl {

Model::Model(const std::string_view name) : m_Model(nullptr, Deleter{true}) {
    std::string normalized = NormalizeModelName(name);
    if (normalized.empty())
        throw Error(SMDL_ERROR_INVALID_ARGUMENT, std::format("invalid model name '{}'", name));
    m_Model.reset(new smdl_model);
    m_Model->name = std::move(normalized);
}

std::string_view Model::name() const { return handle()->name; }

void Model::setName(const std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = handle();
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "model name is empty");
        std::string normalized = NormalizeModelName(name);
        if (normalized.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "model name '{}' is empty after normalization", name);
        model->name = std::move(normalized);
        return Ok(model);
    });
}

void Model::Deleter::operator()(smdl_model *model) const {
    if (owned)
        delete model;
}

namespace detail {
    Model BorrowModel(smdl_model *model) { return Model(model, Model::Borrowed{}); }
    smdl_model *ReleaseModel(Model &&model) { return model.m_Model.release(); }

    // The handles of the API base classes (declared in the part headers; Model, Geometry and Mesh are complete here)
    smdl_model *ModelGlobalsApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelSkeletonApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelBodyApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelFlexApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelRigApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelAnimationApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelSequenceApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelLodApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelCollisionApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_model *ModelBoneDynamicsApi::model() const { return static_cast<const Model *>(this)->handle(); }
    smdl_geometry *GeometryShapesApi::handle() const { return static_cast<const Geometry *>(this)->handle(); }
    smdl_model *GeometryShapesApi::ownerModel() const { return static_cast<const Geometry *>(this)->model(); }
    smdl_mesh *MeshFlexApi::mesh() const { return static_cast<const Mesh *>(this)->handle(); }
    smdl_model *MeshFlexApi::ownerModel() const { return static_cast<const Mesh *>(this)->model(); }
}

} // namespace smdl

extern "C" {

SMDL_API uint32_t smdl_api_version(void) { return SMDL_API_VERSION; }

SMDL_API const char *smdl_status_string(smdl_status status) {
    switch (status) {
        case SMDL_OK: return "ok";
        case SMDL_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case SMDL_ERROR_OUT_OF_RANGE: return "id or index out of range";
        case SMDL_ERROR_DUPLICATE_NAME: return "duplicate name";
        case SMDL_ERROR_NOT_FOUND: return "not found";
        case SMDL_ERROR_LIMIT: return "over a model format limit";
        case SMDL_ERROR_SYNTAX: return "syntax error";
        case SMDL_ERROR_COMPILE: return "compile failed";
        case SMDL_ERROR_IO: return "file write failed";
        case SMDL_ERROR_INTERNAL: return "internal error";
    }
    return "unknown status";
}

SMDL_API smdl_model *smdl_model_create(const char *name) {
    if (!ValidName(name))
        return nullptr;
    try {
        smdl::Model model(name);
        return smdl::detail::ReleaseModel(std::move(model));
    }
    catch (...) {
        return nullptr;
    }
}

SMDL_API void smdl_model_destroy(smdl_model *model) { delete model; }

SMDL_API const char *smdl_model_last_error(const smdl_model *model) { return model ? model->lastError.c_str() : ""; }

SMDL_API smdl_status smdl_model_set_name(smdl_model *model, const char *name) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] { smdl::detail::BorrowModel(model).setName(name ? name : ""); });
}

SMDL_API const char *smdl_model_get_name(const smdl_model *model) { return model ? model->name.c_str() : ""; }

} // extern "C"

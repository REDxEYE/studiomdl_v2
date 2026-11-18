// smdl::Mesh, smdl::BodyGroup and the body part of smdl::Model (cpp/body.hpp): body groups, meshes, presets. The C API
// (smdl_body.h) wraps the C++ methods.
#include "studiomdl/c/smdl_body.h"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>

#include "core_util.hpp"
#include "studiomdl/studiomdl_constants.hpp"

using namespace smdl_api;

namespace {
    // mstudiomodel_t::name holds 64 bytes
    constexpr size_t kMaxMeshNameLength = 63;

    bool MeshNameInUse(const smdl_model &model, std::string_view name) {
        return std::ranges::any_of(model.meshes, [&](const auto &mesh) { return IEquals(mesh->name, name); });
    }

    bool GroupNameInUse(const smdl_model &model, std::string_view name) {
        return std::ranges::any_of(model.bodygroups, [&](const auto &group) { return IEquals(group->name, name); });
    }

    // Empty = fine
    std::string BadMeshName(const char *name) {
        if (!ValidName(name))
            return "mesh name is null or empty";
        if (std::string_view(name).size() > kMaxMeshNameLength)
            return std::format("mesh name '{}' is too long (max {} bytes)", name, kMaxMeshNameLength);
        return {};
    }

    std::string BadGroupName(const char *name) {
        if (!ValidName(name))
            return "bodygroup name is null or empty";
        if (std::string_view(name).size() >= MAXSTUDIONAME)
            return std::format("bodygroup name '{}' is too long (max {} bytes)", name, MAXSTUDIONAME - 1);
        return {};
    }
}

namespace smdl {

std::string_view Mesh::name() const { return handle()->name; }

Geometry Mesh::geometry() const { return Geometry(handle()->geometry, model()); }

Geometry Mesh::appendGeometry() const {
    smdl_mesh *mesh = handle();
    smdl_geometry *result = nullptr;
    smdl_geometry **out_geometry = &result;
    RunBody(mesh->model, [&] {
        smdl_geometry *geometry = NewGeometry(
            *mesh->model, std::format("mesh '{}' / appended geometry {}", mesh->name, mesh->appended.size() + 1));
        geometry->appended = true;
        geometry->sourceName = std::format("{}_append{}", mesh->name, mesh->appended.size() + 1);
        mesh->appended.push_back(geometry);
        if (out_geometry)
            *out_geometry = geometry;
        return Ok(mesh->model);
    });
    return {result, model()};
}

std::string_view BodyGroup::name() const { return handle()->name; }

uint32_t BodyGroup::choiceCount() const { return static_cast<uint32_t>(handle()->choices.size()); }

std::vector<Mesh> BodyGroup::choices() const {
    std::vector<Mesh> items;
    items.reserve(handle()->choices.size());
    for (auto mesh : handle()->choices)
        items.emplace_back(mesh, m_Model);
    return items;
}

Mesh BodyGroup::createMesh(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_bodygroup *group = handle();
    smdl_model *model = group->model;
    smdl_mesh *result = nullptr;
    smdl_mesh **out_mesh = &result;
    RunBody(model, [&] {
        if (std::string bad = BadMeshName(name); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bodygroup '{}': {}", group->name, bad);
        if (MeshNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "mesh '{}' already exists", name);
        smdl_mesh *mesh = NewMesh(*model, name, group);
        mesh->geometry->sourceName = name;
        group->choices.push_back(mesh);
        if (out_mesh)
            *out_mesh = mesh;
        return Ok(model);
    });
    return {result, model};
}

void BodyGroup::addBlank() const { handle()->choices.push_back(nullptr); }

namespace detail {

Mesh ModelBodyApi::createMesh(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_mesh *result = nullptr;
    smdl_mesh **out_mesh = &result;
    RunBody(model, [&] {
        if (std::string bad = BadMeshName(name); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (std::string bad = BadGroupName(name); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (MeshNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "mesh '{}' already exists", name);
        if (GroupNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "bodygroup '{}' already exists", name);
        smdl_bodygroup *group = NewBodyGroup(*model, name);
        smdl_mesh *mesh = NewMesh(*model, name, group);
        mesh->geometry->sourceName = name;
        group->choices.push_back(mesh);
        if (out_mesh)
            *out_mesh = mesh;
        return Ok(model);
    });
    return {result, model};
}

BodyGroup ModelBodyApi::addBodyGroup(std::string_view nameView) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_bodygroup *result = nullptr;
    smdl_bodygroup **out_group = &result;
    RunBody(model, [&] {
        if (std::string bad = BadGroupName(name); !bad.empty())
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}", bad);
        if (GroupNameInUse(*model, name))
            return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "bodygroup '{}' already exists", name);
        smdl_bodygroup *group = NewBodyGroup(*model, name);
        if (out_group)
            *out_group = group;
        return Ok(model);
    });
    return {result, model};
}

Mesh ModelBodyApi::findMesh(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_mesh *result = nullptr;
    smdl_mesh **out_mesh = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "mesh name is null or empty");
        for (const auto &mesh: model->meshes) {
            if (IEquals(mesh->name, name)) {
                if (out_mesh)
                    *out_mesh = mesh.get();
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "no mesh named '{}'", name);
    });
    return {result, model};
}

BodyGroup ModelBodyApi::findBodyGroup(std::string_view nameView) const {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    smdl_bodygroup *result = nullptr;
    smdl_bodygroup **out_group = &result;
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bodygroup name is null or empty");
        for (const auto &group: model->bodygroups) {
            if (IEquals(group->name, name)) {
                if (out_group)
                    *out_group = group.get();
                return Ok(model);
            }
        }
        return Fail(model, SMDL_ERROR_NOT_FOUND, "no bodygroup named '{}'", name);
    });
    return {result, model};
}

uint32_t ModelBodyApi::bodyGroupCount() const { return static_cast<uint32_t>(model()->bodygroups.size()); }

void ModelBodyApi::addBodyGroupPreset(std::string_view nameView,
                                      std::span<const std::pair<BodyGroup, uint32_t>> presetValues) {
    const std::string nameStorage(nameView);
    const char *name = nameStorage.c_str();
    smdl_model *model = this->model();
    std::vector<const smdl_bodygroup *> groupHandles;
    std::vector<uint32_t> choices;
    for (const auto &[group, choice]: presetValues) {
        groupHandles.push_back(group.handle());
        choices.push_back(choice);
    }
    const smdl_bodygroup *const *groups = groupHandles.data();
    const uint32_t *values = choices.data();
    const size_t count = presetValues.size();
    RunBody(model, [&] {
        if (!ValidName(name))
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bodygroup preset name is null or empty");
        if (std::string_view(name).size() >= MAXSTUDIONAME)
            return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bodygroup preset name '{}' is too long (max {} bytes)", name,
                        MAXSTUDIONAME - 1);
        for (const BodyGroupPresetDesc &preset: model->body.presets) {
            if (preset.name == name)
                return Fail(model, SMDL_ERROR_DUPLICATE_NAME, "bodygroup preset '{}' already exists", name);
        }
        BodyGroupPresetDesc preset;
        preset.name = name;
        for (size_t i = 0; i < count; ++i) {
            const bool known = groups[i] && std::ranges::any_of(model->bodygroups, [&](const auto &group) {
                return group.get() == groups[i];
            });
            if (!known)
                return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bodygroup preset '{}': entry {} is not a bodygroup of this model",
                            name, i);
            preset.values.emplace_back(groups[i], values[i]);
        }
        model->body.presets.push_back(std::move(preset));
        return Ok(model);
    });
}

} // namespace detail
} // namespace smdl

// The C API: argument checks that need C (null pointers, out-parameters), then the C++ method
extern "C" {

SMDL_API smdl_status smdl_model_create_mesh(smdl_model *model, const char *name, smdl_mesh **out_mesh) {
    if (out_mesh)
        *out_mesh = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Mesh mesh = smdl::detail::BorrowModel(model).createMesh(name ? name : "");
        if (out_mesh)
            *out_mesh = mesh.handle();
    });
}

SMDL_API smdl_status smdl_model_add_bodygroup(smdl_model *model, const char *name, smdl_bodygroup **out_group) {
    if (out_group)
        *out_group = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::BodyGroup group = smdl::detail::BorrowModel(model).addBodyGroup(name ? name : "");
        if (out_group)
            *out_group = group.handle();
    });
}

SMDL_API smdl_status smdl_bodygroup_create_mesh(smdl_bodygroup *group, const char *name, smdl_mesh **out_mesh) {
    if (out_mesh)
        *out_mesh = nullptr;
    if (!group)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(group->model, [&] {
        const smdl::Mesh mesh = smdl::BodyGroup(group, group->model).createMesh(name ? name : "");
        if (out_mesh)
            *out_mesh = mesh.handle();
    });
}

SMDL_API smdl_status smdl_bodygroup_add_blank(smdl_bodygroup *group) {
    if (!group)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(group->model, [&] { smdl::BodyGroup(group, group->model).addBlank(); });
}

SMDL_API smdl_status smdl_mesh_get_geometry(smdl_mesh *mesh, smdl_geometry **out_geometry) {
    if (out_geometry)
        *out_geometry = nullptr;
    if (!mesh || !out_geometry)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] { *out_geometry = smdl::Mesh(mesh, mesh->model).geometry().handle(); });
}

SMDL_API smdl_status smdl_mesh_append_geometry(smdl_mesh *mesh, smdl_geometry **out_geometry) {
    if (out_geometry)
        *out_geometry = nullptr;
    if (!mesh)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(mesh->model, [&] {
        const smdl::Geometry geometry = smdl::Mesh(mesh, mesh->model).appendGeometry();
        if (out_geometry)
            *out_geometry = geometry.handle();
    });
}

SMDL_API const char *smdl_mesh_name(const smdl_mesh *mesh) { return mesh ? mesh->name.c_str() : ""; }

SMDL_API smdl_status smdl_model_find_mesh(const smdl_model *model, const char *name, smdl_mesh **out_mesh) {
    if (out_mesh)
        *out_mesh = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::Mesh mesh = smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findMesh(name ? name : "");
        if (out_mesh)
            *out_mesh = mesh.handle();
    });
}

SMDL_API smdl_status smdl_model_find_bodygroup(const smdl_model *model, const char *name, smdl_bodygroup **out_group) {
    if (out_group)
        *out_group = nullptr;
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    return CallCpp(model, [&] {
        const smdl::BodyGroup group =
            smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).findBodyGroup(name ? name : "");
        if (out_group)
            *out_group = group.handle();
    });
}

SMDL_API const char *smdl_bodygroup_name(const smdl_bodygroup *group) { return group ? group->name.c_str() : ""; }

SMDL_API uint32_t smdl_bodygroup_choice_count(const smdl_bodygroup *group) {
    return group ? smdl::BodyGroup(const_cast<smdl_bodygroup *>(group), group->model).choiceCount() : 0;
}

SMDL_API uint32_t smdl_model_bodygroup_count(const smdl_model *model) {
    return model ? smdl::detail::BorrowModel(const_cast<smdl_model *>(model)).bodyGroupCount() : 0;
}

SMDL_API smdl_status smdl_model_add_bodygroup_preset(smdl_model *model, const char *name,
                                                     const smdl_bodygroup *const *groups, const uint32_t *values,
                                                     size_t count) {
    if (!model)
        return SMDL_ERROR_INVALID_ARGUMENT;
    if (count && (!groups || !values))
        return Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "bodygroup preset '{}': groups or values is null", name ? name : "");
    return CallCpp(model, [&] {
        std::vector<std::pair<smdl::BodyGroup, uint32_t>> presetValues;
        for (size_t i = 0; i < count; ++i)
            presetValues.emplace_back(smdl::BodyGroup(const_cast<smdl_bodygroup *>(groups[i]), model), values[i]);
        smdl::detail::BorrowModel(model).addBodyGroupPreset(name ? name : "", presetValues);
    });
}

} // extern "C"

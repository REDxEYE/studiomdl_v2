// studiomdl C++ API: body groups and meshes (implemented in api/src/smdl_body.cpp; smdl_body.h is the C form)
#pragma once

#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>

#include "common.hpp"
#include "flex.hpp"
#include "geometry.hpp"
#include "../c/smdl_body.h"

// Defines smdl::Mesh : public detail::MeshFlexApi (flex.hpp) and smdl::BodyGroup.
namespace smdl {

// A mesh: one choice of a body group, with its geometry. Non-owning view, valid as long as its model lives.
class Mesh : public detail::MeshFlexApi {
public:
    Mesh(smdl_mesh *mesh, smdl_model *model) : m_Mesh(mesh), m_Model(model) {}

    smdl_mesh *handle() const { return m_Mesh; }
    smdl_model *model() const { return m_Model; }

    std::string_view name() const;

    Geometry geometry() const;

    // $appendsource: more geometry merged into this mesh's source (see smdl_mesh_append_geometry)
    Geometry appendGeometry() const;

private:
    smdl_mesh *m_Mesh;
    smdl_model *m_Model;
};

// A body group: a list of choices, each a mesh or a blank. Non-owning view.
class BodyGroup {
public:
    BodyGroup(smdl_bodygroup *group, smdl_model *model) : m_Group(group), m_Model(model) {}

    smdl_bodygroup *handle() const { return m_Group; }
    smdl_model *model() const { return m_Model; }

    std::string_view name() const;
    uint32_t choiceCount() const;
    std::vector<Mesh> choices() const;

    // Adds a mesh as the next choice
    Mesh createMesh(std::string_view name) const;

    // Adds a blank choice (nothing drawn)
    void addBlank() const;

private:
    smdl_bodygroup *m_Group;
    smdl_model *m_Model;
};

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
    class ModelBodyApi {
    public:
        // $body: a body group with one choice, a mesh with the same name
        Mesh createMesh(std::string_view name);

        // $bodygroup
        BodyGroup addBodyGroup(std::string_view name);

        // Throws smdl::Error (SMDL_ERROR_NOT_FOUND) for an unknown name
        Mesh findMesh(std::string_view name) const;
        BodyGroup findBodyGroup(std::string_view name) const;

        uint32_t bodyGroupCount() const;

        // $bodygrouppreset: (group, choice index) pairs
        void addBodyGroupPreset(std::string_view name, std::span<const std::pair<BodyGroup, uint32_t>> values);
        void addBodyGroupPreset(std::string_view name, std::initializer_list<std::pair<BodyGroup, uint32_t>> values) {
            addBodyGroupPreset(name, std::span<const std::pair<BodyGroup, uint32_t>>(values.begin(), values.size()));
        }

    private:
        smdl_model *model() const; // api/src/smdl_model.cpp
    };
}

} // namespace smdl

// studiomdl C++ wrapper: smdl_lod.h
#pragma once

#include "common.hpp"
#include "body.hpp"
#include "geometry.hpp"
#include "../c/smdl_lod.h"

namespace smdl {

// A LOD ($lod or $shadowlod). Non-owning view, valid as long as its model lives. See smdl_lod.h.
class Lod {
public:
    Lod(smdl_lod *lod, smdl_model *model) : m_Lod(lod), m_Model(model) {}

    smdl_lod *handle() const { return m_Lod; }
    smdl_model *model() const { return m_Model; }

    bool isShadow() const;

    // replacemodel: the LOD draws a new geometry instead of the mesh (fill it like a mesh geometry)
    Geometry replaceMesh(const Mesh &mesh) const;
    // ... or an existing geometry of the model (shared by several LODs: its source is built once)
    void replaceMesh(const Mesh &mesh, const Geometry &geometry) const;

    // removemodel: the LOD does not draw the mesh
    void removeMesh(const Mesh &mesh) const;

    // replacebone: vertices weighted to `from` are weighted to `to` in this LOD
    void replaceBone(BoneId from, BoneId to) const;

    // bonetreecollapse: the bone and its subtree are replaced by the bone
    void collapseBoneTree(BoneId bone) const;

    // replacematerial
    void replaceMaterial(std::string_view from, std::string_view to) const;

    // removemesh <material>: the LOD drops every mesh that uses the material
    void removeMaterial(std::string_view material) const;

    // facial / nofacial (a shadow LOD cannot enable it)
    void setFacialAnimation(bool enabled) const;

    // use_shadowlod_materials (shadow LOD only)
    void setUseShadowLodMaterials(bool enabled) const;

private:
    smdl_lod *m_Lod;
    smdl_model *m_Model;
};

namespace detail {
    // A base of smdl::Model (model.hpp); model() is the model handle.
        class ModelLodApi {
    public:
        // $lod <switchDistance>: LOD 1, 2, ... in call order
        Lod addLod(float switchDistance);

        // $shadowlod: the last LOD
        Lod addShadowLod();

        // LODs added (the root LOD is not counted, the shadow LOD is)
        uint32_t lodCount() const;

    private:
        smdl_model *model() const; // api/src/cpp_api.cpp
    };
}

} // namespace smdl

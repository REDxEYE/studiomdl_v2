// Builder front end, LodPhys part: LODs. What a `$lod` / `$shadowlod` block does, through the shared apply functions of
// studiomdl/apply_lod.cpp (the QC parser calls the same ones). The replacement geometries are built into sources with
// BuildSourceFromGeometry where the QC loads the SMD of a `replacemodel`.
#include <algorithm>
#include <format>

#include "apply.hpp"
#include "studiomdl/apply_lod.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"
#include "tier1_v2/function_timing.hpp"

namespace {
    // The name a mesh answers to in a replacemodel / removemodel
    std::string MeshLodName(const smdl_mesh &mesh) { return LodSourceName(mesh.name); }

    // studiomdl matches a LOD's replacement by the model's name without extension: two meshes that look the same that
    // way would both be replaced by one entry
    void CheckUnambiguous(const ApplyState &state, const smdl_mesh &mesh) {
        const std::string name = MeshLodName(mesh);
        const auto equals = [&](const std::string &other) {
            return other.size() == name.size() &&
                   std::ranges::equal(other, name, [](unsigned char a, unsigned char b) {
                       return std::tolower(a) == std::tolower(b);
                   });
        };
        for (const auto &other: state.model.meshes) {
            if (other.get() != &mesh && equals(MeshLodName(*other)))
                MdlError("mesh '{}' cannot be replaced or removed by a LOD: mesh '{}' has the same name before the last '.' "
                         "(studiomdl matches LOD replacements by that name)",
                         mesh.name, other->name);
        }
    }

    void ApplyLod(StudioMdlState &ctx, ApplyState &state, const smdl_lod &desc) {
        ScopedApplyObject object(state, desc.label);

        LodScriptData_t *lodPtr = nullptr;
        switch (ApplyLodBegin(ctx, desc.isShadow, lodPtr)) {
            case LodBeginResult::Ok:
                break;
            case LodBeginResult::ShadowLodNotLast:
                MdlError("the model can only have one shadow LOD and it must be the last LOD");
            case LodBeginResult::TooManyLods:
                MdlError("too many LODs (MAX_NUM_LODS == {})", static_cast<int>(MAX_NUM_LODS));
        }
        LodScriptData_t &lod = *lodPtr;
        if (!desc.isShadow)
            lod.switchValue = desc.switchDistance;

        const SkeletonDesc &skeleton = state.model.skeleton;

        // replacemodel
        for (const smdl_lod::ModelReplacement &entry: desc.modelReplacements) {
            if (!entry.geometry)
                continue;
            ScopedApplyObject meshObject(state, std::format("mesh '{}'", entry.mesh->name));
            CheckUnambiguous(state, *entry.mesh);
            ApplyLodReplaceModel(ctx, lod, MeshLodName(*entry.mesh), entry.geometry->sourceName, [&] {
                return BuildSourceFromGeometry(ctx, state, *entry.geometry, false);
            });
        }

        // removemodel
        for (const smdl_lod::ModelReplacement &entry: desc.modelReplacements) {
            if (entry.geometry)
                continue;
            ScopedApplyObject meshObject(state, std::format("mesh '{}'", entry.mesh->name));
            CheckUnambiguous(state, *entry.mesh);
            ApplyLodRemoveModel(lod, MeshLodName(*entry.mesh));
        }

        // replacebone, bonetreecollapse
        for (const auto &[from, to]: desc.boneReplacements)
            ApplyLodReplaceBone(lod, skeleton.bones[from].name, skeleton.bones[to].name);
        for (const smdl_bone_id bone: desc.boneTreeCollapses)
            ApplyLodBoneTreeCollapse(lod, skeleton.bones[bone].name);

        // replacematerial, removemesh
        for (const auto &[from, to]: desc.materialReplacements)
            ApplyLodReplaceMaterial(ctx, lod, from, to);
        for (const std::string &material: desc.removedMaterials)
            ApplyLodRemoveMesh(lod, material);

        ApplyLodFacialAnimation(lod, desc.facialAnimation);
        if (desc.useShadowLodMaterials)
            ApplyLodUseShadowLodMaterials(ctx);

        ApplyLodEnd(ctx, desc.isShadow);
    }
}

void ApplyLods(StudioMdlState &ctx, ApplyState &state) {
    vstd::FunctionTimingRecorder _("API");
    for (const auto &lod: state.model.lods.lods)
        ApplyLod(ctx, state, *lod);
}

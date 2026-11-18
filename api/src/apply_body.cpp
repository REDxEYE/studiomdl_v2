// Builder front end, Core part: body groups, meshes, blanks, presets, skins. The sequence of calls is that of
// Cmd_Body / Cmd_Bodygroup / ProcessCmdBody: a bodypart, a model per choice whose source comes from the geometry
// (BuildSourceFromGeometry instead of Load_Source), the flex part of the mesh, PostProcessSource. Presets, skins and
// the material override follow, as a QC lists them after the bodies.
#include <format>

#include "apply.hpp"
#include "studiomdl/apply_body.hpp"
#include "studiomdl/apply_globals.hpp"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"

namespace {
    // $appendsource: the appended geometries of the mesh are merged into its source (everything ends up in one mesh
    // with material 0)
    void AppendGeometries(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, s_source_t *source) {
        for (const smdl_geometry *appended: mesh.appended) {
            ScopedApplyObject object(state, appended->owner);
            if (!mesh.geometry->shapes.empty())
                MdlError("{}: shapes cannot be combined with appended geometry (the merge reorders the vertices)",
                         mesh.geometry->owner);

            s_source_t *appendSource = BuildSourceFromGeometry(ctx, state, *appended, false);
            std::optional<AppendSourceOffset> offset;
            if (appended->hasAppendOffset) {
                AppendSourceOffset parsed;
                parsed.position = appended->appendPosition;
                parsed.angles = QAngle(appended->appendAngles.x, appended->appendAngles.y, appended->appendAngles.z);
                parsed.scale = appended->appendScale;
                offset = parsed;
            }
            ApplyAppendSource(ctx, source, appendSource, offset);
        }
    }

    void ApplyMesh(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, s_bodypart_t &bodypart) {
        ScopedApplyObject object(state, std::format("mesh '{}'", mesh.name));

        int imodel;
        s_model_t *pmodel = ApplyBodyModel(ctx, bodypart, imodel);

        // ProcessOptionStudio: the model is named by the studio path and the source comes from the geometry
        BeginStudioOption(ctx, pmodel, mesh.name, mesh.geometry->scale);
        pmodel->source = BuildSourceFromGeometry(ctx, state, *mesh.geometry, true);
        EndStudioOption(ctx);

        AppendGeometries(ctx, state, mesh, pmodel->source);

        state.meshModels[&mesh] = pmodel;
        state.meshModelIndex[&mesh] = imodel;

        ApplyMeshFlex(ctx, state, mesh, pmodel, imodel);

        // Body command should add any flex commands in the source loaded
        PostProcessSource(ctx, pmodel->source, imodel);
    }

    void ApplyBodyGroups(StudioMdlState &ctx, ApplyState &state) {
        for (const auto &group: state.model.bodygroups) {
            ScopedApplyObject object(state, std::format("bodygroup '{}'", group->name));
            if (group->choices.empty())
                MdlError("the bodygroup has no choices (add a mesh or a blank)");

            s_bodypart_t &bodypart = ApplyBodyPart(ctx, group->name);
            for (const smdl_mesh *choice: group->choices) {
                if (choice)
                    ApplyMesh(ctx, state, *choice, bodypart);
                else
                    ApplyBlankModel(ctx, bodypart);
            }
        }
    }

    // $bodygrouppreset: the group values baked into a value and a mask
    void ApplyPresets(StudioMdlState &ctx, ApplyState &state) {
        const auto &groups = state.model.bodygroups;
        for (const BodyGroupPresetDesc &desc: state.model.body.presets) {
            ScopedApplyObject object(state, std::format("bodygroup preset '{}'", desc.name));
            BodyGroupPresetBuilder preset;
            preset.name = std::string_view(desc.name);
            for (const auto &[group, value]: desc.values) {
                size_t index = 0;
                while (index < groups.size() && groups[index].get() != group)
                    ++index;
                // the bodyparts are in the order of the model's groups
                const std::string error = ApplyBodyGroupPresetValue(preset, ctx.body.bodypart[index], static_cast<int>(value));
                if (!error.empty())
                    MdlError("bodygroup '{}': {}", group->name, error);
            }
            ApplyBodyGroupPreset(ctx, preset);
        }
    }
}

void ApplyBody(StudioMdlState &ctx, ApplyState &state) {
    ApplyBodyGroups(ctx, state);
    ApplyPresets(ctx, state);

    const GlobalsDesc &globals = state.model.globals;
    if (!globals.skinFamilies.empty()) {
        ScopedApplyObject object(state, "skins");
        ApplyTextureGroup(ctx, globals.skinFamilies, false);
    }
    if (globals.overrideMaterial)
        ApplyOverrideMaterial(ctx, *globals.overrideMaterial);
}

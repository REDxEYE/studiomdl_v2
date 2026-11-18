// Apply functions for the body QC commands ($body, $bodygroup, $appendblankbodygroup, $bodygrouppreset,
// $appendsource), shared by the QC parsers (studiomdl_commands.cpp) and the builder API front end
// (api/src/apply_body.cpp): the parser turns tokens into typed parameters, these apply them to StudioMdlState.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "studiomdl/studiomdl_global_state.hpp"

// $body <name> ... / $bodygroup <name> {: starts a bodygroup (its value stride follows the groups before it)
s_bodypart_t &ApplyBodyPart(StudioMdlState &ctx, std::string_view name);

// A new selectable model of the bodygroup (the model is registered in ctx.body.model; imodel is its index there).
// Its source is set by the studio option or the builder.
s_model_t *ApplyBodyModel(StudioMdlState &ctx, s_bodypart_t &bodypart, int &imodel);

// The first and last part of the studio option's processing around the load of the source: BeginStudioOption names
// the model (the source path or mesh name), sets its scale and the scale of the loader (scale 0 = the $scale);
// EndStudioOption restores the loader's scale to the $scale of the QC.
void BeginStudioOption(StudioMdlState &ctx, s_model_t *pmodel, std::string_view name, float scale);
void EndStudioOption(StudioMdlState &ctx);

// `blank`: a choice without geometry
void ApplyBlankModel(StudioMdlState &ctx, s_bodypart_t &bodypart);

// $bodygrouppreset: the values are baked into a value and a mask as the groups are named
struct BodyGroupPresetBuilder {
    StudioName name;
    int value = 0;
    int mask = 0;
};
bool HasBodyGroupPreset(const StudioMdlState &ctx, std::string_view name);
// "<bodygroup> <value>" of the preset: returns an error text (empty = fine) when the value is not a choice
std::string ApplyBodyGroupPresetValue(BodyGroupPresetBuilder &preset, const s_bodypart_t &bodypart, int value);
void ApplyBodyGroupPreset(StudioMdlState &ctx, const BodyGroupPresetBuilder &preset);

// $appendsource <file> [offset pos[...] angle[...] scale[...]]: merges `append` into `original` (one mesh, material 0).
// Without an offset the appended vertices are scaled by the current $scale; with one the matrix is the offset
// (position, QAngle degrees) and the scale `scale` relative to the current $scale, exactly as the QC command.
struct AppendSourceOffset {
    Vector position;
    QAngle angles;
    float scale = 1.0f;
};
void ApplyAppendSource(StudioMdlState &ctx, s_source_t *original, s_source_t *append,
                       const std::optional<AppendSourceOffset> &offset);

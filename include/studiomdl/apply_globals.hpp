// Apply functions for the model-wide QC commands ($cdmaterials, $surfaceprop, $contents, flags, $bbox, ...), shared by
// the QC parsers (studiomdl_commands.cpp) and the builder API front end (api/src/apply_globals.cpp): the parser turns
// tokens into typed parameters, these apply them to StudioMdlState. Each function documents the QC command it is.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "studiomdl/studiomdl_global_state.hpp"

class ScriptTokenizer;

// ---------------------------------------------------------------------------------------------------------------
// Paths, transforms
// ---------------------------------------------------------------------------------------------------------------

// $internalname <path>
void ApplyInternalName(StudioMdlState &ctx, std::string_view name);

// One path of $cdmaterials: a trailing '/' is added when missing, separators are fixed (Q_FixSlashes), the path is
// appended to ctx.materials.cdtextures (at most 16, the caller checks).
void ApplyCdMaterial(StudioMdlState &ctx, std::string_view path);

// $scale <factor>: the default scale of every source after it
void ApplyScale(StudioMdlState &ctx, float scale);

// $origin <x> <y> <z> [<angle>]: the offset is ApplyOrigin, the optional rotation (degrees around Z, the stored value
// is angle + 90) is ApplyOriginAngle
void ApplyOrigin(StudioMdlState &ctx, float x, float y, float z);
void ApplyOriginAngle(StudioMdlState &ctx, float degrees);

// $upaxis x|-x|y|-y|z|-z
enum class UpAxis { X, NegX, Y, NegY, Z, NegZ };
bool ParseUpAxis(std::string_view text, UpAxis &axis); // case-insensitive; false (axis unchanged) when unknown
// The default root rotation of an axis (before the built-in 90 degree Z rotation of "z")
RadianEuler UpAxisAngles(UpAxis axis);
void ApplyUpAxis(StudioMdlState &ctx, UpAxis axis);
// The rotation form: ProcessUpAxis(ctx, angles), also used by the .mpp loader
void ProcessUpAxis(StudioMdlState &ctx, const RadianEuler &angles);

// $autocenter
void ApplyAutocenter(StudioMdlState &ctx);
// $centerbonesonverts
void ApplyCenterBonesOnVerts(StudioMdlState &ctx);

// ---------------------------------------------------------------------------------------------------------------
// Surface properties and contents
// ---------------------------------------------------------------------------------------------------------------

// $surfaceprop and $jointsurfaceprop need no apply function: SetDefaultSurfaceProp / AddSurfaceProp (studiomdl.h)
// already are the shared code.

// The words of $contents / $jointcontents as bits. grate = GRATE and not SOLID, ladder, solid, monster, notsolid =
// not SOLID; a remove wins over an add of the same bit.
enum ContentsKeyword : uint32_t {
    CONTENTS_KEYWORD_SOLID = 1,
    CONTENTS_KEYWORD_GRATE = 2,
    CONTENTS_KEYWORD_LADDER = 4,
    CONTENTS_KEYWORD_MONSTER = 8,
    CONTENTS_KEYWORD_NOTSOLID = 16,
    CONTENTS_KEYWORD_ALL = 31,
};
// The keyword of one word (case-insensitive); 0 for an unknown word (the QC parser ignores those)
uint32_t ParseContentsKeyword(std::string_view word);
// The flags a set of keywords adds and removes
void ContentsKeywordsToFlags(uint32_t keywords, ContentType &addFlags, ContentType &removeFlags);

// $contents <words...>: starts from the current default (SOLID), adds/removes the flags
void ApplyContents(StudioMdlState &ctx, uint32_t keywords);
// $jointcontents <bone> <words...>: the joint's contents start from SOLID, then the flags apply
void ApplyJointContents(StudioMdlState &ctx, std::string_view boneName, uint32_t keywords);

// ---------------------------------------------------------------------------------------------------------------
// Header values and flags
// ---------------------------------------------------------------------------------------------------------------

// $includemodel <path>: models/<path> (at most 128, the caller checks)
void ApplyIncludeModel(StudioMdlState &ctx, std::string_view path);

// Appends already formatted key values text (what Option_KeyValues produces) to the model's $keyvalues block
void ApplyKeyValues(StudioMdlState &ctx, std::string_view formattedText);
// Parses the content of a $keyvalues { ... } block (without the outer braces) the way the QC parser does and returns
// the text stored in the .mdl. Returns false and sets `error` when the block is malformed (unbalanced braces).
bool FormatKeyValuesText(std::string_view text, std::string &formatted, std::string &error);
// The QC block parser (reads "{ ... }" from the tokenizer and appends the formatted text)
void Option_KeyValues(ScriptTokenizer &qc, std::string &keyValues);

// $eyeposition <a> <b> <c> (the values as written in the QC: stored as (-b, a, c))
void ApplyEyePosition(StudioMdlState &ctx, float a, float b, float c);

// $illumposition <x> <y> <z> [<attachment bone>]; an empty bone is the form without attachment. With a bone an
// attachment "__illumPosition" (rigid) is appended to ctx.bones.attachments, so it takes the next attachment index.
void ApplyIllumPosition(StudioMdlState &ctx, float x, float y, float z, std::string_view attachmentBone);

// $maxeyedeflection <degrees>
void ApplyMaxEyeDeflection(StudioMdlState &ctx, float degrees);

// $gamma <value>
void ApplyGamma(StudioMdlState &ctx, float gamma);

// $bbox <min xyz> <max xyz>, $cbox <min xyz> <max xyz>, $bboxonlyverts, $skipboneinbbox
void ApplyBBox(StudioMdlState &ctx, const Vector &mins, const Vector &maxs);
void ApplyCBox(StudioMdlState &ctx, const Vector &mins, const Vector &maxs);
void ApplyBBoxOnlyVerts(StudioMdlState &ctx);
void ApplySkipBoneInBBox(StudioMdlState &ctx);

// $opaque, $mostlyopaque ($opaque wins whichever comes first). $staticprop is ProcessStaticProp (studiomdl_commands.h)
void ApplyOpaque(StudioMdlState &ctx);
void ApplyMostlyOpaque(StudioMdlState &ctx);

// $ambientboost, $noforcedfade, $donotcastshadows, $casttextureshadows, $subd, $obsolete, $forcephonemecrossfade
void ApplyAmbientBoost(StudioMdlState &ctx);
void ApplyNoForcedFade(StudioMdlState &ctx);
void ApplyDoNotCastShadows(StudioMdlState &ctx);
void ApplyCastTextureShadows(StudioMdlState &ctx);
void ApplySubdivisionSurface(StudioMdlState &ctx);
void ApplyObsolete(StudioMdlState &ctx);
void ApplyForcePhonemeCrossfade(StudioMdlState &ctx);

// $constantdirectionallight <dot 0..1>
void ApplyConstantDirectionalLight(StudioMdlState &ctx, float dot);

// $collisionprecision <value>
void ApplyCollisionPrecision(StudioMdlState &ctx, float precision);

// ---------------------------------------------------------------------------------------------------------------
// Compile options
// ---------------------------------------------------------------------------------------------------------------

// $zbrush, $preservetriangleorder, $cliptotextures, $lcaseallsequences, $erroronsequenceremappingfailure,
// $modelhasnosequences, $calctransitions
void ApplyZBrush(StudioMdlState &ctx);
void ApplyPreserveTriangleOrder(StudioMdlState &ctx);
void ApplyClipToTextures(StudioMdlState &ctx);
void ApplyLCaseAllSequences(StudioMdlState &ctx);
void ApplyErrorOnSeqRemapFail(StudioMdlState &ctx);
void ApplyModelIntentionallyHasZeroSequences(StudioMdlState &ctx);
void ApplyCalcTransitions(StudioMdlState &ctx);

// $flexoptimize auto|on|off [<min static verts>] and -flexopt / -flexopt_min_static
void ApplyFlexOptimizeMode(StudioMdlState &ctx, FlexOptimizeMode_t mode);
void ApplyFlexOptimizeMinStatic(StudioMdlState &ctx, int minStatic);

// $maxverts <limit> [<clamp target>]: both are clamped to 1024..MAXSTUDIOVERTS, without a target it is half the limit
// at most
void ApplyMaxVerts(StudioMdlState &ctx, int limit, std::optional<int> clampTarget);

// $minlod <n> / -minlod <n> and $allowrootlods <n>: only ctx.options; a minlod above the allowed root LODs raises
// them (with a warning)
void ApplyMinLod(StudioMdlState &ctx, int minLod);
void ApplyAllowRootLods(StudioMdlState &ctx, int numAllowedRootLods);

// $motionrollback <value>, $defaultfadein <seconds>, $defaultfadeout <seconds>
void ApplyMotionRollback(StudioMdlState &ctx, float rollback);
void ApplyDefaultFadeInTime(StudioMdlState &ctx, float seconds);
void ApplyDefaultFadeOutTime(StudioMdlState &ctx, float seconds);

// $clampworldspace <min xyz> <max xyz>
void ApplyClampWorldspace(StudioMdlState &ctx, const Vector &mins, const Vector &maxs);

// $sectionframes <frames> <min frames>
void ApplySectionFrames(StudioMdlState &ctx, int frames, int minFrameLimit);

// $allowactivityname <name>
void ApplyAllowActivityName(StudioMdlState &ctx, std::string_view name);

// $animblocksize <size> [nostall] [highres|lowres] [numframes <n>] [cachehighres] [posdelta <value>]: the options in
// the order they were given (the last of highres/lowres wins)
struct AnimBlockSettings {
    enum class Resolution { Default, High, Low };

    int size = 0; // below 1024 it is in KB
    bool noStall = false;
    Resolution resolution = Resolution::Default;
    std::optional<int> numFrames; // clamped to 1..4
    bool cacheHighRes = false;
    std::optional<float> posDelta;
};
void ApplyAnimBlockSize(StudioMdlState &ctx, const AnimBlockSettings &settings);

// $skiptransition <node...>: every ordered pair of different nodes (at most 10 names). Nodes are created by name
// (LookupXNode) in the order they are first named, so the equivalent QC puts this command after the sequences.
void ApplySkipTransition(StudioMdlState &ctx, const std::vector<std::string> &nodeNames);

// ---------------------------------------------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------------------------------------------

// $texturegroup skinfamilies { { default skin materials } { family 1 } ... }: rows[family][index] = material name.
// Row 0 is the default skin. relativePaths is the flag LookupTexture takes (QC: false). Appends one group to
// ctx.materials.texturegroups (at most 32); only the first group is used.
void ApplyTextureGroup(StudioMdlState &ctx, const std::vector<std::vector<std::string>> &rows, bool relativePaths);

// $overridematerial <name>: every material reference of the sources loaded so far becomes <name>
void ApplyOverrideMaterial(StudioMdlState &ctx, std::string_view name);

// -t <replacement> [<texture it replaces>]: loaders replace the named texture (every texture without a name)
void ApplyTextureReplacement(StudioMdlState &ctx, std::string_view replacement, std::string_view replaces);

// -a <degrees>: the angle under which equal-position vertices merge their normals
void ApplyNormalBlendAngle(StudioMdlState &ctx, float degrees);

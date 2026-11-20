// Builder front end, Core part: geometry -> s_source_t. This replays what Load_Source / Load_SMD do for an SMD file
// (skeleton nodes, the "BindPose" frame, the triangles through the same vertex unification, FlipFacing for `reverse`),
// calling the helpers the loader itself uses (studiomdl.h: SetSourceNode, SetSourceAnimBone, AddSourceCorner, ...), so
// equal data gives a byte-identical source.
#include <algorithm>
#include <format>

#include "apply.hpp"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_errors.h"
#include "tier1_v2/path_utils.hpp"

namespace {
    // Sets ctx.script.currentscale for the duration of one source (the `scale` option of studio)
    class ScopedSourceScale {
    public:
        ScopedSourceScale(StudioMdlState &ctx, float scale) : m_Ctx(ctx), m_Previous(ctx.script.currentscale) {
            ctx.script.currentscale = scale;
        }
        ~ScopedSourceScale() { m_Ctx.script.currentscale = m_Previous; }
        ScopedSourceScale(const ScopedSourceScale &) = delete;
        ScopedSourceScale &operator=(const ScopedSourceScale &) = delete;

    private:
        StudioMdlState &m_Ctx;
        float m_Previous;
    };

    // "<key>.smd": the key is the mesh name without an extension, as Load_Source derives it from a studio path, so a
    // later Load_Source of that name finds this source in the cache. Geometries of other parts without a name use their
    // owner text.
    std::string SourceFileName(const smdl_geometry &geometry) {
        std::string key = geometry.sourceName.empty() ? geometry.owner : geometry.sourceName;
        vstd::paths::strip_extension(key);
        vstd::paths::fix_slashes(key); // LoadSource looks the name up with the platform's slashes
        return key + ".smd";
    }

    // The uv sets present, in order (a gap is an error)
    size_t UvSetCount(const smdl_geometry &g) {
        size_t count = 0;
        for (size_t set = 0; set < kMaxUvSets; ++set) {
            if (!g.uvs[set].empty() || !g.cornerUvs[set].empty())
                count = set + 1;
        }
        return count;
    }

    bool HasUvSet(const smdl_geometry &g, size_t set) { return !g.uvs[set].empty() || !g.cornerUvs[set].empty(); }

    void Validate(const ApplyState &state, const smdl_geometry &g) {
        const std::string &who = g.owner;
        if (state.model.skeleton.bones.empty())
            MdlError("{}: the model has no bones (add the skeleton with smdl_model_add_bone first)", who);
        if (g.positions.empty())
            MdlError("{}: no vertices (smdl_geometry_set_positions)", who);
        if (g.indices.empty() && g.quadIndices.empty())
            MdlError("{}: no triangles (smdl_geometry_set_triangles)", who);
        if (g.normals.empty() && (g.cornerNormals.empty() || !g.quadIndices.empty()))
            MdlError("{}: no normals (smdl_geometry_set_normals, or smdl_geometry_set_corner_normals for triangles)", who);
        if (!g.quadIndices.empty() && !state.model.globals.subd)
            MdlError("{}: quads need the model to be a subdivision surface (smdl_model_set_subd)", who);
        if (g.materials.empty())
            MdlError("{}: no materials (smdl_geometry_add_material)", who);
        const size_t uvSets = UvSetCount(g);
        for (size_t set = 0; set + 1 < uvSets; ++set) {
            if (!HasUvSet(g, set))
                MdlError("{}: UV set {} is set but set {} is not (UV sets are filled from 0 upwards)", who, uvSets - 1, set);
        }
    }

    // The skeleton of the source: the model skeleton with the rest pose overrides of the geometry (SMD "nodes" and the
    // "skeleton" block with the single frame `time 0`, loaded as the "BindPose" animation)
    void BuildSkeleton(StudioMdlState &ctx, const ApplyState &state, const smdl_geometry &g, s_source_t *source) {
        const std::vector<BoneDesc> &bones = state.model.skeleton.bones;
        source->numbones = static_cast<int>(bones.size());

        InitSourceNodes(source->localBone);
        for (size_t i = 0; i < bones.size(); ++i) {
            SetSourceNode(source->localBone, static_cast<int>(i), bones[i].name,
                          bones[i].parent == SMDL_NO_BONE ? -1 : static_cast<int>(bones[i].parent));
        }

        s_sourceanim_t *anim = FindOrAddSourceAnim(source, "BindPose");
        anim->startframe = -1;
        int frame = 0;
        SetSourceAnimTime(ctx, source, anim, 0, frame);
        for (size_t i = 0; i < bones.size(); ++i) {
            Vector position = bones[i].position;
            RadianEuler rotation = bones[i].rotation;
            for (const RestPoseOverride &override: g.restOverrides) {
                if (override.bone == i) {
                    position = override.position;
                    rotation = override.rotation;
                }
            }
            SetSourceAnimBone(ctx, anim, frame, static_cast<int>(i), position, rotation);
        }
        const int missing = FinishSourceAnim(source, anim, "BindPose");
        if (missing >= 0)
            MdlError("{}: the bind pose is missing frame {}", g.owner, missing);
    }

    // One triangle corner or quad corner as the loader's SourceCorner. corner = index into the corner arrays
    // (3 per triangle) or -1 when the corner has no per-corner data (quads).
    void FillCorner(const smdl_geometry &g, uint32_t vertex, int64_t corner, SourceCorner &out) {
        out.position = g.positions[vertex];
        out.normal = (corner >= 0 && !g.cornerNormals.empty()) ? g.cornerNormals[corner] : g.normals[vertex];

        Vector2D uv(0.0f, 0.0f);
        if (corner >= 0 && !g.cornerUvs[0].empty())
            uv = g.cornerUvs[0][corner];
        else if (!g.uvs[0].empty())
            uv = g.uvs[0][vertex];
        out.uv = uv;

        // the additional sets travel as extra floats (u, v pairs) that the SMD loader stores as they are (no v flip)
        out.numExtras = 0;
        for (size_t set = 1; set < kMaxUvSets; ++set) {
            Vector2D extra;
            if (corner >= 0 && !g.cornerUvs[set].empty())
                extra = g.cornerUvs[set][corner];
            else if (!g.uvs[set].empty())
                extra = g.uvs[set][vertex];
            else
                break;
            out.extras[out.numExtras++] = extra.x;
            out.extras[out.numExtras++] = extra.y;
        }

        if (g.influencesPerVertex == 0) {
            // rigid to the first bone
            out.bone = 0;
            out.numLinks = 0;
        }
        else {
            const size_t base = static_cast<size_t>(vertex) * g.influencesPerVertex;
            out.numLinks = static_cast<int>(g.influencesPerVertex);
            out.bone = static_cast<int>(g.weightBones[base]);
            for (uint32_t i = 0; i < g.influencesPerVertex; ++i) {
                out.bones[i] = static_cast<int>(g.weightBones[base + i]);
                out.weights[i] = g.weights[base + i];
            }
        }
    }
}

s_source_t *BuildSourceFromGeometry(StudioMdlState &ctx, ApplyState &state, const smdl_geometry &geometry,
                                    bool isActiveModel) {
    if (const auto cached = state.sources.find(&geometry); cached != state.sources.end()) {
        if (isActiveModel)
            cached->second->isActiveModel = true;
        return cached->second;
    }

    Validate(state, geometry);
    if (ctx.anim.sources.size() >= ctx.anim.sources.capacity())
        MdlError("{}: too many sources, max {}", geometry.owner, ctx.anim.sources.capacity());

    // an appended geometry ($appendsource) is loaded without a scale option: the model's $scale
    const float scale = (geometry.scale != 0.0f && !geometry.appended) ? geometry.scale : ctx.script.defaultscale;
    ScopedSourceScale sourceScale(ctx, scale);

    // Load_Source
    s_source_t *source = ctx.arena.Create<s_source_t>();
    ctx.anim.sources.emplace_back(source);
    source->isActiveModel = isActiveModel;
    source->adjust = ctx.script.defaultadjust;
    source->scale = 1.0f;
    source->rotation = ctx.script.defaultrotation;
    source->filename = SourceFileName(geometry);

    // Load_SMD: version, nodes, skeleton, triangles. Version 3 is the one with additional texcoord sets; version 2 the
    // one with materials relative to materials/ (what the materials of a DMX file are: ResolveSourceMaterial). Neither
    // is read anywhere else in the builder's path, so version 2 also serves a geometry with additional sets.
    source->version = geometry.relativeMaterials ? 2 : (UvSetCount(geometry) > 1 ? 3 : 1);
    BuildSkeleton(ctx, state, geometry, source);

    BeginSourceTriangles(ctx);

    // geometry vertices of each unified vertex (for the user tangents and the DMX delta states); corners of different
    // vertices with equal data unify into one
    std::vector<std::vector<uint32_t>> entryVertices;
    SourceCorner corner;
    auto addCorner = [&](int material, uint32_t vertex, int64_t cornerIndex) {
        FillCorner(geometry, vertex, cornerIndex, corner);
        const int index = AddSourceCorner(ctx, source, material, corner);
        if (static_cast<size_t>(index) >= entryVertices.size())
            entryVertices.resize(static_cast<size_t>(index) + 1);
        std::vector<uint32_t> &vertices = entryVertices[static_cast<size_t>(index)];
        if (std::ranges::find(vertices, vertex) == vertices.end())
            vertices.push_back(vertex);
        return index;
    };

    const size_t triangleCount = geometry.indices.size() / 3;
    for (size_t t = 0; t < triangleCount; ++t) {
        const smdl_material_id materialId = geometry.triangleMaterials.empty() ? 0 : geometry.triangleMaterials[t];
        const int material = ResolveSourceMaterial(ctx, source, geometry.materials[materialId]);
        if (material < 0)
            continue;
        std::array<int, 3> indices;
        for (size_t c = 0; c < 3; ++c)
            indices[c] = addCorner(material, geometry.indices[t * 3 + c], static_cast<int64_t>(t * 3 + c));
        AddSourceTriangle(ctx, material, indices);
    }

    const size_t quadCount = geometry.quadIndices.size() / 4;
    for (size_t q = 0; q < quadCount; ++q) {
        const smdl_material_id materialId = geometry.quadMaterials.empty() ? 0 : geometry.quadMaterials[q];
        const int material = ResolveSourceMaterial(ctx, source, geometry.materials[materialId]);
        if (material < 0)
            continue;
        std::array<int, 4> indices;
        for (size_t c = 0; c < 4; ++c)
            indices[c] = addCorner(material, geometry.quadIndices[q * 4 + c], -1);
        // the order of the DMX loader's quads: corners 0, 3, 2, 1
        AddSourceQuad(ctx, material, {indices[0], indices[3], indices[2], indices[1]});
    }

    EndSourceTriangles(ctx, source);

    std::vector<std::vector<int>> &sourceVertices = state.sourceVertices[&geometry];
    sourceVertices.assign(geometry.positions.size(), {});
    for (int i = 0; i < source->numvertices; ++i) {
        const std::vector<uint32_t> &vertices = entryVertices[ctx.input.unifiedVertexOrder[i]];
        for (const uint32_t vertex: vertices)
            sourceVertices[vertex].push_back(i);
        if (!geometry.tangents.empty())
            source->vertex[i].tangentS = geometry.tangents[vertices.front()];
    }

    // Load_Source: `reverse`
    if (geometry.flipTriangles)
        FlipFacing(source);

    state.sources.emplace(&geometry, source);
    BuildShapeAnimations(ctx, state, geometry, source);
    return source;
}

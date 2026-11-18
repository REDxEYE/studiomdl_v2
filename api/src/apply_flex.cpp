// Builder front end, Flex part: shapes -> vertex animation, the per-mesh options of the `$model` block (ApplyMeshFlex)
// and the model-wide flex data (ApplyFlex). Both call the apply functions the QC parsers use (studiomdl/apply_flex.hpp,
// ApplyFlexKey, ApplyEyeball, ...), so equal data gives byte-identical output.
//
// A shape is one frame of a VTA: the first request of a flex/eyelid creates a bone-copy source "<mesh>.vta" with the
// animation "VertexAnimation" (frame 0 = the base pose of the pooled vertices, frame k+1 = shape k) and the flex keys
// point at it, exactly as Load_VTA's source looks after `flexfile`. DMX eyelids get the shapes as new-style (delta
// state) animations of the mesh's own source instead.
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <cmath>
#include <format>
#include <map>
#include <unordered_map>

#include "apply.hpp"
#include "common/scriplib.h"
#include "studiomdl/apply_flex.hpp"
#include "studiomdl/compile_error.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl_commands.h"
#include "studiomdl_errors.h"
#include "tier1_v2/path_utils.hpp"

namespace {
    constexpr const char *kStateName[kEyelidTypeCount] = {"lowerer", "neutral", "raiser"};

    // The error text of an apply function ("" = success) becomes the compile error
    void Check(const std::string &error) {
        if (!error.empty())
            MdlError("{}", error);
    }

    // Exact position as a hash key (-0 and 0 are one position)
    struct PositionKey {
        std::array<uint32_t, 3> bits{};

        explicit PositionKey(const Vector &position) {
            for (int i = 0; i < 3; ++i) {
                const float value = position[i] == 0.0f ? 0.0f : position[i];
                std::memcpy(&bits[i], &value, sizeof(float));
            }
        }
        bool operator==(const PositionKey &) const = default;
    };

    struct PositionKeyHash {
        size_t operator()(const PositionKey &key) const {
            size_t hash = 0xcbf29ce484222325ull;
            for (const uint32_t value: key.bits)
                hash = (hash ^ value) * 0x100000001b3ull;
            return hash;
        }
    };

    // The scale the geometry's source was built with (see BuildSourceFromGeometry)
    float GeometryScale(const StudioMdlState &ctx, const smdl_geometry &geometry) {
        return (geometry.scale != 0.0f && !geometry.appended) ? geometry.scale : ctx.script.defaultscale;
    }

    Vector Scaled(const Vector &value, float scale) {
        Vector result = value;
        result *= scale;
        return result;
    }

    // Normal of a corner as the source builder reads it (corner = index in the corner arrays, -1 when none)
    const Vector &CornerNormal(const smdl_geometry &g, uint32_t vertex, int64_t corner) {
        return (corner >= 0 && !g.cornerNormals.empty()) ? g.cornerNormals[corner] : g.normals[vertex];
    }

    // The distinct source positions of the geometry's corners in first-corner order, with the normal of the first
    // corner there (the vertices of a VTA). Corners whose position is not a vertex of the source (triangles the source
    // builder dropped) are left out.
    struct VertexPool {
        std::vector<Vector> positions; // scaled
        std::vector<Vector> normals;
        std::unordered_map<PositionKey, uint32_t, PositionKeyHash> index;
    };

    VertexPool BuildPool(const StudioMdlState &ctx, const smdl_geometry &g, const s_source_t *base) {
        const float scale = GeometryScale(ctx, g);
        std::unordered_map<PositionKey, bool, PositionKeyHash> inSource;
        for (int i = 0; i < base->numvertices; ++i)
            inSource.emplace(PositionKey(base->vertex[i].position), true);

        VertexPool pool;
        auto addCorner = [&](uint32_t vertex, int64_t corner) {
            const Vector position = Scaled(g.positions[vertex], scale);
            const PositionKey key(position);
            if (!inSource.contains(key) || pool.index.contains(key))
                return;
            pool.index.emplace(key, static_cast<uint32_t>(pool.positions.size()));
            pool.positions.push_back(position);
            pool.normals.push_back(CornerNormal(g, vertex, corner));
        };
        for (size_t c = 0; c < g.indices.size(); ++c)
            addCorner(g.indices[c], static_cast<int64_t>(c));
        for (const uint32_t vertex: g.quadIndices)
            addCorner(vertex, -1);
        return pool;
    }

    // What one shape does to one pooled position
    struct Delta {
        Vector position{0, 0, 0}; // unscaled delta
        Vector normal{0, 0, 0};
        float wrinkle = 0.0f;
        uint32_t vertex = 0; // the geometry vertex the values came from (messages)
    };

    // The shape's deltas by pooled index (position -> index through `lookup`). Two vertices at one position must give
    // the same position delta; the first one's normal delta and wrinkle win.
    template<class Lookup>
    std::map<uint32_t, Delta> ShapeDeltas(const StudioMdlState &ctx, const smdl_geometry &g, const GeometryShapeDesc &shape,
                                          Lookup &&lookup) {
        const float scale = GeometryScale(ctx, g);
        std::map<uint32_t, Delta> result;
        for (size_t i = 0; i < shape.vertices.size(); ++i) {
            const uint32_t vertex = shape.vertices[i];
            if (vertex >= g.positions.size())
                MdlError("{}: shape '{}': vertex index {} is not a vertex ({} vertices)", g.owner, shape.name, vertex,
                         g.positions.size());
            const auto found = lookup(PositionKey(Scaled(g.positions[vertex], scale)));
            if (!found)
                continue; // a vertex no triangle of the source uses
            Delta delta;
            delta.vertex = vertex;
            delta.position = shape.positionDeltas[i];
            if (!shape.normalDeltas.empty())
                delta.normal = shape.normalDeltas[i];
            if (!shape.wrinkle.empty())
                delta.wrinkle = shape.wrinkle[i];
            const auto [it, inserted] = result.emplace(*found, delta);
            if (!inserted) {
                const Vector difference = it->second.position - delta.position;
                if (std::abs(difference.x) > 1e-4f || std::abs(difference.y) > 1e-4f || std::abs(difference.z) > 1e-4f)
                    MdlError("{}: shape '{}': vertices {} and {} are at one position but have different position deltas",
                             g.owner, shape.name, it->second.vertex, vertex);
            }
        }
        return result;
    }

    std::string ShapeSourceName(const smdl_geometry &geometry) {
        std::string key = geometry.sourceName.empty() ? geometry.owner : geometry.sourceName;
        vstd::paths::strip_extension(key);
        return key + ".vta";
    }

    void CheckShapes(const smdl_geometry &g) {
        for (const GeometryShapeDesc &shape: g.shapes) {
            for (const uint32_t vertex: shape.vertices) {
                if (vertex >= g.positions.size())
                    MdlError("{}: shape '{}': vertex index {} is not a vertex ({} vertices)", g.owner, shape.name,
                             vertex, g.positions.size());
            }
        }
    }

    // The VTA-like source of the mesh's geometry, built on first use (Load_VTA's result: nodes, a skeleton frame per
    // vertex animation frame, the "vertexanimation" frames)
    s_source_t *EnsureShapeSource(StudioMdlState &ctx, const smdl_mesh &mesh, s_model_t *pmodel) {
        const smdl_geometry &g = *mesh.geometry;
        if (!mesh.appended.empty())
            MdlError("{}: shapes cannot be combined with appended geometries", g.owner);
        const std::string name = ShapeSourceName(g);
        for (s_source_t *existing: ctx.anim.sources) {
            if (existing->filename == name)
                return existing;
        }

        const s_source_t *base = pmodel->source;
        CheckShapes(g);
        if (ctx.anim.sources.size() >= ctx.anim.sources.capacity())
            MdlError("{}: too many sources, max {}", g.owner, ctx.anim.sources.capacity());
        const VertexPool pool = BuildPool(ctx, g, base);
        if (pool.positions.empty())
            MdlError("{}: no vertices for the shapes", g.owner);

        s_source_t *source = ctx.arena.Create<s_source_t>();
        ctx.anim.sources.emplace_back(source);
        source->filename = name;
        source->adjust = ctx.script.defaultadjust;
        source->scale = 1.0f;
        source->rotation = ctx.script.defaultrotation;
        source->numbones = base->numbones;
        source->localBone = base->localBone;

        // "skeleton": one frame per vertex animation frame, all the base pose
        const s_sourceanim_t *bindPose = FindSourceAnim(base, "BindPose");
        s_sourceanim_t *anim = FindOrAddSourceAnim(source, "VertexAnimation");
        anim->startframe = -1;
        const int frameCount = static_cast<int>(g.shapes.size()) + 1;
        for (int t = 0; t < frameCount; ++t) {
            int frame = 0;
            SetSourceAnimTime(ctx, source, anim, t, frame);
            if (t == 0) {
                for (int b = 0; b < source->numbones; ++b) {
                    anim->rawanim[0][b].pos = bindPose->rawanim[0][b].pos;
                    anim->rawanim[0][b].rot = bindPose->rawanim[0][b].rot;
                }
            }
        }
        if (FinishSourceAnim(source, anim, "VertexAnimation") >= 0)
            MdlError("{}: the vertex animation is missing a frame", g.owner);

        // "vertexanimation": frame 0 lists every vertex, the others the changed ones (absolute values)
        source->numvertices = static_cast<int>(pool.positions.size());
        anim->numvanims[0] = source->numvertices;
        anim->vanim[0] = ctx.arena.CreateArray<s_vertanim_t>(source->numvertices);
        for (int i = 0; i < source->numvertices; ++i) {
            anim->vanim[0][i].vertex = i;
            anim->vanim[0][i].pos = pool.positions[i];
            anim->vanim[0][i].normal = pool.normals[i];
        }
        const float scale = GeometryScale(ctx, g);
        auto lookup = [&pool](const PositionKey &key) -> std::optional<uint32_t> {
            const auto it = pool.index.find(key);
            if (it == pool.index.end())
                return std::nullopt;
            return it->second;
        };
        for (size_t k = 0; k < g.shapes.size(); ++k) {
            const GeometryShapeDesc &shape = g.shapes[k];
            const std::map<uint32_t, Delta> deltas = ShapeDeltas(ctx, g, shape, lookup);
            const int frame = static_cast<int>(k) + 1;
            anim->numvanims[frame] = static_cast<int>(deltas.size());
            anim->vanim[frame] = ctx.arena.CreateArray<s_vertanim_t>(deltas.size());
            int i = 0;
            for (const auto &[pooled, delta]: deltas) {
                s_vertanim_t &vanim = anim->vanim[frame][i++];
                vanim.vertex = static_cast<int>(pooled);
                // the position of the pooled vertex is scaled already; the delta moves the unscaled position
                vanim.pos = pool.positions[pooled] + Scaled(delta.position, scale);
                vanim.normal = pool.normals[pooled] + delta.normal;
                vanim.wrinkle = delta.wrinkle;
            }
        }
        anim->numframes = frameCount;
        return source;
    }

    // The shape as a new-style vertex animation (DMX delta state) of the mesh's own source, named like the shape
    void EnsureDeltaState(StudioMdlState &ctx, const smdl_mesh &mesh, s_source_t *source, uint32_t shapeIndex) {
        const smdl_geometry &g = *mesh.geometry;
        if (!mesh.appended.empty())
            MdlError("{}: shapes cannot be combined with appended geometries", g.owner);
        const GeometryShapeDesc &shape = g.shapes[shapeIndex];
        if (const s_sourceanim_t *existing = FindSourceAnim(source, shape.name)) {
            if (!existing->newStyleVertexAnimations)
                MdlError("{}: shape '{}': the source has an animation of that name already", g.owner, shape.name);
            return;
        }

        // an id per distinct position of the source's vertices, and the vertices at each
        std::unordered_map<PositionKey, uint32_t, PositionKeyHash> ids;
        std::vector<std::vector<int>> verticesAt;
        for (int i = 0; i < source->numvertices; ++i) {
            const auto [it, inserted] = ids.emplace(PositionKey(source->vertex[i].position),
                                                    static_cast<uint32_t>(verticesAt.size()));
            if (inserted)
                verticesAt.emplace_back();
            verticesAt[it->second].push_back(i);
        }
        auto lookup = [&ids](const PositionKey &key) -> std::optional<uint32_t> {
            const auto it = ids.find(key);
            if (it == ids.end())
                return std::nullopt;
            return it->second;
        };
        const std::map<uint32_t, Delta> byPosition = ShapeDeltas(ctx, g, shape, lookup);

        struct Entry {
            int vertex;
            Delta delta;
        };
        std::vector<Entry> entries;
        for (const auto &[id, delta]: byPosition) {
            for (const int vertex: verticesAt[id])
                entries.push_back({vertex, delta});
        }
        std::ranges::sort(entries, {}, &Entry::vertex);

        s_sourceanim_t *anim = FindOrAddSourceAnim(source, shape.name.c_str());
        anim->numframes = 1;
        anim->startframe = 0;
        anim->endframe = 0;
        anim->newStyleVertexAnimations = true;
        anim->numvanims[0] = static_cast<int>(entries.size());
        anim->vanim[0] = ctx.arena.CreateArray<s_vertanim_t>(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            s_vertanim_t &vanim = anim->vanim[0][i];
            vanim.vertex = entries[i].vertex;
            vanim.speed = 1.0f;
            vanim.side = 1.0f;
            vanim.pos = Scaled(entries[i].delta.position, GeometryScale(ctx, g));
            vanim.normal = entries[i].delta.normal;
            vanim.wrinkle = entries[i].delta.wrinkle;
        }
    }

    int FrameOf(uint32_t shape) { return shape == SMDL_NO_ID ? 0 : static_cast<int>(shape) + 1; }

    const std::string &BoneName(const ApplyState &state, smdl_bone_id bone) {
        return state.model.skeleton.bones[bone].name;
    }

    void ApplyEyelidItem(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, const MeshFlexDesc &desc,
                         const EyelidDesc &lid, s_model_t *pmodel, int imodel) {
        ScopedApplyObject object(state, std::format("eyelid '{}'", lid.name));
        for (int i = 0; i < kEyelidTypeCount; ++i) {
            if (!lid.set[i])
                MdlError("the {} state is not set (smdl_mesh_set_eyelid_state)", kStateName[i]);
        }
        EyelidParams params;
        params.type = lid.name;
        params.kind = lid.kind == 0 ? EyelidKind::Upper : EyelidKind::Lower;
        params.split = lid.split;
        params.baseDesc = AddEyelidFlexdesc(ctx, lid.name);
        for (int i = 0; i < kEyelidTypeCount; ++i) {
            params.desc[i] = AddEyelidFlexdesc(ctx, lid.name + "_" + kStateName[i]);
            params.frame[i] = FrameOf(lid.shape[i]);
            params.target[i] = lid.target[i];
        }
        if (lid.eyeball != SMDL_NO_ID)
            params.eyeball = desc.eyeballs[lid.eyeball].name;
        Check(ApplyEyelid(ctx, imodel, params, EnsureShapeSource(ctx, mesh, pmodel)));
    }

    void ApplyDmxEyelidItem(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, const MeshFlexDesc &desc,
                            const DmxEyelidDesc &lid, s_model_t *pmodel, int imodel) {
        ScopedApplyObject object(state, std::format("dmx eyelid '{}'", lid.kind == 0 ? "upper" : "lower"));
        DmxEyelidParams params;
        params.upper = lid.kind == 0;
        for (int i = 0; i < kEyelidTypeCount; ++i) {
            if (!lid.set[i])
                MdlError("the {} state is not set (smdl_mesh_set_dmx_eyelid_state)", kStateName[i]);
            if (lid.shape[i] >= mesh.geometry->shapes.size())
                MdlError("the {} state: shape {} does not exist ({} shapes)", kStateName[i], lid.shape[i],
                         mesh.geometry->shapes.size());
            EnsureDeltaState(ctx, mesh, pmodel->source, lid.shape[i]);
            params.animation[i] = mesh.geometry->shapes[lid.shape[i]].name;
            params.target[i] = lid.target[i];
        }
        params.rightEyeball = desc.eyeballs[lid.rightEyeball].name;
        params.leftEyeball = desc.eyeballs[lid.leftEyeball].name;
        Check(ApplyDmxEyelid(ctx, imodel, params, pmodel->source));
    }
}

void BuildShapeAnimations(StudioMdlState &, ApplyState &, const smdl_geometry &geometry, s_source_t *) {
    // The vertex animation itself is built when a flex or an eyelid of the mesh uses it (EnsureShapeSource): an
    // unused shape must not add a source. Here the shapes are checked against the final vertices.
    CheckShapes(geometry);
}

void ApplyMeshFlex(StudioMdlState &ctx, ApplyState &state, const smdl_mesh &mesh, s_model_t *pmodel, int imodel) {
    const auto found = state.model.flex.meshes.find(&mesh);
    if (found == state.model.flex.meshes.end())
        return;
    const MeshFlexDesc &desc = found->second;

    for (const MeshFlexItem &item: desc.items) {
        switch (item.kind) {
            case MeshFlexItem::Kind::Eyeball: {
                const EyeballDesc &eyeball = desc.eyeballs[item.index];
                ScopedApplyObject object(state, std::format("eyeball '{}'", eyeball.name));
                EyeballParams params;
                params.name = eyeball.name;
                params.bone = BoneName(state, eyeball.bone);
                params.position = eyeball.position;
                params.material = eyeball.material;
                params.diameter = eyeball.diameter;
                params.zAngleDegrees = eyeball.zAngleDegrees;
                params.pupilScale = eyeball.pupilScale;
                Check(ApplyEyeball(ctx, pmodel, params));
                break;
            }
            case MeshFlexItem::Kind::Flex: {
                const FlexKeyDesc &key = desc.flexKeys[item.index];
                ScopedApplyObject object(state, std::format("flex '{}'", key.name));
                FlexKeyParams params;
                params.name = key.name;
                params.stereo = key.pair;
                params.split = key.split;
                params.frame = FrameOf(key.shape);
                params.position = key.position;
                params.decay = key.decay;
                Check(ApplyFlexKey(ctx, params, EnsureShapeSource(ctx, mesh, pmodel), imodel));
                break;
            }
            case MeshFlexItem::Kind::DefaultFlex: {
                ScopedApplyObject object(state, "default flex");
                FlexKeyParams params;
                params.name = "default";
                params.frame = FrameOf(desc.defaultShape);
                Check(ApplyDefaultFlexKey(ctx, params, EnsureShapeSource(ctx, mesh, pmodel), imodel));
                break;
            }
            case MeshFlexItem::Kind::Eyelid:
                ApplyEyelidItem(ctx, state, mesh, desc, desc.eyelids[item.index], pmodel, imodel);
                break;
            case MeshFlexItem::Kind::DmxEyelid:
                ApplyDmxEyelidItem(ctx, state, mesh, desc, desc.dmxEyelids[item.index], pmodel, imodel);
                break;
            case MeshFlexItem::Kind::SphereNormals: {
                const SphereNormalsDesc &normals = desc.sphereNormals[item.index];
                ScopedApplyObject object(state, std::format("sphere normals '{}'", normals.material));
                Check(ApplySphereNormals(ctx, pmodel->source, normals.material, normals.position));
                break;
            }
            case MeshFlexItem::Kind::NoAutoDmxRules:
                ApplyNoAutoDmxRules(ctx, pmodel->source);
                break;
        }
    }
}

void ApplyFlex(StudioMdlState &ctx, ApplyState &state) {
    const FlexDesc &flex = state.model.flex;

    for (const FlexControllerDesc &controller: flex.controllers) {
        ScopedApplyObject object(state, std::format("flex controller '{}'", controller.name));
        Check(ApplyFlexController(ctx, controller.group, controller.name, controller.min, controller.max));
    }

    for (const std::string &name: flex.localVars) {
        ScopedApplyObject object(state, std::format("local variable '{}'", name));
        ApplyLocalVar(ctx, name);
    }

    for (const MouthDesc &mouth: flex.mouths) {
        ScopedApplyObject object(state, std::format("mouth {}", mouth.index));
        ApplyMouth(ctx, mouth.index, mouth.flex, BoneName(state, mouth.bone), mouth.forward);
    }

    if (!flex.rules.empty()) {
        // each rule is parsed by Option_Flexrule from its own buffer
        ScriptTokenizer tokenizer(ctx.paths.qdir, &ctx.paths.content);
        InstallMdlErrorHandler(tokenizer);
        for (const FlexRuleDesc &rule: flex.rules) {
            ScopedApplyObject object(state, std::format("flex rule '{}'", rule.flex));
            std::string buffer = "= " + rule.expression;
            tokenizer.LoadMemory(buffer);
            Option_Flexrule(ctx, tokenizer, nullptr, rule.flex.c_str());
            tokenizer.Close();
        }
    }

    for (const BoneFlexDriverDesc &driver: flex.boneDrivers) {
        ScopedApplyObject object(state, std::format("bone flex driver '{}'", BoneName(state, driver.bone)));
        Check(ApplyBoneFlexDriver(ctx, BoneName(state, driver.bone), driver.component,
                                  flex.controllers[driver.controller].name, driver.min, driver.max));
    }
}

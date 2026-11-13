//
// "Flex optimization", see include/studiomdl/flexoptimize.h
//

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <cmath>
#include <cstdint>

#include "common/cmdlib.h"
#include "mathlib/mathlib.h"
#include "studio.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/flexoptimize.h"
#include "tier1_v2/function_timing.hpp"

#include <ranges>

#include "studiomdl/studiomdl_global_state.hpp"


namespace {
    bool s_bFlexOptimizeActive = false;

    // RemapVertexAnimationsNewVersion drops deltas below (0.001^2, 0.001, 0.001). The deltas are transformed between
    // here and there, so deltas close to those thresholds may still end up as flex data: count them as moving too,
    // with thresholds 10x lower. A vertex wrongly marked as moving only costs a few cache entries; a moving vertex
    // in a static mesh makes the whole static mesh flexed.
    bool IsMovingVertAnim(const s_vertanim_t &vanim) {
        return DotProduct(vanim.pos, vanim.pos) > (0.001f * 0.001f) * 0.1f ||
               DotProduct(vanim.normal, vanim.normal) > 0.001f * 0.1f ||
               vanim.wrinkle > 0.001f * 0.1f;
    }

    // Vertices closer than this are treated as sharing a position. Vertex animations are matched to model vertices
    // by distance after the vertices were moved to global bone space, where seam duplicates may differ by rounding.
    constexpr float TWIN_DISTANCE = 0.001f;

    // Spatial hash of vertex positions with TWIN_DISTANCE cells
    class PositionGrid {
    public:
        void Insert(const Vector &position) {
            m_Cells[Key(Cell(position.x), Cell(position.y), Cell(position.z))].push_back(position);
        }

        bool HasNear(const Vector &position) const {
            const int64_t cx = Cell(position.x), cy = Cell(position.y), cz = Cell(position.z);
            for (int64_t x = cx - 1; x <= cx + 1; x++) {
                for (int64_t y = cy - 1; y <= cy + 1; y++) {
                    for (int64_t z = cz - 1; z <= cz + 1; z++) {
                        auto it = m_Cells.find(Key(x, y, z));
                        if (it == m_Cells.end())
                            continue;
                        for (const Vector &other: it->second) {
                            if ((other - position).LengthSqr() <= TWIN_DISTANCE * TWIN_DISTANCE)
                                return true;
                        }
                    }
                }
            }
            return false;
        }

        bool Empty() const { return m_Cells.empty(); }

    private:
        static int64_t Cell(float f) { return (int64_t) floorf(f / TWIN_DISTANCE); }

        static uint64_t Key(int64_t x, int64_t y, int64_t z) {
            return (uint64_t) x * 73856093u ^ (uint64_t) y * 19349663u ^ (uint64_t) z * 83492791u;
        }

        // collisions only cost extra distance tests
        std::unordered_map<uint64_t, std::vector<Vector> > m_Cells;
    };

    struct SourceFlexInfo {
        std::vector<bool> moving; // indexed by source vertex
        bool hasFlexKeys = false;
        bool hasOldStyleFlexKeys = false;
    };

    // Marks every vertex of the source that is moved by any of its flex keys
    SourceFlexInfo GatherSourceFlexInfo(s_source_t *pSource) {
        SourceFlexInfo info;
        info.moving.assign(pSource->numvertices, false);

        for (int i = 0; i < g_studiomdl.flex.flexkeys.size(); i++) {
            const s_flexkey_t &key = g_studiomdl.flex.flexkeys[i];
            if (key.source != pSource)
                continue;

            const s_sourceanim_t *pAnim = FindSourceAnim(pSource, key.animationname);
            if (!pAnim)
                continue;

            info.hasFlexKeys = true;
            if (!pAnim->newStyleVertexAnimations) {
                info.hasOldStyleFlexKeys = true;
                continue;
            }

            if (key.frame < 0 || key.frame >= MAXSTUDIOANIMFRAMES)
                continue;

            const s_vertanim_t *pVAnim = pAnim->vanim[key.frame];
            for (int k = 0; k < pAnim->numvanims[key.frame]; k++) {
                const int v = pVAnim[k].vertex;
                if (v >= 0 && v < pSource->numvertices && IsMovingVertAnim(pVAnim[k]))
                    info.moving[v] = true;
            }
        }

        // Vertex animations are matched to model vertices by position (BuildModelToVAnimMap): a vertex at the position
        // of a moving vertex (UV/material seam duplicate) can receive that vertex's delta, so it moves too
        PositionGrid movingPositions;
        for (int v = 0; v < pSource->numvertices; v++) {
            if (info.moving[v])
                movingPositions.Insert(pSource->vertex[v].position);
        }
        if (!movingPositions.Empty()) {
            std::vector<bool> twins(pSource->numvertices, false);
            for (int v = 0; v < pSource->numvertices; v++) {
                if (!info.moving[v] && movingPositions.HasNear(pSource->vertex[v].position))
                    twins[v] = true;
            }
            for (int v = 0; v < pSource->numvertices; v++) {
                if (twins[v])
                    info.moving[v] = true;
            }
        }
        return info;
    }

    bool IsFaceMoving(const s_face_t &face, const s_mesh_t &mesh, const std::vector<bool> &moving) {
        return moving[mesh.vertexoffset + face.a] || moving[mesh.vertexoffset + face.b] ||
               moving[mesh.vertexoffset + face.c];
    }

    // Vertex count the flexed meshes of a source add to the flex cache, before any splitting
    int SourceFlexCacheCost(const s_source_t *pSource, const std::vector<bool> &moving) {
        int cost = 0;
        for (const s_mesh_t &mesh: pSource->meshes) {
            for (uint32_t v = 0; v < mesh.numvertices; v++) {
                if (moving[mesh.vertexoffset + v]) {
                    cost += (int) mesh.numvertices;
                    break;
                }
            }
        }
        return cost;
    }

    // The engine budget counts the flexed meshes of every model in every bodypart, whichever bodygroups are active
    // (observed in HLMV: hiding a bodygroup does not help)
    template<typename ModelCost>
    int TotalFlexCacheCost(ModelCost modelCost) {
        int total = 0;
        for (int b = 0; b < std::ssize(g_studiomdl.body.bodypart); b++) {
            for (int m = 0; m < std::ssize(g_studiomdl.body.bodypart[b].pmodel); m++)
                total += modelCost(g_studiomdl.body.bodypart[b].pmodel[m]);
        }
        return total;
    }

    void PrintBudget(const char *pLabel, int nBudget) {
        if (g_studiomdl.options.quiet)
            return;
        if (nBudget > FLEX_CACHE_MAX_VERTICES) {
            std::print("  {}: {} / {} (over by {} flexed-mesh verts)\n", pLabel, nBudget, FLEX_CACHE_MAX_VERTICES,
                   nBudget - FLEX_CACHE_MAX_VERTICES);
        }
        else {
            std::print("  {}: {} / {} ({} left)\n", pLabel, nBudget, FLEX_CACHE_MAX_VERTICES,
                   FLEX_CACHE_MAX_VERTICES - nBudget);
        }
    }

    const char *MaterialName(const int material) {
        return g_studiomdl.materials.textures[g_studiomdl.materials.materials[material]].name;
    }

    bool IsAliasMaterial(int material) {
        for (const auto &alias: g_studiomdl.materials.flexAliasMaterials) {
            if (alias.first == material)
                return true;
        }
        return false;
    }

    const char *ModelName(const s_model_t *pModel) {
        if (pModel->name[0])
            return pModel->name.c_str();
        return pModel->source ? pModel->source->filename.c_str() : "";
    }

    // One row of the statistics table
    struct MeshStats {
        const char *bodypart;
        const char *model;
        int material;
        int verts;
        int flexedVerts; // vertices moved by at least one flex
        int flexes; // flex keys that move at least one vertex of the mesh
    };

    // Prints one row per mesh, most flexed vertices first; "cache" is what the mesh adds to the engine's flex cache
    // (whole mesh if it has flexes)
    void PrintMeshStats(const char *pTitle, std::vector<MeshStats> rows) {
        if (g_studiomdl.options.quiet)
            return;

        std::stable_sort(rows.begin(), rows.end(), [](const MeshStats &a, const MeshStats &b) {
            if (a.flexedVerts != b.flexedVerts)
                return a.flexedVerts > b.flexedVerts;
            return (a.flexes > 0 ? a.verts : 0) > (b.flexes > 0 ? b.verts : 0);
        });

        std::print("  {}\n", pTitle);
        std::print("    {:<16} {:<22} {:<24} {:>7} {:>7} {:>6} {:>7}\n", "bodypart", "model", "mesh", "verts", "flexed", "flexes",
               "cache");
        for (const MeshStats &row: rows) {
            if (row.verts == 0)
                continue; // empty mesh slot (e.g. a material that has no faces in a clamped piece)
            char meshName[MAX_PATH]{};
            V_snprintf(meshName, sizeof(meshName), "%s%s", MaterialName(row.material),
                       IsAliasMaterial(row.material) ? " (static)" : "");
            std::print("    {:<16.16} {:<22.22} {:<24.24} {:7} {:7} {:6} {:7}\n", row.bodypart, row.model, meshName, row.verts,
                   row.flexedVerts, row.flexes, row.flexes > 0 ? row.verts : 0);
        }
    }

    // Per mesh of the source: vertices moved by a flex, and flex keys moving at least one of its vertices
    void SourceMeshStats(s_source_t *pSource, const std::vector<bool> &moving, const char *pBodypart,
                         const char *pModel, std::vector<MeshStats> &rows) {
        std::vector<int> vertexSlot(pSource->numvertices, -1);
        for (int slot = 0; slot < pSource->meshes.size(); slot++) {
            const s_mesh_t &mesh = pSource->meshes[slot];
            for (uint32_t v = 0; v < mesh.numvertices; v++)
                vertexSlot[mesh.vertexoffset + v] = slot;
        }

        std::vector<int> flexes(pSource->meshes.size(), 0);
        for (int i = 0; i < g_studiomdl.flex.flexkeys.size(); i++) {
            const s_flexkey_t &key = g_studiomdl.flex.flexkeys[i];
            if (key.source != pSource || key.frame < 0 || key.frame >= MAXSTUDIOANIMFRAMES)
                continue;
            const s_sourceanim_t *pAnim = FindSourceAnim(pSource, key.animationname);
            if (!pAnim || !pAnim->newStyleVertexAnimations)
                continue;
            std::vector<bool> touched(pSource->meshes.size(), false);
            for (int k = 0; k < pAnim->numvanims[key.frame]; k++) {
                const s_vertanim_t &vanim = pAnim->vanim[key.frame][k];
                if (vanim.vertex >= 0 && vanim.vertex < pSource->numvertices && vertexSlot[vanim.vertex] >= 0 &&
                    IsMovingVertAnim(vanim))
                    touched[vertexSlot[vanim.vertex]] = true;
            }
            for (int slot = 0; slot < pSource->meshes.size(); slot++)
                flexes[slot] += touched[slot] ? 1 : 0;
        }

        for (int slot = 0; slot < pSource->meshes.size(); slot++) {
            const s_mesh_t &mesh = pSource->meshes[slot];
            const int nMaterial = mesh.material;
            int flexed = 0;
            for (uint32_t v = 0; v < mesh.numvertices; v++)
                flexed += moving[mesh.vertexoffset + v] ? 1 : 0;
            rows.push_back({pBodypart, pModel, nMaterial, (int) mesh.numvertices, flexed, flexes[slot]});
        }
    }

    // Material that renders exactly like nBaseMaterial but gives a source a second mesh slot with that look
    int GetAliasMaterial(int nBaseMaterial) {
        for (const auto &alias: g_studiomdl.materials.flexAliasMaterials) {
            if (alias.second == nBaseMaterial)
                return alias.first;
        }

        if (g_studiomdl.materials.textures.size() >= MAXSTUDIOSKINS) {
            MdlError("Too many materials used, max {}\n", (int) MAXSTUDIOSKINS);
        }

        // LookupTexture() would return the existing texture of that name, the alias needs its own slot
        const int nBaseTexture = g_studiomdl.materials.materials[nBaseMaterial];
        auto &aliasTexture = g_studiomdl.materials.textures.emplace(g_studiomdl.materials.textures[nBaseTexture]);
        const int nAliasTexture = g_studiomdl.materials.textures.size() - 1;
        aliasTexture.material = -1;

        const int nAliasMaterial = UseTextureAsMaterial(nAliasTexture);
        g_studiomdl.materials.flexAliasMaterials.emplace_back(nAliasMaterial, nBaseMaterial);
        return nAliasMaterial;
    }

    struct MeshSplit {
        int slot;
        int material;
        int aliasMaterial = -1; // -1: mesh is kept as is
        std::vector<int> movingFaces; // source face indices
        std::vector<int> staticFaces;
        int staticVertexCount = 0;
    };

    // Appends the faces of one destination mesh and the vertices they use (in source order) to the new arrays.
    // Returns the source to new vertex map of that mesh (indexed relative to the source mesh).
    std::vector<int> EmitMesh(const s_source_t *pSource, const s_mesh_t &srcMesh, int dstMaterial,
                              const std::vector<int> &faces, bool bAllVertices,
                              std::vector<s_vertexinfo_t> &vertices, std::vector<s_face_t> &dstFaces,
                              s_mesh_t &dstMesh) {
        std::vector<int> map(srcMesh.numvertices, -1);
        std::vector<bool> used(srcMesh.numvertices, bAllVertices);
        for (int f: faces) {
            const s_face_t &face = pSource->face[f];
            used[face.a] = used[face.b] = used[face.c] = true;
        }

        dstMesh.material = dstMaterial;
        dstMesh.vertexoffset = (uint32_t) vertices.size();
        dstMesh.faceoffset = (int) dstFaces.size();
        dstMesh.numvertices = 0;
        dstMesh.numfaces = (int) faces.size();

        for (uint32_t v = 0; v < srcMesh.numvertices; v++) {
            if (!used[v])
                continue;
            map[v] = (int) dstMesh.numvertices++;
            s_vertexinfo_t vertex = pSource->vertex[srcMesh.vertexoffset + v];
            vertex.material = dstMaterial;
            vertices.push_back(vertex);
        }

        for (int f: faces) {
            s_face_t face = pSource->face[f];
            face.a = map[face.a];
            face.b = map[face.b];
            face.c = map[face.c];
            dstFaces.push_back(face);
        }
        return map;
    }

    // Rebuilds vertex/face/mesh arrays of the source with the planned splits and remaps its vertex animations
    void ApplySplits(s_source_t *pSource, const std::vector<MeshSplit> &splits) {
        std::vector<s_vertexinfo_t> vertices;
        std::vector<s_face_t> faces;
        std::vector<int> oldToNew(pSource->numvertices, -1);
        vertices.reserve(pSource->numvertices);
        faces.reserve(pSource->numfaces);

        // new meshes by material id while they are built; originals first, then the alias (static) meshes
        std::array<s_mesh_t, MAXSTUDIOSKINS> newMesh = {};
        std::array<int, MAXSTUDIOSKINS> newMeshIndex = {};
        int newMeshCount = 0;

        // Original slots first, so existing slot indices (eyeballs) stay valid; they keep the faces with moving
        // vertices, and with them all flex data
        for (const MeshSplit &split: splits) {
            const s_mesh_t &srcMesh = pSource->meshes[split.slot];
            std::vector<int> keptFaces;
            if (split.aliasMaterial < 0) {
                for (int f = 0; f < srcMesh.numfaces; f++)
                    keptFaces.push_back(srcMesh.faceoffset + f);
            }
            else {
                keptFaces = split.movingFaces;
            }

            s_mesh_t &dstMesh = newMesh[split.material];
            std::vector<int> map = EmitMesh(pSource, srcMesh, split.material, keptFaces, split.aliasMaterial < 0,
                                            vertices, faces, dstMesh);
            for (uint32_t v = 0; v < srcMesh.numvertices; v++) {
                if (map[v] >= 0)
                    oldToNew[srcMesh.vertexoffset + v] = static_cast<int>(dstMesh.vertexoffset) + map[v];
            }
            newMeshIndex[newMeshCount++] = split.material;
        }

        // Static parts go into the alias slots. Their vertices are never moved by a flex (a moving vertex makes all of
        // its faces moving faces), so the vertices they share with the moving part can't open a seam.
        for (const MeshSplit &split: splits) {
            if (split.aliasMaterial < 0)
                continue;
            const s_mesh_t &srcMesh = pSource->meshes[split.slot];
            EmitMesh(pSource, srcMesh, split.aliasMaterial, split.staticFaces, false, vertices, faces,
                     newMesh[split.aliasMaterial]);
            newMeshIndex[newMeshCount++] = split.aliasMaterial;
        }

        // Vertex animations index source vertices
        for (int i = 0; i < pSource->m_Animations.Count(); i++) {
            s_sourceanim_t &anim = pSource->m_Animations[i];
            if (!anim.newStyleVertexAnimations)
                continue;
            for (int frame = 0; frame < MAXSTUDIOANIMFRAMES; frame++) {
                int kept = 0;
                for (int k = 0; k < anim.numvanims[frame]; k++) {
                    const int v = anim.vanim[frame][k].vertex;
                    const int newV = (v >= 0 && v < static_cast<int>(oldToNew.size())) ? oldToNew[v] : -1;
                    if (newV < 0)
                        continue; // vertex isn't used by any face
                    anim.vanim[frame][kept] = anim.vanim[frame][k];
                    anim.vanim[frame][kept].vertex = newV;
                    kept++;
                }
                anim.numvanims[frame] = kept;
            }
        }

        free(pSource->vertex);
        free(pSource->face);
        pSource->numvertices = (int) vertices.size();
        pSource->vertex = static_cast<s_vertexinfo_t *>(calloc(vertices.size(), sizeof(s_vertexinfo_t)));
        for (size_t i = 0; i < vertices.size(); i++)
            pSource->vertex[i] = vertices[i];
        pSource->numfaces = (int) faces.size();
        pSource->face = static_cast<s_face_t *>(calloc(faces.size(), sizeof(s_face_t)));
        for (size_t i = 0; i < faces.size(); i++)
            pSource->face[i] = faces[i];

        pSource->meshes.clear();
        for (int i = 0; i < newMeshCount; i++) {
            pSource->meshes.push_back(newMesh[newMeshIndex[i]]);
        }

        // texmap maps the source's materials to textures 1:1 for DMX/OBJ/SMD sources
        for (int i = 0; i < newMeshCount; i++) {
            const int nMaterial = newMeshIndex[i];
            pSource->texmap[g_studiomdl.materials.materials[nMaterial]] = g_studiomdl.materials.materials[nMaterial];
        }
    }

    // Plans the split of every mesh of a source; returns true if at least one mesh is split
    bool PlanSplits(s_source_t *pSource, const std::vector<bool> &moving, const std::vector<bool> &eyeballMaterial,
                    std::vector<MeshSplit> &splits) {
        bool bAnySplit = false;
        const int nMinStatic = g_studiomdl.options.flexOptimizeMinStatic;
        splits.clear();

        for (int slot = 0; slot < pSource->meshes.size(); slot++) {
            MeshSplit split;
            split.slot = slot;
            const s_mesh_t &mesh = pSource->meshes[slot];
            split.material = mesh.material;

            std::vector<bool> staticUsed(mesh.numvertices, false);
            for (int f = 0; f < mesh.numfaces; f++) {
                const int nFace = mesh.faceoffset + f;
                const s_face_t &face = pSource->face[nFace];
                if (IsFaceMoving(face, mesh, moving)) {
                    split.movingFaces.push_back(nFace);
                }
                else {
                    split.staticFaces.push_back(nFace);
                    staticUsed[face.a] = staticUsed[face.b] = staticUsed[face.c] = true;
                }
            }
            for (bool used: staticUsed)
                split.staticVertexCount += used ? 1 : 0;

            const char *pMaterialName = MaterialName(split.material);
            if (split.movingFaces.empty() || split.staticFaces.empty()) {
                // not flexed at all, or every face moves: nothing to gain
            }
            else if (eyeballMaterial[split.material]) {
                if (!g_studiomdl.options.quiet)
                    std::print("  skipped {} mesh \"{}\": used by an eyeball\n", pSource->filename, pMaterialName);
            }
            else if (split.staticVertexCount < nMinStatic) {
                if (!g_studiomdl.options.quiet)
                    std::print("  skipped {} mesh \"{}\": static part {} verts < {}\n", pSource->filename,
                           pMaterialName, split.staticVertexCount, nMinStatic);
            }
            else {
                split.aliasMaterial = GetAliasMaterial(split.material);
                bAnySplit = true;
            }
            splits.push_back(std::move(split));
        }
        return bAnySplit;
    }

    void PrintSplits(const s_source_t *pSource, const std::vector<MeshSplit> &splits) {
        if (g_studiomdl.options.quiet)
            return;
        for (const MeshSplit &split: splits) {
            if (split.aliasMaterial < 0)
                continue;
            // after ApplySplits the original meshes keep their slots, the static parts follow
            const s_mesh_t &flexed = pSource->meshes[split.slot];
            const s_mesh_t &statik = pSource->meshes[FindSourceMeshIndex(pSource, split.aliasMaterial)];
            std::print("  {} mesh \"{}\": flexed {} + static {} verts\n", pSource->filename,
                   MaterialName(split.material), flexed.numvertices, statik.numvertices);
        }
    }
}

bool ParseFlexOptimizeMode(const std::string_view text, FlexOptimizeMode_t &mode) {
    if (vstd::iequals(text, "auto")) {
        mode = FLEXOPT_AUTO;
        return true;
    }
    if (vstd::iequals(text, "on")) {
        mode = FLEXOPT_ON;
        return true;
    }
    if (vstd::iequals(text, "off")) {
        mode = FLEXOPT_OFF;
        return true;
    }
    return false;
}

void FlexOptimizeSources() {
    vstd::FunctionTimingRecorder _("Optimize");
    if (g_studiomdl.options.flexOptimizeMode == FLEXOPT_OFF)
        return;

    // Sources of all models; a source may be shared between models
    std::unordered_map<s_source_t *, SourceFlexInfo> sourceInfo;
    for (int i = 0; i < std::ssize(g_studiomdl.body.model); i++) {
        s_source_t *pSource = g_studiomdl.body.model[i]->source;
        if (!pSource || !pSource->isActiveModel || sourceInfo.contains(pSource))
            continue;
        sourceInfo.emplace(pSource, GatherSourceFlexInfo(pSource));
    }

    bool bAnyFlexes = false;
    for (const auto &val: sourceInfo | std::views::values)
        bAnyFlexes = bAnyFlexes || val.hasFlexKeys;
    if (!bAnyFlexes)
        return;

    auto modelCost = [&](const s_model_t *pModel) {
        if (!pModel || !pModel->source)
            return 0;
        const auto it = sourceInfo.find(pModel->source);
        if (it == sourceInfo.end())
            return 0;
        return SourceFlexCacheCost(pModel->source, it->second.moving);
    };
    const int nBudgetBefore = TotalFlexCacheCost(modelCost);

    if (!g_studiomdl.options.quiet) {
        std::print("Flex optimization\n");

        std::vector<MeshStats> rows;
        for (int b = 0; b < std::ssize(g_studiomdl.body.bodypart); b++) {
            for (int m = 0; m < std::ssize(g_studiomdl.body.bodypart[b].pmodel); m++) {
                const s_model_t *pModel = g_studiomdl.body.bodypart[b].pmodel[m];
                if (!pModel || !pModel->source)
                    continue;
                auto it = sourceInfo.find(pModel->source);
                if (it == sourceInfo.end() || !it->second.hasFlexKeys)
                    continue;
                SourceMeshStats(pModel->source, it->second.moving, g_studiomdl.body.bodypart[b].name, ModelName(pModel),
                                rows);
            }
        }
        PrintMeshStats("meshes of models with flexes, before:", rows);
    }
    PrintBudget("flex cache budget (all bodygroups)", nBudgetBefore);

    if (g_studiomdl.options.flexOptimizeMode == FLEXOPT_AUTO && nBudgetBefore <= FLEX_CACHE_MAX_VERTICES) {
        if (!g_studiomdl.options.quiet)
            std::print("  within budget, meshes are left as they are\n");
        return;
    }

    if (g_studiomdl.header.gflags & STUDIOHDR_FLAGS_SUBDIVISION_SURFACE) {
        MdlWarning("Flex optimization: subdivision surface models are not supported, meshes are left as they are\n");
        return;
    }
    s_bFlexOptimizeActive = true;

    // Materials of eyeball meshes: eyeballs refer to their mesh and the eye shader needs all of its faces
    std::vector<bool> eyeballMaterial(MAXSTUDIOSKINS, false);
    for (int i = 0; i < std::ssize(g_studiomdl.body.model); i++) {
        const s_model_t *pModel = g_studiomdl.body.model[i];
        if (!pModel->source)
            continue;
        for (int e = 0; e < pModel->eyeballs.size(); e++) {
            const int nSlot = pModel->eyeballs[e].mesh;
            if (nSlot >= 0 && nSlot < pModel->source->meshes.size())
                eyeballMaterial[pModel->source->meshes[nSlot].material] = true;
        }
    }

    std::vector<MeshSplit> splits;
    for (int i = 0; i < std::ssize(g_studiomdl.body.model); i++) {
        s_source_t *pSource = g_studiomdl.body.model[i]->source;
        auto it = sourceInfo.find(pSource);
        if (it == sourceInfo.end() || !it->second.hasFlexKeys)
            continue;
        SourceFlexInfo &info = it->second;
        if (info.hasOldStyleFlexKeys) {
            MdlWarning("Flex optimization: {} uses old-style (VTA) flexes, which are not supported, "
                       "meshes are left as they are\n", pSource->filename);
            info.hasFlexKeys = false; // report once per source
            continue;
        }

        if (PlanSplits(pSource, info.moving, eyeballMaterial, splits)) {
            ApplySplits(pSource, splits);
            PrintSplits(pSource, splits);
            info.moving = GatherSourceFlexInfo(pSource).moving;
        }
        info.hasFlexKeys = false; // done, sources shared by several models are split once
    }

    PrintBudget("flex cache budget after splitting (estimate)", TotalFlexCacheCost(modelCost));
}

bool FlexOptimizeIsActive() {
    return s_bFlexOptimizeActive;
}

std::vector<bool> FlexOptimizeMovingVertices(s_source_t *pSource) {
    return GatherSourceFlexInfo(pSource).moving;
}

void FlexOptimizeFixupSkinFamilies() {
    // The alias material shows the same texture as its base material in every skin family
    for (const auto &alias: g_studiomdl.materials.flexAliasMaterials) {
        for (int family = 0; family < std::ssize(g_studiomdl.materials.skinref); family++) {
            g_studiomdl.materials.skinref[family][alias.first] = g_studiomdl.materials.skinref[family][alias.second];
        }
    }
}

void CheckFlexCacheBudget() {
    vstd::FunctionTimingRecorder _("Optimize");
    // Same rule as WriteModel: a mesh has flexes if a vertex animation of a flex key of its model lands in its range
    std::vector<int> meshCost(g_studiomdl.body.nummodelsbeforeLOD, 0);
    std::unordered_map<const s_model_t *, int> modelIndex;
    std::vector<std::vector<MeshStats> > modelRows(g_studiomdl.body.nummodelsbeforeLOD);
    bool bAnyFlexes = false;

    for (int i = 0; i < g_studiomdl.body.nummodelsbeforeLOD; i++) {
        const s_model_t *pModel = g_studiomdl.body.model[i];
        modelIndex[pModel] = i;
        const s_loddata_t *pLodData = pModel->m_pLodData;
        if (!pLodData || !pModel->source)
            continue;

        for (int slot = 0; slot < pModel->source->meshes.size(); slot++) {
            const int nMaterial = pModel->source->meshes[slot].material;
            const s_mesh_t &mesh = pLodData->mesh[nMaterial];
            std::vector<bool> flexedVertex(mesh.numvertices, false);
            int flexes = 0;
            for (int j = 0; j < g_studiomdl.flex.flexkeys.size(); j++) {
                if (g_studiomdl.flex.flexkeys[j].imodel != i)
                    continue;
                bool bTouches = false;
                for (int k = 0; k < g_studiomdl.flex.flexkeys[j].numvanims; k++) {
                    const int n = g_studiomdl.flex.flexkeys[j].vanim[k].vertex - (int) mesh.vertexoffset;
                    if (n >= 0 && n < (int) mesh.numvertices) {
                        bTouches = true;
                        flexedVertex[n] = true;
                    }
                }
                flexes += bTouches ? 1 : 0;
            }
            int flexed = 0;
            for (bool b: flexedVertex)
                flexed += b ? 1 : 0;
            modelRows[i].push_back({"", ModelName(pModel), nMaterial, (int) mesh.numvertices, flexed, flexes});
            if (flexes > 0) {
                meshCost[i] += (int) mesh.numvertices;
                bAnyFlexes = true;
            }
        }
    }

    if (!bAnyFlexes)
        return;

    auto modelCost = [&](const s_model_t *pModel) {
        auto it = modelIndex.find(pModel);
        return it == modelIndex.end() ? 0 : meshCost[it->second];
    };
    const int nBudget = TotalFlexCacheCost(modelCost);

    if (!g_studiomdl.options.quiet) {
        std::print("Flex optimization: result\n");
        // every mesh of every model that has flexes or whose source had flexes (static parts, clamped pieces)
        std::vector<MeshStats> rows;
        for (int b = 0; b < std::ssize(g_studiomdl.body.bodypart); b++) {
            for (int m = 0; m < std::ssize(g_studiomdl.body.bodypart[b].pmodel); m++) {
                auto it = modelIndex.find(g_studiomdl.body.bodypart[b].pmodel[m]);
                if (it == modelIndex.end())
                    continue;
                const int i = it->second;
                const s_source_t *pSource = g_studiomdl.body.model[i]->source;
                bool bShow = meshCost[i] > 0;
                for (const MeshStats &row: modelRows[i])
                    bShow = bShow || IsAliasMaterial(row.material);
                bShow = bShow || (pSource && pSource->m_FlexKeys.Count() > 0);
                if (!bShow)
                    continue;
                for (MeshStats row: modelRows[i]) {
                    row.bodypart = g_studiomdl.body.bodypart[b].name;
                    rows.push_back(row);
                }
            }
        }
        PrintMeshStats("meshes of models with flexes, after:", rows);
    }
    PrintBudget("flex cache budget (all bodygroups)", nBudget);

    if (nBudget <= FLEX_CACHE_MAX_VERTICES)
        return;

    MdlWarning(
        "flex cache budget {} exceeds {} vertices by {}: flexes will move vertices of other meshes in the engine. "
        "Models with flexed meshes, largest first:\n", nBudget, FLEX_CACHE_MAX_VERTICES,
        nBudget - FLEX_CACHE_MAX_VERTICES);
    struct ModelEntry {
        const char *bodypart;
        const s_model_t *model;
        int cost;
    };
    std::vector<ModelEntry> entries;
    for (int b = 0; b < std::ssize(g_studiomdl.body.bodypart); b++) {
        for (int m = 0; m < std::ssize(g_studiomdl.body.bodypart[b].pmodel); m++) {
            const s_model_t *pModel = g_studiomdl.body.bodypart[b].pmodel[m];
            const int cost = modelCost(pModel);
            if (cost > 0)
                entries.push_back({g_studiomdl.body.bodypart[b].name, pModel, cost});
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ModelEntry &a, const ModelEntry &b) { return a.cost > b.cost; });
    for (const ModelEntry &entry: entries)
        MdlWarning("    bodypart \"{}\" model \"{}\": {} verts\n", entry.bodypart, ModelName(entry.model), entry.cost);
}

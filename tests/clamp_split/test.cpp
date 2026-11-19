#include "shim.h"   // minimal stand-ins for the engine types used by the clamp code
#include <map>
#include <set>
#include <tuple>
#include <random>
#include <string>

StudioMdlContext g_StudioMdlContext;
ShimGlobalState g_studiomdl;
void MdlWarning(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("  [MdlWarning] "); vprintf(fmt, a); va_end(a); }
void Warning(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("  [Warning] "); vprintf(fmt, a); va_end(a); }
s_sourceanim_t *FindSourceAnim(s_source_t *source, const std::string_view animName) {
    for (int i = 0; i < source->m_Animations.Count(); i++) if (!Q_stricmp(animName, source->m_Animations[i].animationname)) return &source->m_Animations[i];
    return nullptr;
}
const s_sourceanim_t *FindSourceAnim(const s_source_t *s, const std::string_view n) {
    if (!n[0]) return nullptr;
    for (int i = 0; i < s->m_Animations.Count(); i++) if (!Q_stricmp(n, s->m_Animations[i].animationname)) return &s->m_Animations[i];
    return nullptr;
}
struct FlexCall { s_source_t *src; int imodel; bool controllers; int keys; };
std::vector<FlexCall> g_flexcalls; int g_numflexkeys;
void AddBodyFlexData(s_source_t *s, int imodel, bool bAddControllers) {
    s->m_nKeyStartIndex = g_numflexkeys; g_numflexkeys += s->m_FlexKeys.Count();
    g_flexcalls.push_back({s, imodel, bAddControllers, s->m_FlexKeys.Count()});
}

// Flex optimization hooks: the moving vertices are the ones a new-style flex animation of the source references
static bool g_flexOptActive = false;
bool FlexOptimizeIsActive() { return g_flexOptActive; }
std::vector<bool> FlexOptimizeMovingVertices(s_source_t *s) {
    std::vector<bool> moving(s->numvertices, false);
    for (int k = 0; k < s->m_FlexKeys.Count(); k++) {
        const s_sourceanim_t *an = FindSourceAnim(s, s->m_FlexKeys[k].animationname);
        if (!an || !an->newStyleVertexAnimations) continue;
        for (int j = 0; j < an->numvanims[0]; j++) moving[an->vanim[0][j].vertex] = true;
    }
    return moving;
}

#include "class_decl.inc"
#include "clamped_impl.inc"
#include "clamp_fn.inc"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reset() {
    g_studiomdl.source.clear(); g_studiomdl.nummodels = 0; g_studiomdl.numbodyparts = 0; g_flexcalls.clear(); g_numflexkeys = 0; g_studiomdl.model.v.clear();
    for (auto &b : g_studiomdl.bodypart) b = s_bodypart_t();
}
static s_source_t *new_source(const char *name) {
    s_source_t *s = (s_source_t *)calloc(1, sizeof(s_source_t));
    strcpy(s->filename, name); s->isActiveModel = true;
    g_studiomdl.source.push_back(s);
    s_model_t *m = (s_model_t *)calloc(1, sizeof(s_model_t)); m->source = s; strcpy(m->name, "orig");
    g_studiomdl.model[g_studiomdl.nummodels++] = m;
    s_bodypart_t &bp = g_studiomdl.bodypart[g_studiomdl.numbodyparts++]; bp.nummodels = 1; bp.base = 1; strcpy(bp.name, name); bp.pmodel[0] = m;
    return s;
}

// canonical triangle/quad description: (material, original vertex ids)
typedef std::tuple<int, int, int, int, int> Prim;
static std::multiset<Prim> collect(const s_source_t *s, std::vector<std::string> *errors, int clampLimit, bool check_ranges = true) {
    std::multiset<Prim> out;
    for (int slot = 0; slot < s->nummeshes; slot++) {
        int mat = s->meshindex[slot];
        const s_mesh_t &m = s->mesh[mat];
        for (int f = m.faceoffset; f < m.faceoffset + m.numfaces; f++) {
            const s_face_t &fc = s->face[f];
            uint32_t idx[4] = {fc.a, fc.b, fc.c, fc.d};
            int ids[4] = {-1, -1, -1, -1};
            int n = fc.d == 0 ? 3 : 4;
            for (int k = 0; k < n; k++) {
                if (check_ranges && idx[k] >= m.numvertices) { errors->push_back("face index outside mesh vertex range"); }
                uint32_t gv = m.vertexoffset + idx[k];
                if ((int)gv >= s->numvertices) { errors->push_back("vertex out of range"); continue; }
                ids[k] = s->vertex[gv].id;
            }
            if (n == 4) { int mn = std::min_element(ids, ids + 4) - ids; std::rotate(ids, ids + mn, ids + 4); }   // a quad is the same quad when rotated
            out.insert(Prim(mat, ids[0], ids[1], ids[2], ids[3]));
        }
    }
    return out;
}

static void run_split_check(const char *title, s_source_t *orig, const std::multiset<Prim> &expected, int clamp, int expectMeshesSlots,
                            const int *origMeshIndex, const int *origTexmap) {
    printf("%s\n", title);
    int pieces = (int) g_studiomdl.source.size();
    std::multiset<Prim> got; std::vector<std::string> errs;
    for (int i = 0; i < pieces; i++) {
        s_source_t *s = g_studiomdl.source[i];
        auto part = collect(s, &errs, clamp);
        got.insert(part.begin(), part.end());
        CHECK(s->numvertices <= clamp, "piece %d has %d verts > clamp %d", i, s->numvertices, clamp);
        CHECK(s->nummeshes == expectMeshesSlots, "piece %d nummeshes=%d", i, s->nummeshes);
        CHECK(!memcmp(s->meshindex, origMeshIndex, sizeof(int) * expectMeshesSlots), "piece %d meshindex differs from the original", i);
        CHECK(!memcmp(s->texmap, origTexmap, sizeof(s->texmap)), "piece %d texmap differs from the original", i);
        // meshes that are not in meshindex must be empty
        for (int m = 0; m < MAXSTUDIOSKINS; m++) {
            bool used = false; for (int k = 0; k < s->nummeshes; k++) used |= s->meshindex[k] == m;
            if (!used) CHECK(s->mesh[m].numfaces == 0 && s->mesh[m].numvertices == 0, "piece %d unused mesh %d not empty", i, m);
        }
        // meshes use disjoint contiguous vertex ranges
        std::vector<int> owner(s->numvertices, -1);
        for (int k = 0; k < s->nummeshes; k++) {
            const s_mesh_t &m = s->mesh[s->meshindex[k]];
            for (uint32_t v = 0; v < m.numvertices; v++) {
                CHECK((int)(m.vertexoffset + v) < s->numvertices, "mesh range out of vertex array");
                if ((int)(m.vertexoffset + v) < s->numvertices) { CHECK(owner[m.vertexoffset + v] == -1, "vertex shared by two meshes"); owner[m.vertexoffset + v] = k; }
            }
        }
        for (int v = 0; v < s->numvertices; v++) CHECK(owner[v] != -1, "piece %d vertex %d not owned by any mesh", i, v);
        printf("  piece %d: verts=%d faces=%d\n", i, s->numvertices, s->numfaces);
    }
    for (auto &e : errs) CHECK(false, "%s", e.c_str());
    CHECK(got == expected, "face multiset differs: got %zu expected %zu", got.size(), expected.size());
    if (got != expected) { int shown = 0; for (auto &p : expected) if (!got.count(p) && shown++ < 3) printf("    missing: mat=%d ids=%d %d %d %d\n", std::get<0>(p), std::get<1>(p), std::get<2>(p), std::get<3>(p), std::get<4>(p));
        shown = 0; for (auto &p : got) if (!expected.count(p) && shown++ < 3) printf("    extra:   mat=%d ids=%d %d %d %d\n", std::get<0>(p), std::get<1>(p), std::get<2>(p), std::get<3>(p), std::get<4>(p)); }
    CHECK(g_studiomdl.nummodels == pieces && g_studiomdl.numbodyparts == pieces, "models=%d bodyparts=%d pieces=%d", g_studiomdl.nummodels, g_studiomdl.numbodyparts, pieces);
    (void)orig;
}

int main() {
    // ---------------- differential test against the real SFM studiomdl.exe -----------------
    {
        reset();
        FILE *f = fopen("thongs_faces.bin", "rb");
        if (f) {
            std::vector<uint32_t> raw(81408 * 4); fread(raw.data(), 4, raw.size(), f); fclose(f);
            for (int material : {0, 2}) {   // 0: what the SFM binary handles, 2: what crashes it
                reset();
                s_source_t *s = new_source("thongs.dmx");
                s->numvertices = 47659; s->vertex = (s_vertexinfo_t *)calloc(47659, sizeof(s_vertexinfo_t));
                for (int i = 0; i < 47659; i++) s->vertex[i].id = i;
                s->numfaces = 81408; s->face = (s_face_t *)calloc(81408, sizeof(s_face_t));
                for (int i = 0; i < 81408; i++) { s->face[i].a = raw[i*4]; s->face[i].b = raw[i*4+1]; s->face[i].c = raw[i*4+2]; s->face[i].d = raw[i*4+3]; }
                s->nummeshes = 1; s->meshindex[0] = material; s->mesh[material] = {47659, 0, 81408, 0};
                for (int i = 0; i < 128; i++) s->texmap[i] = i;
                g_StudioMdlContext.maxVertexLimit = g_StudioMdlContext.maxVertexClamp = 21845;
                std::vector<std::string> e0; auto expected = collect(s, &e0, 21845, false);
                int origMI[32]; memcpy(origMI, s->meshindex, sizeof(origMI)); int origTex[128]; memcpy(origTex, s->texmap, sizeof(origTex));
                ClampMaxVerticesPerModel(s);
                char title[128]; snprintf(title, sizeof(title), "[SFM differential] thongs.dmx, material index %d", material);
                run_split_check(title, s, expected, 21845, 1, origMI, origTex);
                // SFM compiled output (gdb): verts 21843/21845/12171, faces 31152/33609/16647
                CHECK((int) g_studiomdl.source.size() == 3, "expected 3 pieces like studiomdl.exe, got %d", (int) g_studiomdl.source.size());
                if ((int) g_studiomdl.source.size() == 3) {
                    int ev[3] = {21843, 21845, 12171}, ef[3] = {31152, 33609, 16647};
                    for (int i = 0; i < 3; i++) CHECK(g_studiomdl.source[i]->numvertices == ev[i] && g_studiomdl.source[i]->numfaces == ef[i], "piece %d: %d/%d, SFM: %d/%d", i, g_studiomdl.source[i]->numvertices, g_studiomdl.source[i]->numfaces, ev[i], ef[i]);
                }
            }
        } else printf("(no thongs dump, skipping differential test)\n");
    }

    // ---------------- randomized multi-material test with flexes, without and with flex optimization ----------------
    for (int flexOpt = 0; flexOpt < 2; flexOpt++) {
    g_flexOptActive = flexOpt != 0;
    std::mt19937 rng(1234);
    for (int round = 0; round < 6; round++) {
        reset();
        s_source_t *s = new_source("multi.dmx");
        const int mats[][4] = {{2, 5, 7, 11}, {0, 1, 2, 3}, {9, 4, 0, 31}, {3, 3 + 1, 20, 6}, {30, 29, 28, 27}, {5, 1, 17, 8}};
        int nm = 3 + (round % 2);   // 3 or 4 meshes; ids can be >= nummeshes
        int total = 0; std::vector<int> vcount(nm), fcount(nm);
        for (int i = 0; i < nm; i++) { vcount[i] = 300 + rng() % 900; fcount[i] = vcount[i] * 2 + rng() % 400; total += vcount[i]; }
        s->numvertices = total; s->vertex = (s_vertexinfo_t *)calloc(total, sizeof(s_vertexinfo_t));
        for (int i = 0; i < total; i++) s->vertex[i].id = i;
        std::vector<s_face_t> faces; s->nummeshes = nm; int voff = 0;
        for (int i = 0; i < nm; i++) {
            int mat = mats[round][i]; s->meshindex[i] = mat;
            s_mesh_t &m = s->mesh[mat]; m.numvertices = vcount[i]; m.vertexoffset = voff; m.faceoffset = faces.size(); m.numfaces = fcount[i];
            for (int f = 0; f < fcount[i]; f++) {
                s_face_t fc; std::set<uint32_t> used4; auto pick = [&]() { uint32_t x; do { x = rng() % vcount[i]; } while (used4.count(x)); used4.insert(x); return x; };
                fc.a = pick(); fc.b = pick(); fc.c = pick();
                fc.d = 0; if (rng() % 4 == 0) { uint32_t d = pick(); fc.d = d; }  // some quads (d == 0 would mean triangle, as in the real format)
                faces.push_back(fc);
            }
            voff += vcount[i];
        }
        s->numfaces = faces.size(); s->face = (s_face_t *)calloc(faces.size(), sizeof(s_face_t)); for (size_t i = 0; i < faces.size(); i++) s->face[i] = faces[i];
        for (int i = 0; i < 128; i++) s->texmap[i] = i * 3 + 1;
        // animations: one bone anim and three new-style flex anims touching different meshes
        auto mkanim = [&](const char *name, bool flex, std::vector<int> verts) {
            int a = s->m_Animations.AddToTail(); s_sourceanim_t &an = s->m_Animations[a]; memset(&an, 0, sizeof(an)); new (&an.rawanim) CUtlVectorAuto<s_bone_t *>();
            strcpy(an.animationname, name); an.numframes = 1;
            an.rawanim.AddToTail(new s_bone_t[1]);
            an.newStyleVertexAnimations = flex;
            if (flex) { an.numvanims[0] = verts.size(); an.vanim[0] = new s_vertanim_t[verts.size()]; for (size_t i = 0; i < verts.size(); i++) { memset(&an.vanim[0][i], 0, sizeof(s_vertanim_t)); an.vanim[0][i].vertex = verts[i]; } }
        };
        mkanim("base", false, {});
        std::vector<int> f0, f1, f2;
        for (int v = 0; v < s->mesh[s->meshindex[0]].numvertices; v += 3) f0.push_back(s->mesh[s->meshindex[0]].vertexoffset + v);
        for (int v = 0; v < s->mesh[s->meshindex[nm - 1]].numvertices; v += 5) f1.push_back(s->mesh[s->meshindex[nm - 1]].vertexoffset + v);
        for (int v = 0; v < total; v += 97) f2.push_back(v);
        mkanim("f0", true, f0); mkanim("f1", true, f1); mkanim("f2", true, f2);
        for (const char *n : {"f0", "f1", "f2", "f_missing"}) { int k = s->m_FlexKeys.AddToTail(); s_flexkey_t &fk = s->m_FlexKeys[k]; memset(&fk, 0, sizeof(fk)); fk.source = s; strcpy(fk.animationname, n); fk.frame = 0; }
        g_numflexkeys = 4;   // as if the original source registered its keys
        int clamp = 500 + rng() % 500;
        g_StudioMdlContext.maxVertexLimit = clamp; g_StudioMdlContext.maxVertexClamp = clamp;
        std::vector<std::string> e0; auto expected = collect(s, &e0, clamp, false);
        int origMI[32]; memcpy(origMI, s->meshindex, sizeof(origMI)); int origTex[128]; memcpy(origTex, s->texmap, sizeof(origTex));
        std::vector<s_vertanim_t> orig_f2(s->m_Animations[3].vanim[0], s->m_Animations[3].vanim[0] + s->m_Animations[3].numvanims[0]);
        ClampMaxVerticesPerModel(s);
        char title[128]; snprintf(title, sizeof(title), "[random %d%s] %d meshes, materials {%d,%d,%d,..}, clamp %d", round, flexOpt ? ", flexopt" : "", nm, mats[round][0], mats[round][1], mats[round][2], clamp);
        run_split_check(title, s, expected, clamp, nm, origMI, origTex);
        if (flexOpt) {
            // faces touching a flexed vertex go into the first pieces, static faces into later pieces of their own
            std::set<int> movingIds(f0.begin(), f0.end()); movingIds.insert(f1.begin(), f1.end()); movingIds.insert(f2.begin(), f2.end());
            bool seenStatic = false;
            for (int i = 0; i < (int) g_studiomdl.source.size(); i++) {
                const s_source_t *p = g_studiomdl.source[i];
                int nMoving = 0, nStatic = 0;
                for (int slot = 0; slot < p->nummeshes; slot++) {
                    const s_mesh_t &m = p->mesh[p->meshindex[slot]];
                    for (int f = m.faceoffset; f < m.faceoffset + m.numfaces; f++) {
                        const s_face_t &fc = p->face[f];
                        bool mv = movingIds.count(p->vertex[m.vertexoffset + fc.a].id) || movingIds.count(p->vertex[m.vertexoffset + fc.b].id) ||
                                  movingIds.count(p->vertex[m.vertexoffset + fc.c].id) || (fc.d != 0 && movingIds.count(p->vertex[m.vertexoffset + fc.d].id));
                        (mv ? nMoving : nStatic)++;
                    }
                }
                CHECK(!(nMoving && nStatic), "%s: piece %d mixes %d moving and %d static faces", title, i, nMoving, nStatic);
                if (nStatic) seenStatic = true;
                CHECK(!(seenStatic && nMoving), "%s: piece %d with moving faces comes after a static piece", title, i);
                if (nStatic && !nMoving) {
                    for (int k = 0; k < p->m_FlexKeys.Count(); k++)
                        CHECK(!strcmp(p->m_FlexKeys[k].animationname, "f_missing"), "%s: static piece %d carries flex key %s", title, i, p->m_FlexKeys[k].animationname);
                }
            }
        }
        // flex checks per piece
        CHECK((int)g_flexcalls.size() == (int) g_studiomdl.source.size() - 1, "expected one AddBodyFlexData per new piece, got %zu", g_flexcalls.size());
        for (auto &c : g_flexcalls) {
            CHECK(!c.controllers, "controllers must not be re-added for a split piece");
            const s_source_t *p = c.src;
            std::set<std::string> have; for (int k = 0; k < p->m_FlexKeys.Count(); k++) { have.insert(p->m_FlexKeys[k].animationname); CHECK(p->m_FlexKeys[k].source == p, "flex key source not the new piece"); }
            for (const char *n : {"f0", "f1", "f2"}) {
                const s_sourceanim_t *an = FindSourceAnim(p, n); CHECK(an != nullptr, "piece lost anim %s", n);
                if (!an) continue;
                bool moves = an->numvanims[0] > 0;
                CHECK(have.count(n) == (moves ? 1u : 0u), "key %s present=%d but anim moves %d verts", n, (int)have.count(n), an->numvanims[0]);
                for (int j = 0; j < an->numvanims[0]; j++) {
                    int nv = an->vanim[0][j].vertex; CHECK(nv >= 0 && nv < p->numvertices, "vanim vertex out of range");
                    if (nv >= 0 && nv < p->numvertices) {
                        int id = p->vertex[nv].id;
                        const std::string nn(n); std::vector<int> &ref = nn == "f0" ? f0 : nn == "f1" ? f1 : f2;
                        CHECK(std::find(ref.begin(), ref.end(), id) != ref.end(), "vanim of %s moved to a vertex (orig id %d) that does not animate", n, id);
                    }
                }
            }
            CHECK(have.count("f_missing") == 1, "key without animation must be kept");
        }
        // per piece: exactly the piece vertices that come from an animated original vertex carry a vertex animation
        for (auto &pair : std::vector<std::pair<const char *, std::vector<int>>>{{"f0", f0}, {"f1", f1}, {"f2", f2}}) {
            std::set<int> flexids(pair.second.begin(), pair.second.end());
            for (int i = 0; i < (int) g_studiomdl.source.size(); i++) {
                const s_sourceanim_t *an = FindSourceAnim(g_studiomdl.source[i], pair.first); if (!an) continue;
                std::set<int> actual, want;
                for (int j = 0; j < an->numvanims[0]; j++) { CHECK(actual.insert(an->vanim[0][j].vertex).second, "duplicate vanim vertex"); }
                for (int v = 0; v < g_studiomdl.source[i]->numvertices; v++) if (flexids.count(g_studiomdl.source[i]->vertex[v].id)) want.insert(v);
                CHECK(actual == want, "piece %d flex %s: %zu animated verts, expected %zu", i, pair.first, actual.size(), want.size());
            }
        }
    }
    }
    printf(g_fail ? "\nFAILED: %d checks\n" : "\nALL CHECKS PASSED\n", g_fail);
    return g_fail != 0;
}

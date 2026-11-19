#pragma once
#include <vector>
#include <array>
#include "../../include/tier1_v2/fixed_vector.hpp"
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <strings.h>
#define MAX_PATH 260
#define MAXSTUDIOSKINS 32
#define MAXSTUDIOANIMFRAMES 5000
#define MAXSTUDIONAME 64
#define MAXSTUDIOSRCBONES 512
#define MAXSTUDIOSEQUENCES 1524
#define MAXSTUDIOBODYPARTS 32
#define Assert(x) assert(x)
#define Q_stricmp strcasecmp

template <class T> class CUtlVector {
public:
    std::vector<T> v;
    int Count() const { return (int)v.size(); }
    T &operator[](int i) { assert(i >= 0 && i < Count()); return v[i]; }
    const T &operator[](int i) const { assert(i >= 0 && i < Count()); return v[i]; }
    T &Element(int i) { return (*this)[i]; }
    const T &Element(int i) const { return (*this)[i]; }
    int AddToTail() { v.emplace_back(); return Count() - 1; }
    int AddToTail(const T &x) { v.push_back(x); return Count() - 1; }
    int AddMultipleToTail(int n) { int s = Count(); v.resize(s + n); return s; }
    void EnsureCount(int n) { if (Count() < n) v.resize(n); }
    void SetCount(int n) { v.resize(n); }
    void RemoveAll() { v.clear(); }
    T *Base() { return v.data(); }
};
template <class T> class CUtlVectorAuto : public CUtlVector<T> {
public:
    T &operator[](int i) { if (i >= this->size()) this->v.resize(i + 1); return this->v[i]; }
};

struct Vector { float x, y, z; };
struct RadianEuler { float x, y, z; };
struct matrix3x4_t { float m[3][4]; };
struct s_bone_t { Vector pos; RadianEuler rot; };
struct s_node_t { char name[MAXSTUDIONAME]; int parent; };
struct s_face_t { s_face_t() { a = b = c = d = 0xFFFFFFFF; } uint32_t a, b, c, d; };
struct s_vertexinfo_t { int id; };   // 'id' = original vertex number, used to verify the remapping
struct s_vertanim_t { int vertex; float speed, side; Vector pos, normal; float wrinkle; };
struct s_mesh_t { uint32_t numvertices; uint32_t vertexoffset; int numfaces; int faceoffset; };
struct s_source_t;
struct s_flexkey_t { int flexdesc; int flexpair; s_source_t *source; char animationname[MAX_PATH]; int imodel; int frame; };
class s_sourceanim_t {
public:
    char animationname[MAX_PATH];
    int numframes, startframe, endframe;
    CUtlVectorAuto<s_bone_t *> rawanim;
    bool newStyleVertexAnimations;
    int *vanim_mapcount; int **vanim_map; int *vanim_flag;
    int numvanims[MAXSTUDIOANIMFRAMES];
    s_vertanim_t *vanim[MAXSTUDIOANIMFRAMES];
};
struct s_source_t {
    char filename[MAX_PATH];
    int version;
    bool isActiveModel;
    int numbones;
    std::array<s_node_t, MAXSTUDIOSRCBONES> localBone;
    std::array<matrix3x4_t, MAXSTUDIOSRCBONES> boneToPose;
    int texmap[MAXSTUDIOSKINS * 4];
    int nummeshes;
    int meshindex[MAXSTUDIOSKINS] = {0};
    s_mesh_t mesh[MAXSTUDIOSKINS] = {0};
    int numvertices; s_vertexinfo_t *vertex;
    int numfaces; s_face_t *face;
    CUtlVector<s_sourceanim_t> m_Animations;
    Vector adjust; float scale; RadianEuler rotation;
    bool bNoAutoDMXRules;
    CUtlVector<s_flexkey_t> m_FlexKeys;
    CUtlVector<int> m_CombinationControls, m_CombinationRules, m_FlexControllerRemaps;
    int m_nKeyStartIndex;
};
struct s_model_t { char name[MAXSTUDIONAME]; char filename[MAX_PATH]; s_source_t *source; };
struct s_bodypart_t { char name[MAXSTUDIONAME]; int nummodels; int base; CUtlVectorAuto<s_model_t *> pmodel; s_bodypart_t() { memset(this, 0, sizeof(s_bodypart_t)); } };
struct StudioMdlContext { int maxVertexLimit; int maxVertexClamp; };
extern StudioMdlContext g_StudioMdlContext;
// Mirrors the members of StudioMDLGlobalState that the extracted code uses
struct ShimGlobalState {
    vstd::FixedVector<s_source_t *, MAXSTUDIOSEQUENCES> source;
    int nummodels; CUtlVectorAuto<s_model_t *> model;
    int numbodyparts; s_bodypart_t bodypart[MAXSTUDIOBODYPARTS];
};
extern ShimGlobalState g_studiomdl;
void MdlWarning(const char *fmt, ...); void Warning(const char *fmt, ...);
s_sourceanim_t *FindSourceAnim(s_source_t *source, std::string_view animName);
const s_sourceanim_t *FindSourceAnim(const s_source_t *pSource, std::string_view pAnimName);
void AddBodyFlexData(s_source_t *pSource, int imodel, bool bAddControllers = true);

// flex optimization hooks (flexoptimize.h); the test drives them directly
bool FlexOptimizeIsActive();
std::vector<bool> FlexOptimizeMovingVertices(s_source_t *pSource);

// tier1 string helpers used by the clamp code
inline void Q_FileBase(const char *in, char *out, int outSize) {
    const char *base = in;
    for (const char *p = in; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    int n = 0;
    for (const char *p = base; *p && *p != '.' && n < outSize - 1; p++) out[n++] = *p;
    out[n] = 0;
}
#define V_snprintf snprintf

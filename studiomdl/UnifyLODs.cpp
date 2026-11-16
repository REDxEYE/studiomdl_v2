//========= Copyright � 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include <stdio.h>
#include <stdlib.h>

#include "common/cmdlib.h"
#include "common/scriplib.h"
#include "studio.h"
#include "studiomdl/studiomdl.h"
#include "tier1/strtools.h"
#include "tier1_v2/function_timing.hpp"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

// debugging only - enabling turns off remapping to create all lod vertexes as unique
// to ensure remapping logic does not introduce collapse anomalies
//#define UNIQUE_VERTEXES_FOR_LOD


//-----------------------------------------------------------------------------
// Forward declarations local to this file
//-----------------------------------------------------------------------------
class CVertexDictionary;

struct VertexInfo_t;

static void BuildBoneLODMapping(StudioMdlState &ctx, std::vector<int> &boneMap, int lodID);


//-----------------------------------------------------------------------------
// Makes sure all boneweights in a s_boneweight_t are valid
//-----------------------------------------------------------------------------
static void ValidateBoneWeight([[maybe_unused]] const StudioMdlState &ctx, const s_boneweight_t &boneWeight) {
#ifdef _DEBUG
    int i;
    if (boneWeight.weight[0] == 1.0f) {
        Assert(boneWeight.numbones == 1);
    }
    for (i = 0; i < boneWeight.numbones; i++) {
        Assert(boneWeight.bone[i] >= 0 && boneWeight.bone[i] < std::ssize(ctx.bones.bonetable));
    }

    float weight = 0.0f;
    for (i = 0; i < boneWeight.numbones; i++) {
        weight += boneWeight.weight[i];
    }
    Assert(fabs(weight - 1.0f) < 1e-3);
#endif
}


//-----------------------------------------------------------------------------
// Swap bones
//-----------------------------------------------------------------------------
static inline void SwapBones(s_boneweight_t &boneWeight, int nBone1, int nBone2) {
    // swap
    int nTmpBone = boneWeight.bone[nBone1];
    float flTmpWeight = boneWeight.weight[nBone1];
    boneWeight.bone[nBone1] = boneWeight.bone[nBone2];
    boneWeight.weight[nBone1] = boneWeight.weight[nBone2];
    boneWeight.bone[nBone2] = nTmpBone;
    boneWeight.weight[nBone2] = flTmpWeight;
}


//-----------------------------------------------------------------------------
// Sort the bone weight structure to be sorted by bone weight
//-----------------------------------------------------------------------------
static void SortBoneWeightByWeight(s_boneweight_t &boneWeight) {
    // bubble sort the bones by weight. . .put the largest weight first.
    for (int j = boneWeight.numbones; j > 1; j--) {
        for (int k = 0; k < j - 1; k++) {
            if (boneWeight.weight[k] >= boneWeight.weight[k + 1])
                continue;

            SwapBones(boneWeight, k, k + 1);
        }
    }
}


//-----------------------------------------------------------------------------
// Sort the bone weight structure to be sorted by bone index
//-----------------------------------------------------------------------------
static void SortBoneWeightByIndex(s_boneweight_t &boneWeight) {
    // bubble sort the bones by index. . .put the smallest index first.
    for (int j = boneWeight.numbones; j > 1; j--) {
        for (int k = 0; k < j - 1; k++) {
            if (boneWeight.bone[k] <= boneWeight.bone[k + 1])
                continue;

            SwapBones(boneWeight, k, k + 1);
        }
    }
}


//-----------------------------------------------------------------------------
// A vertex format
//-----------------------------------------------------------------------------
struct VertexInfo_t {
    Vector m_Position;
    Vector m_Normal;
    vstd::FixedVector<Vector2D, MAXSTUDIOTEXCOORDS> m_TexCoord;
    Vector4D m_TangentS;
    s_boneweight_t m_BoneWeight;
    int m_nLodFlag;
};


//-----------------------------------------------------------------------------
// Stores all vertices in the vertex dictionary
//-----------------------------------------------------------------------------
class CVertexDictionary {
public:
    explicit CVertexDictionary(StudioMdlState &ctx);

    // Adds a vertex to the dictionary
    int AddVertex(const VertexInfo_t &srcVertex);

    int AddVertexFromSource(const s_source_t *pSrc, int nVertexIndex, int nLod);

    // Iteration
    int VertexCount() const;

    VertexInfo_t &Vertex(int i);

    const VertexInfo_t &Vertex(int i) const;

    int RootLODVertexStart() const;

    int RootLODVertexEnd() const;

    // Gets the vertex count for the previous LOD
    int PrevLODVertexCount() const;

    // Marks the dictionary as starting defining vertices for a new LOD
    void StartNewLOD();

    void SetRootVertexRange(int start, int end);

private:
    StudioMdlState &m_ctx;
    std::vector<VertexInfo_t> m_Verts;
    int m_nPrevLODCount;
    int m_nRootLODStart;
    int m_nRootLODEnd;
};


//-----------------------------------------------------------------------------
// Copies in a particular vertex from the s_source_t
//-----------------------------------------------------------------------------
CVertexDictionary::CVertexDictionary(StudioMdlState &ctx) : m_ctx(ctx) {
    m_nPrevLODCount = 0;
}


//-----------------------------------------------------------------------------
// Accessor
//-----------------------------------------------------------------------------
inline VertexInfo_t &CVertexDictionary::Vertex(int i) {
    return m_Verts[i];
}

inline const VertexInfo_t &CVertexDictionary::Vertex(int i) const {
    return m_Verts[i];
}


//-----------------------------------------------------------------------------
// Gets the vertex count for the previous LOD
//-----------------------------------------------------------------------------
inline int CVertexDictionary::PrevLODVertexCount() const {
    return m_nPrevLODCount;
}


inline int CVertexDictionary::RootLODVertexStart() const {
    return m_nRootLODStart;
}


inline int CVertexDictionary::RootLODVertexEnd() const {
    return m_nRootLODEnd;
}


//-----------------------------------------------------------------------------
// Marks the dictionary as starting defining vertices for a new LOD
//-----------------------------------------------------------------------------
void CVertexDictionary::StartNewLOD() {
    m_nPrevLODCount = VertexCount();
}


void CVertexDictionary::SetRootVertexRange(int start, int end) {
    m_nRootLODStart = start;
    m_nRootLODEnd = end;
}


//-----------------------------------------------------------------------------
// Adds a vertex to the dictionary
//-----------------------------------------------------------------------------
int CVertexDictionary::AddVertex(const VertexInfo_t &srcVertex) {
    const int nDstVertID = std::ssize(m_Verts);
    VertexInfo_t &vertex = m_Verts.emplace_back(srcVertex);
    ValidateBoneWeight(m_ctx, vertex.m_BoneWeight);
    SortBoneWeightByIndex(vertex.m_BoneWeight);
    ValidateBoneWeight(m_ctx, vertex.m_BoneWeight);

    return nDstVertID;
}


//-----------------------------------------------------------------------------
// Copies in a particular vertex from the s_source_t
//-----------------------------------------------------------------------------
int CVertexDictionary::AddVertexFromSource(const s_source_t *pSrc, int nVertexIndex, int nLod) {
    const int nDstVertID = std::ssize(m_Verts);
    VertexInfo_t &vertex = m_Verts.emplace_back();

    const s_vertexinfo_t &srcVertex = pSrc->m_GlobalVertices[nVertexIndex];
    vertex.m_Position = srcVertex.position;
    vertex.m_Normal = srcVertex.normal;
    vertex.m_TangentS = srcVertex.tangentS;
    vertex.m_BoneWeight = srcVertex.boneweight;
    vertex.m_nLodFlag = 1 << nLod;

    vertex.m_TexCoord.resize(srcVertex.texcoord.size());

    for (int i = 0; i < srcVertex.texcoord.size(); ++i) {
        vertex.m_TexCoord[i] = srcVertex.texcoord[i];
    }

    ValidateBoneWeight(m_ctx, vertex.m_BoneWeight);
    SortBoneWeightByIndex(vertex.m_BoneWeight);
    ValidateBoneWeight(m_ctx, vertex.m_BoneWeight);

    return nDstVertID;
}


//-----------------------------------------------------------------------------
// How many vertices in the dictionary?
//-----------------------------------------------------------------------------
int CVertexDictionary::VertexCount() const {
    return std::ssize(m_Verts);
}


s_source_t *GetModelLODSource(const char *pModelName,
                              const LodScriptData_t &scriptLOD, bool *pFound) {
    // When doing LOD replacement, ignore all path + extension information
    char *pTempBuf = (char *) _alloca(Q_strlen(pModelName) + 1);

    // Strip off extensions for the source...
    strcpy(pTempBuf, pModelName);
    char *pDot = strrchr(pTempBuf, '.');
    if (pDot) {
        *pDot = 0;
    }

    for (int i = 0; i < std::ssize(scriptLOD.modelReplacements); i++) {
        // FIXME: Should we strip off path information?
//		char* pSlash = strrchr( pTempBuf1, '\\' );
//		char* pSlash2 = strrchr( pTempBuf1, '/' );
//		if (pSlash2 > pSlash)
//			pSlash = pSlash2;
//		if (!pSlash)
//			pSlash = pTempBuf1;

        if (vstd::iequals(scriptLOD.modelReplacements[i].GetSrcName(), pTempBuf)) {
            *pFound = true;
            return scriptLOD.modelReplacements[i].m_pSource;
        }
    }

    *pFound = false;
    return nullptr;
}


//-----------------------------------------------------------------------------
// Tolerances for all fields of the vertex
//-----------------------------------------------------------------------------
#define POSITION_EPSILON    0.05f
#define TEXCOORD_EPSILON    0.01f
#define NORMAL_EPSILON        10.0f    // in degrees
#define TANGENT_EPSILON        10.0f    // in degrees
#define BONEWEIGHT_EPSILON    0.05f
#define EXTRADATA_EPSILON    0.01f

#define UNMATCHED_BONE_WEIGHT 1.0f

//-----------------------------------------------------------------------------
// Computes error between two positions; returns false if the error is too great
//-----------------------------------------------------------------------------
bool ComparePositionFuzzy(const Vector &p1, const Vector &p2, float &flError) {
    Vector vecDelta;
    VectorSubtract(p1, p2, vecDelta);
    flError = DotProduct(vecDelta, vecDelta);
    return (flError <= (POSITION_EPSILON * POSITION_EPSILON));
}


//-----------------------------------------------------------------------------
// Computes error between two normals; returns false if the error is too great
//-----------------------------------------------------------------------------
bool CompareNormalFuzzy(const Vector &n1, const Vector &n2, float &flError) {
    static const float flEpsilon = cos(DEG2RAD(NORMAL_EPSILON));

    Vector v1, v2;
    v1 = n1;
    v2 = n2;
    VectorNormalize(v1);
    VectorNormalize(v2);
    float flDot = DotProduct(v1, v2);
    flError = 1.0F - flDot;
    return (flDot >= flEpsilon);
}

//-----------------------------------------------------------------------------
// Computes error between two tangentS vectors; returns false if the error is too great
//-----------------------------------------------------------------------------
bool CompareTangentSFuzzy(const Vector4D &n1, const Vector4D &n2, float &flError) {
    static const float flEpsilon = cos(DEG2RAD(TANGENT_EPSILON));

    Vector4D v1, v2;
    v1 = n1;
    v2 = n2;

    if (v1.w != v2.w) {
        // must match as -1 or 1
        flError = 2;
        return false;
    }

    VectorNormalize(v1.AsVector3D());
    VectorNormalize(v2.AsVector3D());
    float flDot = DotProduct(v1.AsVector3D(), v2.AsVector3D());

    // error ranges from [0..2]
    flError = 1.0F - flDot;

    return (flDot >= flEpsilon);
}

//-----------------------------------------------------------------------------
// Computes error between two texcoords; returns false if the error is too great
//-----------------------------------------------------------------------------
bool CompareTexCoordsFuzzy(const vstd::FixedVector<Vector2D, MAXSTUDIOTEXCOORDS> &t1,
                           const vstd::FixedVector<Vector2D, MAXSTUDIOTEXCOORDS> &t2,
                           float &flError) {
    Vector2D vecError;
    if (t1.size()!=t2.size()) {
        MdlError("Texcoord count mismatch");
        return false;
    }

    flError = 0.0f;
    for (size_t i = 0; i < t1.size(); ++i) {
        vecError[0] = fabs(t2[i][0] - t1[i][0]);
        vecError[1] = fabs(t2[i][1] - t1[i][1]);
        flError += vecError.LengthSqr();
    }

    return (flError <= (TEXCOORD_EPSILON * TEXCOORD_EPSILON));
}

//-----------------------------------------------------------------------------
// Computes the error between two bone weights, returns false if they are too far
//-----------------------------------------------------------------------------
bool CompareBoneWeightsFuzzy(const s_boneweight_t &b1, const s_boneweight_t &b2, float &flError) {
    // This is a list of which bones that exist in b1 also exist in b2.
    // Use the index to figure out where in the array for b2 that the corresponding bone in b1 is.
    int nMatchingBones = 0;
    int pBoneIndexMap1[MAX_NUM_BONES_PER_VERT];
    int pBoneIndexMap2[MAX_NUM_BONES_PER_VERT];

    int i;
    for (i = 0; i < b2.numbones; ++i) {
        pBoneIndexMap2[i] = -1;
    }

    for (i = 0; i < b1.numbones; ++i) {
        pBoneIndexMap1[i] = -1;
        for (int j = 0; j < b2.numbones; ++j) {
            if (b2.bone[j] == b1.bone[i]) {
                pBoneIndexMap1[i] = j;
                pBoneIndexMap2[j] = i;
                ++nMatchingBones;
                break;
            }
        }
    }

    // If no bones match, we're done
    if (!nMatchingBones) {
        flError = FLT_MAX;
        return false;
    }

    // At least one bone matches, so we're going to consider this vertex as a potential match
    // This loop will take care of figuring out the error for all bones that exist in
    // b1 alone, and all bones that exist in b1 and b2
    flError = 0;
    for (i = 0; i < b1.numbones; ++i) {
        // If we didn't find a match for this bone, compute a more expensive weight
        if (pBoneIndexMap1[i] == -1) {
            flError += b1.weight[i] * b1.weight[i] * UNMATCHED_BONE_WEIGHT;
            continue;
        }

        float flDeltaWeight = fabs(b1.weight[i] - b2.weight[pBoneIndexMap1[i]]);
        flError += flDeltaWeight * flDeltaWeight;
    }

    // This loop will take care of figuring out the error for all bones that exist in b2 alone
    for (i = 0; i < b2.numbones; ++i) {
        // If we didn't find a match for this bone, compute a more expensive weight
        if (pBoneIndexMap2[i] == -1) {
            flError += b2.weight[i] * b2.weight[i] * UNMATCHED_BONE_WEIGHT;
        }
    }

    // This renormalizes the error. The error will become greater with the total
    // number of bones in the two vertices.
    flError /= sqrt((float) (b1.numbones + b2.numbones));
    return (flError <= BONEWEIGHT_EPSILON);
}


//-----------------------------------------------------------------------------
// Searches for a material in the texture list
//-----------------------------------------------------------------------------
int FindMaterialByName(StudioMdlState &ctx, const std::string_view pMaterialName) {
    for (int i = 0; i < ctx.materials.textures.size(); i++) {
        if (vstd::iequals(pMaterialName, ctx.materials.textures[i].name)) {
            return i;
        }
    }
    return -1;
}

static s_mesh_t *FindMeshByMaterial(s_source_t *pSrc, int nMaterialID) {
    for (s_mesh_t &mesh: pSrc->meshes) {
        if (mesh.material == nMaterialID)
            return &mesh;
    }

    // this mesh/material doesn't exist at this lod.
    return nullptr;
}


static s_mesh_t *FindOrCullMesh(StudioMdlState &ctx, int nLodID, s_source_t *pSrc, int nMaterialID) {
    // possibly marked for removal via $removemesh
    // determine mesh name
    int nTextureID = MaterialToTexture(ctx, nMaterialID);
    if (nTextureID == -1) {
        MdlError("Unknown Texture for Material {}\n", nMaterialID);
    }

    const StudioPath baseMeshName = vstd::paths::file_base(ctx.materials.textures[nTextureID].name.view());
    for (int i = 0; i < std::ssize(ctx.body.ScriptLODs[nLodID].meshRemovals); i++) {
        const auto meshRemovalName = ctx.body.ScriptLODs[nLodID].meshRemovals[i].GetSrcName();
        const StudioPath baseRemovalName = vstd::paths::file_base(meshRemovalName);

        if (vstd::iequals(baseRemovalName, baseMeshName)) {
            // mesh has been marked for removal
            return nullptr;
        }
    }

    s_mesh_t *pMesh = FindMeshByMaterial(pSrc, nMaterialID);
    return pMesh;
}


static void CopyVerts(int nLodID, const s_source_t *pSrc, const s_mesh_t *pSrcMesh, CVertexDictionary &vertexDict,
                      s_mesh_t *pDstMesh, int *pMeshVertIndexMap) {
    // populate the dictionary with the verts
    for (uint32_t srcVertID = 0; srcVertID < pSrcMesh->numvertices; srcVertID++) {
        uint32_t nVertexIndex = pSrcMesh->vertexoffset + srcVertID;
        pMeshVertIndexMap[nVertexIndex] =
                vertexDict.AddVertexFromSource(pSrc, nVertexIndex, nLodID) - pDstMesh->vertexoffset;
    }

    pDstMesh->numvertices = pSrcMesh->numvertices;
}

static void
CopyFaces(const s_source_t *pSrc, const s_mesh_t *pSrcMesh, std::vector<s_face_t> &faces, s_mesh_t *pDstMesh) {
    int srcFaceID;
    for (srcFaceID = 0; srcFaceID < pSrcMesh->numfaces; srcFaceID++) {
        int srcID = srcFaceID + pSrcMesh->faceoffset;
        s_face_t *pSrcFace = &pSrc->face[srcID];
        s_face_t *pDstFace = &faces.emplace_back();
        pDstFace->a = pSrcFace->a;
        pDstFace->b = pSrcFace->b;
        pDstFace->c = pSrcFace->c;
        pDstFace->d = pSrcFace->d;
        pDstMesh->numfaces++;
    }
}

#define IGNORE_POSITION        0x01
#define IGNORE_TEXCOORD        0x02
#define IGNORE_BONEWEIGHT    0x04
#define IGNORE_NORMAL        0x08
#define IGNORE_TANGENTS        0x10

//-----------------------------------------------------------------------------
// return -1 if there is no match. The index returned is used to index into vertexDict.
//-----------------------------------------------------------------------------
static int FindVertexWithinVertexDictionary(const VertexInfo_t &find,
                                            const CVertexDictionary &vertexDict, int nStartVert, int nEndVert,
                                            int fIgnore) {
    int nBestIndex = -1;
    float flPositionError = 0.0f;
    float flNormalError = 0.0f;
    float flTangentSError = 0.0f;
    float flTexcoordError = 0.0f;
    float flBoneWeightError = 0.0f;
    float flMinPositionError = FLT_MAX;
    float flMinNormalError = FLT_MAX;
    float flMinTangentSError = FLT_MAX;
    float flMinTexcoordError = FLT_MAX;
    float flMinBoneWeightError = FLT_MAX;
    bool bFound;

    if (fIgnore & IGNORE_POSITION) {
        flMinPositionError = 0;
        flPositionError = 0;
    }

    if (fIgnore & IGNORE_TEXCOORD) {
        flMinTexcoordError = 0;
        flTexcoordError = 0;
    }

    if (fIgnore & IGNORE_BONEWEIGHT) {
        flMinBoneWeightError = 0;
        flBoneWeightError = 0;
    }

    if (fIgnore & IGNORE_NORMAL) {
        flMinNormalError = 0;
        flNormalError = 0;
    }

    if (fIgnore & IGNORE_TANGENTS) {
        flMinTangentSError = 0;
        flTangentSError = 0;
    }

    for (int nVertexIndex = nStartVert; nVertexIndex < nEndVert; ++nVertexIndex) {
        // see if the position is reasonable
        if (!(fIgnore & IGNORE_POSITION) &&
            !ComparePositionFuzzy(find.m_Position, vertexDict.Vertex(nVertexIndex).m_Position, flPositionError))
            continue;

        if (!(fIgnore & IGNORE_TEXCOORD) &&
            !CompareTexCoordsFuzzy(find.m_TexCoord, vertexDict.Vertex(nVertexIndex).m_TexCoord, flTexcoordError))
            continue;

        if (!(fIgnore & IGNORE_BONEWEIGHT) &&
            !CompareBoneWeightsFuzzy(find.m_BoneWeight, vertexDict.Vertex(nVertexIndex).m_BoneWeight,
                                     flBoneWeightError))
            continue;

        if (!(fIgnore & IGNORE_NORMAL) &&
            !CompareNormalFuzzy(find.m_Normal, vertexDict.Vertex(nVertexIndex).m_Normal, flNormalError))
            continue;

        if (!(fIgnore & IGNORE_TANGENTS) &&
            !CompareTangentSFuzzy(find.m_TangentS, vertexDict.Vertex(nVertexIndex).m_TangentS, flTangentSError))
            continue;

        // the vert with minimum error is the best or exact candidate
        bFound = false;
        if (flMinPositionError > flPositionError) {
            bFound = true;
        } else if (flMinPositionError == flPositionError) {
            if (flMinTexcoordError > flTexcoordError) {
                bFound = true;
            } else if (flMinTexcoordError == flTexcoordError) {
                if (flMinBoneWeightError > flBoneWeightError) {
                    bFound = true;
                } else if (flMinBoneWeightError == flBoneWeightError) {
                    if (flMinNormalError > flNormalError) {
                        bFound = true;
                    } else if (flMinNormalError == flNormalError) {
                        if (flMinTangentSError >= flTangentSError) {
                            bFound = true;
                        }
                    }
                }
            }
        }

        if (!bFound)
            continue;

        flMinPositionError = flPositionError;
        flMinTexcoordError = flTexcoordError;
        flMinBoneWeightError = flBoneWeightError;
        flMinNormalError = flNormalError;
        flMinTangentSError = flTangentSError;
        nBestIndex = nVertexIndex;
    }

    return nBestIndex;
}


//-----------------------------------------------------------------------------
// Use position, normal, and texcoord checks across the entire model to find a boneweight
//-----------------------------------------------------------------------------
static void
FindBoneWeightWithinModel(const VertexInfo_t &searchVertex, const s_source_t *pSrc, s_boneweight_t &boneWeight,
                          int fIgnore) {
    int nBestIndex = -1;
    float flPositionError = 0.0f;
    float flNormalError = 0.0f;
    float flTangentSError = 0.0f;
    float flTexcoordError = 0.0f;
    float flMinPositionError = FLT_MAX;
    float flMinNormalError = FLT_MAX;
    float flMinTangentSError = FLT_MAX;
    float flMinTexcoordError = FLT_MAX;
    bool bFound;

    if (fIgnore & IGNORE_NORMAL) {
        flMinNormalError = 0;
        flNormalError = 0;
    }

    if (fIgnore & IGNORE_TEXCOORD) {
        flMinTexcoordError = 0;
        flTexcoordError = 0;
    }

    if (fIgnore & IGNORE_TANGENTS) {
        flMinTangentSError = 0;
        flTangentSError = 0;
    }

    const int nVertexCount = std::ssize(pSrc->m_GlobalVertices);
    for (int i = 0; i < nVertexCount; i++) {
        const s_vertexinfo_t &srcVertex = pSrc->m_GlobalVertices[i];

        // Compute error metrics
        ComparePositionFuzzy(searchVertex.m_Position, srcVertex.position, flPositionError);

        if (!(fIgnore & IGNORE_NORMAL)) {
            CompareNormalFuzzy(searchVertex.m_Normal, srcVertex.normal, flNormalError);
        }

        if (!(fIgnore & IGNORE_TEXCOORD)) {
            CompareTexCoordsFuzzy(searchVertex.m_TexCoord, srcVertex.texcoord, flTexcoordError);
        }

        if (!(fIgnore & IGNORE_TANGENTS)) {
            CompareTangentSFuzzy(searchVertex.m_TangentS, srcVertex.tangentS, flTangentSError);
        }

        // the vert with minimum error is the best or exact candidate
        bFound = false;
        if (flMinPositionError > flPositionError) {
            bFound = true;
        } else if (flMinPositionError == flPositionError) {
            if (flMinTexcoordError > flTexcoordError) {
                bFound = true;
            } else if (flMinTexcoordError == flTexcoordError) {
                if (flMinNormalError > flNormalError) {
                    bFound = true;
                } else if (flMinNormalError == flNormalError) {
                    if (flMinTangentSError >= flTangentSError) {
                        bFound = true;
                    }
                }
            }
        }

        if (bFound) {
            flMinPositionError = flPositionError;
            flMinTexcoordError = flTexcoordError;
            flMinNormalError = flNormalError;
            flMinTangentSError = flTangentSError;
            nBestIndex = i;
        }
    }

    if (nBestIndex == -1) {
        MdlError("Encountered a mesh with no vertices!\n");
    }

    memcpy(&boneWeight, &pSrc->m_GlobalVertices[nBestIndex].boneweight, sizeof(s_boneweight_t));
}


//-----------------------------------------------------------------------------
// Modify the bone weights in all of the vertices....
//-----------------------------------------------------------------------------
static void RemapBoneWeights(const std::vector<int> &boneMap, s_boneweight_t &boneWeight) {
    for (int i = 0; i < boneWeight.numbones; i++) {
        Assert(boneWeight.bone[i] >= 0 && boneWeight.bone[i] < std::ssize(boneMap));
        boneWeight.bone[i] = boneMap[boneWeight.bone[i]];
    }
}


//-----------------------------------------------------------------------------
// After the remapping, we may get multiple instances of the same bone
// which we want to collapse into a single bone
//-----------------------------------------------------------------------------
static void CollapseBoneWeights(const StudioMdlState &ctx, s_boneweight_t &boneWeight) {
    // We need the bones to be sorted by bone index for the loop right below
    SortBoneWeightByIndex(boneWeight);

    for (int i = 0; i < boneWeight.numbones - 1; i++) {
        if (boneWeight.bone[i] != boneWeight.bone[i + 1])
            continue;

        // add i+1's weight to i since they have the same bone index
        boneWeight.weight[i] += boneWeight.weight[i + 1];

        // remove i+1
        for (int j = i + 1; j < boneWeight.numbones - 1; j++) {
            boneWeight.bone[j] = boneWeight.bone[j + 1];
            boneWeight.weight[j] = boneWeight.weight[j + 1];
        }
        --boneWeight.numbones;

        // Gotta step back one, may have many bones collapsing into one
        --i;
    }

    ValidateBoneWeight(ctx, boneWeight);
}


//-----------------------------------------------------------------------------
// Find a matching vertex within the root lod 
//-----------------------------------------------------------------------------
static void CalculateBoneWeightFromRootLod(StudioMdlState &ctx, const VertexInfo_t &searchVertex, CVertexDictionary &vertexDict,
                                           const s_source_t *pRootLODSrc, VertexInfo_t &idealVertex) {
    idealVertex = searchVertex;

    // Look through the part of the vertex dictionary associated with the root LODs for a match
    // bone weights are not defined properly in SMDs for lower LODs, so don't consider
    // we can only accept the boneweight from the root LOD
    int nFlags = ctx.options.bSkinnedLODs ? IGNORE_TANGENTS : IGNORE_BONEWEIGHT | IGNORE_TANGENTS;
    int nVertexDictID = FindVertexWithinVertexDictionary(searchVertex, vertexDict,
                                                         vertexDict.RootLODVertexStart(), vertexDict.RootLODVertexEnd(),
                                                         nFlags);
    if (nVertexDictID != -1) {
        Assert(nVertexDictID >= vertexDict.RootLODVertexStart() && nVertexDictID < vertexDict.RootLODVertexEnd());
        Assert(nVertexDictID >= 0 && nVertexDictID < vertexDict.VertexCount());

        // found vertex in dictionary
#ifdef UNIQUE_VERTEXES_FOR_LOD
        if ( !g_bSkinnedLODs )
        {
            // keep entry vertex and fill in the missing bone weight attribute
            idealVertex.m_BoneWeight = vertexDict.Vertex( nVertexDictID ).m_BoneWeight;
        }
        else
#else
        // discard entry vertex in favor of best match
        // this ensures all the attributes, including bone weight are correct for that vertex
        // the worst case is that the vertex is not an *exact* match for entry attributes just a "close" match
        idealVertex = vertexDict.Vertex(nVertexDictID);
#endif
        return;
    }

    // In this case, we didn't find anything within the tolerance, so we need to
    // do a *positional check only* to give us a bone weight to assign to this vertex.
    if (!ctx.options.bSkinnedLODs) {
        FindBoneWeightWithinModel(searchVertex, pRootLODSrc, idealVertex.m_BoneWeight,
                                  IGNORE_BONEWEIGHT | IGNORE_TANGENTS);
    }
}

//-----------------------------------------------------------------------------
// Find a matching vertex
//-----------------------------------------------------------------------------
static void CalculateIdealVert(const VertexInfo_t &searchVertex, CVertexDictionary &vertexDict,
                               const s_mesh_t *pVertexDictMesh, const s_source_t *pRootLODSrc,
                               VertexInfo_t &idealVertex) {
#ifndef UNIQUE_VERTEXES_FOR_LOD
    // Only look through the part of the vertex dictionary associated with all *higher* LODs for a match
    int nVertexDictID = FindVertexWithinVertexDictionary(searchVertex, vertexDict,
                                                         pVertexDictMesh->vertexoffset, vertexDict.PrevLODVertexCount(),
                                                         0);
    if (nVertexDictID != -1) {
        Assert(nVertexDictID >= pVertexDictMesh->vertexoffset && nVertexDictID < vertexDict.PrevLODVertexCount());
        Assert(nVertexDictID >= 0 && nVertexDictID < vertexDict.VertexCount());

        // found vertex in dictionary
        idealVertex = vertexDict.Vertex(nVertexDictID);
        return;
    }
#endif

    // could not find a tolerant match
    // the search vertex is unique
    idealVertex = searchVertex;
}




//-----------------------------------------------------------------------------
// Are two bone-weight structures equal?
//-----------------------------------------------------------------------------
static bool AreBoneWeightsEqual(const s_boneweight_t &b1, const s_boneweight_t &b2) {
    // Have to have the same number of bones
    if (b1.numbones != b2.numbones)
        return false;

    // This is a list of which bones that exist in b1 also exist in b2.
    // Use the index to figure out where in the array for b2 that the corresponding bone in b1 is.
    int nMatchingBones = 0;
    int pBoneIndexMap[MAX_NUM_BONES_PER_VERT];

    int i;
    for (i = 0; i < b1.numbones; ++i) {
        pBoneIndexMap[i] = -1;
        for (int j = 0; j < b2.numbones; ++j) {
            if (b2.bone[j] == b1.bone[i]) {
                pBoneIndexMap[i] = j;
                ++nMatchingBones;
                break;
            }
        }
    }

    // If we aren't using the same bone indices, we're done
    if (nMatchingBones != b1.numbones)
        return false;

    // Check to see if the weights are the same
    for (i = 0; i < b1.numbones; ++i) {
        Assert(pBoneIndexMap[i] != -1);
        if (b1.weight[i] != b2.weight[pBoneIndexMap[i]])
            return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Finds an *exact* requested vertex in the dictionary
//-----------------------------------------------------------------------------
static int
FindVertexInDictionaryExact(CVertexDictionary &vertexDict, int nStartVert, int nEndVert, const VertexInfo_t &vertex) {
    for (int nVertID = nStartVert; nVertID < nEndVert; ++nVertID) {
        if (vertexDict.Vertex(nVertID).m_Position != vertex.m_Position)
            continue;

        if (!AreBoneWeightsEqual(vertexDict.Vertex(nVertID).m_BoneWeight, vertex.m_BoneWeight))
            continue;

        bool bMatch = true;
        for (int i = 0; i < vertex.m_TexCoord.size(); ++i) {
            if (vertexDict.Vertex(nVertID).m_TexCoord[i] != vertex.m_TexCoord[i]) {
                bMatch = false;
                break;
            }
        }
        if (!bMatch)
            continue;

        if (vertexDict.Vertex(nVertID).m_Normal != vertex.m_Normal)
            continue;

        if (vertexDict.Vertex(nVertID).m_TangentS != vertex.m_TangentS)
            continue;

        return nVertID;
    }

    return -1;
}


//-----------------------------------------------------------------------------
// Finds the *exact* requested vertex in the dictionary or creates it
//-----------------------------------------------------------------------------
static int FindOrCreateExactVertexInDictionary(CVertexDictionary &vertexDict,
                                               const VertexInfo_t &vertex, s_mesh_t *pDstMesh) {
    int nMeshVertID = FindVertexInDictionaryExact(vertexDict, pDstMesh->vertexoffset,
                                                  pDstMesh->vertexoffset + pDstMesh->numvertices, vertex);
    if (nMeshVertID != -1) {
        // flag vertex for what LoD's are using it
        vertexDict.Vertex(nMeshVertID).m_nLodFlag |= vertex.m_nLodFlag;
        return nMeshVertID - pDstMesh->vertexoffset;
    }

    nMeshVertID = vertexDict.AddVertex(vertex);
    ++pDstMesh->numvertices;
    return nMeshVertID - pDstMesh->vertexoffset;
}


//-----------------------------------------------------------------------------
// Indicates a particular set of bones is used by a particular LOD
//-----------------------------------------------------------------------------
static void MarkBonesUsedByLod(StudioMdlState &ctx, const s_boneweight_t &boneWeight, int nLodID) {
    for (int j = 0; j < boneWeight.numbones; ++j) {
        int nGlobalBoneID = boneWeight.bone[j];
        s_bonetable_t *pBone = &ctx.bones.bonetable[nGlobalBoneID];
        pBone->flags |= BoneUsedByVertexAtLod(nLodID);
    }
}



//-----------------------------------------------------------------------------
// In the non-top LOD, look for vertices that would be appropriate from the
// vertex dictionary, and use them if you find them, or add new vertices to the 
// vertex dictionary if not and use those new vertices.
//-----------------------------------------------------------------------------
static void CreateLODVertsInDictionary(StudioMdlState &ctx, int nLodID, const s_source_t *pRootLODSrc, s_source_t *pCurrentLODSrc,
                                       const s_mesh_t *pCurrLODMesh, s_mesh_t *pVertexDictMesh,
                                       CVertexDictionary &vertexDict, int *pMeshVertIndexMap) {
    // this function is specific to lods and not the root
    Assert(nLodID);

    int nNumCurrentVerts = vertexDict.VertexCount();

    // Used to control where we look for vertices + merging rules
    vertexDict.StartNewLOD();

    std::vector<int> boneMap;
    BuildBoneLODMapping(ctx, boneMap, nLodID);

    for (int nSrcVertID = 0; nSrcVertID < pCurrLODMesh->numvertices; ++nSrcVertID) {
        int nSrcID = nSrcVertID + pCurrLODMesh->vertexoffset;

        // candidate vertex
        // vertices at lower LODs have bogus boneweights assigned
        // must get the boneweight from the nearest or exact vertex at root lod
        const s_vertexinfo_t &srcVertex = pCurrentLODSrc->m_GlobalVertices[nSrcID];
        VertexInfo_t vertex;
        vertex.m_Position = srcVertex.position;
        vertex.m_Normal = srcVertex.normal;
        vertex.m_TangentS = srcVertex.tangentS;

        vertex.m_TexCoord.resize(srcVertex.texcoord.size());

        for (int i = 0; i < srcVertex.texcoord.size(); ++i) {
            vertex.m_TexCoord[i] = srcVertex.texcoord[i];
        }

        if (ctx.options.bSkinnedLODs) {
            vertex.m_BoneWeight = srcVertex.boneweight;
        } else {
#ifdef _DEBUG
            memset(&vertex.m_BoneWeight, 0xDD, sizeof(s_boneweight_t));
#endif
        }

        // determine the best bone weight for the desired vertex within the root lod only
        // the root lod contains no bone remappings
        // this ensures we get a vertex with its matched proper boneweight assignment
        VertexInfo_t idealVertex;
        CalculateBoneWeightFromRootLod(ctx, vertex, vertexDict, pRootLODSrc, idealVertex);

        // try again to match the candidate vertex
        // determine the ideal vertex with desired remapped boneweight
        vertex = idealVertex;
        CalculateIdealVert(vertex, vertexDict, pVertexDictMesh, pRootLODSrc, idealVertex);

        // remap bone
        RemapBoneWeights(boneMap, idealVertex.m_BoneWeight);
        CollapseBoneWeights(ctx, idealVertex.m_BoneWeight);
        SortBoneWeightByWeight(idealVertex.m_BoneWeight);

        // FIXME: this is marking bones based on the slammed vertex data
        MarkBonesUsedByLod(ctx, idealVertex.m_BoneWeight, nLodID);

        // tag ideal vertex as being part of the current lod
        idealVertex.m_nLodFlag = 1 << nLodID;

        // Find the exact vertex or create it in the dictionary
        int nMeshVertID = FindOrCreateExactVertexInDictionary(vertexDict, idealVertex, pVertexDictMesh);

        // Indicate where in the higher LODs the vertex we selected resides
        pMeshVertIndexMap[nSrcID] = nMeshVertID;
    }

    int nNewVertsCreated = vertexDict.VertexCount() - nNumCurrentVerts;
    if (!ctx.options.quiet && nNewVertsCreated) {
        std::print("Lod {}: vertexes: {} ({} new)\n", nLodID, vertexDict.VertexCount(), nNewVertsCreated);
    }
}


//-----------------------------------------------------------------------------
// Copy the vertex dictionary to the finalized processed data
// Leaves the source data intact, necessary for later processes.
// Routines can then choose which data they operate on
//-----------------------------------------------------------------------------
static void SetProcessedWithDictionary(s_model_t *pSrcModel, CVertexDictionary &vertexDict,
                                       std::vector<s_face_t> &faces, std::vector<s_mesh_t> &meshes,
                                       int *pMeshVertIndexMaps[MAX_NUM_LODS]) {
    int i;

    s_loddata_t *pLodData = new s_loddata_t;
    memset(pLodData, 0, sizeof(s_loddata_t));

    pSrcModel->m_pLodData = pLodData;

    int nVertexCount = vertexDict.VertexCount();

    pLodData->vertex = (s_lodvertexinfo_t *) calloc(nVertexCount, sizeof(s_lodvertexinfo_t));
    pLodData->numvertices = nVertexCount;
    pLodData->face = (s_face_t *) calloc(faces.size(), sizeof(s_face_t));
    pLodData->numfaces = std::ssize(faces);

    for (i = 0; i < nVertexCount; ++i) {
        const VertexInfo_t &srcVertex = vertexDict.Vertex(i);
        s_lodvertexinfo_t &dstVertex = pLodData->vertex[i];

        dstVertex.boneweight = srcVertex.m_BoneWeight;
        Assert(dstVertex.boneweight.numbones <= 4);
        dstVertex.position = srcVertex.m_Position;
        dstVertex.normal = srcVertex.m_Normal;
        dstVertex.tangentS = srcVertex.m_TangentS;
        dstVertex.lodFlag = srcVertex.m_nLodFlag;

        dstVertex.texcoord.resize(srcVertex.m_TexCoord.size());

        for (int j = 0; j < srcVertex.m_TexCoord.size(); ++j) {
            dstVertex.texcoord[j] = srcVertex.m_TexCoord[j];
        }
    }

    memcpy(pLodData->face, faces.data(), faces.size() * sizeof(s_face_t));
    memcpy(pLodData->mesh, meshes.data(), meshes.size() * sizeof(s_mesh_t));

    for (i = 0; i < MAX_NUM_LODS; i++) {
        pLodData->pMeshVertIndexMaps[i] = pMeshVertIndexMaps[i];
    }
}


//-----------------------------------------------------------------------------
// This fills out boneMap, which is a mapping from src bone to src bone replacement (or to itself
// if there is no bone replacement.
//-----------------------------------------------------------------------------
static void BuildBoneLODMapping(StudioMdlState &ctx, std::vector<int> &boneMap, int lodID) {
    boneMap.resize(boneMap.size() + ctx.bones.bonetable.size());

    Assert(lodID < std::ssize(ctx.body.ScriptLODs));
    LodScriptData_t &scriptLOD = ctx.body.ScriptLODs[lodID];

    // First, create a direct mapping where no bones are collapsed
    int i;
    for (i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        boneMap[i] = i;
    }

    for (i = 0; i < std::ssize(scriptLOD.boneReplacements); i++) {
        std::string_view src = scriptLOD.boneReplacements[i].GetSrcName();
        std::string_view dst = scriptLOD.boneReplacements[i].GetDstName();
        int j = findGlobalBone(ctx, src);
        int k = findGlobalBone(ctx, dst);

        if (j != -1 && k != -1) {
            boneMap[j] = k;
        } else if (j == -1) {
            // FIXME: is this really an error?  It could just be  replacement command for bone that doesnt' exist anymore.
            if (ctx.options.verbose) {
                MdlWarning("Couldn't replace unknown bone \"{}\" with \"{}\"\n", src, dst);
            }
        } else {
            // FIXME: is this really an error?  It could just be  replacement command for bone that doesnt' exist anymore.
            if (ctx.options.verbose) {
                MdlWarning("Couldn't replace bone \"{}\" with unknown \"{}\"\n", src, dst);
            }
        }
    }
}

static void MarkRootLODBones(StudioMdlState &ctx, CVertexDictionary &vertexDictionary) {
    // should result in an identity mapping
    // because their are no bone remaps at the root lod
    std::vector<int> boneMap;
    BuildBoneLODMapping(ctx, boneMap, 0);

    // iterate and mark bones
    for (int nVertDictID = vertexDictionary.RootLODVertexStart();
         nVertDictID < vertexDictionary.RootLODVertexEnd(); nVertDictID++) {
        s_boneweight_t &boneWeight = vertexDictionary.Vertex(nVertDictID).m_BoneWeight;

        RemapBoneWeights(boneMap, boneWeight);
        CollapseBoneWeights(ctx, boneWeight);
        SortBoneWeightByWeight(boneWeight);

        MarkBonesUsedByLod(ctx, boneWeight, 0);
    }
}


//-----------------------------------------------------------------------------
// Computes LOD vertices for a model piece.
//-----------------------------------------------------------------------------
static void UnifyModelLODs(StudioMdlState &ctx, s_model_t *pSrcModel) {
    if (vstd::iequals(pSrcModel->name, "blank"))
        return;

    // each lod has a unique vertex mapping table
    const int nNumLODs = std::ssize(pSrcModel->m_LodSources);
    int nLodID;
    int *pMeshVertIndexMaps[MAX_NUM_LODS];
    for (nLodID = 0; nLodID < MAX_NUM_LODS; nLodID++) {
        if (nLodID < nNumLODs && pSrcModel->m_LodSources[nLodID]) {
            const int nVertexCount = std::ssize(pSrcModel->m_LodSources[nLodID]->m_GlobalVertices);
            pMeshVertIndexMaps[nLodID] = new int[nVertexCount];
#ifdef _DEBUG
            memset(pMeshVertIndexMaps[nLodID], 0xDD, nVertexCount * sizeof(int));
#endif
        } else {
            pMeshVertIndexMaps[nLodID] = nullptr;
        }
    }

    // These hold the aggregate data for the model that grows as lods are processed
    CVertexDictionary vertexDictionary(ctx);
    std::vector<s_face_t> faces;
    std::vector<s_mesh_t> meshes(MAXSTUDIOSKINS);

    int nMeshID;
    for (nMeshID = 0; nMeshID < pSrcModel->source->meshes.size(); nMeshID++) {
        // the LOD data is indexed by material id
        s_mesh_t *pVertexDictMesh = &meshes[pSrcModel->source->meshes[nMeshID].material];
        pVertexDictMesh->material = pSrcModel->source->meshes[nMeshID].material;

        pVertexDictMesh->numvertices = 0;
        pVertexDictMesh->vertexoffset = vertexDictionary.VertexCount();
        pVertexDictMesh->numfaces = 0;
        pVertexDictMesh->faceoffset = std::ssize(faces);

        // First build up information for LOD 0
        if (!pSrcModel->m_LodSources[0])
            continue;

        s_source_t *pLOD0Source = pSrcModel->m_LodSources[0];

        // lookup the material used by this mesh
        int nMaterialID = pLOD0Source->meshes[nMeshID].material;
        const char *pName = ctx.materials.textures[nMaterialID].name;
        if (!ctx.options.quiet) {
            std::print("Processing LOD for material: {}\n", pName);
        }
        s_mesh_t *pLOD0Mesh = FindMeshByMaterial(pLOD0Source, nMaterialID);
        if (!pLOD0Mesh)
            continue;

        // populate with all vertices from LOD 0
        int nStart = vertexDictionary.VertexCount();
        CopyVerts(0, pLOD0Source, pLOD0Mesh, vertexDictionary, pVertexDictMesh, pMeshVertIndexMaps[0]);
        vertexDictionary.SetRootVertexRange(nStart, vertexDictionary.VertexCount());

        MarkRootLODBones(ctx, vertexDictionary);

        // only fix up the faces for the highest lod since the lowest ones are going
        // to be reprocessed later.
        CopyFaces(pLOD0Source, pLOD0Mesh, faces, pVertexDictMesh);

        // Now, for each LOD, try to build meshes using the vertices in LOD 0.
        // Ideally, vertices used in an LOD would be in LOD 0 for the benefit of shared vertices.
        // If we don't find vertices in LOD 0, this code will add vertices into LOD 0's list
        // of vertices for the next LOD to find
        for (nLodID = 1; nLodID < nNumLODs; ++nLodID) {
            s_source_t *pCurrLOD = pSrcModel->m_LodSources[nLodID];
            if (!pCurrLOD)
                continue;

            // Find the mesh that matches the material
            // mesh may not be present or could be culled due to $removemesh commands
            s_mesh_t *pCurrLODMesh = FindOrCullMesh(ctx, nLodID, pCurrLOD, nMaterialID);
            if (!pCurrLODMesh)
                continue;

            CreateLODVertsInDictionary(ctx, nLodID, pLOD0Source, pCurrLOD, pCurrLODMesh, pVertexDictMesh, vertexDictionary,
                                       pMeshVertIndexMaps[nLodID]);
        }
    }

#ifdef _DEBUG
    Msg("Total vertex count: {:d}\n", vertexDictionary.VertexCount());
#endif

    // save the data we just built into the processed data section
    // The processed data has all of the verts that are needed for all LODs.
    SetProcessedWithDictionary(pSrcModel, vertexDictionary, faces, meshes, pMeshVertIndexMaps);
//	PrintSourceVerts( pSrcModel->m_LodModels[0] );
}


//-----------------------------------------------------------------------------
// Force the vertex array for a model to have all of the vertices that are needed
// for all of the LODs of the model.
//-----------------------------------------------------------------------------
void UnifyLODs(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Simplify");
    // todo: need to fixup the firstref/lastref stuff . . do we really need it anymore?
    for (int modelID = 0; modelID < ctx.body.nummodelsbeforeLOD; modelID++) {
        UnifyModelLODs(ctx, ctx.body.model[modelID]);
    }
}


static void PrintSpaces(int numSpaces) {
    int i;
    for (i = 0; i < numSpaces; i++) {
        std::print(" ");
    }
}

static void SpewBoneInfo(StudioMdlState &ctx, std::array<int, MAX_NUM_LODS> &numBonesInLOD, int globalBoneID, int depth) {
    s_bonetable_t *pBone = &ctx.bones.bonetable[globalBoneID];
    if (ctx.options.printBones) {
        PrintSpaces(depth * 2);
        std::print("{} \"{}\" ", depth, pBone->name);
    }
    int i;
    for (i = 0; i < 8; i++) {
        if (HasFlag(pBone->flags, BoneUsedByVertexAtLod(i))) {
            if (ctx.options.printBones) {
                std::print("lod{} ", i);
            }
            numBonesInLOD[i]++;
        }
    }

    if (ctx.options.printBones) {
        if (HasFlag(pBone->flags, BoneFlags::UsedByHitbox))
            std::print("hitbox ");

        if (HasFlag(pBone->flags, BoneFlags::UsedByAttachment))
            std::print("attachment ");

        if (HasFlag(pBone->flags, BoneFlags::UsedByBoneMerge))
            std::print("merge ");

        std::print("\n");
    }

    int j;
    for (j = 0; j < std::ssize(ctx.bones.bonetable); j++) {
        s_bonetable_t *pBone = &ctx.bones.bonetable[j];
        if (pBone->parent == globalBoneID) {
            SpewBoneInfo(ctx, numBonesInLOD, j, depth + 1);
        }
    }
}

void SpewBoneUsageStats(StudioMdlState &ctx) {
    std::array<int, MAX_NUM_LODS> numBonesInLOD{};
    if (std::ssize(ctx.bones.bonetable) == 0) {
        return;
    }
    SpewBoneInfo(ctx, numBonesInLOD, 0, 0);
    if (ctx.options.printBones) {
        int i;
        for (i = 0; i < std::ssize(ctx.body.ScriptLODs); i++) {
            std::print("\t{} bones used in lod {}\n", numBonesInLOD[i], i);
        }
    }
}

void MarkParentBoneLODs(StudioMdlState &ctx) {
    int i;
    for (i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        BoneFlags flags = ctx.bones.bonetable[i].flags & BoneFlags::UsedByVertexMask;
        int globalBoneID = ctx.bones.bonetable[i].parent;
        while (globalBoneID != -1) {
            ctx.bones.bonetable[globalBoneID].flags |= flags;
            globalBoneID = ctx.bones.bonetable[globalBoneID].parent;
        }
    }
}


//-----------------------------------------------------------------------------
// Returns the sources associated with the various LODs based on the script commands
//-----------------------------------------------------------------------------
static void GetLODSources(StudioMdlState &ctx, std::vector<s_source_t *> &lods, const s_model_t *pSrcModel) {
    const int nNumLODs = std::ssize(ctx.body.ScriptLODs);
    if (std::ssize(lods) < nNumLODs)
        lods.resize(nNumLODs);
    for (int lodID = 0; lodID < nNumLODs; lodID++) {
        LodScriptData_t &scriptLOD = ctx.body.ScriptLODs[lodID];

        bool bFound;
        s_source_t *pSource = GetModelLODSource(pSrcModel->filename.c_str(), scriptLOD, &bFound);
        if (!pSource && !bFound) {
            pSource = pSrcModel->source;
        }

        lods[lodID] = pSource;
    }
}


//-----------------------------------------------------------------------------
// Creates models to store converted data for the various LODs
//-----------------------------------------------------------------------------
void LoadLODSources(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Simplify");
    ctx.body.nummodelsbeforeLOD = std::ssize(ctx.body.model);
    for (int modelID = 0; modelID < ctx.body.nummodelsbeforeLOD; modelID++) {
        if (vstd::iequals(ctx.body.model[modelID]->name, "blank")) {
            int nNumLODs = std::ssize(ctx.body.ScriptLODs);
            ctx.body.model[modelID]->m_LodSources.assign(nNumLODs, nullptr);
            continue;
        }

        GetLODSources(ctx, ctx.body.model[modelID]->m_LodSources, ctx.body.model[modelID]);
    }
}

static void ReplaceBonesRecursive(StudioMdlState &ctx, int globalBoneID, bool replaceThis,
                                  std::vector<CLodScriptReplacement_t> &boneReplacements,
                                  const char *replacementName) {
    if (replaceThis) {
        CLodScriptReplacement_t &boneReplacement = boneReplacements.emplace_back();
        boneReplacement.SetSrcName(ctx.bones.bonetable[globalBoneID].name);
        boneReplacement.SetDstName(replacementName);
    }

    // find children and recurse.
    int i;
    for (i = 0; i < std::ssize(ctx.bones.bonetable); i++) {
        if (ctx.bones.bonetable[i].parent == globalBoneID) {
            ReplaceBonesRecursive(ctx, i, true, boneReplacements, replacementName);
        }
    }
}

static void ConvertSingleBoneTreeCollapseToReplaceBones(StudioMdlState &ctx, CLodScriptReplacement_t &boneTreeCollapse,
                                                        std::vector<CLodScriptReplacement_t> &boneReplacements) {
    // find the bone that we are starting with.
    int i = findGlobalBone(ctx, boneTreeCollapse.GetSrcName());
    if (i != -1) {
        ReplaceBonesRecursive(ctx, i, false, boneReplacements, ctx.bones.bonetable[i].name);
        return;
    }
    MdlWarning("Couldn't find bone {} for bonetreecollapse, skipping\n", boneTreeCollapse.GetSrcName());
}

void ConvertBoneTreeCollapsesToReplaceBones(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Simplify");
    int i;
    for (i = 0; i < std::ssize(ctx.body.ScriptLODs); i++) {
        LodScriptData_t &lod = ctx.body.ScriptLODs[i];
        int j;
        for (j = 0; j < std::ssize(lod.boneTreeCollapses); j++) {
            ConvertSingleBoneTreeCollapseToReplaceBones(ctx, lod.boneTreeCollapses[j],
                                                        lod.boneReplacements);
        }
    }
}

/*
static void PrintReplacedBones( LodScriptData_t &lod )
{
	int i;
	for( i = 0; i < std::ssize(lod.boneReplacements); i++ )
	{
		printf( "%s -> %s\n", 
			lod.boneReplacements[i].GetSrcName(), 
			lod.boneReplacements[i].GetDstName() );
	}
}
*/

void FixupReplacedBonesForLOD(LodScriptData_t &lod) {
/*
	printf( "before:\n" );
	PrintReplacedBones( lod );
*/
    bool changed;
    int i;
    int j;
    do {
        changed = false;
        for (i = 0; i < std::ssize(lod.boneReplacements); i++) {
            for (j = 0; j < std::ssize(lod.boneReplacements); j++) {
                if (i == j) {
                    continue;
                }
                if (vstd::iequals(lod.boneReplacements[i].GetSrcName(), lod.boneReplacements[j].GetDstName())) {
                    lod.boneReplacements[j].SetDstName(lod.boneReplacements[i].GetDstName());
                    changed = true;
                }
            }
        }
    } while (changed);
/*
	printf( "after:\n" );
	PrintReplacedBones( lod );
*/
}

void FixupReplacedBones(StudioMdlState &ctx) {
    vstd::FunctionTimingRecorder _("Simplify");
    int i;
    for (i = 0; i < std::ssize(ctx.body.ScriptLODs); i++) {
        FixupReplacedBonesForLOD(ctx.body.ScriptLODs[i]);
    }
}

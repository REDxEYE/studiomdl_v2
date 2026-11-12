//========= Copyright � 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once
#include "tier1_v2/ptr_utils.hpp"

#ifdef _WIN32
#pragma once
#endif

#include "studio.h"

#define MAX_NUM_BONES_PER_STRIP 512

#define OPTIMIZED_MODEL_FILE_VERSION 7

extern bool g_bDumpGLViewFiles;

struct s_bodypart_t;

namespace OptimizedModel {
#pragma pack(1)

    struct BoneStateChangeHeader_t {
        int hardwareID;
        int newBoneID;
    };

    struct Vertex_t {
        // these index into the mesh's vert[origMeshVertID]'s bones
        unsigned char boneWeightIndex[MAX_NUM_BONES_PER_VERT];
        unsigned char numBones;

        unsigned short origMeshVertID;

        // for sw skinned verts, these are indices into the global list of bones
        // for hw skinned verts, these are hardware bone indices
        uint8_t boneID[MAX_NUM_BONES_PER_VERT];
    };

    // We don't do actual strips anymore, only triangle lists and subd quad lists
    enum StripHeaderFlags_t {
        STRIP_IS_TRILIST = 0x01,
        STRIP_IS_QUADLIST_REG = 0x02, // Regular sub-d quads
        STRIP_IS_QUADLIST_EXTRA = 0x04 // Extraordinary sub-d quads
    };

    // A strip is a piece of a stripgroup that is divided by bones
    struct StripHeader_t {
        int numIndices; // indexOffset offsets into the mesh's index array
        int indexOffset;

        int numVerts; // vertexOffset offsets into the mesh's vert array
        int vertOffset;

        // Use this to enable/disable skinning.
        // May decide (in optimize.cpp) to put all with 1 bone in a different strip
        // than those that need skinning.
        short numBones;

        unsigned char flags;

        int numBoneStateChanges;
        int boneStateChangeOffset;

        BoneStateChangeHeader_t* pBoneStateChange(const int i) const {
            return (BoneStateChangeHeader_t*) (((uint8_t*) this) + boneStateChangeOffset) + i;
        };

        // These go last on purpose!
        int numTopologyIndices;
        int topologyOffset;
    };

    enum StripGroupFlags_t {
        STRIPGROUP_IS_HWSKINNED = 0x02,
        STRIPGROUP_IS_DELTA_FLEXED = 0x04,
        STRIPGROUP_SUPPRESS_HW_MORPH = 0x08, // NOTE: This is a temporary flag used at run time.
    };

    // a locking group
    // a single vertex buffer
    // a single index buffer
    struct StripGroupHeader_t {
        // These are the arrays of all verts and indices for this mesh.  strips index into this.
        int numVerts;
        int vertOffset;

        Vertex_t* pVertex(const int i) const {
            return (Vertex_t*) (((uint8_t*) this) + vertOffset) + i;
        };

        int numIndices;
        int indexOffset;

        unsigned short* pIndex(const int i) const {
            return (unsigned short*) (((uint8_t*) this) + indexOffset) + i;
        };

        int numStrips;
        int stripOffset;

        StripHeader_t* pStrip(const int i) const {
            return (StripHeader_t*) (((uint8_t*) this) + stripOffset) + i;
        };

        unsigned char flags;

        int numTopologyIndices;
        int topologyOffset;

        unsigned short* pTopologyIndex(const int i) const {
            return (unsigned short*) (((uint8_t*) this) + topologyOffset) + i;
        };
    };

    enum MeshFlags_t {
        // these are both material properties, and a mesh has a single material.
        MESH_IS_TEETH = 0x01,
        MESH_IS_EYES = 0x02
    };

    // a collection of locking groups:
    // up to 4:
    // non-flexed, hardware skinned
    // flexed, hardware skinned
    // non-flexed, software skinned
    // flexed, software skinned
    //
    // A mesh has a material associated with it.
    struct MeshHeader_t {
        int numStripGroups;
        int stripGroupHeaderOffset;

        StripGroupHeader_t* pStripGroup(const int i) const {
            StripGroupHeader_t* pDebug = (StripGroupHeader_t*) (((uint8_t*) this) + stripGroupHeaderOffset) + i;
            return pDebug;
        };
        unsigned char flags;
    };

    struct ModelLODHeader_t {
        int numMeshes;
        int meshOffset;
        float switchPoint;

        const MeshHeader_t* pMesh(const int i) const {
            return vstd::index_array_at<MeshHeader_t>(this, meshOffset, i);
        }

        MeshHeader_t* pMesh(const int i) {
            return vstd::index_array_at<MeshHeader_t>(this, meshOffset, i);
        }
    };

    // This maps one to one with models in the mdl file.
    // There are a bunch of model LODs stored inside potentially due to the qc $lod command
    struct ModelHeader_t {
        int numLODs; // garymcthack - this is also specified in FileHeader_t
        int lodOffset;

        const ModelLODHeader_t* pLOD(const int i) const {
            return vstd::index_array_at<ModelLODHeader_t>(this, lodOffset, i);
        }

        ModelLODHeader_t* pLOD(const int i) {
            return vstd::index_array_at<ModelLODHeader_t>(this, lodOffset, i);
        }
    };

    struct BodyPartHeader_t {
        int numModels;
        int modelOffset;

        const ModelHeader_t* pModel(const int i) const {
            return vstd::index_array_at<ModelHeader_t>(this, modelOffset, i);
        };

        ModelHeader_t* pModel(const int i) {
            return vstd::index_array_at<ModelHeader_t>(this, modelOffset, i);
        };
    };

    struct MaterialReplacementHeader_t {
        short materialID;
        int replacementMaterialNameOffset;

        const char* pMaterialReplacementName() {
            return vstd::rel_ptr<const char>(this, replacementMaterialNameOffset);
        }
    };

    struct MaterialReplacementListHeader_t {
        int numReplacements;
        int replacementOffset;

        const MaterialReplacementHeader_t* pMaterialReplacement(const int i) const {
            return vstd::index_array_at<MaterialReplacementHeader_t>(this, replacementOffset, i);
        }
        MaterialReplacementHeader_t* pMaterialReplacement(const int i) {
            return vstd::index_array_at<MaterialReplacementHeader_t>(this, replacementOffset, i);
        }
    };

    struct FileHeader_t {
        // file version as defined by OPTIMIZED_MODEL_FILE_VERSION
        int version;

        // hardware params that affect how the model is to be optimized.
        int vertCacheSize;
        unsigned short maxBonesPerStrip;
        unsigned short maxBonesPerFace;
        int maxBonesPerVert;

        // must match checkSum in the .mdl
        int checkSum;

        int numLODs; // garymcthack - this is also specified in ModelHeader_t and should match

        // one of these for each LOD
        int materialReplacementListOffset;

        const MaterialReplacementListHeader_t* pMaterialReplacementList(const uint32_t lodID) const {
            return vstd::index_array_at<MaterialReplacementListHeader_t>(
                this, materialReplacementListOffset, lodID);
        }
        MaterialReplacementListHeader_t* pMaterialReplacementList(const uint32_t lodID) {
            return vstd::index_array_at<MaterialReplacementListHeader_t>(
                this, materialReplacementListOffset, lodID);
        }

        int numBodyParts;
        int bodyPartOffset;

        const BodyPartHeader_t* pBodyPart(const uint32_t i) const {
            return vstd::index_array_at<BodyPartHeader_t>(this, bodyPartOffset, i);
        }
        BodyPartHeader_t* pBodyPart(const uint32_t i) {
            return vstd::index_array_at<BodyPartHeader_t>(this, bodyPartOffset, i);
        };
    };

#pragma pack()

    void WriteOptimizedFiles(studiohdr_t* phdr, s_bodypart_t* pSrcBodyParts);
}; // namespace OptimizedModel

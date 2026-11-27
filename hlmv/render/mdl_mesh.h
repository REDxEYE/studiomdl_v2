#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <QString>
#include <QStringList>
#include <QMatrix4x4>
#include <QVector3D>

#include "mdl_flex.h"

struct Vertex
{
    float position[4];
    float uv[2];
    float normal[3];
    float tangent[4];
};

struct MeshBatch
{
    size_t first;
    size_t count;
    int texture;  // texture in skin family 0
    int slot;     // skin reference (mstudiomesh_t::material)
    int bodyPart, model, lod;
    int eyeball = -1; // index into MdlMesh::eyeballs for mstudiomesh_t::materialtype 1 (eyeball) meshes
};

// mstudioboneweight_t without zero-weight slots, weights renormalized.
struct VertexWeights
{
    uint8_t count;
    uint8_t bone[3];
    float weight[3];
};

struct MdlAttachment
{
    int bone;
    QMatrix4x4 local; // attachment to bone
};

// mstudioeyeball_t fields studiorender's R_StudioEyeballPosition reads.
struct MdlEyeball
{
    int bone;
    QVector3D org, up, forward;
    float zoffset, radius, irisScale;
};

// mstudiomouth_t: the bone and direction studiorender lights Teeth materials with, and the flex driving them.
struct MdlMouth
{
    int bone;
    QVector3D forward;
    int flexdesc;
};

// Header data shown by HLMV's Model, Sequence, Bones and Attachments tabs.
struct MdlInfo
{
    QStringList bones, attachments, bodyParts, sequences;
    QStringList surfaceProps; // per bone
    bool autoHitboxes = false;
    std::vector<QStringList> bodyModels; // submodel names per body part
    int lodCount = 1;
    int boneControllers = 0, hitboxes = 0, hitboxSets = 0, skinFamilies = 0;
    QVector3D hullMin, hullMax;
};

struct MdlHitbox
{
    int bone, group;
    QVector3D min, max;
    QVector3D angles; // pitch, yaw, roll degrees (angOffsetOrientation)
    float radius;     // capsule radius; 0 for boxes
    QString name;
};

struct MdlHitboxSet
{
    QString name;
    std::vector<MdlHitbox> boxes;
};

struct MdlMesh
{
    std::vector<Vertex> vertices; // bind pose for skinned models (skin with SkinVertices), model space for static props
    std::vector<uint32_t> indices;
    std::vector<MeshBatch> batches;
    QStringList textures;
    QStringList materialPaths;
    QVector3D center;
    bool hasTangents;
    float radius;
    MdlInfo info;
    MdlFlexData flex;
    std::vector<int16_t> skinTable;               // families x skinRefs texture indices
    int skinRefs = 0;
    std::vector<VertexWeights> weights;           // per vertex; empty for static props
    std::vector<QMatrix4x4> poseToBone;           // inverse bind pose per bone (mstudiobone_t::poseToBone)
    std::vector<QMatrix4x4> bones;                // rest pose bone-to-model, or inverse bind for static props
    std::vector<int> boneParents;
    std::vector<MdlAttachment> attachments;
    std::vector<MdlHitboxSet> hitboxSets;
    std::vector<MdlEyeball> eyeballs;
    std::vector<MdlMouth> mouths;
    float maxEyeDeflection = 0.866f; // studiohdr2_t::MaxEyeDeflection (cos 30 when unset)

    int Texture(const MeshBatch &batch, const int32_t family) const
    {
        const size_t count = skinRefs ? skinTable.size() / skinRefs : 0;
        return family > 0 && family < count ? skinTable[family * skinRefs + batch.slot] : batch.texture;
    }
};

MdlMesh LoadMdlMesh(const QString &path);

// Linear blend skinning: out = sum(weight * boneToModel[bone] * poseToBone[bone]) applied to base.
void SkinVertices(const MdlMesh &mesh, std::span<const Vertex> base, std::span<const QMatrix4x4> boneToModel,
                  std::vector<Vertex> &out);

// Bounds of the indexed vertices -> mesh.center/radius; false (mesh unchanged) when degenerate or non-finite.
bool FrameVertices(MdlMesh &mesh, std::span<const Vertex> vertices);

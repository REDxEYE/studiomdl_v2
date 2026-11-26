#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>
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
};

// Header data shown by HLMV's Model, Sequence, Bones and Attachments tabs.
struct MdlInfo
{
    QStringList bones, attachments, bodyParts, sequences;
    std::vector<QStringList> bodyModels; // submodel names per body part
    int lodCount = 1;
    int boneControllers = 0, hitboxes = 0, hitboxSets = 0, skinFamilies = 0;
    QVector3D hullMin, hullMax;
};

struct MdlMesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshBatch> batches;
    QStringList textures;
    QStringList materialPaths;
    QVector3D center;
    bool hasTangents;
    float radius;
    MdlInfo info;
    MdlFlexData flex;
    std::vector<std::array<float, 9>> skinLinear; // per vertex, row-major; empty for static props
    std::vector<int16_t> skinTable;               // families x skinRefs texture indices
    int skinRefs = 0;

    int Texture(const MeshBatch &batch, int family) const
    {
        const int count = skinRefs ? int(skinTable.size()) / skinRefs : 0;
        return family > 0 && family < count ? skinTable[size_t(family) * skinRefs + batch.slot] : batch.texture;
    }
};

MdlMesh LoadMdlMesh(const QString &path);

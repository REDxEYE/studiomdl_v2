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
    int texture;
};

// Header data shown by HLMV's Model, Sequence, Bones and Attachments tabs.
struct MdlInfo
{
    QStringList bones, attachments, bodyParts, sequences;
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
};

MdlMesh LoadMdlMesh(const QString &path);

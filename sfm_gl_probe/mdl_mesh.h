#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>
#include <QVector3D>

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
};

MdlMesh LoadMdlMesh(const QString &path);

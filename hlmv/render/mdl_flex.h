#pragma once

// CPU flex (vertex animation), following Source's software path: CStudioHdr::RunFlexRulesOld (flex
// controllers -> flex descriptor weights) and studiorender's R_StudioFlexVerts (weights -> vertex deltas).

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <QStringList>

#include "studio.h"

struct Vertex;

struct FlexController
{
    QString name;
    QString type;
    float min = 0, max = 1;
};

struct FlexOp
{
    StudioOpcode op;     // StudioOpcode
    int32_t index;   // for index operands
    float value;     // for StudioOpcode::Const
};

struct FlexRule
{
    int flex; // flex descriptor written by the rule
    std::vector<FlexOp> ops;
};

struct FlexVertex
{
    uint32_t vertex;     // index into MdlMesh::vertices
    float position[3];   // bind-space deltas
    float normal[3];
    float speed, side;   // 0..1
};

struct MeshFlex
{
    int desc, pair;      // flex descriptor and stereo partner (0 = none)
    std::array<float, 4> target;     // RampFlexWeight targets
    std::vector<FlexVertex> vertices;
};

struct MdlFlexData
{
    QStringList descs;
    std::vector<FlexController> controllers;
    std::vector<FlexRule> rules;
    std::vector<MeshFlex> flexes;
};

// Controller values in controller units (min..max) -> one weight per flex descriptor.
std::vector<float> RunFlexRules(const MdlFlexData &flex, std::span<const float> controllers);

// base + weighted deltas -> out (same size as base), in bind space; skinning follows, as in studiorender.
void ApplyFlexes(const MdlFlexData &flex, std::span<const float> descWeights, std::span<const Vertex> base,
                 std::vector<Vertex> &out);

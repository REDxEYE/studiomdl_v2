#include "mdl_flex.h"

#include <algorithm>
#include <cmath>

#include <QVector3D>

#include "mdl_mesh.h"
#include "studio.h"

namespace
{
// studiorender CStudioRender::RampFlexWeight
float RampFlexWeight(const MeshFlex &flex, float w)
{
    const auto &t = flex.target;
    if (w <= t[0] || w >= t[3])
        return 0;
    if (w < t[1])
        return (w - t[0]) / (t[1] - t[0]);
    if (w > t[2])
        return (t[3] - w) / (t[3] - t[2]);
    return 1;
}
} // namespace

std::vector<float> RunFlexRules(const MdlFlexData &flex, std::span<const float> src)
{
    // Rule shapes and operand indices are validated at load; runtime indices taken from the stack are checked here.
    std::vector<float> dest(flex.descs.size(), 0.f);
    const auto controller = [&](float stackValue) -> const FlexController * {
        const int i = int(stackValue);
        return i >= 0 && size_t(i) < flex.controllers.size() ? &flex.controllers[i] : nullptr;
    };
    const auto normalized = [&](const FlexController *c) {
        return c ? RemapValClamped(src[c - flex.controllers.data()], c->min, c->max, 0, 1) : 0.f;
    };
    for (const auto &rule : flex.rules)
    {
        float stack[32] = {};
        int k = 0;
        for (const auto &op : rule.ops)
        {
            switch (StudioOpcode(op.op))
            {
            case StudioOpcode::Add: stack[k - 2] += stack[k - 1]; k--; break;
            case StudioOpcode::Sub: stack[k - 2] -= stack[k - 1]; k--; break;
            case StudioOpcode::Mul: stack[k - 2] *= stack[k - 1]; k--; break;
            case StudioOpcode::Div:
                stack[k - 2] = stack[k - 1] > 0.0001f ? stack[k - 2] / stack[k - 1] : 0;
                k--;
                break;
            case StudioOpcode::Neg: stack[k - 1] = -stack[k - 1]; break;
            case StudioOpcode::Max: stack[k - 2] = std::max(stack[k - 2], stack[k - 1]); k--; break;
            case StudioOpcode::Min: stack[k - 2] = std::min(stack[k - 2], stack[k - 1]); k--; break;
            case StudioOpcode::Const: stack[k++] = op.value; break;
            case StudioOpcode::Fetch1: stack[k++] = src[op.index]; break;
            case StudioOpcode::Fetch2: stack[k++] = dest[op.index]; break;
            case StudioOpcode::Combo:
            {
                const int km = k - op.index;
                for (int i = km + 1; i < k; ++i)
                    stack[km] *= stack[i];
                k = km + 1;
                break;
            }
            case StudioOpcode::Dominate:
            {
                const int km = k - op.index;
                float dv = stack[km];
                for (int i = km + 1; i < k; ++i)
                    dv *= stack[i];
                stack[km - 1] *= 1.0f - dv;
                k -= op.index;
                break;
            }
            case StudioOpcode::TwoWay0: stack[k++] = RemapValClamped(src[op.index], -1, 0, 1, 0); break;
            case StudioOpcode::TwoWay1: stack[k++] = RemapValClamped(src[op.index], 0, 1, 0, 1); break;
            case StudioOpcode::NWay:
            {
                const auto *valueController = controller(stack[k - 1]);
                float value = valueController ? src[valueController - flex.controllers.data()] : 0;
                const float x = stack[k - 5], y = stack[k - 4], z = stack[k - 3], w = stack[k - 2];
                if (value <= x || value >= w)
                    value = 0;
                else if (value < y)
                    value = RemapValClamped(value, x, y, 0, 1);
                else if (value > z)
                    value = RemapValClamped(value, z, w, 1, 0);
                else
                    value = 1;
                stack[k - 5] = value * src[op.index];
                k -= 4;
                break;
            }
            case StudioOpcode::DmeLowerEyelid:
            case StudioOpcode::DmeUpperEyelid:
            {
                const float closeLidV = normalized(&flex.controllers[op.index]);
                const float closeLid = normalized(controller(stack[k - 1]));
                const auto *eyeUpDownController = controller(stack[k - 3]);
                const float eyeUpDown = eyeUpDownController
                    ? RemapValClamped(src[eyeUpDownController - flex.controllers.data()], eyeUpDownController->min,
                                      eyeUpDownController->max, -1, 1)
                    : 0.f;
                if (StudioOpcode(op.op) == StudioOpcode::DmeLowerEyelid)
                    stack[k - 3] = eyeUpDown > 0 ? (1 - eyeUpDown) * (1 - closeLidV) * closeLid : (1 - closeLidV) * closeLid;
                else
                    stack[k - 3] = eyeUpDown < 0 ? (1 + eyeUpDown) * closeLidV * closeLid : closeLidV * closeLid;
                k -= 2;
                break;
            }
            default: // Exp, Open, Close, Comma: not executed by Source either
                break;
            }
        }
        dest[rule.flex] = stack[0];
    }
    return dest;
}

void ApplyFlexes(const MdlFlexData &flex, std::span<const float> weights, std::span<const Vertex> base,
                 std::vector<Vertex> &out)
{
    out.assign(base.begin(), base.end());
    std::vector<bool> touched(base.size());
    const auto delta = [](const float *d) { return QVector3D(d[0], d[1], d[2]); };
    for (const auto &mesh : flex.flexes)
    {
        // A static viewer has no lagged weights, so studiorender's w2/w4 equal w1/w3.
        const float w1 = RampFlexWeight(mesh, weights[mesh.desc]);
        const float w3 = mesh.pair ? RampFlexWeight(mesh, weights[mesh.pair]) : w1;
        if (std::abs(w1) < 0.001f && std::abs(w3) < 0.001f)
            continue;
        for (const auto &v : mesh.vertices)
        {
            const float w = w1 * (1 - v.side) + v.side * w3;
            auto &target = out[v.vertex];
            const auto dp = delta(v.position) * w;
            const auto dn = delta(v.normal) * w;
            for (int i = 0; i < 3; ++i)
            {
                target.position[i] += dp[i];
                target.normal[i] += dn[i];
                target.tangent[i] += dn[i]; // studiorender adds the normal delta to tangentS too
            }
            touched[v.vertex] = true;
        }
    }
    for (size_t i = 0; i < out.size(); ++i)
        if (touched[i])
        {
            auto &v = out[i];
            const auto n = QVector3D(v.normal[0], v.normal[1], v.normal[2]).normalized();
            const auto t = QVector3D(v.tangent[0], v.tangent[1], v.tangent[2]).normalized();
            for (int j = 0; j < 3; ++j)
            {
                v.normal[j] = n[j];
                v.tangent[j] = t[j];
            }
        }
}

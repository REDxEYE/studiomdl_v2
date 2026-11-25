#include "studiomdl/weight_fit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <print>
#include <random>
#include <string>

#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "tier1_v2/function_timing.hpp"

static_assert(MAXSTUDIOBONEWEIGHTS == 3, "the solver below fits exactly three weights");

namespace {
    constexpr int kKept = 3;
    constexpr float kMaxAngle = 0.75f; // radians (43 degrees): uniform in +-kMaxAngle is a 25 degree deviation

    using Samples = std::span<const double>;

    // sum of (a - b) * (c - d)
    double DotOfDifferences(const Samples a, const Samples b, const Samples c, const Samples d) {
        double sum = 0;
        for (size_t i = 0; i < a.size(); i++)
            sum += (a[i] - b[i]) * (c[i] - d[i]);
        return sum;
    }

    // Weights w >= 0, w0 + w1 + w2 = 1, minimising |w0 a0 + w1 a1 + w2 a2 - t|
    void SolveSimplex(const std::array<Samples, kKept> &a, const Samples t, double w[kKept]) {
        vstd::FunctionTimingRecorder _("WeightFit");
        // w2 = 1 - w0 - w1 leaves two unknowns
        const double g00 = DotOfDifferences(a[0], a[2], a[0], a[2]), g01 = DotOfDifferences(a[0], a[2], a[1], a[2]),
                     g11 = DotOfDifferences(a[1], a[2], a[1], a[2]);
        const double det = g00 * g11 - g01 * g01;
        if (det > 1e-12 * g00 * g11) {
            const double b0 = DotOfDifferences(a[0], a[2], t, a[2]), b1 = DotOfDifferences(a[1], a[2], t, a[2]);
            w[0] = (b0 * g11 - b1 * g01) / det;
            w[1] = (b1 * g00 - b0 * g01) / det;
            w[2] = 1.0 - w[0] - w[1];
            if (w[0] >= 0 && w[1] >= 0 && w[2] >= 0)
                return;
        }

        // The optimum is outside the triangle (or the bones move alike): it lies on one of the edges
        static constexpr int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
        double bestError = -1;
        for (const auto &pair: pairs) {
            const Samples p = a[pair[0]], q = a[pair[1]];
            const double length = DotOfDifferences(p, q, p, q);
            const double x = length < 1e-20 ? 0.5 : std::clamp(DotOfDifferences(t, q, p, q) / length, 0.0, 1.0);
            double error = 0;
            for (size_t i = 0; i < t.size(); i++) {
                const double e = x * p[i] + (1.0 - x) * q[i] - t[i];
                error += e * e;
            }
            if (bestError < 0 || error < bestError) {
                bestError = error;
                w[0] = w[1] = w[2] = 0;
                w[pair[0]] = x;
                w[pair[1]] = 1.0 - x;
            }
        }
    }
}

CWeightFitter::CWeightFitter(const s_source_t &source, const bool fit, const int fitPoseCount, const int testPoseCount)
    : m_fitPoses(fitPoseCount), m_testPoses(testPoseCount), m_numBones(source.numbones),
      m_skin(static_cast<size_t>(fitPoseCount + testPoseCount) * source.numbones),
      m_kept(static_cast<size_t>(kKept) * fitPoseCount * 3), m_keptEval(static_cast<size_t>(kKept) * testPoseCount * 3),
      m_full(static_cast<size_t>(fitPoseCount) * 3), m_fullEval(static_cast<size_t>(testPoseCount) * 3), m_fit(fit) {
    vstd::FunctionTimingRecorder _("WeightFit");
    // fixed seeds: a source always gets the same poses, and raising a count only appends poses
    std::mt19937 rng(0x5eed);
    const auto signedUnit = [&rng] { return static_cast<float>(rng() * (2.0 / 4294967296.0) - 1.0); }; // [-1, 1)

    std::vector<matrix3x4_t> bindLocal(m_numBones), bindInverse(m_numBones), posed(m_numBones);
    for (int i = 0; i < m_numBones; i++) {
        const int parent = source.localBone[i].parent;
        if (parent < 0) {
            MatrixCopy(source.boneToPose[i], bindLocal[i]);
        }
        else {
            matrix3x4_t parentInverse;
            MatrixInvert(source.boneToPose[parent], parentInverse);
            ConcatTransforms(parentInverse, source.boneToPose[i], bindLocal[i]);
        }
        MatrixInvert(source.boneToPose[i], bindInverse[i]);
    }

    for (int pose = 0; pose < m_fitPoses + m_testPoses; pose++) {
        if (pose == m_fitPoses)
            rng.seed(0x7e57); // the test poses don't depend on how many fitting poses there are
        for (int i = 0; i < m_numBones; i++) {
            Vector axis;
            float lengthSqr;
            do {
                axis.Init(signedUnit(), signedUnit(), signedUnit());
                lengthSqr = axis.LengthSqr();
            } while (lengthSqr < 1e-4f || lengthSqr > 1.0f);
            const float halfAngle = 0.5f * kMaxAngle * signedUnit();
            const float scale = std::sin(halfAngle) / std::sqrt(lengthSqr);
            Quaternion q;
            q.Init(axis.x * scale, axis.y * scale, axis.z * scale, std::cos(halfAngle));

            matrix3x4_t rotation, local;
            QuaternionMatrix(q, rotation);
            ConcatTransforms(bindLocal[i], rotation, local);

            const int parent = source.localBone[i].parent; // parents come first (Build_Reference relies on it too)
            if (parent < 0)
                MatrixCopy(local, posed[i]);
            else
                ConcatTransforms(posed[parent], local, posed[i]);
            ConcatTransforms(posed[i], bindInverse[i], m_skin[static_cast<size_t>(pose) * m_numBones + i]);
        }
    }
}

void CWeightFitter::Reduce(const Vector &position, const std::span<const int> bones, const std::span<float> weights,
                           const int count) {
    // vstd::FunctionTimingRecorder _("WeightFit");
    // kept bone k alone puts the vertex at Kept(k) in every fitting pose (KeptEval: test poses);
    // all the influences together put it at m_full / m_fullEval
    const size_t fitSize = static_cast<size_t>(m_fitPoses) * 3, testSize = static_cast<size_t>(m_testPoses) * 3;
    const auto Kept = [this, fitSize](const int k) { return std::span(m_kept).subspan(k * fitSize, fitSize); };
    const auto KeptEval = [this, testSize](const int k) { return std::span(m_keptEval).subspan(k * testSize, testSize); };
    std::ranges::fill(m_full, 0.0);
    std::ranges::fill(m_fullEval, 0.0);
    for (int pose = 0; pose < m_fitPoses + m_testPoses; pose++) {
        const bool evaluation = pose >= m_fitPoses;
        const int base = (evaluation ? pose - m_fitPoses : pose) * 3;
        for (int j = 0; j < count; j++) {
            Vector p;
            VectorTransform(position, Skin(pose, bones[j]), p);
            for (int c = 0; c < 3; c++) {
                if (j < kKept)
                    (evaluation ? KeptEval(j) : Kept(j))[base + c] = p[c];
                (evaluation ? m_fullEval : m_full)[base + c] += static_cast<double>(weights[j]) * p[c];
            }
        }
    }

    const double cutScale = 1.0 / (static_cast<double>(weights[0]) + weights[1] + weights[2]);
    const double cut[kKept] = {weights[0] * cutScale, weights[1] * cutScale, weights[2] * cutScale};

    double fitted[kKept] = {};
    if (m_fit) {
        const std::span<const double> a0 = Kept(0), a1 = Kept(1), a2 = Kept(2), full = m_full;
        SolveSimplex({a0, a1, a2}, full, fitted);

        // The fit minimises the summed error, which can cost the worst pose. It is only used when it is no worse
        // there than the plain cut (judged on the poses it was fitted on).
        const auto worstPose = [&](const double w[kKept]) {
            double worst = 0;
            for (int pose = 0; pose < m_fitPoses; pose++) {
                double error = 0;
                for (int i = pose * 3; i < pose * 3 + 3; i++) {
                    const double e = w[0] * a0[i] + w[1] * a1[i] + w[2] * a2[i] - full[i];
                    error += e * e;
                }
                worst = std::max(worst, error);
            }
            return worst;
        };
        if (worstPose(fitted) <= worstPose(cut)) {
            m_fitUsed++;
        }
        else {
            std::copy_n(cut, kKept, fitted);
        }
    }

    // statistics on the poses the fit has not seen: plain cut (rescaled largest weights) against what is written
    for (int pose = 0; pose < m_testPoses; pose++) {
        double cutError = 0, fitError = 0;
        for (int c = 0; c < 3; c++) {
            const int i = pose * 3 + c;
            double cutPos = 0, fit = 0;
            for (int k = 0; k < kKept; k++) {
                cutPos += cut[k] * KeptEval(k)[i];
                fit += fitted[k] * KeptEval(k)[i];
            }
            cutError += (cutPos - m_fullEval[i]) * (cutPos - m_fullEval[i]);
            fitError += (fit - m_fullEval[i]) * (fit - m_fullEval[i]);
        }
        cutError = std::sqrt(cutError);
        fitError = std::sqrt(fitError);
        m_cutErrorSum += cutError;
        m_fitErrorSum += fitError;
        m_cutErrorMax = std::max(m_cutErrorMax, cutError);
        m_fitErrorMax = std::max(m_fitErrorMax, fitError);
    }
    m_reduced++;

    if (m_fit) {
        for (int k = 0; k < kKept; k++)
            weights[k] = static_cast<float>(fitted[k]);
    }
}

s_boneweight_t CWeightFitter::FitVertex(const Vector &position, s_sourceboneweight_t &full) {
    Reduce(position, full.bone, full.weight, full.numbones);
    s_boneweight_t fitted{};
    fitted.numbones = SortAndBalanceBones(kKept, kKept, full.bone, full.weight);
    std::copy_n(full.bone.begin(), fitted.numbones, fitted.bone.begin());
    std::copy_n(full.weight.begin(), fitted.numbones, fitted.weight.begin());
    return fitted;
}

bool CWeightFitter::FitIsBetter() const {
    return m_fit && m_fitErrorMax <= m_cutErrorMax && m_fitErrorSum < m_cutErrorSum;
}

CWeightFitter::Stats CWeightFitter::GetStats() const {
    Stats stats;
    stats.reduced = m_reduced;
    stats.fitUsed = m_fitUsed;
    if (m_reduced > 0) {
        const double samples = static_cast<double>(m_reduced) * m_testPoses;
        stats.cutMean = m_cutErrorSum / samples;
        stats.fitMean = m_fitErrorSum / samples;
    }
    stats.cutMax = m_cutErrorMax;
    stats.fitMax = m_fitErrorMax;
    return stats;
}

namespace {
    CWeightFitter MakeFitter(const StudioMdlState &ctx, const s_source_t &source) {
        return CWeightFitter(source, ctx.options.fitWeights, ctx.options.fitWeightsFitPoses,
                             ctx.options.fitWeightsTestPoses);
    }

    bool RecordFit(StudioMdlState &ctx, const s_source_t &source, const CWeightFitter &fitter) {
        const bool written = fitter.FitIsBetter();
        ctx.weightFitRows.push_back({source.filename, fitter.GetStats(), written});
        return written;
    }

    // One row per source that had vertices with more than MAXSTUDIOBONEWEIGHTS influences. Distances (source units)
    // between a vertex skinned with its full weights and with the reduced ones, over the test poses.
    void PrintSourceWeightRows(const StudioMdlState &ctx, const std::vector<SourceWeightRow> &rows) {
        if (ctx.options.quiet || rows.empty())
            return;
        const bool fit = ctx.options.fitWeights;
        if (fit) {
            MdlPrint("Weight fitting (-fitweights): {} influences per vertex, {} fitting poses, {} test poses\n", kKept,
                     ctx.options.fitWeightsFitPoses, ctx.options.fitWeightsTestPoses);
            MdlPrint("    {:<32} {:>7} {:>9} {:>9} {:>9} {:>9} {:>7} {}\n", "source", "verts", "trim mean", "trim max",
                     "fit mean", "fit max", "differ", "written");
        }
        else {
            MdlPrint("Weight trimming: {} influences per vertex, {} test poses (-fitweights fits them)\n", kKept,
                     ctx.options.fitWeightsTestPoses);
            MdlPrint("    {:<32} {:>7} {:>9} {:>9}\n", "source", "verts", "trim mean", "trim max");
        }
        for (const SourceWeightRow &row: rows) {
            const CWeightFitter::Stats &s = row.stats;
            if (fit) {
                MdlPrint("    {:<32.32} {:7} {:9.4f} {:9.4f} {:9.4f} {:9.4f} {:7} {}\n", row.source, s.reduced,
                         s.cutMean, s.cutMax, s.fitMean, s.fitMax, s.fitUsed,
                         row.fitWritten ? "fit" : "trim");
            }
            else {
                MdlPrint("    {:<32.32} {:7} {:9.4f} {:9.4f}\n", row.source, s.reduced, s.cutMean, s.cutMax);
            }
        }
    }
}

void FitInputWeights(StudioMdlState &ctx, const s_source_t &source) {
    if (!ctx.options.fitWeights || std::ranges::none_of(ctx.input.sourceBone, [](const auto &full) {
        return full.numbones > kKept;
    }))
        return;

    // Count a position once, regardless of how many corners use it.
    const size_t count = ctx.input.vertex.size();
    std::vector<bool> used(count);
    const auto mark = [&](uint32_t index) {
        if (index >= count)
            MdlError("{}: face vertex index {} out of range\n", source.filename, index);
        used[index] = true;
    };
    for (const s_tmpface_t &face : ctx.input.faces) {
        mark(face.a);
        mark(face.b);
        mark(face.c);
        if (face.d != 0xFFFFFFFF)
            mark(face.d);
    }
    std::optional<CWeightFitter> fitter;
    std::vector<std::pair<size_t, s_boneweight_t>> fitted;
    for (size_t i = 0; i < std::min(count, ctx.input.sourceBone.size()); ++i) {
        auto &full = ctx.input.sourceBone[i];
        if (!used[i] || full.numbones <= kKept)
            continue;
        if (!fitter)
            fitter.emplace(MakeFitter(ctx, source));
        fitted.emplace_back(i, fitter->FitVertex(ctx.input.vertex[i], full));
    }
    ctx.input.sourceBone.clear();
    if (fitter && RecordFit(ctx, source, *fitter))
        for (const auto &[i, weight] : fitted)
            ctx.input.bone[i] = weight;
}

std::vector<s_boneweight_t> FitGeometryWeights(StudioMdlState &ctx, const s_source_t &source,
                                            std::span<const Vector> positions, std::span<const uint32_t> bones,
                                            std::span<const float> weights, uint32_t stride) {
    if (!ctx.options.fitWeights || stride <= kKept)
        return {};

    std::optional<CWeightFitter> fitter;
    std::vector<s_boneweight_t> fitted;
    std::array<int, MAXSTUDIOSOURCEBONEWEIGHTS> indices;
    for (size_t i = 0; i < positions.size(); ++i) {
        const size_t base = i * stride;
        std::copy_n(bones.begin() + base, stride, indices.begin());
        s_sourceboneweight_t full;
        MakeSourceBoneWeights(stride, indices, weights.subspan(base, stride), full);
        if (full.numbones <= kKept)
            continue;
        if (!fitter) {
            fitter.emplace(MakeFitter(ctx, source));
            fitted.resize(positions.size());
        }
        Vector position = positions[i];
        scale_vertex(ctx, position);
        fitted[i] = fitter->FitVertex(position, full);
    }
    if (!fitter || !RecordFit(ctx, source, *fitter))
        return {};
    return fitted;
}

void FitSourceWeights(StudioMdlState &ctx) {
    const bool fit = ctx.options.fitWeights;
    for (s_source_t *source: ctx.anim.sources) {
        const std::span<s_vertexinfo_t> vertices(source->vertex, source->vertex ? source->numvertices : 0);
        const auto hasMore = [](const s_vertexinfo_t &v) {
            return v.sourceWeights.numbones > static_cast<int>(MAXSTUDIOBONEWEIGHTS);
        };
        if (std::ranges::none_of(vertices, hasMore)) {
            continue;
        }

        CWeightFitter fitter = MakeFitter(ctx, *source);
        struct Fitted {
            s_vertexinfo_t *vertex;
            s_boneweight_t boneweight;
        };
        std::vector<Fitted> fitted;
        for (s_vertexinfo_t &vertex: vertices) {
            s_sourceboneweight_t &full = vertex.sourceWeights;
            if (hasMore(vertex)) {
                if (fit)
                    fitted.push_back({&vertex, fitter.FitVertex(vertex.position, full)});
                else
                    fitter.Reduce(vertex.position, full.bone, full.weight, full.numbones);
            }
            full = {}; // reduced now; the full list isn't needed past this point
        }
        // boneweight already holds the plain trim; the fit replaces it only where it is better over the source
        if (RecordFit(ctx, *source, fitter)) {
            for (const Fitted &f: fitted) {
                f.vertex->boneweight = f.boneweight;
            }
        }
    }
    PrintSourceWeightRows(ctx, ctx.weightFitRows);
}

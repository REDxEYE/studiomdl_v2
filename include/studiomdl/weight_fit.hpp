//
// Experimental (-fitweights): reducing a vertex to MAXSTUDIOBONEWEIGHTS bone influences.
//
// The default cut keeps the largest weights and rescales them. The fitter keeps the same bones but picks their
// weights so the vertex stays as close as possible to where its full weight set puts it, over a set of sample
// poses of the skeleton (per-vertex least squares, weights >= 0 summing to 1).
//

#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "mathlib/mathlib.h"

struct s_source_t;
struct StudioMdlState;

// Largest pose count -fitweights accepts (fitting or test)
inline constexpr int kMaxFitWeightsPoses = 4096;

// The pipeline step (SimplifyModel, after every source including the LODs is loaded and before bones are remapped):
// for each source vertex with more than MAXSTUDIOBONEWEIGHTS influences (s_vertexinfo_t::sourceWeights), measures
// what the plain trim in boneweight loses and, with -fitweights, writes the fitted weights instead when the fit is
// better over the source. Clears sourceWeights. Prints one line per source that had such vertices (unless -quiet).
void FitSourceWeights(StudioMdlState &ctx);

class CWeightFitter {
public:
    // Builds the sample poses: every bone of the source rotated at random around its bind pose (fixed seeds).
    // Needs source.boneToPose (Build_Reference). fit = false only measures what the default cut loses.
    // fitPoseCount: poses the fit minimises the error over. More give a better fit (below ~12 it is worse than the
    // plain cut, the gain flattens past ~100). testPoseCount: poses the reported distances and FitIsBetter are
    // measured on; more give a steadier verdict. Cost grows linearly with the sum.
    CWeightFitter(const s_source_t &source, bool fit, int fitPoseCount, int testPoseCount);

    // bones/weights: `count` influences of a vertex at `position` (bind pose, the space of source.boneToPose),
    // sorted by descending weight, summing to 1, count > MAXSTUDIOBONEWEIGHTS.
    // Records how far the reduced vertex ends up from the full weights. When fitting, also overwrites the first
    // MAXSTUDIOBONEWEIGHTS weights with the fitted ones; they are not sorted anymore.
    void Reduce(const Vector &position, std::span<const int> bones, std::span<float> weights, int count);

    // True when fitting and, over the mesh, the written fit is no worse than the plain trim at the worst vertex and
    // better on average (on the test poses). Otherwise the caller should write the trim.
    bool FitIsBetter() const;

    // What was recorded; the distances are measured on the test poses (not used for fitting)
    struct Stats {
        int reduced = 0; // vertices
        int fitUsed = 0; // of those, vertices whose fitted weights differ from the trim
        double cutMean = 0, cutMax = 0, fitMean = 0, fitMax = 0;
    };
    Stats GetStats() const;

private:
    const matrix3x4_t &Skin(const int pose, const int bone) const { return m_skin[pose * m_numBones + bone]; }

    int m_fitPoses, m_testPoses;
    int m_numBones;
    std::vector<matrix3x4_t> m_skin; // [pose][bone]: bind pose space -> posed space, fitting poses first
    // scratch of Reduce: positions of one vertex over the poses, 3 doubles each
    std::vector<double> m_kept, m_keptEval; // [kept bone][pose]: that bone alone
    std::vector<double> m_full, m_fullEval; // [pose]: all the influences

    bool m_fit;
    int m_reduced = 0; // vertices
    int m_fitUsed = 0; // of those, vertices where the fitted weights were written (not the plain cut)
    double m_cutErrorSum = 0, m_fitErrorSum = 0, m_cutErrorMax = 0, m_fitErrorMax = 0; // over vertices x evaluation poses
};

#pragma once

#include <vector>

struct LegacyQHullFacet_t
{
    std::vector<int> pointIndices;
    double normal[3];
};

// Build a 3D convex hull with the same QHull revision/flags used by IVP's
// IVP_SurfaceBuilder_Pointsoup. Input is xyzxyz... in IVP/physics coordinates.
// Facet point indices reference the input point array.
bool LegacyQHullBuild3D( const std::vector<double> &coordinates,
    std::vector<LegacyQHullFacet_t> &facets );

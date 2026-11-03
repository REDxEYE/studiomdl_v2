#include "legacy_qhull_bridge.h"
#include "legacy_qhull_compat.h"
#include "qhull_a.hxx"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
// The supplied IVP branch defines IVP_NO_DOUBLE. Pointsoup feeds QHull doubles,
// but its post-QHull plane/area/edge cleanup copies both points and facet normals
// back to IVP_U_Point/IVP_DOUBLE, which are floats on Win32. Keep that split:
// QHull remains coordT/double while the IVP compatibility tests below are float.
static const float kMinEdgeLength = 0.01f;

static float PointDistanceSquared( const std::vector<coordT> &p, int a, int b )
{
    const float ax=(float)p[a*3+0], ay=(float)p[a*3+1], az=(float)p[a*3+2];
    const float bx=(float)p[b*3+0], by=(float)p[b*3+1], bz=(float)p[b*3+2];
    const float dx=ax-bx;
    const float dy=ay-by;
    const float dz=az-bz;
    return dx*dx + dy*dy + dz*dz;
}

static void BuildUniqueInput( const std::vector<double> &coordinates,
    std::vector<coordT> &points, std::vector<int> &sourceIndices )
{
    points.clear();
    sourceIndices.clear();

    const int count = static_cast<int>(coordinates.size()/3);
    for ( int i=0; i<count; ++i )
    {
        const double candidate[3] = {
            coordinates[i*3+0], coordinates[i*3+1], coordinates[i*3+2]
        };

        bool duplicate = false;
        for ( int j=0; j<(int)sourceIndices.size(); ++j )
        {
            // Input coordinates arrived by exact promotion from IVP floats, so
            // double-byte equality is equivalent here to the original
            // IVP_I_Point_VHash float-byte equality.
            if ( std::memcmp( &points[j*3], candidate, sizeof(candidate) ) == 0 )
            {
                duplicate = true;
                break;
            }
        }
        if ( duplicate )
            continue;

        points.push_back( candidate[0] );
        points.push_back( candidate[1] );
        points.push_back( candidate[2] );
        sourceIndices.push_back( i );
    }
}

static float FacetAreaSum( const std::vector<coordT> &points,
    const std::vector<int> &indices, const float normal[3] )
{
    if ( indices.empty() )
        return 0.0f;

    const int p0 = indices[0];
    const float p0x=(float)points[p0*3+0];
    const float p0y=(float)points[p0*3+1];
    const float p0z=(float)points[p0*3+2];
    float sum = 0.0f;
    for ( int i=0; i<(int)indices.size(); ++i )
    {
        const int a = indices[i];
        const int b = indices[(i+1)%indices.size()];

        const float ax=(float)points[a*3+0], ay=(float)points[a*3+1], az=(float)points[a*3+2];
        const float bx=(float)points[b*3+0], by=(float)points[b*3+1], bz=(float)points[b*3+2];

        const float d0x = bx-ax;
        const float d0y = by-ay;
        const float d0z = bz-az;
        const float d1x = ax-p0x;
        const float d1y = ay-p0y;
        const float d1z = az-p0z;

        // IVP_U_Point::calc_cross_product()/dot_product() scalar ordering.
        const float cx = d0y*d1z - d0z*d1y;
        const float cy = d0z*d1x - d0x*d1z;
        const float cz = d0x*d1y - d0y*d1x;
        sum += cx*normal[0] + cy*normal[1] + cz*normal[2];
    }
    return sum;
}

static float FacetEdgeLength( const std::vector<coordT> &points,
    const std::vector<int> &indices )
{
    float sum = 0.0f;
    for ( int i=0; i<(int)indices.size(); ++i )
    {
        const int a = indices[i];
        const int b = indices[(i+1)%indices.size()];
        sum += sqrtf( PointDistanceSquared(points,a,b) );
    }
    return sum;
}

}

bool LegacyQHullBuild3D( const std::vector<double> &coordinates,
    std::vector<LegacyQHullFacet_t> &facets )
{
    facets.clear();
    if ( coordinates.size() < 12 || (coordinates.size() % 3) != 0 )
        return false;

    std::vector<coordT> activePoints;
    std::vector<int> activeSourceIndices;
    BuildUniqueInput( coordinates, activePoints, activeSourceIndices );
    if ( activeSourceIndices.size() < 4 )
        return false;

    const int dim = 3;
    FILE *outfile = NULL;
    FILE *errfile = NULL;
    const boolT ismalloc = False;

    bool qhullAllocated = false;
    bool tryUnjoggled = true;
    int exitCode = 0;

    for ( double randomEpsilon = double(1.0e-12f); randomEpsilon < double(0.02f); )
    {
        const int numPoints = static_cast<int>(activeSourceIndices.size());
        std::vector<coordT> qhPoints = activePoints;
        char flags[250];

        if ( tryUnjoggled )
        {
            std::strcpy( flags, "qhull Qs Pp C-0 W1e-14 E1.0e-6" );
            if ( qhullAllocated )
                qh_freeqhull( !qh_ALL );
            exitCode = qh_new_qhull( dim, numPoints, &qhPoints[0], ismalloc,
                flags, outfile, errfile );
            qhullAllocated = true;
        }

        if ( exitCode || !tryUnjoggled )
        {
            std::sprintf( flags, "qhull Qs QJ%G C-0 Pp W1e-14 E1.0e-18", randomEpsilon );
            if ( qhullAllocated )
                qh_freeqhull( !qh_ALL );
            qhPoints = activePoints;
            exitCode = qh_new_qhull( dim, numPoints, &qhPoints[0], ismalloc,
                flags, outfile, errfile );
            qhullAllocated = true;
            if ( exitCode )
            {
                randomEpsilon = (randomEpsilon + double(1.0e-12f)) * double(1.2f);
                tryUnjoggled = false;
                continue;
            }
        }

        std::vector<unsigned char> skipList(numPoints,0);
        std::vector<unsigned char> useList(numPoints,0);
        std::vector<LegacyQHullFacet_t> candidateFacets;
        bool skipPoint = false;

        facetT *facet;
        FORALLfacets
        {
            std::vector<int> localIndices;
            setT *vertices = qh_facet3vertex( facet );
            vertexT *vertex, **vertexp;
            FOREACHvertex_(vertices)
            {
                const int pointIndex = qh_pointid( vertex->point );
                if ( pointIndex >= 0 && pointIndex < numPoints )
                {
                    localIndices.push_back( pointIndex );
                    if ( useList[pointIndex] != 0xff )
                        ++useList[pointIndex];
                }
            }
            qh_settempfree( &vertices );

            if ( localIndices.size() < 3 )
                continue;

            float planeNormal[3] = {
                (float)facet->normal[0], (float)facet->normal[1], (float)facet->normal[2]
            };
            float areaSum = FacetAreaSum( activePoints, localIndices, planeNormal );
            const float edgeSum = FacetEdgeLength( activePoints, localIndices );
            if ( areaSum < 0.0f )
            {
                planeNormal[0] = -planeNormal[0];
                planeNormal[1] = -planeNormal[1];
                planeNormal[2] = -planeNormal[2];
                areaSum = -areaSum;
            }

            LegacyQHullFacet_t out;
            out.normal[0] = planeNormal[0];
            out.normal[1] = planeNormal[1];
            out.normal[2] = planeNormal[2];
            for ( int i=0; i<(int)localIndices.size(); ++i )
                out.pointIndices.push_back( activeSourceIndices[localIndices[i]] );
            candidateFacets.push_back( out );

            // This is the non-obvious Pointsoup cleanup that removes very thin
            // coplanar/interior hull vertices before accepting QHull's result.
            // It is responsible for several vertex-count differences versus a
            // plain QHull invocation on Source collision meshes.
            if ( areaSum < 0.5 * kMinEdgeLength * edgeSum )
            {
                bool alreadySkippedOnFacet = false;
                for ( int i=0; i<(int)localIndices.size(); ++i )
                {
                    if ( skipList[localIndices[i]] )
                    {
                        alreadySkippedOnFacet = true;
                        break;
                    }
                }
                if ( alreadySkippedOnFacet )
                    continue;

                float maxDist = -1.0f;
                int maxIndex = 0;
                for ( int i=0; i<(int)localIndices.size(); ++i )
                {
                    const float dist = sqrtf( PointDistanceSquared(activePoints,
                        localIndices[0],localIndices[i]) );
                    if ( dist > maxDist )
                    {
                        maxDist = dist;
                        maxIndex = i;
                    }
                }

                maxDist = -1.0f;
                int maxIndex2 = 0;
                for ( int i=0; i<(int)localIndices.size(); ++i )
                {
                    skipPoint = true;
                    const float dist = sqrtf( PointDistanceSquared(activePoints,
                        localIndices[maxIndex],localIndices[i]) );
                    if ( dist > maxDist )
                    {
                        maxDist = dist;
                        maxIndex2 = i;
                    }
                }

                for ( int i=0; i<(int)localIndices.size(); ++i )
                {
                    if ( i != maxIndex && i != maxIndex2 )
                    {
                        ++skipList[localIndices[i]];
                        break;
                    }
                }
            }
        }

        if ( !skipPoint )
        {
            facets.swap( candidateFacets );
            break;
        }

        std::vector<coordT> compactPoints;
        std::vector<int> compactSourceIndices;
        compactPoints.reserve( activePoints.size() );
        compactSourceIndices.reserve( activeSourceIndices.size() );
        for ( int i=0; i<numPoints; ++i )
        {
            if ( useList[i] && !skipList[i] )
            {
                compactPoints.push_back( activePoints[i*3+0] );
                compactPoints.push_back( activePoints[i*3+1] );
                compactPoints.push_back( activePoints[i*3+2] );
                compactSourceIndices.push_back( activeSourceIndices[i] );
            }
        }

        if ( compactSourceIndices.size() == activeSourceIndices.size() )
        {
            randomEpsilon = (randomEpsilon + double(1.0e-12f)) * double(1.2f);
            tryUnjoggled = false;
        }

        activePoints.swap( compactPoints );
        activeSourceIndices.swap( compactSourceIndices );
        if ( activeSourceIndices.size() <= 3 )
            break;
    }

    if ( qhullAllocated )
    {
        qh_freeqhull( !qh_ALL );
        int currentLong = 0, totalLong = 0;
        qh_memfreeshort( &currentLong, &totalLong );
    }

    return exitCode == 0 && !facets.empty();
}

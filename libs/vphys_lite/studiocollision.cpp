#include "studiocollision.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <list>
#include <utility>
#include <vector>

#include "mathlib/mathlib.h"
#include "legacy_qhull/legacy_qhull_bridge.h"
#include "studiomdl/studiomdl_global_state.hpp"

CStudioCollision g_StudioCollision;

namespace
{
static const float kMetersPerInch = 0.0254f;
static const float kHullEpsilon = 1.0e-5f;
static const float kCompactStep = 1.0f / 250.0f;

static uint32_t MakeId( char a, char b, char c, char d )
{
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
        (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
}

static Vector ToPhysics( const Vector &v )
{
    return Vector( v.x * kMetersPerInch, -v.z * kMetersPerInch, v.y * kMetersPerInch );
}

static Vector FromPhysics( const Vector &v )
{
    const float s = 1.0f / kMetersPerInch;
    return Vector( v.x * s, v.z * s, -v.y * s );
}

#pragma pack(push, 4)
struct CompactSurfaceDisk_t
{
    float massCenter[3];
    float rotationInertia[3];
    float upperLimitRadius;
    uint32_t sizeAndDeviation;
    int32_t ledgeTreeRootOffset;
    int32_t runtimeCollideIndex;
    int32_t reserved;
    uint32_t formatTag;
};

struct CompactLedgeDisk_t
{
    int32_t pointOffset;
    int32_t treeNodeOrClientData;
    uint32_t flagsAndSize;
    int16_t triangleCount;
    uint16_t cacheOrReserved;
};

struct CompactTriangleDisk_t
{
    uint32_t info;
    uint32_t edge[3];
};

struct CompactPointDisk_t
{
    float xyz[3];
    uint32_t clientData;
};

struct CompactTreeNodeDisk_t
{
    int32_t rightNodeOffset;
    int32_t compactLedgeOffset;
    float center[3];
    float radius;
    uint8_t boxSize[3];
    uint8_t ledgeIndex;
};

// Matches vphysics::compactsurfaceheader_t: 8-byte base header + 20-byte compact-surface fields.
struct CompactSurfaceFileHeader_t
{
    uint32_t vphysicsID;
    uint16_t version;
    uint16_t modelType;
    int32_t surfaceSize;
    float dragAxisAreas[3];
    int32_t axisMapSize;
};
#pragma pack(pop)

static_assert( sizeof(CompactSurfaceDisk_t) == 48, "compact surface layout changed" );
static_assert( sizeof(CompactLedgeDisk_t) == 16, "compact ledge layout changed" );
static_assert( sizeof(CompactTriangleDisk_t) == 16, "compact triangle layout changed" );
static_assert( sizeof(CompactPointDisk_t) == 16, "compact point layout changed" );
static_assert( sizeof(CompactTreeNodeDisk_t) == 28, "compact tree layout changed" );
static_assert( sizeof(CompactSurfaceFileHeader_t) == 28, "VPHY header layout changed" );

struct HullFace_t
{
    int v[3];
    Vector normal;
    bool removed;
};

static Vector FaceNormal( const std::vector<Vector> &p, int a, int b, int c )
{
    Vector n = CrossProduct( p[b] - p[a], p[c] - p[a] );
    const float len = n.Length();
    if ( len > 0.0f )
        n /= len;
    return n;
}

static float FaceDistance( const HullFace_t &f, const std::vector<Vector> &p, const Vector &q )
{
    return DotProduct( f.normal, q - p[f.v[0]] );
}

static HullFace_t MakeFace( const std::vector<Vector> &p, int a, int b, int c, const Vector &inside )
{
    HullFace_t f;
    f.v[0] = a; f.v[1] = b; f.v[2] = c; f.removed = false;
    f.normal = FaceNormal( p, a, b, c );
    if ( DotProduct( f.normal, inside - p[a] ) > 0.0f )
    {
        std::swap( f.v[1], f.v[2] );
        f.normal = FaceNormal( p, f.v[0], f.v[1], f.v[2] );
    }
    return f;
}

static bool SamePoint( const Vector &a, const Vector &b )
{
    return (a - b).LengthSqr() <= kHullEpsilon * kHullEpsilon;
}

static bool BuildConvexHull( const std::vector<Vector> &input, std::vector<Vector> &outVerts, std::vector<HullFace_t> &outFaces )
{
    std::vector<Vector> p;
    p.reserve( input.size() );
    for ( size_t i = 0; i < input.size(); ++i )
    {
        bool dup = false;
        for ( size_t j = 0; j < p.size(); ++j )
        {
            if ( SamePoint( p[j], input[i] ) ) { dup = true; break; }
        }
        if ( !dup ) p.push_back( input[i] );
    }
    if ( p.size() < 4 ) return false;

    int i0 = 0, i1 = 1;
    float best = -1.0f;
    for ( int a = 0; a < (int)p.size(); ++a )
    for ( int b = a + 1; b < (int)p.size(); ++b )
    {
        float d = (p[a] - p[b]).LengthSqr();
        if ( d > best ) { best = d; i0 = a; i1 = b; }
    }
    if ( best <= kHullEpsilon * kHullEpsilon ) return false;

    int i2 = -1;
    best = -1.0f;
    Vector line = p[i1] - p[i0];
    float lineLen2 = line.LengthSqr();
    for ( int i = 0; i < (int)p.size(); ++i )
    {
        if ( i == i0 || i == i1 ) continue;
        Vector cross = CrossProduct( line, p[i] - p[i0] );
        float d = cross.LengthSqr() / lineLen2;
        if ( d > best ) { best = d; i2 = i; }
    }
    if ( i2 < 0 || best <= kHullEpsilon * kHullEpsilon ) return false;

    Vector baseN = CrossProduct( p[i1] - p[i0], p[i2] - p[i0] );
    float baseLen = baseN.Length();
    if ( baseLen <= kHullEpsilon ) return false;
    baseN /= baseLen;

    int i3 = -1;
    best = -1.0f;
    for ( int i = 0; i < (int)p.size(); ++i )
    {
        if ( i == i0 || i == i1 || i == i2 ) continue;
        float d = fabsf( DotProduct( baseN, p[i] - p[i0] ) );
        if ( d > best ) { best = d; i3 = i; }
    }
    if ( i3 < 0 || best <= kHullEpsilon ) return false;

    Vector inside = (p[i0] + p[i1] + p[i2] + p[i3]) * 0.25f;
    std::vector<HullFace_t> faces;
    faces.push_back( MakeFace( p, i0, i1, i2, inside ) );
    faces.push_back( MakeFace( p, i0, i3, i1, inside ) );
    faces.push_back( MakeFace( p, i1, i3, i2, inside ) );
    faces.push_back( MakeFace( p, i2, i3, i0, inside ) );

    std::set<int> seed;
    seed.insert(i0); seed.insert(i1); seed.insert(i2); seed.insert(i3);

    for ( int ip = 0; ip < (int)p.size(); ++ip )
    {
        if ( seed.count(ip) ) continue;
        std::vector<int> visible;
        for ( int fi = 0; fi < (int)faces.size(); ++fi )
        {
            if ( !faces[fi].removed && FaceDistance( faces[fi], p, p[ip] ) > kHullEpsilon )
                visible.push_back(fi);
        }
        if ( visible.empty() ) continue;

        std::map<std::pair<int,int>, int> horizon;
        for ( size_t vi = 0; vi < visible.size(); ++vi )
        {
            HullFace_t &f = faces[visible[vi]];
            f.removed = true;
            for ( int e = 0; e < 3; ++e )
            {
                int a = f.v[e], b = f.v[(e+1)%3];
                std::pair<int,int> rev(b,a);
                std::map<std::pair<int,int>, int>::iterator it = horizon.find(rev);
                if ( it != horizon.end() ) horizon.erase(it);
                else horizon[std::make_pair(a,b)] = 1;
            }
        }
        for ( std::map<std::pair<int,int>, int>::const_iterator it = horizon.begin(); it != horizon.end(); ++it )
            faces.push_back( MakeFace( p, it->first.first, it->first.second, ip, inside ) );
    }

    std::set<int> used;
    for ( size_t i = 0; i < faces.size(); ++i )
        if ( !faces[i].removed ) for ( int e=0; e<3; ++e ) used.insert( faces[i].v[e] );
    if ( used.size() < 4 ) return false;

    std::map<int,int> remap;
    outVerts.clear();
    for ( std::set<int>::const_iterator it = used.begin(); it != used.end(); ++it )
    {
        remap[*it] = (int)outVerts.size();
        outVerts.push_back( p[*it] );
    }
    outFaces.clear();
    for ( size_t i = 0; i < faces.size(); ++i )
    {
        if ( faces[i].removed ) continue;
        HullFace_t f = faces[i];
        f.v[0]=remap[f.v[0]]; f.v[1]=remap[f.v[1]]; f.v[2]=remap[f.v[2]];
        f.normal = FaceNormal( outVerts, f.v[0], f.v[1], f.v[2] );
        outFaces.push_back(f);
    }
    return !outFaces.empty();
}

// Legacy Pointsoup/QHull path used by the original IVP convex builder.  This
// branch of IVP unconditionally defines IVP_NO_DOUBLE, so IVP_DOUBLE and
// IVP_U_Point are FLOAT based on Win32.  QHull itself still uses doubles; we
// therefore keep compiler-side IVP geometry in float and promote to double only
// at the QHull boundary, matching the original Pointsoup pipeline.
struct LegacyPhysicsPoint_t
{
    float k[3];

    LegacyPhysicsPoint_t()
    {
        k[0] = k[1] = k[2] = 0.0f;
    }

    LegacyPhysicsPoint_t( float x, float y, float z )
    {
        k[0] = x; k[1] = y; k[2] = z;
    }
};

static bool BuildLegacyPointsoupConvexPhysics(
    const std::vector<LegacyPhysicsPoint_t> &inputPhysics,
    const std::vector<Vector> *pSourceHL,
    std::vector<Vector> &outVertsHL, std::vector<Vector> &outVertsPhysics,
    std::vector<HullFace_t> &outFaces );

static bool BuildLegacyPointsoupConvex( const std::vector<Vector> &inputHL,
    std::vector<Vector> &outVertsHL, std::vector<Vector> &outVertsPhysics,
    std::vector<HullFace_t> &outFaces );

// -----------------------------------------------------------------------------
// Legacy-compatible half-space cleanup
// -----------------------------------------------------------------------------
// CPhysicsCollision::RebuildConvexFromPlanes() is unusually sensitive to both
// scalar precision and insertion order.  In the supplied IVP branch
// IVP_NO_DOUBLE is defined in ivu_types.hxx, which means IVP_DOUBLE is float:
//   * compact FLOAT vertices are copied into FLOAT IVP_U_Point values;
//   * Hesse planes, matrix inversion and plane intersections are FLOAT;
//   * replacement near-parallel planes are appended, then old planes removed;
//   * i<j<k intersections remain FLOAT through Halfspacesoup;
//   * those floats are promoted to QHull's coordT (double) only in Pointsoup.
// Preserve those details here.  Earlier versions of this replacement kept the
// half-space algebra in double, which was numerically close but not what this
// actual VPhysics/IVP build did.
struct HullPlane_t
{
    float k[3];       // inward-facing unit normal
    float hesseVal;   // inside: dot(k,p) + hesseVal >= -tolerance
};

static const float kHalfspaceTolerance = 0.01f * 0.01f; // HALFSPACESOUP_TOLERANCE
static const float kHalfspacePointMerge = 0.01f;
static const float kIvpScalarEpsilon = 1.0e-10f;        // P_DOUBLE_EPS with IVP_NO_DOUBLE

static float DotLegacy( const LegacyPhysicsPoint_t &a, const LegacyPhysicsPoint_t &b )
{
    return a.k[0]*b.k[0] + a.k[1]*b.k[1] + a.k[2]*b.k[2];
}

static float DistanceSqrLegacy( const LegacyPhysicsPoint_t &a, const LegacyPhysicsPoint_t &b )
{
    const float x=a.k[0]-b.k[0];
    const float y=a.k[1]-b.k[1];
    const float z=a.k[2]-b.k[2];
    return x*x+y*y+z*z;
}

static LegacyPhysicsPoint_t CrossLegacy( const LegacyPhysicsPoint_t &v1,
    const LegacyPhysicsPoint_t &v2 )
{
    // Keep the operation ordering from IVP_U_Point::inline_calc_cross_product;
    // it matters once every temporary is a float rather than an expression
    // evaluated as a wider scalar.
    float a = v1.k[1]*v2.k[2];
    const float a2 = v2.k[1]*v1.k[2];
    float b = v1.k[2]*v2.k[0];
    const float b2 = v2.k[2]*v1.k[0];
    a -= a2;
    float c = v1.k[0]*v2.k[1];
    const float c2 = v2.k[0]*v1.k[1];
    b -= b2;
    c -= c2;
    return LegacyPhysicsPoint_t(a,b,c);
}

static bool IntersectThreePlanes( const HullPlane_t &a, const HullPlane_t &b,
    const HullPlane_t &c, LegacyPhysicsPoint_t &out )
{
    // Equivalent to IVP_U_Matrix::real_invert() + vmult3() in set_crossing().
    // With IVP_NO_DOUBLE every quantity below is a float.
    LegacyPhysicsPoint_t rowA(a.k[0],a.k[1],a.k[2]);
    LegacyPhysicsPoint_t rowB(b.k[0],b.k[1],b.k[2]);
    LegacyPhysicsPoint_t rowC(c.k[0],c.k[1],c.k[2]);
    LegacyPhysicsPoint_t r0 = CrossLegacy(rowB,rowC);
    LegacyPhysicsPoint_t r1 = CrossLegacy(rowC,rowA);
    LegacyPhysicsPoint_t r2 = CrossLegacy(rowA,rowB);

    const float det = DotLegacy(rowA,r0);
    if ( fabsf(det) < kIvpScalarEpsilon )
        return false;

    const float invDet = 1.0f / det;
    r0.k[0]*=invDet; r0.k[1]*=invDet; r0.k[2]*=invDet;
    r1.k[0]*=invDet; r1.k[1]*=invDet; r1.k[2]*=invDet;
    r2.k[0]*=invDet; r2.k[1]*=invDet; r2.k[2]*=invDet;

    const float right0 = -a.hesseVal;
    const float right1 = -b.hesseVal;
    const float right2 = -c.hesseVal;

    // set_col(r0/r1/r2), then matrix.vmult3(right_side).
    out.k[0] = r0.k[0]*right0 + r1.k[0]*right1 + r2.k[0]*right2;
    out.k[1] = r0.k[1]*right0 + r1.k[1]*right1 + r2.k[1]*right2;
    out.k[2] = r0.k[2]*right0 + r1.k[2]*right1 + r2.k[2]*right2;
    return true;
}

static void AddHalfspacePlane( std::vector<HullPlane_t> &planes,
    const HullPlane_t &candidate )
{
    // Mirror IVP_Halfspacesoup::add_halfspace() literally.  A replacement is
    // appended first and matching old planes are then removed, which moves the
    // surviving plane to the end of the vector.  Replacing in-place changes
    // the later i<j<k intersection order and therefore compact point ordering.
    std::vector<size_t> removeIndices;

    for ( size_t i=0; i<planes.size(); ++i )
    {
        const float dp =
            candidate.k[0]*planes[i].k[0] +
            candidate.k[1]*planes[i].k[1] +
            candidate.k[2]*planes[i].k[2];

        if ( dp > 1.0f-kHalfspaceTolerance )
        {
            if ( planes[i].hesseVal < candidate.hesseVal )
                return;
            removeIndices.push_back(i);
        }
    }

    planes.push_back(candidate);
    for ( int i=(int)removeIndices.size()-1; i>=0; --i )
        planes.erase(planes.begin()+removeIndices[i]);
}

static LegacyPhysicsPoint_t CompactPhysicsPoint( const Vector &hl )
{
    // The source is the previous compact ledge, whose point coordinates are
    // floats. ToPhysics() gives us the same compiler-side quantisation.
    const Vector p=ToPhysics(hl);
    return LegacyPhysicsPoint_t(p.x,p.y,p.z);
}

static Vector PhysicsLegacyToHL( const LegacyPhysicsPoint_t &p )
{
    return FromPhysics(Vector(p.k[0],p.k[1],p.k[2]));
}

static bool RebuildConvexFromPlanes( const std::vector<Vector> &hullVertsHL,
    const std::vector<HullFace_t> &hullFaces, std::vector<Vector> &outVertsHL,
    std::vector<Vector> &outVertsPhysics, std::vector<HullFace_t> &outFaces )
{
    std::vector<HullPlane_t> planes;
    planes.reserve(hullFaces.size());

    // Match CPhysicsCollision::RebuildConvexFromPlanes():
    // plane.calc_hesse(p0,p2,p1), testLen > 1e-6f, plane.normize(),
    // then halfspaces.add_halfspace().  Under IVP_NO_DOUBLE both calc_hesse()
    // and normize() operate on floats; normize() is 1/sqrtf(quad_length).
    for ( size_t i=0; i<hullFaces.size(); ++i )
    {
        const HullFace_t &f=hullFaces[i];
        const LegacyPhysicsPoint_t p0=CompactPhysicsPoint(hullVertsHL[f.v[0]]);
        const LegacyPhysicsPoint_t pNext=CompactPhysicsPoint(hullVertsHL[f.v[1]]);
        const LegacyPhysicsPoint_t pPrev=CompactPhysicsPoint(hullVertsHL[f.v[2]]);

        const float fp0=p0.k[0], fp1=p0.k[1], fp2=p0.k[2];
        const float a0=pNext.k[0]-fp0;
        const float a1=pNext.k[1]-fp1;
        const float a2=pNext.k[2]-fp2;
        const float b0=pPrev.k[0]-fp0;
        const float b1=pPrev.k[1]-fp1;
        const float b2=pPrev.k[2]-fp2;

        // IVP_U_Hesse::calc_hesse(): b x a.
        HullPlane_t plane;
        plane.k[0]=b1*a2-a1*b2;
        plane.k[1]=b2*a0-a2*b0;
        plane.k[2]=b0*a1-a0*b1;
        plane.hesseVal=-(plane.k[0]*fp0 + plane.k[1]*fp1 + plane.k[2]*fp2);

        const float qlen=plane.k[0]*plane.k[0] +
                         plane.k[1]*plane.k[1] +
                         plane.k[2]*plane.k[2];
        const float testLen=sqrtf(qlen);
        if ( !(testLen > 1.0e-6f) )
            continue;

        if ( qlen < kIvpScalarEpsilon )
            continue;
        const float invLen=1.0f/sqrtf(qlen);
        plane.k[0]*=invLen;
        plane.k[1]*=invLen;
        plane.k[2]*=invLen;
        plane.hesseVal*=invLen;
        AddHalfspacePlane(planes,plane);
    }

    if ( planes.size()<4 )
        return false;

    std::vector<LegacyPhysicsPoint_t> points;
    const float mergeDist2=kHalfspacePointMerge*kHalfspacePointMerge;

    // IVP_SurfaceBuilder_Halfspacesoup::convert_halfspacesoup_to_points().
    for ( size_t i=0; i<planes.size(); ++i )
    for ( size_t j=i+1; j<planes.size(); ++j )
    for ( size_t k=j+1; k<planes.size(); ++k )
    {
        LegacyPhysicsPoint_t point;
        if ( !IntersectThreePlanes(planes[i],planes[j],planes[k],point) )
            continue;

        bool outside=false;
        for ( size_t l=0; l<planes.size(); ++l )
        {
            const float distance =
                planes[l].k[0]*point.k[0] +
                planes[l].k[1]*point.k[1] +
                planes[l].k[2]*point.k[2] +
                planes[l].hesseVal;
            if ( distance < -kHalfspaceTolerance )
            {
                outside=true;
                break;
            }
        }
        if ( outside )
            continue;

        // insert_point_into_list(): first existing point within threshold wins.
        bool merged=false;
        for ( size_t existing=0; existing<points.size(); ++existing )
        {
            if ( DistanceSqrLegacy(point,points[existing]) < mergeDist2 )
            {
                merged=true;
                break;
            }
        }
        if ( !merged )
            points.push_back(point);
    }

    if ( points.size()<4 )
        return false;

    // Preserve the seemingly redundant second merge pass in the old builder.
    for ( size_t i=0; i<points.size(); ++i )
    {
        for ( int j=(int)points.size()-1; j>(int)i; --j )
        {
            if ( DistanceSqrLegacy(points[i],points[j]) < mergeDist2 )
                points.erase(points.begin()+j);
        }
    }

    if ( points.size()<4 )
        return false;

    // Halfspacesoup hands its FLOAT IVP_U_Point list directly to Pointsoup.
    // BuildLegacyPointsoupConvexPhysics promotes those floats to QHull doubles
    // without an unnecessary Source-unit round trip.
    if ( BuildLegacyPointsoupConvexPhysics(points,NULL,outVertsHL,outVertsPhysics,outFaces) )
        return true;

    // Safety fallback only. Runtime-compatible output is preferable to losing
    // collision on a pathological input rejected by the legacy path.
    std::vector<Vector> rebuiltHL;
    rebuiltHL.reserve(points.size());
    for ( size_t i=0; i<points.size(); ++i )
        rebuiltHL.push_back(PhysicsLegacyToHL(points[i]));
    if ( !BuildConvexHull(rebuiltHL,outVertsHL,outFaces) )
        return false;
    outVertsPhysics.resize(outVertsHL.size());
    for ( size_t i=0; i<outVertsHL.size(); ++i )
        outVertsPhysics[i]=ToPhysics(outVertsHL[i]);
    return true;
}

static bool BuildStudioConvex( const std::vector<Vector> &input,
    std::vector<Vector> &outVerts, std::vector<Vector> &outVertsPhysics,
    std::vector<HullFace_t> &outFaces )
{
    std::vector<Vector> firstVerts;
    std::vector<Vector> firstPhysics;
    std::vector<HullFace_t> firstFaces;
    if ( !BuildLegacyPointsoupConvex(input,firstVerts,firstPhysics,firstFaces) )
    {
        if ( !BuildConvexHull(input,firstVerts,firstFaces) )
            return false;
        firstPhysics.resize(firstVerts.size());
        for ( size_t i=0; i<firstVerts.size(); ++i )
            firstPhysics[i]=ToPhysics(firstVerts[i]);
    }

    // Same fallback behaviour as CPhysicsCollision::ConvexFromVerts(): if the
    // half-space rebuild fails, keep the initial pointsoup hull.
    if ( RebuildConvexFromPlanes(firstVerts,firstFaces,outVerts,outVertsPhysics,outFaces) )
        return true;

    outVerts.swap(firstVerts);
    outVertsPhysics.swap(firstPhysics);
    outFaces.swap(firstFaces);
    return true;
}

static uint32_t PackTriangleInfo( int tri, int pierce, bool isVirtual = false )
{
    return (uint32_t(tri) & 0xFFFu) |
        ((uint32_t(pierce) & 0xFFFu) << 12) |
        (isVirtual ? 0x80000000u : 0u);
}

static uint32_t PackEdge( int pointIndex, int oppositeDelta, bool isVirtual = false )
{
    uint32_t opp = uint32_t(oppositeDelta) & 0x7FFFu;
    return (uint32_t(pointIndex) & 0xFFFFu) | (opp << 16) |
        (isVirtual ? 0x80000000u : 0u);
}

static uint32_t PackLedgeFlags( bool hasChildren, int sizeBytes )
{
    // bit layout matches the MSVC layout used by the original 32-bit compact ledge:
    // has_children:2, is_compact:2, reserved:4, size_div_16:24.
    return (hasChildren ? 1u : 0u) | (1u << 2) | ((uint32_t(sizeBytes >> 4) & 0xFFFFFFu) << 8);
}

static uint32_t PackSurfaceSize( int byteSize, uint8_t deviation )
{
    return uint32_t(deviation) | ((uint32_t(byteSize) & 0xFFFFFFu) << 8);
}

static void AddBounds( const Vector &p, Vector &mins, Vector &maxs )
{
    mins.x = MIN(mins.x,p.x); mins.y = MIN(mins.y,p.y); mins.z = MIN(mins.z,p.z);
    maxs.x = MAX(maxs.x,p.x); maxs.y = MAX(maxs.y,p.y); maxs.z = MAX(maxs.z,p.z);
}

struct TreeBuild_t
{
    int left, right;
    int convexIndex;
    Vector mins, maxs;
};

static int BuildLegacyMedianCutTree( std::vector<TreeBuild_t> &nodes, const std::vector<StudioConvex_t*> &convexes,
    const std::vector<int> &indices );
}

struct StudioConvex_t
{
    std::vector<Vector> vertsHL;
    // Preserve the exact compact-space FLOAT coordinates produced by the
    // Pointsoup/Halfspacesoup pipeline.  The old VPhysics builder serializes
    // these values directly; converting them IVP -> Source -> IVP introduces
    // a one-ULP round trip on a subset of coordinates and changes pierce data.
    std::vector<Vector> vertsPhysics;
    std::vector<HullFace_t> faces;
    unsigned int gameData;
};

struct StudioCollide_t
{
    std::vector<unsigned char> surface;
    Vector orthoAreas;
    Vector minsHL, maxsHL;
};

namespace
{
static Vector PhysicsPoint( const StudioConvex_t *convex, int vertexIndex )
{
    if ( vertexIndex>=0 && vertexIndex<(int)convex->vertsPhysics.size() )
        return convex->vertsPhysics[vertexIndex];
    return ToPhysics( convex->vertsHL[vertexIndex] );
}

static void ConvexBounds( const StudioConvex_t *c, Vector &mins, Vector &maxs )
{
    mins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
    maxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    for ( size_t i=0; i<c->vertsHL.size(); ++i ) AddBounds( PhysicsPoint(c,(int)i), mins, maxs );
}

// Reconstruct the terminal IVV_Sphere box exactly as
// IVP_SurfaceBuilder_Ledge_Soup::ledges_to_boxes_and_spheres().  The terminal
// center/radius is based on the raw compact-ledge AABB, while box_sizes is a
// quantized expansion in 1/250th-radius steps.
static void ConvexQuantizedBounds( const StudioConvex_t *c, Vector &mins, Vector &maxs )
{
    Vector rawMins, rawMaxs;
    ConvexBounds( c, rawMins, rawMaxs );

    Vector center;
    center.x = rawMaxs.x * 0.5f + rawMins.x * 0.5f;
    center.y = rawMaxs.y * 0.5f + rawMins.y * 0.5f;
    center.z = rawMaxs.z * 0.5f + rawMins.z * 0.5f;

    const Vector half = rawMaxs - center;
    const float radius = half.Length();
    const float work = radius * kCompactStep;

    mins = center;
    maxs = center;
    for ( int axis=0; axis<3; ++axis )
    {
        // Deliberately do not clamp this.  The original stores the result in
        // uchar after this exact int conversion.  Valid compact ledges stay in
        // range naturally because half[axis] <= radius.
        const int boxSize = int( half[axis] / work ) + 1;
        const float extent = float(boxSize) * work;
        mins[axis] -= extent;
        maxs[axis] += extent;
    }
}

static Vector ConvexTerminalCenter( const StudioConvex_t *c )
{
    Vector mins, maxs, center;
    ConvexBounds( c, mins, maxs );
    center.x = maxs.x * 0.5f + mins.x * 0.5f;
    center.y = maxs.y * 0.5f + mins.y * 0.5f;
    center.z = maxs.z * 0.5f + mins.z * 0.5f;
    return center;
}

// Literal equivalent of IVP_SurfaceBuilder_Ledge_Soup::calculate_boundingbox().
// It operates on terminal spheres (not recursively-created parent spheres),
// which is important for reproducing the old median-cut split decisions.
static void CalculateLegacyTreeBounds( const std::vector<StudioConvex_t*> &convexes,
    const std::vector<int> &indices, Vector &mins, Vector &maxs )
{
    mins.Init( 1000000.0f, 1000000.0f, 1000000.0f );
    maxs.Init( -1000000.0f, -1000000.0f, -1000000.0f );

    for ( size_t i=0; i<indices.size(); ++i )
    {
        Vector mi, ma;
        ConvexQuantizedBounds( convexes[indices[i]], mi, ma );
        AddBounds( mi, mins, maxs );
        AddBounds( ma, mins, maxs );
    }
}

static float LegacyTreeBoxVolume( const Vector &mins, const Vector &maxs )
{
    // Keep the old float operation order used by IVP_U_Float_Point dims and
    // volume = x*y*z.  Bounds should already be ordered; fabsf matches the
    // original code and also preserves its behavior for an empty subset.
    const float dx = fabsf( maxs.x - mins.x );
    const float dy = fabsf( maxs.y - mins.y );
    const float dz = fabsf( maxs.z - mins.z );
    return dx * dy * dz;
}

// Reproduce IVP_SurfaceBuilder_Ledge_Soup::
// cluster_spheres_topdown_mediancut_recursively() with the active Source-era
// defines IVP_CLUSTER_SHORTRANGE_TOPDOWN + IVP_CLUSTER_SHORTRANGE_BALANCED.
//
// This is intentionally *not* a sorted median split.  IVP uses the arithmetic
// mean of terminal centers on each axis, handles centers lying on that mean in
// input order, evaluates all three candidate splits, and selects the one with
// the smallest sum of child bounding-box volumes.
static int BuildLegacyMedianCutTree( std::vector<TreeBuild_t> &nodes,
    const std::vector<StudioConvex_t*> &convexes, const std::vector<int> &indices )
{
    TreeBuild_t n;
    n.left = n.right = -1;
    n.convexIndex = -1;

    if ( indices.size() == 1 )
        ConvexBounds( convexes[indices[0]], n.mins, n.maxs );
    else
        CalculateLegacyTreeBounds( convexes, indices, n.mins, n.maxs );

    const int me = (int)nodes.size();
    nodes.push_back( n );

    if ( indices.size() == 1 )
    {
        nodes[me].convexIndex = indices[0];
        return me;
    }

    // Exact special case in the IVP routine: preserve incoming order.
    if ( indices.size() == 2 )
    {
        std::vector<int> left( 1, indices[0] );
        std::vector<int> right( 1, indices[1] );
        const int leftChild = BuildLegacyMedianCutTree( nodes, convexes, left );
        const int rightChild = BuildLegacyMedianCutTree( nodes, convexes, right );
        nodes[me].left = leftChild;
        nodes[me].right = rightChild;
        return me;
    }

    std::vector<int> left[3];
    std::vector<int> right[3];
    float differenceInBoundingboxVolumes[3] = { 0.0f, 0.0f, 0.0f };
    static const float kLegacyFloatResolution = 1.0e-6f; // P_FLOAT_RES

    for ( int axis=0; axis<3; ++axis )
    {
        left[axis].reserve( indices.size() );
        right[axis].reserve( indices.size() );

        float median = 0.0f;
        for ( size_t i=0; i<indices.size(); ++i )
            median += ConvexTerminalCenter( convexes[indices[i]] )[axis];
        median /= float(indices.size());

        bool toggle = true; // IVP_TRUE
        for ( size_t x=0; x<indices.size(); ++x )
        {
            const int convexIndex = indices[x];
            const float value = ConvexTerminalCenter( convexes[convexIndex] )[axis];

            if ( value < (median - kLegacyFloatResolution) )
            {
                left[axis].push_back( convexIndex );
            }
            else if ( value > (median + kLegacyFloatResolution) )
            {
                right[axis].push_back( convexIndex );
            }
            else
            {
                // Center lies on the median.  IVP first looks at the adjacent
                // terminal and deliberately places this one on the opposite
                // side.  If both are on the median it ensures both subsets are
                // non-empty, then alternates sides.
                const size_t referenceIndex = (x == indices.size()-1) ? x-1 : x+1;
                const float referenceValue = ConvexTerminalCenter(
                    convexes[indices[referenceIndex]] )[axis];

                if ( referenceValue < (median - kLegacyFloatResolution) )
                {
                    right[axis].push_back( convexIndex );
                    continue;
                }
                if ( referenceValue > (median + kLegacyFloatResolution) )
                {
                    left[axis].push_back( convexIndex );
                    continue;
                }

                if ( left[axis].empty() )
                {
                    left[axis].push_back( convexIndex );
                    continue;
                }
                if ( right[axis].empty() )
                {
                    right[axis].push_back( convexIndex );
                    continue;
                }

                if ( toggle )
                {
                    left[axis].push_back( convexIndex );
                    toggle = false;
                }
                else
                {
                    right[axis].push_back( convexIndex );
                    toggle = true;
                }
            }
        }

        Vector leftMins, leftMaxs, rightMins, rightMaxs;
        CalculateLegacyTreeBounds( convexes, left[axis], leftMins, leftMaxs );
        CalculateLegacyTreeBounds( convexes, right[axis], rightMins, rightMaxs );

        const float volume1 = LegacyTreeBoxVolume( leftMins, leftMaxs );
        const float volume2 = LegacyTreeBoxVolume( rightMins, rightMaxs );
        differenceInBoundingboxVolumes[axis] = volume2 + volume1;
    }

    int chosenAxis = 0;
    float maxDiff = 1.0e20f; // P_DOUBLE_MAX when IVP_NO_DOUBLE is enabled
    for ( int axis=0; axis<3; ++axis )
    {
        // Preserve the slightly unusual original control flow: maxDiff is
        // updated even when this particular split has an empty side.
        if ( differenceInBoundingboxVolumes[axis] < maxDiff )
        {
            if ( !left[axis].empty() && !right[axis].empty() )
                chosenAxis = axis;
            maxDiff = differenceInBoundingboxVolumes[axis];
        }
    }

    // Valid ledge soups produced by StudioMDL should always have a usable
    // candidate (the original code asserts this).  Keep a release-build safety
    // fallback rather than recursing with an empty vector on malformed input.
    if ( left[chosenAxis].empty() || right[chosenAxis].empty() )
    {
        for ( int axis=0; axis<3; ++axis )
        {
            if ( !left[axis].empty() && !right[axis].empty() )
            {
                chosenAxis = axis;
                break;
            }
        }
    }

    const int leftChild = BuildLegacyMedianCutTree( nodes, convexes, left[chosenAxis] );
    const int rightChild = BuildLegacyMedianCutTree( nodes, convexes, right[chosenAxis] );
    nodes[me].left = leftChild;
    nodes[me].right = rightChild;
    return me;
}

// The original compact-surface mass solver operates on terminal ledges in
// left-to-right tree order, then integrates that list backwards.  Keep an
// explicit leaf order so the accumulation order remains deterministic and is
// as close as possible to the VPhysics result without depending on IVP types.
static void GatherTreeLeafOrder( const std::vector<TreeBuild_t> &nodes, int nodeIndex,
    std::vector<int> &convexOrder )
{
    if ( nodeIndex < 0 || nodeIndex >= (int)nodes.size() )
        return;

    const TreeBuild_t &node = nodes[nodeIndex];
    if ( node.convexIndex >= 0 )
    {
        convexOrder.push_back( node.convexIndex );
        return;
    }

    GatherTreeLeafOrder( nodes, node.left, convexOrder );
    GatherTreeLeafOrder( nodes, node.right, convexOrder );
}

struct StudioMassProperties_t
{
    Vector center;
    Vector inertia;
    float radius;
    uint8_t deviationFactor;
};

// Integrate the closed convex surfaces to obtain their volume centroid.  This
// is the same tetrahedral surface integral used by the legacy compact-surface
// builder, expressed directly in terms of our own hull representation.
static bool ComputeVolumeMassCenter( const std::vector<StudioConvex_t*> &convexes,
    const std::vector<int> &convexOrder, Vector &center )
{
    Vector sumMass;
    sumMass.Init();
    float sumDet = 0.0f;
    float qsumSurface = 0.0f;

    for ( int orderIndex=(int)convexOrder.size()-1; orderIndex>=0; --orderIndex )
    {
        const StudioConvex_t *convex = convexes[convexOrder[orderIndex]];
        for ( size_t triangleIndex=0; triangleIndex<convex->faces.size(); ++triangleIndex )
        {
            const HullFace_t &face = convex->faces[triangleIndex];
            const Vector p0 = PhysicsPoint( convex, face.v[0] );
            const Vector p1 = PhysicsPoint( convex, face.v[1] );
            const Vector p2 = PhysicsPoint( convex, face.v[2] );

            const Vector surfaceNormal = CrossProduct( p1-p0, p2-p0 );
            const float det = DotProduct( surfaceNormal, p0 );
            const float weight = det * 0.25f;

            qsumSurface += DotProduct( surfaceNormal, surfaceNormal );
            sumDet += det;
            sumMass += p0 * weight;
            sumMass += p1 * weight;
            sumMass += p2 * weight;
        }
    }

    const float surface = sqrtf( qsumSurface );
    if ( !(sumDet > surface * surface * surface * 1.0e-9f) )
        return false;

    center = sumMass * (1.0f / sumDet);
    return true;
}

// Reproduce IVP_Compact_Ledge_Mass_Center_Solver with IVP_NO_DOUBLE.
// In the stock Win32 IVP configuration IVP_DOUBLE is float.  Keep every
// intermediate and accumulator as float and preserve the original expression
// ordering: the serialized rotation_inertia values are sensitive to these
// rounding points.
static void ComputeProjectedMassProperties( const std::vector<StudioConvex_t*> &convexes,
    const std::vector<int> &convexOrder, const Vector &center, int x, int y, int z,
    float &centerOut, float &massOut, float &inertiaOut )
{
    float massSum = 0.0f;
    float firstMomentSum = 0.0f;
    float secondMomentSum = 0.0f;

    for ( int orderIndex=(int)convexOrder.size()-1; orderIndex>=0; --orderIndex )
    {
        const StudioConvex_t *convex = convexes[convexOrder[orderIndex]];
        for ( size_t triangleIndex=0; triangleIndex<convex->faces.size(); ++triangleIndex )
        {
            const HullFace_t &face = convex->faces[triangleIndex];
            const Vector pObj0 = PhysicsPoint( convex, face.v[0] );
            const Vector pObj1 = PhysicsPoint( convex, face.v[1] );
            const Vector pObj2 = PhysicsPoint( convex, face.v[2] );

            // IVP_CLS.calc_hesse_vec_object_not_normized(edge,ledge,&hesse2)
            // calls inline_set_vert_to_area_defined_by_three_points(p0,p2,p1).
            // Spell it out to retain the IVP float operation order.
            const float fp0 = pObj0.x;
            const float fp1 = pObj0.y;
            const float fp2 = pObj0.z;
            const float a0 = pObj2.x - fp0;
            const float a1 = pObj2.y - fp1;
            const float a2 = pObj2.z - fp2;
            const float b0 = pObj1.x - fp0;
            const float b1 = pObj1.y - fp1;
            const float b2 = pObj1.z - fp2;

            float normalK[3];
            normalK[0] = b1*a2 - a1*b2;
            normalK[1] = b2*a0 - a2*b0;
            normalK[2] = b0*a1 - a0*b1;

            // IVP_U_Float_Point::normize() with IVP_NO_DOUBLE:
            // f = 1.0f / sqrtf(quad_length()); this->mult(f).
            const float normalLengthSquared = normalK[0]*normalK[0] +
                                              normalK[1]*normalK[1] +
                                              normalK[2]*normalK[2];
            if ( normalLengthSquared < 1.0e-10f )
                continue;
            const float invNormalLength = 1.0f / sqrtf( normalLengthSquared );
            normalK[0] *= invNormalLength;
            normalK[1] *= invNormalLength;
            normalK[2] *= invNormalLength;

            float fzdy;
            float fydz;
            bool threeEdgeArea;

            if ( fabsf(normalK[z]) < fabsf(normalK[y]) )
            {
                threeEdgeArea = false;
                fydz = 0.5f * normalK[z] / normalK[y];
                fzdy = 0.0f;
            }
            else
            {
                threeEdgeArea = true;
                if ( fabsf(normalK[z]) > 1.0e-6f )
                    fzdy = -0.5f * normalK[y] / normalK[z];
                else
                    fzdy = 0.0f;
                fydz = 0.0f;
            }

            float areaMassSum = 0.0f;
            float areaFirstMomentSum = 0.0f;
            float areaSecondMomentSum = 0.0f;

            for ( int edgeIndex=0; edgeIndex<3; ++edgeIndex )
            {
                // transform->vimult4() is an inverse translation here because the
                // inertia solver's matrix is identity with vv = mass center.
                const Vector p0 = PhysicsPoint( convex, face.v[edgeIndex] ) - center;
                const Vector p1 = PhysicsPoint( convex, face.v[(edgeIndex+1)%3] ) - center;

                float vecK[3];
                vecK[0] = p1.x - p0.x;
                vecK[1] = p1.y - p0.y;
                vecK[2] = p1.z - p0.z;

                const float vecLength = sqrtf( vecK[0]*vecK[0] +
                                               vecK[1]*vecK[1] +
                                               vecK[2]*vecK[2] );
                if ( fabsf(vecK[x]) < 1.0e-6f * vecLength )
                    continue;

                const float lydx = vecK[y] / vecK[x];
                const float ly0 = p0[y] - lydx * p0[x];

                float ka, kb, kc;
                if ( threeEdgeArea )
                {
                    ka = fzdy * ly0 * ly0;
                    kb = fzdy * 2.0f * ly0 * lydx;
                    kc = fzdy * lydx * lydx;
                }
                else
                {
                    const float lzdx = vecK[z] / vecK[x];
                    const float lz0 = p0[z] - lzdx * p0[x];
                    ka = (ly0 + fydz * lz0) * lz0;
                    kb = ly0 * lzdx + lydx * lz0 + 2.0f * fydz * lz0 * lzdx;
                    kc = (lydx + fydz * lzdx) * lzdx;
                }

                const float x0 = p0[x];
                const float x1 = p1[x];
                const float dx  = x1 - x0;
                const float dx2 = (x1*x1 - x0*x0) * .5f;
                const float dx3 = (x1*x1*x1 - x0*x0*x0) * (1.0f/3.0f);
                const float dx4 = (x1*x1*x1*x1 - x0*x0*x0*x0) * .25f;
                const float dx5 = (x1*x1*x1*x1*x1 - x0*x0*x0*x0*x0) * .2f;
                const float f = dx*ka + dx2*kb + dx3*kc;
                const float k = dx2*ka + dx3*kb + dx4*kc;
                const float m = dx3*ka + dx4*kb + dx5*kc;

                areaMassSum += f;
                areaFirstMomentSum += k;
                areaSecondMomentSum += m;
            }

            massSum += areaMassSum;
            firstMomentSum += areaFirstMomentSum;
            secondMomentSum += areaSecondMomentSum;
        }
    }

    if ( massSum < 1.0e-10f )
    {
        massOut = 0.0f;
        centerOut = 0.0f;
        inertiaOut = 1.0f;
    }
    else
    {
        centerOut = firstMomentSum / massSum;
        massOut = massSum;
        inertiaOut = secondMomentSum / massSum;
    }
}

static void ComputeRadiusAndDeviation( const std::vector<StudioConvex_t*> &convexes,
    const std::vector<int> &convexOrder, const Vector &center,
    float &radius, uint8_t &deviationFactor )
{
    float maxRadiusSquared = 0.0f;
    float maxDeviationSquared = 0.0f;

    for ( int orderIndex=(int)convexOrder.size()-1; orderIndex>=0; --orderIndex )
    {
        const StudioConvex_t *convex = convexes[convexOrder[orderIndex]];
        for ( size_t triangleIndex=0; triangleIndex<convex->faces.size(); ++triangleIndex )
        {
            const HullFace_t &face = convex->faces[triangleIndex];
            const Vector p0 = PhysicsPoint( convex, face.v[0] );
            const Vector p1 = PhysicsPoint( convex, face.v[1] );
            const Vector p2 = PhysicsPoint( convex, face.v[2] );
            const Vector normal = CrossProduct( p1-p0, p2-p0 );
            const float normalLengthSquared = DotProduct( normal, normal );
            if ( normalLengthSquared <= 0.0f )
                continue;

            const float invNormalLengthSquared = 1.0f / normalLengthSquared;
            for ( int vertex=0; vertex<3; ++vertex )
            {
                const Vector point = PhysicsPoint( convex, face.v[vertex] ) - center;
                const float radiusSquared = DotProduct( point, point );
                if ( radiusSquared > maxRadiusSquared )
                    maxRadiusSquared = radiusSquared;

                const Vector cross = CrossProduct( point, normal );
                const float deviationSquared = DotProduct( cross, cross ) * invNormalLengthSquared;
                if ( deviationSquared > maxDeviationSquared )
                    maxDeviationSquared = deviationSquared;
            }
        }
    }

    radius = sqrtf( maxRadiusSquared );
    const float radiusDeviation = sqrtf( maxDeviationSquared );

    int factor = 1;
    if ( radius > 0.0f )
        factor = int( 1.0f + radiusDeviation / (radius * kCompactStep) );
    if ( factor < 1 ) factor = 1;
    if ( factor > 255 ) factor = 255;
    deviationFactor = (uint8_t)factor;
}

static StudioMassProperties_t ComputeMassProperties( const std::vector<StudioConvex_t*> &convexes,
    const std::vector<TreeBuild_t> &tree )
{
    StudioMassProperties_t props;
    props.center.Init();
    props.inertia.Init( 1.0f, 1.0f, 1.0f );
    props.radius = 0.0f;
    props.deviationFactor = 1;

    std::vector<int> convexOrder;
    convexOrder.reserve( convexes.size() );
    if ( !tree.empty() )
        GatherTreeLeafOrder( tree, 0, convexOrder );
    if ( convexOrder.empty() )
        for ( int i=0; i<(int)convexes.size(); ++i ) convexOrder.push_back(i);

    if ( ComputeVolumeMassCenter(convexes,convexOrder,props.center) )
    {
        float volume;
        Vector inertia2d;
        Vector checkCenter;
        ComputeProjectedMassProperties( convexes,convexOrder,props.center,0,1,2,checkCenter.x,volume,inertia2d.x );
        ComputeProjectedMassProperties( convexes,convexOrder,props.center,1,2,0,checkCenter.y,volume,inertia2d.y );
        ComputeProjectedMassProperties( convexes,convexOrder,props.center,2,0,1,checkCenter.z,volume,inertia2d.z );

        float a = inertia2d.x;
        float b = inertia2d.y;
        float c = inertia2d.z;
        a *= a;
        b *= b;
        c *= c;
        props.inertia.Init( sqrtf(b+c), sqrtf(a+c), sqrtf(a+b) );
    }
    else
    {
        // Degenerate/open input fallback: match the old builder's geometric
        // approximation rather than emitting invalid mass properties.
        Vector mins, maxs;
        mins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
        maxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
        for ( size_t i=0; i<convexes.size(); ++i )
        {
            Vector convexMins, convexMaxs;
            ConvexBounds( convexes[i], convexMins, convexMaxs );
            AddBounds( convexMins, mins, maxs );
            AddBounds( convexMaxs, mins, maxs );
        }
        props.center = (mins+maxs) * 0.5f;
        const float geometricRadius = (maxs-mins).Length() * 0.5f;
        const float estimate = geometricRadius * geometricRadius * 0.5f;
        props.inertia.Init( estimate, estimate, estimate );
    }

    ComputeRadiusAndDeviation( convexes,convexOrder,props.center,props.radius,props.deviationFactor );
    return props;
}

// Reproduce IVP_Triangle::calc_hesse() for the final compact triangle points.
// This branch of IVP defines IVP_NO_DOUBLE, so IVP_U_Point/IVP_DOUBLE are
// floats and normalization uses sqrtf().  Keep the scalar operation order
// explicit; pierce selection is sensitive to tiny differences between nearly
// opposite face normals.
static Vector CalcLegacyPierceNormal( const StudioConvex_t *c, const HullFace_t &face )
{
    const Vector p0=PhysicsPoint(c,face.v[0]);
    const Vector p1=PhysicsPoint(c,face.v[1]);
    const Vector p2=PhysicsPoint(c,face.v[2]);

    // IVP_Triangle::calc_hesse():
    //   p0 = edge[0].start; p1 = edge[0].next->start; p2 = edge[0].prev->start;
    //   hesse.calc_hesse(p0,p2,p1); hesse.normize();
    // IVP_U_Hesse::calc_hesse() forms b x a, so with (p0,p2,p1) this is
    // (p1-p0) x (p2-p0), but preserve the original arithmetic ordering.
    const float fp0=p0.x, fp1=p0.y, fp2=p0.z;
    const float a0=p2.x-fp0;
    const float a1=p2.y-fp1;
    const float a2=p2.z-fp2;
    const float b0=p1.x-fp0;
    const float b1=p1.y-fp1;
    const float b2=p1.z-fp2;

    float c0=b1*a2-a1*b2;
    float c1=b2*a0-a2*b0;
    float c2=b0*a1-a0*b1;

    const float length=sqrtf(c0*c0+c1*c1+c2*c2);
    if ( length>0.0f )
    {
        const float invLength=1.0f/length;
        c0*=invLength;
        c1*=invLength;
        c2*=invLength;
    }
    return Vector(c0,c1,c2);
}

static float LegacyPierceDot( const Vector &a, const Vector &b )
{
    // Matches IVP_U_Point::dot_product().
    return a.x*b.x+a.y*b.y+a.z*b.z;
}

static void AssignPierce( const StudioConvex_t *c, std::vector<int> &pierce )
{
    const int n=(int)c->faces.size();
    pierce.assign(n,-1);

    std::vector<Vector> hesse;
    hesse.reserve(n);
    for ( int i=0; i<n; ++i )
        hesse.push_back(CalcLegacyPierceNormal(c,c->faces[i]));

    // Literal equivalent of IVP_Object_Polygon_Tetra::insert_pierce_info().
    // In particular, found_tri->pierced_triangle is overwritten unconditionally;
    // the original code does NOT test whether the found triangle was paired before.
    for ( int i=0; i<n; ++i )
    {
        if ( pierce[i]>=0 )
            continue;

        float minScalVal=-1.0e-6f;
        int found=-1;
        for ( int j=0; j<n; ++j )
        {
            const float scalVal=LegacyPierceDot(hesse[i],hesse[j]);
            if ( scalVal<minScalVal )
            {
                minScalVal=scalVal;
                found=j;
            }
        }

        // A valid closed convex should always have an opposite-enough triangle,
        // matching IVP's assertion. Keep a defensive fallback for malformed input.
        if ( found<0 )
            found=i;

        pierce[i]=found;
        pierce[found]=i;
    }
}

struct PointBits_t
{
    uint32_t xyz[3];

    bool operator<( const PointBits_t &rhs ) const
    {
        if ( xyz[0] != rhs.xyz[0] ) return xyz[0] < rhs.xyz[0];
        if ( xyz[1] != rhs.xyz[1] ) return xyz[1] < rhs.xyz[1];
        return xyz[2] < rhs.xyz[2];
    }
};

struct EdgeFeatureKey_t
{
    PointBits_t point[2];

    bool operator<( const EdgeFeatureKey_t &rhs ) const
    {
        if ( point[0] < rhs.point[0] ) return true;
        if ( rhs.point[0] < point[0] ) return false;
        return point[1] < rhs.point[1];
    }
};

struct TriangleFeatureKey_t
{
    PointBits_t point[3];

    bool operator<( const TriangleFeatureKey_t &rhs ) const
    {
        for ( int i=0; i<3; ++i )
        {
            if ( point[i] < rhs.point[i] ) return true;
            if ( rhs.point[i] < point[i] ) return false;
        }
        return false;
    }
};

struct RecursiveHullFeatures_t
{
    std::set<EdgeFeatureKey_t> edges;
    std::set<TriangleFeatureKey_t> triangles;
};

struct SharedLedgeBuild_t
{
    const StudioConvex_t *convex;
    bool hasChildren;
    int treeOrClient;
    int outputOffset;
    std::vector<int> edgePointIndex;
    std::vector<unsigned char> triangleVirtual;
    std::vector<unsigned char> edgeVirtual;
};

static PointBits_t PointBits( const Vector &v )
{
    PointBits_t bits;
    memcpy( &bits.xyz[0], &v.x, sizeof(bits.xyz[0]) );
    memcpy( &bits.xyz[1], &v.y, sizeof(bits.xyz[1]) );
    memcpy( &bits.xyz[2], &v.z, sizeof(bits.xyz[2]) );
    return bits;
}

// Keep feature keys in the exact edge/triangle vertex order.  This looks a bit
// odd, but it intentionally mirrors IVP_Compact_Recursive::set_rekursive_convex_hull().
// Its Edge_Key::set_edge() / Triangle_Key::set_tri() contain the historical
// in-place swap sequence
//
//     a = b; b = a; a = x;
//
// which does not actually swap the values.  As a result, the original VPhysics
// recursive-hull feature lookup is order-sensitive despite the comments saying
// the indices are sorted.  Preserving this quirk matters for the serialized
// is_virtual flags and therefore for runtime recursion into child ledges.
static EdgeFeatureKey_t MakeEdgeFeatureKey( const Vector &a, const Vector &b )
{
    EdgeFeatureKey_t key;
    key.point[0] = PointBits(a);
    key.point[1] = PointBits(b);
    return key;
}

static TriangleFeatureKey_t MakeTriangleFeatureKey( const Vector &a, const Vector &b, const Vector &c )
{
    TriangleFeatureKey_t key;
    key.point[0] = PointBits(a);
    key.point[1] = PointBits(b);
    key.point[2] = PointBits(c);
    return key;
}

// Reproduce the recursive hull's original IVP Pointsoup path closely enough
// to preserve its coplanar-point rejection.  A plain convex hull is not quite
// equivalent here: Pointsoup deliberately retries after dropping vertices from
// very thin facets (area < 0.5 * P_MIN_EDGE_LEN * edgeLength).  On real Source
// ragdoll collision this is what turns e.g. 117/230 outer hulls into the
// original 113/222 or 114/224 ledges.
struct LegacyFacetLine2D_t
{
    int a, b;
    LegacyFacetLine2D_t() : a(-1), b(-1) {}
    LegacyFacetLine2D_t( int ia, int ib ) : a(ia), b(ib) {}
};

static float LegacyPoint2DDistanceSquared( int a, int b,
    const std::vector<float> &x, const std::vector<float> &y )
{
    const float dx=x[a]-x[b], dy=y[a]-y[b];
    return dx*dx+dy*dy;
}

static float LegacyLineHesseDistance( const LegacyFacetLine2D_t &line, int p,
    const std::vector<float> &x, const std::vector<float> &y )
{
    const float dx=x[line.b]-x[line.a];
    const float dy=y[line.b]-y[line.a];
    return dy*(x[line.a]-x[p]) + dx*(y[p]-y[line.a]);
}

static bool LegacyLineHasPoints( const LegacyFacetLine2D_t &line, int a, int b )
{
    return (line.a==a && line.b==b) || (line.a==b && line.b==a);
}

static float LegacyLineDistanceToPoint( const LegacyFacetLine2D_t &line, int p,
    const std::vector<float> &x, const std::vector<float> &y )
{
    const float dx=x[line.b]-x[line.a];
    const float dy=y[line.b]-y[line.a];
    const float norm=dx*dx+dy*dy;
    if ( norm < kIvpScalarEpsilon )
        return LegacyPoint2DDistanceSquared(line.a,p,x,y);
    return LegacyLineHesseDistance(line,p,x,y)/sqrtf(norm);
}

static bool LegacyPointInLineInterval( const LegacyFacetLine2D_t &line, int p,
    const std::vector<float> &x, const std::vector<float> &y )
{
    const float dx=x[line.b]-x[line.a];
    const float dy=y[line.b]-y[line.a];
    const float eps=0.0001f; // P_CROSS_EPS
    const float adx=fabsf(dx);
    if ( adx > kIvpScalarEpsilon )
    {
        const int sign=dx>0.0f?1:-1;
        const float d=(x[p]-x[line.a])*float(sign);
        return d>eps && d<adx-eps;
    }
    const float ady=fabsf(dy);
    if ( ady > kIvpScalarEpsilon )
    {
        const int sign=dy>0.0f?1:-1;
        const float d=(y[p]-y[line.a])*float(sign);
        return d>eps && d<ady-eps;
    }
    return false;
}

static bool LegacyLinesOverlap( const LegacyFacetLine2D_t &u,
    const LegacyFacetLine2D_t &v, const std::vector<float> &x,
    const std::vector<float> &y )
{
    const float eps=1.0e-8f; // P_OVERLAP_EPS
    const float dx=x[u.b]-x[u.a];
    const float dy=y[u.b]-y[u.a];
    if ( fabsf(dx)>eps )
    {
        float lo,hi;
        if ( dx>0.0f ){lo=x[u.a];hi=x[u.b];}else{lo=x[u.b];hi=x[u.a];}
        lo+=eps;hi-=eps;
        const float s=x[v.a],e=x[v.b];
        if ( (s<=lo&&e<=lo)||(s>=hi&&e>=hi) ) return false;
        return true;
    }
    if ( fabsf(dy)>eps )
    {
        float lo,hi;
        if ( dy>0.0f ){lo=y[u.a];hi=y[u.b];}else{lo=y[u.b];hi=y[u.a];}
        lo+=eps;hi-=eps;
        const float s=y[v.a],e=y[v.b];
        if ( (s<=lo&&e<=lo)||(s>=hi&&e>=hi) ) return false;
        return true;
    }
    return LegacyPointInLineInterval(v,u.a,x,y);
}

static bool LegacyLinesCross( const LegacyFacetLine2D_t &u,
    const LegacyFacetLine2D_t &v, const std::vector<float> &x,
    const std::vector<float> &y )
{
    const float udx=x[u.b]-x[u.a], udy=y[u.b]-y[u.a];
    const float vdx=x[v.b]-x[v.a], vdy=y[v.b]-y[v.a];
    const float determinant=vdx*udy-udx*vdy;
    if ( fabsf(determinant)<1.0e-9f ) // DET_EPS
    {
        // The original compares dist_to_point() against P_CROSS_EPS_QUAD.
        if ( fabsf(LegacyLineDistanceToPoint(u,v.a,x,y))<(0.0001f*0.0001f) )
            return LegacyLinesOverlap(u,v,x,y);
        return false;
    }
    if ( u.a==v.a||u.a==v.b||u.b==v.a||u.b==v.b )
        return false;

    const float odx=x[v.a]-x[u.a], ody=y[v.a]-y[u.a];
    const float left=determinant>0.0f?0.0f:determinant;
    const float right=determinant>0.0f?determinant:0.0f;
    const float ut=vdx*ody-vdy*odx;
    if ( ut<left||ut>right ) return false;
    const float vt=udx*ody-odx*udy;
    if ( vt<left||vt>right ) return false;
    return true;
}

static bool LegacyPointLeftOfLine( const LegacyFacetLine2D_t &line, int p,
    const std::vector<float> &x, const std::vector<float> &y )
{
    // P_Sur_2D_Line::point_lies_to_the_left() only rejects <= P_DOUBLE_EPS.
    // Its second return value ("too flat") is still truthy at this call site.
    return LegacyLineHesseDistance(line,p,x,y)>kIvpScalarEpsilon;
}

static void OrientLegacyTriangle( int &a, int &b, int &c,
    const std::vector<LegacyPhysicsPoint_t> &sourcePhysics, const float normal[3] )
{
    const LegacyPhysicsPoint_t &p0=sourcePhysics[a];
    const LegacyPhysicsPoint_t &p1=sourcePhysics[b];
    const LegacyPhysicsPoint_t &p2=sourcePhysics[c];

    const float a0=p1.k[0]-p0.k[0], a1=p1.k[1]-p0.k[1], a2=p1.k[2]-p0.k[2];
    const float b0=p2.k[0]-p0.k[0], b1=p2.k[1]-p0.k[1], b2=p2.k[2]-p0.k[2];
    const float cx=a1*b2-a2*b1;
    const float cy=a2*b0-a0*b2;
    const float cz=a0*b1-a1*b0;
    const float d=cx*normal[0]+cy*normal[1]+cz*normal[2];
    if ( d<0.0f )
        std::swap(b,c);
}

static bool TriangulateLegacyFacet( const LegacyQHullFacet_t &facet,
    const std::vector<LegacyPhysicsPoint_t> &sourcePhysics, std::vector<int> &triangles )
{
    triangles.clear();
    const int n=(int)facet.pointIndices.size();
    if ( n<3 ) return false;

    // IVP copies QHull's double facet normal into IVP_Template_Surface::normal,
    // which is an IVP_U_Point and therefore FLOAT in this IVP_NO_DOUBLE build.
    const float facetNormal[3] = {
        (float)facet.normal[0], (float)facet.normal[1], (float)facet.normal[2]
    };

    // Do not special-case triangular QHull facets. IVP still routes them through
    // P_Sur_2D::calc_triangle_representation(), which can rotate the cyclic start
    // vertex and therefore affects the old order-sensitive recursive feature hash.
    int maxCoord=0;
    float maxValue=fabsf(facetNormal[0]);
    if ( fabsf(facetNormal[1])>maxValue ){maxValue=fabsf(facetNormal[1]);maxCoord=1;}
    if ( fabsf(facetNormal[2])>maxValue ){maxCoord=2;}
    int k0=0,k1=1;
    if ( maxCoord==0 ){k0=1;k1=2;}
    else if ( maxCoord==1 ){k0=0;k1=2;}
    else {k0=0;k1=1;}

    bool mirrored=facetNormal[maxCoord]<0.0f;
    if ( maxCoord==1 ) mirrored=!mirrored;

    std::vector<float> x(sourcePhysics.size()),y(sourcePhysics.size());
    for ( size_t i=0;i<sourcePhysics.size();++i )
    {
        x[i]=sourcePhysics[i].k[k0];
        y[i]=sourcePhysics[i].k[k1];
    }

    std::list<LegacyFacetLine2D_t> lines;
    for ( int i=0;i<n;++i )
    {
        const int p0=facet.pointIndices[i];
        const int p1=facet.pointIndices[(i+1)%n];
        lines.push_back(mirrored?LegacyFacetLine2D_t(p0,p1):LegacyFacetLine2D_t(p1,p0));
    }

    int loopCounter=0;
    while ( !lines.empty() )
    {
        if ( loopCounter++>100 ) return false;
        const LegacyFacetLine2D_t base=lines.front();
        const int a=base.a,b=base.b;

        struct Candidate_t{float score;int point;int order;};
        std::vector<Candidate_t> candidates;
        int order=0;
        for ( std::list<LegacyFacetLine2D_t>::const_iterator it=lines.begin();it!=lines.end();++it,++order )
        {
            if ( order==0 ) continue;
            const int c=it->a;
            if ( c==b ) continue;
            Candidate_t candidate;
            candidate.score=LegacyPoint2DDistanceSquared(a,c,x,y)+LegacyPoint2DDistanceSquared(b,c,x,y);
            candidate.point=c;candidate.order=order;
            candidates.push_back(candidate);
        }
        // Preserve insertion order for equal keys. IVP_U_Min_Hash has its own
        // address/hash tie behaviour; exact tie replication can be added later,
        // but do not introduce extra double precision here.
        for ( size_t i=0;i<candidates.size();++i )
        for ( size_t j=i+1;j<candidates.size();++j )
            if ( candidates[j].score<candidates[i].score ) std::swap(candidates[i],candidates[j]);

        bool found=false;
        int c=-1;
        for ( size_t ci=0;ci<candidates.size()&&!found;++ci )
        {
            c=candidates[ci].point;
            if ( !LegacyPointLeftOfLine(base,c,x,y) ) continue;
            const LegacyFacetLine2D_t ca(c,a),bc(b,c);

            bool reject=false;
            for ( std::list<LegacyFacetLine2D_t>::const_iterator it=lines.begin();it!=lines.end();++it )
            {
                if ( !LegacyLineHasPoints(*it,c,a) && LegacyLinesCross(*it,ca,x,y) ){reject=true;break;}
                if ( !LegacyLineHasPoints(*it,b,c) && LegacyLinesCross(*it,bc,x,y) ){reject=true;break;}
            }
            if ( reject ) continue;

            for ( std::list<LegacyFacetLine2D_t>::const_iterator it=lines.begin();it!=lines.end();++it )
            {
                const int p=it->a;
                if ( LegacyPointLeftOfLine(base,p,x,y) &&
                    LegacyPointLeftOfLine(bc,p,x,y) &&
                    LegacyPointLeftOfLine(ca,p,x,y) )
                { reject=true;break; }
            }
            if ( reject ) continue;
            found=true;
        }
        if ( !found ) return false;

        int ta=b,tb=c,tc=a;
        OrientLegacyTriangle(ta,tb,tc,sourcePhysics,facetNormal);
        triangles.push_back(ta);triangles.push_back(tb);triangles.push_back(tc);

        lines.pop_front();
        bool caRemoved=false,bcRemoved=false;
        for ( std::list<LegacyFacetLine2D_t>::iterator it=lines.begin();it!=lines.end(); )
        {
            if ( LegacyLineHasPoints(*it,c,a) )
            {
                it=lines.erase(it);caRemoved=true;continue;
            }
            if ( LegacyLineHasPoints(*it,c,b) )
            {
                it=lines.erase(it);bcRemoved=true;continue;
            }
            ++it;
        }
        if ( !caRemoved ) lines.push_front(LegacyFacetLine2D_t(a,c));
        if ( !bcRemoved ) lines.push_front(LegacyFacetLine2D_t(c,b));
    }

    return triangles.size()==size_t((n-2)*3);
}

static bool BuildLegacyPointsoupConvexPhysics(
    const std::vector<LegacyPhysicsPoint_t> &inputPhysics,
    const std::vector<Vector> *pSourceHL,
    std::vector<Vector> &outVertsHL, std::vector<Vector> &outVertsPhysics,
    std::vector<HullFace_t> &outFaces )
{
    if ( inputPhysics.size()<4 )
        return false;
    if ( pSourceHL && pSourceHL->size()!=inputPhysics.size() )
        return false;

    std::vector<double> coordinates;
    coordinates.reserve(inputPhysics.size()*3);
    for ( size_t i=0; i<inputPhysics.size(); ++i )
    {
        coordinates.push_back(inputPhysics[i].k[0]);
        coordinates.push_back(inputPhysics[i].k[1]);
        coordinates.push_back(inputPhysics[i].k[2]);
    }

    std::vector<LegacyQHullFacet_t> facets;
    if ( !LegacyQHullBuild3D(coordinates,facets) )
        return false;

    // IVP_Object_Polygon_Tetra processes QHull surfaces in forward order but
    // inserts each surface's triangles at the head of a P_List.  Therefore the
    // final compact-ledged triangle order has surface groups reversed while
    // preserving the ear-generation order inside each individual surface.
    std::vector<HullFace_t> compactFaces;
    std::vector<int> facetTriangles;
    for ( int fi=(int)facets.size()-1; fi>=0; --fi )
    {
        bool ok=TriangulateLegacyFacet(facets[fi],inputPhysics,facetTriangles);
        if ( !ok )
        {
            facetTriangles.clear();
            const std::vector<int> &poly=facets[fi].pointIndices;
            for ( int i=1; i+1<(int)poly.size(); ++i )
            {
                int a=poly[0],b=poly[i],c=poly[i+1];
                const float facetNormal[3] = {
                    (float)facets[fi].normal[0], (float)facets[fi].normal[1], (float)facets[fi].normal[2]
                };
                OrientLegacyTriangle(a,b,c,inputPhysics,facetNormal);
                facetTriangles.push_back(a);
                facetTriangles.push_back(b);
                facetTriangles.push_back(c);
            }
        }

        for ( size_t t=0; t+2<facetTriangles.size(); t+=3 )
        {
            const int a=facetTriangles[t+0],b=facetTriangles[t+1],c=facetTriangles[t+2];
            if ( a<0 || b<0 || c<0 ||
                a>=(int)inputPhysics.size() ||
                b>=(int)inputPhysics.size() ||
                c>=(int)inputPhysics.size() )
                continue;

            HullFace_t face;
            face.v[0]=a;face.v[1]=b;face.v[2]=c;face.removed=false;
            // QHull is double, but IVP copies its facet data into float P_Sur geometry.
            // Face normals are only a working/runtime-compiler helper, so form
            // them after remapping to the final Source-coordinate float points.
            face.normal.Init(0.0f,0.0f,0.0f);
            compactFaces.push_back(face);
        }
    }
    if ( compactFaces.empty() )
        return false;

    // IVP_Compact_Ledge_Generator numbers compact points on first encounter
    // while traversing compact triangles and edges 0..2.
    std::vector<int> remap(inputPhysics.size(),-1);
    outVertsHL.clear();
    outVertsPhysics.clear();
    outFaces.clear();
    outFaces.reserve(compactFaces.size());

    for ( size_t i=0; i<compactFaces.size(); ++i )
    {
        HullFace_t face=compactFaces[i];
        for ( int e=0; e<3; ++e )
        {
            const int oldIndex=face.v[e];
            if ( remap[oldIndex]<0 )
            {
                remap[oldIndex]=(int)outVertsHL.size();
                const LegacyPhysicsPoint_t &physics=inputPhysics[oldIndex];
                outVertsPhysics.push_back(Vector(physics.k[0],physics.k[1],physics.k[2]));
                if ( pSourceHL )
                    outVertsHL.push_back((*pSourceHL)[oldIndex]);
                else
                    outVertsHL.push_back(PhysicsLegacyToHL(physics));
            }
            face.v[e]=remap[oldIndex];
        }
        face.normal=FaceNormal(outVertsHL,face.v[0],face.v[1],face.v[2]);
        if ( face.normal.LengthSqr()>0.0f )
            outFaces.push_back(face);
    }

    return outVertsHL.size()>=4 && !outFaces.empty();
}

static bool BuildLegacyPointsoupConvex( const std::vector<Vector> &inputHL,
    std::vector<Vector> &outVertsHL, std::vector<Vector> &outVertsPhysics,
    std::vector<HullFace_t> &outFaces )
{
    std::vector<LegacyPhysicsPoint_t> inputPhysics;
    inputPhysics.reserve(inputHL.size());

    for ( size_t i=0; i<inputHL.size(); ++i )
    {
        // ConvertPositionToIVP multiplies Source FLOAT coordinates by the FLOAT
        // unit scale and stores them in IVP_U_Point, which is float here because
        // this IVP branch defines IVP_NO_DOUBLE.
        const Vector p=ToPhysics(inputHL[i]);
        inputPhysics.push_back( LegacyPhysicsPoint_t(p.x,p.y,p.z) );
    }

    return BuildLegacyPointsoupConvexPhysics(
        inputPhysics,&inputHL,outVertsHL,outVertsPhysics,outFaces );
}

static bool BuildLegacyRecursiveOuterHull( const std::vector<StudioConvex_t*> &convexes,
    const std::vector<int> &treeLeafOrder, std::vector<Vector> &outVertsHL,
    std::vector<Vector> &outVertsPhysics, std::vector<HullFace_t> &outFaces )
{
    std::vector<LegacyPhysicsPoint_t> sourcePhysics;
    std::map<PointBits_t,int> pointMap;

    // add_ledge_tree_to_convex_hull() first adds terminal ledges in left-to-right
    // tree order. IVP_Compact_Recursive::build_convex_hull() then walks that
    // vector backwards. Reproduce both ordering steps before feeding Pointsoup;
    // the point set is the same either way, but QHull serialization is observably
    // sensitive to first-seen point order.
    for ( int orderIndex=(int)treeLeafOrder.size()-1; orderIndex>=0; --orderIndex )
    {
        const int ci = treeLeafOrder[orderIndex];
        const StudioConvex_t *c=convexes[ci];
        for ( size_t ti=0; ti<c->faces.size(); ++ti )
        {
            const HullFace_t &f=c->faces[ti];
            for ( int e=0; e<3; ++e )
            {
                const Vector physics=PhysicsPoint(c,f.v[e]);
                const PointBits_t bits=PointBits(physics);
                if ( pointMap.find(bits)!=pointMap.end() )
                    continue;
                pointMap.insert(std::make_pair(bits,(int)sourcePhysics.size()));
                sourcePhysics.push_back(LegacyPhysicsPoint_t(physics.x,physics.y,physics.z));
            }
        }
    }

    return BuildLegacyPointsoupConvexPhysics(
        sourcePhysics,NULL,outVertsHL,outVertsPhysics,outFaces );
}

// The recursive outer hull is only an acceleration hull. The original builder
// tags every hull triangle/edge that is not an exact feature of any terminal
// convex as "virtual". Runtime mindist code uses those bits to recurse into the
// child ledges instead of resolving a contact against an artificial hull face.
// Reproduce that behavior using exact serialized float coordinates.
static void BuildRecursiveHullFeatures( const std::vector<StudioConvex_t*> &convexes,
    RecursiveHullFeatures_t &features )
{
    features.edges.clear();
    features.triangles.clear();

    for ( int convexIndex=(int)convexes.size()-1; convexIndex>=0; --convexIndex )
    {
        const StudioConvex_t *convex = convexes[convexIndex];
        for ( size_t triangleIndex=0; triangleIndex<convex->faces.size(); ++triangleIndex )
        {
            const HullFace_t &face = convex->faces[triangleIndex];
            Vector p[3];
            for ( int e=0; e<3; ++e )
                p[e] = PhysicsPoint( convex,face.v[e] );

            features.triangles.insert( MakeTriangleFeatureKey(p[0],p[1],p[2]) );
            features.edges.insert( MakeEdgeFeatureKey(p[0],p[1]) );
            features.edges.insert( MakeEdgeFeatureKey(p[1],p[2]) );
            features.edges.insert( MakeEdgeFeatureKey(p[2],p[0]) );
        }
    }
}

static void MarkRecursiveHullVirtualFeatures( const StudioConvex_t *outer,
    const RecursiveHullFeatures_t &features, SharedLedgeBuild_t &build )
{
    const int triangleCount = (int)outer->faces.size();
    build.triangleVirtual.assign( triangleCount, 0 );
    build.edgeVirtual.assign( triangleCount * 3, 0 );

    for ( int triangleIndex=0; triangleIndex<triangleCount; ++triangleIndex )
    {
        const HullFace_t &face = outer->faces[triangleIndex];
        Vector p[3];
        for ( int e=0; e<3; ++e )
            p[e] = PhysicsPoint( outer,face.v[e] );

        if ( features.triangles.find(MakeTriangleFeatureKey(p[0],p[1],p[2])) == features.triangles.end() )
            build.triangleVirtual[triangleIndex] = 1;

        for ( int e=0; e<3; ++e )
        {
            if ( features.edges.find(MakeEdgeFeatureKey(p[e],p[(e+1)%3])) == features.edges.end() )
                build.edgeVirtual[triangleIndex*3+e] = 1;
        }
    }
}

static void AddSharedPoint( const Vector &v, std::map<PointBits_t,int> &pointMap,
    std::vector<Vector> &points, int &pointIndex )
{
    const PointBits_t bits = PointBits(v);
    std::map<PointBits_t,int>::const_iterator found = pointMap.find(bits);
    if ( found != pointMap.end() )
    {
        pointIndex = found->second;
        return;
    }

    pointIndex = (int)points.size();
    points.push_back(v);
    pointMap.insert( std::make_pair(bits,pointIndex) );
}

// Build the common point array in the same traversal order used by
// IVP_SurfaceBuilder_Ledge_Soup::recompile_point_indizes_of_compact_ledge():
// ledges in output order, compact triangles in forward memory order, edges
// 0..2. (Its loop counter counts down, but the triangle pointer advances.) IVP's
// point hash compares exactly the first three floats (memcmp), so use the
// float bit patterns as the key rather than an epsilon comparison here.
static void BuildSharedPointPool( std::vector<SharedLedgeBuild_t> &ledges,
    std::vector<Vector> &sharedPoints )
{
    std::map<PointBits_t,int> pointMap;
    sharedPoints.clear();

    for ( size_t l=0; l<ledges.size(); ++l )
    {
        SharedLedgeBuild_t &build = ledges[l];
        const StudioConvex_t *c = build.convex;
        const int nt = (int)c->faces.size();
        build.edgePointIndex.assign( nt * 3, -1 );

        for ( int t=0; t<nt; ++t )
        {
            const HullFace_t &f = c->faces[t];
            for ( int e=0; e<3; ++e )
            {
                const Vector p = PhysicsPoint( c,f.v[e] );
                int pointIndex = -1;
                AddSharedPoint( p, pointMap, sharedPoints, pointIndex );
                build.edgePointIndex[t*3+e] = pointIndex;
            }
        }
    }
}

// Single-ledge surfaces in IVP keep the point array inside the ledge. Keep
// that representation because it already matches the original single-convex
// surface size/layout and avoids changing the common case unnecessarily.
static int AppendStandaloneLedge( std::vector<unsigned char> &dst, const StudioConvex_t *c,
    bool hasChildren, int treeOrClient )
{
    const int base=(int)dst.size();
    const int nt=(int)c->faces.size();
    const int np=(int)c->vertsHL.size();
    const int size=16+nt*16+np*16;
    dst.resize(base+size,0);
    CompactLedgeDisk_t *h=(CompactLedgeDisk_t*)&dst[base];
    h->pointOffset=16+nt*16;
    h->treeNodeOrClientData=treeOrClient;
    h->flagsAndSize=PackLedgeFlags(hasChildren,size);
    h->triangleCount=(int16_t)nt;

    std::map<std::pair<int,int>,std::pair<int,int> > edgeMap;
    std::vector<int> pierce; AssignPierce(c,pierce);
    CompactTriangleDisk_t *tri=(CompactTriangleDisk_t*)(&dst[base+16]);
    for(int i=0;i<nt;++i)
    {
        tri[i].info=PackTriangleInfo(i,pierce[i]);
        for(int e=0;e<3;++e)
        {
            int a=c->faces[i].v[e], b=c->faces[i].v[(e+1)%3];
            edgeMap[std::make_pair(a,b)]=std::make_pair(i,e);
            tri[i].edge[e]=PackEdge(a,0);
        }
    }
    for(int i=0;i<nt;++i) for(int e=0;e<3;++e)
    {
        int a=c->faces[i].v[e], b=c->faces[i].v[(e+1)%3];
        std::map<std::pair<int,int>,std::pair<int,int> >::iterator it=edgeMap.find(std::make_pair(b,a));
        int delta=0;
        if(it!=edgeMap.end()) delta=(it->second.first*4+it->second.second+1)-(i*4+e+1);
        tri[i].edge[e]=PackEdge(a,delta);
    }
    CompactPointDisk_t *pt=(CompactPointDisk_t*)(&dst[base+16+nt*16]);
    for(int i=0;i<np;++i)
    {
        Vector v=PhysicsPoint(c,i); pt[i].xyz[0]=v.x;pt[i].xyz[1]=v.y;pt[i].xyz[2]=v.z;pt[i].clientData=0;
    }
    return base;
}

// Multi-ledge surfaces use the IVP ledge-soup representation: every ledge
// stores only its 16-byte header and triangles, while c_point_offset points to
// one common point array placed after all ledges. The size field deliberately
// retains the original standalone ledge size, exactly as IVP does when it
// memcpy()s the source ledge header before remapping the point indices.
static int AppendSharedLedgeBody( std::vector<unsigned char> &dst, SharedLedgeBuild_t &build )
{
    const StudioConvex_t *c = build.convex;
    const int base=(int)dst.size();
    const int nt=(int)c->faces.size();
    const int np=(int)c->vertsHL.size();
    const int bodySize=16+nt*16;
    const int originalStandaloneSize=bodySize+np*16;

    dst.resize(base+bodySize,0);
    build.outputOffset=base;

    CompactLedgeDisk_t *h=(CompactLedgeDisk_t*)&dst[base];
    h->pointOffset=0; // patched after the common point array position is known
    h->treeNodeOrClientData=build.treeOrClient;
    h->flagsAndSize=PackLedgeFlags(build.hasChildren,originalStandaloneSize);
    h->triangleCount=(int16_t)nt;

    std::map<std::pair<int,int>,std::pair<int,int> > edgeMap;
    std::vector<int> pierce; AssignPierce(c,pierce);
    CompactTriangleDisk_t *tri=(CompactTriangleDisk_t*)(&dst[base+16]);
    for(int i=0;i<nt;++i)
    {
        const bool triangleVirtual = i < (int)build.triangleVirtual.size() && build.triangleVirtual[i] != 0;
        tri[i].info=PackTriangleInfo(i,pierce[i],triangleVirtual);
        for(int e=0;e<3;++e)
        {
            int a=c->faces[i].v[e], b=c->faces[i].v[(e+1)%3];
            edgeMap[std::make_pair(a,b)]=std::make_pair(i,e);
            const bool edgeVirtual = i*3+e < (int)build.edgeVirtual.size() && build.edgeVirtual[i*3+e] != 0;
            tri[i].edge[e]=PackEdge(build.edgePointIndex[i*3+e],0,edgeVirtual);
        }
    }
    for(int i=0;i<nt;++i) for(int e=0;e<3;++e)
    {
        int a=c->faces[i].v[e], b=c->faces[i].v[(e+1)%3];
        std::map<std::pair<int,int>,std::pair<int,int> >::iterator it=edgeMap.find(std::make_pair(b,a));
        int delta=0;
        if(it!=edgeMap.end()) delta=(it->second.first*4+it->second.second+1)-(i*4+e+1);
        const bool edgeVirtual = i*3+e < (int)build.edgeVirtual.size() && build.edgeVirtual[i*3+e] != 0;
        tri[i].edge[e]=PackEdge(build.edgePointIndex[i*3+e],delta,edgeVirtual);
    }
    return base;
}

static int AppendSharedPointArray( std::vector<unsigned char> &dst, const std::vector<Vector> &points )
{
    const int base=(int)dst.size();
    dst.resize( base + points.size()*sizeof(CompactPointDisk_t), 0 );
    if ( points.empty() )
        return base;

    CompactPointDisk_t *pt=(CompactPointDisk_t*)(&dst[base]);
    for(size_t i=0;i<points.size();++i)
    {
        pt[i].xyz[0]=points[i].x;
        pt[i].xyz[1]=points[i].y;
        pt[i].xyz[2]=points[i].z;
        pt[i].clientData=0;
    }
    return base;
}

static bool RayTriangle( const Vector &o,const Vector &d,const Vector &a,const Vector &b,const Vector &c )
{
    Vector e1=b-a,e2=c-a,p=CrossProduct(d,e2); float det=DotProduct(e1,p);
    if(fabsf(det)<1e-8f)return false; float inv=1.0f/det;
    Vector t=o-a; float u=DotProduct(t,p)*inv; if(u<0||u>1)return false;
    Vector q=CrossProduct(t,e1); float v=DotProduct(d,q)*inv; if(v<0||u+v>1)return false;
    float hit=DotProduct(e2,q)*inv; return hit>=0.0f && hit<=1.0f;
}

static bool RayHits( const std::vector<StudioConvex_t*> &cv, const Vector &start, const Vector &end )
{
    Vector d=end-start;
    for(size_t k=0;k<cv.size();++k) for(size_t i=0;i<cv[k]->faces.size();++i)
    {
        const HullFace_t &f=cv[k]->faces[i];
        if(RayTriangle(start,d,cv[k]->vertsHL[f.v[0]],cv[k]->vertsHL[f.v[1]],cv[k]->vertsHL[f.v[2]])) return true;
    }
    return false;
}
}

StudioConvex_t *CStudioCollision::ConvexFromVerts( Vector **pVerts, int vertCount )
{
    FunctionTimingRecorder _("Phys");
    if(!pVerts||vertCount<4)return NULL;
    std::vector<Vector> input; input.reserve(vertCount);
    for(int i=0;i<vertCount;++i) if(pVerts[i]) input.push_back(*pVerts[i]);
    StudioConvex_t *c=new StudioConvex_t; c->gameData=0;
    if(!BuildStudioConvex(input,c->vertsHL,c->vertsPhysics,c->faces)){delete c;return NULL;}
    return c;
}

void CStudioCollision::ConvexFree( StudioConvex_t *pConvex ){ delete pConvex; }
void CStudioCollision::SetConvexGameData( StudioConvex_t *pConvex,unsigned int gameData ){if(pConvex)pConvex->gameData=gameData;}

static float LegacyTetrahedronVolume( const Vector &p0, const Vector &p1,
    const Vector &p2, const Vector &p3 )
{
    // Literal CPhysicsCollision::TetrahedronVolume() arithmetic/order.
    Vector a, b, c, cross;
    float volume = 1.0f / 6.0f;
    a = p1 - p0;
    b = p2 - p0;
    c = p3 - p0;
    cross = CrossProduct( b, c );
    volume *= DotProduct( a, cross );
    if ( volume < 0.0f )
        return -volume;
    return volume;
}

float CStudioCollision::ConvexVolume( const StudioConvex_t *c ) const
{
    if ( !c || c->faces.empty() )
        return 0.0f;

    // Legacy LedgeInsidePoint() does not use the convex centroid.  It takes the
    // start point of edge 0 from the first compact triangle, after the same
    // compact-space -> HL conversion used for every triangle vertex.
    const HullFace_t &firstFace = c->faces[0];
    const Vector inside = FromPhysics( PhysicsPoint( c, firstFace.v[0] ) );

    float volume = 0.0f;
    for ( size_t i=0; i<c->faces.size(); ++i )
    {
        const HullFace_t &face = c->faces[i];
        const Vector p0 = FromPhysics( PhysicsPoint( c, face.v[0] ) );
        const Vector p1 = FromPhysics( PhysicsPoint( c, face.v[1] ) );
        const Vector p2 = FromPhysics( PhysicsPoint( c, face.v[2] ) );
        volume += LegacyTetrahedronVolume( inside, p0, p1, p2 );
    }
    return volume;
}

float CStudioCollision::ConvexSurfaceArea( const StudioConvex_t *c ) const
{
    if(!c)return 0;float area=0;for(size_t i=0;i<c->faces.size();++i){const HullFace_t &f=c->faces[i];area+=0.5f*CrossProduct(c->vertsHL[f.v[1]]-c->vertsHL[f.v[0]],c->vertsHL[f.v[2]]-c->vertsHL[f.v[0]]).Length();}return area;
}

StudioCollide_t *CStudioCollision::ConvertConvexToCollideParams( StudioConvex_t **input,int count,const StudioConvertConvexParams_t &params )
{
    if(!input||count<=0)return NULL;
    std::vector<StudioConvex_t*> cv; for(int i=0;i<count;++i)if(input[i])cv.push_back(input[i]); if(cv.empty())return NULL;
    StudioCollide_t *out=new StudioCollide_t; out->orthoAreas.Init(1,1,1); out->minsHL.Init(FLT_MAX,FLT_MAX,FLT_MAX); out->maxsHL.Init(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    for(size_t i=0;i<cv.size();++i)for(size_t j=0;j<cv[i]->vertsHL.size();++j)AddBounds(cv[i]->vertsHL[j],out->minsHL,out->maxsHL);

    std::vector<TreeBuild_t> tree;
    if ( !cv.empty() )
        tree.reserve( cv.size() * 2 - 1 );
    std::vector<int> ids;
    ids.reserve( cv.size() );
    for(int i=0;i<(int)cv.size();++i)ids.push_back(i);
    BuildLegacyMedianCutTree(tree,cv,ids);

    std::vector<int> treeLeafOrder;
    treeLeafOrder.reserve( cv.size() );
    GatherTreeLeafOrder( tree, 0, treeLeafOrder );

    std::vector<StudioConvex_t*> owned;
    StudioConvex_t *outer=NULL;
    if(params.buildOuterConvexHull && cv.size()>1)
    {
        outer=new StudioConvex_t;
        outer->gameData=0;
        if ( BuildLegacyRecursiveOuterHull(cv,treeLeafOrder,outer->vertsHL,outer->vertsPhysics,outer->faces) )
            owned.push_back(outer);
        else
        {
            // Keep the previous standalone builder as a safety fallback for
            // malformed/degenerate input where the legacy Pointsoup path fails.
            std::vector<Vector> all;
            for(size_t i=0;i<cv.size();++i)all.insert(all.end(),cv[i]->vertsHL.begin(),cv[i]->vertsHL.end());
            if ( BuildConvexHull(all,outer->vertsHL,outer->faces) )
            {
                outer->vertsPhysics.resize(outer->vertsHL.size());
                for ( size_t i=0; i<outer->vertsHL.size(); ++i )
                    outer->vertsPhysics[i]=ToPhysics(outer->vertsHL[i]);
                owned.push_back(outer);
            }
            else
            {
                delete outer;
                outer=NULL;
            }
        }
    }

    out->surface.resize(sizeof(CompactSurfaceDisk_t),0);
    std::vector<int> ledgeOffset(cv.size(),0);
    int outerOffset=-1;

    const size_t compiledLedgeCount = cv.size() + (outer ? 1 : 0);
    if ( compiledLedgeCount > 1 )
    {
        std::vector<SharedLedgeBuild_t> ledges;
        ledges.reserve( compiledLedgeCount );

        for ( size_t i=0; i<cv.size(); ++i )
        {
            SharedLedgeBuild_t build;
            build.convex = cv[i];
            build.hasChildren = false;
            build.treeOrClient = (int)cv[i]->gameData;
            build.outputOffset = -1;
            ledges.push_back(build);
        }

        if ( outer )
        {
            SharedLedgeBuild_t build;
            build.convex = outer;
            build.hasChildren = true;
            build.treeOrClient = 0;
            build.outputOffset = -1;

            RecursiveHullFeatures_t recursiveFeatures;
            BuildRecursiveHullFeatures( cv, recursiveFeatures );
            MarkRecursiveHullVirtualFeatures( outer, recursiveFeatures, build );

            ledges.push_back(build);
        }

        std::vector<Vector> sharedPoints;
        BuildSharedPointPool( ledges, sharedPoints );

        for ( size_t i=0; i<ledges.size(); ++i )
            AppendSharedLedgeBody( out->surface, ledges[i] );

        const int sharedPointBase = AppendSharedPointArray( out->surface, sharedPoints );
        for ( size_t i=0; i<ledges.size(); ++i )
        {
            CompactLedgeDisk_t *h = (CompactLedgeDisk_t*)(&out->surface[ledges[i].outputOffset]);
            h->pointOffset = sharedPointBase - ledges[i].outputOffset;
        }

        for ( size_t i=0; i<cv.size(); ++i )
            ledgeOffset[i] = ledges[i].outputOffset;
        if ( outer )
            outerOffset = ledges[cv.size()].outputOffset;
    }
    else
    {
        // The original ledge-soup builder only enables point merging when more
        // than one ledge is compiled. Preserve the old inline-point layout for
        // single-convex collisions.
        for ( size_t i=0; i<cv.size(); ++i )
            ledgeOffset[i] = AppendStandaloneLedge(out->surface,cv[i],false,(int)cv[i]->gameData);
        if ( outer )
            outerOffset = AppendStandaloneLedge(out->surface,outer,true,0);
    }

    const int treeBase=(int)out->surface.size(); out->surface.resize(treeBase+tree.size()*sizeof(CompactTreeNodeDisk_t),0);
    CompactTreeNodeDisk_t *diskTree=(CompactTreeNodeDisk_t*)(&out->surface[treeBase]);
    for(size_t i=0;i<tree.size();++i)
    {
        TreeBuild_t &n=tree[i]; CompactTreeNodeDisk_t &d=diskTree[i];
        // IVP ledge-soup uses set_interpolate(max,min,.5f), then
        // rad = max-center. Preserve that exact float operation order.
        Vector center;
        center.x=n.maxs.x*0.5f+n.mins.x*0.5f;
        center.y=n.maxs.y*0.5f+n.mins.y*0.5f;
        center.z=n.maxs.z*0.5f+n.mins.z*0.5f;
        Vector half=n.maxs-center; float radius=half.Length();
        d.center[0]=center.x;d.center[1]=center.y;d.center[2]=center.z;d.radius=radius;
        float work=radius*kCompactStep;
        for(int a=0;a<3;++a)
        {
            const int q=int(half[a]/work)+1;
            d.boxSize[a]=(uint8_t)q;
        }
        // IVP_SurfaceBuilder_Ledge_Soup::build_ledgetree() initializes
        // nLedgeIndex to zero for every node; it is not the convex number.
        d.ledgeIndex=0;
        if(n.convexIndex>=0){d.rightNodeOffset=0;d.compactLedgeOffset=ledgeOffset[n.convexIndex]-(treeBase+(int)i*sizeof(CompactTreeNodeDisk_t));}
        else{d.rightNodeOffset=(n.right-(int)i)*(int)sizeof(CompactTreeNodeDisk_t);d.compactLedgeOffset=(i==0&&outerOffset>=0)?outerOffset-(treeBase+(int)i*sizeof(CompactTreeNodeDisk_t)):0;}
    }
    if(outerOffset>=0){CompactLedgeDisk_t *h=(CompactLedgeDisk_t*)(&out->surface[outerOffset]);h->treeNodeOrClientData=treeBase-outerOffset;}

    CompactSurfaceDisk_t *s=(CompactSurfaceDisk_t*)&out->surface[0];
    const StudioMassProperties_t massProps = ComputeMassProperties( cv, tree );
    s->massCenter[0]=massProps.center.x;s->massCenter[1]=massProps.center.y;s->massCenter[2]=massProps.center.z;
    s->rotationInertia[0]=massProps.inertia.x;s->rotationInertia[1]=massProps.inertia.y;s->rotationInertia[2]=massProps.inertia.z;
    s->upperLimitRadius=massProps.radius;
    s->sizeAndDeviation=PackSurfaceSize((int)out->surface.size(),massProps.deviationFactor);
    s->ledgeTreeRootOffset=treeBase;s->runtimeCollideIndex=0;s->reserved=0;s->formatTag=MakeId('I','V','P','S');

    if(params.buildDragAxisAreas)
    {
        float side=sqrtf(params.dragAreaEpsilon);if(side<1e-4f)side=1e-4f;Vector mins=out->minsHL,maxs=out->maxsHL;
        for(int axis=0;axis<3;++axis){int u=(axis+1)%3,v=(axis+2)%3,hits=0,total=0;float hs=side*0.5f;for(float u0=mins[u]+hs;u0<maxs[u];u0+=side)for(float v0=mins[v]+hs;v0<maxs[v];v0+=side){Vector a,b;a[axis]=mins[axis]-1;b[axis]=maxs[axis]+1;a[u]=b[u]=u0;a[v]=b[v]=v0;if(RayHits(cv,a,b))++hits;++total;}if(total<=0)total=1;out->orthoAreas[axis]=float(hits)/float(total);}
    }

    for(size_t i=0;i<owned.size();++i)delete owned[i];
    for(int i=0;i<count;++i){delete input[i];input[i]=NULL;}
    return out;
}

void CStudioCollision::CollideSetMassCenter( StudioCollide_t *c,const Vector &massCenter )
{
    if(!c||c->surface.size()<sizeof(CompactSurfaceDisk_t))return;Vector p=ToPhysics(massCenter);CompactSurfaceDisk_t *s=(CompactSurfaceDisk_t*)&c->surface[0];s->massCenter[0]=p.x;s->massCenter[1]=p.y;s->massCenter[2]=p.z;
}

void CStudioCollision::CollideGetAABB( Vector *mins,Vector *maxs,const StudioCollide_t *c,const Vector &origin,const QAngle &angles ) const
{
    if(!mins||!maxs||!c)return;
    // StudioMDL calls this with identity transform. Keep a conservative translated result for non-identity callers.
    *mins=c->minsHL+origin;*maxs=c->maxsHL+origin;
}

int CStudioCollision::CollideSize( const StudioCollide_t *c ) const{return c?int(sizeof(CompactSurfaceFileHeader_t)+c->surface.size()):0;}

static void PatchSerializedTreeLedgeIndicesRecursive( unsigned char *surface, int surfaceSize,
    int nodeOffset, unsigned int &terminalIndex )
{
    if ( !surface || nodeOffset < 0 || nodeOffset + (int)sizeof(CompactTreeNodeDisk_t) > surfaceSize )
        return;

    CompactTreeNodeDisk_t *node = (CompactTreeNodeDisk_t *)( surface + nodeOffset );
    if ( node->rightNodeOffset == 0 )
    {
        // Valve-produced PHYs number terminal nodes in left-to-right tree
        // traversal order.  Do this only in the serialized copy so the v19
        // tree/radius floating-point codegen remains completely untouched.
        node->ledgeIndex = (uint8_t)terminalIndex++;
        return;
    }

    node->ledgeIndex = 0;
    const int leftOffset = nodeOffset + (int)sizeof(CompactTreeNodeDisk_t);
    const int rightOffset = nodeOffset + node->rightNodeOffset;
    PatchSerializedTreeLedgeIndicesRecursive( surface, surfaceSize, leftOffset, terminalIndex );
    PatchSerializedTreeLedgeIndicesRecursive( surface, surfaceSize, rightOffset, terminalIndex );
}

static void PatchSerializedTreeLedgeIndices( unsigned char *surface, int surfaceSize )
{
    if ( !surface || surfaceSize < (int)sizeof(CompactSurfaceDisk_t) )
        return;

    CompactSurfaceDisk_t *header = (CompactSurfaceDisk_t *)surface;
    if ( header->ledgeTreeRootOffset < 0 ||
         header->ledgeTreeRootOffset + (int)sizeof(CompactTreeNodeDisk_t) > surfaceSize )
        return;

    unsigned int terminalIndex = 0;
    PatchSerializedTreeLedgeIndicesRecursive( surface, surfaceSize,
        header->ledgeTreeRootOffset, terminalIndex );
}

int CStudioCollision::CollideWrite( char *dest,const StudioCollide_t *c ) const
{
    if(!dest||!c)return 0;CompactSurfaceFileHeader_t h;h.vphysicsID=MakeId('V','P','H','Y');h.version=0x0100;h.modelType=0;h.surfaceSize=(int)c->surface.size();h.dragAxisAreas[0]=c->orthoAreas.x;h.dragAxisAreas[1]=c->orthoAreas.y;h.dragAxisAreas[2]=c->orthoAreas.z;h.axisMapSize=0;memcpy(dest,&h,sizeof(h));memcpy(dest+sizeof(h),&c->surface[0],c->surface.size());

    // Keep the in-memory compact surface exactly as v19 generated it.  Only
    // patch the on-disk copy after all floating-point work is complete; putting
    // the index counter inside the tree-build loop changed /fp:fast codegen and
    // moved a few radius values by 1 ULP on otherwise exact samples.
    PatchSerializedTreeLedgeIndices(
        (unsigned char *)( dest + sizeof(h) ), (int)c->surface.size() );

    return int(sizeof(h)+c->surface.size());
}

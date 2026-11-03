#pragma once

#include <cstddef>

#include "mathlib/vector.h"
#include "tier1/utlvector.h"

struct StudioConvex_t;
struct StudioCollide_t;

struct StudioConvertConvexParams_t
{
    bool buildOuterConvexHull;
    bool buildDragAxisAreas;
    bool checkOptimalTracing;
    float dragAreaEpsilon;
    StudioConvex_t *pForcedOuterHull;

    StudioConvertConvexParams_t() { Defaults(); }
    void Defaults()
    {
        buildOuterConvexHull = false;
        buildDragAxisAreas = false;
        checkOptimalTracing = false;
        dragAreaEpsilon = 1.0f;
        pForcedOuterHull = NULL;
    }
};

class CStudioCollision
{
public:
    StudioConvex_t *ConvexFromVerts( Vector **pVerts, int vertCount );
    void ConvexFree( StudioConvex_t *pConvex );
    void SetConvexGameData( StudioConvex_t *pConvex, unsigned int gameData );
    float ConvexVolume( const StudioConvex_t *pConvex ) const;
    float ConvexSurfaceArea( const StudioConvex_t *pConvex ) const;

    StudioCollide_t *ConvertConvexToCollideParams( StudioConvex_t **pConvex, int convexCount,
        const StudioConvertConvexParams_t &params );

    void CollideSetMassCenter( StudioCollide_t *pCollide, const Vector &massCenter );
    void CollideGetAABB( Vector *pMins, Vector *pMaxs, const StudioCollide_t *pCollide,
        const Vector &collideOrigin, const QAngle &collideAngles ) const;

    int CollideSize( const StudioCollide_t *pCollide ) const;
    int CollideWrite( char *pDest, const StudioCollide_t *pCollide ) const;
};

extern CStudioCollision g_StudioCollision;

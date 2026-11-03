#pragma once

#include <cstddef>

#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

class CStudioSurfaceProps
{
public:
    CStudioSurfaceProps();
    void ParseSurfaceData( const char *pFilename, const char *pText );
    int GetSurfaceIndex( const char *pName ) const;
    void GetPhysicsProperties( int index, float *pDensity, float *pThickness, float *pFriction = NULL, float *pElasticity = NULL ) const;

private:
    struct Surface_t
    {
        CUtlString name;
        CUtlString base;
        float density;
        float thickness;
        bool hasDensity;
        bool hasThickness;
    };

    void ResolveInheritance();
    CUtlVector<Surface_t> m_Surface;
};

extern CStudioSurfaceProps g_StudioSurfaceProps;

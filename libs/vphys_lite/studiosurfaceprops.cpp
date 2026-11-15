#include "studiosurfaceprops.h"

#include <cctype>
#include <cstdlib>
#include <string>

#include "tier1/strtools.h"

CStudioSurfaceProps g_StudioSurfaceProps;

namespace
{
}

CStudioSurfaceProps::CStudioSurfaceProps()
{
    // The VPhysics DLL used by this StudioMDL revision creates surface index 0
    // internally, before any surfaceproperties scripts are parsed.  This is
    // important for tools/game branches that do not ship a manifest at all:
    // unknown surfaceprops still fall back to a real "default" material.
    //
    // cevphysics.dll initialization values:
    //   friction   = 0.8f
    //   elasticity = 0.25f
    //   density    = 2000.0f kg/m^3
    //   thickness  = 0.0f
    //   dampening  = 0.0f
    //
    // StudioMDL only consumes density/thickness from this lightweight
    // replacement, so keep the database representation limited to those
    // properties while preserving the original built-in defaults.
    Surface_t defaultSurface;
    defaultSurface.name = "default";
    defaultSurface.base = "";
    defaultSurface.density = 2000.0f;
    defaultSurface.thickness = 0.0f;
    defaultSurface.hasDensity = true;
    defaultSurface.hasThickness = true;
    m_Surface.AddToTail( defaultSurface );
}

int CStudioSurfaceProps::GetSurfaceIndex( const char *pName ) const
{
    if ( !pName || !pName[0] ) pName = "default";
    for ( int i = 0; i < m_Surface.size(); ++i )
        if ( !Q_stricmp( m_Surface[i].name.String(), pName ) ) return i;
    return -1;
}

void CStudioSurfaceProps::GetPhysicsProperties( int index, float *pDensity, float *pThickness,
    float *pFriction, float *pElasticity ) const
{
    // Match CPhysicsSurfaceProps::GetPhysicsProperties(): an unknown material
    // falls back to the "default" surface instead of returning zero density.
    // Returning zero here makes $automass compute a total mass of 0 kg.
    if ( index < 0 || index >= m_Surface.size() )
        index = GetSurfaceIndex( "default" );

    float density = 0.0f, thickness = 0.0f;
    if ( index >= 0 && index < m_Surface.size() )
    {
        const Surface_t &s = m_Surface[index];
        density = s.density;
        thickness = s.thickness;
    }

    if ( pDensity ) *pDensity = density;
    if ( pThickness ) *pThickness = thickness;
    if ( pFriction ) *pFriction = 0.0f;
    if ( pElasticity ) *pElasticity = 0.0f;
}

#include "studiosurfaceprops.h"

#include <cctype>
#include <cstdlib>
#include <string>

#include "tier1/strtools.h"

CStudioSurfaceProps g_StudioSurfaceProps;

namespace
{
static bool NextToken( const char *&p, std::string &out )
{
    out.clear();
    if ( !p ) return false;
    for ( ;; )
    {
        while ( *p && std::isspace( (unsigned char)*p ) ) ++p;
        if ( p[0] == '/' && p[1] == '/' )
        {
            p += 2;
            while ( *p && *p != '\n' ) ++p;
            continue;
        }
        break;
    }
    if ( !*p ) return false;
    if ( *p == '{' || *p == '}' )
    {
        out.assign( 1, *p++ );
        return true;
    }
    if ( *p == '"' )
    {
        ++p;
        while ( *p && *p != '"' )
        {
            if ( *p == '\\' && p[1] ) ++p;
            out.push_back( *p++ );
        }
        if ( *p == '"' ) ++p;
        return true;
    }
    while ( *p && !std::isspace( (unsigned char)*p ) && *p != '{' && *p != '}' )
        out.push_back( *p++ );
    return !out.empty();
}
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
    for ( int i = 0; i < m_Surface.Count(); ++i )
        if ( !Q_stricmp( m_Surface[i].name.String(), pName ) ) return i;
    return -1;
}

void CStudioSurfaceProps::ResolveInheritance()
{
    // Parsing applies bases immediately, matching the original surfaceprops database.
}

void CStudioSurfaceProps::ParseSurfaceData( const char *pFilename, const char *pText )
{
    (void)pFilename;
    if ( !pText ) return;

    const char *p = pText;
    std::string name, token, value;
    while ( NextToken( p, name ) )
    {
        if ( name == "}" ) continue;
        if ( !NextToken( p, token ) || token != "{" ) break;

        int index = GetSurfaceIndex( name.c_str() );
        Surface_t work;

        // CPhysicsSurfaceProps starts an override from the material's current
        // physics properties.  A new material starts from "default" when it
        // exists.  This matters for surfaceproperties manifests where a later
        // file only overrides a subset of an existing material.
        if ( index >= 0 )
        {
            work = m_Surface[index];
        }
        else
        {
            work.name = name.c_str();
            work.base = "";
            work.density = 0.0f;
            work.thickness = 0.0f;
            work.hasDensity = false;
            work.hasThickness = false;

            int defaultIndex = GetSurfaceIndex( "default" );
            if ( defaultIndex >= 0 )
            {
                work.density = m_Surface[defaultIndex].density;
                work.thickness = m_Surface[defaultIndex].thickness;
                work.hasDensity = m_Surface[defaultIndex].hasDensity;
                work.hasThickness = m_Surface[defaultIndex].hasThickness;
            }
        }

        // The name belongs to this block even when its physics values were
        // inherited from an existing/default material.
        work.name = name.c_str();

        while ( NextToken( p, token ) && token != "}" )
        {
            if ( !NextToken( p, value ) ) break;
            if ( !Q_stricmp( token.c_str(), "base" ) )
            {
                int baseIndex = GetSurfaceIndex( value.c_str() );
                if ( baseIndex >= 0 )
                {
                    work.base = value.c_str();
                    work.density = m_Surface[baseIndex].density;
                    work.thickness = m_Surface[baseIndex].thickness;
                    work.hasDensity = m_Surface[baseIndex].hasDensity;
                    work.hasThickness = m_Surface[baseIndex].hasThickness;
                }
            }
            else if ( !Q_stricmp( token.c_str(), "density" ) )
            {
                work.density = (float)std::atof( value.c_str() );
                work.hasDensity = true;
            }
            else if ( !Q_stricmp( token.c_str(), "thickness" ) )
            {
                work.thickness = (float)std::atof( value.c_str() );
                work.hasThickness = true;
            }
        }

        if ( index >= 0 ) m_Surface[index] = work;
        else m_Surface.AddToTail( work );
    }
}

void CStudioSurfaceProps::GetPhysicsProperties( int index, float *pDensity, float *pThickness,
    float *pFriction, float *pElasticity ) const
{
    // Match CPhysicsSurfaceProps::GetPhysicsProperties(): an unknown material
    // falls back to the "default" surface instead of returning zero density.
    // Returning zero here makes $automass compute a total mass of 0 kg.
    if ( index < 0 || index >= m_Surface.Count() )
        index = GetSurfaceIndex( "default" );

    float density = 0.0f, thickness = 0.0f;
    if ( index >= 0 && index < m_Surface.Count() )
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

//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Common collision utility methods
//
// $Header: $
// $NoKeywords: $
//=============================================================================//

#pragma once

#include "tier0/platform.h"



#include "mathlib/ssemath.h"

//-----------------------------------------------------------------------------
// forward declarations
//-----------------------------------------------------------------------------
struct Ray_t;
class Vector;
class Vector2D;
class Vector4D;
struct cplane_t;
class QAngle;
class CBaseTrace;
struct matrix3x4_t;


//-----------------------------------------------------------------------------
//
// IntersectRayWithTriangle
//
// Intersects a ray with a triangle, returns distance t along ray.
// t will be less than zero if no intersection occurred
// oneSided will cull collisions which approach the triangle from the back
// side, assuming the vertices are specified in counter-clockwise order
// The vertices need not be specified in that order if oneSided is not used
//
//-----------------------------------------------------------------------------
float IntersectRayWithTriangle( const Ray_t& ray, 
		                        const Vector& v1, const Vector& v2, const Vector& v3, 
								bool oneSided );

//-----------------------------------------------------------------------------
//
// ComputeIntersectionBarycentricCoordinates
//
// Figures out the barycentric coordinates (u,v) where a ray hits a 
// triangle. Note that this will ignore the ray extents, and it also ignores
// the ray length. Note that the edge from v1->v2 represents u (v2: u = 1), 
// and the edge from v1->v3 represents v (v3: v = 1). It returns false
// if the ray is parallel to the triangle (or when t is specified if t is less
// than zero).
//
//-----------------------------------------------------------------------------
bool ComputeIntersectionBarycentricCoordinates( const Ray_t& ray, 
		const Vector& v1, const Vector& v2, const Vector& v3, float& u, float& v,
		float *t = nullptr );

//-----------------------------------------------------------------------------
//
// IntersectRayWithRay
//
// Returns whether or not there was an intersection.  The "t" paramter is the
// distance along ray0 and the "s" parameter is the distance along ray1.  If 
// the two lines to not intersect the "t" and "s" represent the closest approach.
// "t" and "s" will not change if the rays are parallel.
//
//-----------------------------------------------------------------------------
bool IntersectRayWithRay( const Ray_t &ray0, const Ray_t &ray1, float &t, float &s );


//-----------------------------------------------------------------------------
//
// IntersectRayWithSphere
//
// Returns whether or not there was an intersection. Returns the two intersection points.
// NOTE: The point of closest approach can be found at the average t value.
//
//-----------------------------------------------------------------------------
bool IntersectRayWithSphere( const Vector &vecRayOrigin, const Vector &vecRayDelta, const Vector &vecSphereCenter, float flRadius, float *pT1, float *pT2 );


//-----------------------------------------------------------------------------
//
// IntersectInfiniteRayWithSphere
//
// Returns whether or not there was an intersection of a sphere against an infinitely
// extending ray. 
// Returns the two intersection points
//
//-----------------------------------------------------------------------------
bool IntersectInfiniteRayWithSphere( const Vector &vecRayOrigin, const Vector &vecRayDelta, 
	const Vector &vecSphereCenter, float flRadius, float *pT1, float *pT2 );


//-----------------------------------------------------------------------------
//
// IntersectRayWithPlane
//
// Intersects a ray with a plane, returns distance t along ray.
// t will be less than zero the intersection occurs in the opposite direction of the ray.
//
//-----------------------------------------------------------------------------
float IntersectRayWithPlane( const Ray_t& ray, const cplane_t& plane ); 
float IntersectRayWithPlane( const Vector& org, const Vector& dir, const cplane_t& plane );
float IntersectRayWithPlane( const Vector& org, const Vector& dir, const Vector& normal, float dist );


//-----------------------------------------------------------------------------
// IntersectRayWithBox
//
// Purpose: Computes the intersection of a ray with a box (AABB)
// Output : Returns true if there is an intersection + trace information
//-----------------------------------------------------------------------------
bool IntersectRayWithBox( const Vector &rayStart, const Vector &rayDelta, const Vector &boxMins, const Vector &boxMaxs, float epsilon, CBaseTrace *pTrace, float *pFractionLeftSolid = nullptr);
bool IntersectRayWithBox( const Ray_t &ray, const Vector &boxMins, const Vector &boxMaxs, float epsilon, CBaseTrace *pTrace, float *pFractionLeftSolid = nullptr);

//-----------------------------------------------------------------------------
// Intersects a ray against a box
//-----------------------------------------------------------------------------
struct BoxTraceInfo_t
{
	float t1;
	float t2;
	int	hitside;
	bool startsolid;
};

bool IntersectRayWithBox( const Vector &vecRayStart, const Vector &vecRayDelta, 
	const Vector &boxMins, const Vector &boxMaxs, float flTolerance, BoxTraceInfo_t *pTrace );


//-----------------------------------------------------------------------------
// IntersectRayWithOBB
//
// Purpose: Computes the intersection of a ray with a oriented box (OBB)
// Output : Returns true if there is an intersection + trace information
//-----------------------------------------------------------------------------
bool IntersectRayWithOBB( const Vector &vecRayStart, const Vector &vecRayDelta, 
	const matrix3x4_t &matOBBToWorld, const Vector &vecOBBMins, const Vector &vecOBBMaxs, 
	float flTolerance, CBaseTrace *pTrace );

bool IntersectRayWithOBB( const Vector &vecRayOrigin, const Vector &vecRayDelta, 
	const Vector &vecBoxOrigin, const QAngle &angBoxRotation,
	const Vector &vecOBBMins, const Vector &vecOBBMaxs, float flTolerance, CBaseTrace *pTrace );

bool IntersectRayWithOBB( const Ray_t &ray, const Vector &vecBoxOrigin, const QAngle &angBoxRotation,
	const Vector &vecOBBMins, const Vector &vecOBBMaxs, float flTolerance, CBaseTrace *pTrace );

bool IntersectRayWithOBB( const Ray_t &ray, const matrix3x4_t &matOBBToWorld,
	const Vector &vecOBBMins, const Vector &vecOBBMaxs, float flTolerance, CBaseTrace *pTrace );

bool IntersectRayWithOBB( const Vector &vecRayStart, const Vector &vecRayDelta, 
	const matrix3x4_t &matOBBToWorld, const Vector &vecOBBMins, const Vector &vecOBBMaxs, 
	float flTolerance, BoxTraceInfo_t *pTrace );

//-----------------------------------------------------------------------------
// 
// IsSphereIntersectingSphere
//
// returns true if there's an intersection between sphere and sphere
//
//-----------------------------------------------------------------------------
bool IsSphereIntersectingSphere( const Vector& center1, float radius1, 
								 const Vector& center2, float radius2 );


//-----------------------------------------------------------------------------
// 
// IsBoxIntersectingSphere
//
// returns true if there's an intersection between box and sphere
//
//-----------------------------------------------------------------------------
bool IsBoxIntersectingSphere( const Vector& boxMin, const Vector& boxMax, 
						      const Vector& center, float radius );


//-----------------------------------------------------------------------------
// Returns true if a box intersects with a sphere
// NOTE: f4RadiusSq must have the radius sq in all components
//-----------------------------------------------------------------------------
FORCEINLINE bool IsBoxIntersectingSphere( const Vector& boxMin, const Vector& boxMax, 
											 const fltx4& f4Center, const fltx4& f4RadiusSq )
{
	// See Graphics Gems, box-sphere intersection
	fltx4 f4Mins = LoadUnalignedSIMD( &boxMin.x );
	fltx4 f4Maxs = LoadUnalignedSIMD( &boxMax.x );
	fltx4 f4MinDelta = SubSIMD( f4Mins, f4Center );
	fltx4 f4MaxDelta = SubSIMD( f4Center, f4Maxs );
	f4MinDelta = MaxSIMD( f4MinDelta, Four_Zeros );
	f4MaxDelta = MaxSIMD( f4MaxDelta, Four_Zeros );
	fltx4 f4Delta = AddSIMD( f4MinDelta, f4MaxDelta );
	fltx4 f4DistSq = Dot3SIMD( f4Delta, f4Delta );
	return IsAllGreaterThan( f4RadiusSq, f4DistSq );
}



//-----------------------------------------------------------------------------
// 
// IsCircleIntersectingRectangle
//
// returns true if there's an intersection between rectangle and circle
//
//-----------------------------------------------------------------------------
bool IsCircleIntersectingRectangle( const Vector2D& boxMin, const Vector2D& boxMax, 
						      const Vector2D& center, float radius );


//-----------------------------------------------------------------------------
// 
// IsBoxIntersectingBox
//
// returns true if there's an intersection between two boxes
//
//-----------------------------------------------------------------------------
bool IsBoxIntersectingBox( const Vector& boxMin1, const Vector& boxMax1, 
						   const Vector& boxMin2, const Vector& boxMax2 );



//-----------------------------------------------------------------------------
// 
// IsOBBIntersectingOBB
//
// returns true if there's an intersection between two OBBs
//
//-----------------------------------------------------------------------------
bool IsOBBIntersectingOBB( const Vector &vecOrigin1, const QAngle &vecAngles1, const Vector& boxMin1, const Vector& boxMax1, 
						   const Vector &vecOrigin2, const QAngle &vecAngles2, const Vector& boxMin2, const Vector& boxMax2, float flTolerance = 0.0f );


//-----------------------------------------------------------------------------
// 
// IsBoxIntersectingRay
//
// returns true if there's an intersection between box and ray
//
//-----------------------------------------------------------------------------

bool FASTCALL IsBoxIntersectingRay( const Vector& boxMin, const Vector& boxMax, 
									const Vector& origin, const Vector& delta, float flTolerance = 0.0f );

bool FASTCALL IsBoxIntersectingRay( const Vector& boxMin, const Vector& boxMax, 
									const Ray_t& ray, float flTolerance );

bool FASTCALL IsBoxIntersectingRay( const Vector& boxMin, const Vector& boxMax, 
									const Vector& origin, const Vector& delta,
									const Vector& invDelta, float flTolerance = 0.0f );


// On the PC, we can't pass fltx4's in registers like this. On the x360, it is 
// much better if we do.
bool FASTCALL IsBoxIntersectingRay( const fltx4 &boxMin, const fltx4 &boxMax, 
								   const fltx4 & origin, const fltx4 & delta, const fltx4 & invDelta, // ray parameters
								   const fltx4 & vTolerance = Four_Zeros ///< eg from ReplicateX4(flTolerance)
								   );

bool inline FASTCALL IsBoxIntersectingRay( const fltx4& boxMin, const fltx4& boxMax, 
								   const fltx4& origin, const fltx4& delta, float flTolerance = 0.0f )
{
	return IsBoxIntersectingRay( boxMin, boxMax, origin, delta, ReciprocalSIMD(delta), ReplicateX4(flTolerance) );
}


bool FASTCALL IsBoxIntersectingRay( const fltx4& boxMin, const fltx4& boxMax, 
								   const Ray_t& ray, const fltx4 &fl4Tolerance = LoadZeroSIMD() );

FORCEINLINE bool IsBoxIntersectingRay( const Vector& boxMin, const Vector& boxMax, const Ray_t& ray )
{
	return IsBoxIntersectingRay( LoadUnaligned3SIMD(boxMin.Base()), LoadUnaligned3SIMD(boxMax.Base()), ray, LoadZeroSIMD() );
}


//-----------------------------------------------------------------------------
// 
// IsPointInBox
//
// returns true if the point is in the box
//
//-----------------------------------------------------------------------------
bool IsPointInBox( const Vector& pt, const Vector& boxMin, const Vector& boxMax );


// SIMD version
FORCEINLINE bool IsPointInBox( const fltx4& pt, const fltx4& boxMin, const fltx4& boxMax )
{
	bi32x4 greater = CmpGtSIMD( pt,boxMax );
	bi32x4 less = CmpLtSIMD( pt, boxMin );
	return (IsAllZeros(SetWToZeroSIMD(OrSIMD(greater,less))));
}



//-----------------------------------------------------------------------------
//
// PointInQuadBarycentric
//
//	Given a point and a quad in a plane return the u and v (barycentric) positions
//  of the point relative to the quad.  The points (v1,v2,v3,v4) should be given
//  in a counter-clockwise order with v1 acting as the primary corner (u=0, v=0).
//  Thus, u0 = v2 - v1, and v0 = v4 - v1.
//
//-----------------------------------------------------------------------------

enum QuadBarycentricRetval_t
{
	BARY_QUADRATIC_FALSE					= 0,
	BARY_QUADRATIC_TRUE						= 1,
	BARY_QUADRATIC_NEGATIVE_DISCRIMINANT	= 2
};




//-----------------------------------------------------------------------------
// Compute point from barycentric specification
// Edge u goes from v0 to v1, edge v goes from v0 to v2
//-----------------------------------------------------------------------------
void ComputePointFromBarycentric( const Vector& v0, const Vector& v1, const Vector& v2, 
								 float u, float v, Vector& pt );
void ComputePointFromBarycentric( const Vector2D& v0, const Vector2D& v1, const Vector2D& v2, 
								 float u, float v, Vector2D& pt );



//-----------------------------------------------------------------------------
// IsBoxIntersectingTriangle
//
// Test for an intersection (overlap) between an axial-aligned bounding 
// box (AABB) and a triangle.
//
// Triangle points are in counter-clockwise order with the normal facing "out."
//
// Using the "Separating-Axis Theorem" to test for intersections between
// a triangle and an axial-aligned bounding box (AABB).
// 1. 3 Axis Plane Tests - x, y, z
// 2. 9 Edge Planes Tests - the 3 edges of the triangle crossed with all 3 axial 
//                          planes (x, y, z)
// 3. 1 Face Plane Test - the plane the triangle resides in (cplane_t plane)
//-----------------------------------------------------------------------------
bool IsBoxIntersectingTriangle( const Vector &vecBoxCenter, const Vector &vecBoxExtents,
				   		        const Vector &v1, const Vector &v2, const Vector &v3,
						        const cplane_t &plane, float flTolerance );







//-----------------------------------------------------------------------------
// INLINES
//-----------------------------------------------------------------------------





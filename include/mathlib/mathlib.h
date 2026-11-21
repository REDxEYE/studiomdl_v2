//===== Copyright  1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#pragma once

#include <math.h>
#include <cmath>
#include "tier0/basetypes.h"
#include "mathlib/vector.h"
#include "mathlib/vector2d.h"

#include "mathlib/math_pfns.h"
#include "mathlib/fltx4.h"

#ifndef ALIGN8_POST
#define ALIGN8_POST
#endif



// plane_t structure
// !!! if this is changed, it must be changed in asm code too !!!
// FIXME: does the asm code even exist anymore?
// FIXME: this should move to a different file
struct cplane_t
{
	Vector	normal;
	float	dist;
	uint8_t	type;			// for fast side tests
	uint8_t	signbits;		// signx + (signy<<1) + (signz<<1)
	uint8_t	pad[2];

#ifdef VECTOR_NO_SLOW_OPERATIONS
	cplane_t() {}

private:
	// No copy constructors allowed if we're in optimal mode
	cplane_t(const cplane_t& vOther);
#endif
};

// structure offset for asm code
#define CPLANE_NORMAL_X			0
#define CPLANE_NORMAL_Y			4
#define CPLANE_NORMAL_Z			8
#define CPLANE_DIST				12
#define CPLANE_TYPE				16
#define CPLANE_SIGNBITS			17
#define CPLANE_PAD0				18
#define CPLANE_PAD1				19

// 0-2 are axial planes
#define	PLANE_X			0
#define	PLANE_Y			1
#define	PLANE_Z			2

// 3-5 are non-axial planes snapped to the nearest
#define	PLANE_ANYX		3
#define	PLANE_ANYY		4
#define	PLANE_ANYZ		5


//-----------------------------------------------------------------------------
// Frustum plane indices.
// WARNING: there is code that depends on these values
//-----------------------------------------------------------------------------

enum
{
	FRUSTUM_RIGHT		= 0,
	FRUSTUM_LEFT		= 1,
	FRUSTUM_TOP			= 2,
	FRUSTUM_BOTTOM		= 3,
	FRUSTUM_NEARZ		= 4,
	FRUSTUM_FARZ		= 5,
	FRUSTUM_NUMPLANES	= 6
};

// Computes Y fov from an X fov and a screen aspect ratio + X from Y

// Generate a frustum based on perspective view parameters
// NOTE: FOV is specified in degrees, as the *full* view angle (not half-angle)
class VPlane;

class CTransform;
class matrix3x4a_t;

struct matrix3x4_t
{
	matrix3x4_t() {}
	matrix3x4_t( 
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23 )
	{
		m_flMatVal[0][0] = m00;	m_flMatVal[0][1] = m01; m_flMatVal[0][2] = m02; m_flMatVal[0][3] = m03;
		m_flMatVal[1][0] = m10;	m_flMatVal[1][1] = m11; m_flMatVal[1][2] = m12; m_flMatVal[1][3] = m13;
		m_flMatVal[2][0] = m20;	m_flMatVal[2][1] = m21; m_flMatVal[2][2] = m22; m_flMatVal[2][3] = m23;
	}

	/// Creates a matrix where the X axis = forward the Y axis = left, and the Z axis = up
	void InitXYZ( const Vector& xAxis, const Vector& yAxis, const Vector& zAxis, const Vector &vecOrigin )
	{
		m_flMatVal[ 0 ][ 0 ] = xAxis.x; m_flMatVal[ 0 ][ 1 ] = yAxis.x; m_flMatVal[ 0 ][ 2 ] = zAxis.x; m_flMatVal[ 0 ][ 3 ] = vecOrigin.x;
		m_flMatVal[ 1 ][ 0 ] = xAxis.y; m_flMatVal[ 1 ][ 1 ] = yAxis.y; m_flMatVal[ 1 ][ 2 ] = zAxis.y; m_flMatVal[ 1 ][ 3 ] = vecOrigin.y;
		m_flMatVal[ 2 ][ 0 ] = xAxis.z; m_flMatVal[ 2 ][ 1 ] = yAxis.z; m_flMatVal[ 2 ][ 2 ] = zAxis.z; m_flMatVal[ 2 ][ 3 ] = vecOrigin.z;
	}

	//-----------------------------------------------------------------------------
	// Creates a matrix where the X axis = forward
	// the Y axis = left, and the Z axis = up
	//-----------------------------------------------------------------------------
	void Init( const Vector& xAxis, const Vector& yAxis, const Vector& zAxis, const Vector &vecOrigin )
	{
		m_flMatVal[0][0] = xAxis.x; m_flMatVal[0][1] = yAxis.x; m_flMatVal[0][2] = zAxis.x; m_flMatVal[0][3] = vecOrigin.x;
		m_flMatVal[1][0] = xAxis.y; m_flMatVal[1][1] = yAxis.y; m_flMatVal[1][2] = zAxis.y; m_flMatVal[1][3] = vecOrigin.y;
		m_flMatVal[2][0] = xAxis.z; m_flMatVal[2][1] = yAxis.z; m_flMatVal[2][2] = zAxis.z; m_flMatVal[2][3] = vecOrigin.z;
	}

	//-----------------------------------------------------------------------------
	// Creates a matrix where the X axis = forward
	// the Y axis = left, and the Z axis = up
	//-----------------------------------------------------------------------------
	matrix3x4_t( const Vector& xAxis, const Vector& yAxis, const Vector& zAxis, const Vector &vecOrigin )
	{
		Init( xAxis, yAxis, zAxis, vecOrigin );
	}

	inline void InitFromQAngles( const QAngle &angles, const Vector &vPosition );
	inline void InitFromQAngles( const QAngle &angles );
	inline void InitFromRadianEuler( const RadianEuler &angles, const Vector &vPosition );
	inline void InitFromRadianEuler( const RadianEuler &angles );
	inline void InitFromQuaternion( const Quaternion &orientation, const Vector &vPosition );
	inline void InitFromQuaternion( const Quaternion &orientation );
	inline void InitFromDiagonal( const Vector &vDiagonal );

	inline Quaternion ToQuaternion() const;
	inline QAngle ToQAngle() const;

	inline void SetToIdentity();

	/// multiply the scale/rot part of the matrix by a constant. This doesn't init the matrix ,
	/// just scale in place. So if you want to construct a scaling matrix, init to identity and
	/// then call this.
	FORCEINLINE void ScaleUpper3x3Matrix( float flScale );

	/// modify the origin
	inline void SetOrigin( Vector const & p )
	{
		m_flMatVal[0][3] = p.x;
		m_flMatVal[1][3] = p.y;
		m_flMatVal[2][3] = p.z;
	}

	/// return the origin
	inline Vector GetOrigin( void ) const
	{
		Vector vecRet( m_flMatVal[ 0 ][ 3 ], m_flMatVal[ 1 ][ 3 ], m_flMatVal[ 2 ][ 3 ] );
		return vecRet;
	}

	inline void Invalidate( void )
	{
		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				m_flMatVal[i][j] = VEC_T_NAN;
			}
		}
	}

		/// check all components for invalid floating point values
	inline bool IsValid( void ) const
	{
		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				if ( !IsFinite( m_flMatVal[i][j] ) )
					return false;
			}
		}
		return true;
	}

	bool operator==( const matrix3x4_t &other ) const
	{
		return memcmp( this, &other, sizeof(matrix3x4_t) ) == 0;
	}

	bool operator!=( const matrix3x4_t &other ) const
	{
		return memcmp( this, &other, sizeof(matrix3x4_t) ) != 0;
	}

	inline bool IsEqualTo( const matrix3x4_t &other, float flTolerance = 1e-5f ) const;

	inline void GetBasisVectorsFLU( Vector *pForward, Vector *pLeft, Vector *pUp ) const;
	inline Vector TransformVector( const Vector &v0 ) const;
	inline Vector RotateVector( const Vector &v0 ) const;
	inline Vector TransformVectorByInverse( const Vector &v0 ) const;
	inline Vector RotateVectorByInverse( const Vector &v0 ) const;
	inline Vector RotateExtents( const Vector &vBoxExtents ) const; // these are extents and must remain positive/symmetric after rotation
	inline void TransformAABB( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const;
	inline void TransformAABBByInverse( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const;
	inline void RotateAABB( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const;
	inline void RotateAABBByInverse( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const;
	inline void TransformPlane( const cplane_t &inPlane, cplane_t &outPlane ) const;
	inline void TransformPlaneByInverse( const cplane_t &inPlane, cplane_t &outPlane ) const;
	inline float GetOrthogonalityError() const;
	inline float GetDeterminant( )const;
	inline float GetSylvestersCriterion()const; // for symmetrical matrices only: should be >0 iff it's a positive definite matrix

	inline Vector GetColumn( MatrixAxisType_t nColumn ) const;
	inline void SetColumn( const Vector &vColumn, MatrixAxisType_t nColumn );
	inline Vector GetForward() const { return GetColumn( FORWARD_AXIS ); }
	inline Vector GetLeft() const { return GetColumn( LEFT_AXIS ); }
	inline Vector GetUp() const { return GetColumn( UP_AXIS ); }
	inline Vector GetRow( int nRow ) const { return *(Vector *)(m_flMatVal[nRow]); }
	inline void SetRow( int nRow, const Vector &vRow ) { m_flMatVal[nRow][0] = vRow.x; m_flMatVal[nRow][1] = vRow.y; m_flMatVal[nRow][2] = vRow.z; }

	inline void InverseTR( matrix3x4_t &out ) const;
	inline matrix3x4_t InverseTR() const;


	float *operator[]( int i )				{ Assert(( i >= 0 ) && ( i < 3 )); return m_flMatVal[i]; }
	const float *operator[]( int i ) const	{ Assert(( i >= 0 ) && ( i < 3 )); return m_flMatVal[i]; }
	float *Base()							{ return &m_flMatVal[0][0]; }
	const float *Base() const				{ return &m_flMatVal[0][0]; }

	float m_flMatVal[3][4];
};

class ALIGN16 matrix3x4a_t : public matrix3x4_t
{
public:
	/*
	matrix3x4a_t() { if (((size_t)Base()) % 16 != 0) { Error( "matrix3x4a_t missaligned" ); } }
	*/
	matrix3x4a_t( const matrix3x4_t& src ) { *this = src; };
	matrix3x4a_t& operator=( const matrix3x4_t& src ) { memcpy( Base(), src.Base(), sizeof( float ) * 3 * 4 ); return *this; };

	matrix3x4a_t(
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23 )
	{
		AssertDbg( ( ( size_t )Base() & 0xf ) == 0 );
		m_flMatVal[ 0 ][ 0 ] = m00;	m_flMatVal[ 0 ][ 1 ] = m01; m_flMatVal[ 0 ][ 2 ] = m02; m_flMatVal[ 0 ][ 3 ] = m03;
		m_flMatVal[ 1 ][ 0 ] = m10;	m_flMatVal[ 1 ][ 1 ] = m11; m_flMatVal[ 1 ][ 2 ] = m12; m_flMatVal[ 1 ][ 3 ] = m13;
		m_flMatVal[ 2 ][ 0 ] = m20;	m_flMatVal[ 2 ][ 1 ] = m21; m_flMatVal[ 2 ][ 2 ] = m22; m_flMatVal[ 2 ][ 3 ] = m23;
	}
	matrix3x4a_t(){}

	static FORCEINLINE bool TypeIsAlignedForSIMD( void ) { return true; }


	// raw data simd accessor
	FORCEINLINE fltx4 &SIMDRow( uint32_t nIdx ) { AssertDbg( nIdx < 3 ); return *( ( fltx4 * )( &( m_flMatVal[ nIdx ] ) ) ); }
	FORCEINLINE const fltx4 &SIMDRow( uint32_t nIdx ) const { AssertDbg( nIdx < 3 ); return *( ( const fltx4 * )( &( m_flMatVal[ nIdx ] ) ) ); }

} ALIGN16_POST;


FORCEINLINE void matrix3x4_t::ScaleUpper3x3Matrix( float flScale )
{
	for ( int i = 0; i < 3; i++ )
	{
		for ( int j = 0; j < 3; j++ )
		{
			m_flMatVal[ i ][ j ] *= flScale;
		}
	}
}


#ifndef M_PI
	#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

#ifndef M_PI_F
	#define M_PI_F		((float)(M_PI))
#endif

// NJS: Inlined to prevent floats from being autopromoted to doubles, as with the old system.
#ifndef RAD2DEG
	#define RAD2DEG( x  )  ( (float)(x) * (float)(180.f / M_PI_F) )
#endif

#ifndef DEG2RAD
	#define DEG2RAD( x  )  ( (float)(x) * (float)(M_PI_F / 180.f) )
#endif

// Used to represent sides of things like planes.
#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2
#define SIDE_CROSS  -2      // necessary for polylib.c

// Use different side values (1, 2, 4) instead of (0, 1, 2) so we can '|' and '&' them, and quickly determine overall clipping
// without having to maintain counters and read / write memory.
enum Sides
{
	OR_SIDE_FRONT	=	1,
	OR_SIDE_BACK	=	2,
	OR_SIDE_ON		=	4,
};

#define ON_VIS_EPSILON  0.01    // necessary for vvis (flow.c) -- again look into moving later!
#define	EQUAL_EPSILON	0.001   // necessary for vbsp (faces.c) -- should look into moving it there?

extern bool s_bMathlibInitialized;

extern const matrix3x4a_t g_MatrixIdentity;
extern  const Vector vec3_origin;
extern  const QAngle vec3_angle;
extern	const Quaternion quat_identity;
extern const Vector vec3_invalid;
extern	const int nanmask;

#define	IS_NAN(x) (((*(int *)&x)&nanmask)==nanmask)

FORCEINLINE vec_t DotProduct(const vec_t *v1, const vec_t *v2)
{
	return v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2];
}
FORCEINLINE void VectorSubtract(const vec_t *a, const vec_t *b, vec_t *c)
{
	c[0]=a[0]-b[0];
	c[1]=a[1]-b[1];
	c[2]=a[2]-b[2];
}
FORCEINLINE void VectorAdd(const vec_t *a, const vec_t *b, vec_t *c)
{
	c[0]=a[0]+b[0];
	c[1]=a[1]+b[1];
	c[2]=a[2]+b[2];
}
FORCEINLINE void VectorCopy(const vec_t *a, vec_t *b)
{
	b[0]=a[0];
	b[1]=a[1];
	b[2]=a[2];
}
FORCEINLINE void VectorClear(vec_t *a)
{
	a[0]=a[1]=a[2]=0;
}

FORCEINLINE float VectorMaximum(const vec_t *v)
{
	return MAX( v[0], MAX( v[1], v[2] ) );
}

FORCEINLINE float VectorMaximum(const Vector& v)
{
	return MAX( v.x, MAX( v.y, v.z ) );
}

FORCEINLINE void VectorScale (const float* in, vec_t scale, float* out)
{
	out[0] = in[0]*scale;
	out[1] = in[1]*scale;
	out[2] = in[2]*scale;
}


// Cannot be forceinline as they have overloads:
inline void VectorFill(vec_t *a, float b)
{
	a[0]=a[1]=a[2]=b;
}

inline void VectorNegate(vec_t *a)
{
	a[0]=-a[0];
	a[1]=-a[1];
	a[2]=-a[2];
}


//#define VectorMaximum(a)		( max( (a)[0], max( (a)[1], (a)[2] ) ) )
#define Vector2Clear(x)			{(x)[0]=(x)[1]=0;}
#define Vector2Negate(x)		{(x)[0]=-((x)[0]);(x)[1]=-((x)[1]);}
#define Vector2Copy(a,b)		{(b)[0]=(a)[0];(b)[1]=(a)[1];}
#define Vector2Subtract(a,b,c)	{(c)[0]=(a)[0]-(b)[0];(c)[1]=(a)[1]-(b)[1];}
#define Vector2Add(a,b,c)		{(c)[0]=(a)[0]+(b)[0];(c)[1]=(a)[1]+(b)[1];}
#define Vector2Scale(a,b,c)		{(c)[0]=(b)*(a)[0];(c)[1]=(b)*(a)[1];}

// NJS: Some functions in VBSP still need to use these for dealing with mixing vec4's and shorts with vec_t's.
// remove when no longer needed.
#define VECTOR_COPY( A, B ) do { (B)[0] = (A)[0]; (B)[1] = (A)[1]; (B)[2]=(A)[2]; } while(0)
#define DOT_PRODUCT( A, B ) ( (A)[0]*(B)[0] + (A)[1]*(B)[1] + (A)[2]*(B)[2] )

FORCEINLINE void VectorMAInline( const float* start, float scale, const float* direction, float* dest )
{
	dest[0]=start[0]+direction[0]*scale;
	dest[1]=start[1]+direction[1]*scale;
	dest[2]=start[2]+direction[2]*scale;
}

FORCEINLINE void VectorMAInline( const Vector& start, float scale, const Vector& direction, Vector& dest )
{
	dest.x=start.x+direction.x*scale;
	dest.y=start.y+direction.y*scale;
	dest.z=start.z+direction.z*scale;
}

FORCEINLINE void VectorMA( const Vector& start, float scale, const Vector& direction, Vector& dest )
{
	VectorMAInline(start, scale, direction, dest);
}

FORCEINLINE void VectorMA( const float * start, float scale, const float *direction, float *dest )
{
	VectorMAInline(start, scale, direction, dest);
}


inline float VectorLength(const float *v)
{
	return FastSqrt( v[0]*v[0] + v[1]*v[1] + v[2]*v[2] + FLT_EPSILON );
}

void CrossProduct (const float *v1, const float *v2, float *cross);

inline float CrossProductX( const Vector & v1, const Vector& v2 )
{
	return v1.y * v2.z - v1.z * v2.y;
}

inline float CrossProductY( const Vector & v1, const Vector& v2 )
{
	return v1.z * v2.x - v1.x * v2.z;
}

inline float CrossProductZ( const Vector & v1, const Vector& v2 )
{
	return v1.x * v2.y - v1.y * v2.x;
}

inline vec_t RoundInt (vec_t in)
{
	return floor(in + 0.5f);
}

size_t Q_log2( unsigned int val );

// Sine and cosine of an angle in radians
inline void SinCos( float radians, float * RESTRICT sine, float * RESTRICT cosine )
{
	// double precision, then round to float: same results as the former x87 fsincos (extended -> double -> float)
	*sine = static_cast<float>( sin( static_cast<double>( radians ) ) );
	*cosine = static_cast<float>( cos( static_cast<double>( radians ) ) );
}

#define SIN_TABLE_SIZE	256
#define FTOIBIAS		12582912.f
extern float SinCosTable[SIN_TABLE_SIZE];

inline float TableCos( float theta )
{
#if defined( LINUX )
	return cos(theta); // under the GCC compiler the float-represented-as-an-int causes an internal compiler error
#else

	union
	{
		int i;
		float f;
	} ftmp;

	// ideally, the following should compile down to: theta * constant + constant, changing any of these constants from defines sometimes fubars this.
	ftmp.f = theta * ( float )( SIN_TABLE_SIZE / ( 2.0f * M_PI ) ) + ( FTOIBIAS + ( SIN_TABLE_SIZE / 4 ) );
	return SinCosTable[ ftmp.i & ( SIN_TABLE_SIZE - 1 ) ];
#endif
}

inline float TableSin( float theta )
{
#if defined( LINUX )
	return sin(theta); // under the GCC compiler the float-represented-as-an-int causes an internal compiler error
#else
	union
	{
		int i;
		float f;
	} ftmp;

	// ideally, the following should compile down to: theta * constant + constant
	ftmp.f = theta * ( float )( SIN_TABLE_SIZE / ( 2.0f * M_PI ) ) + FTOIBIAS;
	return SinCosTable[ ftmp.i & ( SIN_TABLE_SIZE - 1 ) ];
#endif
}

template<class T>
FORCEINLINE T Square( T const &a )
{
	return a * a;
}

FORCEINLINE bool IsPowerOfTwo( uint32_t x )
{
	return ( x & ( x - 1 ) ) == 0;
}

// return the smallest power of two >= x.
// returns 0 if x == 0 or x > 0x80000000 (ie numbers that would be negative if x was signed)
// NOTE: the old code took an int, and if you pass in an int of 0x80000000 casted to a uint,
//       you'll get 0x80000000, which is correct for uints, instead of 0, which was correct for ints
FORCEINLINE uint32_t SmallestPowerOfTwoGreaterOrEqual( uint32_t x )
{
	x -= 1;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return x + 1;
}

// return the largest power of two <= x. Will return 0 if passed 0
FORCEINLINE uint32_t LargestPowerOfTwoLessThanOrEqual( uint32_t x )
{
	if ( x >= 0x80000000 )
		return 0x80000000;

	return SmallestPowerOfTwoGreaterOrEqual( x + 1 ) >> 1;
}


// Math routines for optimizing division

// MOVEMENT INFO
enum
{
	PITCH = 0,	// up / down
	YAW,		// left / right
	ROLL		// fall over
};

void MatrixVectorsFLU( const matrix3x4_t &matrix, Vector* pForward, Vector *pLeft, Vector *pUp );
void MatrixAngles( const matrix3x4_t & matrix, float *angles ); // !!!!
void VectorTransform (const float *in1, const matrix3x4_t & in2, float *out);
void VectorITransform (const float *in1, const matrix3x4_t & in2, float *out);
void VectorRotate( const float *in1, const matrix3x4_t & in2, float *out);
void VectorRotate( const Vector &in1, const QAngle &in2, Vector &out );
void VectorRotate( const Vector &in1, const Quaternion &in2, Vector &out );
void VectorIRotate( const float *in1, const matrix3x4_t & in2, float *out);

inline const Vector VectorRotate( const Vector &vIn1, const Quaternion &qIn2 )
{
	Vector out;
	VectorRotate( vIn1, qIn2, out );
	return out;
}


#ifndef VECTOR_NO_SLOW_OPERATIONS


#endif

void MatrixCopy( const matrix3x4_t &in, matrix3x4_t &out );
void MatrixInvert( const matrix3x4_t &in, matrix3x4_t &out );

// Matrix equality test
bool MatricesAreEqual( const matrix3x4_t &src1, const matrix3x4_t &src2, float flTolerance = 1e-5 );

void MatrixGetColumn( const matrix3x4_t &in, int column, Vector &out );
void MatrixSetColumn( const Vector &in, int column, matrix3x4_t &out );

//void DecomposeRotation( const matrix3x4_t &mat, float *out );
void ConcatTransforms (const matrix3x4_t &in1, const matrix3x4_t &in2, matrix3x4_t &out);
// faster version assumes m0, m1, out are 16-byte aligned addresses
void ConcatTransforms_Aligned( const matrix3x4a_t &m0, const matrix3x4a_t &m1, matrix3x4a_t &out );

// For identical interface w/ VMatrix
inline void MatrixMultiply ( const matrix3x4_t &in1, const matrix3x4_t &in2, matrix3x4_t &out )
{
	ConcatTransforms( in1, in2, out );
}

void QuaternionExp( const Quaternion &p, Quaternion &q );
void QuaternionLn( const Quaternion &p, Quaternion &q );
void QuaternionAverageExponential( Quaternion &q, int nCount, const Quaternion *pQuaternions, const float *pflWeights = nullptr);
void QuaternionSlerp( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt );
void QuaternionSlerpNoAlign( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt );
void QuaternionBlend( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt );
void QuaternionBlendNoAlign( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt );
float QuaternionAngleDiff( const Quaternion &p, const Quaternion &q );
void QuaternionScale( const Quaternion &p, float t, Quaternion &q );
void QuaternionAlign( const Quaternion &p, const Quaternion &q, Quaternion &qt );
float QuaternionDotProduct( const Quaternion &p, const Quaternion &q );
void QuaternionConjugate( const Quaternion &p, Quaternion &q );
void QuaternionInvert( const Quaternion &p, Quaternion &q );
float QuaternionNormalize( Quaternion &q );
void QuaternionAdd( const Quaternion &p, const Quaternion &q, Quaternion &qt );
void QuaternionMult( const Quaternion &p, const Quaternion &q, Quaternion &qt );
void QuaternionMatrix( const Quaternion &q, matrix3x4_t &matrix );
void QuaternionMatrix( const Quaternion &q, const Vector &pos, matrix3x4_t &matrix );
void QuaternionMatrix( const Quaternion &q, const Vector &pos, const Vector &vScale, matrix3x4_t& mat );
void QuaternionAngles( const Quaternion &q, QAngle &angles );
void AngleQuaternion( const QAngle& angles, Quaternion &qt );
void QuaternionAngles( const Quaternion &q, RadianEuler &angles );
void AngleQuaternion( RadianEuler const &angles, Quaternion &qt );
void AxisAngleQuaternion( const Vector &axis, float angle, Quaternion &q );
void BasisToQuaternion( const Vector &vecForward, const Vector &vecRight, const Vector &vecUp, Quaternion &q );
void MatrixQuaternion( const matrix3x4_t &mat, Quaternion &q );


void MatrixQuaternionFast( const matrix3x4_t &mat, Quaternion &q );
void MatrixPosition( const matrix3x4_t &matrix, Vector &position );

inline void MatrixQuaternion( const matrix3x4_t &mat, Quaternion &q, Vector &o )
{
	MatrixQuaternion( mat, q );
	MatrixPosition( mat, o );
}




/// qt = p + s * q
void QuaternionAccumulate( const Quaternion &p, float s, const Quaternion &q, Quaternion &qt );

/// qt = ( s * p ) * q
void QuaternionSM( float s, const Quaternion &p, const Quaternion &q, Quaternion &qt );

/// qt = p * ( s * q )
void QuaternionMA( const Quaternion &p, float s, const Quaternion &q, Quaternion &qt );

/*
//-----------------------------------------------------------------------------
// Quaternion equality with tolerance
//-----------------------------------------------------------------------------
inline bool QuaternionsAreEqualInternal( const Quaternion& src1, const Quaternion& src2, float flTolerance )
{
	if ( !FloatsAreEqual( src1.x, src2.x, flTolerance ) )
		return false;

	if ( !FloatsAreEqual( src1.y, src2.y, flTolerance ) )
		return false;

	if ( !FloatsAreEqual( src1.z, src2.z, flTolerance ) )
		return false;

	return FloatsAreEqual( src1.w, src2.w, flTolerance );
}

inline bool QuaternionsAreEqual( const Quaternion& src1, const Quaternion& src2, float flTolerance )
{
	if ( QuaternionsAreEqualInternal( src1, src2, flTolerance ) )
		return true;

	// negated quaternions are also 'equal'
	Quaternion src2neg( -src2.x, -src2.y, -src2.z, -src2.w );
	return QuaternionsAreEqualInternal( src1, src2neg, flTolerance );
}
*/
inline const Quaternion GetNormalized( const Quaternion & q )
{
	float flInv = 1.0f / sqrtf( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
	return Quaternion( q.x * flInv, q.y * flInv, q.z * flInv, q.w * flInv );
}

inline const Quaternion AngleQuaternion( const QAngle& angles )
{
	Quaternion qt;
	AngleQuaternion( angles, qt );
	return qt;
}


inline const Quaternion AngleQuaternion( RadianEuler const &angles )
{
	Quaternion qt;
	AngleQuaternion( angles, qt );
	return qt;
}



inline Quaternion QuaternionFromPitchYawRoll( float flPitch, float flYaw, float flRoll )
{
	QAngle ang( flPitch, flYaw, flRoll );

	Quaternion q;
	AngleQuaternion( ang, q );
	return q;
}

inline Quaternion QuaternionAddPitch( const Quaternion &q, float flPitch )
{
	// FIXME: I know this can be made *tons* faster, but I just want to get something working quickly
	// that matches being able to add to the pitch of a QAngles so I can expose Quats to script/game code
	QAngle ang;
	QuaternionAngles( q, ang );
	ang[ PITCH ] += flPitch;

	Quaternion res;
	AngleQuaternion( ang, res );
	return res;
}

inline Quaternion QuaternionAddYaw( const Quaternion &q, float flYaw )
{
	// FIXME: I know this can be made *tons* faster, but I just want to get something working quickly
	// that matches being able to add to the yaw of a QAngles so I can expose Quats to script/game code
	QAngle ang;
	QuaternionAngles( q, ang );
	ang[ YAW ] += flYaw;

	Quaternion res;
	AngleQuaternion( ang, res );
	return res;
}

inline Quaternion QuaternionAddRoll( const Quaternion &q, float flRoll )
{
	// FIXME: I know this can be made *tons* faster, but I just want to get something working quickly
	// that matches being able to add to the roll of a QAngles so I can expose Quats to script/game code
	QAngle ang;
	QuaternionAngles( q, ang );
	ang[ ROLL ] += flRoll;

	Quaternion res;
	AngleQuaternion( ang, res );
	return res;
}

inline const Quaternion MatrixQuaternion( const matrix3x4_t &mat )
{
	Quaternion tmp;
	MatrixQuaternion( mat, tmp );
	return tmp;
}

inline const Quaternion MatrixQuaternionFast( const matrix3x4_t &mat )
{
	Quaternion tmp;
	MatrixQuaternionFast( mat, tmp );
	return tmp;
}

inline const matrix3x4_t QuaternionMatrix( const Quaternion &q )
{
	matrix3x4_t mat;
	QuaternionMatrix( q, mat );
	return mat;
}

inline const matrix3x4_t QuaternionMatrix( const Quaternion &q, const Vector &pos )
{
	matrix3x4_t mat;
	QuaternionMatrix( q, pos, mat );
	return mat;
}

inline const Quaternion QuaternionConjugate( const Quaternion &p )
{
	Quaternion q;
	QuaternionConjugate( p, q );
	return q;
}

inline const Quaternion QuaternionInvert( const Quaternion &p )
{
	Quaternion q;
	QuaternionInvert( p, q );
	return q;
}





/// Actual quaternion multiplication; NOTE: QuaternionMult aligns quaternions first, so that q *
/// conjugate(q) may be -1 instead of 1!
inline const Quaternion operator * ( const Quaternion &p, const Quaternion &q )
{
	Quaternion qt;
	qt.x = p.x * q.w + p.y * q.z - p.z * q.y + p.w * q.x;
	qt.y = -p.x * q.z + p.y * q.w + p.z * q.x + p.w * q.y;
	qt.z = p.x * q.y - p.y * q.x + p.z * q.w + p.w * q.z;
	qt.w = -p.x * q.x - p.y * q.y - p.z * q.z + p.w * q.w;
	return qt;
}

inline Quaternion& operator *= ( Quaternion &p, const Quaternion &q )
{
	QuaternionMult( p, q, p );
	return p;
}

inline const matrix3x4_t ConcatTransforms( const matrix3x4_t &in1, const matrix3x4_t &in2 )
{
	matrix3x4_t out;
	ConcatTransforms( in1, in2, out );
	return out;
}

inline const matrix3x4_t operator *( const matrix3x4_t &in1, const matrix3x4_t &in2 )
{
	matrix3x4_t out;
	ConcatTransforms( in1, in2, out );
	return out;
}


inline const matrix3x4_t MatrixInvert( const matrix3x4_t &in )
{
	matrix3x4_t out;
	::MatrixInvert( in, out );
	return out;
}

inline const Vector MatrixGetColumn( const matrix3x4_t &in, MatrixAxisType_t nColumn )
{
	return in.GetColumn( nColumn );
}

// A couple methods to find the dot product of a vector with a matrix row or column...
inline float MatrixRowDotProduct( const matrix3x4_t &in1, int row, const Vector& in2 )
{
	Assert( (row >= 0) && (row < 3) );
	return DotProduct( in1[row], in2.Base() ); 
}

inline float MatrixColumnDotProduct( const matrix3x4_t &in1, int col, const Vector& in2 )
{
	Assert( (col >= 0) && (col < 4) );
	return in1[0][col] * in2[0] + in1[1][col] * in2[1] + in1[2][col] * in2[2]; 
}

int __cdecl BoxOnPlaneSide (const float *emins, const float *emaxs, const cplane_t *plane);

inline float anglemod(float a)
{
	a = (360.f/65536) * ((int)(a*(65536.f/360.0f)) & 65535);
	return a;
}

//// CLAMP

// Remap a value in the range [A,B] to [C,D].
inline float RemapVal( float val, float A, float B, float C, float D)
{
	if ( A == B )
		return fsel( val - B , D , C );
	return C + (D - C) * (val - A) / (B - A);
}

inline float RemapValClamped( float val, float A, float B, float C, float D)
{
	if ( A == B )
		return fsel( val - B , D , C );
	float cVal = (val - A) / (B - A);
	cVal = clamp<float>( cVal, 0.0f, 1.0f );

	return C + (D - C) * cVal;
}

// Returns A + (B-A)*flPercent.
// float Lerp( float flPercent, float A, float B );
template <class T>
FORCEINLINE T Lerp( float flPercent, T const &A, T const &B )
{
	return A + (B - A) * flPercent;
}

FORCEINLINE float Sqr( float f )
{
	return f*f;
}


#ifndef VECTOR_NO_SLOW_OPERATIONS

// YWB:  Specialization for interpolating euler angles via quaternions...
template<> FORCEINLINE QAngle Lerp<QAngle>( float flPercent, const QAngle& q1, const QAngle& q2 )
{
	// Avoid precision errors
	if ( q1 == q2 )
		return q1;

	Quaternion src, dest;

	// Convert to quaternions
	AngleQuaternion( q1, src );
	AngleQuaternion( q2, dest );

	Quaternion result;

	// Slerp
	QuaternionSlerp( src, dest, flPercent, result );

	// Convert to euler
	QAngle output;
	QuaternionAngles( result, output );
	return output;
}

#else

#pragma error

// NOTE NOTE: I haven't tested this!! It may not work! Check out interpolatedvar.cpp in the client dll to try it
template<> FORCEINLINE QAngleByValue Lerp<QAngleByValue>( float flPercent, const QAngleByValue& q1, const QAngleByValue& q2 )
{
	// Avoid precision errors
	if ( q1 == q2 )
		return q1;

	Quaternion src, dest;

	// Convert to quaternions
	AngleQuaternion( q1, src );
	AngleQuaternion( q2, dest );

	Quaternion result;

	// Slerp
	QuaternionSlerp( src, dest, flPercent, result );

	// Convert to euler
	QAngleByValue output;
	QuaternionAngles( result, output );
	return output;
}

#endif // VECTOR_NO_SLOW_OPERATIONS


// Swap two of anything.
template <class T> 
FORCEINLINE void V_swap( T& x, T& y )
{
	T temp = x;
	x = y;
	y = temp;
}

template <class T> FORCEINLINE T AVG(T a, T b)
{
	return (a+b)/2;
}

// number of elements in an array of static size
#define NELEMS(x) ((sizeof(x))/sizeof(x[0]))

// XYZ macro, for printf type functions - ex printf("%f %f %f",XYZ(myvector));
#define XYZ(v) (v).x,(v).y,(v).z




inline float Sign( float x )
{
	return fsel( x, 1.0f, -1.0f ); // x >= 0 ? 1.0f : -1.0f
	//return (x <0.0f) ? -1.0f : 1.0f;
}

//
// Clamps the input integer to the given array bounds.
// Equivalent to the following, but without using any branches:
//
// if( n < 0 ) return 0;
// else if ( n > maxindex ) return maxindex;
// else return n;
//
// This is not always a clear performance win, but when you have situations where a clamped 
// value is thrashing against a boundary this is a big win. (ie, valid, invalid, valid, invalid, ...)
//
// Note: This code has been run against all possible integers.
//
inline int ClampArrayBounds( int n, unsigned maxindex )
{
	// mask is 0 if less than 4096, 0xFFFFFFFF if greater than
	unsigned int inrangemask = 0xFFFFFFFF + (((unsigned) n) > maxindex );
	unsigned int lessthan0mask = 0xFFFFFFFF + ( n >= 0 );
	
	// If the result was valid, set the result, (otherwise sets zero)
	int result = (inrangemask & n);

	// if the result was out of range or zero.
	result |= ((~inrangemask) & (~lessthan0mask)) & maxindex;

	return result;
}



#define BOX_ON_PLANE_SIDE(emins, emaxs, p)	\
	(((p)->type < 3)?						\
	(										\
		((p)->dist <= (emins)[(p)->type])?	\
			1								\
		:									\
		(									\
			((p)->dist >= (emaxs)[(p)->type])?\
				2							\
			:								\
				3							\
		)									\
	)										\
	:										\
		BoxOnPlaneSide( (emins), (emaxs), (p)))

//-----------------------------------------------------------------------------
// FIXME: Vector versions.... the float versions will go away hopefully soon!
//-----------------------------------------------------------------------------

void AngleVectorsFLU( const QAngle& angles, Vector *pForward, Vector *pLeft, Vector *pUp );
void AngleMatrix (const QAngle &angles, matrix3x4_t &mat );
void AngleMatrix( const QAngle &angles, const Vector &position, matrix3x4_t &mat );
void AngleMatrix (const RadianEuler &angles, matrix3x4_t &mat );
void AngleMatrix( RadianEuler const &angles, const Vector &position, matrix3x4_t &mat );
void AngleIMatrix (const QAngle &angles, matrix3x4_t &mat );
void AngleIMatrix (const RadianEuler &angles, matrix3x4_t &mat );
void VectorAngles( const Vector &forward, QAngle &angles );
void SetIdentityMatrix( matrix3x4_t &mat );
void SetScaleMatrix( float x, float y, float z, matrix3x4_t &dst );
void MatrixBuildRotationAboutAxis( const Vector &vAxisOfRot, float angleDegrees, matrix3x4_t &dst );

inline bool MatrixIsIdentity( const matrix3x4_t &m )
{
	return 
		m.m_flMatVal[0][0] == 1.0f && m.m_flMatVal[0][1] == 0.0f && m.m_flMatVal[0][2] == 0.0f && m.m_flMatVal[0][3] == 0.0f &&
		m.m_flMatVal[1][0] == 0.0f && m.m_flMatVal[1][1] == 1.0f && m.m_flMatVal[1][2] == 0.0f && m.m_flMatVal[1][3] == 0.0f &&
		m.m_flMatVal[2][0] == 0.0f && m.m_flMatVal[2][1] == 0.0f && m.m_flMatVal[2][2] == 1.0f && m.m_flMatVal[2][3] == 0.0f;
}


inline void SetScaleMatrix( float flScale, matrix3x4_t &dst )
{
	SetScaleMatrix( flScale, flScale, flScale, dst );
}

inline void SetScaleMatrix( const Vector& scale, matrix3x4_t &dst )
{
	SetScaleMatrix( scale.x, scale.y, scale.z, dst );
}

// Computes the inverse transpose
void MatrixTranspose( matrix3x4_t& mat );
void MatrixTranspose( const matrix3x4_t& src, matrix3x4_t& dst );
void MatrixInverseTranspose( const matrix3x4_t& src, matrix3x4_t& dst );

inline void PositionMatrix( const Vector &position, matrix3x4_t &mat )
{
	MatrixSetColumn( position, 3, mat );
}

inline void MatrixPosition( const matrix3x4_t &matrix, Vector &position )
{
	position[0] = matrix[0][3];
	position[1] = matrix[1][3];
	position[2] = matrix[2][3];
}

inline void VectorRotate( const Vector& in1, const matrix3x4_t &in2, Vector &out)
{
	VectorRotate( &in1.x, in2, &out.x );
}

inline void VectorIRotate( const Vector& in1, const matrix3x4_t &in2, Vector &out)
{
	VectorIRotate( &in1.x, in2, &out.x );
}

inline void MatrixAngles( const matrix3x4_t &matrix, QAngle &angles )
{
	MatrixAngles( matrix, &angles.x );
}

inline void MatrixAngles( const matrix3x4_t &matrix, QAngle &angles, Vector &position )
{
	MatrixAngles( matrix, angles );
	MatrixPosition( matrix, position );
}

inline void MatrixAngles( const matrix3x4_t &matrix, RadianEuler &angles )
{
	MatrixAngles( matrix, &angles.x );

	angles.Init( DEG2RAD( angles.z ), DEG2RAD( angles.x ), DEG2RAD( angles.y ) );
}

void MatrixAngles( const matrix3x4_t &mat, RadianEuler &angles, Vector &position );

void MatrixAngles( const matrix3x4_t &mat, Quaternion &q, Vector &position );

inline int VectorCompare (const Vector& v1, const Vector& v2)
{
	return v1 == v2;
}

inline void VectorTransform (const Vector& in1, const matrix3x4_t &in2, Vector &out)
{
	VectorTransform( &in1.x, in2, &out.x );
}

// MSVC folds the return value nicely and creates no temporaries on the stack,
//    we need more experiments with different compilers and in different circumstances
inline const Vector VectorTransform( const Vector& in1, const matrix3x4_t &in2 )
{
	Vector out;
	VectorTransform( in1, in2, out );
	return out;
}

inline const Vector VectorRotate( const Vector& in1, const matrix3x4_t &in2 )
{
	Vector out;
	VectorRotate( in1, in2, out );
	return out;
}



inline void VectorITransform (const Vector& in1, const matrix3x4_t &in2, Vector &out)
{
	VectorITransform( &in1.x, in2, &out.x );
}

inline const Vector VectorITransform( const Vector& in1, const matrix3x4_t &in2 )
{
	Vector out;
	VectorITransform( in1, in2, out );
	return out;
}

/*
inline void DecomposeRotation( const matrix3x4_t &mat, Vector &out )
{
	DecomposeRotation( mat, &out.x );
}
*/

inline int BoxOnPlaneSide (const Vector& emins, const Vector& emaxs, const cplane_t *plane )
{
	return BoxOnPlaneSide( &emins.x, &emaxs.x, plane );
}

inline void VectorFill(Vector& a, float b)
{
	a[0]=a[1]=a[2]=b;
}

inline void VectorNegate(Vector& a)
{
	a[0] = -a[0];
	a[1] = -a[1];
	a[2] = -a[2];
}

inline vec_t VectorAvg(Vector& a)
{
	return ( a[0] + a[1] + a[2] ) / 3;
}

//-----------------------------------------------------------------------------
// Box/plane test (slow version)
//-----------------------------------------------------------------------------
inline int FASTCALL BoxOnPlaneSide2 (const Vector& emins, const Vector& emaxs, const cplane_t *p, float tolerance = 0.f )
{
	Vector	corners[2];

	if (p->normal[0] < 0)
	{
		corners[0][0] = emins[0];
		corners[1][0] = emaxs[0];
	}
	else
	{
		corners[1][0] = emins[0];
		corners[0][0] = emaxs[0];
	}

	if (p->normal[1] < 0)
	{
		corners[0][1] = emins[1];
		corners[1][1] = emaxs[1];
	}
	else
	{
		corners[1][1] = emins[1];
		corners[0][1] = emaxs[1];
	}

	if (p->normal[2] < 0)
	{
		corners[0][2] = emins[2];
		corners[1][2] = emaxs[2];
	}
	else
	{
		corners[1][2] = emins[2];
		corners[0][2] = emaxs[2];
	}

	int sides = 0;

	float dist1 = DotProduct (p->normal, corners[0]) - p->dist;
	if (dist1 >= tolerance)
		sides = 1;

	float dist2 = DotProduct (p->normal, corners[1]) - p->dist;
	if (dist2 < -tolerance)
		sides |= 2;

	return sides;
}

//-----------------------------------------------------------------------------
// Helpers for bounding box construction
//-----------------------------------------------------------------------------

void ClearBounds (Vector& mins, Vector& maxs);
void AddPointToBounds (const Vector& v, Vector& mins, Vector& maxs);


// solves for "a, b, c" where "a x^2 + b x + c = y", return true if solution exists
bool SolveInverseQuadratic( float x1, float y1, float x2, float y2, float x3, float y3, float &a, float &b, float &c );




// rotate a vector around the Z axis (YAW)
void VectorYawRotate( const Vector& in, float flYaw, Vector &out);






//float ExponentialDecay( float halflife, float dt );
//float ExponentialDecay( float decayTo, float decayTime, float dt );

// halflife is time for value to reach 50%
inline float ExponentialDecay( float halflife, float dt )
{
	// log(0.5) == -0.69314718055994530941723212145818
	return expf( -0.69314718f / halflife * dt);
}

// decayTo is factor the value should decay to in decayTime
inline float ExponentialDecay( float decayTo, float decayTime, float dt )
{
	return expf( logf( decayTo ) / decayTime * dt);
}

// Get the integrated distanced traveled
// decayTo is factor the value should decay to in decayTime
// dt is the time relative to the last velocity update
inline float ExponentialDecayIntegral( float decayTo, float decayTime, float dt  )
{
	return (powf( decayTo, dt / decayTime) * decayTime - decayTime) / logf( decayTo );
}

// hermite basis function for smooth interpolation
// Similar to Gain() above, but very cheap to call
// value should be between 0 & 1 inclusive
inline float SimpleSpline( float value )
{
	float valueSquared = value * value;

	// Nice little ease-in, ease-out spline-like curve
	return (3 * valueSquared - 2 * valueSquared * value);
}

// remaps a value in [startInterval, startInterval+rangeInterval] from linear to
// spline using SimpleSpline
inline float SimpleSplineRemapVal( float val, float A, float B, float C, float D)
{
	if ( A == B )
		return val >= B ? D : C;
	float cVal = (val - A) / (B - A);
	return C + (D - C) * SimpleSpline( cVal );
}

// remaps a value in [startInterval, startInterval+rangeInterval] from linear to
// spline using SimpleSpline
inline float SimpleSplineRemapValClamped( float val, float A, float B, float C, float D )
{
	if ( A == B )
		return val >= B ? D : C;
	float cVal = (val - A) / (B - A);
	cVal = clamp( cVal, 0.0f, 1.0f );
	return C + (D - C) * SimpleSpline( cVal );
}


// Round to nearest, ties to even (current FP rounding mode), like the former x87 fistp
FORCEINLINE int RoundFloatToInt(float f)
{
	return static_cast<int>( std::lrintf( f ) );
}

FORCEINLINE unsigned char RoundFloatToByte(float f)
{
	int nResult = RoundFloatToInt( f );

#ifdef Assert
	Assert( nResult >= 0 && nResult <= 255 );
#endif 
	return nResult;

}

FORCEINLINE uint32_t RoundFloatToUnsignedLong(float f)
{
	return static_cast<uint32_t>( RoundFloatToInt( f ) );
}

FORCEINLINE bool IsIntegralValue( float flValue, float flTolerance = 0.001f )
{
	return fabs( RoundFloatToInt( flValue ) - flValue ) < flTolerance;
}

// Fast, accurate ftol:
FORCEINLINE int Float2Int( float a )
{
	return static_cast<int>( a );
}



// Over 15x faster than: (int)floor(value)
inline int Floor2Int( float a )
{
	return static_cast<int>( floor(a) );
}

//-----------------------------------------------------------------------------
// Fast color conversion from float to unsigned char
//-----------------------------------------------------------------------------
FORCEINLINE unsigned char FastFToC( float c )
{
	volatile float dc;

	// ieee trick
	dc = c * 255.0f + (float)(1 << 23);
	
	// return the lsb
	return *(unsigned char*)&dc;
}

//-----------------------------------------------------------------------------
// Purpose: Bound input float to .001 (millisecond) boundary
// Input  : in - 
// Output : inline float
//-----------------------------------------------------------------------------
inline float ClampToMsec( float in )
{
	int msec = Floor2Int( in * 1000.0f + 0.5f );
	return msec / 1000.0f;
}

// Over 15x faster than: (int)ceil(value)
inline int Ceil2Int( float a )
{
	return static_cast<int>( ceil(a) );
}


// Regular signed area of triangle
#define TriArea2D( A, B, C ) \
	( 0.5f * ( ( B.x - A.x ) * ( C.y - A.y ) - ( B.y - A.y ) * ( C.x - A.x ) ) )

// This version doesn't premultiply by 0.5f, so it's the area of the rectangle instead
#define TriArea2DTimesTwo( A, B, C ) \
	( ( ( B.x - A.x ) * ( C.y - A.y ) - ( B.y - A.y ) * ( C.x - A.x ) ) )


// Get the barycentric coordinates of "pt" in triangle [A,B,C].
inline void GetBarycentricCoords2D( 
	Vector2D const &A,
	Vector2D const &B,
	Vector2D const &C,
	Vector2D const &pt,
	float bcCoords[3] )
{
	// Note, because to top and bottom are both x2, the issue washes out in the composite
	float invTriArea = 1.0f / TriArea2DTimesTwo( A, B, C );

	// NOTE: We assume here that the lightmap coordinate vertices go counterclockwise.
	// If not, TriArea2D() is negated so this works out right.
	bcCoords[0] = TriArea2DTimesTwo( B, C, pt ) * invTriArea;
	bcCoords[1] = TriArea2DTimesTwo( C, A, pt ) * invTriArea;
	bcCoords[2] = TriArea2DTimesTwo( A, B, pt ) * invTriArea;
}


// Return true of the sphere might touch the box (the sphere is actually treated
// like a box itself, so this may return true if the sphere's bounding box touches
// a corner of the box but the sphere itself doesn't).
inline bool QuickBoxSphereTest( 
	const Vector& vOrigin,
	float flRadius,
	const Vector& bbMin,
	const Vector& bbMax )
{
	return vOrigin.x - flRadius < bbMax.x && vOrigin.x + flRadius > bbMin.x &&
		vOrigin.y - flRadius < bbMax.y && vOrigin.y + flRadius > bbMin.y && 
		vOrigin.z - flRadius < bbMax.z && vOrigin.z + flRadius > bbMin.z;
}


// Return true of the boxes intersect (but not if they just touch).
inline bool QuickBoxIntersectTest( 
	const Vector& vBox1Min,
	const Vector& vBox1Max,
	const Vector& vBox2Min,
	const Vector& vBox2Max )
{
	return 
		vBox1Min.x < vBox2Max.x && vBox1Max.x > vBox2Min.x &&
		vBox1Min.y < vBox2Max.y && vBox1Max.y > vBox2Min.y && 
		vBox1Min.z < vBox2Max.z && vBox1Max.z > vBox2Min.z;
}




// linear (0..4) to screen corrected vertex space (0..1?)
FORCEINLINE float LinearToVertexLight( float f )
{
	extern float lineartovertex[4096];	

	// Gotta clamp before the multiply; could overflow...
	// assume 0..4 range
	int i = RoundFloatToInt( f * 1024.f );

	// Presumably the comman case will be not to clamp, so check that first:
	if( (unsigned)i > 4095 )
	{
		if ( i < 0 )
			i = 0;		// Compare to zero instead of 4095 to save 4 bytes in the instruction stream
		else
			i = 4095;
	}

	return lineartovertex[i];
}


FORCEINLINE unsigned char LinearToLightmap( float f )
{
	extern unsigned char lineartolightmap[4096];	

	// Gotta clamp before the multiply; could overflow...
	int i = RoundFloatToInt( f * 1024.f );	// assume 0..4 range

	// Presumably the comman case will be not to clamp, so check that first:
	if ( (unsigned)i > 4095 )
	{
		if ( i < 0 )
			i = 0;		// Compare to zero instead of 4095 to save 4 bytes in the instruction stream
		else
			i = 4095;
	}

	return lineartolightmap[i];
}

FORCEINLINE void ColorClamp( Vector& color )
{
	float maxc = MAX( color.x, MAX( color.y, color.z ) );
	if ( maxc > 1.0f )
	{
		float ooMax = 1.0f / maxc;
		color.x *= ooMax;
		color.y *= ooMax;
		color.z *= ooMax;
	}

	if ( color[0] < 0.f ) color[0] = 0.f;
	if ( color[1] < 0.f ) color[1] = 0.f;
	if ( color[2] < 0.f ) color[2] = 0.f;
}

inline void ColorClampTruncate( Vector& color )
{
	if (color[0] > 1.0f) color[0] = 1.0f; else if (color[0] < 0.0f) color[0] = 0.0f;
	if (color[1] > 1.0f) color[1] = 1.0f; else if (color[1] < 0.0f) color[1] = 0.0f;
	if (color[2] > 1.0f) color[2] = 1.0f; else if (color[2] < 0.0f) color[2] = 0.0f;
}

// Interpolate a Catmull-Rom spline.
// t is a [0,1] value and interpolates a curve between p2 and p3.
void Catmull_Rom_Spline(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector &output );

// Interpolate a Catmull-Rom spline.
// Returns the tangent of the point at t of the spline
void Catmull_Rom_Spline_Tangent( 
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector &output );

// Interpolate a Catmull-Rom spline.
// Normalize p2->p1 and p3->p4 to be the same length as p2->p3
void Catmull_Rom_Spline_Normalize(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector &output );

// Interpolate a Catmull-Rom spline.
// Normalize p2.x->p1.x and p3.x->p4.x to be the same length as p2.x->p3.x
void Catmull_Rom_Spline_NormalizeX(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector &output );

// area under the curve [0..t]
void Catmull_Rom_Spline_NormalizeX(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output );



// See http://en.wikipedia.org/wiki/Kochanek-Bartels_curves
// 
// Tension:  -1 = Round -> 1 = Tight
// Bias:     -1 = Pre-shoot (bias left) -> 1 = Post-shoot (bias right)
// Continuity: -1 = Box corners -> 1 = Inverted corners
//
// If T=B=C=0 it's the same matrix as Catmull-Rom.
// If T=1 & B=C=0 it's the same as Cubic.
// If T=B=0 & C=-1 it's just linear interpolation
// 
// See http://news.povray.org/povray.binaries.tutorials/attachment/%3CXns91B880592482seed7@povray.org%3E/Splines.bas.txt
// for example code and descriptions of various spline types...
// 
void Kochanek_Bartels_Spline(
	float tension, 
	float bias, 
	float continuity,
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output );

void Kochanek_Bartels_Spline_NormalizeX(
	float tension, 
	float bias, 
	float continuity,
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output );

// See link at Kochanek_Bartels_Spline for info on the basis matrix used
void Cubic_Spline(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output );

void Cubic_Spline_NormalizeX(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output );

// See link at Kochanek_Bartels_Spline for info on the basis matrix used
void BSpline(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output );

// Evaluate the cubic Bernstein basis for the input parametric coordinate.
// Output is the coefficient for that basis polynomial.

// quintic interpolating polynomial from Perlin.
// 0->0, 1->1, smooth-in between with smooth tangents
inline float QuinticInterpolatingPolynomial(float t)
{
	// 6t^5-15t^4+10t^3
	return t * t * t *( t * ( t* 6.0 - 15.0 ) + 10.0 );
}

// given a table of sorted tabulated positions, return the two indices and blendfactor to linear
// interpolate. Does a search. Can be used to find the blend value to interpolate between
// keyframes.

// Get the minimum distance from vOrigin to the bounding box defined by [mins,maxs]
// using voronoi regions.
// 0 is returned if the origin is inside the box.
float CalcSqrDistanceToAABB( const Vector &mins, const Vector &maxs, const Vector &point );

inline float CalcDistanceToAABB( const Vector &mins, const Vector &maxs, const Vector &point )
{
	float flDistSqr = CalcSqrDistanceToAABB( mins, maxs, point );
	return sqrt(flDistSqr);
}

// Get the closest point from P to the (infinite) line through vLineA and vLineB and
// calculate the shortest distance from P to the line.
// If you pass in a value for t, it will tell you the t for (A + (B-A)t) to get the closest point.
// If the closest point lies on the segment between A and B, then 0 <= t <= 1.

// The same three functions as above, except now the line is closed between A and B.

// The above functions in 2D

// Init the mathlib
void MathLib_Init( float gamma = 2.2f, float texGamma = 2.2f, float brightness = 0.0f, int overbright = 2.0f );

inline float Approach( float target, float value, float speed );
float AngleNormalize( float angle );

// ensure that 0 <= angle <= 360
float AngleNormalizePositive( float angle );




/// return surface area of an AABB
FORCEINLINE float BoxSurfaceArea( Vector const &vecBoxMin, Vector const &vecBoxMax )
{
	Vector boxdim = vecBoxMax - vecBoxMin;
	return 2.0 * ( ( boxdim[0] * boxdim[2] ) + ( boxdim[0] * boxdim[1] ) + ( boxdim[1] * boxdim[2] ) );
}

//-----------------------------------------------------------------------------
// Computes a reasonable tangent space for a triangle
//-----------------------------------------------------------------------------
void CalcTriangleTangentSpace( const Vector &p0, const Vector &p1, const Vector &p2,
							  const Vector2D &t0, const Vector2D &t1, const Vector2D& t2,
							  Vector &sVect, Vector &tVect );

//-----------------------------------------------------------------------------
// Transforms a AABB into another space; which will inherently grow the box.
//-----------------------------------------------------------------------------
void TransformAABB( const matrix3x4_t &in1, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut );

//-----------------------------------------------------------------------------
// Uses the inverse transform of in1
//-----------------------------------------------------------------------------
void ITransformAABB( const matrix3x4_t &in1, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut );

//-----------------------------------------------------------------------------
// Rotates a AABB into another space; which will inherently grow the box. 
// (same as TransformAABB, but doesn't take the translation into account)
//-----------------------------------------------------------------------------
void RotateAABB( const matrix3x4_t &in1, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut );

//-----------------------------------------------------------------------------
// Uses the inverse transform of in1
//-----------------------------------------------------------------------------
void IRotateAABB( const matrix3x4_t &in1, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut );

//-----------------------------------------------------------------------------
// Transform a plane
//-----------------------------------------------------------------------------
inline void MatrixTransformPlane( const matrix3x4_t &src, const cplane_t &inPlane, cplane_t &outPlane )
{
	// What we want to do is the following:
	// 1) transform the normal into the new space.
	// 2) Determine a point on the old plane given by plane dist * plane normal
	// 3) Transform that point into the new space
	// 4) Plane dist = DotProduct( new normal, new point )

	// An optimized version, which works if the plane is orthogonal.
	// 1) Transform the normal into the new space
	// 2) Realize that transforming the old plane point into the new space
	// is given by [ d * n'x + Tx, d * n'y + Ty, d * n'z + Tz ]
	// where d = old plane dist, n' = transformed normal, Tn = translational component of transform
	// 3) Compute the new plane dist using the dot product of the normal result of #2

	// For a correct result, this should be an inverse-transpose matrix
	// but that only matters if there are nonuniform scale or skew factors in this matrix.
	VectorRotate( inPlane.normal, src, outPlane.normal );
	outPlane.dist = inPlane.dist * DotProduct( outPlane.normal, outPlane.normal );
	outPlane.dist += outPlane.normal.x * src[0][3] + outPlane.normal.y * src[1][3] + outPlane.normal.z * src[2][3];
}

inline void MatrixITransformPlane( const matrix3x4_t &src, const cplane_t &inPlane, cplane_t &outPlane )
{
	// The trick here is that Tn = translational component of transform,
	// but for an inverse transform, Tn = - R^-1 * T
	Vector vecTranslation;
	MatrixGetColumn( src, 3, vecTranslation );

	Vector vecInvTranslation;
	VectorIRotate( vecTranslation, src, vecInvTranslation );

	VectorIRotate( inPlane.normal, src, outPlane.normal );
	outPlane.dist = inPlane.dist * DotProduct( outPlane.normal, outPlane.normal );
	outPlane.dist -= outPlane.normal.x * vecInvTranslation[0] + outPlane.normal.y * vecInvTranslation[1] + outPlane.normal.z * vecInvTranslation[2];
}


FORCEINLINE float * UnpackNormal_HEND3N( const unsigned int *pPackedNormal, float *pNormal )
{
	int temp[3];
	temp[0] = ((*pPackedNormal >> 0L) & 0x7ff);
	if ( temp[0] & 0x400 )
	{
		temp[0] = 2048 - temp[0];
	}
	temp[1] = ((*pPackedNormal >> 11L) & 0x7ff);
	if ( temp[1] & 0x400 )
	{
		temp[1] = 2048 - temp[1];
	}
	temp[2] = ((*pPackedNormal >> 22L) & 0x3ff);
	if ( temp[2] & 0x200 )
	{
		temp[2] = 1024 - temp[2];
	}
	pNormal[0] = (float)temp[0] * 1.0f/1023.0f;
	pNormal[1] = (float)temp[1] * 1.0f/1023.0f;
	pNormal[2] = (float)temp[2] * 1.0f/511.0f;
	return pNormal;
}


FORCEINLINE unsigned int * PackNormal_HEND3N( const float *pNormal, unsigned int *pPackedNormal )
{
	int temp[3];

	temp[0] = Float2Int( pNormal[0] * 1023.0f );
	temp[1] = Float2Int( pNormal[1] * 1023.0f );
	temp[2] = Float2Int( pNormal[2] * 511.0f );

	// the normal is out of bounds, determine the source and fix
	// clamping would be even more of a slowdown here
	Assert( temp[0] >= -1023 && temp[0] <= 1023 );
	Assert( temp[1] >= -1023 && temp[1] <= 1023 );
	Assert( temp[2] >= -511 && temp[2] <= 511 );
	
	*pPackedNormal = ( ( temp[2] & 0x3ff ) << 22L ) |
                     ( ( temp[1] & 0x7ff ) << 11L ) |
                     ( ( temp[0] & 0x7ff ) << 0L );
	return pPackedNormal;
}


FORCEINLINE unsigned int * PackNormal_HEND3N( float nx, float ny, float nz, unsigned int *pPackedNormal )
{
	int temp[3];

	temp[0] = Float2Int( nx * 1023.0f );
	temp[1] = Float2Int( ny * 1023.0f );
	temp[2] = Float2Int( nz * 511.0f );

	// the normal is out of bounds, determine the source and fix
	// clamping would be even more of a slowdown here
	Assert( temp[0] >= -1023 && temp[0] <= 1023 );
	Assert( temp[1] >= -1023 && temp[1] <= 1023 );
	Assert( temp[2] >= -511 && temp[2] <= 511 );
	
	*pPackedNormal = ( ( temp[2] & 0x3ff ) << 22L ) |
                     ( ( temp[1] & 0x7ff ) << 11L ) |
                     ( ( temp[0] & 0x7ff ) << 0L );
	return pPackedNormal;
}



FORCEINLINE float * UnpackNormal_SHORT2( const unsigned int *pPackedNormal, float *pNormal, bool bIsTangent = FALSE )
{
	// Unpacks from Jason's 2-short format (fills in a 4th binormal-sign (+1/-1) value, if this is a tangent vector)

	// FIXME: short math is slow on 360 - use ints here instead (bit-twiddle to deal w/ the sign bits)
	short iX = (*pPackedNormal & 0x0000FFFF);
	short iY = (*pPackedNormal & 0xFFFF0000) >> 16;

	float zSign = +1;
	if ( iX < 0 )
	{
		zSign = -1;
		iX    = -iX;
	}
	float tSign = +1;
	if ( iY < 0 )
	{
		tSign = -1;
		iY    = -iY;
	}

	pNormal[0] = ( iX - 16384.0f ) / 16384.0f;
	pNormal[1] = ( iY - 16384.0f ) / 16384.0f;
	float mag = ( pNormal[0]*pNormal[0] + pNormal[1]*pNormal[1] );
	if ( mag > 1.0f )
	{
		mag = 1.0f;
	}
	pNormal[2] = zSign*sqrtf( 1.0f - mag );
	if ( bIsTangent )
	{
		pNormal[3] = tSign;
	}

	return pNormal;
}

FORCEINLINE unsigned int * PackNormal_SHORT2( float nx, float ny, float nz, unsigned int *pPackedNormal, float binormalSign = +1.0f )
{
	// Pack a vector (ASSUMED TO BE NORMALIZED) into Jason's 4-byte (SHORT2) format.
	// This simply reconstructs Z from X & Y. It uses the sign bits of the X & Y coords
	// to reconstruct the sign of Z and, if this is a tangent vector, the sign of the
	// binormal (this is needed because tangent/binormal vectors are supposed to follow
	// UV gradients, but shaders reconstruct the binormal from the tangent and normal
	// assuming that they form a right-handed basis).

	nx += 1;					// [-1,+1] -> [0,2]
	ny += 1;
	nx *= 16384.0f;				// [ 0, 2] -> [0,32768]
	ny *= 16384.0f;

	// '0' and '32768' values are invalid encodings
	nx = MAX( nx, 1.0f );		// Make sure there are no zero values
	ny = MAX( ny, 1.0f );
	nx = MIN( nx, 32767.0f );	// Make sure there are no 32768 values
	ny = MIN( ny, 32767.0f );

	if ( nz < 0.0f )
		nx = -nx;				// Set the sign bit for z

	ny *= binormalSign;			// Set the sign bit for the binormal (use when encoding a tangent vector)

	// FIXME: short math is slow on 360 - use ints here instead (bit-twiddle to deal w/ the sign bits), also use Float2Int()
	short sX = (short)nx;		// signed short [1,32767]
	short sY = (short)ny;

	*pPackedNormal = ( sX & 0x0000FFFF ) | ( sY << 16 ); // NOTE: The mask is necessary (if sX is negative and cast to an int...)

	return pPackedNormal;
}

FORCEINLINE unsigned int * PackNormal_SHORT2( const float *pNormal, unsigned int *pPackedNormal, float binormalSign = +1.0f )
{
	return PackNormal_SHORT2( pNormal[0], pNormal[1], pNormal[2], pPackedNormal, binormalSign );
}

// Unpacks a UBYTE4 normal (for a tangent, the result's fourth component receives the binormal 'sign')
FORCEINLINE float * UnpackNormal_UBYTE4( const unsigned int *pPackedNormal, float *pNormal, bool bIsTangent = FALSE )
{
	unsigned char cX, cY;
	if ( bIsTangent )
	{
		cX = *pPackedNormal >> 16;					// Unpack Z
		cY = *pPackedNormal >> 24;					// Unpack W
	}
	else
	{
		cX = *pPackedNormal >>  0;					// Unpack X
		cY = *pPackedNormal >>  8;					// Unpack Y
	}

	float x = cX - 128.0f;
	float y = cY - 128.0f;
	float z;

	float zSignBit = x < 0 ? 1.0f : 0.0f;			// z and t negative bits (like slt asm instruction)
	float tSignBit = y < 0 ? 1.0f : 0.0f;
	float zSign    = -( 2*zSignBit - 1 );			// z and t signs
	float tSign    = -( 2*tSignBit - 1 );

	x = x*zSign - zSignBit;							// 0..127
	y = y*tSign - tSignBit;
	x = x - 64;										// -64..63
	y = y - 64;

	float xSignBit = x < 0 ? 1.0f : 0.0f;	// x and y negative bits (like slt asm instruction)
	float ySignBit = y < 0 ? 1.0f : 0.0f;
	float xSign    = -( 2*xSignBit - 1 );			// x and y signs
	float ySign    = -( 2*ySignBit - 1 );

	x = ( x*xSign - xSignBit ) / 63.0f;				// 0..1 range
	y = ( y*ySign - ySignBit ) / 63.0f;
	z = 1.0f - x - y;

	float oolen	 = 1.0f / sqrt( x*x + y*y + z*z );	// Normalize and
	x			*= oolen * xSign;					// Recover signs
	y			*= oolen * ySign;
	z			*= oolen * zSign;

	pNormal[0] = x;
	pNormal[1] = y;
	pNormal[2] = z;
	if ( bIsTangent )
	{
		pNormal[3] = tSign;
	}

	return pNormal;
}

//////////////////////////////////////////////////////////////////////////////
// See: http://www.oroboro.com/rafael/docserv.php/index/programming/article/unitv2
//
// UBYTE4 encoding, using per-octant projection onto x+y+z=1
// Assume input vector is already unit length
//
// binormalSign specifies 'sign' of binormal, stored in t sign bit of tangent
// (lets the shader know whether norm/tan/bin form a right-handed basis)
//
// bIsTangent is used to specify which WORD of the output to store the data
// The expected usage is to call once with the normal and once with
// the tangent and binormal sign flag, bitwise OR'ing the returned DWORDs
FORCEINLINE unsigned int * PackNormal_UBYTE4( float nx, float ny, float nz, unsigned int *pPackedNormal, bool bIsTangent = false, float binormalSign = +1.0f )
{
	float xSign = nx < 0.0f ? -1.0f : 1.0f;			// -1 or 1 sign
	float ySign = ny < 0.0f ? -1.0f : 1.0f;
	float zSign = nz < 0.0f ? -1.0f : 1.0f;
	float tSign = binormalSign;
	Assert( ( binormalSign == +1.0f ) || ( binormalSign == -1.0f ) );

	float xSignBit = 0.5f*( 1 - xSign );			// [-1,+1] -> [1,0]
	float ySignBit = 0.5f*( 1 - ySign );			// 1 is negative bit (like slt instruction)
	float zSignBit = 0.5f*( 1 - zSign );
	float tSignBit = 0.5f*( 1 - binormalSign );		

	float absX = xSign*nx;							// 0..1 range (abs)
	float absY = ySign*ny;
	float absZ = zSign*nz;

	float xbits = absX / ( absX + absY + absZ );	// Project onto x+y+z=1 plane
	float ybits = absY / ( absX + absY + absZ );

	xbits *= 63;									// 0..63
	ybits *= 63;

	xbits  = xbits * xSign - xSignBit;				// -64..63 range
	ybits  = ybits * ySign - ySignBit;
	xbits += 64.0f;									// 0..127 range
	ybits += 64.0f;

	xbits  = xbits * zSign - zSignBit;				// Negate based on z and t
	ybits  = ybits * tSign - tSignBit;				// -128..127 range

	xbits += 128.0f;								// 0..255 range
	ybits += 128.0f;

	unsigned char cX = (unsigned char) xbits;
	unsigned char cY = (unsigned char) ybits;

	if ( !bIsTangent )
		*pPackedNormal = (cX <<  0) | (cY <<  8);	// xy for normal
	else						   
		*pPackedNormal = (cX << 16) | (cY << 24);	// zw for tangent

	return pPackedNormal;
}

FORCEINLINE unsigned int * PackNormal_UBYTE4( const float *pNormal, unsigned int *pPackedNormal, bool bIsTangent = false, float binormalSign = +1.0f )
{
	return PackNormal_UBYTE4( pNormal[0], pNormal[1], pNormal[2], pPackedNormal, bIsTangent, binormalSign );
}

FORCEINLINE void RGB2YUV( int &nR, int &nG, int &nB, float &fY, float &fU, float &fV, bool bApplySaturationCurve )
{
	// YUV conversion:
	//  |Y|   |  0.299f     0.587f     0.114f   |   |R|
	//  |U| = | -0.14713f  -0.28886f   0.436f   | x |G|
	//  |V|   |  0.615f    -0.51499f  -0.10001f |   |B|
	//
	// The coefficients in the first row sum to one, whereas the 2nd and 3rd rows each sum to zero (UV (0,0) means greyscale).
	// Ranges are Y [0,1], U [-0.436,+0.436] and V [-0.615,+0.615].
	// We scale and offset to [0,1] and allow the caller to round as they please.

	fY = (  0.29900f*nR +  0.58700f*nG +  0.11400f*nB ) / 255;
	fU = ( -0.14713f*nR + -0.28886f*nG +  0.43600f*nB )*( 0.5f / 0.436f ) / 255 + 0.5f;
	fV = (  0.61500f*nR + -0.51499f*nG + -0.10001f*nB )*( 0.5f / 0.615f ) / 255 + 0.5f;

	if ( bApplySaturationCurve )
	{
		// Apply a curve to saturation, and snap-to-grey for low saturations
		const float SNAP_TO_GREY = 0;//0.0125f; Disabled, saturation curve seems sufficient
		float dX, dY, sat, scale;
		dX    = 2*( fU - 0.5f );
		dY    = 2*( fV - 0.5f );
		sat   = sqrtf( dX*dX + dY*dY );
		sat   = clamp( ( sat*( 1 + SNAP_TO_GREY ) - SNAP_TO_GREY ), 0, 1 );
		scale = ( sat == 0 ) ? 0 : MIN( ( sqrtf( sat ) / sat ), 4.0f );
		fU    = 0.5f + scale*( fU - 0.5f );
		fV    = 0.5f + scale*( fV - 0.5f );
	}
}




//-----------------------------------------------------------------------------
// Fast version of pow and log
//-----------------------------------------------------------------------------
float FastLog2(float i);			// log2( i )
float FastPow2(float i);			// 2^i
float FastPow(float a, float b);	// a^b
float FastPow10( float i );			// 10^i

//-----------------------------------------------------------------------------
// For testing float equality
//-----------------------------------------------------------------------------

inline bool CloseEnough( float a, float b, float epsilon = EQUAL_EPSILON )
{
	return fabs( a - b ) <= epsilon;
}

inline bool CloseEnough( const Vector &a, const Vector &b, float epsilon = EQUAL_EPSILON )
{
	return fabs( a.x - b.x ) <= epsilon &&
		fabs( a.y - b.y ) <= epsilon &&
		fabs( a.z - b.z ) <= epsilon;
}

// Fast compare
// maxUlps is the maximum error in terms of Units in the Last Place. This 
// specifies how big an error we are willing to accept in terms of the value
// of the least significant digit of the floating point number�s 
// representation. maxUlps can also be interpreted in terms of how many 
// representable floats we are willing to accept between A and B. 
// This function will allow maxUlps-1 floats between A and B.
bool AlmostEqual(float a, float b, int maxUlps = 10);

inline bool AlmostEqual( const Vector &a, const Vector &b, int maxUlps = 10)
{
	return AlmostEqual( a.x, b.x, maxUlps ) &&
		AlmostEqual( a.y, b.y, maxUlps ) &&
		AlmostEqual( a.z, b.z, maxUlps );
}

inline Vector Approach( Vector target, Vector value, float speed )
{
	Vector diff = (target - value);
	float delta = diff.Length();

	if ( delta > speed )
		value += diff.Normalized() * speed;
	else if ( delta < -speed )
		value -= diff.Normalized() * speed;
	else 
		value = target;
		
	return value;
}

inline float Approach( float target, float value, float speed )
{
	float delta = target - value;


	if ( delta > speed )
		value += speed;
	else if ( delta < -speed )
		value -= speed;
	else 
		value = target;
		
	return value;

}


// return a 0..1 value based on the position of x between edge0 and edge1
inline float smoothstep_bounds(float edge0, float edge1, float x)
{
	x = clamp((x - edge0)/(edge1 - edge0),0,1);
	return x*x*(3 - 2*x);
}

// return a value between edge0 and edge1 based on the 0..1 value of x
inline float interpstep(float edge0, float edge1, float x)
{
	return edge0 + (x * ( edge1 - edge0 ));
}

// on PPC we can do this truncate without converting to int

inline double SubtractIntegerPart( double flVal )
{
	return flVal - int(flVal);
}


inline void matrix3x4_t::InitFromQAngles( const QAngle &angles, const Vector &vPosition )
{
	AngleMatrix( angles, vPosition, *this );
}
inline void matrix3x4_t::InitFromQAngles( const QAngle &angles ) { InitFromQAngles( angles, vec3_origin ); }

inline void matrix3x4_t::InitFromRadianEuler( const RadianEuler &angles, const Vector &vPosition )
{
	AngleMatrix( angles, vPosition, *this );
}

inline void matrix3x4_t::InitFromRadianEuler( const RadianEuler &angles ) { InitFromRadianEuler( angles, vec3_origin ); }

inline void matrix3x4_t::InitFromQuaternion( const Quaternion &orientation, const Vector &vPosition )
{
	QuaternionMatrix( orientation, vPosition, *this );
}

inline void matrix3x4_t::InitFromDiagonal( const Vector &vDiagonal )
{
	SetToIdentity();
	m_flMatVal[ 0 ][ 0 ] = vDiagonal.x;
	m_flMatVal[ 1 ][ 1 ] = vDiagonal.y;
	m_flMatVal[ 2 ][ 2 ] = vDiagonal.z;
}


inline void matrix3x4_t::InitFromQuaternion( const Quaternion &orientation ) { InitFromQuaternion( orientation, vec3_origin ); }

inline Quaternion matrix3x4_t::ToQuaternion() const
{
	return MatrixQuaternion( *this );
}

inline QAngle matrix3x4_t::ToQAngle() const
{
	QAngle tmp;
	MatrixAngles( *this, tmp );
	return tmp;
}

inline void matrix3x4_t::SetToIdentity()
{
	SetIdentityMatrix( *this );
}

inline bool matrix3x4_t::IsEqualTo( const matrix3x4_t &other, float flTolerance ) const
{
	return MatricesAreEqual( *this, other, flTolerance );
}

inline void matrix3x4_t::GetBasisVectorsFLU( Vector *pForward, Vector *pLeft, Vector *pUp ) const
{
	return MatrixVectorsFLU( *this, pForward, pLeft, pUp );
}

inline Vector matrix3x4_t::TransformVector( const Vector &v0 ) const
{
	return VectorTransform( v0, *this );
}

inline Vector matrix3x4_t::RotateVector( const Vector &v0 ) const
{
	return VectorRotate( v0, *this );
}

inline Vector matrix3x4_t::TransformVectorByInverse( const Vector &v0 ) const
{
	return VectorITransform( v0, *this );
}

inline Vector matrix3x4_t::RotateVectorByInverse( const Vector &v0 ) const
{
	Vector tmp;
	VectorIRotate( v0, *this, tmp );
	return tmp;
}

inline Vector matrix3x4_t::RotateExtents( const Vector &vBoxExtents ) const
{
	return Vector( DotProductAbs( vBoxExtents, m_flMatVal[ 0 ] ), DotProductAbs( vBoxExtents, m_flMatVal[ 1 ] ), DotProductAbs( vBoxExtents, m_flMatVal[ 2 ] ) );
}

inline Vector matrix3x4_t::GetColumn( MatrixAxisType_t nColumn ) const
{
	return Vector( m_flMatVal[ 0 ][ nColumn ], m_flMatVal[ 1 ][ nColumn ], m_flMatVal[ 2 ][ nColumn ] );
}

inline void matrix3x4_t::SetColumn( const Vector &vColumn, MatrixAxisType_t nColumn )
{
	m_flMatVal[ 0 ][ nColumn ] = vColumn.x;
	m_flMatVal[ 1 ][ nColumn ] = vColumn.y;
	m_flMatVal[ 2 ][ nColumn ] = vColumn.z;
}

inline void matrix3x4_t::InverseTR( matrix3x4_t &out ) const
{
	::MatrixInvert( *this, out );
}

inline matrix3x4_t matrix3x4_t::InverseTR() const
{
	matrix3x4_t out;
	::MatrixInvert( *this, out );
	return out;
}

inline void matrix3x4_t::TransformAABB( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const
{
	::TransformAABB( *this, vecMinsIn, vecMaxsIn, vecMinsOut, vecMaxsOut );
}

inline void matrix3x4_t::TransformAABBByInverse( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const
{
	::ITransformAABB( *this, vecMinsIn, vecMaxsIn, vecMinsOut, vecMaxsOut );
}

inline void matrix3x4_t::RotateAABB( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const
{
	::RotateAABB( *this, vecMinsIn, vecMaxsIn, vecMinsOut, vecMaxsOut );
}
inline void matrix3x4_t::RotateAABBByInverse( const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut ) const
{
	::IRotateAABB( *this, vecMinsIn, vecMaxsIn, vecMinsOut, vecMaxsOut );
}

inline void matrix3x4_t::TransformPlane( const cplane_t &inPlane, cplane_t &outPlane ) const
{
	::MatrixTransformPlane( *this, inPlane, outPlane );
}
inline void matrix3x4_t::TransformPlaneByInverse( const cplane_t &inPlane, cplane_t &outPlane ) const
{
	::MatrixITransformPlane( *this, inPlane, outPlane );
}

inline float matrix3x4_t::GetOrthogonalityError() const
{
	return
		fabsf( m_flMatVal[ 0 ][ 0 ] * m_flMatVal[ 0 ][ 1 ] + m_flMatVal[ 1 ][ 0 ] * m_flMatVal[ 1 ][ 1 ] + m_flMatVal[ 2 ][ 0 ] * m_flMatVal[ 2 ][ 1 ] ) +
		fabsf( m_flMatVal[ 0 ][ 1 ] * m_flMatVal[ 0 ][ 2 ] + m_flMatVal[ 1 ][ 1 ] * m_flMatVal[ 1 ][ 2 ] + m_flMatVal[ 2 ][ 1 ] * m_flMatVal[ 2 ][ 2 ] ) +
		fabsf( m_flMatVal[ 0 ][ 2 ] * m_flMatVal[ 0 ][ 0 ] + m_flMatVal[ 1 ][ 2 ] * m_flMatVal[ 1 ][ 0 ] + m_flMatVal[ 2 ][ 2 ] * m_flMatVal[ 2 ][ 0 ] );
}

inline matrix3x4_t Quaternion::ToMatrix() const
{
	matrix3x4_t mat;
	mat.InitFromQuaternion( *this );
	return mat;
}

inline matrix3x4_t QAngle::ToMatrix() const
{
	matrix3x4_t mat;
	AngleMatrix( *this, mat );
	return mat;
}

inline Quaternion QAngle::ToQuaternion() const
{
	return AngleQuaternion( *this );
}

inline float matrix3x4_t::GetDeterminant() const
{
	return
		m_flMatVal[ 0 ][ 0 ] * ( m_flMatVal[ 1 ][ 1 ] * m_flMatVal[ 2 ][ 2 ] - m_flMatVal[ 2 ][ 1 ] * m_flMatVal[ 1 ][ 2 ] )
		- m_flMatVal[ 0 ][ 1 ] * ( m_flMatVal[ 1 ][ 0 ] * m_flMatVal[ 2 ][ 2 ] - m_flMatVal[ 1 ][ 2 ] * m_flMatVal[ 2 ][ 0 ] )
		+ m_flMatVal[ 0 ][ 2 ] * ( m_flMatVal[ 1 ][ 0 ] * m_flMatVal[ 2 ][ 1 ] - m_flMatVal[ 1 ][ 1 ] * m_flMatVal[ 2 ][ 0 ] );
}

inline float GetRelativeDifferenceSqr( const Vector &a, const Vector &b )
{
	return ( a - b ).LengthSqr() / Max( 1.0f, Max( a.LengthSqr(), b.LengthSqr() ) );
}


inline float GetRelativeDifference( const Vector &a, const Vector &b )
{
	return sqrtf( GetRelativeDifferenceSqr( a, b ) );
}


// a good measure of relative error between two TR matrices, perhaps with a reasonable scale
inline float GetRelativeDifference( const matrix3x4_t &a, const matrix3x4_t &b )
{
	return sqrtf( Max( Max( GetRelativeDifferenceSqr( a.GetColumn( X_AXIS ), b.GetColumn( X_AXIS ) ),
		GetRelativeDifferenceSqr( a.GetColumn( Y_AXIS ), b.GetColumn( Y_AXIS ) ) ),
		Max( GetRelativeDifferenceSqr( a.GetColumn( Z_AXIS ), b.GetColumn( Z_AXIS ) ),
		GetRelativeDifferenceSqr( a.GetOrigin(), b.GetOrigin() ) )
		)
		);
}



inline float matrix3x4_t::GetSylvestersCriterion()const
{
	// http://en.wikipedia.org/wiki/Sylvester%27s_criterion
	float flDet1 = m_flMatVal[ 0 ][ 0 ];
	float flDet2 = m_flMatVal[ 0 ][ 0 ] * m_flMatVal[ 1 ][ 1 ] - m_flMatVal[ 1 ][ 0 ] * m_flMatVal[ 0 ][ 1 ];
	float flDet3 = GetDeterminant();
	return MIN( MIN( flDet1, flDet2 ), flDet3 );
}



// Generate the corner points of a box:
// +y       _+z
// ^        /|
// |       /
// |  3---7   
//   /|  /|
//  / | / |
// 2---6  |
// |  1|--5
// | / | /
// |/  |/
// 0---4   --> +x
//





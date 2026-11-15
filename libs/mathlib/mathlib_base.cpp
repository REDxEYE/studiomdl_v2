//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Math primitives.
//
//===========================================================================//

/// FIXME: As soon as all references to mathlib.c are gone, include it in here

#include <cmath>
#include <cfloat>

#include "tier0/basetypes.h"



#pragma warning(disable:4244)   // "conversion from 'const int' to 'float', possible loss of data"
#pragma warning(disable:4730)	// "mixing _m64 and floating point expressions may result in incorrect code"

#include "mathlib/mathlib.h"
#include "mathlib/vector.h"
#include "mathlib/vplane.h"
#include "mathlib/vmatrix.h"

#include "mathlib/ssemath.h"


bool s_bMathlibInitialized = false;
#ifdef PARANOID
// User must provide an implementation of Sys_Error()
void Sys_Error (char *error, ...);
#endif

const Vector vec3_origin(0,0,0);
const QAngle vec3_angle(0,0,0);
const Quaternion quat_identity(0,0,0,1);
const Vector vec3_invalid( FLT_MAX, FLT_MAX, FLT_MAX );
const int nanmask = 255<<23;

const matrix3x4a_t g_MatrixIdentity(
	1,0,0,0,
	0,1,0,0,
	0,0,1,0
);

float VectorNormalize (Vector& vec)
{
#ifdef _VPROF_MATHLIB

#endif
	Assert( s_bMathlibInitialized );
	float radius = sqrtf(vec.x*vec.x + vec.y*vec.y + vec.z*vec.z);

	// FLT_EPSILON is added to the radius to eliminate the possibility of divide by zero.
	float iradius = 1.f / ( radius + FLT_EPSILON );
	
	vec.x *= iradius;
	vec.y *= iradius;
	vec.z *= iradius;
	
	return radius;
}


// TODO: Add fast C VectorNormalizeFast.
// Perhaps use approximate rsqrt trick, if the accuracy isn't too bad.
void FASTCALL _VectorNormalizeFast (Vector& vec)
{
	Assert( s_bMathlibInitialized );

	// FLT_EPSILON is added to the radius to eliminate the possibility of divide by zero.
	float iradius = 1.f / ( sqrtf(vec.x*vec.x + vec.y*vec.y + vec.z*vec.z) + FLT_EPSILON );
	
	vec.x *= iradius;
	vec.y *= iradius;
	vec.z *= iradius;
	
}

//-----------------------------------------------------------------------------
// Function pointers selecting the appropriate implementation
//-----------------------------------------------------------------------------
void  (FASTCALL *pfVectorNormalizeFast)(Vector& v) = _VectorNormalizeFast;

float SinCosTable[SIN_TABLE_SIZE];
void InitSinCosTable()
{
	for( int i = 0; i < SIN_TABLE_SIZE; i++ )
	{
		SinCosTable[i] = sin(i * 2.0 * M_PI / SIN_TABLE_SIZE);
	}
}


//-----------------------------------------------------------------------------
// Purpose: Generates Euler angles given a left-handed orientation matrix. The
//			columns of the matrix contain the forward, left, and up vectors.
// Input  : matrix - Left-handed orientation matrix.
//			angles[PITCH, YAW, ROLL]. Receives right-handed counterclockwise
//				rotations in degrees around Y, Z, and X respectively.
//-----------------------------------------------------------------------------

void MatrixAngles( const matrix3x4_t& matrix, RadianEuler &angles, Vector &position )
{
	MatrixGetColumn( matrix, 3, position );
	MatrixAngles( matrix, angles );
}

void MatrixAngles( const matrix3x4_t &matrix, Quaternion &q, Vector &pos )
{
#ifdef _VPROF_MATHLIB

#endif
	float trace;
	trace = matrix[0][0] + matrix[1][1] + matrix[2][2] + 1.0f;
	if( trace > 1.0f + FLT_EPSILON ) 
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion A",1);
		q.x = ( matrix[2][1] - matrix[1][2] );
		q.y = ( matrix[0][2] - matrix[2][0] );
		q.z = ( matrix[1][0] - matrix[0][1] );
		q.w = trace;
	} 
	else if ( matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2] ) 
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion B",1);
		trace = 1.0f + matrix[0][0] - matrix[1][1] - matrix[2][2];
		q.x = trace;
		q.y = (matrix[1][0] + matrix[0][1] );
		q.z = (matrix[0][2] + matrix[2][0] );
		q.w = (matrix[2][1] - matrix[1][2] );
	} 
	else if (matrix[1][1] > matrix[2][2])
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion C",1);
		trace = 1.0f + matrix[1][1] - matrix[0][0] - matrix[2][2];
		q.x = (matrix[0][1] + matrix[1][0] );
		q.y = trace;
		q.z = (matrix[2][1] + matrix[1][2] );
		q.w = (matrix[0][2] - matrix[2][0] );
	}
	else
	{
		// VPROF_INCREMENT_COUNTER("MatrixQuaternion D",1);
		trace = 1.0f + matrix[2][2] - matrix[0][0] - matrix[1][1];
		q.x = (matrix[0][2] + matrix[2][0] );
		q.y = (matrix[2][1] + matrix[1][2] );
		q.z = trace;
		q.w = (matrix[1][0] - matrix[0][1] );
	}

	QuaternionNormalize( q );

#if 0
	// check against the angle version
	RadianEuler ang;
	MatrixAngles( matrix, ang );
	Quaternion test;
	AngleQuaternion( ang, test );
	float d = QuaternionDotProduct( q, test );
	Assert( fabs(d) > 0.99 && fabs(d) < 1.01 );
#endif

	MatrixGetColumn( matrix, 3, pos );
}

void MatrixAngles( const matrix3x4_t& matrix, float *angles )
{ 
#ifdef _VPROF_MATHLIB

#endif
	Assert( s_bMathlibInitialized );
	float forward[3];
	float left[3];
	float up[3];

	//
	// Extract the basis vectors from the matrix. Since we only need the Z
	// component of the up vector, we don't get X and Y.
	//
	forward[0] = matrix[0][0];
	forward[1] = matrix[1][0];
	forward[2] = matrix[2][0];
	left[0] = matrix[0][1];
	left[1] = matrix[1][1];
	left[2] = matrix[2][1];
	up[2] = matrix[2][2];

	float xyDist = sqrtf( forward[0] * forward[0] + forward[1] * forward[1] );
	
	// enough here to get angles?
	if ( xyDist > 0.001f )
	{
		// (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
		angles[1] = RAD2DEG( atan2f( forward[1], forward[0] ) );

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG( atan2f( -forward[2], xyDist ) );

		// (roll)	z = ATAN( left.z, up.z );
		angles[2] = RAD2DEG( atan2f( left[2], up[2] ) );
	}
	else	// forward is mostly Z, gimbal lock-
	{
		// (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
		angles[1] = RAD2DEG( atan2f( -left[0], left[1] ) );

		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG( atan2f( -forward[2], xyDist ) );

		// Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
		angles[2] = 0;
	}
}



// transform in1 by the matrix in2
void VectorTransform (const float * RESTRICT in1, const matrix3x4_t& in2, float * RESTRICT out)
{
	Assert( s_bMathlibInitialized );
	float x = DotProduct(in1, in2[0]) + in2[0][3];
	float y = DotProduct(in1, in2[1]) + in2[1][3];
	float z = DotProduct(in1, in2[2]) + in2[2][3];

	out[ 0 ] = x;
	out[ 1 ] = y;
	out[ 2 ] = z;
}


// assuming the matrix is orthonormal, transform in1 by the transpose (also the inverse in this case) of in2.
void VectorITransform (const float *in1, const matrix3x4_t& in2, float *out)
{
	Assert( s_bMathlibInitialized );
	float in1t[3];

	in1t[0] = in1[0] - in2[0][3];
	in1t[1] = in1[1] - in2[1][3];
	in1t[2] = in1[2] - in2[2][3];

	float x = in1t[0] * in2[0][0] + in1t[1] * in2[1][0] + in1t[2] * in2[2][0];
	float y = in1t[0] * in2[0][1] + in1t[1] * in2[1][1] + in1t[2] * in2[2][1];
	float z = in1t[0] * in2[0][2] + in1t[1] * in2[1][2] + in1t[2] * in2[2][2];

	out[ 0 ] = x;
	out[ 1 ] = y;
	out[ 2 ] = z;
}

// assume in2 is a rotation and rotate the input vector
void VectorRotate( const float * RESTRICT in1, const matrix3x4_t& in2, float * RESTRICT out )
{
	Assert( s_bMathlibInitialized );
	float x = DotProduct( in1, in2[ 0 ] );
	float y = DotProduct( in1, in2[ 1 ] );
	float z = DotProduct( in1, in2[ 2 ] );

	out[ 0 ] = x;
	out[ 1 ] = y;
	out[ 2 ] = z;
}

// assume in2 is a rotation and rotate the input vector
void VectorRotate( const Vector &in1, const QAngle &in2, Vector &out )
{
	matrix3x4_t matRotate;
	AngleMatrix( in2, matRotate );
	VectorRotate( in1, matRotate, out );
}

// assume in2 is a rotation and rotate the input vector
void VectorRotate( const Vector &in1, const Quaternion &in2, Vector &out )
{
#if WE_WANT_OUR_CODE_TO_BE_POINTLESSLY_SLOW
	matrix3x4_t matRotate;
	QuaternionMatrix( in2, matRotate );
	VectorRotate( in1, matRotate, out );
#else
	// rotation is  q * v * q^-1

	Quaternion conjugate = in2.Conjugate();


	// do the rotation as unrolled flop code ( QuaternionMult is a function call, which murders instruction scheduling )
	// first q*v
	Quaternion temp;
	temp.x =  in2.y * in1.z - in2.z * in1.y + in2.w * in1.x;
	temp.y = -in2.x * in1.z + in2.z * in1.x + in2.w * in1.y;
	temp.z =  in2.x * in1.y - in2.y * in1.x + in2.w * in1.z;
	temp.w = -in2.x * in1.x - in2.y * in1.y - in2.z * in1.z;

	// now  (qv)(q*)
	out.x =  temp.x * conjugate.w + temp.y * conjugate.z - temp.z * conjugate.y + temp.w * conjugate.x;
	out.y = -temp.x * conjugate.z + temp.y * conjugate.w + temp.z * conjugate.x + temp.w * conjugate.y;
	out.z =  temp.x * conjugate.y - temp.y * conjugate.x + temp.z * conjugate.w + temp.w * conjugate.z;
	Assert( fabs(-temp.x * conjugate.x - temp.y * conjugate.y - temp.z * conjugate.z + temp.w * conjugate.w) < 0.0001 );
#endif
}


// rotate by the inverse of the matrix
void VectorIRotate( const float * RESTRICT in1, const matrix3x4_t& in2, float * RESTRICT out )
{
	Assert( s_bMathlibInitialized );
	Assert( in1 != out );
	out[0] = in1[0]*in2[0][0] + in1[1]*in2[1][0] + in1[2]*in2[2][0];
	out[1] = in1[0]*in2[0][1] + in1[1]*in2[1][1] + in1[2]*in2[2][1];
	out[2] = in1[0]*in2[0][2] + in1[1]*in2[1][2] + in1[2]*in2[2][2];
}

#ifndef VECTOR_NO_SLOW_OPERATIONS

#endif // VECTOR_NO_SLOW_OPERATIONS

void MatrixCopy( const matrix3x4_t& in, matrix3x4_t& out )
{
	Assert( s_bMathlibInitialized );
	memcpy( out.Base(), in.Base(), sizeof( float ) * 3 * 4 );
}

//-----------------------------------------------------------------------------
// Matrix equality test
//-----------------------------------------------------------------------------
bool MatricesAreEqual( const matrix3x4_t &src1, const matrix3x4_t &src2, float flTolerance )
{
	for ( int i = 0; i < 3; ++i )
	{
		for ( int j = 0; j < 4; ++j )
		{
			if ( fabs( src1[i][j] - src2[i][j] ) > flTolerance )
				return false;
		}
	}
	return true;
}


// NOTE: This is just the transpose not a general inverse
void MatrixInvert( const matrix3x4_t& in, matrix3x4_t& out )
{
	Assert( s_bMathlibInitialized );
	if ( &in == &out )
	{
		V_swap(out[0][1],out[1][0]);
		V_swap(out[0][2],out[2][0]);
		V_swap(out[1][2],out[2][1]);
	}
	else
	{
		// transpose the matrix
		out[0][0] = in[0][0];
		out[0][1] = in[1][0];
		out[0][2] = in[2][0];

		out[1][0] = in[0][1];
		out[1][1] = in[1][1];
		out[1][2] = in[2][1];

		out[2][0] = in[0][2];
		out[2][1] = in[1][2];
		out[2][2] = in[2][2];
	}

	// now fix up the translation to be in the other space
	float tmp[3];
	tmp[0] = in[0][3];
	tmp[1] = in[1][3];
	tmp[2] = in[2][3];

	out[0][3] = -DotProduct( tmp, out[0] );
	out[1][3] = -DotProduct( tmp, out[1] );
	out[2][3] = -DotProduct( tmp, out[2] );
}

void MatrixGetColumn( const matrix3x4_t& in, int column, Vector &out )
{
	out.x = in[0][column];
	out.y = in[1][column];
	out.z = in[2][column];
}

void MatrixSetColumn( const Vector &in, int column, matrix3x4_t& out )
{
	out[0][column] = in.x;
	out[1][column] = in.y;
	out[2][column] = in.z;
}

void CrossProduct (const float* v1, const float* v2, float* cross)
{
	Assert( s_bMathlibInitialized );
	Assert( v1 != cross );
	Assert( v2 != cross );
	cross[0] = v1[1]*v2[2] - v1[2]*v2[1];
	cross[1] = v1[2]*v2[0] - v1[0]*v2[2];
	cross[2] = v1[0]*v2[1] - v1[1]*v2[0];
}

size_t Q_log2( unsigned int val )
{
	int answer=0;
	while (val>>=1)
		answer++;
	return answer;
}

// Matrix is right-handed x=forward, y=left, z=up.  We a left-handed convention for vectors in the game code (forward, right, up)
void MatrixVectorsFLU( const matrix3x4_t &matrix, Vector* pForward, Vector *pLeft, Vector *pUp )
{
	MatrixGetColumn( matrix, FORWARD_AXIS, *pForward );
	MatrixGetColumn( matrix, LEFT_AXIS, *pLeft );
	MatrixGetColumn( matrix, UP_AXIS, *pUp );
}


void VectorPerpendicularToVector( Vector const &in, Vector *pvecOut )
{
	float flY = in.y * in.y;
	pvecOut->x = RemapVal( flY, 0, 1, in.z, 1 );
	pvecOut->y = 0;
	pvecOut->z = -in.x;
	pvecOut->NormalizeInPlace();
	float flDot = DotProduct( *pvecOut, in );
	*pvecOut -= flDot * in;
	pvecOut->NormalizeInPlace();
}

//-----------------------------------------------------------------------------
// Euler QAngle -> Basis Vectors.  Each vector is optional
//-----------------------------------------------------------------------------
void AngleVectorsFLU( const QAngle &angles, Vector *pForward, Vector *pLeft, Vector *pUp )
{
	Assert( s_bMathlibInitialized );

	float sr, sp, sy, cr, cp, cy;

	SinCos( DEG2RAD( angles[YAW] ), &sy, &cy );
	SinCos( DEG2RAD( angles[PITCH] ), &sp, &cp );
	SinCos( DEG2RAD( angles[ROLL] ), &sr, &cr );

	if ( pForward )
	{
		(*pForward)[FORWARD_AXIS] = cp*cy;
		(*pForward)[LEFT_AXIS] = cp*sy;
		(*pForward)[UP_AXIS] = -sp;
	}

	if ( pLeft )
	{
		(*pLeft)[FORWARD_AXIS] = (sr*sp*cy+cr*-sy);
		(*pLeft)[LEFT_AXIS] = (sr*sp*sy+cr*cy);
		(*pLeft)[UP_AXIS] = sr*cp;
	}

	if ( pUp )
	{
		(*pUp)[FORWARD_AXIS] = (cr*sp*cy+-sr*-sy);
		(*pUp)[LEFT_AXIS] = (cr*sp*sy+-sr*cy);
		(*pUp)[UP_AXIS] = cr*cp;
	}
}


/*
================
R_ConcatRotations
================
*/


void ConcatTransforms_Aligned( const matrix3x4a_t &m0, const matrix3x4a_t &m1, matrix3x4a_t &out )
{
	AssertAligned( &m0 );
	AssertAligned( &m1 );
	AssertAligned( &out );

	fltx4 lastMask = *(fltx4 *)(&g_SIMD_ComponentMask[3]);
	fltx4 rowA0 = LoadAlignedSIMD( m0.m_flMatVal[0] );
	fltx4 rowA1 = LoadAlignedSIMD( m0.m_flMatVal[1] );
	fltx4 rowA2 = LoadAlignedSIMD( m0.m_flMatVal[2] );

	fltx4 rowB0 = LoadAlignedSIMD( m1.m_flMatVal[0] );
	fltx4 rowB1 = LoadAlignedSIMD( m1.m_flMatVal[1] );
	fltx4 rowB2 = LoadAlignedSIMD( m1.m_flMatVal[2] );

	// now we have the rows of m0 and the columns of m1
	// first output row
	fltx4 A0 = SplatXSIMD(rowA0);
	fltx4 A1 = SplatYSIMD(rowA0);
	fltx4 A2 = SplatZSIMD(rowA0);
	fltx4 mul00 = MulSIMD( A0, rowB0 );
	fltx4 mul01 = MulSIMD( A1, rowB1 );
	fltx4 mul02 = MulSIMD( A2, rowB2 );
	fltx4 out0 = AddSIMD( mul00, AddSIMD(mul01,mul02) );

	// second output row
	A0 = SplatXSIMD(rowA1);
	A1 = SplatYSIMD(rowA1);
	A2 = SplatZSIMD(rowA1);
	fltx4 mul10 = MulSIMD( A0, rowB0 );
	fltx4 mul11 = MulSIMD( A1, rowB1 );
	fltx4 mul12 = MulSIMD( A2, rowB2 );
	fltx4 out1 = AddSIMD( mul10, AddSIMD(mul11,mul12) );

	// third output row
	A0 = SplatXSIMD(rowA2);
	A1 = SplatYSIMD(rowA2);
	A2 = SplatZSIMD(rowA2);
	fltx4 mul20 = MulSIMD( A0, rowB0 );
	fltx4 mul21 = MulSIMD( A1, rowB1 );
	fltx4 mul22 = MulSIMD( A2, rowB2 );
	fltx4 out2 = AddSIMD( mul20, AddSIMD(mul21,mul22) );

	// add in translation vector
	A0 = AndSIMD(rowA0,lastMask);
	A1 = AndSIMD(rowA1,lastMask);
	A2 = AndSIMD(rowA2,lastMask);
	out0 = AddSIMD(out0, A0);
	out1 = AddSIMD(out1, A1);
	out2 = AddSIMD(out2, A2);

	StoreAlignedSIMD( out.m_flMatVal[0], out0 );
	StoreAlignedSIMD( out.m_flMatVal[1], out1 );
	StoreAlignedSIMD( out.m_flMatVal[2], out2 );
}

/*
================
R_ConcatTransforms
================
*/

void ConcatTransforms (const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
#if 0
	// test for ones that'll be 2x faster
	if ( (((size_t)&in1) % 16) == 0 && (((size_t)&in2) % 16) == 0 && (((size_t)&out) % 16) == 0 )
	{
		ConcatTransforms_Aligned( in1, in2, out );
		return;
	}
#endif

	fltx4 lastMask = *(fltx4 *)(&g_SIMD_ComponentMask[3]);
	fltx4 rowA0 = LoadUnalignedSIMD( in1.m_flMatVal[0] );
	fltx4 rowA1 = LoadUnalignedSIMD( in1.m_flMatVal[1] );
	fltx4 rowA2 = LoadUnalignedSIMD( in1.m_flMatVal[2] );

	fltx4 rowB0 = LoadUnalignedSIMD( in2.m_flMatVal[0] );
	fltx4 rowB1 = LoadUnalignedSIMD( in2.m_flMatVal[1] );
	fltx4 rowB2 = LoadUnalignedSIMD( in2.m_flMatVal[2] );

	// now we have the rows of m0 and the columns of m1
	// first output row
	fltx4 A0 = SplatXSIMD(rowA0);
	fltx4 A1 = SplatYSIMD(rowA0);
	fltx4 A2 = SplatZSIMD(rowA0);
	fltx4 mul00 = MulSIMD( A0, rowB0 );
	fltx4 mul01 = MulSIMD( A1, rowB1 );
	fltx4 mul02 = MulSIMD( A2, rowB2 );
	fltx4 out0 = AddSIMD( mul00, AddSIMD(mul01,mul02) );

	// second output row
	A0 = SplatXSIMD(rowA1);
	A1 = SplatYSIMD(rowA1);
	A2 = SplatZSIMD(rowA1);
	fltx4 mul10 = MulSIMD( A0, rowB0 );
	fltx4 mul11 = MulSIMD( A1, rowB1 );
	fltx4 mul12 = MulSIMD( A2, rowB2 );
	fltx4 out1 = AddSIMD( mul10, AddSIMD(mul11,mul12) );

	// third output row
	A0 = SplatXSIMD(rowA2);
	A1 = SplatYSIMD(rowA2);
	A2 = SplatZSIMD(rowA2);
	fltx4 mul20 = MulSIMD( A0, rowB0 );
	fltx4 mul21 = MulSIMD( A1, rowB1 );
	fltx4 mul22 = MulSIMD( A2, rowB2 );
	fltx4 out2 = AddSIMD( mul20, AddSIMD(mul21,mul22) );

	// add in translation vector
	A0 = AndSIMD(rowA0,lastMask);
	A1 = AndSIMD(rowA1,lastMask);
	A2 = AndSIMD(rowA2,lastMask);
	out0 = AddSIMD(out0, A0);
	out1 = AddSIMD(out1, A1);
	out2 = AddSIMD(out2, A2);

	// write to output
	StoreUnalignedSIMD( out.m_flMatVal[0], out0 );
	StoreUnalignedSIMD( out.m_flMatVal[1], out1 );
	StoreUnalignedSIMD( out.m_flMatVal[2], out2 );
}


/*
===================
FloorDivMod

Returns mathematically correct (floor-based) quotient and remainder for
numer and denom, both of which should contain no fractional part. The
quotient must fit in 32 bits.
====================
*/


/*
===================
GreatestCommonDivisor
====================
*/


/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
int __cdecl BoxOnPlaneSide (const float *emins, const float *emaxs, const cplane_t *p)
{
	Assert( s_bMathlibInitialized );
	float	dist1, dist2;
	int		sides;

	// fast axial cases
	if (p->type < 3)
	{
		if (p->dist <= emins[p->type])
			return 1;
		if (p->dist >= emaxs[p->type])
			return 2;
		return 3;
	}
	
	// general case
	switch (p->signbits)
	{
	case 0:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		break;
	case 1:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		break;
	case 2:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		break;
	case 3:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		break;
	case 4:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		break;
	case 5:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		break;
	case 6:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		break;
	case 7:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		break;
	default:
		dist1 = dist2 = 0;		// shut up compiler
		Assert( 0 );
		break;
	}

	sides = 0;
	if (dist1 >= p->dist)
		sides = 1;
	if (dist2 < p->dist)
		sides |= 2;

	Assert( sides != 0 );

	return sides;
}

//-----------------------------------------------------------------------------
// Euler QAngle -> Basis Vectors
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Euler QAngle -> Basis Vectors transposed
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Forward direction vector -> Euler angles
//-----------------------------------------------------------------------------

void VectorAngles( const Vector& forward, QAngle &angles )
{
	Assert( s_bMathlibInitialized );
	float	tmp, yaw, pitch;
	
	if (forward[1] == 0 && forward[0] == 0)
	{
		yaw = 0;
		if (forward[2] > 0)
			pitch = 270;
		else
			pitch = 90;
	}
	else
	{
		yaw = (atan2(forward[1], forward[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		tmp = FastSqrt (forward[0]*forward[0] + forward[1]*forward[1]);
		pitch = (atan2(-forward[2], tmp) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}
	
	angles[0] = pitch;
	angles[1] = yaw;
	angles[2] = 0;
}

//-----------------------------------------------------------------------------
// Forward direction vector with a reference up vector -> Euler angles
//-----------------------------------------------------------------------------


void SetIdentityMatrix( matrix3x4_t& matrix )
{
	memset( matrix.Base(), 0, sizeof(float)*3*4 );
	matrix[0][0] = 1.0;
	matrix[1][1] = 1.0;
	matrix[2][2] = 1.0;
}


//-----------------------------------------------------------------------------
// Builds a scale matrix
//-----------------------------------------------------------------------------
void SetScaleMatrix( float x, float y, float z, matrix3x4_t &dst )
{
	dst[0][0] = x;		dst[0][1] = 0.0f;	dst[0][2] = 0.0f;	dst[0][3] = 0.0f;
	dst[1][0] = 0.0f;	dst[1][1] = y;		dst[1][2] = 0.0f;	dst[1][3] = 0.0f;
	dst[2][0] = 0.0f;	dst[2][1] = 0.0f;	dst[2][2] = z;		dst[2][3] = 0.0f;
}


//-----------------------------------------------------------------------------
// Purpose: Builds the matrix for a counterclockwise rotation about an arbitrary axis.
//
//		   | ax2 + (1 - ax2)cosQ		axay(1 - cosQ) - azsinQ		azax(1 - cosQ) + aysinQ |
// Ra(Q) = | axay(1 - cosQ) + azsinQ	ay2 + (1 - ay2)cosQ			ayaz(1 - cosQ) - axsinQ |
//		   | azax(1 - cosQ) - aysinQ	ayaz(1 - cosQ) + axsinQ		az2 + (1 - az2)cosQ     |
//          
// Input  : mat - 
//			vAxisOrRot - 
//			angle - 
//-----------------------------------------------------------------------------
void MatrixBuildRotationAboutAxis( const Vector &vAxisOfRot, float angleDegrees, matrix3x4_t &dst )
{
	float radians;
	float axisXSquared;
	float axisYSquared;
	float axisZSquared;
	float fSin;
	float fCos;

	radians = angleDegrees * ( M_PI / 180.0 );
	fSin = sin( radians );
	fCos = cos( radians );

	axisXSquared = vAxisOfRot[0] * vAxisOfRot[0];
	axisYSquared = vAxisOfRot[1] * vAxisOfRot[1];
	axisZSquared = vAxisOfRot[2] * vAxisOfRot[2];

	// Column 0:
	dst[0][0] = axisXSquared + (1 - axisXSquared) * fCos;
	dst[1][0] = vAxisOfRot[0] * vAxisOfRot[1] * (1 - fCos) + vAxisOfRot[2] * fSin;
	dst[2][0] = vAxisOfRot[2] * vAxisOfRot[0] * (1 - fCos) - vAxisOfRot[1] * fSin;

	// Column 1:
	dst[0][1] = vAxisOfRot[0] * vAxisOfRot[1] * (1 - fCos) - vAxisOfRot[2] * fSin;
	dst[1][1] = axisYSquared + (1 - axisYSquared) * fCos;
	dst[2][1] = vAxisOfRot[1] * vAxisOfRot[2] * (1 - fCos) + vAxisOfRot[0] * fSin;

	// Column 2:
	dst[0][2] = vAxisOfRot[2] * vAxisOfRot[0] * (1 - fCos) + vAxisOfRot[1] * fSin;
	dst[1][2] = vAxisOfRot[1] * vAxisOfRot[2] * (1 - fCos) - vAxisOfRot[0] * fSin;
	dst[2][2] = axisZSquared + (1 - axisZSquared) * fCos;

	// Column 3:
	dst[0][3] = 0;
	dst[1][3] = 0;
	dst[2][3] = 0;
}


//-----------------------------------------------------------------------------
// Computes the transpose
//-----------------------------------------------------------------------------
void MatrixTranspose( matrix3x4_t& mat )
{
	vec_t tmp;
	tmp = mat[0][1]; mat[0][1] = mat[1][0]; mat[1][0] = tmp;
	tmp = mat[0][2]; mat[0][2] = mat[2][0]; mat[2][0] = tmp;
	tmp = mat[1][2]; mat[1][2] = mat[2][1]; mat[2][1] = tmp;
}

void MatrixTranspose( const matrix3x4_t& src, matrix3x4_t& dst )
{
	dst[0][0] = src[0][0]; dst[0][1] = src[1][0]; dst[0][2] = src[2][0]; dst[0][3] = 0.0f;
	dst[1][0] = src[0][1]; dst[1][1] = src[1][1]; dst[1][2] = src[2][1]; dst[1][3] = 0.0f;
	dst[2][0] = src[0][2]; dst[2][1] = src[1][2]; dst[2][2] = src[2][2]; dst[2][3] = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: converts engine euler angles into a matrix
// Input  : vec3_t angles - PITCH, YAW, ROLL
// Output : *matrix - left-handed column matrix
//			the basis vectors for the rotations will be in the columns as follows:
//			matrix[][0] is forward
//			matrix[][1] is left
//			matrix[][2] is up
//-----------------------------------------------------------------------------
void AngleMatrix( RadianEuler const &angles, const Vector &position, matrix3x4_t& matrix )
{
	AngleMatrix( angles, matrix );
	MatrixSetColumn( position, 3, matrix );
}

void AngleMatrix( const RadianEuler& angles, matrix3x4_t& matrix )
{
	QAngle quakeEuler( RAD2DEG( angles.y ), RAD2DEG( angles.z ), RAD2DEG( angles.x ) );

	AngleMatrix( quakeEuler, matrix );
}


void AngleMatrix( const QAngle &angles, const Vector &position, matrix3x4_t& matrix )
{
	AngleMatrix( angles, matrix );
	MatrixSetColumn( position, 3, matrix );
}

void AngleMatrix( const QAngle &angles, matrix3x4_t& matrix )
{
#ifdef _VPROF_MATHLIB

#endif
	Assert( s_bMathlibInitialized );

	float sr, sp, sy, cr, cp, cy;

	SinCos( DEG2RAD( angles[YAW] ), &sy, &cy );
	SinCos( DEG2RAD( angles[PITCH] ), &sp, &cp );
	SinCos( DEG2RAD( angles[ROLL] ), &sr, &cr );

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp*cy;
	matrix[1][0] = cp*sy;
	matrix[2][0] = -sp;

	// NOTE: Do not optimize this to reduce multiplies! optimizer bug will screw this up.
	matrix[0][1] = sr*sp*cy+cr*-sy;
	matrix[1][1] = sr*sp*sy+cr*cy;
	matrix[2][1] = sr*cp;
	matrix[0][2] = (cr*sp*cy+-sr*-sy);
	matrix[1][2] = (cr*sp*sy+-sr*cy);
	matrix[2][2] = cr*cp;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
}

void AngleIMatrix( const RadianEuler& angles, matrix3x4_t& matrix )
{
	QAngle quakeEuler( RAD2DEG( angles.y ), RAD2DEG( angles.z ), RAD2DEG( angles.x ) );

	AngleIMatrix( quakeEuler, matrix );
}

void AngleIMatrix (const QAngle& angles, matrix3x4_t& matrix )
{
	Assert( s_bMathlibInitialized );
	float		sr, sp, sy, cr, cp, cy;
	
	SinCos( DEG2RAD( angles[YAW] ), &sy, &cy );
	SinCos( DEG2RAD( angles[PITCH] ), &sp, &cp );
	SinCos( DEG2RAD( angles[ROLL] ), &sr, &cr );

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp*cy;
	matrix[0][1] = cp*sy;
	matrix[0][2] = -sp;
	matrix[1][0] = sr*sp*cy+cr*-sy;
	matrix[1][1] = sr*sp*sy+cr*cy;
	matrix[1][2] = sr*cp;
	matrix[2][0] = (cr*sp*cy+-sr*-sy);
	matrix[2][1] = (cr*sp*sy+-sr*cy);
	matrix[2][2] = cr*cp;
	matrix[0][3] = 0.f;
	matrix[1][3] = 0.f;
	matrix[2][3] = 0.f;
}

//-----------------------------------------------------------------------------
// Bounding box construction methods
//-----------------------------------------------------------------------------

void ClearBounds (Vector& mins, Vector& maxs)
{
	Assert( s_bMathlibInitialized );
	mins[0] = mins[1] = mins[2] = FLT_MAX;
	maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
}

void AddPointToBounds (const Vector& v, Vector& mins, Vector& maxs)
{
	Assert( s_bMathlibInitialized );
	int		i;
	vec_t	val;

	for (i=0 ; i<3 ; i++)
	{
		val = v[i];
		if (val < mins[i])
			mins[i] = val;
		if (val > maxs[i])
			maxs[i] = val;
	}
}

// solves for "a, b, c" where "a x^2 + b x + c = y", return true if solution exists
bool SolveInverseQuadratic( float x1, float y1, float x2, float y2, float x3, float y3, float &a, float &b, float &c )
{
	float det = (x1 - x2)*(x1 - x3)*(x2 - x3);

	// FIXME: check with some sort of epsilon
	if (det == 0.0)
		return false;

	a = (x3*(-y1 + y2) + x2*(y1 - y3) + x1*(-y2 + y3)) / det;

	b = (x3*x3*(y1 - y2) + x1*x1*(y2 - y3) + x2*x2*(-y1 + y3)) / det;

    c = (x1*x3*(-x1 + x3)*y2 + x2*x2*(x3*y1 - x1*y3) + x2*(-(x3*x3*y1) + x1*x1*y3)) / det;

	return true;
}



// Rotate a vector around the Z axis (YAW)
void VectorYawRotate( const Vector &in, float flYaw, Vector &out)
{
	Assert( s_bMathlibInitialized );
	if (&in == &out )
	{
		Vector tmp;
		tmp = in;
		VectorYawRotate( tmp, flYaw, out );
		return;
	}

	float sy, cy;

	SinCos( DEG2RAD(flYaw), &sy, &cy );

	out.x = in.x * cy - in.y * sy;
	out.y = in.x * sy + in.y * cy;
	out.z = in.z;
}






inline float MovePeak( float x, float flPeakPos )
{
	// Todo: make this higher-order?
	if( x < flPeakPos )
		return x * 0.5f / flPeakPos;
	else
		return 0.5 + 0.5 * (x - flPeakPos) / (1 - flPeakPos);
}



//-----------------------------------------------------------------------------
// make sure quaternions are within 180 degrees of one another, if not, reverse q
//-----------------------------------------------------------------------------

void QuaternionAlign( const Quaternion &p, const Quaternion &q, Quaternion &qt )
{
	Assert( s_bMathlibInitialized );

	// FIXME: can this be done with a quat dot product?

	int i;
	// decide if one of the quaternions is backwards
	float a = 0;
	float b = 0;
	for (i = 0; i < 4; i++) 
	{
		a += (p[i]-q[i])*(p[i]-q[i]);
		b += (p[i]+q[i])*(p[i]+q[i]);
	}
	if (a > b) 
	{
		for (i = 0; i < 4; i++) 
		{
			qt[i] = -q[i];
		}
	}
	else if (&qt != &q)
	{
		for (i = 0; i < 4; i++) 
		{
			qt[i] = q[i];
		}
	}
}


//-----------------------------------------------------------------------------
// Do a piecewise addition of the quaternion elements. This actually makes little 
// mathematical sense, but it's a cheap way to simulate a slerp.
//-----------------------------------------------------------------------------
void QuaternionBlend( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt )
{
	Assert( s_bMathlibInitialized );
#if ALLOW_SIMD_QUATERNION_MATH
	fltx4 psimd, qsimd, qtsimd;
	psimd = LoadUnalignedSIMD( p.Base() );
	qsimd = LoadUnalignedSIMD( q.Base() );
	qtsimd = QuaternionBlendSIMD( psimd, qsimd, t );
	StoreUnalignedSIMD( qt.Base(), qtsimd );
#else
	// decide if one of the quaternions is backwards
	Quaternion q2;
	QuaternionAlign( p, q, q2 );
	QuaternionBlendNoAlign( p, q2, t, qt );
#endif
}


void QuaternionBlendNoAlign( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt )
{
	Assert( s_bMathlibInitialized );
	float sclp, sclq;
	int i;

	// 0.0 returns p, 1.0 return q.
	sclp = 1.0f - t;
	sclq = t;
	for (i = 0; i < 4; i++) {
		qt[i] = sclp * p[i] + sclq * q[i];
	}
	QuaternionNormalize( qt );
}



//-----------------------------------------------------------------------------
// Quaternion sphereical linear interpolation
//-----------------------------------------------------------------------------

void QuaternionSlerp( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt )
{
	Quaternion q2;
	// 0.0 returns p, 1.0 return q.

	// decide if one of the quaternions is backwards
	QuaternionAlign( p, q, q2 );

	QuaternionSlerpNoAlign( p, q2, t, qt );
}


void QuaternionSlerpNoAlign( const Quaternion &p, const Quaternion &q, float t, Quaternion &qt )
{
	Assert( s_bMathlibInitialized );
	float omega, cosom, sinom, sclp, sclq;
	int i;

	// 0.0 returns p, 1.0 return q.

	cosom = p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3];

	if ((1.0f + cosom) > 0.000001f) {
		if ((1.0f - cosom) > 0.000001f) {
			omega = acos( cosom );
			sinom = sin( omega );
			sclp = sin( (1.0f - t)*omega) / sinom;
			sclq = sin( t*omega ) / sinom;
		}
		else {
			// TODO: add short circuit for cosom == 1.0f?
			sclp = 1.0f - t;
			sclq = t;
		}
		for (i = 0; i < 4; i++) {
			qt[i] = sclp * p[i] + sclq * q[i];
		}
	}
	else {
		Assert( &qt != &q );

		qt[0] = -q[1];
		qt[1] = q[0];
		qt[2] = -q[3];
		qt[3] = q[2];
		sclp = sin( (1.0f - t) * (0.5f * M_PI));
		sclq = sin( t * (0.5f * M_PI));
		for (i = 0; i < 3; i++) {
			qt[i] = sclp * p[i] + sclq * qt[i];
		}
	}

	Assert( qt.IsValid() );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the angular delta between the two normalized quaternions in degrees.
//-----------------------------------------------------------------------------
float QuaternionAngleDiff( const Quaternion &p, const Quaternion &q )
{
#if 1
	// this code path is here for 2 reasons:
	// 1 - acos maps 1-epsilon to values much larger than epsilon (vs asin, which maps epsilon to itself)
	//     this means that in floats, anything below ~0.05 degrees truncates to 0
	// 2 - normalized quaternions are frequently slightly non-normalized due to float precision issues,
	//     and the epsilon off of normalized can be several percents of a degree
	Quaternion qInv, diff;
	QuaternionConjugate( q, qInv );
	QuaternionMult( p, qInv, diff );

	// Note if the quaternion is slightly non-normalized the square root below may be more than 1,
	// the value is clamped to one otherwise it may result in asin() returning an undefined result.
	float sinang = MIN( 1.0f, sqrt( diff.x * diff.x + diff.y * diff.y + diff.z * diff.z ) );
	float angle = RAD2DEG( 2 * asin( sinang ) );
	return angle;
#else
	Quaternion q2;
	QuaternionAlign( p, q, q2 );

	Assert( s_bMathlibInitialized );
	float cosom = p.x * q2.x + p.y * q2.y + p.z * q2.z + p.w * q2.w;

	if ( cosom > -1.0f )
	{
		if ( cosom < 1.0f )
		{
			float omega = 2 * fabs( acos( cosom ) );
			return RAD2DEG( omega );
		}
		return 0.0f;
	}

	return 180.0f;
#endif
}

void QuaternionConjugate( const Quaternion &p, Quaternion &q )
{
	Assert( s_bMathlibInitialized );
	Assert( q.IsValid() );

	q.x = -p.x;
	q.y = -p.y;
	q.z = -p.z;
	q.w = p.w;
}

void QuaternionInvert( const Quaternion &p, Quaternion &q )
{
	Assert( s_bMathlibInitialized );
	Assert( q.IsValid() );

	QuaternionConjugate( p, q );

	float magnitudeSqr = QuaternionDotProduct( p, p );
	Assert( magnitudeSqr );
	if ( magnitudeSqr )
	{
		float inv = 1.0f / magnitudeSqr;
		q.x *= inv;
		q.y *= inv;
		q.z *= inv;
		q.w *= inv;
	}
}


//-----------------------------------------------------------------------------
// Make sure the quaternion is of unit length
//-----------------------------------------------------------------------------
float QuaternionNormalize( Quaternion &q )
{
	Assert( s_bMathlibInitialized );
	float radius, iradius;

	Assert( q.IsValid() );

	radius = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];

	if ( radius ) // > FLT_EPSILON && ((radius < 1.0f - 4*FLT_EPSILON) || (radius > 1.0f + 4*FLT_EPSILON))
	{
		radius = sqrt(radius);
		iradius = 1.0f/radius;
		q[3] *= iradius;
		q[2] *= iradius;
		q[1] *= iradius;
		q[0] *= iradius;
	}
	return radius;
}


void QuaternionScale( const Quaternion &p, float t, Quaternion &q )
{
	Assert( s_bMathlibInitialized );

#if 0
	Quaternion p0;
	Quaternion q;
	p0.Init( 0.0, 0.0, 0.0, 1.0 );

	// slerp in "reverse order" so that p doesn't get realigned
	QuaternionSlerp( p, p0, 1.0 - fabs( t ), q );
	if (t < 0.0)
	{
		q.w = -q.w;
	}
#else
	float r;

	// FIXME: nick, this isn't overly sensitive to accuracy, and it may be faster to 
	// use the cos part (w) of the quaternion (sin(omega)*N,cos(omega)) to figure the new scale.
	float sinom = sqrt( DotProduct( &p.x, &p.x ) );
	sinom = MIN( sinom, 1.f );

	float sinsom = sin( asin( sinom ) * t );

	t = sinsom / (sinom + FLT_EPSILON);
	VectorScale( &p.x, t, &q.x );

	// rescale rotation
	r = 1.0f - sinsom * sinsom;

	// Assert( r >= 0 );
	if (r < 0.0f) 
		r = 0.0f;
	r = sqrt( r );

	// keep sign of rotation
	if (p.w < 0)
		q.w = -r;
	else
		q.w = r;
#endif

	Assert( q.IsValid() );

	return;
}


void QuaternionAdd( const Quaternion &p, const Quaternion &q, Quaternion &qt )
{
	Assert( s_bMathlibInitialized );
	Assert( p.IsValid() );
	Assert( q.IsValid() );

	// decide if one of the quaternions is backwards
	Quaternion q2;
	QuaternionAlign( p, q, q2 );

	// is this right???
	qt[0] = p[0] + q2[0];
	qt[1] = p[1] + q2[1];
	qt[2] = p[2] + q2[2];
	qt[3] = p[3] + q2[3];

	return;
}


float QuaternionDotProduct( const Quaternion &p, const Quaternion &q )
{
	Assert( s_bMathlibInitialized );
	Assert( p.IsValid() );
	Assert( q.IsValid() );

	return p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w;
}


// qt = p * q
void QuaternionMult( const Quaternion &p, const Quaternion &q, Quaternion &qt )
{
	Assert( s_bMathlibInitialized );
	Assert( p.IsValid() );
	Assert( q.IsValid() );

	if (&p == &qt)
	{
		Quaternion p2 = p;
		QuaternionMult( p2, q, qt );
		return;
	}

	// decide if one of the quaternions is backwards
	Quaternion q2;
	QuaternionAlign( p, q, q2 );

	qt.x =  p.x * q2.w + p.y * q2.z - p.z * q2.y + p.w * q2.x;
	qt.y = -p.x * q2.z + p.y * q2.w + p.z * q2.x + p.w * q2.y;
	qt.z =  p.x * q2.y - p.y * q2.x + p.z * q2.w + p.w * q2.z;
	qt.w = -p.x * q2.x - p.y * q2.y - p.z * q2.z + p.w * q2.w;
}



void QuaternionExp( const Quaternion &p, Quaternion &q )
{
	float r  = sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
	float et = exp(p[3]);
	float s  = r>=0.00001f? et*sin(r)/r: 0.f;
	q.Init( s*p[0],s*p[1],s*p[2], et*cos( r ) );
}

void QuaternionLn( const Quaternion &p, Quaternion &q )
{
	float r  = sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
	float t  = r>0.00001f? atan2(r,p[3])/r: 0.f;
	float norm = p[0]*p[0] + p[1]*p[1] + p[2]*p[2] + p[3]*p[3];
	q.Init( t*p[0],t*p[1],t*p[2],0.5*log(norm) );
}

// Average using exponential method
// Qave = exp( 1 / n * log( Q1 ) + ... + 1 / n * log( Qn ) ) where
// if pflWeights passed in 1/n is replaced by normalized weighting
void QuaternionAverageExponential( Quaternion &q, int nCount, const Quaternion *pQuaternions, const float *pflWeights /*=NULL*/ )
{
	Assert( nCount >= 1 );
	Assert( pQuaternions );

	// Nothing to do if only one input quaternions
	if ( nCount == 1 )
	{
		q = pQuaternions[ 0 ];
		return;
	}

	float ooWeightSum = 1.0f;
	float flWeightSum = 0.0f;
	for ( int i = 0 ; i < nCount; ++i )
	{
		if ( pflWeights )
		{
			flWeightSum += pflWeights[ i ];
		}
		else
		{
			flWeightSum += 1.0f;
		}
	}
	
	if ( flWeightSum > 0.0f ) 
	{
		ooWeightSum = 1.0f / flWeightSum;
	}

	Quaternion sum( 0, 0, 0, 0 );
	// Now sum the ln of the quaternions
	for ( int i = 0; i < nCount; ++i )
	{
		float weight = ooWeightSum;
		if ( pflWeights )
		{
			weight *= pflWeights[ i ];
		}

		// Make sure all quaternions are aligned with the 
		// first to avoid blending the wrong direction.
		Quaternion alignedQuat;
		QuaternionAlign( pQuaternions[ 0 ], pQuaternions[ i ], alignedQuat );

		Quaternion qLn;
		QuaternionLn( alignedQuat, qLn );
		for ( int j = 0; j < 4; ++j )
		{
			sum[ j ] += ( qLn[ j ] * weight );
		}
	}

	// then exponentiate to get final value
	QuaternionExp( sum, q );
}


void QuaternionMatrix( const Quaternion &q, const Vector &pos, matrix3x4_t& matrix )
{
	Assert( pos.IsValid() );

	QuaternionMatrix( q, matrix );

	matrix[0][3] = pos.x;
	matrix[1][3] = pos.y;
	matrix[2][3] = pos.z;
}

void QuaternionMatrix( const Quaternion &q, const Vector &pos, const Vector &vScale, matrix3x4_t& mat )
{
	Assert( pos.IsValid() );
	Assert( q.IsValid() );
	Assert( vScale.IsValid() );

	QuaternionMatrix( q, mat );

	mat[ 0 ][ 0 ] *= vScale.x; mat[ 1 ][ 0 ] *= vScale.x; mat[ 2 ][ 0 ] *= vScale.x;
	mat[ 0 ][ 1 ] *= vScale.y; mat[ 1 ][ 1 ] *= vScale.y; mat[ 2 ][ 1 ] *= vScale.y;
	mat[ 0 ][ 2 ] *= vScale.z; mat[ 1 ][ 2 ] *= vScale.z; mat[ 2 ][ 2 ] *= vScale.z;
	mat[ 0 ][ 3 ] = pos.x; mat[ 1 ][ 3 ] = pos.y; mat[ 2 ][ 3 ] = pos.z;
}


void QuaternionMatrix( const Quaternion &q, matrix3x4_t& matrix )
{
	Assert( s_bMathlibInitialized );
	Assert( q.IsValid() );

#ifdef _VPROF_MATHLIB

#endif

// Original code
// This should produce the same code as below with optimization, but looking at the assmebly,
// it doesn't.  There are 7 extra multiplies in the release build of this, go figure.
#if 1
	matrix[0][0] = 1.0 - 2.0 * q.y * q.y - 2.0 * q.z * q.z;
	matrix[1][0] = 2.0 * q.x * q.y + 2.0 * q.w * q.z;
	matrix[2][0] = 2.0 * q.x * q.z - 2.0 * q.w * q.y;

	matrix[0][1] = 2.0f * q.x * q.y - 2.0f * q.w * q.z;
	matrix[1][1] = 1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z;
	matrix[2][1] = 2.0f * q.y * q.z + 2.0f * q.w * q.x;

	matrix[0][2] = 2.0f * q.x * q.z + 2.0f * q.w * q.y;
	matrix[1][2] = 2.0f * q.y * q.z - 2.0f * q.w * q.x;
	matrix[2][2] = 1.0f - 2.0f * q.x * q.x - 2.0f * q.y * q.y;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
#else
   float wx, wy, wz, xx, yy, yz, xy, xz, zz, x2, y2, z2;

    // precalculate common multiplitcations
    x2 = q.x + q.x; 
	y2 = q.y + q.y; 
    z2 = q.z + q.z;
    xx = q.x * x2;
	xy = q.x * y2;
	xz = q.x * z2;
    yy = q.y * y2;
	yz = q.y * z2;
	zz = q.z * z2;
    wx = q.w * x2;
	wy = q.w * y2;
	wz = q.w * z2;

    matrix[0][0] = 1.0 - (yy + zz);
    matrix[0][1] = xy - wz;
	matrix[0][2] = xz + wy;
    matrix[0][3] = 0.0f;

    matrix[1][0] = xy + wz;
	matrix[1][1] = 1.0 - (xx + zz);
    matrix[1][2] = yz - wx;
	matrix[1][3] = 0.0f;

    matrix[2][0] = xz - wy;
	matrix[2][1] = yz + wx;
    matrix[2][2] = 1.0 - (xx + yy);
	matrix[2][3] = 0.0f;
#endif
}


const Vector Quaternion::GetForward()const
{
	Vector vAxisX;
	vAxisX.x = 1.0 - 2.0 * y * y - 2.0 * z * z;
	vAxisX.y = 2.0 * x * y + 2.0 * w * z;
	vAxisX.z = 2.0 * x * z - 2.0 * w * y;
	return vAxisX;
}


const Vector Quaternion::GetLeft()const
{
	Vector vAxisY;
	vAxisY.x = 2.0f * x * y - 2.0f * w * z;
	vAxisY.y = 1.0f - 2.0f * x * x - 2.0f * z * z;
	vAxisY.z = 2.0f * y * z + 2.0f * w * x;
	return vAxisY;
}



const Vector Quaternion::GetUp()const
{
	Vector vAxisZ;
	vAxisZ.x = 2.0f * x * z + 2.0f * w * y;
	vAxisZ.y = 2.0f * y * z - 2.0f * w * x;
	vAxisZ.z = 1.0f - 2.0f * x * x - 2.0f * y * y;
	return vAxisZ;
}






//-----------------------------------------------------------------------------
// Purpose: Converts a quaternion into engine angles
// Input  : *quaternion - q3 + q0.i + q1.j + q2.k
//			*outAngles - PITCH, YAW, ROLL
//-----------------------------------------------------------------------------
void QuaternionAngles( const Quaternion &q, QAngle &angles )
{
	Assert( s_bMathlibInitialized );
	Assert( q.IsValid() );

#ifdef _VPROF_MATHLIB

#endif

#if 1
	// FIXME: doing it this way calculates too much data, needs to do an optimized version...
	matrix3x4_t matrix;
	QuaternionMatrix( q, matrix );
	MatrixAngles( matrix, angles );
#else
	float m11, m12, m13, m23, m33;

	m11 = ( 2.0f * q.w * q.w ) + ( 2.0f * q.x * q.x ) - 1.0f;
	m12 = ( 2.0f * q.x * q.y ) + ( 2.0f * q.w * q.z );
	m13 = ( 2.0f * q.x * q.z ) - ( 2.0f * q.w * q.y );
	m23 = ( 2.0f * q.y * q.z ) + ( 2.0f * q.w * q.x );
	m33 = ( 2.0f * q.w * q.w ) + ( 2.0f * q.z * q.z ) - 1.0f;

	// FIXME: this code has a singularity near PITCH +-90
	angles[YAW] = RAD2DEG( atan2(m12, m11) );
	angles[PITCH] = RAD2DEG( asin(-m13) );
	angles[ROLL] = RAD2DEG( atan2(m23, m33) );
#endif

	Assert( angles.IsValid() );
}






//-----------------------------------------------------------------------------
// Purpose: Converts an exponential map (ang/axis) to a quaternion
//-----------------------------------------------------------------------------
void AxisAngleQuaternion( const Vector &axis, float angle, Quaternion &q )
{
	float sa, ca;
	
	SinCos( DEG2RAD(angle) * 0.5f, &sa, &ca );
	
	q.x = axis.x * sa;
	q.y = axis.y * sa;
	q.z = axis.z * sa;
	q.w = ca;
}

//-----------------------------------------------------------------------------
// Purpose: Converts radian-euler axis aligned angles to a quaternion
// Input  : *pfAngles - Right-handed Euler angles in radians
//			*outQuat - quaternion of form (i,j,k,real)
//-----------------------------------------------------------------------------
void AngleQuaternion( const RadianEuler &angles, Quaternion &outQuat )
{
	Assert( s_bMathlibInitialized );
//	Assert( angles.IsValid() );

#ifdef _VPROF_MATHLIB

#endif

	float sr, sp, sy, cr, cp, cy;

	SinCos( angles.z * 0.5f, &sy, &cy );
	SinCos( angles.y * 0.5f, &sp, &cp );
	SinCos( angles.x * 0.5f, &sr, &cr );

	// NJS: for some reason VC6 wasn't recognizing the common subexpressions:
	float srXcp = sr * cp, crXsp = cr * sp;
	outQuat.x = srXcp*cy-crXsp*sy; // X
	outQuat.y = crXsp*cy+srXcp*sy; // Y

	float crXcp = cr * cp, srXsp = sr * sp;
	outQuat.z = crXcp*sy-srXsp*cy; // Z
	outQuat.w = crXcp*cy+srXsp*sy; // W (real component)
}



//-----------------------------------------------------------------------------
// Purpose: Converts engine-format euler angles to a quaternion
// Input  : angles - Right-handed Euler angles in degrees as follows:
//				[0]: PITCH: Clockwise rotation around the Y axis.
//				[1]: YAW:	Counterclockwise rotation around the Z axis.
//				[2]: ROLL:	Counterclockwise rotation around the X axis.
//			*outQuat - quaternion of form (i,j,k,real)
//-----------------------------------------------------------------------------
void AngleQuaternion( const QAngle &angles, Quaternion &outQuat )
{
#ifdef _VPROF_MATHLIB

#endif

	float sr, sp, sy, cr, cp, cy;

	SinCos( DEG2RAD( angles.y ) * 0.5f, &sy, &cy );
	SinCos( DEG2RAD( angles.x ) * 0.5f, &sp, &cp );
	SinCos( DEG2RAD( angles.z ) * 0.5f, &sr, &cr );

	// NJS: for some reason VC6 wasn't recognizing the common subexpressions:
	float srXcp = sr * cp, crXsp = cr * sp;
	outQuat.x = srXcp*cy-crXsp*sy; // X
	outQuat.y = crXsp*cy+srXcp*sy; // Y

	float crXcp = cr * cp, srXsp = sr * sp;
	outQuat.z = crXcp*sy-srXsp*cy; // Z
	outQuat.w = crXcp*cy+srXsp*sy; // W (real component)
}

//-----------------------------------------------------------------------------
// Purpose: Converts a basis to a quaternion
//-----------------------------------------------------------------------------
void BasisToQuaternion( const Vector &vecForward, const Vector &vecRight, const Vector &vecUp, Quaternion &q )
{
	Assert( fabs( vecForward.LengthSqr() - 1.0f ) < 1e-3 );
	Assert( fabs( vecRight.LengthSqr() - 1.0f ) < 1e-3 );
	Assert( fabs( vecUp.LengthSqr() - 1.0f ) < 1e-3 );

	Vector vecLeft;
	VectorMultiply( vecRight, -1.0f, vecLeft );

	// FIXME: Don't know why, but this doesn't match at all with other result
	// so we can't use this super-fast way.
	/*
	// Find the trace of the matrix:
	float flTrace = vecForward.x + vecLeft.y + vecUp.z + 1.0f;
	if ( flTrace > 1e-6 )
	{
		float flSqrtTrace = FastSqrt( flTrace );
		float s = 0.5f / flSqrtTrace;
		q.x = ( vecUp.y - vecLeft.z ) * s;
		q.y = ( vecForward.z - vecUp.x ) * s;
		q.z = ( vecLeft.x - vecForward.y ) * s;
		q.w = 0.5f * flSqrtTrace;
	}
	else
	{
		if (( vecForward.x > vecLeft.y ) && ( vecForward.x > vecUp.z ) )
		{
			float flSqrtTrace = FastSqrt( 1.0f + vecForward.x - vecLeft.y - vecUp.z );
			float s = 0.5f / flSqrtTrace;
			q.x = 0.5f * flSqrtTrace;
			q.y = ( vecForward.y + vecLeft.x ) * s;
			q.z = ( vecUp.x + vecForward.z ) * s;
			q.w = ( vecUp.y - vecLeft.z ) * s;
		}
		else if ( vecLeft.y > vecUp.z )
		{
			float flSqrtTrace = FastSqrt( 1.0f + vecLeft.y - vecForward.x - vecUp.z );
			float s = 0.5f / flSqrtTrace;
			q.x = ( vecForward.y + vecLeft.x ) * s;
			q.y = 0.5f * flSqrtTrace;
			q.z = ( vecUp.y + vecLeft.z ) * s;
			q.w = ( vecForward.z - vecUp.x ) * s;
		}
		else
		{
			float flSqrtTrace = FastSqrt( 1.0 + vecUp.z - vecForward.x - vecLeft.y );
			float s = 0.5f / flSqrtTrace;
			q.x = ( vecUp.x + vecForward.z ) * s;
			q.y = ( vecUp.y + vecLeft.z ) * s;
			q.z = 0.5f * flSqrtTrace;
			q.w = ( vecLeft.x - vecForward.y ) * s;
		}
	}
	QuaternionNormalize( q );
	*/

	// Version 2: Go through angles

	matrix3x4_t mat;
	MatrixSetColumn( vecForward, 0, mat );
	MatrixSetColumn( vecLeft, 1, mat );
	MatrixSetColumn( vecUp, 2, mat );

	QAngle angles;
	MatrixAngles( mat, angles );

//	Quaternion q2;
	AngleQuaternion( angles, q );

//	Assert( fabs(q.x - q2.x) < 1e-3 );
//	Assert( fabs(q.y - q2.y) < 1e-3 );
//	Assert( fabs(q.z - q2.z) < 1e-3 );
//	Assert( fabs(q.w - q2.w) < 1e-3 );
}

// FIXME: Optimize!
void MatrixQuaternion( const matrix3x4_t &mat, Quaternion &q )
{
	QAngle angles;
	MatrixAngles( mat, angles );
	AngleQuaternion( angles, q );
}

void MatrixQuaternionFast( const matrix3x4_t &mat, Quaternion &q )
{
	float t;
	if ( mat[ 2 ][ 2 ] < 0 )
	{
		if ( mat[ 0 ][ 0 ] > mat[ 1 ][ 1 ] )
		{
			t = 1 + mat[ 0 ][ 0 ] - mat[ 1 ][ 1 ] - mat[ 2 ][ 2 ];
			q.Init( t, mat[ 0 ][ 1 ] + mat[ 1 ][ 0 ], mat[ 2 ][ 0 ] + mat[ 0 ][ 2 ], mat[ 2 ][ 1 ] - mat[ 1 ][ 2 ] );
		}
		else
		{
			t = 1 - mat[ 0 ][ 0 ] + mat[ 1 ][ 1 ] - mat[ 2 ][ 2 ];
			q.Init( mat[ 0 ][ 1 ] + mat[ 1 ][ 0 ], t, mat[ 1 ][ 2 ] + mat[ 2 ][ 1 ], mat[ 0 ][ 2 ] - mat[ 2 ][ 0 ] );
		}
	}
	else
	{
		if ( mat[ 0 ][ 0 ] < -mat[ 1 ][ 1 ] )
		{
			t = 1 - mat[ 0 ][ 0 ] - mat[ 1 ][ 1 ] + mat[ 2 ][ 2 ];
			q.Init( mat[ 2 ][ 0 ] + mat[ 0 ][ 2 ], mat[ 1 ][ 2 ] + mat[ 2 ][ 1 ], t, mat[ 1 ][ 0 ] - mat[ 0 ][ 1 ] );
		}
		else
		{
			t = 1 + mat[ 0 ][ 0 ] + mat[ 1 ][ 1 ] + mat[ 2 ][ 2 ];
			q.Init( mat[ 2 ][ 1 ] - mat[ 1 ][ 2 ], mat[ 0 ][ 2 ] - mat[ 2 ][ 0 ], mat[ 1 ][ 0 ] - mat[ 0 ][ 1 ], t );
		}
	}
	q = q * ( 0.5f / sqrtf( t ) );
}


//-----------------------------------------------------------------------------
// Purpose: Converts a quaternion into engine angles
// Input  : *quaternion - q3 + q0.i + q1.j + q2.k
//			*outAngles - PITCH, YAW, ROLL
//-----------------------------------------------------------------------------
void QuaternionAngles( const Quaternion &q, RadianEuler &angles )
{
	Assert( s_bMathlibInitialized );
	Assert( q.IsValid() );

	// FIXME: doing it this way calculates too much data, needs to do an optimized version...
	matrix3x4_t matrix;
	QuaternionMatrix( q, matrix );
	MatrixAngles( matrix, angles );

	Assert( angles.IsValid() );
}

//-----------------------------------------------------------------------------
// Purpose: A helper function to normalize p2.x->p1.x and p3.x->p4.x to 
//  be the same length as p2.x->p3.x
// Input  : &p2 - 
//			&p4 - 
//			p4n - 
//-----------------------------------------------------------------------------
void Spline_Normalize( 
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	Vector& p1n,
	Vector& p4n )
{
	float dt = p3.x - p2.x;

	p1n = p1;
	p4n = p4;

	if ( dt != 0.0 )
	{
		if (p1.x != p2.x)
		{
			// Equivalent to p1n = p2 - (p2 - p1) * (dt / (p2.x - p1.x));
			VectorLerp( p2, p1, dt / (p2.x - p1.x), p1n );
		}
		if (p4.x != p3.x)
		{
			// Equivalent to p4n = p3 + (p4 - p3) * (dt / (p4.x - p3.x));
			VectorLerp( p3, p4, dt / (p4.x - p3.x), p4n );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
//-----------------------------------------------------------------------------

void Catmull_Rom_Spline(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Assert( s_bMathlibInitialized );
	float tSqr = t*t*0.5f;
	float tSqrSqr = t*tSqr;
	t *= 0.5f;

	Assert( &output != &p1 );
	Assert( &output != &p2 );
	Assert( &output != &p3 );
	Assert( &output != &p4 );

	output.Init();

	Vector a, b, c, d;

	// matrix row 1
	VectorScale( p1, -tSqrSqr, a );		// 0.5 t^3 * [ (-1*p1) + ( 3*p2) + (-3*p3) + p4 ]
	VectorScale( p2, tSqrSqr*3, b );
	VectorScale( p3, tSqrSqr*-3, c );
	VectorScale( p4, tSqrSqr, d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 2
	VectorScale( p1, tSqr*2,  a );		// 0.5 t^2 * [ ( 2*p1) + (-5*p2) + ( 4*p3) - p4 ]
	VectorScale( p2, tSqr*-5, b );
	VectorScale( p3, tSqr*4,  c );
	VectorScale( p4, -tSqr,    d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 3
	VectorScale( p1, -t, a );			// 0.5 t * [ (-1*p1) + p3 ]
	VectorScale( p3, t,  b );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );

	// matrix row 4
	VectorAdd( p2, output, output );	// p2
}

void Catmull_Rom_Spline_Tangent(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Assert( s_bMathlibInitialized );
	float tOne = 3*t*t*0.5f;
	float tTwo = 2*t*0.5f;
	float tThree = 0.5;

	Assert( &output != &p1 );
	Assert( &output != &p2 );
	Assert( &output != &p3 );
	Assert( &output != &p4 );

	output.Init();

	Vector a, b, c, d;

	// matrix row 1
	VectorScale( p1, -tOne, a );		// 0.5 t^3 * [ (-1*p1) + ( 3*p2) + (-3*p3) + p4 ]
	VectorScale( p2, tOne*3, b );
	VectorScale( p3, tOne*-3, c );
	VectorScale( p4, tOne, d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 2
	VectorScale( p1, tTwo*2,  a );		// 0.5 t^2 * [ ( 2*p1) + (-5*p2) + ( 4*p3) - p4 ]
	VectorScale( p2, tTwo*-5, b );
	VectorScale( p3, tTwo*4,  c );
	VectorScale( p4, -tTwo,    d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 3
	VectorScale( p1, -tThree, a );			// 0.5 t * [ (-1*p1) + p3 ]
	VectorScale( p3, tThree,  b );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
}



void Catmull_Rom_Spline_Normalize(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	// Normalize p2->p1 and p3->p4 to be the same length as p2->p3
	float dt = p3.DistTo(p2);

	Vector p1n, p4n;
	VectorSubtract( p1, p2, p1n );
	VectorSubtract( p4, p3, p4n );

	VectorNormalize( p1n );
	VectorNormalize( p4n );

	VectorMA( p2, dt, p1n, p1n );
	VectorMA( p3, dt, p4n, p4n );
	
	Catmull_Rom_Spline( p1n, p2, p3, p4n, t, output );
}



void Catmull_Rom_Spline_NormalizeX(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Vector p1n, p4n;
	Spline_Normalize( p1, p2, p3, p4, p1n, p4n );
	Catmull_Rom_Spline( p1n, p2, p3, p4n, t, output );
}


//-----------------------------------------------------------------------------
// Purpose: basic hermite spline.  t = 0 returns p1, t = 1 returns p2, 
//			d1 and d2 are used to entry and exit slope of curve
// Input  : 
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: simple three data point hermite spline.  
//			t = 0 returns p1, t = 1 returns p2, 
//			slopes are generated from the p0->p1 and p1->p2 segments
//			this is reasonable C1 method when there's no "p3" data yet.
// Input  : 
//-----------------------------------------------------------------------------

// BUG: the VectorSubtract()'s calls go away if the global optimizer is enabled
#pragma optimize( "g", off )

#pragma optimize( "", on )



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
	Vector& output )
{
	Assert( s_bMathlibInitialized );

	float ffa, ffb, ffc, ffd;

	ffa = ( 1.0f - tension ) * ( 1.0f + continuity ) * ( 1.0f + bias );
	ffb = ( 1.0f - tension ) * ( 1.0f - continuity ) * ( 1.0f - bias );
	ffc = ( 1.0f - tension ) * ( 1.0f - continuity ) * ( 1.0f + bias );
	ffd = ( 1.0f - tension ) * ( 1.0f + continuity ) * ( 1.0f - bias );

	float tSqr = t*t*0.5f;
	float tSqrSqr = t*tSqr;
	t *= 0.5f;

	Assert( &output != &p1 );
	Assert( &output != &p2 );
	Assert( &output != &p3 );
	Assert( &output != &p4 );

	output.Init();

	Vector a, b, c, d;

	// matrix row 1
	VectorScale( p1, tSqrSqr * -ffa, a );		
	VectorScale( p2, tSqrSqr * ( 4.0f + ffa - ffb - ffc ), b );
	VectorScale( p3, tSqrSqr * ( -4.0f + ffb + ffc - ffd ), c );
	VectorScale( p4, tSqrSqr * ffd, d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 2
	VectorScale( p1, tSqr* 2 * ffa,  a );		
	VectorScale( p2, tSqr * ( -6 - 2 * ffa + 2 * ffb + ffc ), b );
	VectorScale( p3, tSqr * ( 6 - 2 * ffb - ffc + ffd ),  c );
	VectorScale( p4, tSqr * -ffd,    d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 3
	VectorScale( p1, t * -ffa,  a );		
	VectorScale( p2, t * ( ffa - ffb ), b );
	VectorScale( p3, t * ffb,  c );
	// p4 unchanged

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );

	// matrix row 4
	// p1, p3, p4 unchanged
	// p2 is multiplied by 1 and added, so just added it directly

	VectorAdd( p2, output, output );
}

void Kochanek_Bartels_Spline_NormalizeX(
	float tension, 
	float bias, 
	float continuity,
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Vector p1n, p4n;
	Spline_Normalize( p1, p2, p3, p4, p1n, p4n );
	Kochanek_Bartels_Spline( tension, bias, continuity, p1n, p2, p3, p4n, t, output );
}

void Cubic_Spline(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Assert( s_bMathlibInitialized );

	float tSqr = t*t;
	float tSqrSqr = t*tSqr;

	Assert( &output != &p1 );
	Assert( &output != &p2 );
	Assert( &output != &p3 );
	Assert( &output != &p4 );

	output.Init();

	Vector a, b, c, d;

	// matrix row 1
	VectorScale( p2, tSqrSqr * 2, b );
	VectorScale( p3, tSqrSqr * -2, c );

	VectorAdd( b, output, output );
	VectorAdd( c, output, output );

	// matrix row 2
	VectorScale( p2, tSqr * -3, b );
	VectorScale( p3, tSqr * 3,  c );

	VectorAdd( b, output, output );
	VectorAdd( c, output, output );

	// matrix row 3
	// no influence
	// p4 unchanged

	// matrix row 4
	// p1, p3, p4 unchanged
	VectorAdd( p2, output, output );
}

void Cubic_Spline_NormalizeX(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Vector p1n, p4n;
	Spline_Normalize( p1, p2, p3, p4, p1n, p4n );
	Cubic_Spline( p1n, p2, p3, p4n, t, output );
}

void BSpline(
	const Vector &p1,
	const Vector &p2,
	const Vector &p3,
	const Vector &p4,
	float t, 
	Vector& output )
{
	Assert( s_bMathlibInitialized );

	float oneOver6 = 1.0f / 6.0f;

	float tSqr = t * t * oneOver6;
	float tSqrSqr = t*tSqr;
	t *= oneOver6;

	Assert( &output != &p1 );
	Assert( &output != &p2 );
	Assert( &output != &p3 );
	Assert( &output != &p4 );

	output.Init();

	Vector a, b, c, d;

	// matrix row 1
	VectorScale( p1, -tSqrSqr, a );		
	VectorScale( p2, tSqrSqr * 3.0f, b );
	VectorScale( p3, tSqrSqr * -3.0f, c );
	VectorScale( p4, tSqrSqr, d );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
	VectorAdd( d, output, output );

	// matrix row 2
	VectorScale( p1, tSqr * 3.0f,  a );		
	VectorScale( p2, tSqr * -6.0f, b );
	VectorScale( p3, tSqr * 3.0f,  c );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );

	// matrix row 3
	VectorScale( p1, t * -3.0f,  a );		
	VectorScale( p3, t * 3.0f,  c );
	// p4 unchanged

	VectorAdd( a, output, output );
	VectorAdd( c, output, output );

	// matrix row 4
	// p1 and p3 scaled by 1.0f, so done below
	VectorScale( p1, oneOver6, a );
	VectorScale( p2, 4.0f * oneOver6, b );
	VectorScale( p3, oneOver6, c );

	VectorAdd( a, output, output );
	VectorAdd( b, output, output );
	VectorAdd( c, output, output );
}


//-----------------------------------------------------------------------------
// Purpose: Compress the input values for a ranged result such that from 75% to 200% smoothly of the range maps 
//-----------------------------------------------------------------------------


//#pragma optimize( "", on )

//-----------------------------------------------------------------------------
// Transforms a AABB into another space; which will inherently grow the box.
//-----------------------------------------------------------------------------
void TransformAABB( const matrix3x4_t& transform, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut )
{
	Vector localCenter;
	VectorAdd( vecMinsIn, vecMaxsIn, localCenter );
	localCenter *= 0.5f;

	Vector localExtents;
	VectorSubtract( vecMaxsIn, localCenter, localExtents );

	Vector worldCenter;
	VectorTransform( localCenter, transform, worldCenter );

	Vector worldExtents;
	worldExtents.x = DotProductAbs( localExtents, transform[0] );
	worldExtents.y = DotProductAbs( localExtents, transform[1] );
	worldExtents.z = DotProductAbs( localExtents, transform[2] );

	VectorSubtract( worldCenter, worldExtents, vecMinsOut );
	VectorAdd( worldCenter, worldExtents, vecMaxsOut );
	// sanity chec	
	Assert( vecMinsOut.LengthSqr()  + vecMaxsOut.LengthSqr() < 1e+12 ); 
}


//-----------------------------------------------------------------------------
// Uses the inverse transform of in1
//-----------------------------------------------------------------------------
void ITransformAABB( const matrix3x4_t& transform, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut )
{
	Vector worldCenter;
	VectorAdd( vecMinsIn, vecMaxsIn, worldCenter );
	worldCenter *= 0.5f;

	Vector worldExtents;
	VectorSubtract( vecMaxsIn, worldCenter, worldExtents );

	Vector localCenter;
	VectorITransform( worldCenter, transform, localCenter );

	Vector localExtents;
	localExtents.x =	FloatMakePositive( worldExtents.x * transform[0][0] ) + 
						FloatMakePositive( worldExtents.y * transform[1][0] ) + 
						FloatMakePositive( worldExtents.z * transform[2][0] );
	localExtents.y =	FloatMakePositive( worldExtents.x * transform[0][1] ) + 
						FloatMakePositive( worldExtents.y * transform[1][1] ) + 
						FloatMakePositive( worldExtents.z * transform[2][1] );
	localExtents.z =	FloatMakePositive( worldExtents.x * transform[0][2] ) + 
						FloatMakePositive( worldExtents.y * transform[1][2] ) + 
						FloatMakePositive( worldExtents.z * transform[2][2] );

	VectorSubtract( localCenter, localExtents, vecMinsOut );
	VectorAdd( localCenter, localExtents, vecMaxsOut );
}


//-----------------------------------------------------------------------------
// Rotates a AABB into another space; which will inherently grow the box. 
// (same as TransformAABB, but doesn't take the translation into account)
//-----------------------------------------------------------------------------
void RotateAABB( const matrix3x4_t &transform, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut )
{
	Vector localCenter;
	VectorAdd( vecMinsIn, vecMaxsIn, localCenter );
	localCenter *= 0.5f;

	Vector localExtents;
	VectorSubtract( vecMaxsIn, localCenter, localExtents );

	Vector newCenter;
	VectorRotate( localCenter, transform, newCenter );

	Vector newExtents;
	newExtents.x = DotProductAbs( localExtents, transform[0] );
	newExtents.y = DotProductAbs( localExtents, transform[1] );
	newExtents.z = DotProductAbs( localExtents, transform[2] );

	VectorSubtract( newCenter, newExtents, vecMinsOut );
	VectorAdd( newCenter, newExtents, vecMaxsOut );
}


//-----------------------------------------------------------------------------
// Uses the inverse transform of in1
//-----------------------------------------------------------------------------
void IRotateAABB( const matrix3x4_t &transform, const Vector &vecMinsIn, const Vector &vecMaxsIn, Vector &vecMinsOut, Vector &vecMaxsOut )
{
	Vector oldCenter;
	VectorAdd( vecMinsIn, vecMaxsIn, oldCenter );
	oldCenter *= 0.5f;

	Vector oldExtents;
	VectorSubtract( vecMaxsIn, oldCenter, oldExtents );

	Vector newCenter;
	VectorIRotate( oldCenter, transform, newCenter );

	Vector newExtents;
	newExtents.x =	FloatMakePositive( oldExtents.x * transform[0][0] ) + 
					FloatMakePositive( oldExtents.y * transform[1][0] ) + 
					FloatMakePositive( oldExtents.z * transform[2][0] );
	newExtents.y =	FloatMakePositive( oldExtents.x * transform[0][1] ) + 
					FloatMakePositive( oldExtents.y * transform[1][1] ) + 
					FloatMakePositive( oldExtents.z * transform[2][1] );
	newExtents.z =	FloatMakePositive( oldExtents.x * transform[0][2] ) + 
					FloatMakePositive( oldExtents.y * transform[1][2] ) + 
					FloatMakePositive( oldExtents.z * transform[2][2] );

	VectorSubtract( newCenter, newExtents, vecMinsOut );
	VectorAdd( newCenter, newExtents, vecMaxsOut );
}


float CalcSqrDistanceToAABB( const Vector &mins, const Vector &maxs, const Vector &point )
{
	float flDelta;
	float flDistSqr = 0.0f;

	if ( point.x < mins.x )
	{
		flDelta = (mins.x - point.x);
		flDistSqr += flDelta * flDelta;
	}
	else if ( point.x > maxs.x )
	{
		flDelta = (point.x - maxs.x);
		flDistSqr += flDelta * flDelta;
	}

	if ( point.y < mins.y )
	{
		flDelta = (mins.y - point.y);
		flDistSqr += flDelta * flDelta;
	}
	else if ( point.y > maxs.y )
	{
		flDelta = (point.y - maxs.y);
		flDistSqr += flDelta * flDelta;
	}

	if ( point.z < mins.z )
	{
		flDelta = (mins.z - point.z);
		flDistSqr += flDelta * flDelta;
	}
	else if ( point.z > maxs.z )
	{
		flDelta = (point.z - maxs.z);
		flDistSqr += flDelta * flDelta;
	}

	return flDistSqr;
}




// Do we have another epsilon we could use
#define LINE_EPS ( 0.000001f )

#pragma optimize( "", off )

#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER       1
#endif

#pragma optimize( "", on )


#ifndef NDEBUG
volatile static char const *pDebugString;
#endif

void MathLib_Init( float gamma, float texGamma, float brightness, int overbright, bool bAllow3DNow, bool bAllowSSE, bool bAllowSSE2, bool bAllowMMX )
{
	if ( s_bMathlibInitialized )
		return;
#ifdef _WIN32
	Assert( _rotl( 0xC7654321, 1 ) == 0x8ECA8643 );
	Assert( _rotl64( 0xC7654321ABCDEF00ull, 1 ) == 0x8ECA8643579BDE01ull );
#endif
#ifndef NDEBUG
	pDebugString = "mathlib.lib built debug!";
#endif

	s_bMathlibInitialized = true;

	InitSinCosTable();
	SeedRandSIMD( 0x31415926 );
}






float AngleNormalize( float angle )
{
	angle = fmodf(angle, 360.0f);
	if (angle > 180) 
	{
		angle -= 360;
	}
	if (angle < -180)
	{
		angle += 360;
	}
	return angle;
}

//--------------------------------------------------------------------------------------------------------------
// ensure that 0 <= angle <= 360
float AngleNormalizePositive( float angle )
{
	angle = fmodf( angle, 360.0f );

	if (angle < 0.0f)
	{
		angle += 360.0f;
	}

	return angle;
}


//-----------------------------------------------------------------------------
// Purpose: clip a poly to the plane and return the poly on the front side of the plane
// Input  : *inVerts - input polygon
//			vertCount - # verts in input poly
//			*outVerts - destination poly
//			normal - plane normal
//			dist - plane constant
// Output : int - # verts in output poly
//-----------------------------------------------------------------------------



void fourplanes_t::ComputeSignbits()
{
	xSign = CmpLtSIMD( nX, Four_Zeros );
	ySign = CmpLtSIMD( nY, Four_Zeros );
	zSign = CmpLtSIMD( nZ, Four_Zeros );
	nXAbs = fabs(nX);
	nYAbs = fabs(nY);
	nZAbs = fabs(nZ);
}

void fourplanes_t::GetPlane( int index, Vector *pNormalOut, float *pDistOut ) const
{
	pNormalOut->x = SubFloat(nX,index);
	pNormalOut->y = SubFloat(nY,index);
	pNormalOut->z = SubFloat(nZ,index);
	*pDistOut = SubFloat(dist,index);
}
void fourplanes_t::SetPlane( int index, const Vector &vecNormal, float planeDist )
{
	SubFloat(nX,index) = vecNormal.x;
	SubFloat(nY,index) = vecNormal.y;
	SubFloat(nZ,index) = vecNormal.z;
	SubFloat(dist,index) = planeDist;
	ComputeSignbits();
}

void fourplanes_t::Set4Planes( const VPlane *pPlanes )
{
	nX = LoadUnalignedSIMD( &pPlanes[0].m_Normal.x );
	nY = LoadUnalignedSIMD( &pPlanes[1].m_Normal.x );
	nZ = LoadUnalignedSIMD( &pPlanes[2].m_Normal.x );
	dist = LoadUnalignedSIMD( &pPlanes[3].m_Normal.x );
	TransposeSIMD(nX, nY, nZ, dist);
	ComputeSignbits();
}

void fourplanes_t::Set2Planes( const VPlane *pPlanes )
{
	nX = LoadUnalignedSIMD( &pPlanes[0].m_Normal.x );
	nY = LoadUnalignedSIMD( &pPlanes[1].m_Normal.x );
	nZ = Four_Zeros;
	dist = Four_Zeros;
	TransposeSIMD(nX, nY, nZ, dist);
	ComputeSignbits();
}

void fourplanes_t::Get4Planes( VPlane *pPlanesOut ) const
{
	fltx4 p0 = nX;
	fltx4 p1 = nY;
	fltx4 p2 = nZ;
	fltx4 p3 = dist;
	TransposeSIMD(p0, p1, p2, p3);
	StoreUnalignedSIMD( &pPlanesOut[0].m_Normal.x, p0 );
	StoreUnalignedSIMD( &pPlanesOut[1].m_Normal.x, p1 );
	StoreUnalignedSIMD( &pPlanesOut[2].m_Normal.x, p2 );
	StoreUnalignedSIMD( &pPlanesOut[3].m_Normal.x, p3 );
}

void fourplanes_t::Get2Planes( VPlane *pPlanesOut ) const
{
	fltx4 p0 = nX;
	fltx4 p1 = nY;
	fltx4 p2 = nZ;
	fltx4 p3 = dist;
	TransposeSIMD(p0, p1, p2, p3);
	StoreUnalignedSIMD( &pPlanesOut[0].m_Normal.x, p0 );
	StoreUnalignedSIMD( &pPlanesOut[1].m_Normal.x, p1 );
}


Frustum_t::Frustum_t()
{
	memset(this, 0, sizeof(*this));
}

void Frustum_t::SetPlane( int i, const Vector &vecNormal, float dist )
{
	if ( i < 4 )
	{
		planes[0].SetPlane( i, vecNormal, dist );
	}
	else
	{
		planes[1].SetPlane( i-4, vecNormal, dist );
	}
}

void Frustum_t::GetPlane( int i, Vector *pNormalOut, float *pDistOut ) const
{
	if ( i < 4 )
	{
		planes[0].GetPlane( i, pNormalOut, pDistOut );
	}
	else
	{
		planes[1].GetPlane( i-4, pNormalOut, pDistOut );
	}
}

void Frustum_t::SetPlanes( const VPlane *pPlanes )
{
	planes[0].Set4Planes(pPlanes);
	planes[1].Set2Planes(pPlanes+4);
}

void Frustum_t::GetPlanes( VPlane *pPlanesOut ) const
{
	planes[0].Get4Planes(pPlanesOut);
	planes[1].Get2Planes(pPlanesOut+4);
}


bool Frustum_t::CullBox( const Vector &mins, const Vector &maxs ) const
{
	fltx4 mins4 = LoadUnalignedSIMD( &mins.x );
	fltx4 minx = SplatXSIMD(mins4);
	fltx4 miny = SplatYSIMD(mins4);
	fltx4 minz = SplatZSIMD(mins4);
	fltx4 maxs4 = LoadUnalignedSIMD( &maxs.x );
	fltx4 maxx = SplatXSIMD(maxs4);
	fltx4 maxy = SplatYSIMD(maxs4);
	fltx4 maxz = SplatZSIMD(maxs4);

	// compute the dot product of the normal and the farthest corner
	// dotBack0 = DotProduct( normal, normals.x < 0 ? mins.x : maxs.x );
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = MulSIMD( planes[i].nX, MaskedAssign( planes[i].xSign, minx, maxx ) );
		fltx4 yTotalBack = MulSIMD( planes[i].nY, MaskedAssign( planes[i].ySign, miny, maxy ) );
		fltx4 zTotalBack = MulSIMD( planes[i].nZ, MaskedAssign( planes[i].zSign, minz, maxz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		if  ( IsVector4LessThan( dotBack, planes[i].dist ) )
			return true;
	}
	return false;
}

bool Frustum_t::CullBox( const fltx4 &mins4, const fltx4 &maxs4 ) const
{
	fltx4 minx = SplatXSIMD(mins4);
	fltx4 miny = SplatYSIMD(mins4);
	fltx4 minz = SplatZSIMD(mins4);
	fltx4 maxx = SplatXSIMD(maxs4);
	fltx4 maxy = SplatYSIMD(maxs4);
	fltx4 maxz = SplatZSIMD(maxs4);

	// compute the dot product of the normal and the farthest corner
	// dotBack0 = DotProduct( normal, normals.x < 0 ? mins.x : maxs.x );
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = MulSIMD( planes[i].nX, MaskedAssign( planes[i].xSign, minx, maxx ) );
		fltx4 yTotalBack = MulSIMD( planes[i].nY, MaskedAssign( planes[i].ySign, miny, maxy ) );
		fltx4 zTotalBack = MulSIMD( planes[i].nZ, MaskedAssign( planes[i].zSign, minz, maxz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		if  ( IsVector4LessThan( dotBack, planes[i].dist ) )
			return true;
	}
	return false;
}

bool Frustum_t::CullBoxCenterExtents( const Vector &center, const Vector &extents ) const
{
	fltx4 center4 = LoadUnalignedSIMD( &center.x );
	fltx4 centerx = SplatXSIMD(center4);
	fltx4 centery = SplatYSIMD(center4);
	fltx4 centerz = SplatZSIMD(center4);
	fltx4 extents4 = LoadUnalignedSIMD( &extents.x );
	fltx4 extx = SplatXSIMD(extents4);
	fltx4 exty = SplatYSIMD(extents4);
	fltx4 extz = SplatZSIMD(extents4);

	// compute the dot product of the normal and the farthest corner
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = AddSIMD( MulSIMD( planes[i].nX, centerx ), MulSIMD(planes[i].nXAbs, extx ) );
		fltx4 yTotalBack = AddSIMD( MulSIMD( planes[i].nY, centery ), MulSIMD(planes[i].nYAbs, exty ) );
		fltx4 zTotalBack = AddSIMD( MulSIMD( planes[i].nZ, centerz ), MulSIMD(planes[i].nZAbs, extz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		if  ( IsVector4LessThan( dotBack, planes[i].dist ) )
			return true;
	}
	return false;
}


bool Frustum_t::CullBoxCenterExtents( const fltx4 &fl4Center, const fltx4 &fl4Extents ) const
{
	fltx4 centerx = SplatXSIMD(fl4Center);
	fltx4 centery = SplatYSIMD(fl4Center);
	fltx4 centerz = SplatZSIMD(fl4Center);
	fltx4 extx = SplatXSIMD(fl4Extents);
	fltx4 exty = SplatYSIMD(fl4Extents);
	fltx4 extz = SplatZSIMD(fl4Extents);

	// compute the dot product of the normal and the farthest corner
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = AddSIMD( MulSIMD( planes[i].nX, centerx ), MulSIMD(planes[i].nXAbs, extx ) );
		fltx4 yTotalBack = AddSIMD( MulSIMD( planes[i].nY, centery ), MulSIMD(planes[i].nYAbs, exty ) );
		fltx4 zTotalBack = AddSIMD( MulSIMD( planes[i].nZ, centerz ), MulSIMD(planes[i].nZAbs, extz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		if  ( IsVector4LessThan( dotBack, planes[i].dist ) )
			return true;
	}
	return false;
}

// Brute force SAT frustum intersection between two frustums
bool Frustum_t::Intersects( Frustum_t &otherFrustum ) const
{
	Vector pPointsA[8];
	bool bResult = false;
	bResult = GetCorners( pPointsA );
	Assert( bResult );
	VPlane pPlanesA[FRUSTUM_NUMPLANES];
	GetPlanes( pPlanesA );

	Vector pPointsB[8];
	bResult = otherFrustum.GetCorners( pPointsB );
	Assert( bResult );
	VPlane pPlanesB[FRUSTUM_NUMPLANES];
	otherFrustum.GetPlanes( pPlanesB );

	// See if all points in B are on one side of any plane in A
	for ( int p=0; p<6; ++p )
	{
		bool bPointsOnOutside = true;
		for ( int i=0; i<8; ++i )
		{
			float flDist = pPlanesA[ p ].DistTo( pPointsB[ i ] );

			// If dist is pos, we are not on the outside
			if ( flDist > 0 )
			{
				bPointsOnOutside = false;
				break;
			}
		}

		// We never hit a negative case, we have a separating axis
		if ( bPointsOnOutside )
		{
			return false;
		}
	}

	// See if all points in A are on one side of any plane in B
	for ( int p=0; p<6; ++p )
	{
		bool bPointsOnOutside = true;
		for ( int i=0; i<8; ++i )
		{
			float flDist = pPlanesB[ p ].DistTo( pPointsA[ i ] );

			// If dist is pos, we are not on the outside
			if ( flDist > 0 )
			{
				bPointsOnOutside = false;
				break;
			}
		}

		// We never hit a negative case, we have a separating axis
		if ( bPointsOnOutside )
		{
			return false;
		}
	}

	// They intersect
	return true;
}

// Return true if this bounding volume intersects the frustum, false if it is outside
bool Frustum_t::Intersects( const Vector &mins, const Vector &maxs ) const
{
	fltx4 mins4 = LoadUnalignedSIMD( &mins.x );
	fltx4 minx = SplatXSIMD(mins4);
	fltx4 miny = SplatYSIMD(mins4);
	fltx4 minz = SplatZSIMD(mins4);
	fltx4 maxs4 = LoadUnalignedSIMD( &maxs.x );
	fltx4 maxx = SplatXSIMD(maxs4);
	fltx4 maxy = SplatYSIMD(maxs4);
	fltx4 maxz = SplatZSIMD(maxs4);

	// compute the dot product of the normal and the farthest corner
	// dotBack0 = DotProduct( normal, normals.x < 0 ? mins.x : maxs.x );
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = MulSIMD( planes[i].nX, MaskedAssign( planes[i].xSign, minx, maxx ) );
		fltx4 yTotalBack = MulSIMD( planes[i].nY, MaskedAssign( planes[i].ySign, miny, maxy ) );
		fltx4 zTotalBack = MulSIMD( planes[i].nZ, MaskedAssign( planes[i].zSign, minz, maxz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		fltx4 isOut = CmpLtSIMD( dotBack, planes[i].dist );
		if ( IsAnyNegative(isOut) )
			return false;
	}
	return true;
}

bool Frustum_t::Intersects( const fltx4 &mins4, const fltx4 &maxs4 ) const
{
	fltx4 minx = SplatXSIMD(mins4);
	fltx4 miny = SplatYSIMD(mins4);
	fltx4 minz = SplatZSIMD(mins4);
	fltx4 maxx = SplatXSIMD(maxs4);
	fltx4 maxy = SplatYSIMD(maxs4);
	fltx4 maxz = SplatZSIMD(maxs4);

	// compute the dot product of the normal and the farthest corner
	// dotBack0 = DotProduct( normal, normals.x < 0 ? mins.x : maxs.x );
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = MulSIMD( planes[i].nX, MaskedAssign( planes[i].xSign, minx, maxx ) );
		fltx4 yTotalBack = MulSIMD( planes[i].nY, MaskedAssign( planes[i].ySign, miny, maxy ) );
		fltx4 zTotalBack = MulSIMD( planes[i].nZ, MaskedAssign( planes[i].zSign, minz, maxz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		fltx4 isOut = CmpLtSIMD( dotBack, planes[i].dist );
		if ( IsAnyNegative(isOut) )
			return false;
	}
	return true;
}

bool Frustum_t::IntersectsCenterExtents( const Vector &center, const Vector &extents ) const
{
	fltx4 center4 = LoadUnalignedSIMD( &center.x );
	fltx4 centerx = SplatXSIMD(center4);
	fltx4 centery = SplatYSIMD(center4);
	fltx4 centerz = SplatZSIMD(center4);
	fltx4 extents4 = LoadUnalignedSIMD( &extents.x );
	fltx4 extx = SplatXSIMD(extents4);
	fltx4 exty = SplatYSIMD(extents4);
	fltx4 extz = SplatZSIMD(extents4);

	// compute the dot product of the normal and the farthest corner
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = AddSIMD( MulSIMD( planes[i].nX, centerx ), MulSIMD(planes[i].nXAbs, extx ) );
		fltx4 yTotalBack = AddSIMD( MulSIMD( planes[i].nY, centery ), MulSIMD(planes[i].nYAbs, exty ) );
		fltx4 zTotalBack = AddSIMD( MulSIMD( planes[i].nZ, centerz ), MulSIMD(planes[i].nZAbs, extz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		fltx4 isOut = CmpLtSIMD( dotBack, planes[i].dist );
		if ( IsAnyNegative(isOut) )
			return false;
	}
	return true;
}


bool Frustum_t::IntersectsCenterExtents( const fltx4 &fl4Center, const fltx4 &fl4Extents ) const
{
	fltx4 centerx = SplatXSIMD(fl4Center);
	fltx4 centery = SplatYSIMD(fl4Center);
	fltx4 centerz = SplatZSIMD(fl4Center);
	fltx4 extx = SplatXSIMD(fl4Extents);
	fltx4 exty = SplatYSIMD(fl4Extents);
	fltx4 extz = SplatZSIMD(fl4Extents);

	// compute the dot product of the normal and the farthest corner
	for ( int i = 0; i < 2; i++ )
	{
		fltx4 xTotalBack = AddSIMD( MulSIMD( planes[i].nX, centerx ), MulSIMD(planes[i].nXAbs, extx ) );
		fltx4 yTotalBack = AddSIMD( MulSIMD( planes[i].nY, centery ), MulSIMD(planes[i].nYAbs, exty ) );
		fltx4 zTotalBack = AddSIMD( MulSIMD( planes[i].nZ, centerz ), MulSIMD(planes[i].nZAbs, extz ) );
		fltx4 dotBack = AddSIMD( xTotalBack, AddSIMD(yTotalBack, zTotalBack) );
		// if plane of the farthest corner is behind the plane, then the box is completely outside this plane
		fltx4 isOut = CmpLtSIMD( dotBack, planes[i].dist );
		if ( IsAnyNegative(isOut) )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Generate a frustum based on perspective view parameters
//-----------------------------------------------------------------------------
void GeneratePerspectiveFrustumFLU( const Vector& origin, const Vector &forward, 
	const Vector &vLeft, const Vector &up, float flZNear, float flZFar, 
	float flFovX, float flAspect, VPlane *pPlanesOut )
{
	// YUP_ACTIVE: FIXME : This is actually producing incorrect planes (see the VectorMA below)
	Vector vRight = vLeft;
	vRight *= -1.0f;

	float flIntercept = DotProduct( origin, forward );

	// Setup the near and far planes.
	pPlanesOut[FRUSTUM_FARZ].Init( -forward, -flZFar - flIntercept );
	pPlanesOut[FRUSTUM_NEARZ].Init( forward, flZNear + flIntercept );

	flFovX *= 0.5f;

	float flTanX = tan( DEG2RAD( flFovX ) );
	float flTanY = flTanX / flAspect;

	// OPTIMIZE: Normalizing these planes is not necessary for culling
	Vector normalPos, normalNeg;

	// NOTE: This should be using left and not right to produce correct planes, not changing it quite yet
	// because I'm not able to test whether fixing this breaks anything.
	VectorMA( vRight, flTanX, forward, normalPos );
	VectorMA( normalPos, -2.0f, vRight, normalNeg );

	VectorNormalize( normalPos );
	VectorNormalize( normalNeg );

	pPlanesOut[FRUSTUM_LEFT].Init( normalPos, normalPos.Dot( origin ) );
	pPlanesOut[FRUSTUM_RIGHT].Init( normalNeg, normalNeg.Dot( origin ) );

	VectorMA( up, flTanY, forward, normalPos );
	VectorMA( normalPos, -2.0f, up, normalNeg );

	VectorNormalize( normalPos );
	VectorNormalize( normalNeg );

	pPlanesOut[FRUSTUM_BOTTOM].Init( normalPos, normalPos.Dot( origin ) );
	pPlanesOut[FRUSTUM_TOP].Init( normalNeg, normalNeg.Dot( origin ) );
}

// Generate a frustum based on perspective view parameters
void Frustum_t::CreatePerspectiveFrustumFLU( const Vector &vOrigin, const Vector &vForward, 
	const Vector &vLeft, const Vector &vUp, float flZNear, float flZFar, 
	float flFovX, float flAspect )
{
	VPlane planes[FRUSTUM_NUMPLANES];
	GeneratePerspectiveFrustumFLU( vOrigin, vForward, vLeft, vUp, flZNear, flZFar, flFovX, flAspect, planes );
	SetPlanes( planes );
}

//#ifndef YUP_ACTIVE
void Frustum_t::CreatePerspectiveFrustum( const Vector& origin, const Vector &forward, 
	const Vector &right, const Vector &up, float flZNear, float flZFar, 
	float flFovX, float flAspect )
{
	Vector vLeft = right;
	vLeft *= -1.0f;
	CreatePerspectiveFrustumFLU( origin, forward, vLeft, up, flZNear, flZFar, flFovX, flAspect );
}
//#endif

// Version that accepts angles instead of vectors
void Frustum_t::CreatePerspectiveFrustum( const Vector& origin, const QAngle &angles, float flZNear, float flZFar, float flFovX, float flAspectRatio )
{
	VPlane planes[FRUSTUM_NUMPLANES];
	Vector vecForward, vecLeft, vecUp;
	AngleVectorsFLU( angles, &vecForward, &vecLeft, &vecUp );
	GeneratePerspectiveFrustumFLU( origin, vecForward, vecLeft, vecUp, flZNear, flZFar, flFovX, flAspectRatio, planes );
	SetPlanes( planes );
}

// The points returned correspond to the corners of the frustum faces 
// Points 0 to 3 correspond to the near face 
// Points 4 to 7 correspond to the far face 
// Returns points in a face in this order:
//  2--3
//	|  |
//	0--1
bool Frustum_t::GetCorners( Vector *pPoints ) const
{
	VPlane planes[FRUSTUM_NUMPLANES];
	GetPlanes( planes );

	// Near face
	// Bottom Left
	if ( !PlaneIntersection( planes[FRUSTUM_NEARZ], planes[FRUSTUM_LEFT], planes[FRUSTUM_BOTTOM], pPoints[0] ) )
		return false;

	// Bottom right
	if ( !PlaneIntersection( planes[FRUSTUM_NEARZ], planes[FRUSTUM_RIGHT], planes[FRUSTUM_BOTTOM], pPoints[1] ) )
		return false;

	// Upper Left
	if ( !PlaneIntersection( planes[FRUSTUM_NEARZ], planes[FRUSTUM_LEFT], planes[FRUSTUM_TOP], pPoints[2] ) )
		return false;

	// Upper right
	if ( !PlaneIntersection( planes[FRUSTUM_NEARZ], planes[FRUSTUM_RIGHT], planes[FRUSTUM_TOP], pPoints[3] ) )
		return false;

	// Far face
	// Bottom Left
	if ( !PlaneIntersection( planes[FRUSTUM_FARZ], planes[FRUSTUM_LEFT], planes[FRUSTUM_BOTTOM], pPoints[4] ) )
		return false;

	// Bottom right
	if ( !PlaneIntersection( planes[FRUSTUM_FARZ], planes[FRUSTUM_RIGHT], planes[FRUSTUM_BOTTOM], pPoints[5] ) )
		return false;

	// Upper Left
	if ( !PlaneIntersection( planes[FRUSTUM_FARZ], planes[FRUSTUM_LEFT], planes[FRUSTUM_TOP], pPoints[6] ) )
		return false;

	// Upper right
	if ( !PlaneIntersection( planes[FRUSTUM_FARZ], planes[FRUSTUM_RIGHT], planes[FRUSTUM_TOP], pPoints[7] ) )
		return false;


	return true;
}

// NOTE: This routine was taken (and modified) from NVidia's BlinnReflection demo
// Creates basis vectors, based on a vertex and index list.
// See the NVidia white paper 'GDC2K PerPixel Lighting' for a description
// of how this computation works
#define SMALL_FLOAT 1e-12

void CalcTriangleTangentSpace( const Vector &p0, const Vector &p1, const Vector &p2,
							   const Vector2D &t0, const Vector2D &t1, const Vector2D& t2,
							   Vector &sVect, Vector &tVect )
{
	/* Compute the partial derivatives of X, Y, and Z with respect to S and T. */
	sVect.Init( 0.0f, 0.0f, 0.0f );
	tVect.Init( 0.0f, 0.0f, 0.0f );

	// x, s, t
	Vector edge01( p1.x - p0.x, t1.x - t0.x, t1.y - t0.y );
	Vector edge02( p2.x - p0.x, t2.x - t0.x, t2.y - t0.y );

	Vector cross;
	CrossProduct( edge01, edge02, cross );
	if ( fabs( cross.x ) > SMALL_FLOAT )
	{
		sVect.x += -cross.y / cross.x;
		tVect.x += -cross.z / cross.x;
	}

	// y, s, t
	edge01.Init( p1.y - p0.y, t1.x - t0.x, t1.y - t0.y );
	edge02.Init( p2.y - p0.y, t2.x - t0.x, t2.y - t0.y );

	CrossProduct( edge01, edge02, cross );
	if ( fabs( cross.x ) > SMALL_FLOAT )
	{
		sVect.y += -cross.y / cross.x;
		tVect.y += -cross.z / cross.x;
	}

	// z, s, t
	edge01.Init( p1.z - p0.z, t1.x - t0.x, t1.y - t0.y );
	edge02.Init( p2.z - p0.z, t2.x - t0.x, t2.y - t0.y );

	CrossProduct( edge01, edge02, cross );
	if( fabs( cross.x ) > SMALL_FLOAT )
	{
		sVect.z += -cross.y / cross.x;
		tVect.z += -cross.z / cross.x;
	}

	// Normalize sVect and tVect
	VectorNormalize( sVect );
	VectorNormalize( tVect );
}














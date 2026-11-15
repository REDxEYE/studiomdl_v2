//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#if !defined(_STATIC_LINKED) || defined(_SHARED_LIB)

#include "tier0/basetypes.h"
#include "mathlib/vmatrix.h"
#include "mathlib/mathlib.h"
#include <string.h>
#include "mathlib/vector4d.h"
#include "ssemath.h"




#pragma warning (disable : 4700) // local variable 'x' used without having been initialized

// ------------------------------------------------------------------------------------------- //
// Helper functions.
// ------------------------------------------------------------------------------------------- //

#ifndef VECTOR_NO_SLOW_OPERATIONS


VMatrix SetupMatrixOrgAngles(const Vector &origin, const QAngle &vAngles)
{
	VMatrix mRet;
	mRet.SetupMatrixOrgAngles( origin, vAngles );
	return mRet;
}

#endif // VECTOR_NO_SLOW_OPERATIONS

#if 1
bool PlaneIntersection( const VPlane &vp1, const VPlane &vp2, const VPlane &vp3, Vector &vOut )
{
	Vector v2Cross3 = CrossProduct( vp2.m_Normal, vp3.m_Normal );
	float flDenom = DotProduct( vp1.m_Normal, v2Cross3 );
	if ( fabs( flDenom ) < FLT_EPSILON )
		return false;
	Vector vRet = vp1.m_Dist * v2Cross3 + vp2.m_Dist * CrossProduct( vp3.m_Normal, vp1.m_Normal ) + vp3.m_Dist * CrossProduct( vp1.m_Normal, vp2.m_Normal );
	vOut = vRet * ( 1.0 / flDenom );
	return true;
}
#else  // old slow innaccurate code
bool PlaneIntersection( const VPlane &vp1, const VPlane &vp2, const VPlane &vp3, Vector &vOut )
{
	VMatrix mMat, mInverse;

	mMat.Init(
		vp1.m_Normal.x, vp1.m_Normal.y, vp1.m_Normal.z, -vp1.m_Dist,
		vp2.m_Normal.x, vp2.m_Normal.y, vp2.m_Normal.z, -vp2.m_Dist,
		vp3.m_Normal.x, vp3.m_Normal.y, vp3.m_Normal.z, -vp3.m_Dist,
		0.0f, 0.0f, 0.0f, 1.0f
		);
	if(mMat.InverseGeneral(mInverse))
	{
		//vOut = mInverse * Vector(0.0f, 0.0f, 0.0f);
		mInverse.GetTranslation( vOut );
		return true;
	}
	else
	{
		return false;
	}
}
#endif


// ------------------------------------------------------------------------------------------- //
// VMatrix functions.
// ------------------------------------------------------------------------------------------- //

VMatrix& VMatrix::operator=(const VMatrix &mOther)
{
	m[0][0] = mOther.m[0][0];
	m[0][1] = mOther.m[0][1];
	m[0][2] = mOther.m[0][2];
	m[0][3] = mOther.m[0][3];

	m[1][0] = mOther.m[1][0];
	m[1][1] = mOther.m[1][1];
	m[1][2] = mOther.m[1][2];
	m[1][3] = mOther.m[1][3];

	m[2][0] = mOther.m[2][0];
	m[2][1] = mOther.m[2][1];
	m[2][2] = mOther.m[2][2];
	m[2][3] = mOther.m[2][3];

	m[3][0] = mOther.m[3][0];
	m[3][1] = mOther.m[3][1];
	m[3][2] = mOther.m[3][2];
	m[3][3] = mOther.m[3][3];

	return *this;
}

bool VMatrix::operator==( const VMatrix& src ) const
{
	return !memcmp( src.m, m, sizeof(m) );
}

void VMatrix::MatrixMul( const VMatrix &vm, VMatrix &out ) const
{
	out.Init(
		m[0][0]*vm.m[0][0] + m[0][1]*vm.m[1][0] + m[0][2]*vm.m[2][0] + m[0][3]*vm.m[3][0],
		m[0][0]*vm.m[0][1] + m[0][1]*vm.m[1][1] + m[0][2]*vm.m[2][1] + m[0][3]*vm.m[3][1],
		m[0][0]*vm.m[0][2] + m[0][1]*vm.m[1][2] + m[0][2]*vm.m[2][2] + m[0][3]*vm.m[3][2],
		m[0][0]*vm.m[0][3] + m[0][1]*vm.m[1][3] + m[0][2]*vm.m[2][3] + m[0][3]*vm.m[3][3],

		m[1][0]*vm.m[0][0] + m[1][1]*vm.m[1][0] + m[1][2]*vm.m[2][0] + m[1][3]*vm.m[3][0],
		m[1][0]*vm.m[0][1] + m[1][1]*vm.m[1][1] + m[1][2]*vm.m[2][1] + m[1][3]*vm.m[3][1],
		m[1][0]*vm.m[0][2] + m[1][1]*vm.m[1][2] + m[1][2]*vm.m[2][2] + m[1][3]*vm.m[3][2],
		m[1][0]*vm.m[0][3] + m[1][1]*vm.m[1][3] + m[1][2]*vm.m[2][3] + m[1][3]*vm.m[3][3],

		m[2][0]*vm.m[0][0] + m[2][1]*vm.m[1][0] + m[2][2]*vm.m[2][0] + m[2][3]*vm.m[3][0],
		m[2][0]*vm.m[0][1] + m[2][1]*vm.m[1][1] + m[2][2]*vm.m[2][1] + m[2][3]*vm.m[3][1],
		m[2][0]*vm.m[0][2] + m[2][1]*vm.m[1][2] + m[2][2]*vm.m[2][2] + m[2][3]*vm.m[3][2],
		m[2][0]*vm.m[0][3] + m[2][1]*vm.m[1][3] + m[2][2]*vm.m[2][3] + m[2][3]*vm.m[3][3],

		m[3][0]*vm.m[0][0] + m[3][1]*vm.m[1][0] + m[3][2]*vm.m[2][0] + m[3][3]*vm.m[3][0],
		m[3][0]*vm.m[0][1] + m[3][1]*vm.m[1][1] + m[3][2]*vm.m[2][1] + m[3][3]*vm.m[3][1],
		m[3][0]*vm.m[0][2] + m[3][1]*vm.m[1][2] + m[3][2]*vm.m[2][2] + m[3][3]*vm.m[3][2],
		m[3][0]*vm.m[0][3] + m[3][1]*vm.m[1][3] + m[3][2]*vm.m[2][3] + m[3][3]*vm.m[3][3]
		);
}

#ifndef VECTOR_NO_SLOW_OPERATIONS

VMatrix VMatrix::operator*(const VMatrix &vm) const
{
	VMatrix ret;
	MatrixMul( vm, ret );
	return ret;
}

#endif

bool VMatrix::InverseGeneral(VMatrix &vInverse) const
{
	return MatrixInverseGeneral( *this, vInverse );
}


bool MatrixInverseGeneral(const VMatrix& src, VMatrix& dst)
{
	int iRow, i, j, iTemp, iTest;
	vec_t mul, fTest, fLargest;
	vec_t mat[4][8];
	int rowMap[4], iLargest;
	vec_t *pOut, *pRow, *pScaleRow;


	// How it's done.
	// AX = I
	// A = this
	// X = the matrix we're looking for
	// I = identity

	// Setup AI
	for(i=0; i < 4; i++)
	{
		const vec_t *pIn = src[i];
		pOut = mat[i];

		for(j=0; j < 4; j++)
		{
			pOut[j] = pIn[j];
		}

		pOut[4] = 0.0f;
		pOut[5] = 0.0f;
		pOut[6] = 0.0f;
		pOut[7] = 0.0f;
		pOut[i+4] = 1.0f;

		rowMap[i] = i;
	}

	// Use row operations to get to reduced row-echelon form using these rules:
	// 1. Multiply or divide a row by a nonzero number.
	// 2. Add a multiple of one row to another.
	// 3. Interchange two rows.

	for(iRow=0; iRow < 4; iRow++)
	{
		// Find the row with the largest element in this column.
		fLargest = 1e-6f;
		iLargest = -1;
		for(iTest=iRow; iTest < 4; iTest++)
		{
			fTest = (vec_t)FloatMakePositive(mat[rowMap[iTest]][iRow]);
			if(fTest > fLargest)
			{
				iLargest = iTest;
				fLargest = fTest;
			}
		}

		// They're all too small.. sorry.
		if(iLargest == -1)
		{
			return false;
		}

		// Swap the rows.
		iTemp = rowMap[iLargest];
		rowMap[iLargest] = rowMap[iRow];
		rowMap[iRow] = iTemp;

		pRow = mat[rowMap[iRow]];

		// Divide this row by the element.
		mul = 1.0f / pRow[iRow];
		for(j=0; j < 8; j++)
			pRow[j] *= mul;

		pRow[iRow] = 1.0f; // Preserve accuracy...
		
		// Eliminate this element from the other rows using operation 2.
		for(i=0; i < 4; i++)
		{
			if(i == iRow)
				continue;

			pScaleRow = mat[rowMap[i]];
		
			// Multiply this row by -(iRow*the element).
			mul = -pScaleRow[iRow];
			for(j=0; j < 8; j++)
			{
				pScaleRow[j] += pRow[j] * mul;
			}

			pScaleRow[iRow] = 0.0f; // Preserve accuracy...
		}
	}

	// The inverse is on the right side of AX now (the identity is on the left).
	for(i=0; i < 4; i++)
	{
		const vec_t *pIn = mat[rowMap[i]] + 4;
		pOut = dst.m[i];

		for(j=0; j < 4; j++)
		{
			pOut[j] = pIn[j];
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// Does a fast inverse, assuming the matrix only contains translation and rotation.
//-----------------------------------------------------------------------------
void MatrixInverseTR( const VMatrix& src, VMatrix &dst )
{
	Vector vTrans, vNewTrans;

	// Transpose the upper 3x3.
	dst.m[0][0] = src.m[0][0];  dst.m[0][1] = src.m[1][0]; dst.m[0][2] = src.m[2][0];
	dst.m[1][0] = src.m[0][1];  dst.m[1][1] = src.m[1][1]; dst.m[1][2] = src.m[2][1];
	dst.m[2][0] = src.m[0][2];  dst.m[2][1] = src.m[1][2]; dst.m[2][2] = src.m[2][2];

	// Transform the translation.
	vTrans.Init( -src.m[0][3], -src.m[1][3], -src.m[2][3] );
	Vector3DMultiply( dst, vTrans, vNewTrans );
	MatrixSetColumn( dst, 3, vNewTrans );

	// Fill in the bottom row.
	dst.m[3][0] = dst.m[3][1] = dst.m[3][2] = 0.0f;
	dst.m[3][3] = 1.0f;
}


void VMatrix::InverseTR( VMatrix &ret ) const
{
	MatrixInverseTR( *this, ret );
}

void MatrixInverseTranspose( const VMatrix& src, VMatrix& dst )
{
	src.InverseGeneral( dst );
	MatrixTranspose( dst, dst );
}

//-----------------------------------------------------------------------------
// Computes the inverse transpose
//-----------------------------------------------------------------------------
void MatrixInverseTranspose( const matrix3x4_t& src, matrix3x4_t& dst )
{
	VMatrix tmp, out;
	tmp.CopyFrom3x4( src );
	::MatrixInverseTranspose( tmp, out );
	out.Set3x4( dst );
}


#ifndef VECTOR_NO_SLOW_OPERATIONS

VMatrix VMatrix::InverseTR() const
{
	VMatrix ret;
	MatrixInverseTR( *this, ret );
	return ret;
}

VMatrix VMatrix::Transpose() const
{
	return VMatrix(
		m[0][0], m[1][0], m[2][0], m[3][0],
		m[0][1], m[1][1], m[2][1], m[3][1],
		m[0][2], m[1][2], m[2][2], m[3][2],
		m[0][3], m[1][3], m[2][3], m[3][3]);
}

#endif // VECTOR_NO_SLOW_OPERATIONS


void VMatrix::SetupMatrixOrgAngles( const Vector &origin, const QAngle &vAngles )
{
	float		sr, sp, sy, cr, cp, cy;

	SinCos( DEG2RAD( vAngles[YAW] ), &sy, &cy );
	SinCos( DEG2RAD( vAngles[PITCH] ), &sp, &cp );
	SinCos( DEG2RAD( vAngles[ROLL] ), &sr, &cr );

	// matrix = (YAW * PITCH) * ROLL
	m[0][0] = cp*cy;
	m[1][0] = cp*sy;
	m[2][0] = -sp;
	m[0][1] = sr*sp*cy+cr*-sy;
	m[1][1] = sr*sp*sy+cr*cy;
	m[2][1] = sr*cp;
	m[0][2] = (cr*sp*cy+-sr*-sy);
	m[1][2] = (cr*sp*sy+-sr*cy);
	m[2][2] = cr*cp;
	m[0][3] = 0.f;
	m[1][3] = 0.f;
	m[2][3] = 0.f;
	
	// Add translation
	m[0][3] = origin.x;
	m[1][3] = origin.y;
	m[2][3] = origin.z;
	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 0.0f;
	m[3][3] = 1.0f;
}


//-----------------------------------------------------------------------------
// Sets matrix to identity
//-----------------------------------------------------------------------------
void MatrixSetIdentity( VMatrix &dst )
{
	dst[0][0] = 1.0f; dst[0][1] = 0.0f; dst[0][2] = 0.0f; dst[0][3] = 0.0f;
	dst[1][0] = 0.0f; dst[1][1] = 1.0f; dst[1][2] = 0.0f; dst[1][3] = 0.0f;
	dst[2][0] = 0.0f; dst[2][1] = 0.0f; dst[2][2] = 1.0f; dst[2][3] = 0.0f;
	dst[3][0] = 0.0f; dst[3][1] = 0.0f; dst[3][2] = 0.0f; dst[3][3] = 1.0f;
}




//-----------------------------------------------------------------------------
// Transpose
//-----------------------------------------------------------------------------
inline void Swap( float& a, float& b )
{
	float tmp = a;
	a = b;
	b = tmp;
}

void MatrixTranspose( const VMatrix& src, VMatrix& dst )
{
	if (&src == &dst)
	{
		Swap( dst[0][1], dst[1][0] );
		Swap( dst[0][2], dst[2][0] );
		Swap( dst[0][3], dst[3][0] );
		Swap( dst[1][2], dst[2][1] );
		Swap( dst[1][3], dst[3][1] );
		Swap( dst[2][3], dst[3][2] );
	}
	else
	{
		dst[0][0] = src[0][0]; dst[0][1] = src[1][0]; dst[0][2] = src[2][0]; dst[0][3] = src[3][0];
		dst[1][0] = src[0][1]; dst[1][1] = src[1][1]; dst[1][2] = src[2][1]; dst[1][3] = src[3][1];
		dst[2][0] = src[0][2]; dst[2][1] = src[1][2]; dst[2][2] = src[2][2]; dst[2][3] = src[3][2];
		dst[3][0] = src[0][3]; dst[3][1] = src[1][3]; dst[3][2] = src[2][3]; dst[3][3] = src[3][3];
	}
}


//-----------------------------------------------------------------------------
// Matrix copy
//-----------------------------------------------------------------------------

void MatrixCopy( const VMatrix& src, VMatrix& dst )
{
	if (&src != &dst)
	{
		memcpy( dst.m, src.m, 16 * sizeof(float) );
	}
}

//-----------------------------------------------------------------------------
// Matrix multiply
//-----------------------------------------------------------------------------
typedef float VMatrixRaw_t[4];

void MatrixMultiply( const VMatrix& src1, const VMatrix& src2, VMatrix& dst )
{
	// Make sure it works if src1 == dst or src2 == dst
	VMatrix tmp1, tmp2;
	const VMatrixRaw_t* s1 = (&src1 == &dst) ? tmp1.m : src1.m;
	const VMatrixRaw_t* s2 = (&src2 == &dst) ? tmp2.m : src2.m;

	if (&src1 == &dst)
	{
		MatrixCopy( src1, tmp1 );
	}
	if (&src2 == &dst)
	{
		MatrixCopy( src2, tmp2 );
	}

	dst[0][0] = s1[0][0] * s2[0][0] + s1[0][1] * s2[1][0] + s1[0][2] * s2[2][0] + s1[0][3] * s2[3][0];
	dst[0][1] = s1[0][0] * s2[0][1] + s1[0][1] * s2[1][1] + s1[0][2] * s2[2][1] + s1[0][3] * s2[3][1];
	dst[0][2] = s1[0][0] * s2[0][2] + s1[0][1] * s2[1][2] + s1[0][2] * s2[2][2] + s1[0][3] * s2[3][2];
	dst[0][3] = s1[0][0] * s2[0][3] + s1[0][1] * s2[1][3] + s1[0][2] * s2[2][3] + s1[0][3] * s2[3][3];

	dst[1][0] = s1[1][0] * s2[0][0] + s1[1][1] * s2[1][0] + s1[1][2] * s2[2][0] + s1[1][3] * s2[3][0];
	dst[1][1] = s1[1][0] * s2[0][1] + s1[1][1] * s2[1][1] + s1[1][2] * s2[2][1] + s1[1][3] * s2[3][1];
	dst[1][2] = s1[1][0] * s2[0][2] + s1[1][1] * s2[1][2] + s1[1][2] * s2[2][2] + s1[1][3] * s2[3][2];
	dst[1][3] = s1[1][0] * s2[0][3] + s1[1][1] * s2[1][3] + s1[1][2] * s2[2][3] + s1[1][3] * s2[3][3];

	dst[2][0] = s1[2][0] * s2[0][0] + s1[2][1] * s2[1][0] + s1[2][2] * s2[2][0] + s1[2][3] * s2[3][0];
	dst[2][1] = s1[2][0] * s2[0][1] + s1[2][1] * s2[1][1] + s1[2][2] * s2[2][1] + s1[2][3] * s2[3][1];
	dst[2][2] = s1[2][0] * s2[0][2] + s1[2][1] * s2[1][2] + s1[2][2] * s2[2][2] + s1[2][3] * s2[3][2];
	dst[2][3] = s1[2][0] * s2[0][3] + s1[2][1] * s2[1][3] + s1[2][2] * s2[2][3] + s1[2][3] * s2[3][3];

	dst[3][0] = s1[3][0] * s2[0][0] + s1[3][1] * s2[1][0] + s1[3][2] * s2[2][0] + s1[3][3] * s2[3][0];
	dst[3][1] = s1[3][0] * s2[0][1] + s1[3][1] * s2[1][1] + s1[3][2] * s2[2][1] + s1[3][3] * s2[3][1];
	dst[3][2] = s1[3][0] * s2[0][2] + s1[3][1] * s2[1][2] + s1[3][2] * s2[2][2] + s1[3][3] * s2[3][2];
	dst[3][3] = s1[3][0] * s2[0][3] + s1[3][1] * s2[1][3] + s1[3][2] * s2[2][3] + s1[3][3] * s2[3][3];
}

//-----------------------------------------------------------------------------
// Matrix/vector multiply
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Matrix/vector multiply
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Matrix/vector multiply
//-----------------------------------------------------------------------------

void Vector3DMultiply( const VMatrix &src1, const Vector &src2, Vector &dst )
{
	// Make sure it works if src2 == dst
	Vector tmp;
	const Vector &v = (&src2 == &dst) ?  static_cast<const Vector>(tmp) : src2;

	if( &src2 == &dst )
	{
		VectorCopy( src2, tmp );
	}

	dst[0] = src1[0][0] * v[0] + src1[0][1] * v[1] + src1[0][2] * v[2];
	dst[1] = src1[1][0] * v[0] + src1[1][1] * v[1] + src1[1][2] * v[2];
	dst[2] = src1[2][0] * v[0] + src1[2][1] * v[1] + src1[2][2] * v[2];
}


//-----------------------------------------------------------------------------
// Vector3DMultiplyPositionProjective treats src2 as if it's a point 
// and does the perspective divide at the end
//-----------------------------------------------------------------------------
void Vector3DMultiplyPositionProjective( const VMatrix& src1, const Vector &src2, Vector& dst )
{
	// Make sure it works if src2 == dst
	Vector tmp;
	const Vector &v = (&src2 == &dst) ? static_cast<const Vector>(tmp): src2;
	if( &src2 == &dst )
	{
		VectorCopy( src2, tmp );
	}

	float w = src1[3][0] * v[0] + src1[3][1] * v[1] + src1[3][2] * v[2] + src1[3][3];
	if ( w != 0.0f ) 
	{
		w = 1.0f / w;
	}

	dst[0] = src1[0][0] * v[0] + src1[0][1] * v[1] + src1[0][2] * v[2] + src1[0][3];
	dst[1] = src1[1][0] * v[0] + src1[1][1] * v[1] + src1[1][2] * v[2] + src1[1][3];
	dst[2] = src1[2][0] * v[0] + src1[2][1] * v[1] + src1[2][2] * v[2] + src1[2][3];
	dst *= w;
}


//-----------------------------------------------------------------------------
// Vector3DMultiplyProjective treats src2 as if it's a direction 
// and does the perspective divide at the end
//-----------------------------------------------------------------------------
void Vector3DMultiplyProjective( const VMatrix& src1, const Vector &src2, Vector& dst )
{
	// Make sure it works if src2 == dst
	Vector tmp;
	const Vector &v = (&src2 == &dst) ? static_cast<const Vector>(tmp) : src2;
	if( &src2 == &dst )
	{
		VectorCopy( src2, tmp );
	}

	float w;
	dst[0] = src1[0][0] * v[0] + src1[0][1] * v[1] + src1[0][2] * v[2];
	dst[1] = src1[1][0] * v[0] + src1[1][1] * v[1] + src1[1][2] * v[2];
	dst[2] = src1[2][0] * v[0] + src1[2][1] * v[1] + src1[2][2] * v[2];
	w = src1[3][0] * v[0] + src1[3][1] * v[1] + src1[3][2] * v[2];
	if (w != 0.0f)
	{
		dst /= w;
	}
	else
	{
		dst = vec3_origin;
	}
}




#ifndef VECTOR_NO_SLOW_OPERATIONS

VPlane VMatrix::operator*(const VPlane &thePlane) const
{
	VPlane ret;
	TransformPlane( thePlane, ret );
	return ret;
}

#endif


//-----------------------------------------------------------------------------
// Builds a rotation matrix that rotates one direction vector into another
//-----------------------------------------------------------------------------
void MatrixBuildTranslation( VMatrix& dst, float x, float y, float z )
{
	MatrixSetIdentity( dst );
	dst[0][3] = x;
	dst[1][3] = y;
	dst[2][3] = z;
}

void MatrixBuildTranslation( VMatrix& dst, const Vector &translation )
{
	MatrixSetIdentity( dst );
	dst[0][3] = translation[0];
	dst[1][3] = translation[1];
	dst[2][3] = translation[2];
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
void MatrixBuildRotationAboutAxis( VMatrix &dst, const Vector &vAxisOfRot, float angleDegrees )
{
	MatrixBuildRotationAboutAxis( vAxisOfRot, angleDegrees, dst.As3x4() );
	dst[3][0] = 0;
	dst[3][1] = 0;
	dst[3][2] = 0;
	dst[3][3] = 1;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void MatrixBuildRotateZ( VMatrix &dst, float angleDegrees )
{
	float radians = angleDegrees * ( M_PI / 180.0f );

	float fSin = ( float )sin( radians );
	float fCos = ( float )cos( radians );

	dst[0][0] = fCos; dst[0][1] = -fSin; dst[0][2] = 0.0f; dst[0][3] = 0.0f;
	dst[1][0] = fSin; dst[1][1] =  fCos; dst[1][2] = 0.0f; dst[1][3] = 0.0f;
	dst[2][0] = 0.0f; dst[2][1] =  0.0f; dst[2][2] = 1.0f; dst[2][3] = 0.0f;
	dst[3][0] = 0.0f; dst[3][1] =  0.0f; dst[3][2] = 0.0f; dst[3][3] = 1.0f;
}

// Builds a scale matrix
void MatrixBuildScale( VMatrix &dst, float x, float y, float z )
{
	dst[0][0] = x;		dst[0][1] = 0.0f;	dst[0][2] = 0.0f;	dst[0][3] = 0.0f;
	dst[1][0] = 0.0f;	dst[1][1] = y;		dst[1][2] = 0.0f;	dst[1][3] = 0.0f;
	dst[2][0] = 0.0f;	dst[2][1] = 0.0f;	dst[2][2] = z;		dst[2][3] = 0.0f;
	dst[3][0] = 0.0f;	dst[3][1] = 0.0f;	dst[3][2] = 0.0f;	dst[3][3] = 1.0f;
}

void MatrixBuildScale( VMatrix &dst, const Vector& scale )
{
	MatrixBuildScale( dst, scale.x, scale.y, scale.z );
}

static inline void CalculateAABBForNormalizedFrustum_Helper( float x, float y, float z, const VMatrix &volumeToWorld, Vector &mins, Vector &maxs )
{
	Vector volumeSpacePos( x, y, z );

	// Make sure it's been clipped
	Assert( volumeSpacePos[0] >= -1e-3f );
	Assert( volumeSpacePos[0] - 1.0f <= 1e-3f );
	Assert( volumeSpacePos[1] >= -1e-3f );
	Assert( volumeSpacePos[1] - 1.0f <= 1e-3f );
	Assert( volumeSpacePos[2] >= -1e-3f );
	Assert( volumeSpacePos[2] - 1.0f <= 1e-3f );

	Vector worldPos;
	Vector3DMultiplyPositionProjective( volumeToWorld, volumeSpacePos, worldPos );
	AddPointToBounds( worldPos, mins, maxs );
}


static inline void FrustumPlanesFromMatrixHelper( const VMatrix &shadowToWorld, const Vector &p1, const Vector &p2, const Vector &p3, VPlane &plane )
{
	Vector world1, world2, world3;
	Vector3DMultiplyPositionProjective( shadowToWorld, p1, world1 );
	Vector3DMultiplyPositionProjective( shadowToWorld, p2, world2 );
	Vector3DMultiplyPositionProjective( shadowToWorld, p3, world3 );

	Vector v1, v2;
	VectorSubtract( world2, world1, v1 );
	VectorSubtract( world3, world1, v2 );

	CrossProduct( v1, v2, plane.m_Normal );
	VectorNormalize( plane.m_Normal );
	plane.m_Dist = DotProduct( plane.m_Normal, world1 );	
}

// BEWARE: top/bottom are FLIPPED relative to D3DXMatrixOrthoOffCenterRH().
void MatrixBuildOrtho( VMatrix& dst, double left, double top, double right, double bottom, double zNear, double zFar )
{
	// FIXME: This is being used incorrectly! Should read:
	// D3DXMatrixOrthoOffCenterRH( &matrix, left, right, bottom, top, zNear, zFar );
	// Which is certainly why we need these extra -1 scales in y. Bleah

	// NOTE: The camera can be imagined as the following diagram:
	//		/z
	//	   /
	//	  /____ x	Z is going into the screen
	//	  |
	//	  |
	//	  |y
	//
	// (0,0,z) represents the upper-left corner of the screen.
	// Our projection transform needs to transform from this space to a LH coordinate
	// system that looks thusly:
	// 
	//	y|  /z
	//	 | /
	//	 |/____ x	Z is going into the screen
	//
	// Where x,y lies between -1 and 1, and z lies from 0 to 1
	// This is because the viewport transformation from projection space to pixels
	// introduces a -1 scale in the y coordinates
	//		D3DXMatrixOrthoOffCenterRH( &matrix, left, right, top, bottom, zNear, zFar );

	dst.Init(	 2.0f / ( right - left ),						0.0f,						0.0f, ( left + right ) / ( left - right ),
				0.0f,	 2.0f / ( bottom - top ),						0.0f, ( bottom + top ) / ( top - bottom ),
				0.0f,						0.0f,	 1.0f / ( zNear - zFar ),			 zNear / ( zNear - zFar ),
				0.0f,						0.0f,						0.0f,								1.0f );
}

void MatrixBuildPerspectiveX( VMatrix& dst, double flFovX, double flAspect, double flZNear, double flZFar )
{
	float flWidth = 2.0f * flZNear * tanf( flFovX * M_PI / 360.0f );
	float flHeight = flWidth / flAspect;
	dst.Init(   2.0f * flZNear / flWidth,						0.0f,							0.0f,										0.0f,
				0.0f,  2.0f  * flZNear/ flHeight,							0.0f,										0.0f,
				0.0f,						0.0f,  flZFar / ( flZNear - flZFar ),	 flZNear * flZFar / ( flZNear - flZFar ),
				0.0f,						0.0f,						   -1.0f,										0.0f );
}

void MatrixBuildPerspectiveOffCenterX( VMatrix& dst, double flFovX, double flAspect, double flZNear, double flZFar, double bottom, double top, double left, double right )
{
	float flWidth = 2.0f * flZNear * tanf( flFovX * M_PI / 360.0f );
	float flHeight = flWidth / flAspect;

	// bottom, top, left, right are 0..1 so convert to -<val>/2..<val>/2
	float flLeft   = -(flWidth/2.0f)  * (1.0f - left)   + left   * (flWidth/2.0f);
	float flRight  = -(flWidth/2.0f)  * (1.0f - right)  + right  * (flWidth/2.0f);
	float flBottom = -(flHeight/2.0f) * (1.0f - bottom) + bottom * (flHeight/2.0f);
	float flTop    = -(flHeight/2.0f) * (1.0f - top)    + top    * (flHeight/2.0f);

	dst.Init(   (2.0f * flZNear) / (flRight-flLeft),                           0.0f, (flLeft+flRight)/(flRight-flLeft),                            0.0f,
				0.0f,  2.0f*flZNear/(flTop-flBottom), (flTop+flBottom)/(flTop-flBottom),                            0.0f,
				0.0f,                           0.0f,           flZFar/(flZNear-flZFar),  flZNear*flZFar/(flZNear-flZFar),
				0.0f,                           0.0f,                             -1.0f,                            0.0f );
}

#endif // !_STATIC_LINKED || _SHARED_LIB


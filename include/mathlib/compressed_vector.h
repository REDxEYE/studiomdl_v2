//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#pragma once



#include <math.h>
#include <float.h>

// For vec_t, put this somewhere else?
#include "tier0/basetypes.h"

// For rand(). We really need a library!


#include "mathlib/vector.h"


#ifdef OSX
#pragma GCC diagnostic ignored "-Wtautological-compare"
#endif

class Quaternion48;


//=========================================================
// fit a 3D vector into 32 bits
//=========================================================



//=========================================================
// Fit a unit vector into 32 bits
//=========================================================




//=========================================================
// 64 bit Quaternion
//=========================================================

class Quaternion64
{
public:
	// Construction/destruction:
	Quaternion64(void); 
	Quaternion64(vec_t X, vec_t Y, vec_t Z);

	// assignment
	// Quaternion& operator=(const Quaternion64 &vOther);
	Quaternion64& operator=(const Quaternion &vOther);
	operator Quaternion () const;

private:
	Quaternion64(const uint64_t xx, const uint64_t yy, const uint64_t zz, const uint64_t ww ) : x(xx), y(yy), z(zz), wneg(ww) {}; // stricly for static construction
	uint64_t x:21;
	uint64_t y:21;
	uint64_t z:21;
	uint64_t wneg:1;
};


inline Quaternion64::operator Quaternion ()	 const
{
	Quaternion tmp;

	// shift to -1048576, + 1048575, then round down slightly to -1.0 < x < 1.0
 	tmp.x = ((int)x - 1048576) * (1 / 1048576.5f);
 	tmp.y = ((int)y - 1048576) * (1 / 1048576.5f);
 	tmp.z = ((int)z - 1048576) * (1 / 1048576.5f);

	tmp.w = sqrt( 1 - tmp.x * tmp.x - tmp.y * tmp.y - tmp.z * tmp.z );
	if (wneg)
		tmp.w = -tmp.w;
	return tmp; 
}

inline Quaternion64& Quaternion64::operator=(const Quaternion &vOther)	
{
	CHECK_VALID(vOther);

	x = clamp( (int)(vOther.x * 1048576) + 1048576, 0, 2097151 );
	y = clamp( (int)(vOther.y * 1048576) + 1048576, 0, 2097151 );
	z = clamp( (int)(vOther.z * 1048576) + 1048576, 0, 2097151 );
	wneg = (vOther.w < 0);
	return *this; 
}

//=========================================================
// 48 bit Quaternion
//=========================================================

class Quaternion48
{
public:
	// Construction/destruction:
	Quaternion48(void); 
	Quaternion48(vec_t X, vec_t Y, vec_t Z);

	// assignment
	// Quaternion& operator=(const Quaternion48 &vOther);
	Quaternion48& operator=(const Quaternion &vOther);
	operator Quaternion () const;

//private:
	unsigned short x:16;
	unsigned short y:16;
	unsigned short z:15;
	unsigned short wneg:1;
};


inline Quaternion48::operator Quaternion ()	const
{

	Quaternion tmp;

	tmp.x = ((int)x - 32768) * (1 / 32768.5);
	tmp.y = ((int)y - 32768) * (1 / 32768.5);
	tmp.z = ((int)z - 16384) * (1 / 16384.5);
	tmp.w = sqrt( 1 - tmp.x * tmp.x - tmp.y * tmp.y - tmp.z * tmp.z );
	if (wneg)
		tmp.w = -tmp.w;
	return tmp; 

}


inline Quaternion48& Quaternion48::operator=(const Quaternion &vOther)	
{
	CHECK_VALID(vOther);

	x = clamp( (int)(vOther.x * 32768) + 32768, 0, 65535 );
	y = clamp( (int)(vOther.y * 32768) + 32768, 0, 65535 );
	z = clamp( (int)(vOther.z * 16384) + 16384, 0, 32767 );
	wneg = (vOther.w < 0);
	return *this; 
}


//=========================================================
// 48 bit sorted Quaternion
//=========================================================


class Quaternion48S
{
public:
	// Construction/destruction:
	Quaternion48S(void); 
	Quaternion48S(vec_t X, vec_t Y, vec_t Z);

	// assignment
	// Quaternion& operator=(const Quaternion48 &vOther);
	Quaternion48S& operator=(const Quaternion &vOther);
	operator Quaternion () const;
//private:
	// shift the quaternion so that the largest value is recreated by the sqrt()
	// abcd maps modulo into quaternion xyzw starting at "offset"
	// "offset" is split into two 1 bit fields so that the data packs into 6 bytes (3 shorts)
	unsigned short a:15;		// first of the 3 consecutive smallest quaternion elements 
	unsigned short offsetH:1;	// high bit of "offset"
	unsigned short b:15;
	unsigned short offsetL:1;	// low bit of "offset"
	unsigned short c:15;
	unsigned short dneg:1;		// sign of the largest quaternion element
};

#define SCALE48S 23168.0f		// needs to fit 2*sqrt(0.5) into 15 bits.
#define SHIFT48S 16384			// half of 2^15 bits.

inline Quaternion48S::operator Quaternion ()	const
{

	Quaternion tmp;

	COMPILE_TIME_ASSERT( sizeof( Quaternion48S ) == 6 );

	float *ptmp = &tmp.x;
	int ia = offsetL + offsetH * 2;
	int ib = ( ia + 1 ) % 4;
	int ic = ( ia + 2 ) % 4;
	int id = ( ia + 3 ) % 4;
	ptmp[ia] = ( (int)a - SHIFT48S ) * ( 1.0f / SCALE48S );
	ptmp[ib] = ( (int)b - SHIFT48S ) * ( 1.0f / SCALE48S );
	ptmp[ic] = ( (int)c - SHIFT48S ) * ( 1.0f / SCALE48S );
	ptmp[id] = sqrt( 1.0f - ptmp[ia] * ptmp[ia] - ptmp[ib] * ptmp[ib] - ptmp[ic] * ptmp[ic] );
	if (dneg)
		ptmp[id] = -ptmp[id];

	return tmp; 

}

inline Quaternion48S& Quaternion48S::operator=(const Quaternion &vOther)	
{
	CHECK_VALID(vOther);

	const float *ptmp = &vOther.x;

	// find largest field, make sure that one is recreated by the sqrt to minimize error
	int i = 0;
	if ( fabs( ptmp[i] ) < fabs( ptmp[1] ) )
	{
		i = 1;
	}
	if ( fabs( ptmp[i] ) < fabs( ptmp[2] ) )
	{
		i = 2;
	}
	if ( fabs( ptmp[i] ) < fabs( ptmp[3] ) )
	{
		i = 3;
	}

	int offset = ( i + 1 ) % 4; // make "a" so that "d" is the largest element
	offsetL = offset & 1;
	offsetH = offset > 1;
	a = clamp( (int)(ptmp[ offset ] * SCALE48S) + SHIFT48S, 0, (int)(SCALE48S * 2) );
	b = clamp( (int)(ptmp[ ( offset + 1 ) % 4 ] * SCALE48S) + SHIFT48S, 0, (int)(SCALE48S * 2) );
	c = clamp( (int)(ptmp[ ( offset + 2 ) % 4 ] * SCALE48S) + SHIFT48S, 0, (int)(SCALE48S * 2) );
	dneg = ( ptmp[ ( offset + 3 ) % 4 ] < 0.0f );

	return *this; 
}


//=========================================================
// 32 bit Quaternion
//=========================================================

class Quaternion32
{
public:
	// Construction/destruction:
	Quaternion32(void); 
	Quaternion32(vec_t X, vec_t Y, vec_t Z);

	// assignment
	// Quaternion& operator=(const Quaternion48 &vOther);
	Quaternion32& operator=(const Quaternion &vOther);
	operator Quaternion ();

private:
	unsigned int x:11;
	unsigned int y:10;
	unsigned int z:10;
	unsigned int wneg:1;
};


inline Quaternion32::operator Quaternion ()	
{

	Quaternion tmp;

	tmp.x = ((int)x - 1024) * (1 / 1024.0);
	tmp.y = ((int)y - 512) * (1 / 512.0);
	tmp.z = ((int)z - 512) * (1 / 512.0);
	tmp.w = sqrt( 1 - tmp.x * tmp.x - tmp.y * tmp.y - tmp.z * tmp.z );
	if (wneg)
		tmp.w = -tmp.w;
	return tmp; 

}

inline Quaternion32& Quaternion32::operator=(const Quaternion &vOther)	
{
	CHECK_VALID(vOther);

	x = clamp( (int)(vOther.x * 1024) + 1024, 0, 2047 );
	y = clamp( (int)(vOther.y * 512) + 512, 0, 1023 );
	z = clamp( (int)(vOther.z * 512) + 512, 0, 1023 );
	wneg = (vOther.w < 0);
	return *this; 
}



//=========================================================
// 16 bit float
//=========================================================


const int float32bias = 127;
const int float16bias = 15;

const float maxfloat16bits = 65504.0f;

class float16
{
public:
	// float16() {};
	//float16( float f ) { m_storage.rawWord = ConvertFloatTo16bits(f); }
	float16& operator=(const unsigned short &other)  { m_storage.rawWord = other; return *this; };

	void Init() { m_storage.rawWord = 0; }
//	float16& operator=(const float16 &other) { m_storage.rawWord = other.m_storage.rawWord; return *this; }
	//	float16& operator=(const float &other) { m_storage.rawWord = ConvertFloatTo16bits(other); return *this; }
//	operator unsigned short () { return m_storage.rawWord; }
//	operator float () { return Convert16bitFloatTo32bits( m_storage.rawWord ); }
	unsigned short GetBits() const 
	{ 
		return m_storage.rawWord; 
	}
	float GetFloat() const 
	{ 
		return Convert16bitFloatTo32bits( m_storage.rawWord ); 
	}
	void SetFloat(const float in )
	{ 
		m_storage.rawWord = ConvertFloatTo16bits( in ); 
	}

	bool IsInfinity() const
	{
		return m_storage.bits.biased_exponent == 31 && m_storage.bits.mantissa == 0;
	}
	bool IsNaN() const
	{
		return m_storage.bits.biased_exponent == 31 && m_storage.bits.mantissa != 0;
	}

	bool operator==(const float16 other) const { return m_storage.rawWord == other.m_storage.rawWord; }
	bool operator!=(const float16 other) const { return m_storage.rawWord != other.m_storage.rawWord; }
	
//	bool operator< (const float other) const	   { return GetFloat() < other; }
//	bool operator> (const float other) const	   { return GetFloat() > other; }

	template< bool BRANCHLESS > // allows you to force branchy/branchless implementation regardless of the current platform
	static unsigned short ConvertFloatTo16bitsNonDefault( float input );
	static float Convert16bitFloatTo32bits( unsigned short input );
	// unfortunately, function templates can't have default template parameters in 2010-era C++ 
	inline static unsigned short ConvertFloatTo16bits(const float input )
	{	// default to branchless on ppc and branchy on x86
		return ConvertFloatTo16bitsNonDefault<false>(input);
	}	

protected:
	union float32bits
	{
		float rawFloat;
		uint32_t rawAsInt;
		struct 
		{
			uint32_t mantissa : 23;
			uint32_t biased_exponent : 8;
			uint32_t sign : 1;
		} bits;
	};

	union float16bits
	{
		unsigned short rawWord;
		struct
		{
			unsigned short mantissa : 10;
			unsigned short biased_exponent : 5;
			unsigned short sign : 1;
		} bits;
	};

	static bool IsNaN(const float16bits in )
	{
		return in.bits.biased_exponent == 31 && in.bits.mantissa != 0;
	}
	static bool IsInfinity(const float16bits in )
	{
		return in.bits.biased_exponent == 31 && in.bits.mantissa == 0;
	}

	// 0x0001 - 0x03ff
	float16bits m_storage;
};

//=========================================================
// Fit a 3D vector in 48 bits
//=========================================================

class Vector48
{
public:
	// Construction/destruction:
	Vector48() {}
	Vector48(const vec_t X, const vec_t Y, const vec_t Z) { x.SetFloat( X ); y.SetFloat( Y ); z.SetFloat( Z ); }

	// assignment
	Vector48& operator=(const Vector &vOther);
	operator Vector ();

	const float operator[](const int i ) const { return (((float16 *)this)[i]).GetFloat(); }

	float16 x;
	float16 y;
	float16 z;
};


template< bool BRANCHLESS >
inline unsigned short float16::ConvertFloatTo16bitsNonDefault( float input )
{ 
	float16bits output;
	float32bits inFloat;
	//if ( !BRANCHLESS ) // x86 code
	{
		if ( input > maxfloat16bits )
			input = maxfloat16bits;
		else if ( input < -maxfloat16bits )
			input = -maxfloat16bits;


		inFloat.rawFloat = input;

	}
	output.bits.sign = inFloat.bits.sign;

	if ( inFloat.bits.biased_exponent==0u )
	{ 
		// zero and denorm both map to zero
		output.bits.mantissa = 0;
		output.bits.biased_exponent = 0;
	}
	else if ( inFloat.bits.biased_exponent==0xff )
	{
		if ( !BRANCHLESS )
		{
			if ( inFloat.bits.mantissa==0u )
			{ 
				/*
				// infinity
				output.bits.mantissa = 0;
				output.bits.biased_exponent = 31;
				*/

				// infinity maps to maxfloat
				output.bits.mantissa = 0x3ff;
				output.bits.biased_exponent = 0x1e;
			}
			else if ( inFloat.bits.mantissa == 0 )
			{ 
				/*
				// NaN
				output.bits.mantissa = 1;
				output.bits.biased_exponent = 31;
				*/

				// NaN maps to zero
				output.bits.mantissa = 0;
				output.bits.biased_exponent = 0;
			}
		}
	}
	else 
	{ 
		// regular number
		int new_exp = inFloat.bits.biased_exponent-float32bias;
		// it's actually better to branch in these cases on PPC, 
		// because the variable bit shift is such a massive penalty 
		// that it's worth a branch penalty to avoid it.
		if (new_exp<-24) 
		{ 
			// this maps to 0
			output.bits.mantissa = 0;
			output.bits.biased_exponent = 0;
		}

		if (new_exp<-14) 
		{
			// this maps to a denorm
			output.bits.biased_exponent = 0;
			unsigned int exp_val = ( unsigned int )( -14 - new_exp );
			if( exp_val > 0 && exp_val < 11 )
			{
				output.bits.mantissa = ( 1 << ( 10 - exp_val ) ) + ( inFloat.bits.mantissa >> ( 13 + exp_val ) );
			}
		}
		else if (new_exp>15) 
		{ 
#if 0
			// map this value to infinity
			output.bits.mantissa = 0;
			output.bits.biased_exponent = 31;
#else
			// to big. . . maps to maxfloat
			output.bits.mantissa = 0x3ff;
			output.bits.biased_exponent = 0x1e;
#endif
		}
		else 
		{
			output.bits.biased_exponent = new_exp+15;
			output.bits.mantissa = (inFloat.bits.mantissa >> 13);
		}
		

	}
	return output.rawWord;
}

inline float float16::Convert16bitFloatTo32bits( unsigned short input )
{
	float32bits output;
	const float16bits &inFloat = *((float16bits *)&input);

	if( IsInfinity( inFloat ) )
	{
		return maxfloat16bits * ( ( inFloat.bits.sign == 1 ) ? -1.0f : 1.0f );
	}
	if( IsNaN( inFloat ) )
	{
		return 0.0;
	}
	if( inFloat.bits.biased_exponent == 0 && inFloat.bits.mantissa != 0 )
	{
		// denorm
		const float half_denorm = (1.0f/16384.0f); // 2^-14
		float mantissa = ((float)(inFloat.bits.mantissa)) / 1024.0f;
		float sgn = (inFloat.bits.sign)? -1.0f :1.0f;
		output.rawFloat = sgn*mantissa*half_denorm;
	}
	else
	{
		// regular number
		unsigned mantissa = inFloat.bits.mantissa;
		unsigned biased_exponent = inFloat.bits.biased_exponent;
		unsigned sign = ((unsigned)inFloat.bits.sign) << 31;
		biased_exponent = ( (biased_exponent - float16bias + float32bias) * (biased_exponent != 0) ) << 23;
		mantissa <<= (23-10);

		*((unsigned *)&output) = ( mantissa | biased_exponent | sign );
	}

	return output.rawFloat;
}




inline Vector48& Vector48::operator=(const Vector &vOther)	
{
	CHECK_VALID(vOther);

	x.SetFloat( vOther.x );
	y.SetFloat( vOther.y );
	z.SetFloat( vOther.z );
	return *this; 
}


inline Vector48::operator Vector ()
{
	Vector tmp;

	tmp.x = x.GetFloat();
	tmp.y = y.GetFloat();
	tmp.z = z.GetFloat(); 

	return tmp;
}

//=========================================================
// Fit a 2D vector in 32 bits
//=========================================================















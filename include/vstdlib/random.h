//===== Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Random number generator
//
// $Workfile: $
// $NoKeywords: $
//===========================================================================//

#pragma once


#include "vstdlib/vstdlib.h"

#define NTAB 32

#pragma warning(push)
#pragma warning( disable:4251 )

//-----------------------------------------------------------------------------
// A generator of uniformly distributed random numbers
//-----------------------------------------------------------------------------
class IUniformRandomStream
{
public:
	// Sets the seed of the random number generator
	virtual void	SetSeed( int iSeed ) = 0;

	// Generates random numbers
	virtual float	RandomFloat( float flMinVal = 0.0f, float flMaxVal = 1.0f ) = 0;
	virtual int		RandomInt( int iMinVal, int iMaxVal ) = 0;
	virtual float	RandomFloatExp( float flMinVal = 0.0f, float flMaxVal = 1.0f, float flExponent = 1.0f ) = 0;
};


//-----------------------------------------------------------------------------
// The standard generator of uniformly distributed random numbers
//-----------------------------------------------------------------------------
class VSTDLIB_CLASS CUniformRandomStream : public IUniformRandomStream
{
public:
	CUniformRandomStream();

	// Sets the seed of the random number generator
    void	SetSeed( int iSeed ) override;

	// Generates random numbers
    float	RandomFloat( float flMinVal = 0.0f, float flMaxVal = 1.0f ) override;

    int		RandomInt( int iMinVal, int iMaxVal ) override;

    float	RandomFloatExp( float flMinVal = 0.0f, float flMaxVal = 1.0f, float flExponent = 1.0f ) override;

private:

	int m_idum;
	int m_iy;
	int m_iv[NTAB];

};


//-----------------------------------------------------------------------------
// A generator of gaussian distributed random numbers
//-----------------------------------------------------------------------------
class VSTDLIB_CLASS CGaussianRandomStream
{
public:
	// Passing in NULL will cause the gaussian stream to use the
	// installed global random number generator
	CGaussianRandomStream( IUniformRandomStream *pUniformStream = nullptr);

	// Generates random numbers
	float	RandomFloat( float flMean = 0.0f, float flStdDev = 1.0f );

private:
	IUniformRandomStream *m_pUniformStream;
	bool	m_bHaveValue;
	float	m_flRandomValue;

};


//-----------------------------------------------------------------------------
// A couple of convenience functions to access the library's global uniform stream
//-----------------------------------------------------------------------------
VSTDLIB_INTERFACE float	RandomFloat( float flMinVal = 0.0f, float flMaxVal = 1.0f );


#pragma warning(pop)







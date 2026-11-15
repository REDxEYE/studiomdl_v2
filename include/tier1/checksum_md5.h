//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Generic MD5 hashing algo
//
//=============================================================================//

#pragma once



// 16 bytes == 128 bit digest
#define MD5_DIGEST_LENGTH 16  
struct MD5Value_t
{
	unsigned char bits[MD5_DIGEST_LENGTH];

	void Zero();
	bool IsZero() const;

	bool operator==( const MD5Value_t &src ) const;
	bool operator!=( const MD5Value_t &src ) const;

};


// MD5 Hash
typedef struct
{
	unsigned int	buf[4];
    unsigned int	bits[2];
    unsigned char	in[64];
} MD5Context_t;


/// Returns true if the values match.
bool MD5_Compare( const MD5Value_t &data, const MD5Value_t &compare );

inline bool MD5Value_t::operator==( const MD5Value_t &src ) const
{
	return MD5_Compare( *this, src );
}

inline bool MD5Value_t::operator!=( const MD5Value_t &src ) const
{
	return !MD5_Compare( *this, src );
}


//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#pragma once
#ifdef _WIN32
#pragma once
#endif


#include "studio.h"


class C_BaseAnimating;


class CBoneAccessor
{
public:
	
	CBoneAccessor();
	explicit CBoneAccessor( matrix3x4a_t *pBones ); // This can be used to allow access to all bones.
	
	// Initialize.
#if defined( CLIENT_DLL )
	void Init( const C_BaseAnimating *pAnimating, matrix3x4a_t *pBones );
#endif
	
	BoneFlags GetReadableBones();
	void SetReadableBones( BoneFlags flags );

	BoneFlags GetWritableBones();
	void SetWritableBones( BoneFlags flags );

	bool isBoneAvailableForRead( int iBone ) const;
	bool isBoneAvailableForWrite( int iBone ) const;

	// Get bones for read or write access.
	const matrix3x4a_t&	GetBone( int iBone ) const;
	const matrix3x4a_t&	operator[]( int iBone ) const;
	matrix3x4a_t&		GetBoneForWrite( int iBone );

	matrix3x4a_t			*GetBoneArrayForWrite( ) const;

private:

#if defined( CLIENT_DLL ) && defined( _DEBUG )
	void SanityCheckBone( int iBone, bool bReadable ) const;
#endif

	// Only used in the client DLL for debug verification.
	const C_BaseAnimating *m_pAnimating;

	matrix3x4a_t *m_pBones;

	BoneFlags m_ReadableBones;		// Which bones can be read.
	BoneFlags m_WritableBones;		// Which bones can be written.
};


inline CBoneAccessor::CBoneAccessor()
{
	m_pAnimating = nullptr;
	m_pBones = nullptr;
	m_ReadableBones = m_WritableBones = BoneFlags::None;
}

inline CBoneAccessor::CBoneAccessor( matrix3x4a_t *pBones )
{
	m_pAnimating = nullptr;
	m_pBones = pBones;
}

#if defined( CLIENT_DLL )
	inline void CBoneAccessor::Init( const C_BaseAnimating *pAnimating, matrix3x4a_t *pBones )
	{
		m_pAnimating = pAnimating;
		m_pBones = pBones;
	}
#endif

inline BoneFlags CBoneAccessor::GetReadableBones()
{
	return m_ReadableBones;
}

inline void CBoneAccessor::SetReadableBones( BoneFlags flags )
{
	m_ReadableBones = flags;
}

inline BoneFlags CBoneAccessor::GetWritableBones()
{
	return m_WritableBones;
}

inline void CBoneAccessor::SetWritableBones( BoneFlags flags )
{
	m_WritableBones = flags;
}

inline const matrix3x4a_t& CBoneAccessor::GetBone( int iBone ) const
{
#if defined( CLIENT_DLL ) && defined( _DEBUG )
	SanityCheckBone( iBone, true );
#endif
	return m_pBones[iBone];
}

inline const matrix3x4a_t& CBoneAccessor::operator[]( int iBone ) const
{
#if defined( CLIENT_DLL ) && defined( _DEBUG )
	SanityCheckBone( iBone, true );
#endif
	return m_pBones[iBone];
}

inline matrix3x4a_t& CBoneAccessor::GetBoneForWrite( int iBone )
{
#if defined( CLIENT_DLL ) && defined( _DEBUG )
	SanityCheckBone( iBone, false );
#endif
	return m_pBones[iBone];
}

inline matrix3x4a_t *CBoneAccessor::GetBoneArrayForWrite( void ) const
{
	return m_pBones;
}



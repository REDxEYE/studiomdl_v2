//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once


#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "datamodel/dmehandle.h"
#include "datamodel/dmelementfactoryhelper.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeClip;
class CDmeTimeFrame;
class CDmeChannel;
class CDmeChannelsClip;
struct TimeSelection_t;
enum ChannelMode_t : int;


struct DmeClipStack_t
{
public:
	DmeClipStack_t() : m_bOptimized( false ) {}
	DmeClipStack_t( const CDmeClip *pRoot, CDmeClip *pShot, CDmeClip *pClip ) { BuildClipStack( pRoot, pShot, pClip ); }

	int GetClipCount() const { return m_clips.size(); }
	const CDmeClip *GetClip( int i ) const { return m_clips[ i ]; }
	      CDmeClip *GetClip( int i )       { return m_clips[ i ]; }
	int InvalidClipIndex() const { return m_clips.InvalidIndex(); }

	bool BuildClipStack( const CDmeClip *pRoot, const CDmeClip *pShot, const CDmeClip *pClip );

	int AddClipToHead( const CDmeClip *pClip );
	void RemoveClip( int i ) { m_clips.Remove( i ); m_bOptimized = false; }
	void RemoveAll() { m_clips.RemoveAll(); m_bOptimized = false; }

	DmeTime_t ToChildMediaTime  ( DmeTime_t t, bool bClamp = true ) const;

protected:
	CUtlVector< CDmeHandle< CDmeClip > > m_clips;
	mutable DmeTime_t m_tStart;
	mutable DmeTime_t m_tDuration;
	mutable DmeTime_t m_tOffset;
	mutable double m_flScale;
	mutable bool m_bOptimized;

	void Optimize() const;
};


//-----------------------------------------------------------------------------
// Base class of CDmeChannelsClip: a time frame mapping parent time to the time of the channel logs
//-----------------------------------------------------------------------------
class CDmeClip : public CDmElement
{
	DEFINE_ELEMENT( CDmeClip, CDmElement );

public:
	// Returns the time frame
	DmeTime_t ToChildMediaTime  ( DmeTime_t t, bool bClamp = true ) const;
	DmeTime_t FromChildMediaTime( DmeTime_t t, bool bClamp = true ) const;
	DmeTime_t FromChildMediaDuration( DmeTime_t dt ) const;
	DmeTime_t GetStartTime() const;
	DmeTime_t GetEndTime() const;
	DmeTime_t GetDuration() const;
	DmeTime_t GetTimeOffset() const;
	float GetTimeScale() const;

	virtual void BakeTimeScale( float scale = 1.0f );

	// Builds the clip stack from pRoot down to this clip. Clips do not nest any more (the SFM
	// film clip / track hierarchy was removed), so this only succeeds when this clip is pRoot
	// (and pShot is null or this clip as well)
	bool BuildClipStack( DmeClipStack_t* pStack, const CDmeClip *pRoot, CDmeClip *pShot = nullptr);

	// Checks for muteness
	void SetMute( bool state );
	bool IsMute( ) const;

	// Clip vertical display size
	void SetDisplayScale( float flDisplayScale );
	float GetDisplayScale() const;

protected:
	CDmaElement< CDmeTimeFrame > m_TimeFrame;
	CDmaVar< Color > m_ClipColor;
	CDmaVar< bool > m_bMute;
	CDmaVar< float > m_flDisplayScale;

	CDmaString m_ClipText;
};

inline void CDmeClip::SetMute( bool state )
{
	m_bMute = state;
}

inline bool CDmeClip::IsMute( ) const
{
	return m_bMute;
}

//-----------------------------------------------------------------------------
// Clip vertical display size
//-----------------------------------------------------------------------------
inline void CDmeClip::SetDisplayScale( float flDisplayScale )
{
	m_flDisplayScale = flDisplayScale;
}

inline float CDmeClip::GetDisplayScale() const
{
	return m_flDisplayScale;
}


//-----------------------------------------------------------------------------
// Clip containing recorded data from the game
//-----------------------------------------------------------------------------
class CDmeChannelsClip : public CDmeClip
{
	DEFINE_ELEMENT( CDmeChannelsClip, CDmeClip );

public:
    void BakeTimeScale( float scale = 1.0f ) override;

	CDmaElementArray< CDmeChannel > m_Channels;
};


//-----------------------------------------------------------------------------
// helper methods
//-----------------------------------------------------------------------------
CDmeChannel *FindChannelTargetingElement( CDmElement *pElement, const char *pAttributeName = nullptr);

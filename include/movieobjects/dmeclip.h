//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "datamodel/dmehandle.h"
#include "tier1/utlmap.h"
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

	int GetClipCount() const { return m_clips.Count(); }
	const CDmeClip *GetClip( int i ) const { return m_clips[ i ]; }
	      CDmeClip *GetClip( int i )       { return m_clips[ i ]; }
	int FindClip( const CDmeClip *pClip ) const;
	int InvalidClipIndex() const { return m_clips.InvalidIndex(); }

	bool BuildClipStack( const CDmeClip *pRoot, const CDmeClip *pShot, const CDmeClip *pClip );

	int AddClipToHead( const CDmeClip *pClip );
	int AddClipToTail( const CDmeClip *pClip );
	void RemoveClip( int i ) { m_clips.Remove( i ); m_bOptimized = false; }
	void RemoveAll() { m_clips.RemoveAll(); m_bOptimized = false; }

	DmeTime_t ToChildMediaTime  ( DmeTime_t t, bool bClamp = true ) const;
	DmeTime_t FromChildMediaTime( DmeTime_t t, bool bClamp = true ) const;
	DmeTime_t ToChildMediaDuration ( DmeTime_t t ) const;
	DmeTime_t FromChildMediaDuration( DmeTime_t t ) const;

	void ToChildMediaTime( TimeSelection_t &params ) const;

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
	CDmeTimeFrame *GetTimeFrame() const;
	DmeTime_t ToChildMediaTime  ( DmeTime_t t, bool bClamp = true ) const;
	DmeTime_t FromChildMediaTime( DmeTime_t t, bool bClamp = true ) const;
	DmeTime_t ToChildMediaDuration  ( DmeTime_t dt ) const;
	DmeTime_t FromChildMediaDuration( DmeTime_t dt ) const;
	DmeTime_t GetStartTime() const;
	DmeTime_t GetEndTime() const;
	DmeTime_t GetDuration() const;
	DmeTime_t GetTimeOffset() const;
	DmeTime_t GetStartInChildMediaTime() const;
	DmeTime_t GetEndInChildMediaTime() const;
	float GetTimeScale() const;
	void SetStartTime ( DmeTime_t t );
	void SetDuration  ( DmeTime_t t );
	void SetTimeOffset( DmeTime_t t );
	void SetTimeScale ( float s );

	virtual void BakeTimeScale( float scale = 1.0f );

	// Builds the clip stack from pRoot down to this clip. Clips do not nest any more (the SFM
	// film clip / track hierarchy was removed), so this only succeeds when this clip is pRoot
	// (and pShot is null or this clip as well)
	bool BuildClipStack( DmeClipStack_t* pStack, const CDmeClip *pRoot, CDmeClip *pShot = nullptr);

	void SetClipColor( const Color& clr );

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
	virtual void BakeTimeScale( float scale = 1.0f );

	CDmeChannel *CreatePassThruConnection
	( 
		char const *passThruName,
		CDmElement *pFrom, 
		char const *pFromAttribute, 
		CDmElement *pTo, 
		char const *pToAttribute,
		int index = 0 
	);

	void RemoveChannel( CDmeChannel *pChannel );
	
	// Set the mode of all of the channels in the clip
	void SetChannelMode( const ChannelMode_t &mode );

	// Operate all of the channels in the clip
	void OperateChannels();

	// Play all of the channels in the clip
	void PlayChannels(); 

	CDmaElementArray< CDmeChannel > m_Channels;
};


//-----------------------------------------------------------------------------
// helper methods
//-----------------------------------------------------------------------------
CDmeChannel *FindChannelTargetingElement( CDmElement *pElement, const char *pAttributeName = nullptr);
CDmeChannel *FindChannelTargetingElement( CDmeChannelsClip *pChannelsClip, CDmElement *pElement, const char *pAttributeName = nullptr);

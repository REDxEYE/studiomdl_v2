//====== Copyright (c) 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class IDmNotify;


//-----------------------------------------------------------------------------
// Notification stuff:
//-----------------------------------------------------------------------------
class CNotifyManager
{
public:
	CNotifyManager();
	~CNotifyManager();

	bool			InstallNotificationCallback( IDmNotify *pNotify );
	void			RemoveNotificationCallback( IDmNotify *pNotify );
	bool			IsSuppressingNotify( ) const;
	void			SetSuppressingNotify( bool bSuppress );
	void			PushNotificationScope( const char *pReason, int nNotifySource, int nNotifyFlags );
	void			PopNotificationScope( bool bAbort );

	void			NotifyState( int nNotifyFlags );

private:
	CUtlVector< IDmNotify* >			m_Notifiers;
	int				m_nNotifyNesting;
	int				m_nNotifySource;
	int				m_nNotifyFlags;
	const char*		m_pNotifyReason;
	bool			m_bSuppressingNotify : 1;
};


inline void CNotifyManager::NotifyState( int nNotifyFlags )
{
	// FIXME: Should suppress prevent notification being sent,
	// or prevent notification flags from being set in the first place?
	m_nNotifyFlags |= nNotifyFlags;
}



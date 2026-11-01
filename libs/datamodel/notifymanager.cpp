//====== Copyright (c) 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "notifymanager.h"
#include "datamodel.h"

extern CDataModel *g_pDataModelImp;

CNotifyManager::CNotifyManager( ) :
	m_nNotifyNesting( 0 ),
	m_nNotifySource( 0 ),
	m_nNotifyFlags( 0 ),
	m_pNotifyReason(nullptr),
	m_bSuppressingNotify( false )
{
}

CNotifyManager::~CNotifyManager()
{
}

bool CNotifyManager::InstallNotificationCallback( IDmNotify *pNotify )
{
	if ( m_Notifiers.Find( pNotify ) >= 0 )
		return false;
	m_Notifiers.AddToTail( pNotify );
	return true;
}

void CNotifyManager::RemoveNotificationCallback( IDmNotify *pNotify )
{
	m_Notifiers.FindAndRemove( pNotify );
}

bool CNotifyManager::IsSuppressingNotify( ) const
{
	return m_bSuppressingNotify;
}

void CNotifyManager::SetSuppressingNotify( bool bSuppress )
{
	m_bSuppressingNotify = bSuppress;
}

void CNotifyManager::PushNotificationScope( const char *pReason, int nNotifySource, int nNotifyFlags )
{
	if ( m_nNotifyNesting++ == 0 )
	{
		m_pNotifyReason = pReason;
		m_nNotifySource = nNotifySource;
		m_nNotifyFlags = nNotifyFlags;
	}
}

void CNotifyManager::PopNotificationScope( bool bAbort )
{
	--m_nNotifyNesting;
	Assert( m_nNotifyNesting >= 0 );
	if ( m_nNotifyNesting == 0 )
	{
		if ( !bAbort && // If aborting, then everything should be unchanged, so no need to notify.
			 !m_bSuppressingNotify && 
			 ( ( m_nNotifyFlags & NOTIFY_CHANGE_MASK ) != 0 ) )
		{
			int nNotifyCount = m_Notifiers.Count();
			for( int i = 0; i < nNotifyCount; ++i )
			{
				m_Notifiers[i]->NotifyDataChanged( m_pNotifyReason, m_nNotifySource, m_nNotifyFlags );
			}
		}
		m_nNotifySource = 0;
		m_nNotifyFlags = 0;
	}
}

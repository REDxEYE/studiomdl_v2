//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once

#include <vector>

// emulate a hardware post T&L vertex fifo

class CHardwareVertexCache
{
public:
	CHardwareVertexCache();
	void Init( int size );
	void Insert( int index );
	bool IsPresent( int index );
	void Flush( void );
private:
	int m_Size;
	std::vector<int> m_Fifo;
	int m_HeadIndex;
	int m_NumEntries;
};


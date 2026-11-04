//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once
#include "studiomdl_global_state.hpp"
#ifdef _WIN32
#pragma once
#endif

#include "tier1/smartptr.h"
#include "tier1_v2/path_utils.hpp"

#include <fstream>

class CFileBuffer
{
public:
	CFileBuffer( int size )
	{
		m_pData = new unsigned char[size];
#ifdef _DEBUG
		m_pUsed = new const char *[size];
		memset( m_pUsed, 0, size * sizeof( const char * ) );
#endif
		m_Size = size;
		m_pCurPos = m_pData;
#ifdef _DEBUG
		memset( m_pData, 0xbaadf00d, size );
#endif
	}
	~CFileBuffer()
	{
		delete [] m_pData;
#ifdef _DEBUG
		delete [] m_pUsed;
#endif
	}

#ifdef _DEBUG
	void TestWritten( int EndOfFileOffset )
	{
		int i;
		for( i = 0; i < EndOfFileOffset; i++ )
		{
			if( !m_pUsed[i] )
			{
				printf( "offset %d not written, end of file invalid!\n", i );
				Assert( 0 );
			}
		}
	}
#endif
	
	// fileName is UTF-8
	void WriteToFile( const char *fileName, int size )
	{
		std::ofstream file( vstd::paths::to_fs( fileName ), std::ios::binary | std::ios::trunc );
		if( !file )
		{
			MdlWarning( "Can't open \"%s\" for writing!\n", fileName );
			return;
		}

		file.write( reinterpret_cast<const char *>( m_pData ), size );
	}
	
	void WriteAt( int offset, void *data, int size, const char *name )
	{
//		printf( "WriteAt: \"%s\" offset: %d end: %d size: %d\n", name, offset, offset + size - 1, size );
		m_pCurPos = m_pData + offset;

#ifdef _DEBUG
		int i;
		const char **used = m_pUsed + offset;
		bool bitched = false;
		for( i = 0; i < size; i++ )
		{
			if( used[i] )
			{
				if( !bitched )
				{
					printf( "overwrite at %d! (overwriting \"%s\" with \"%s\")\n", i + offset, used[i], name );
					Assert( 0 );
					bitched = true;
				}
			}
			else
			{
				used[i] = name;
			}
		}
#endif // _DEBUG

		Append( data, size );
	}
	int GetOffset( void )
	{
		return m_pCurPos - m_pData;
	}
	void *GetPointer( int offset )
	{
		return m_pData + offset;
	}
private:
	void Append( void *data, int size )
	{
		Assert( m_pCurPos + size - m_pData < m_Size );
		memcpy( m_pCurPos, data, size );
		m_pCurPos += size;
	}
	CFileBuffer(); // undefined
	int m_Size;
	unsigned char *m_pData;
	unsigned char *m_pCurPos;
#ifdef _DEBUG
	const char **m_pUsed;
#endif
};
	


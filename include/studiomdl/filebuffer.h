//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Fixed-layout output file (.vtx): every block's offset is computed up front, then the blocks are written
//          in any order with WriteAt. The size is fixed at construction, so pointers from GetPointer stay valid.
//
//=============================================================================//

#pragma once

#include <cstddef>
#include <cstring>
#include <print>
#include <vector>

#include "tier0/dbg.h"
#include "tier1_v2/buffer.hpp"
#include "studiomdl/studiomdl_log.hpp"

class CFileBuffer
{
public:
	explicit CFileBuffer( const size_t size ) : m_Data( size )
#ifdef _DEBUG
		, m_Used( size, nullptr )
#endif
	{
	}

	CFileBuffer( const CFileBuffer & ) = delete;
	CFileBuffer &operator=( const CFileBuffer & ) = delete;

#ifdef _DEBUG
	void TestWritten( const size_t endOfFileOffset ) const
	{
		for ( size_t i = 0; i < endOfFileOffset; i++ )
		{
			if ( !m_Used[i] )
			{
				std::print( "offset {} not written, end of file invalid!\n", i );
				Assert( 0 );
			}
		}
	}
#endif

	void WriteAt( const size_t offset, const void *data, const size_t size, const char *name )
	{
		if ( offset + size > m_Data.size() )
		{
			MdlError( "{}: write of {} bytes at {} is past the end of the {} byte file\n", name, size, offset, m_Data.size() );
		}

#ifdef _DEBUG
		bool reported = false;
		for ( size_t i = offset; i < offset + size; i++ )
		{
			if ( m_Used[i] )
			{
				if ( !reported )
				{
					std::print( "overwrite at {}! (overwriting \"{}\" with \"{}\")\n", i, m_Used[i], name );
					Assert( 0 );
					reported = true;
				}
			}
			else
			{
				m_Used[i] = name;
			}
		}
#endif // _DEBUG

		if ( size )
			memcpy( m_Data.data() + offset, data, size );
	}

	void *GetPointer( const size_t offset ) { return m_Data.data() + offset; }

	// the file contents; move it out once nothing reads the file through GetPointer anymore
	vstd::Buffer &Data() { return m_Data; }

private:
	vstd::Buffer m_Data;
#ifdef _DEBUG
	std::vector<const char *> m_Used; // which block wrote each byte
#endif
};

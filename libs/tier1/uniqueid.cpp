//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//
// Unique ID generation
//=============================================================================//

#include "tier0/platform.h"
#include "tier1/uniqueid.h"
#include "tier1/utlbuffer.h"

#include <charconv>
#include <cstring>
#include <random>
#include <string_view>

// One implementation on every platform. The 16 bytes use the memory layout of the Windows UUID struct (Data1 uint32,
// Data2/Data3 uint16, little-endian, then Data4[8]) and the text form is the one of UuidToString/UuidFromString
// ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", lowercase), so binary and text DMX files match what Valve's Windows tools
// (SFM's dmxconvert/studiomdl) read and write.

namespace {
    constexpr size_t UUID_TEXT_LENGTH = 36;
    // Byte order of the hex digit pairs in the text form: Data1, Data2 and Data3 are little-endian in memory
    constexpr int TEXT_BYTE_ORDER[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};

    std::string_view TrimSpace(std::string_view text) {
        while (!text.empty() && V_isspace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && V_isspace(text.back()))
            text.remove_suffix(1);
        return text;
    }

    bool ParseUuid(std::string_view text, UniqueId_t &dest) {
        if (text.size() != UUID_TEXT_LENGTH || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
            return false;

        size_t pos = 0;
        for (const int byteIndex : TEXT_BYTE_ORDER) {
            if (text[pos] == '-')
                ++pos;
            unsigned value = 0;
            const auto [ptr, ec] = std::from_chars(text.data() + pos, text.data() + pos + 2, value, 16);
            if (ec != std::errc() || ptr != text.data() + pos + 2)
                return false;
            dest.m_Value[byteIndex] = static_cast<unsigned char>(value);
            pos += 2;
        }
        return true;
    }

    // Writes the 36 characters plus the terminator
    void FormatUuid(const UniqueId_t &id, char (&out)[UUID_TEXT_LENGTH + 1]) {
        constexpr char hex[] = "0123456789abcdef";
        size_t pos = 0;
        for (size_t i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                out[pos++] = '-';
            const unsigned char value = id.m_Value[TEXT_BYTE_ORDER[i]];
            out[pos++] = hex[value >> 4];
            out[pos++] = hex[value & 15];
        }
        out[pos] = '\0';
    }
}


//-----------------------------------------------------------------------------
// Creates a new unique id (random, RFC 4122 version 4)
//-----------------------------------------------------------------------------
void CreateUniqueId( UniqueId_t *pDest )
{
	thread_local std::mt19937_64 generator( [] {
		std::random_device device;
		std::seed_seq seed{ device(), device(), device(), device(), device(), device(), device(), device() };
		return std::mt19937_64( seed );
	}() );

	const uint64_t lo = generator();
	const uint64_t hi = generator();
	std::memcpy( pDest->m_Value, &lo, 8 );
	std::memcpy( pDest->m_Value + 8, &hi, 8 );
	pDest->m_Value[7] = static_cast<unsigned char>( ( pDest->m_Value[7] & 0x0F ) | 0x40 ); // version 4 (Data3 high nibble)
	pDest->m_Value[8] = static_cast<unsigned char>( ( pDest->m_Value[8] & 0x3F ) | 0x80 ); // RFC 4122 variant
}


//-----------------------------------------------------------------------------
// Creates a new unique id from a string representation of one
//-----------------------------------------------------------------------------
bool UniqueIdFromString( UniqueId_t *pDest, const char *pBuf, int nMaxLen )
{
	const size_t length = nMaxLen == 0 ? std::strlen( pBuf ) : strnlen( pBuf, static_cast<size_t>( nMaxLen ) );
	if ( !ParseUuid( TrimSpace( std::string_view( pBuf, length ) ), *pDest ) )
	{
		InvalidateUniqueId( pDest );
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Sets an object ID to be an invalid state
//-----------------------------------------------------------------------------
void InvalidateUniqueId( UniqueId_t *pDest )
{
	Assert( pDest );
	memset( pDest, 0, sizeof( UniqueId_t ) );
}

bool IsUniqueIdEqual( const UniqueId_t &id1, const UniqueId_t &id2 )
{
	return memcmp( &id1, &id2, sizeof( UniqueId_t ) ) == 0; 
}

void UniqueIdToString( const UniqueId_t &id, char *pBuf, int nMaxLen )
{
	char text[UUID_TEXT_LENGTH + 1];
	FormatUuid( id, text );
	Q_strncpy( pBuf, text, nMaxLen );
}

void CopyUniqueId( const UniqueId_t &src, UniqueId_t *pDest )
{
	memcpy( pDest, &src, sizeof( UniqueId_t ) );
}

bool Serialize( CUtlBuffer &buf, const UniqueId_t &src )
{
	if ( buf.IsText() )
	{
		char text[UUID_TEXT_LENGTH + 1];
		FormatUuid( src, text );
		buf.PutString( text );
	}
	else
	{
		buf.Put( &src, sizeof(UniqueId_t) );
	}
	return buf.IsValid();
}

bool Unserialize( CUtlBuffer &buf, UniqueId_t &dest )
{
	if ( buf.IsText() )
	{
		int nTextLen = buf.PeekStringLength();
		char *pBuf = (char*)stackalloc( nTextLen );
		buf.GetString( pBuf, nTextLen );
		UniqueIdFromString( &dest, pBuf, nTextLen );
	}
	else
	{
		buf.Get( &dest, sizeof(UniqueId_t) );
	}
	return buf.IsValid();
}

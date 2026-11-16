//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once


//-----------------------------------------------------------------------------
// data file format info
//-----------------------------------------------------------------------------
constexpr std::string DMX_LEGACY_VERSION_STARTING_TOKEN =  "<!-- DMXVersion";
constexpr std::string DMX_LEGACY_VERSION_ENDING_TOKEN =  "-->";

constexpr std::string DMX_VERSION_STARTING_TOKEN =  "<!-- dmx";
constexpr std::string DMX_VERSION_ENDING_TOKEN =  "-->";

constexpr std::string GENERIC_DMX_FORMAT =  "dmx";


enum
{
	DMX_MAX_FORMAT_NAME_MAX_LENGTH = 64,
	DMX_MAX_HEADER_LENGTH = 40 + 2 * DMX_MAX_FORMAT_NAME_MAX_LENGTH,
};

struct DmxHeader_t
{
	vstd::FixedString<DMX_MAX_FORMAT_NAME_MAX_LENGTH> encodingName;
	int nEncodingVersion;
	vstd::FixedString<DMX_MAX_FORMAT_NAME_MAX_LENGTH> formatName;
	int nFormatVersion;

	DmxHeader_t() : nEncodingVersion( -1 ), nFormatVersion( -1 )
	{
		encodingName[ 0 ] = formatName[ 0 ] = '\0';
	}
};

//-----------------------------------------------------------------------------
// file id - also used to refer to elements that don't have file associations
//-----------------------------------------------------------------------------
enum DmFileId_t
{
	DMFILEID_INVALID = 0xffffffff
};


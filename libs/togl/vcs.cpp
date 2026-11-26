// Source VCS container decoder and togl translation, following shaderapidx9/vertexshaderdx8.cpp.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <vector>

#include <lzma.h>

#include "tier0/dbg.h"
#include "tier0/platform.h"
#include "tier1/strtools.h"
#include "tier1/utlbuffer.h"
#include "togl/dx9asmtogl2.h"
#include "togl/vcs.h"


namespace
{
constexpr uint32_t BLOCK_TERMINATOR = 0xffffffff;
constexpr uint32_t BLOCK_TYPE_MASK = 0xc0000000;
constexpr uint32_t BLOCK_BZIP2 = 0x00000000;
constexpr uint32_t BLOCK_LZMA = 0x40000000;
constexpr uint32_t BLOCK_UNCOMPRESSED = 0x80000000;
constexpr uint32_t BLOCK_SIZE_MASK = 0x3fffffff;
constexpr uint32_t MAX_UNPACKED_BLOCK_SIZE = 1 << 17; // MAX_SHADER_UNPACKED_BLOCK_SIZE
constexpr uint32_t D3D_END_TOKEN = 0x0000ffff;
constexpr int MAX_TRANSLATION_SIZE = 500000; // dxabstract.cpp uses this for vertex shaders, 50000 for pixel shaders

// ShaderHeader_t (version 5/6) and ShaderHeader_t_v4 share this layout; the sixth field differs
struct vcs_header
{
	int32_t version;
	int32_t totalCombos;
	int32_t dynamicCombos;
	uint32_t flags;
	uint32_t centroidMask;
	uint32_t numStaticCombosOrDiffReferenceSize;
	uint32_t sourceCrc32;
};
static_assert( sizeof( vcs_header ) == 28 );

struct file_stats
{
	int translated = 0;
	int failed = 0;
	int unsupported = 0;
};

class VcsError : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

uint32_t ReadU32( std::span<const uint8_t> data, size_t offset )
{
	if ( offset > data.size() || data.size() - offset < 4 )
		throw VcsError( std::format( "read past the end at offset {}", offset ) );
	uint32_t value;
	std::memcpy( &value, data.data() + offset, 4 );
	return value;
}

// Valve's LZMA block: "LZMA", uncompressed size, compressed size, 5 property bytes, raw LZMA1 stream
// (tier1/lzmaDecoder.h lzma_header_t). liblzma's .lzma ("alone") decoder takes the same stream behind a 13 byte header
// of the properties and the 64 bit uncompressed size.
std::vector<uint8_t> DecompressLzma( std::span<const uint8_t> block )
{
	if ( block.size() < 17 || std::memcmp( block.data(), "LZMA", 4 ) != 0 )
		throw VcsError( "bad LZMA block header" );
	const uint32_t actualSize = ReadU32( block, 4 );
	const uint32_t lzmaSize = ReadU32( block, 8 );
	if ( actualSize > MAX_UNPACKED_BLOCK_SIZE || lzmaSize > block.size() - 17 )
		throw VcsError( std::format( "bad LZMA block sizes {} / {}", actualSize, lzmaSize ) );

	std::vector<uint8_t> stream( 13 + lzmaSize );
	std::memcpy( stream.data(), block.data() + 12, 5 );
	const uint64_t size64 = actualSize;
	std::memcpy( stream.data() + 5, &size64, 8 );
	std::memcpy( stream.data() + 13, block.data() + 17, lzmaSize );

	std::vector<uint8_t> out( actualSize );
	lzma_stream lzma = LZMA_STREAM_INIT;
	if ( lzma_alone_decoder( &lzma, UINT64_MAX ) != LZMA_OK )
		throw VcsError( "lzma_alone_decoder failed" );
	lzma.next_in = stream.data();
	lzma.avail_in = stream.size();
	lzma.next_out = out.data();
	lzma.avail_out = out.size();
	const lzma_ret result = lzma_code( &lzma, LZMA_FINISH );
	const size_t written = out.size() - lzma.avail_out;
	lzma_end( &lzma );
	if ( ( result != LZMA_STREAM_END && result != LZMA_OK ) || written != actualSize )
		throw VcsError( std::format( "LZMA decode failed ({})", static_cast<int>( result ) ) );
	return out;
}

// dxabstract.cpp CentroidMaskFromName: the GL translator needs to know which interpolators are centroid, and the
// bytecode doesn't carry it for vertex shaders. Must match between linked vertex and pixel shaders.
uint32_t CentroidMaskFromName( bool pixelShader, const char *name )
{
	if ( pixelShader )
	{
		if ( V_stristr( name, "lightmappedgeneric_ps" ) || V_strstr( name, "worldtwotextureblend_ps" ) )
			return ( 1 << 2 ) | ( 1 << 3 );
		if ( V_stristr( name, "lightmappedreflective_ps" ) )
			return ( 1 << 6 ) | ( 1 << 7 );
		if ( V_stristr( name, "water_ps" ) )
			return 0xE0;
		if ( V_stristr( name, "shadow_ps" ) )
			return 0xE;
		if ( V_stristr( name, "ShatteredGlass_ps" ) )
			return 0xC;
		if ( V_stristr( name, "flashlight_ps" ) )
			return 0xC;
	}
	else
	{
		if ( V_stristr( name, "lightmappedgeneric_vs" ) )
			return ( 1 << 2 ) | ( 1 << 3 );
		if ( V_stristr( name, "lightmappedreflective_vs" ) )
			return ( 1 << 6 ) | ( 1 << 7 );
		if ( V_stristr( name, "water_vs" ) )
			return 0xE0;
		if ( V_stristr( name, "shadow_vs" ) )
			return 0xE;
		if ( V_stristr( name, "ShatteredGlass_vs" ) )
			return 0xC;
		if ( V_stristr( name, "flashlight_vs" ) )
			return 0xC;
	}
	return 0;
}

// dxabstract.cpp ShadowDepthSamplerMaskFromName: samplers that hold shadow depth maps (sampler2DShadow in GLSL).
// These are the CS:GO shader names; SFM shaders with depth samplers under other names get a plain sampler2D.
int ShadowDepthSamplerMaskFromName( const char *name )
{
	if ( V_stristr( name, "water_ps" ) )
		return 1 << 7;
	if ( V_stristr( name, "infected_ps" ) )
		return 1 << 1;
	if ( V_stristr( name, "phong_ps" ) )
		return ( 1 << 4 ) | ( 1 << 15 );
	if ( V_stristr( name, "vertexlit_and_unlit_generic_bump_ps" ) )
		return ( 1 << 8 ) | ( 1 << 15 );
	if ( V_stristr( name, "vertexlit_and_unlit_generic_ps" ) )
		return ( 1 << 8 ) | ( 1 << 15 );
	if ( V_stristr( name, "eye_refract_ps" ) )
		return 1 << 6;
	if ( V_stristr( name, "eyes_flashlight_ps" ) )
		return 1 << 4;
	if ( V_stristr( name, "worldtwotextureblend_ps" ) )
		return 1 << 7;
	if ( V_stristr( name, "teeth_flashlight_ps" ) )
		return 1 << 2;
	if ( V_stristr( name, "flashlight_ps" ) ) // substring of the above, keep it last
		return 1 << 7;
	if ( V_stristr( name, "lightmappedgeneric_ps" ) )
		return 1 << 15;
	if ( V_stristr( name, "character_ps" ) )
		return 1 << 8;
	return 0;
}

class Translator
{
public:
	Translator( std::string name, std::filesystem::path outDir ) : m_name( std::move( name ) ), m_outDir( std::move( outDir ) ) {}

	void SelectDynamic( uint32_t id, std::string &output ) { m_dynamic = id; m_result = &output; }
	bool Done() const { return m_result && !m_result->empty(); }

	void Translate( uint32_t staticCombo, uint32_t dynamicCombo, std::span<const uint8_t> byteCode )
	{
		if ( m_dynamic && dynamicCombo != *m_dynamic )
			return;
		if ( byteCode.size() < 8 || byteCode.size() % 4 != 0 )
			throw VcsError( "invalid D3D shader size" );
		// Aligned copy; the translator reads tokens until the end token, so it has to be there
		std::vector<uint32_t> tokens( byteCode.size() / 4 );
		std::memcpy( tokens.data(), byteCode.data(), tokens.size() * 4 );
		if ( byteCode.size() % 4 != 0 || tokens.empty() || tokens.back() != D3D_END_TOKEN )
		{
			std::println( stderr, "{} s{} d{}: bytecode is not a D3D9 shader", m_name, staticCombo, dynamicCombo );
			++m_stats.failed;
			return;
		}

		const bool pixelShader = ( tokens[0] & 0xffff0000 ) == 0xffff0000;
		// Clients can enable the sRGB write suffix when rendering into a linear framebuffer.
		if ( m_result && ((tokens[0] >> 16) != 0xffff && (tokens[0] >> 16) != 0xfffe) )
			throw VcsError( "invalid D3D shader version token" );
		const uint32_t options = pixelShader
			? D3DToGL_OptionUseEnvParams | D3DToGL_OptionSRGBWriteSuffix
			: D3DToGL_OptionUseEnvParams | D3DToGL_OptionDoFixupZ | D3DToGL_OptionDoFixupY | D3DToGL_OptionDoUserClipPlanes |
				  D3DToGL_OptionGenerateBoneUniformBuffer;
		const int shadowMask = pixelShader ? ShadowDepthSamplerMaskFromName( m_name.c_str() ) : -1;
		const uint32_t centroidMask = CentroidMaskFromName( pixelShader, m_name.c_str() );
		std::string label = std::format( "{} {}-combo s{} d{}", m_name, pixelShader ? "ps" : "vs", staticCombo, dynamicCombo );

		CUtlBuffer glsl( 1000, MAX_TRANSLATION_SIZE, CUtlBuffer::TEXT_BUFFER );
		glsl.EnsureCapacity( MAX_TRANSLATION_SIZE );
		bool vertexShader = false;
		D3DToGL translator;
		try
		{
			translator.TranslateShader( tokens.data(), &glsl, &vertexShader, options, shadowMask, centroidMask, label.data() );
		}
		// Fatal tier0 errors become normal lookup errors for runtime callers.
		catch ( const Plat_ExitException & )
		{
			if ( m_result )
				throw VcsError( label + ": translation failed" );
			// Error() inside the translator; it already logged the reason
			std::println( stderr, "{}: translation failed", label );
			++m_stats.failed;
			return;
		}

		const char *text = static_cast<const char *>( glsl.Base() );
		const size_t length = std::strlen( text );
		// The translator bounds its writes to fixed buffers and truncates silently; a complete program ends with "}\n"
		if ( length < 2 || std::strcmp( text + length - 2, "}\n" ) != 0 )
		{
			if ( m_result )
				throw VcsError( label + ": output truncated" );
			std::println( stderr, "{}: output truncated", label );
			++m_stats.failed;
			return;
		}
		++m_stats.translated;
		if ( m_result )
			m_result->assign( text, length );

		if ( !m_outDir.empty() )
		{
			const std::filesystem::path dir = m_outDir / m_name;
			std::filesystem::create_directories( dir );
			std::ofstream( dir / std::format( "s{}_d{}.{}", staticCombo, dynamicCombo, vertexShader ? "vert" : "frag" ), std::ios::binary )
				.write( text, static_cast<std::streamsize>( length ) );
		}
	}

	const std::string &Name() const { return m_name; }
	file_stats &Stats() { return m_stats; }

private:
	std::string m_name;
	std::filesystem::path m_outDir;
	file_stats m_stats;
	std::optional<uint32_t> m_dynamic;
	std::string *m_result = nullptr;
};

void ProcessVersion4( std::span<const uint8_t> data, const vcs_header &header, Translator &translator )
{
	const uint32_t diffReferenceSize = header.numStaticCombosOrDiffReferenceSize;
	if ( diffReferenceSize != 0 )
	{
		// shortcut: diff-compressed version 4 files (vertexshaderdx8.cpp ApplyDiffs) are not decoded; add it if a game ships them
		std::println( stderr, "{}: diff-compressed version 4 file, not supported", translator.Name() );
		++translator.Stats().unsupported;
		return;
	}
	const size_t dictionary = sizeof( vcs_header );
	for ( int32_t combo = 0; combo < header.totalCombos; ++combo )
	{
		const uint32_t offset = ReadU32( data, dictionary + combo * 8 );
		const uint32_t size = ReadU32( data, dictionary + combo * 8 + 4 );
		if ( offset == BLOCK_TERMINATOR || size == 0 ) // skipped combo
			continue;
		if ( offset > data.size() || size > data.size() - offset )
			throw VcsError( std::format( "combo {} lies outside the file", combo ) );
		translator.Translate( combo / header.dynamicCombos, combo % header.dynamicCombos, data.subspan( offset, size ) );
	}
}

// maxStaticCombos > 0: translate at most that many static combos, spread evenly over the file (some files hold millions
// of combos: customhero_ps30 is 11520 static x 1920 dynamic)
void ProcessVersion5( std::span<const uint8_t> data, const vcs_header &header, uint32_t maxStaticCombos,
	std::optional<uint32_t> onlyStaticCombo, Translator &translator )
{
	const uint32_t numStaticCombos = header.numStaticCombosOrDiffReferenceSize; // includes the sentinel record
	const size_t dictionary = sizeof( vcs_header );
	const uint32_t numRecords = numStaticCombos > 0 ? numStaticCombos - 1 : 0;
	if ( !numStaticCombos || numStaticCombos > (data.size() - dictionary) / 8 )
		throw VcsError( "invalid static combo directory" );
	if ( onlyStaticCombo && header.version == 6 )
	{
		const size_t aliases = dictionary + size_t(numStaticCombos) * 8;
		const auto count = ReadU32( data, aliases );
		if ( count > (data.size() - aliases - 4) / 8 )
			throw VcsError( "invalid static alias directory" );
		for ( size_t entry = aliases + 4, i = 0; i < count; ++i, entry += 8 )
			if ( ReadU32( data, entry ) == *onlyStaticCombo )
			{
				onlyStaticCombo = ReadU32( data, entry + 4 );
				break;
			}
	}
	const uint32_t stride = maxStaticCombos > 0 && numRecords > maxStaticCombos ? 1 + (numRecords - 1) / maxStaticCombos : 1;
	for ( uint32_t record = 0; record < numRecords; record += stride )
	{
		const uint32_t staticCombo = ReadU32( data, dictionary + record * 8 );
		if ( onlyStaticCombo && staticCombo != *onlyStaticCombo )
			continue;
		size_t offset = ReadU32( data, dictionary + size_t(record) * 8 + 4 );
		const size_t end = ReadU32( data, dictionary + size_t(record + 1) * 8 + 4 );
		if ( offset > end || end > data.size() )
			throw VcsError( "invalid static combo bounds" );
		const auto group = data.first( end );
		for ( ;; )
		{
			const uint32_t blockWord = ReadU32( group, offset );
			offset += 4;
			if ( blockWord == BLOCK_TERMINATOR )
				break;
			const uint32_t blockSize = blockWord & BLOCK_SIZE_MASK;
			if ( blockSize > group.size() - offset )
				throw VcsError( std::format( "static combo {}: block lies outside the file", staticCombo ) );
			const std::span<const uint8_t> block = data.subspan( offset, blockSize );
			offset += blockSize;

			std::vector<uint8_t> unpacked;
			switch ( blockWord & BLOCK_TYPE_MASK )
			{
				case BLOCK_LZMA:
					unpacked = DecompressLzma( block );
					break;
				case BLOCK_UNCOMPRESSED:
					unpacked.assign( block.begin(), block.end() );
					break;
				case BLOCK_BZIP2:
					// shortcut: bzip2 blocks (older Source branches) are not decoded; SFM only uses LZMA. Link libbz2 if needed
					std::println( stderr, "{} s{}: bzip2 block, not supported", translator.Name(), staticCombo );
					++translator.Stats().unsupported;
					continue;
				default:
					throw VcsError( std::format( "static combo {}: unknown block type", staticCombo ) );
			}

			const std::span<const uint8_t> records( unpacked );
			for ( size_t read = 0; read < records.size(); )
			{
				const uint32_t comboId = ReadU32( records, read );
				const uint32_t size = ReadU32( records, read + 4 );
				read += 8;
				if ( size > records.size() - read )
					throw VcsError( std::format( "static combo {}: combo record lies outside its block", staticCombo ) );
				const uint64_t base = header.version == 5 ? uint64_t(staticCombo) * header.dynamicCombos : 0;
				if ( comboId < base || comboId - base >= uint32_t(header.dynamicCombos) )
					throw VcsError( "invalid dynamic combo ID" );
				const auto dynamicCombo = uint32_t(comboId - base);
				translator.Translate( staticCombo, dynamicCombo, records.subspan( read, size ) );
				if ( translator.Done() )
					return;
				read += size;
			}
		}
		if ( onlyStaticCombo )
			return;
	}
	if ( onlyStaticCombo )
		throw VcsError( std::format( "static combo {} has no stored record", *onlyStaticCombo ) );
}

std::vector<uint8_t> ReadFile( const std::filesystem::path &path )
{
	std::ifstream file( path, std::ios::binary | std::ios::ate );
	const auto length = file.tellg();
	if ( length < std::streamoff(sizeof(vcs_header)) || length > std::streamoff(UINT32_MAX) )
		throw VcsError( "cannot read VCS file or invalid file size" );
	std::vector<uint8_t> data(static_cast<size_t>(length));
	file.seekg( 0 );
	if ( !file.read( reinterpret_cast<char *>(data.data()), length ) )
		throw VcsError( "incomplete VCS read" );
	return data;
}
} // namespace

bool togl::ExportVcsShaders( const std::filesystem::path &path, const std::filesystem::path &outDir,
	uint32_t maxStaticCombos, std::optional<uint32_t> onlyStaticCombo )
{
	Translator translator( path.stem().string(), outDir );
	int32_t version = 0;
	try
	{
		const auto data = ReadFile(path);
		vcs_header header;
		std::memcpy( &header, data.data(), sizeof( header ) );
		version = header.version;
		if ( header.dynamicCombos <= 0 )
			throw VcsError( "no dynamic combos" );
		if ( onlyStaticCombo && header.version == 4 )
			throw VcsError( "-s requires VCS version 5 or 6" );
		if ( header.version == 4 )
			ProcessVersion4( data, header, translator );
		else if ( header.version == 5 || header.version == 6 )
			ProcessVersion5( data, header, maxStaticCombos, onlyStaticCombo, translator );
		else
			throw VcsError( std::format( "unsupported version {}", header.version ) );
	}
	catch ( const VcsError &e )
	{
		std::println( stderr, "{}: {}", path.string(), e.what() );
		++translator.Stats().failed;
	}
	const file_stats &stats = translator.Stats();
	std::println( "{} v{}: {} translated, {} failed, {} unsupported", translator.Name(), version, stats.translated, stats.failed,
		stats.unsupported );
	return stats.failed == 0 && stats.unsupported == 0;
}

std::string togl::LoadVcsShader( const std::filesystem::path &file, uint32_t staticID, uint32_t dynamicID )
{
	const auto data = ReadFile(file);
	vcs_header header;
	std::memcpy( &header, data.data(), sizeof(header) );
	if ( header.dynamicCombos <= 0 || dynamicID >= uint32_t(header.dynamicCombos) )
		throw VcsError( "dynamic combo ID outside declared range" );
	Translator translator( file.stem().string(), {} );
	std::string output;
	translator.SelectDynamic( dynamicID, output );
	if ( header.version == 5 || header.version == 6 )
		ProcessVersion5( data, header, 0, staticID, translator );
	else
		throw VcsError( "exact lookup requires VCS version 5 or 6" );
	if ( output.empty() )
		throw VcsError( std::format("missing shader combo s{} d{}", staticID, dynamicID) );
	return output;
}

//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#pragma once



// cmdlib.h																   

#include "tier0/basetypes.h"

// This can go away when everything is in bin.
#if defined( CMDLIB_NODBGLIB )
	void Error( char const *pMsg, ... );
#else

#endif

#include <cstring>
#include <cstdlib>
#include <ctime>
#include "filesystem.h"
#include "tier1_v2/buffer.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


// Tools should use this as the read path ID. It'll look into the paths specified by gameinfo.txt
#define TOOLS_READ_PATH_ID "GAME"


// Tools should use this to fprintf data to files.
void CmdLib_FPrintf( FileHandle_t hFile, const char *pFormat, ... );
char* CmdLib_FGets( char *pOut, int outSize, FileHandle_t hFile );


// This can be set so Msg() sends output to hook functions (like the VMPI MySQL database),
// but doesn't actually printf the output.
extern bool g_bSuppressPrintfOutput;

extern IBaseFileSystem *g_pFileSystem;

#ifdef _WIN32
#pragma warning(disable : 4244)     // MIPS
#pragma warning(disable : 4136)     // X86
#pragma warning(disable : 4051)     // ALPHA

#pragma warning(disable : 4018)     // signed/unsigned mismatch
#pragma warning(disable : 4305)     // truncate from double to float

#pragma warning(disable : 4389)     // singned/unsigned mismatch in ==
#pragma warning(disable: 4512) // assignment operator could not be generated
#endif


// the dec offsetof macro doesnt work very well...
#define myoffsetof(type,identifier) offsetof( type, identifier )


// UTF-8 paths
std::string ExpandArg( std::string_view path );	// absolute, relative to the current directory
std::string ExpandPath( std::string_view qdir, std::string_view path );	// absolute, relative to qdir (the QC directory)

// Content roots that mirror the game search paths (UTF-8, native separators); owned by the caller (studiomdl: per compile)
struct CmdLibContentPaths {
	std::vector<std::string> basePaths; // CmdLib_AddBasePath, then rebuilt by CmdLib_AddNewSearchPath
	std::vector<std::string> relativeDirs; // CmdLib_AddNewSearchPath: directories added below every root
	std::vector<std::string> originalBasePaths; // the roots before the first CmdLib_AddNewSearchPath
};


FileHandle_t	SafeOpenWrite (std::string_view filename);
FileHandle_t	SafeOpenRead (const CmdLibContentPaths &content, std::string_view filename);
void			SafeRead(FileHandle_t f, vstd::Buffer &buffer);
void			SafeWrite(FileHandle_t f, const vstd::Buffer &buffer);

std::optional<vstd::Buffer> LoadFile(const CmdLibContentPaths &content, std::string_view filename);
void	SaveFile (std::string_view filename, const vstd::Buffer &buffer);
qboolean	FileExists ( const char *filename );

// Do a printf in the specified color.
#define CP_ERROR	stderr, 1, 0, 0, 1		// default colors..
#define CP_WARNING	stderr, 1, 1, 0, 1		
#define CP_STARTUP	stdout, 0, 1, 1, 1		
#define CP_NOTIFY	stdout, 1, 1, 1, 1

// Initialize spew output.
void InstallSpewFunction();


// Install allocation hooks so we error out if an allocation can't happen.
void InstallAllocationFunctions();

// entrypoint if chaining spew functions
unsigned short SetConsoleTextColor( int red, int green, int blue, int intensity );
void RestoreConsoleTextColor( unsigned short color );

class CCmdLibStandardLoggingListener : public ILoggingListener
{
public:
    void Log( const LoggingContext_t *pContext, const tchar *pMessage ) override;
};

class CCmdLibFileLoggingListener : public ILoggingListener
{
public:
	CCmdLibFileLoggingListener();

    void Log( const LoggingContext_t *pContext, const tchar *pMessage ) override;
	void Open( char const *pFilename );
	void Close();

private:
	FileHandle_t m_pLogFile;
};

extern CCmdLibStandardLoggingListener g_CmdLibOutputLoggingListener;
extern CCmdLibFileLoggingListener g_CmdLibFileLoggingListener;

char *copystring(const char *s);

void	SafeCreatePath( char *path );

void CmdLib_AddBasePath(CmdLibContentPaths &content, std::string_view path);
void CmdLib_AddNewSearchPath(CmdLibContentPaths &content, std::string_view path);

// If fileName is below a content base path: (that base path, the rest of fileName with '/' separators)
std::optional<std::pair<std::string, std::string>> CmdLib_HasBasePath(const CmdLibContentPaths &content, std::string_view fileName);

// for compression routines
typedef struct
{
	uint8_t	*data;
	int		count;
} cblock_t;



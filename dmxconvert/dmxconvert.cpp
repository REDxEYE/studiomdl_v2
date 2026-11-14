//========= Copyright Valve Corporation, All rights reserved. ============//
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// $Header: $
// $NoKeywords: $
//
// Converts from any one DMX file format to another
// Can also output SMD or a QCI header from DMX input
//
//=============================================================================


#include "tier0/icommandline.h"
#include "datamodel/idatamodel.h"
#include "filesystem.h"
#include "appframework/app.h"
#include "dmserializers/idmserializers.h"
#include "tier1/utlstring.h"
#include "datamodel/dmelement.h"
#include "tier2/tier2.h"
#include "tier2/tier2dm.h"
#include "common/filesystem_tools.h"
#include "studiomdl/studiomdl_global_state.hpp"


class CDmElement;
StudioMdlState g_studiomdl;

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
class CDmxConvertApp : public CApplication
{
protected:
	bool PreInit() override;
	bool Init() override { return InitDataModel(); }
	int Main() override;
	void Shutdown() override { ShutdownDataModel(); }
};

int main( int argc, char **argv )
{
	CDmxConvertApp app;
	return app.Run( argc, argv );
}


bool CDmxConvertApp::PreInit( )
{
	g_pFileSystem = g_pFullFileSystem;
	return true;
}


//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
int CDmxConvertApp::Main()
{
    MathLib_Init(2.2f, 2.2f, 0.0f, 2.0f, false, false, false, false);
	g_pDataModel->OnlyCreateUntypedElements( true );
	g_pDataModel->SetDefaultElementFactory(nullptr );

	// This bit of hackery allows us to access files on the harddrive
	g_pFullFileSystem->AddSearchPath( "", "LOCAL", PATH_ADD_TO_HEAD ); 

	const auto parm = []( const std::string_view key ) { return std::string( CommandLine()->ParmValue( key, std::string_view() ) ); };
	const std::string inFileName = parm( "-i" );
	std::string outFileName = parm( "-o" );
	const std::string inFormat = parm( "-if" );
	std::string outFormat = parm( "-of" );
	std::string outEncoding = parm( "-oe" );

	if ( inFileName.empty() )
	{
		Msg( "Usage: dmxconvert -i <in file> [-if <in format_hint>] [-o <out file>] [-oe <out encoding>] [-of <out format>]\n" );
		Msg( "If no output file is specified, dmx to dmx conversion will overwrite the input\n" );
		Msg( "Supported DMX file encodings:\n" );
		for ( int i = 0; i < g_pDataModel->GetEncodingCount(); ++i )
		{
			Msg( "   %s\n", g_pDataModel->GetEncodingName( i ) );
		}

		Msg( "Supported DMX file formats:\n" );
		for ( int i = 0; i < g_pDataModel->GetFormatCount(); ++i )
		{
			Msg( "   %s\n", g_pDataModel->GetFormatName( i ) );
		}

		return -1;
	}

	// When reading, keep the CRLF; this will make ReadFile read it in binary format
	// and also append a couple 0s to the end of the buffer.
	DmxHeader_t header;
	CDmElement *pRoot;
	if ( g_pDataModel->RestoreFromFile( inFileName.c_str(), nullptr, inFormat.empty() ? nullptr : inFormat.c_str(), &pRoot, CR_DELETE_NEW, &header ) == DMFILEID_INVALID )
	{
		Error( "Encountered an error reading file \"%s\"!\n", inFileName.c_str() );
		return -1;
	}

	if ( outFormat.empty() )
	{
		outFormat = header.formatName;
		if ( !g_pDataModel->FindFormatUpdater( outFormat.c_str() ) )
		{
			outFormat = "dmx"; // default to generic dmx format for legacy files
		}
	}

	if ( outEncoding.empty() )
	{
		const char *pDefaultEncoding = g_pDataModel->GetDefaultEncoding( outFormat.c_str() );
		if ( !pDefaultEncoding )
		{
			// no default encoding was found, try to convert the file to the generic dmx format
			outFormat = GENERIC_DMX_FORMAT;
			pDefaultEncoding = g_pDataModel->GetDefaultEncoding( outFormat.c_str() );
		}
		if ( pDefaultEncoding )
			outEncoding = pDefaultEncoding;
	}

	if ( outFileName.empty() )
	{
		outFileName = inFileName;
	}

	// TODO - in theory, at some point, we may have converters from pInFormat to pOutFormat
	//		  until then, treat it as a noop, and hope for the best

	if ( !g_pDataModel->SaveToFile( outFileName.c_str(), nullptr, outEncoding.c_str(), outFormat.c_str(), pRoot ) )
	{
		Error( "Encountered an error writing file \"%s\"!\n", outFileName.c_str() );
		return -1;
	}

	g_pDataModel->RemoveFileId( pRoot->GetFileId() );

	return 0;
}




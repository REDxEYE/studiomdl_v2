//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Describes an asset: something that is compiled from sources, 
// in potentially multiple steps, to a compiled resource
//
//=============================================================================


#include "movieobjects/dmemdlmakefile.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "tier2/fileutils.h"




//-----------------------------------------------------------------------------
// Hook into element factories
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeSource, CDmeSource );


//-----------------------------------------------------------------------------
// Construction/destruction 
//-----------------------------------------------------------------------------
void CDmeSource::OnConstruction()
{
	m_DependentMakefile = nullptr;
}

void CDmeSource::OnDestruction()
{
}





//-----------------------------------------------------------------------------
// Hook into element factories
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeMakefile, CDmeMakefile );


//-----------------------------------------------------------------------------
// Construction/destruction 
//-----------------------------------------------------------------------------
void CDmeMakefile::OnConstruction()
{
	m_Sources.Init( this, "sources" );
	m_hOutput = nullptr;
	m_bIsDirty = false;
}

void CDmeMakefile::OnDestruction()
{
	DestroyOutputElement( m_hOutput.Get() );
	m_hOutput = nullptr;
}

						   


//-----------------------------------------------------------------------------
// Gets the path of the makefile
//-----------------------------------------------------------------------------
void CDmeMakefile::GetMakefilePath( char *pFullPath, int nBufLen )
{
	DmFileId_t fileId = GetFileId();
	const char *pFileName = ( fileId != DMFILEID_INVALID ) ? g_pDataModel->GetFileName( fileId ) : "";
	Assert( !pFileName[0] || Q_IsAbsolutePath( pFileName ) );

	Q_ExtractFilePath( pFileName, pFullPath, nBufLen );
}


//-----------------------------------------------------------------------------
// Returns the output directory we expect to compile files into
//-----------------------------------------------------------------------------
bool CDmeMakefile::GetOutputDirectory( char *pFullPath, int nBufLen )
{
	return GetDefaultDirectory( GetOutputDirectoryID(), pFullPath, nBufLen );
}



//-----------------------------------------------------------------------------
// Converts the m_pDefaultDirectoryID field of the DmeMakefileType_t to a full path
//-----------------------------------------------------------------------------
bool CDmeMakefile::GetDefaultDirectory( const char *pDefaultDirectoryID, char *pFullPath, int nBufLen )
{
	if ( StringHasPrefix( pDefaultDirectoryID, "contentdir:" ) )
	{
		pDefaultDirectoryID += 11;
		GetModContentSubdirectory( pDefaultDirectoryID, pFullPath, nBufLen );
		Q_RemoveDotSlashes( pFullPath );
		return true;
	}

	if ( StringHasPrefix( pDefaultDirectoryID, "gamedir:" ) )
	{
		pDefaultDirectoryID += 8;
		GetModSubdirectory( pDefaultDirectoryID, pFullPath, nBufLen );
		Q_RemoveDotSlashes( pFullPath );
		return true;
	}

	if ( StringHasPrefix( pDefaultDirectoryID, "makefiledir:" ) )
	{
		char pMakefilePath[MAX_PATH]{0};
		GetMakefilePath( pMakefilePath, sizeof(pMakefilePath) );
		pDefaultDirectoryID += 12;
		Q_ComposeFileName( pMakefilePath, pDefaultDirectoryID, pFullPath, nBufLen );
		Q_RemoveDotSlashes( pFullPath );
		return true;
	}

	if ( StringHasPrefix( pDefaultDirectoryID, "makefilegamedir:" ) )
	{
		char pMakefilePath[MAX_PATH]{0};
		GetMakefilePath( pMakefilePath, sizeof(pMakefilePath) );

		char pModContentDirectory[MAX_PATH]{0};
		GetModContentSubdirectory(nullptr, pModContentDirectory, sizeof(pModContentDirectory) );

		char pRelativePath[MAX_PATH]{0};
		if ( !Q_MakeRelativePath( pMakefilePath, pModContentDirectory, pRelativePath, sizeof(pRelativePath) ) )
		{
			pFullPath[0] = 0;
			return false;
		}

		char pModDirectory[MAX_PATH]{0};
		GetModSubdirectory(nullptr, pModDirectory, sizeof(pModDirectory) );

		char pMakefileGamePath[MAX_PATH]{0};
		Q_ComposeFileName( pModDirectory, pRelativePath, pMakefileGamePath, sizeof(pMakefileGamePath) );

		pDefaultDirectoryID += 16;
		Q_ComposeFileName( pMakefileGamePath, pDefaultDirectoryID, pFullPath, nBufLen );
		Q_RemoveDotSlashes( pFullPath );
		return true;
	}

	// Assume it's a content subdir
	GetModContentSubdirectory( pDefaultDirectoryID, pFullPath, nBufLen );
	Q_RemoveDotSlashes( pFullPath );
	return true;
}














//-----------------------------------------------------------------------------
// Updates the source names to be relative to a particular path
//-----------------------------------------------------------------------------
bool CDmeMakefile::UpdateSourceNames( const char *pOldRootDir, const char *pNewRootDir, bool bApplyChanges )
{ 
	char pOldSourcePath[ MAX_PATH ];
	char pNewSourcePath[ MAX_PATH ];

	int nCount = m_Sources.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		const char *pOldRelativePath = m_Sources[i]->GetRelativeFileName();
		if ( pOldRelativePath[0] )
		{
			Q_ComposeFileName( pOldRootDir, pOldRelativePath, pOldSourcePath, sizeof(pOldSourcePath) );
			Q_RemoveDotSlashes( pOldSourcePath );
			if ( !Q_MakeRelativePath( pOldSourcePath, pNewRootDir, pNewSourcePath, sizeof(pNewSourcePath) ) )
			{
				Assert( !bApplyChanges );
				return false;
			}
		}
		else
		{
			pNewSourcePath[0] = 0;
		}

		if ( !bApplyChanges )
			continue;

		m_Sources[i]->SetRelativeFileName( pNewSourcePath );
	}
	return true;
}


//-----------------------------------------------------------------------------
// Returns the filename
//-----------------------------------------------------------------------------
const char *CDmeMakefile::GetFileName() const
{
	DmFileId_t fileId = GetFileId();
	return g_pDataModel->GetFileName( fileId );
}


//-----------------------------------------------------------------------------
// Call this to change the file the makefile is stored in
// Will make all sources be relative to this path
//-----------------------------------------------------------------------------
bool CDmeMakefile::SetFileName( const char *pFileName )
{
	if ( !Q_IsAbsolutePath( pFileName ) )
		return false;

	char pOldRootDir[ MAX_PATH ];
	char pNewRootDir[ MAX_PATH ];
	GetMakefilePath( pOldRootDir,  sizeof(pOldRootDir) );
	Q_ExtractFilePath( pFileName, pNewRootDir, sizeof(pNewRootDir) );

	// Gotta do this twice; once to check for validity, once to actually do it
	if ( !UpdateSourceNames( pOldRootDir, pNewRootDir, false ) )
		return false;

	UpdateSourceNames( pOldRootDir, pNewRootDir, true );

	DmFileId_t fileId = GetFileId();
	if ( fileId == DMFILEID_INVALID )
	{
		fileId = g_pDataModel->FindOrCreateFileId( pFileName );
		SetFileId( fileId, TD_DEEP );
	}
	else
	{
		g_pDataModel->SetFileName( fileId, pFileName );
	}

	return true;
}







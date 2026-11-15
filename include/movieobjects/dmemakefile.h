//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =====//
//
// Describes the way to compile a MDL file (eventual replacement for qc)
//
//===========================================================================//

#pragma once



#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "datamodel/dmehandle.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeMakefile;


//-----------------------------------------------------------------------------
// Returns a list of source types and whether there can be only 1 or not
//-----------------------------------------------------------------------------
struct DmeMakefileType_t
{
	const char *m_pTypeName;
	const char *m_pHumanReadableName;
	bool m_bIsSingleton;
	const char *m_pDefaultDirectoryID;	// NOTE: Use CDmeMakefile::GetDefaultDirectory, passing this in to crack it.
	const char *m_pFileFilter;
	const char *m_pFileFilterString;
};


//-----------------------------------------------------------------------------
// Describes an asset source; contains a source name + options
//-----------------------------------------------------------------------------
class CDmeSource : public CDmElement
{
	DEFINE_ELEMENT( CDmeSource, CDmElement );

public:
	// NOTE: Filenames are stored as relative file names in dmesource
	// To resolve them to full paths, use CDmeMakefile::GetSourceFullPath
	const char *GetRelativeFileName() const;
	void SetRelativeFileName( const char *pFileName );

	// If this source can be built by another makefile, return the type of makefile
	// NOTE: This can be a base class of a number of makefile types
	virtual const char **GetSourceMakefileTypes() { return nullptr; }

	// Sets/gets the makefile that was used to build this source
	// NOTE: This information isn't saved and must be reconstructed each frame

private:
	// The makefile that built this source
	CDmeHandle< CDmeMakefile > m_DependentMakefile;	
};


inline const char *CDmeSource::GetRelativeFileName() const
{
	return m_Name;
}

inline void CDmeSource::SetRelativeFileName( const char *pName )
{
	m_Name = pName;
}


//-----------------------------------------------------------------------------
// Describes an asset: something that is compiled from sources
//-----------------------------------------------------------------------------
class CDmeMakefile : public CDmElement
{
	DEFINE_ELEMENT( CDmeMakefile, CDmElement );

public:
	// NOTE: Adding or removing sources of the specified type will invalidate the index
	// NOTE: This index is the same index used in GetSources()
	template< class T >
	T* AddSource( const char *pFullPath );
	template< class T >
	T* SetSingleSource( const char *pFullPath );

	// Gets/sets paths associated with sources

	// Returns the output directory we expect to compile files into
	bool GetOutputDirectory( char *pFullPath, int nBufLen );

	// Call this to change the file the makefile is stored in
	// Will make all sources be relative to this path
	bool SetFileName( const char *pFileName );
	const char *GetFileName() const;

	// Gets a list of all sources of a particular type
	template< class T >
	void GetSources( CUtlVector< CDmeHandle<T> > &sources );

	// Gets a list of all sources, regardless of type

	virtual DmeMakefileType_t *GetMakefileType() { return nullptr; }
	virtual DmeMakefileType_t* GetSourceTypes() { Assert(0); return nullptr; }

	// FIXME: Should we have output types? Not sure...
	virtual void GetOutputs( CUtlVector<CUtlString> &fullPaths ) { Assert(0); }

	// Converts the m_pDefaultDirectoryID field of the DmeMakefileType_t to a full path
	bool GetDefaultDirectory( const char *pDefaultDirectoryID, char *pFullPath, int nBufLen );

	// These methods are used to help traverse a dependency graph.
	// They work with information that is not saved.

	// Performs compilation steps

	// Dirty, dirty!
	bool IsDirty() const;
	void SetDirty( bool bDirty );

protected:

	// Gets the path of the makefile
	void GetMakefilePath( char *pFullPath, int nBufLen );

private:
	// Inherited classes should re-implement these methods
	virtual CDmElement *CreateOutputElement( ) { return nullptr; }
	virtual void DestroyOutputElement( CDmElement *pOutput ) { }
	virtual const char *GetOutputDirectoryID() { return "makefilegamedir:"; }

private:

	// Updates the source names to be relative to a particular path
	bool UpdateSourceNames( const char *pOldRootDir, const char *pNewRootDir, bool bApplyChanges );

	CDmaElementArray< CDmeSource > m_Sources;
	CDmeHandle< CDmElement > m_hOutput;
	bool m_bIsDirty;
};


//-----------------------------------------------------------------------------
// Dirty, dirty!
//-----------------------------------------------------------------------------
inline bool CDmeMakefile::IsDirty() const
{
	return m_bIsDirty;
}

inline void CDmeMakefile::SetDirty( bool bDirty )
{
	m_bIsDirty = bDirty;
}


//-----------------------------------------------------------------------------
// Gets a list of all sources of a particular type
//-----------------------------------------------------------------------------
template< class T >
void CDmeMakefile::GetSources( CUtlVector< CDmeHandle<T> > &sources )
{
	int nCount = m_Sources.Count();
	sources.EnsureCapacity( nCount );
	for ( int i = 0; i < nCount; ++i )
	{
		if ( m_Sources[i]->IsA( T::GetStaticTypeSymbol() ) )
		{
			int j = sources.AddToTail();
			sources[j] = CastElement<T>( m_Sources[i] );
		}
	}
}


//-----------------------------------------------------------------------------
// Template version to add a source
//-----------------------------------------------------------------------------
template< class T >
T* CDmeMakefile::AddSource( const char *pFullPath )
{
	return CastElement< T >( AddSource( T::GetStaticTypeSymbol().String(), pFullPath ) );
}


//-----------------------------------------------------------------------------
// Template version to set a single source
//-----------------------------------------------------------------------------
template< class T >
T* CDmeMakefile::SetSingleSource( const char *pFullPath )
{
	return CastElement< T >( SetSingleSource( T::GetStaticTypeSymbol().String(), pFullPath ) );
}



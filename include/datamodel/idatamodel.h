//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"
#include "tier1/utlsymbollarge.h"
#include "datamodel/dmattributetypes.h"
#include "datamodel/dmxheader.h"

//-----------------------------------------------------------------------------
// Forward declarations: 
//-----------------------------------------------------------------------------
class CDmAttribute;
class CDmElement;
class IDmeOperator;
class IElementForKeyValueCallback;
class CDmElementFactoryHelper;

struct DmValueBase_t;
class CUtlBuffer;
class KeyValues;
class CUtlCharConversion;
class CElementIdHash;

// Use this for profiling dmx binary unserialization times
//#define DMX_PROFILE_UNSERIALIZE 2
// If you set it to 2, then SFM will exit(-1) right after the first session is loaded (so you can see the PROFILE_SCOPE counters)

#if defined( DMX_PROFILE_UNSERIALIZE )
#define DMX_PROFILE_SCOPE( name ) PROFILE_SCOPE( name )
#else
#define DMX_PROFILE_SCOPE( name )
#endif


//-----------------------------------------------------------------------------
// element framework phases
//-----------------------------------------------------------------------------
enum DmPhase_t
{
	PH_EDIT,
	PH_EDIT_APPLY,
	PH_EDIT_RESOLVE,
	PH_DEPENDENCY,
	PH_OPERATE,
	PH_OPERATE_RESOLVE,
	PH_OUTPUT,
};

//-----------------------------------------------------------------------------
// Handle to an CDmAttribute
//-----------------------------------------------------------------------------
enum DmAttributeHandle_t:uint64_t
{  
	DMATTRIBUTE_HANDLE_INVALID = 0xffffffff
};

//-----------------------------------------------------------------------------
// Handle to an DmAttributeList_t
//-----------------------------------------------------------------------------
enum DmAttributeReferenceIterator_t:uint64_t
{  
	DMATTRIBUTE_REFERENCE_ITERATOR_INVALID = 0,
};

//-----------------------------------------------------------------------------
// element framework interface
//-----------------------------------------------------------------------------
class IDmElementFramework
{
public:
	// Called by the application at startup/exit (InitDataModel/ShutdownDataModel)
	virtual bool Init() = 0;
	virtual void Shutdown() = 0;

	virtual DmPhase_t GetPhase() = 0;

	virtual void SetOperators( const CUtlVector< IDmeOperator* > &operators ) = 0;

	virtual void BeginEdit() = 0; // ends in edit phase, forces apply/resolve if from edit phase
	virtual void Operate( bool bResolve ) = 0; // ends in output phase
	virtual void Resolve() = 0;

	virtual const CUtlVector< IDmeOperator* > &GetSortedOperators() const = 0;
};



class IDmeElementCreated
{
	public:
		virtual void OnElementCreated( CDmElement *pElement ) = 0;
};


//-----------------------------------------------------------------------------
// Main interface
//-----------------------------------------------------------------------------
extern IDmElementFramework *g_pDmElementFramework;


//-----------------------------------------------------------------------------
// datamodel operator interface - for all elements that need to be sorted in the operator dependency graph
//-----------------------------------------------------------------------------
class IDmeOperator
{
public:
	virtual bool IsDirty() = 0; // ie needs to operate
	virtual void Operate() = 0;
	virtual const char* GetOperatorName() const { return "Unknown"; }

	virtual void GetInputAttributes ( CUtlVector< CDmAttribute * > &attrs ) = 0;
	virtual void GetOutputAttributes( CUtlVector< CDmAttribute * > &attrs ) = 0;

	virtual void SetSortKey( int key ) = 0;
	virtual int GetSortKey() const = 0;
};

//-----------------------------------------------------------------------------
// Class factory methods: 
//-----------------------------------------------------------------------------
class IDmElementFactory
{
public:
	// Creation, destruction
	virtual CDmElement* Create( DmElementHandle_t handle, const char *pElementType, const char *pElementName, DmFileId_t fileid, const DmObjectId_t &id ) = 0;
	virtual void Destroy( DmElementHandle_t hElement ) = 0;
	virtual void AddOnElementCreatedCallback( IDmeElementCreated *callback ) = 0;
	virtual void RemoveOnElementCreatedCallback( IDmeElementCreated *callback ) = 0;
	virtual void OnElementCreated( CDmElement* pElement ) = 0;
};


//-----------------------------------------------------------------------------
// Various serialization methods can be installed into the data model factory 
//-----------------------------------------------------------------------------
enum DmConflictResolution_t
{
	CR_DELETE_NEW,
	CR_DELETE_OLD,
	CR_COPY_NEW,
	CR_FORCE_COPY,
};

// convert files to elements and back
// current file encodings supported: binary, keyvalues2, keyvalues2_flat, keyvalues (vmf/vmt/actbusy), text? (qc/obj)
class IDmSerializer
{
public:
	virtual const char *GetName() const = 0;
	virtual const char *GetDescription() const = 0;
	virtual bool IsBinaryFormat() const = 0;
	virtual bool StoresVersionInFile() const = 0;
	virtual int GetCurrentVersion() const = 0;

	// Write into the UtlBuffer, return true if successful
	// if we decide to implement non-identity conversions between formats on write, then the source and dest format will need to be passed in here
	virtual bool Serialize( CUtlBuffer &buf, CDmElement *pRoot ) = 0;

	// Read from the UtlBuffer, return true if successful, and return the read-in root in ppRoot.
	virtual bool Unserialize( CUtlBuffer &buf, const char *pEncodingName, int nEncodingVersion,
							  const char *pSourceFormatName, int nSourceFormatVersion,
							  DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement **ppRoot ) = 0;

	// Methods used for importing (only should return non-NULL for serializers that return false from StoresVersionInFile)
	virtual const char *GetImportedFormat() const = 0;
 	virtual int GetImportedVersion() const = 0;
};

// convert legacy elements to non-legacy elements
// legacy formats include: sfm_vN, binary_vN, keyvalues2_v1, keyvalues2_flat_v1
//   where N is a version number (1..9 for sfm, 1..2 for binary)
class IDmLegacyUpdater
{
public:
	virtual const char *GetName() const = 0;
	virtual bool IsLatestVersion() const = 0;

	// Updates ppRoot to first non-legacy generic dmx format, returns false if the conversion fails
	virtual bool Update( CDmElement **ppRoot ) = 0;
};

// converts old elements to new elements
// current formats include: sfm session, animset presets, particle definitions, exported maya character, etc.
class IDmFormatUpdater
{
public:
	virtual const char *GetName() const = 0;
	virtual const char *GetDescription() const = 0;
	virtual const char *GetExtension() const = 0;
	virtual int GetCurrentVersion() const = 0;
	virtual const char *GetDefaultEncoding() const = 0;

	// Converts pSourceRoot from nSourceVersion to the current version, returns false if the conversion fails
	virtual bool Update( CDmElement **pRoot, int nSourceVersion ) = 0;
};

//-----------------------------------------------------------------------------
// Interface for callbacks to supply element types for specific keys inside keyvalues files
//-----------------------------------------------------------------------------
class IElementForKeyValueCallback
{
public:
	virtual const char *GetElementForKeyValue( const char *pszKeyName, int iNestingLevel ) = 0;
};


//-----------------------------------------------------------------------------
// Purpose: Can be installed to be called back when data changes
//-----------------------------------------------------------------------------
enum DmNotifySource_t
{
	// Sources
	NOTIFY_SOURCE_APPLICATION = 0,
	NOTIFY_SOURCE_FIRST_DME_CONTROL_SOURCE = 4,	// Sources from dme_controls starts here
	NOTIFY_SOURCE_FIRST_APPLICATION_SOURCE = 8, // Sources from applications starts here
};

enum DmNotifyFlags_t
{
	// Does this dirty the document?
	NOTIFY_SOURCE_BITS					= 8,
	NOTIFY_SETDIRTYFLAG					= (1<<NOTIFY_SOURCE_BITS),

	// Type of change (note NOTIFY_CHANGE_TOPOLOGICAL/NOTIFY_CHANGE_ATTRIBUTE_VALUE/NOTIFY_CHANGE_ATTRIBUTE_ARRAY_SIZE
	// are set by the Datamodel itself)
	NOTIFY_CHANGE_TOPOLOGICAL			= (1<<(NOTIFY_SOURCE_BITS+4)),	// Element created, destroyed, element attribute or element array attribute value changed
	NOTIFY_CHANGE_ATTRIBUTE_VALUE		= (1<<(NOTIFY_SOURCE_BITS+5)),	// Non-element attribute value changed
	NOTIFY_CHANGE_ATTRIBUTE_ARRAY_SIZE	= (1<<(NOTIFY_SOURCE_BITS+6)),	// Non-element array attribute added or removed
	NOTIFY_CHANGE_OTHER					= (1<<(NOTIFY_SOURCE_BITS+7)),	// Non attribute related change (a change in UI, for example)
	NOTIFY_FLAG_FIRST_APPLICATION_BIT	= NOTIFY_SOURCE_BITS+8,			// First bit # the app can use for its own notify flags

	NOTIFY_CHANGE_MASK = ( NOTIFY_CHANGE_TOPOLOGICAL | NOTIFY_CHANGE_ATTRIBUTE_VALUE | NOTIFY_CHANGE_ATTRIBUTE_ARRAY_SIZE | NOTIFY_CHANGE_OTHER ),

};

class IDmNotify
{
public:
	// See DmNotifySource_t and DmNotifyFlags_t
	virtual void NotifyDataChanged( const char *pReason, int nNotifySource, int nNotifyFlags ) = 0;
};


//-----------------------------------------------------------------------------
// traversal depth for copy, search, and other element/attribute traversals
//-----------------------------------------------------------------------------
enum TraversalDepth_t
{
	TD_ALL,		// traverse all attributes
	TD_DEEP,	// traverse attributes with FATTRIB_NEVERCOPY clear
	TD_SHALLOW,	// traverse attributes with FATTRIB_MUSTCOPY set
	TD_NONE,	// don't traverse any attributes
};

//-----------------------------------------------------------------------------
// Main interface for creation of all IDmeElements: 
//-----------------------------------------------------------------------------
class IDataModel
{
public:	
	// Called by the application at startup/exit (InitDataModel/ShutdownDataModel)
	virtual bool			Init() = 0;
	virtual void			Shutdown() = 0;

	// Installs factories used to instance elements
	virtual void			AddElementFactory( CDmElementFactoryHelper *pFactoryHelper ) = 0;
	virtual CDmElementFactoryHelper	*GetElementFactoryHelper( const char *pClassName ) = 0;

	// This factory will be used to instance all elements whose type name isn't found.
	virtual void			SetDefaultElementFactory( IDmElementFactory *pFactory ) = 0;

	virtual int				GetFirstFactory() const = 0;
	virtual int				GetNextFactory( int index ) const = 0;
	virtual bool			IsValidFactory( int index ) const = 0;
	virtual const char		*GetFactoryName( int index ) const = 0;

	// create/destroy element methods - proxies to installed element factories
	virtual DmElementHandle_t	CreateElement( CUtlSymbolLarge typeSymbol, const char *pElementName, DmFileId_t fileid, const DmObjectId_t *pObjectID = nullptr) = 0;
	virtual DmElementHandle_t	CreateElement( const char *pTypeName, const char *pElementName, DmFileId_t fileid, const DmObjectId_t *pObjectID = nullptr) = 0;
	virtual void				DestroyElement( DmElementHandle_t hElement ) = 0;

	// element handle related methods
	virtual	CDmElement*			GetElement			( DmElementHandle_t hElement ) const = 0;
	virtual CUtlSymbolLarge		GetElementType	    ( DmElementHandle_t hElement ) const = 0;
	virtual const char*			GetElementName	    ( DmElementHandle_t hElement ) const = 0;
	virtual const DmObjectId_t&	GetElementId	    ( DmElementHandle_t hElement ) const = 0;

	virtual const char*			GetAttributeNameForType( DmAttributeType_t attType ) const = 0;
	virtual DmAttributeType_t	GetAttributeTypeForName( const char *name ) const = 0;

	// Adds various serializers and updaters
	virtual void			AddSerializer( IDmSerializer *pSerializer ) = 0;
	virtual void			AddLegacyUpdater( IDmLegacyUpdater *pUpdater ) = 0;
	virtual void			AddFormatUpdater( IDmFormatUpdater *pUpdater ) = 0;

	// file format methods
	virtual const char*		GetFormatExtension( const char *pFormatName ) = 0;
	virtual const char*		GetFormatDescription( const char *pFormatName ) = 0;
	virtual int				GetFormatCount() const = 0;
	virtual const char *	GetFormatName( int i ) const = 0;
	virtual const char *	GetDefaultEncoding( const char *pFormatName ) = 0;

	// file encoding methods
	virtual int				GetEncodingCount() const = 0;
	virtual const char *	GetEncodingName( int i ) const = 0;
	virtual bool			IsEncodingBinary( const char *pEncodingName ) const = 0;
	virtual bool			DoesEncodingStoreVersionInFile( const char *pEncodingName ) const = 0;

	// For serialization, set the delimiter rules
	// These methods are meant to be used by importer/exporters
	virtual void			SetSerializationDelimiter( CUtlCharConversion *pConv ) = 0;
	virtual void			SetSerializationArrayDelimiter( const char *pDelimiter ) = 0;

	// used to skip auto-creation of child elements during unserialization
	virtual bool			IsUnserializing() = 0;

	// Serialization of a element tree into a utlbuffer
	virtual bool			Serialize( CUtlBuffer &outBuf, const char *pEncodingName, const char *pFormatName, DmElementHandle_t hRoot ) = 0;

	// Unserializes, returns the root of the unserialized tree in hRoot 
	// The file name passed in is simply for error messages and fileid creation
	virtual bool			Unserialize( CUtlBuffer &inBuf, const char *pEncodingName, const char *pRequiredFormat, const char *pUnused,
										 const char *pFileName, DmConflictResolution_t idConflictResolution, DmElementHandle_t &hRoot ) = 0;

	// converts from elements from old file formats to elements for the current file format
	virtual bool			UpdateUnserializedElements( const char *pSourceFormatName, int nSourceFormatVersion,
														DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement **ppRoot ) = 0;

	// force creation of untyped elements, ignoring type
	virtual void			OnlyCreateUntypedElements( bool bEnable ) = 0;

	// Finds a serializer by name
	virtual IDmSerializer*		FindSerializer( const char *pEncodingName ) const = 0;
	virtual IDmLegacyUpdater*	FindLegacyUpdater( const char *pLegacyFormatName ) const = 0;
	virtual IDmFormatUpdater*	FindFormatUpdater( const char *pFormatName ) const = 0;

	// saves element tree to a file
	virtual bool				SaveToFile( const char *pFileName, const char *pPathID, const char *pEncodingName, const char *pFormatName, CDmElement *pRoot ) = 0;

	// restores file into an element tree
	// NOTE: Format name is only used here for those formats which don't store
	// the format name in the file. Use NULL for those formats which store the 
	// format name in the file.
	virtual DmFileId_t			RestoreFromFile( const char *pFileName, const char *pPathID, const char *pEncodingHint, CDmElement **ppRoot, DmConflictResolution_t idConflictResolution = CR_DELETE_NEW, DmxHeader_t *pHeaderOut = nullptr) = 0;

	// Sets the name of the DME element to create in keyvalues serialization
	virtual void			SetKeyValuesElementCallback( IElementForKeyValueCallback *pCallbackInterface ) = 0;
	virtual const char		*GetKeyValuesElementName( const char *pszKeyName, int iNestingLevel ) = 0;

	// Global symbol table for the datamodel system
	virtual CUtlSymbolLarge		GetSymbol( const char *pString ) = 0;
	// Once you have a CUtlSymbolLarge, don't need an external API to get the char *, just use the String() method.

	// Returns the total number of elements allocated at the moment
	virtual int				GetMaxNumberOfElements() = 0;
	virtual int				GetElementsAllocatedSoFar() = 0;
	virtual int				GetAllocatedAttributeCount() = 0;
	virtual int				GetAllocatedElementCount() = 0;
	virtual DmElementHandle_t	FirstAllocatedElement() = 0;
	virtual DmElementHandle_t	NextAllocatedElement( DmElementHandle_t it ) = 0;

	// estimate memory usage
	virtual int				EstimateMemoryUsage( DmElementHandle_t hElement, TraversalDepth_t depth ) = 0;

	// Handles to attributes
	virtual CDmAttribute *	GetAttribute( DmAttributeHandle_t h ) = 0;
	virtual bool			IsAttributeHandleValid( DmAttributeHandle_t h ) const = 0;

	// file id reference methods
	virtual int					NumFileIds() = 0;
	virtual DmFileId_t			GetFileId( int i ) = 0;
	virtual DmFileId_t			FindOrCreateFileId( const char *pFilename ) = 0;
	virtual void				RemoveFileId( DmFileId_t fileid ) = 0;
	virtual DmFileId_t			GetFileId( const char *pFilename ) = 0;
	virtual const char *		GetFileName( DmFileId_t fileid ) = 0;
	virtual void				SetFileName( DmFileId_t fileid, const char *pFileName ) = 0;
	virtual const char *		GetFileFormat( DmFileId_t fileid ) = 0;
	virtual void				SetFileFormat( DmFileId_t fileid, const char *pFormat ) = 0;
	virtual DmElementHandle_t	GetFileRoot( DmFileId_t fileid ) = 0;
	virtual void				SetFileRoot( DmFileId_t fileid, DmElementHandle_t hRoot ) = 0;
	virtual int64_t				GetFileModificationUTCTime( DmFileId_t fileid ) = 0;
	virtual int64_t				GetCurrentUTCTime() = 0;
	virtual void				UTCTimeToString( char *pString, int maxChars, int64_t fileTime ) = 0;
	virtual bool				IsFileLoaded( DmFileId_t fileid ) = 0;
	virtual void				MarkFileLoaded( DmFileId_t fileid ) = 0;
	virtual void				UnloadFile( DmFileId_t fileid ) = 0;
	virtual int					NumElementsInFile( DmFileId_t fileid ) = 0;

	virtual void				DontAutoDelete( DmElementHandle_t hElement ) = 0;

	// handle validity methods - these shouldn't really be here
	virtual void			MarkHandleInvalid( DmElementHandle_t hElement ) = 0;
	virtual void			MarkHandleValid( DmElementHandle_t hElement ) = 0;

	virtual DmElementHandle_t	FindElement( const DmObjectId_t &id ) = 0;
	virtual void				GetExistingElements( CElementIdHash &hash ) const = 0;

	virtual	DmAttributeReferenceIterator_t	FirstAttributeReferencingElement( DmElementHandle_t hElement ) = 0;
	virtual DmAttributeReferenceIterator_t	NextAttributeReferencingElement( DmAttributeReferenceIterator_t hAttrIter ) = 0;
	virtual CDmAttribute *					GetAttribute( DmAttributeReferenceIterator_t hAttrIter ) = 0;

	// Install, remove notify callbacks
	virtual bool InstallNotificationCallback( IDmNotify *pNotify ) = 0;
	virtual void RemoveNotificationCallback( IDmNotify *pNotify ) = 0;
	virtual bool IsSuppressingNotify( ) const = 0;
	virtual void SetSuppressingNotify( bool bSuppress ) = 0;
	virtual void PushNotificationScope( const char *pReason, int nNotifySource, int nNotifyFlags ) = 0;
	virtual void PopNotificationScope( bool bAbort = false ) = 0;

	virtual bool HasElementFactory( const char *pElementType ) const = 0;

	// Displays memory stats to the console (passing in invalid handle causes memory for all elements to be estimated, otherwise it's only
	//  the element and it's recursive children)
	virtual void DisplayMemoryStats( DmElementHandle_t hElement = DMELEMENT_HANDLE_INVALID ) = 0;

	// Is this file a DMX file?
	virtual bool IsDMXFormat( CUtlBuffer &buf ) const = 0;

	// Dump the symbol table to the console
	virtual void DumpSymbolTable() = 0;
	
	virtual void AddOnElementCreatedCallback( const char *pElementType, IDmeElementCreated *callback ) = 0;
	virtual void RemoveOnElementCreatedCallback( const char *pElementType, IDmeElementCreated *callback ) = 0;
};



//-----------------------------------------------------------------------------
// Main interface accessor
//-----------------------------------------------------------------------------
extern IDataModel *g_pDataModel;


//-----------------------------------------------------------------------------
// Purpose: Simple helper class to disable NotifyDataChanged from current scope
//-----------------------------------------------------------------------------
class CNotifyScopeGuard
{
public:
	CNotifyScopeGuard( const char *pReason, int nNotifySource, int nNotifyFlags, IDmNotify *pNotify = nullptr)
	{
		m_bReleased = false;
		m_pNotify = nullptr;
		g_pDataModel->PushNotificationScope( pReason, nNotifySource, nNotifyFlags );
		if ( pNotify )
		{
			if ( g_pDataModel->InstallNotificationCallback( pNotify ) )
			{
				m_pNotify = pNotify;
			}
		}
	};

	~CNotifyScopeGuard()
	{
		Release();
	}

	void Release()
	{
		// Releases the guard...
		if ( !m_bReleased )
		{
			g_pDataModel->PopNotificationScope( );
			if ( m_pNotify )
			{
				g_pDataModel->RemoveNotificationCallback( m_pNotify );
				m_pNotify = nullptr;
			}
			m_bReleased = true;
		}
	}

private:
	CNotifyScopeGuard( const CNotifyScopeGuard& g );

private:
	IDmNotify *m_pNotify;
	bool m_bReleased;
};


#define DEFINE_SOURCE_NOTIFY_SCOPE_GUARD( _classnameprefix, _source )	\
	class C ## _classnameprefix ## NotifyScopeGuard : public CNotifyScopeGuard	\
	{															\
		typedef CNotifyScopeGuard BaseClass;					\
																\
	public:														\
		C ## _classnameprefix ## NotifyScopeGuard( const char *pReason, int nNotifyFlags, IDmNotify *pNotify = NULL ) : \
			BaseClass( pReason, _source, nNotifyFlags, pNotify )\
		{														\
		}														\
	}


//-----------------------------------------------------------------------------
// Purpose: Simple helper class to disable notifications when in scope
//-----------------------------------------------------------------------------
class CChangeNotifyScopeGuard
{
public:
	CChangeNotifyScopeGuard( bool bNewState )
	{
		m_bReleased = false;
		m_bOldValue = g_pDataModel->IsSuppressingNotify();
		g_pDataModel->SetSuppressingNotify( bNewState );
	};

	~CChangeNotifyScopeGuard()
	{
		Release();
	}

	void Release()
	{
		// Releases the guard...
		if ( !m_bReleased )
		{
			g_pDataModel->SetSuppressingNotify( m_bOldValue );
			m_bReleased = true;
		}
	}

private:
	bool m_bOldValue;
	bool m_bReleased;
};

class CDisableNotifyScopeGuard : public CChangeNotifyScopeGuard
{
	typedef CChangeNotifyScopeGuard BaseClass;

public:
	CDisableNotifyScopeGuard() : BaseClass( true ) { }

private:
	CDisableNotifyScopeGuard( const CDisableNotifyScopeGuard& g );
};

class CEnableNotifyScopeGuard : public CChangeNotifyScopeGuard
{
	typedef CChangeNotifyScopeGuard BaseClass;

public:
	CEnableNotifyScopeGuard( ) : BaseClass( false ) { }

private:
	CEnableNotifyScopeGuard( const CEnableNotifyScopeGuard& g );
};


//-----------------------------------------------------------------------------
// Standard notify guards for the application
//-----------------------------------------------------------------------------
DEFINE_SOURCE_NOTIFY_SCOPE_GUARD( App, NOTIFY_SOURCE_APPLICATION );



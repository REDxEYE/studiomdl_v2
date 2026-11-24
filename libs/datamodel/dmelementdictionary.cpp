//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "dmelementdictionary.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "datamodel/idatamodel.h"
#include "datamodel.h"


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CDmElementDictionary::CDmElementDictionary() = default;


//-----------------------------------------------------------------------------
// Clears the dictionary
//-----------------------------------------------------------------------------
void CDmElementDictionary::Clear()
{
	m_Dict.Purge();
	m_Attributes.Purge();
	m_ArrayAttributes.Purge();
	m_elementsToDelete.Purge();
	m_idmap.clear();
}

	
//-----------------------------------------------------------------------------
// Inserts an element into the table
//-----------------------------------------------------------------------------
DmElementDictHandle_t CDmElementDictionary::InsertElement( CDmElement *pElement )
{
	// Insert it into the reconnection table
	return m_Dict.AddToTail( pElement ? pElement->GetHandle() : DMELEMENT_HANDLE_INVALID );
}


//-----------------------------------------------------------------------------
// Returns a particular element
//-----------------------------------------------------------------------------
CDmElement *CDmElementDictionary::GetElement( DmElementDictHandle_t handle )
{
	if ( handle == ELEMENT_DICT_HANDLE_INVALID )
		return nullptr;

	return g_pDataModel->GetElement( m_Dict[ handle ] );
}


//-----------------------------------------------------------------------------
// Adds an attribute to the fixup list
//-----------------------------------------------------------------------------
void CDmElementDictionary::AddAttribute( CDmAttribute *pAttribute, const DmObjectId_t &objectId )
{
	if ( m_elementsToDelete.Find( pAttribute->GetOwner()->GetHandle() ) != m_elementsToDelete.InvalidIndex() )
		return; // don't add attributes if their element is being deleted

	int i = m_Attributes.AddToTail();
	m_Attributes[i].m_bId = true;
	m_Attributes[i].m_pAttribute = pAttribute;
	CopyUniqueId( objectId, &m_Attributes[i].m_ObjectId );
}


//-----------------------------------------------------------------------------
// Adds an element of an attribute array to the fixup list
//-----------------------------------------------------------------------------
void CDmElementDictionary::AddArrayAttribute( CDmAttribute *pAttribute, DmElementDictHandle_t hElement )
{
	if ( m_elementsToDelete.Find( pAttribute->GetOwner()->GetHandle() ) != m_elementsToDelete.InvalidIndex() )
		return; // don't add attributes if their element is being deleted

	int i = m_ArrayAttributes.AddToTail();
	m_ArrayAttributes[i].m_bId = false;
	m_ArrayAttributes[i].m_pAttribute = pAttribute;
	m_ArrayAttributes[i].m_hElement = hElement;
}

void CDmElementDictionary::AddArrayAttribute( CDmAttribute *pAttribute, const DmObjectId_t &objectId )
{
	if ( m_elementsToDelete.Find( pAttribute->GetOwner()->GetHandle() ) != m_elementsToDelete.InvalidIndex() )
		return; // don't add attributes if their element is being deleted

	int i = m_ArrayAttributes.AddToTail();
	m_ArrayAttributes[i].m_bId = true;
	m_ArrayAttributes[i].m_pAttribute = pAttribute;
	CopyUniqueId( objectId, &m_ArrayAttributes[i].m_ObjectId );
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CDmElementDictionary::RemoveAttributeInfosOfElement( AttributeList_t &attributes, DmElementHandle_t hElement )
{
	while ( attributes.size() > 0 && attributes.back().m_pAttribute->GetOwner()->GetHandle() == hElement )
	{
		attributes.Remove( attributes.size() - 1 );
	}
}

DmElementHandle_t CDmElementDictionary::SetElementId( DmElementDictHandle_t hDictHandle, const DmObjectId_t &newId, DmConflictResolution_t idConflictResolution )
{
	DmElementHandle_t hElement = m_Dict[ hDictHandle ];
	CDmElement *pElement = g_pDataModel->GetElement( hElement );
	Assert( pElement );
	if ( !pElement )
		return DMELEMENT_HANDLE_INVALID;

	const DmObjectId_t &oldId = pElement->GetId();

	if ( idConflictResolution == CR_FORCE_COPY )
	{
		m_idmap.emplace( newId, oldId ); // map the newId back to the old id, and keep the old id
		return hElement;
	}

	DmElementHandle_t newHandle = g_pDataModelImp->ChangeElementId( hElement, oldId, newId );
	if ( newHandle != DMELEMENT_HANDLE_INVALID )
	{
		// if ChangeElementId returns a handle, the id has been changed
		if ( newHandle != hElement )
		{
			int i = m_Dict.Find( hElement );
			if ( i != m_Dict.InvalidIndex() )
			{
				m_Dict[ i ] = newHandle;
			}
		}
		return newHandle; // either keeping the old handle, with the new id, or found a new handle associated with that new id
	}

	// id not changed because that id is already in use
	if ( idConflictResolution == CR_DELETE_NEW )
	{
		DmElementHandle_t hExistingElement = g_pDataModel->FindElement( newId );

		int i = m_elementsToDelete.AddToTail( );
		m_elementsToDelete[i].m_hDictHandle = hDictHandle;
		m_elementsToDelete[i].m_hElementToDelete = hElement;
		m_elementsToDelete[i].m_hReplacementElement = hExistingElement;

		// remove all element ref attributes read in before the id (typically none)
		RemoveAttributeInfosOfElement( m_Attributes, hElement );
		RemoveAttributeInfosOfElement( m_ArrayAttributes, hElement );

		return DMELEMENT_HANDLE_INVALID;
	}

	if ( idConflictResolution == CR_DELETE_OLD )
	{
		DmElementHandle_t hExistingElement = g_pDataModel->FindElement( newId );
		Assert( hExistingElement != DMELEMENT_HANDLE_INVALID );
		if ( hExistingElement == DMELEMENT_HANDLE_INVALID )
			return DMELEMENT_HANDLE_INVALID; // unexpected error in ChangeElementId (failed due to something other than a conflict)

		g_pDataModelImp->DeleteElement( hExistingElement, HR_NEVER ); // need to keep the handle around until ChangeElemendId
		newHandle = g_pDataModelImp->ChangeElementId( hElement, oldId, newId );
		Assert( newHandle == hExistingElement );

		int i = m_Dict.Find( hElement );
		if ( i != m_Dict.InvalidIndex() )
		{
			m_Dict[ i ] = newHandle;
		}

		return newHandle;
	}

	if ( idConflictResolution == CR_COPY_NEW )
	{
		m_idmap.emplace( newId, oldId ); // map the newId back to the old id, and keep the old id
		return hElement;
	}

	Assert( 0 );
	return DMELEMENT_HANDLE_INVALID;
}



//-----------------------------------------------------------------------------
// Hook up all element references (which were unserialized as object ids)
//-----------------------------------------------------------------------------
void CDmElementDictionary::HookUpElementAttributes()
{
	int n = m_Attributes.size();
	for ( int i = 0; i < n; ++i )
	{
		Assert( m_Attributes[i].m_pAttribute->GetType() == AT_ELEMENT );
		Assert( m_Attributes[i].m_bId );

		const auto it = m_idmap.find( m_Attributes[i].m_ObjectId );
		const DmObjectId_t &id = it == m_idmap.end() ? m_Attributes[i].m_ObjectId : it->second;

		// search id->handle table (both loaded and unloaded) for id, and if not found, create a new handle, map it to the id and return it
		DmElementHandle_t hElement = g_pDataModelImp->FindOrCreateElementHandle( id );
		m_Attributes[i].m_pAttribute->SetValue<DmElementHandle_t>( hElement );
	}
}


//-----------------------------------------------------------------------------
// Hook up all element array references
//-----------------------------------------------------------------------------
void CDmElementDictionary::HookUpElementArrayAttributes()
{
	// Find unique array attributes; we need to clear them all before adding stuff.
	// This clears them of stuff added during their construction phase.
	int n = m_ArrayAttributes.size();
	CUtlRBTree< CDmAttribute*, unsigned short >	lookup( 0, n, DefLessFunc(CDmAttribute*) );
	for ( int i = 0; i < n; ++i )
	{
		Assert( m_ArrayAttributes[i].m_pAttribute->GetType() == AT_ELEMENT_ARRAY );
		CDmAttribute *pElementArray = m_ArrayAttributes[i].m_pAttribute;
		CDmrElementArray<> array( pElementArray );
		if ( lookup.Find( pElementArray ) == lookup.InvalidIndex() )
		{
			array.RemoveAll();
			lookup.Insert( pElementArray );
		}
	}

	for ( int i = 0; i < n; ++i )
	{
		Assert( m_ArrayAttributes[i].m_pAttribute->GetType() == AT_ELEMENT_ARRAY );

		CDmrElementArray<> array( m_ArrayAttributes[i].m_pAttribute );

		if ( !m_ArrayAttributes[i].m_bId )
		{
			CDmElement *pElement = GetElement( m_ArrayAttributes[i].m_hElement );
			array.AddToTail( pElement );
		}
		else
		{
			const auto it = m_idmap.find( m_ArrayAttributes[i].m_ObjectId );
			const DmObjectId_t &id = it == m_idmap.end() ? m_ArrayAttributes[i].m_ObjectId : it->second;

			// search id->handle table (both loaded and unloaded) for id, and if not found, create a new handle, map it to the id and return it
			DmElementHandle_t hElement = g_pDataModelImp->FindOrCreateElementHandle( id );
			int nIndex = array.AddToTail();
			array.SetHandle( nIndex, hElement );
		}
	}
}


//-----------------------------------------------------------------------------
// Hook up all element references (which were unserialized as object ids)
//-----------------------------------------------------------------------------
void CDmElementDictionary::HookUpElementReferences()
{
	int nElementsToDelete = m_elementsToDelete.size();
	for ( int i = 0; i < nElementsToDelete; ++i )
	{
		DmElementDictHandle_t hDictIndex = m_elementsToDelete[i].m_hDictHandle;
		DmElementHandle_t hElement = m_Dict[ hDictIndex ];
		g_pDataModelImp->DeleteElement( hElement );
		m_Dict[ hDictIndex ] = m_elementsToDelete[i].m_hReplacementElement;
	}

	HookUpElementArrayAttributes();
	HookUpElementAttributes();
}



//-----------------------------------------------------------------------------
//
// Element dictionary used in serialization
//
//-----------------------------------------------------------------------------
CDmElementSerializationDictionary::CDmElementSerializationDictionary() = default;


//-----------------------------------------------------------------------------
// Finds the handle of the element
//-----------------------------------------------------------------------------
DmElementDictHandle_t CDmElementSerializationDictionary::Find( CDmElement *pElement )
{
	const auto it = m_indices.find( pElement );
	return it == m_indices.end() ? ELEMENT_DICT_HANDLE_INVALID : it->second;
}

	
//-----------------------------------------------------------------------------
// Creates the list of all things to serialize
//-----------------------------------------------------------------------------
void CDmElementSerializationDictionary::BuildElementList_R( CDmElement *pElement, bool bFlatMode, bool bIsRoot )
{
	if ( !pElement )
		return;

	// FIXME: Right here we should ask the element if it's an external
	// file reference and exit immediately if so.

	// This means we've already encountered this guy.
	// Therefore, he can never be a root element
	DmElementDictHandle_t h = Find( pElement );
	if ( h != ELEMENT_DICT_HANDLE_INVALID )
	{
		m_Dict[h].m_bRoot = true;
		return;
	}

	m_indices.emplace( pElement, static_cast<DmElementDictHandle_t>( m_Dict.size() ) );
	m_Dict.push_back( { bFlatMode || bIsRoot, pElement } );

	// Tell the element we're about to serialize it
	pElement->OnElementSerialized();

	for ( CDmAttribute *pAttribute = pElement->FirstAttribute(); pAttribute; pAttribute = pAttribute->NextAttribute() )
	{
		if ( pAttribute->IsFlagSet( FATTRIB_DONTSAVE ) )
			continue;

		switch( pAttribute->GetType() )
		{
		case AT_ELEMENT:
			{
				CDmElement *pChild = pAttribute->GetValueElement<CDmElement>();
				if ( !pChild || pChild->GetFileId() != pElement->GetFileId() )
					break;

				BuildElementList_R( pChild, bFlatMode, false );
			}
			break;

		case AT_ELEMENT_ARRAY:
			{
				CDmrElementArray<> array( pAttribute );
				int nCount = array.Count();
				for ( int i = 0; i < nCount; ++i )
				{
					CDmElement *pChild = array[i];
					if ( !pChild || pChild->GetFileId() != pElement->GetFileId() )
						continue;

					BuildElementList_R( pChild, bFlatMode, false );
				}
			}
			break;
		}
	}
}

void CDmElementSerializationDictionary::BuildElementList( CDmElement *pElement, bool bFlatMode )
{
	BuildElementList_R( pElement, bFlatMode, true );
}


//-----------------------------------------------------------------------------
// Should I inline the serialization of this element?
//-----------------------------------------------------------------------------
bool CDmElementSerializationDictionary::ShouldInlineElement( CDmElement *pElement )
{
	// This means we've already encountered this guy.
	// Therefore, he can never be a root element
	DmElementDictHandle_t h = Find( pElement );
	if ( h != ELEMENT_DICT_HANDLE_INVALID )
		return !m_Dict[h].m_bRoot;

	// If we didn't find the element, it means it's a reference to an external
	// element (or it's NULL), so don't inline ie.
	return false;
}


//-----------------------------------------------------------------------------
// Clears the dictionary
//-----------------------------------------------------------------------------
void CDmElementSerializationDictionary::Clear()
{
	m_Dict.clear();
	m_indices.clear();
}


//-----------------------------------------------------------------------------
// How many root elements do we have?
//-----------------------------------------------------------------------------
int CDmElementSerializationDictionary::RootElementCount() const
{
	int nCount = 0;
	for ( const auto &info : m_Dict )
	{
		if ( info.m_bRoot )
			++nCount;
	}
	return nCount;
}

	
//-----------------------------------------------------------------------------
// Iterates over all root elements to serialize
//-----------------------------------------------------------------------------
DmElementDictHandle_t CDmElementSerializationDictionary::FirstRootElement() const
{
	int nCount = m_Dict.size();
	for ( DmElementDictHandle_t h = 0; h < nCount; ++h )
	{
		if ( m_Dict[h].m_bRoot )
			return h;
	}
	return ELEMENT_DICT_HANDLE_INVALID;
}

DmElementDictHandle_t CDmElementSerializationDictionary::NextRootElement( DmElementDictHandle_t h ) const
{
	++h;
	int nCount = m_Dict.size();
	for ( ; h < nCount; ++h )
	{
		if ( m_Dict[h].m_bRoot )
			return h;
	}
	return ELEMENT_DICT_HANDLE_INVALID;
}

CDmElement *CDmElementSerializationDictionary::GetRootElement( DmElementDictHandle_t h )
{
	Assert( m_Dict[h].m_bRoot );
	return m_Dict[h].m_pElement;
}



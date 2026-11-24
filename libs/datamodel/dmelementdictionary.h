//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#include <unordered_map>
#include <vector>
#include "tier1/utlvector.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmattribute.h"
#include "tier1/utlrbtree.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmElement;
class CDmAttribute;


//-----------------------------------------------------------------------------
// Element dictionary used in unserialization
//-----------------------------------------------------------------------------
typedef int DmElementDictHandle_t;
enum
{
	ELEMENT_DICT_HANDLE_INVALID = (DmElementDictHandle_t)~0
};


class CDmElementDictionary
{
public:
	CDmElementDictionary();

	DmElementDictHandle_t InsertElement( CDmElement *pElement );
	CDmElement *GetElement( DmElementDictHandle_t handle );
	void AddAttribute( CDmAttribute *pAttribute, const DmObjectId_t &pElementId );
	void AddArrayAttribute( CDmAttribute *pAttribute, DmElementDictHandle_t hChild );
	void AddArrayAttribute( CDmAttribute *pAttribute, const DmObjectId_t &pElementId );

	DmElementHandle_t SetElementId( DmElementDictHandle_t hDictHandle,
									const DmObjectId_t &newId,
									DmConflictResolution_t idConflictResolution );

 	// Hook up all element references (which were unserialized as object ids)
	void HookUpElementReferences();

	// Clears the dictionary
	void Clear();

	// iteration through elements
	DmElementDictHandle_t FirstElement() { return 0; }
	DmElementDictHandle_t NextElement( DmElementDictHandle_t h )
	{
		return m_Dict.IsValidIndex( h+1 ) ? h+1 : ELEMENT_DICT_HANDLE_INVALID;
	}


private:
	struct AttributeInfo_t
	{
		CDmAttribute *m_pAttribute;
		int m_bId;
		union
		{
			DmElementDictHandle_t m_hElement;
			DmObjectId_t m_ObjectId;
		};
	};
	typedef CUtlVector<AttributeInfo_t> AttributeList_t;



	struct DeletionInfo_t
	{
		DeletionInfo_t() {}
		DeletionInfo_t( DmElementHandle_t hElement ) : m_hElementToDelete( hElement ) {}
		bool operator==( const DeletionInfo_t& src ) const { return m_hElementToDelete == src.m_hElementToDelete; }

		DmElementDictHandle_t m_hDictHandle;
		DmElementHandle_t m_hElementToDelete;
		DmElementHandle_t m_hReplacementElement;
	};

	// Hook up all element references (which were unserialized as object ids)
	void HookUpElementAttributes();
	void HookUpElementArrayAttributes();

	void RemoveAttributeInfosOfElement( AttributeList_t &attributes, DmElementHandle_t hElement );

	CUtlVector< DmElementHandle_t > m_Dict;
	AttributeList_t m_Attributes;
	AttributeList_t m_ArrayAttributes;

	CUtlVector< DeletionInfo_t > m_elementsToDelete;
	std::unordered_map<DmObjectId_t, DmObjectId_t, DmObjectIdHash> m_idmap;
};


//-----------------------------------------------------------------------------
// Element dictionary used in serialization
//-----------------------------------------------------------------------------
class CDmElementSerializationDictionary
{
public:
	CDmElementSerializationDictionary();

	// Creates the list of all things to serialize
	void BuildElementList( CDmElement *pRoot, bool bFlatMode );

	// Should I inline the serialization of this element?
	bool ShouldInlineElement( CDmElement *pElement );

	// Clears the dictionary
	void Clear();

	// Iterates over all root elements to serialize
	DmElementDictHandle_t FirstRootElement() const;
	DmElementDictHandle_t NextRootElement( DmElementDictHandle_t h ) const;
 	CDmElement* GetRootElement( DmElementDictHandle_t h );

	// Finds the handle of the element
	DmElementDictHandle_t Find( CDmElement *pElement );

	// How many root elements do we have?
	int RootElementCount() const;

private:
	struct ElementInfo_t
	{
		bool m_bRoot;
		CDmElement* m_pElement;
	};

	// Creates the list of all things to serialize
	void BuildElementList_R( CDmElement *pRoot, bool bFlatMode, bool bIsRoot );

	std::vector<ElementInfo_t> m_Dict;
	std::unordered_map<CDmElement *, DmElementDictHandle_t> m_indices;
};



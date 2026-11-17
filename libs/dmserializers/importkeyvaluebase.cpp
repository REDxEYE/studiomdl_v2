//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "importkeyvaluebase.h"
#include "dmserializers.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattributevar.h"
#include "tier1/keyvalues.h"
#include "tier1/utlbuffer.h"


#include <algorithm>

//-----------------------------------------------------------------------------
// Default serialization method
//-----------------------------------------------------------------------------
bool CImportKeyValueBase::Serialize( CUtlBuffer &outBuf, CDmElement *pRoot )
{
	Warning( "Serialization not supported for importing from keyvalues files\n");
	return false;
}


//-----------------------------------------------------------------------------
// Creates a new element
//-----------------------------------------------------------------------------
CDmElement* CImportKeyValueBase::CreateDmElement(const std::string_view elementType, const std::string_view elementName, DmObjectId_t *pId)
{
	// See if we can create an element of that type
	DmElementHandle_t hElement = g_pDataModel->CreateElement( elementType, elementName, m_fileid, pId );
	if ( hElement == DMELEMENT_HANDLE_INVALID )
	{
		Warning("{}: Element uses unknown element type {}\n", m_pFileName, elementType );
		return nullptr;
	}

	return g_pDataModel->GetElement( hElement );
}


std::vector<CDmAttribute*> CImportKeyValueBase::AttributesInFileOrder( CDmElement* pElement )
{
	std::vector<CDmAttribute*> attributes;
	for ( CDmAttribute *pAttribute = pElement->FirstAttribute(); pAttribute; pAttribute = pAttribute->NextAttribute() )
		attributes.push_back( pAttribute );
	std::reverse( attributes.begin(), attributes.end() );
	return attributes;
}


//-----------------------------------------------------------------------------
// Used to output typed attributes to keyvalues
//-----------------------------------------------------------------------------
void CImportKeyValueBase::PrintBoolAttribute( CDmElement* pElement, CUtlBuffer &outBuf, const std::string_view pKeyName )
{
	if ( pElement->HasAttribute( pKeyName ) )
	{
		CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
		if ( pAttribute->GetType() == AT_BOOL )
		{
			outBuf.Printf("\"%.*s\" \"%d\"\n", static_cast<int>(pKeyName.size()), pKeyName.data(), pAttribute->GetValue<bool>( ) );
		}
	}
}

void CImportKeyValueBase::PrintIntAttribute( CDmElement* pElement, CUtlBuffer &outBuf, const std::string_view pKeyName )
{
	if ( pElement->HasAttribute( pKeyName ) )
	{
		CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
		if ( pAttribute->GetType() == AT_INT )
		{
			outBuf.Printf("\"%.*s\" \"%d\"\n", static_cast<int>(pKeyName.size()), pKeyName.data(), pAttribute->GetValue<int>( ) );
		}
	}
}

void CImportKeyValueBase::PrintFloatAttribute( CDmElement* pElement, CUtlBuffer &outBuf, const std::string_view pKeyName )
{
	if ( pElement->HasAttribute( pKeyName ) )
	{
		CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
		if ( pAttribute->GetType() == AT_FLOAT )
		{
			outBuf.Printf("\"%.*s\" \"%.10f\"\n", static_cast<int>(pKeyName.size()), pKeyName.data(), pAttribute->GetValue<float>( ) );
		}
	}
}

void CImportKeyValueBase::PrintStringAttribute( CDmElement* pElement, CUtlBuffer &outBuf, const std::string_view pKeyName, bool bSkipEmptryStrings, bool bPrintValueOnly )
{
	if ( pElement->HasAttribute( pKeyName ) )
	{
		CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
		if ( pAttribute->GetType() == AT_STRING )
		{
			const char *pValue = pAttribute->GetValueString();
			if ( !bSkipEmptryStrings || pValue[0] )
			{
				if ( !bPrintValueOnly )
				{
					outBuf.Printf("\"%.*s\" \"%s\"\n", static_cast<int>(pKeyName.size()), pKeyName.data(), pValue );
				}
				else
				{
					outBuf.Printf("\"%s\"\n", pValue );
				}
			}
		}
	}
}

	
//-----------------------------------------------------------------------------
// Used to add typed attributes from keyvalues
//-----------------------------------------------------------------------------
bool CImportKeyValueBase::AddBoolAttribute( CDmElement* pElement, KeyValues *pKeyValues, const std::string_view pKeyName, bool *pDefault )
{
	KeyValues *pKey = pKeyValues->FindKey( pKeyName );
	bool bValue;
	if ( pKey )
	{
		bValue = pKey->GetInt() != 0;
	}
	else
	{
		if ( !pDefault )
			return true;
		bValue = *pDefault;
	}

	return pElement->SetValue( pKeyName, bValue ) != nullptr;
}

	
//-----------------------------------------------------------------------------
// Used to add typed attributes from keyvalues
//-----------------------------------------------------------------------------
bool CImportKeyValueBase::AddIntAttribute( CDmElement* pElement, KeyValues *pKeyValues, const std::string_view pKeyName, int *pDefault )
{
	KeyValues *pKey = pKeyValues->FindKey( pKeyName );
	int nValue;
	if ( pKey )
	{
		nValue = pKey->GetInt();
	}
	else
	{
		if ( !pDefault )
			return true;
		nValue = *pDefault;
	}

	return pElement->SetValue( pKeyName, nValue ) != nullptr;
}

bool CImportKeyValueBase::AddFloatAttribute( CDmElement* pElement, KeyValues *pKeyValues, const std::string_view pKeyName, float *pDefault )
{
	KeyValues *pKey = pKeyValues->FindKey( pKeyName );
	float flValue;
	if ( pKey )
	{
		flValue = pKey->GetFloat();
	}
	else
	{
		if ( !pDefault )
			return true;
		flValue = *pDefault;
	}

	return pElement->SetValue( pKeyName, flValue ) != nullptr;
}

bool CImportKeyValueBase::AddStringAttribute( CDmElement* pElement, KeyValues *pKeyValues, const std::string_view pKeyName, const std::optional<std::string_view> pDefault )
{
	KeyValues *pKey = pKeyValues->FindKey( pKeyName );
	std::string_view value;
	if ( pKey )
	{
		value = pKey->GetString();
	}
	else
	{
		if ( !pDefault )
			return true;
		value = *pDefault;
	}

	return pElement->SetValue( pKeyName, value ) != nullptr;
}


bool CImportKeyValueBase::AddIntAttributeFlags( CDmElement* pElement, KeyValues *pKeyValue, const std::string_view pKeyName, int nFlags, int *pDefault )
{
	if ( !AddIntAttribute( pElement, pKeyValue, pKeyName, pDefault ) )
		return false;

	CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
	pAttribute->AddFlag( nFlags );
	return true;
}

bool CImportKeyValueBase::AddFloatAttributeFlags( CDmElement* pElement, KeyValues *pKeyValue, const std::string_view pKeyName, int nFlags, float *pDefault )
{
	if ( !AddFloatAttribute( pElement, pKeyValue, pKeyName, pDefault ) )
		return false;

	CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
	pAttribute->AddFlag( nFlags );
	return true;
}

bool CImportKeyValueBase::AddStringAttributeFlags( CDmElement* pElement, KeyValues *pKeyValue, const std::string_view pKeyName, int nFlags, const std::optional<std::string_view> pDefault )
{
	if ( !AddStringAttribute( pElement, pKeyValue, pKeyName, pDefault ) )
		return false;

	CDmAttribute *pAttribute = pElement->GetAttribute( pKeyName );
	pAttribute->AddFlag( nFlags );
	return true;
}

	
//-----------------------------------------------------------------------------
// Recursively resolves all attributes pointing to elements
//-----------------------------------------------------------------------------
void CImportKeyValueBase::RecursivelyResolveElement( CDmElement* pElement )
{
	if ( !pElement )
		return;

	pElement->Resolve();

	CDmAttribute *pAttribute = pElement->FirstAttribute();
	while ( pAttribute )
	{
		switch ( pAttribute->GetType() )
		{
		case AT_ELEMENT:
			{
				CDmElement *pElement = pAttribute->GetValueElement<CDmElement>();
				RecursivelyResolveElement( pElement );
			}
			break;

		case AT_ELEMENT_ARRAY:
			{
				CDmrElementArray<> array( pAttribute );
				int nCount = array.Count();
				for ( int i = 0; i < nCount; ++i )
				{
					CDmElement *pElement = array[ i ];
					RecursivelyResolveElement( pElement );
				}
			}
			break;
		}

		pAttribute = pAttribute->NextAttribute( );
	}
}

	
//-----------------------------------------------------------------------------
// Main entry point for the unserialization
//-----------------------------------------------------------------------------
bool CImportKeyValueBase::Unserialize( CUtlBuffer &buf, const char *pEncodingName, int nEncodingVersion,
									   const char *pSourceFormatName, int nSourceFormatVersion,
									   DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement **ppRoot )
{
	*ppRoot = nullptr;
	const char *pFileName = g_pDataModel->GetFileName( fileid );
	m_pFileName = pFileName ? pFileName : "";
	m_fileid = fileid;

	KeyValues *kv = new KeyValues( "dmx file" );
	if ( !kv )
		return false;

	bool bOk = kv->LoadFromBuffer( "dmx file", buf );
	if ( bOk )
	{
		*ppRoot = UnserializeFromKeyValues( kv );
	}

	kv->deleteThis();
	return bOk;
}

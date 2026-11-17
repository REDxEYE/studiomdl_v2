//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once



#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datamodel/idatamodel.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CUtlBuffer;
class KeyValues;
class CDmElement;
class CDmAttribute;


//-----------------------------------------------------------------------------
// Serialization class for Key Values
//-----------------------------------------------------------------------------
class CImportKeyValueBase : public IDmSerializer
{
public:
	// Inherited from IDMSerializer
    bool StoresVersionInFile() const override { return false; }
    bool IsBinaryFormat() const override { return false; }

    bool Serialize( CUtlBuffer &buf, CDmElement *pRoot ) override;

    bool Unserialize( CUtlBuffer &buf, const char *pEncodingName, int nEncodingVersion,
                      const char *pSourceFormatName, int nSourceFormatVersion,
                      DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement **ppRoot ) override;

protected:
	// Main entry point for derived classes to implement unserialization
	virtual CDmElement* UnserializeFromKeyValues( KeyValues *pKeyValues ) = 0;

	// Returns the file name associated with the unserialization
    std::string_view FileName() const;

	// Creates new elements
	CDmElement* CreateDmElement(std::string_view elementType, std::string_view elementName, DmObjectId_t *pId);

	// Recursively resolves all attributes pointing to elements
	void RecursivelyResolveElement( CDmElement* pElement );

	// Used to add typed attributes from keyvalues. A missing key adds nothing unless a default is given.
	bool AddBoolAttribute( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, bool *pDefault = nullptr);
	bool AddIntAttribute( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, int *pDefault = nullptr);
	bool AddFloatAttribute( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, float *pDefault = nullptr);
	bool AddStringAttribute( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, std::optional<std::string_view> pDefault = std::nullopt);

	// Used to add typed attributes from keyvalues
	bool AddIntAttributeFlags( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, int nFlags, int *pDefault = nullptr);
	bool AddFloatAttributeFlags( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, int nFlags, float *pDefault = nullptr);
	bool AddStringAttributeFlags( CDmElement* pElement, KeyValues *pKeyValue, std::string_view pKeyName, int nFlags, std::optional<std::string_view> pDefault = std::nullopt);

	// Used to output typed attributes to keyvalues
	void PrintBoolAttribute( CDmElement* pElement, CUtlBuffer &outBuf, std::string_view pKeyName );
	void PrintIntAttribute( CDmElement* pElement, CUtlBuffer &outBuf, std::string_view pKeyName );
	void PrintFloatAttribute( CDmElement* pElement, CUtlBuffer &outBuf, std::string_view pKeyName );
	void PrintStringAttribute( CDmElement* pElement, CUtlBuffer &outBuf, std::string_view pKeyName, bool bSkipEmptryStrings = false, bool bPrintValueOnly = false );

	// The element's attributes in the order they were added (FirstAttribute() is the most recently added one), so
	// re-exported files keep the key order of the imported file
	static std::vector<CDmAttribute*> AttributesInFileOrder( CDmElement* pElement );

private:
	std::string m_pFileName;
	DmFileId_t m_fileid = DMFILEID_INVALID;	// file being unserialized; imported elements belong to it
};


//-----------------------------------------------------------------------------
// Returns the file name associated with the unserialization
//-----------------------------------------------------------------------------
inline std::string_view CImportKeyValueBase::FileName() const {
	return m_pFileName;
}



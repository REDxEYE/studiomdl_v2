//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "importkeyvaluebase.h"
#include "dmserializers.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"


//-----------------------------------------------------------------------------
// Serialization class for Key Values
//-----------------------------------------------------------------------------
class CImportCommentary : public CImportKeyValueBase
{
public:
    const char *GetName() const override { return "commentary"; }
    const char *GetDescription() const override { return "Commentary File"; }
    int GetCurrentVersion() const override { return 0; } // doesn't store a version
    const char *GetImportedFormat() const override { return "commentary"; }
    int GetImportedVersion() const override { return 1; }

    bool Serialize( CUtlBuffer &outBuf, CDmElement *pRoot ) override;

    bool Unserialize( CUtlBuffer &buf, const char *pEncodingName, int nEncodingVersion,
                      const char *pSourceFormatName, int nSourceFormatVersion,
                      DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement **ppRoot ) override;

protected:
	// Main entry point for derived classes to implement unserialization
    CDmElement* UnserializeFromKeyValues( KeyValues *pKeyValues ) override { Assert( 0 ); return nullptr; }
};


//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
static CImportCommentary s_ImportCommentary;

void InstallCommentaryImporter( IDataModel *pFactory )
{
	pFactory->AddSerializer( &s_ImportCommentary );
}


bool CImportCommentary::Serialize( CUtlBuffer &buf, CDmElement *pRoot )
{
	IDmSerializer *pKVSerializer = g_pDataModel->FindSerializer( "keyvalues" );
	if ( !pKVSerializer )
		return false;

	return pKVSerializer->Serialize( buf, pRoot );
}

//-----------------------------------------------------------------------------
// Handles creation of the right element for a keyvalue
//-----------------------------------------------------------------------------
class CElementForKeyValueCallback : public IElementForKeyValueCallback
{
public:
	std::optional<std::string_view> GetElementForKeyValue(const std::string_view keyName, int iNestingLevel) override {
		if ( iNestingLevel == 1 && keyName.starts_with("entity") )
			return "DmeCommentaryNodeEntity";

		return std::nullopt;
	}
};

bool CImportCommentary::Unserialize( CUtlBuffer &buf, const char *pEncodingName, int nEncodingVersion,
									  const char *pSourceFormatName, int nSourceFormatVersion,
									  DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement **ppRoot )
{
	*ppRoot = nullptr;

	IDmSerializer *pKVSerializer = g_pDataModel->FindSerializer( "keyvalues" );
	if ( !pKVSerializer )
		return false;

	CElementForKeyValueCallback KeyValuesCallback;
	g_pDataModel->SetKeyValuesElementCallback( &KeyValuesCallback );

	bool bSuccess = pKVSerializer->Unserialize( buf, "keyvalues", nEncodingVersion, pSourceFormatName, nSourceFormatVersion, fileid, idConflictResolution, ppRoot );

	g_pDataModel->SetKeyValuesElementCallback(nullptr);

	return bSuccess;
}

//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once


#include "datamodel/idatamodel.h"

class CDmeBaseImporter : public IDmLegacyUpdater
{
	typedef IDmLegacyUpdater BaseClass;

public:
	CDmeBaseImporter( char const *formatName, char const *nextFormatName );

    const char *GetName() const override { return m_pFormatName; }

    bool IsLatestVersion() const override;

    bool Update( CDmElement **ppRoot ) override;

private:
	virtual bool DoFixup( CDmElement *pRoot ) = 0;

protected:
	char const *m_pFormatName;
	char const *m_pNextSerializer;
};

class CSFMBaseImporter : public CDmeBaseImporter
{
	typedef CDmeBaseImporter BaseClass;

public:
	CSFMBaseImporter( char const *formatName, char const *nextFormatName );
};


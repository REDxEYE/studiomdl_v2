//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// The abstract base operator class - all actions within the scenegraph happen via operators
//
//=============================================================================

#pragma once


#include "datamodel/dmelement.h"
#include "datamodel/dmattributevar.h"

#include <vector>


//-----------------------------------------------------------------------------
// A class representing an generic operator
//-----------------------------------------------------------------------------
class CDmeOperator : public IDmeOperator, public CDmElement
{
	DEFINE_ELEMENT( CDmeOperator, CDmElement );

public:
    bool IsDirty() override; // ie needs to operate
    void Operate() override {}
    const char* GetOperatorName() const override { return m_Name.Get(); }

    void GetInputAttributes ( std::vector< CDmAttribute * > &attrs ) override {}
    void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override {}
	virtual void GatherInputOperators( std::vector< CDmeOperator * > &operators );

    void SetSortKey( int key ) override;

    int GetSortKey() const override;
	int	m_nSortKey;
};

void GatherOperatorsForElement(  CDmElement *pElement, std::vector< CDmeOperator * > &operators );


//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once



#include "datamodel/idatamodel.h"
#include "tier1/utlvector.h"
#include "dependencygraph.h"


//-----------------------------------------------------------------------------
// element framework implementation
//-----------------------------------------------------------------------------
class CDmElementFramework : public IDmElementFramework
{
public:
	CDmElementFramework();

public:
	bool Init() override;
	void Shutdown() override;

	// Methods of IDmElementFramework
    DmPhase_t GetPhase() override;

    void SetOperators( const std::vector< IDmeOperator* > &operators ) override;

    void BeginEdit() override; // ends in edit phase, forces apply/resolve if from edit phase
    void Operate( bool bResolve ) override; // ends in output phase
    void Resolve() override;

	// Get the sorted operators in their current order, does not perform sort.
    const std::vector< IDmeOperator* > &GetSortedOperators() const override;

	
public:
	// Other public methods
	void AddElementToDirtyList( DmElementHandle_t hElement );
	void RemoveCleanElementsFromDirtyList();

	// Non-virtual methods of identical virtual functions
	DmPhase_t FastGetPhase();


private:
	void EditApply();

	// Invoke the resolve method
	void Resolve( bool clearDirtyFlags );

	CDependencyGraph m_dependencyGraph;
	CUtlVector< DmElementHandle_t > m_dirtyElements;
	DmPhase_t m_phase;
};


//-----------------------------------------------------------------------------
// Singleton
//-----------------------------------------------------------------------------
extern CDmElementFramework *g_pDmElementFrameworkImp;


//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
inline DmPhase_t CDmElementFramework::FastGetPhase()
{
	return m_phase;
}



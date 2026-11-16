//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once


#include <vector>
#include "tier1/utlvector.h"
#include "tier1/utlhash.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmAttribute;
class IDmeOperator;
struct COperatorNode;
class CAttributeNode;


//-----------------------------------------------------------------------------
// CDependencyGraph class - sorts operators based upon the input/output graph
//-----------------------------------------------------------------------------
class CDependencyGraph
{
public:
	CDependencyGraph();
	~CDependencyGraph();

	void Reset( const std::vector< IDmeOperator * > &operators );

	// caches only the operators that need to be evaluated, sorted by dependencies
	// returns true if a cycle found - in this case, an arbitrary link of the cycle will be ignored
	bool CullAndSortOperators();

	const std::vector< IDmeOperator* > &GetSortedOperators() const { return m_operators; }

	void Cleanup();

private:
	static bool GetOperatorOrdering( CUtlVector< COperatorNode* > &pOpNodes, std::vector< IDmeOperator * > &operators );

	void FindRoots();
	CAttributeNode *FindAttrNode( CDmAttribute *pAttr );

	CUtlVector< COperatorNode* > m_opRoots;
//	CUtlVector< COperatorNode* > m_opLeaves;

	CUtlVector< COperatorNode* > m_opNodes;

	CUtlHash< CAttributeNode* > m_attrNodes;

	std::vector< IDmeOperator* > m_operators;
};


//=========== Copyright (c) Valve Corporation, All rights reserved. ===========
//
//=============================================================================

#pragma once

#if defined( COMPILER_MSVC )
#pragma once
#endif


// Valve includes
#include "datamodel/dmattributevar.h"
#include "mathlib/expressioncalculator.h"
#include "movieobjects/dmeoperator.h"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeCombinationOperator;


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CDmeFlexRules;
class CDmeFlexRuleBase : public CDmeOperator
{
	DEFINE_ELEMENT( CDmeFlexRuleBase, CDmeOperator );

public:

	// CDmeOperator
    void GetInputAttributes ( std::vector< CDmAttribute * > &attrs ) override;

    void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override;

	CDmAttribute *ResultAttr() { return m_flResult.GetAttribute(); }

protected:
	friend class CDmeFlexRules;

	CDmeFlexRules *GetFlexRules() const;

	CDmaVar< float > m_flResult;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CDmeFlexRulePassThrough : public CDmeFlexRuleBase
{
	DEFINE_ELEMENT( CDmeFlexRulePassThrough, CDmeFlexRuleBase );

public:
	// CDmeOperator
    void Operate() override;

protected:
	friend class CDmeFlexRules;

};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CDmeFlexRuleExpression : public CDmeFlexRuleBase
{
	DEFINE_ELEMENT( CDmeFlexRuleExpression, CDmeFlexRuleBase );

public:

	// CDmeOperator
    void Operate() override;

    void Resolve() override;

	const char *GetExpression() const { return m_expr.Get(); }

protected:
	friend class CDmeFlexRules;

	CDmaString m_expr;
	CExpressionCalculator m_calc;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CDmeFlexRuleLocalVar : public CDmeFlexRuleBase
{
	DEFINE_ELEMENT( CDmeFlexRuleLocalVar, CDmeFlexRuleBase );

public:

protected:
	friend class CDmeFlexRules;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CDmeFlexRules : public CDmeOperator
{
	DEFINE_ELEMENT( CDmeFlexRules, CDmeOperator );

public:
	// From DmeOperator
    void GetInputAttributes ( std::vector< CDmAttribute * > &attrs ) override;

    void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override;

    void Operate() override;

    void Resolve() override;

	int GetRuleCount() const { return m_eDeltaStates.Count(); }
	CDmeFlexRuleBase *GetRule( int i ) const { return ( i < 0 || i >= GetRuleCount() ) ? nullptr : m_eDeltaStates[i]; }

	int GetDeltaStateIndex( const char *pszDeltaName ) const;
	float GetDeltaStateWeight( const char *pszDeltaName ) const;

protected:
	friend class CDmeFlexRuleBase;
	friend class CDmeFlexRuleExpression;

	// Required to drive this node from a DmeCombinationOperator
	CDmaElementArray< CDmeFlexRuleBase > m_eDeltaStates;
	CDmaArray< Vector2D > m_vDeltaStateWeights;

	CDmaElement< CDmElement > m_eTarget;

	std::vector< int > m_deltaToTargetMap;
};



//====== Copyright � 1996-2009, Valve Corporation, All rights reserved. =======
//
// DmeEyeball
//
//=============================================================================


// Standard includes


// Valve includes
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmeflexrules.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeFlexRuleBase, CDmeFlexRuleBase);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleBase::OnConstruction() {
    m_flResult.Init(this, "result");
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleBase::OnDestruction() {
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleBase::GetInputAttributes(std::vector<CDmAttribute*> &attrs) {
    BaseClass::GetInputAttributes(attrs);

    CDmeFlexRules* pDmeFlexRules = GetFlexRules();
    if (pDmeFlexRules) {
        attrs.emplace_back(pDmeFlexRules->m_vDeltaStateWeights.GetAttribute());
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleBase::GetOutputAttributes(std::vector<CDmAttribute*> &attrs) {
    BaseClass::GetOutputAttributes(attrs);

    attrs.emplace_back(m_flResult.GetAttribute());
}


//-----------------------------------------------------------------------------
// Returns the first DmeExpressionRules element that refers to this element
//-----------------------------------------------------------------------------
CDmeFlexRules* CDmeFlexRuleBase::GetFlexRules() const {
    return FindReferringElement<CDmeFlexRules>(this, "deltaStates");
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeFlexRulePassThrough, CDmeFlexRulePassThrough);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRulePassThrough::OnConstruction() {
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRulePassThrough::OnDestruction() {
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRulePassThrough::Operate() {
    CDmeFlexRules* pDmeFlexRules = GetFlexRules();
    if (!pDmeFlexRules)
        return;

    const float flResult = pDmeFlexRules->GetDeltaStateWeight(GetName());

    m_flResult.Set(flResult);
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeFlexRuleExpression, CDmeFlexRuleExpression);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleExpression::OnConstruction() {
    m_expr.Init(this, "expr");
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleExpression::OnDestruction() {
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleExpression::Operate() {
    CDmeFlexRules* pDmeFlexRules = GetFlexRules();
    if (pDmeFlexRules) {
        for (int i = 0; i < m_calc.VariableCount(); ++i) {
            m_calc.SetVariable(i, pDmeFlexRules->GetDeltaStateWeight(m_calc.VariableName(i)));
        }
    }

    float flVal = 0.0f;

    if (m_calc.Evaluate(flVal)) {
        m_flResult.Set(flVal);
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleExpression::Resolve() {
    if (m_expr.IsDirty()) {
        m_calc.SetExpression(m_expr.Get());
        m_calc.BuildVariableListFromExpression();
    }
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeFlexRuleLocalVar, CDmeFlexRuleLocalVar);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleLocalVar::OnConstruction() {
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRuleLocalVar::OnDestruction() {
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeFlexRules, CDmeFlexRules);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRules::OnConstruction() {
    // Required to drive this node from a DmeCombinationOperator
    m_eDeltaStates.Init(this, "deltaStates");
    m_vDeltaStateWeights.Init(this, "deltaStateWeights");

    m_eTarget.Init(this, "target");
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRules::OnDestruction() {
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRules::Operate() {
    if (m_deltaToTargetMap.size() <= 0) {
        Resolve();
    }

    const int nDeltaCount = MIN(m_eDeltaStates.Count(), m_deltaToTargetMap.size());

    CDmrArray<Vector2D> targetWeights(m_eTarget, "deltaStateWeights");
    const int nTargetWeightCount = targetWeights.Count();

    for (int i = 0; i < nDeltaCount; ++i) {
        const int nTargetIndex = m_deltaToTargetMap[i];
        if (nTargetIndex < 0 || nTargetIndex >= nTargetWeightCount)
            continue;

        CDmeFlexRuleBase* pDmeFlexRuleBase = m_eDeltaStates[i];
        if (!pDmeFlexRuleBase)
            continue;

        const CDmAttribute* pDmResultAttr = pDmeFlexRuleBase->ResultAttr();
        if (!pDmResultAttr)
            continue;

        const float flVal = pDmResultAttr->GetValue<float>();

        targetWeights.Set(nTargetIndex, Vector2D(flVal, flVal)); // FlexRules ignore the built in stereo
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRules::Resolve() {
    if (m_deltaToTargetMap.size() <= 0 || m_eTarget.IsDirty() || m_eDeltaStates.IsDirty()) {
        m_deltaToTargetMap.resize(m_eDeltaStates.Count());

        const CDmrElementArrayConst<CDmElement> targetStates(m_eTarget, "deltaStates");

        for (int i = 0; i < m_eDeltaStates.Count(); ++i) {
            m_deltaToTargetMap[i] = -1;

            CDmeFlexRuleBase* pDmeFlexRuleBase = m_eDeltaStates[i];
            if (!pDmeFlexRuleBase)
                continue;

            for (int j = 0; j < targetStates.Count(); ++j) {
                if (!V_strcmp(targetStates[j]->GetName(), pDmeFlexRuleBase->GetName())) {
                    m_deltaToTargetMap[i] = j;
                    break;
                }
            }
        }
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRules::GetInputAttributes(std::vector<CDmAttribute*> &attrs) {
    attrs.emplace_back(m_eDeltaStates.GetAttribute());
    attrs.emplace_back(m_vDeltaStateWeights.GetAttribute());

    for (int i = 0; i < m_eDeltaStates.Count(); ++i) {
        m_eDeltaStates[i]->GetOutputAttributes(attrs);
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeFlexRules::GetOutputAttributes(std::vector<CDmAttribute*> &attrs) {
    CDmrArray<Vector2D> targetWeights(m_eTarget, "deltaStateWeights");

    if (targetWeights.IsValid()) {
        attrs.emplace_back(targetWeights.GetAttribute());
    }
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int CDmeFlexRules::GetDeltaStateIndex(const char* pszDeltaName) const {
    for (int i = 0; i < m_eDeltaStates.Count(); ++i) {
        if (!V_strcmp(pszDeltaName, m_eDeltaStates[i]->GetName()))
            return i;
    }

    return -1;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
float CDmeFlexRules::GetDeltaStateWeight(const char* pszDeltaName) const {
    const int nDeltaStateIndex = GetDeltaStateIndex(pszDeltaName);
    if (nDeltaStateIndex < 0 || nDeltaStateIndex >= m_vDeltaStateWeights.Count())
        return 0.0f;

    return m_vDeltaStateWeights[nDeltaStateIndex].x; // Weights are stereo...
}

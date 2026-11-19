//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmecombinationoperator.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "movieobjects/dmechannel.h"
#include "movieobjects/dmemodel.h"
#include "movieobjects/dmevertexdata.h"
#include "movieobjects/dmemesh.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeCombinationInputControl, CDmeCombinationInputControl);


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationInputControl::OnConstruction() {
    m_RawControlNames.Init(this, "rawControlNames");
    m_bIsStereo.Init(this, "stereo");
    m_bIsEyelid.InitAndSet(this, "eyelid", false);
    m_WrinkleScales.Init(this, "wrinkleScales");
}

void CDmeCombinationInputControl::OnDestruction() {
}


//-----------------------------------------------------------------------------
// Backward compat
//-----------------------------------------------------------------------------
void CDmeCombinationInputControl::OnElementUnserialized() {
    BaseClass::OnElementUnserialized();

    int nWrinkleCount = m_WrinkleScales.size();
    int nControlCount = m_RawControlNames.size();
    if (nWrinkleCount < nControlCount) {
        for (int i = nWrinkleCount; i < nControlCount; ++i) {
            m_WrinkleScales.AddToTail(0.0f);
        }
    }
    else if (nWrinkleCount > nControlCount) {
        m_WrinkleScales.RemoveMultiple(nControlCount, nWrinkleCount - nControlCount);
    }
}


//-----------------------------------------------------------------------------
// Iterates remapped controls
//-----------------------------------------------------------------------------
int CDmeCombinationInputControl::RawControlCount() const {
    return m_RawControlNames.size();
}

const char* CDmeCombinationInputControl::RawControlName(int nIndex) const {
    return m_RawControlNames[nIndex];
}


//-----------------------------------------------------------------------------
// Is this control a stereo control?
//-----------------------------------------------------------------------------
bool CDmeCombinationInputControl::IsStereo() const {
    return m_bIsStereo;
}


//-----------------------------------------------------------------------------
// Is this control an eyelid control?
//-----------------------------------------------------------------------------
bool CDmeCombinationInputControl::IsEyelid() const {
    return m_bIsEyelid;
}


float CDmeCombinationInputControl::WrinkleScale(int nIndex) {
    if (nIndex < 0 || (nIndex >= m_WrinkleScales.size()))
        return 0.0f;
    return m_WrinkleScales[nIndex];
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
const char* CDmeCombinationInputControl::GetEyesUpDownFlexName() const {
    const CDmAttribute* pEyesUpDownFlexAttr = GetAttribute("eyesUpDownFlex", AT_STRING);

    if (pEyesUpDownFlexAttr)
        return pEyesUpDownFlexAttr->GetValueString();

    return nullptr;
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeCombinationDominationRule, CDmeCombinationDominationRule);


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationDominationRule::OnConstruction() {
    m_Dominators.Init(this, "dominators", FATTRIB_HAS_CALLBACK);
    m_Suppressed.Init(this, "suppressed", FATTRIB_HAS_CALLBACK);
}

void CDmeCombinationDominationRule::OnDestruction() {
}


//-----------------------------------------------------------------------------
// Notify parent that one of our attributes has changed
//-----------------------------------------------------------------------------
void CDmeCombinationDominationRule::OnAttributeChanged(CDmAttribute* pAttribute) {
    BaseClass::OnAttributeChanged(pAttribute);

    if (pAttribute == m_Dominators.GetAttribute() || pAttribute == m_Suppressed.GetAttribute()) {
        InvokeOnAttributeChangedOnReferrers(GetHandle(), pAttribute);
    }
}


//-----------------------------------------------------------------------------
// Iteration
//-----------------------------------------------------------------------------
int CDmeCombinationDominationRule::DominatorCount() const {
    return m_Dominators.size();
}

const char* CDmeCombinationDominationRule::GetDominator(int i) const {
    return m_Dominators[i];
}

int CDmeCombinationDominationRule::SuppressedCount() const {
    return m_Suppressed.size();
}

const char* CDmeCombinationDominationRule::GetSuppressed(int i) const {
    return m_Suppressed[i];
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeCombinationOperator, CDmeCombinationOperator);


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::OnConstruction() {
    m_InputControls.Init(this, "controls");
    m_ControlValues[COMBO_CONTROL_NORMAL].Init(this, "controlValues");
    m_ControlValues[COMBO_CONTROL_LAGGED].Init(this, "controlValuesLagged");
    m_bSpecifyingLaggedData.Init(this, "usesLaggedValues");

    m_Dominators.Init(this, "dominators", FATTRIB_HAS_CALLBACK);

    m_Targets.Init(this, "targets");
    m_flLastLaggedComputationTime = FLT_MIN;
}

void CDmeCombinationOperator::OnDestruction() {
    m_RawControlInfo.clear();
    m_CombinationInfo.clear();
    m_DominatorInfo.clear();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::OnAttributeChanged(CDmAttribute* pAttribute) {
    m_Dominators.GetAttribute()->AddFlag(FATTRIB_DIRTY);
}


//-----------------------------------------------------------------------------
// Finds the index of the input control with the specified name (and creates one if necessary)
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::OnElementUnserialized() {
    BaseClass::OnElementUnserialized();

    // Mark all of the input as not being in their default state since we read it from a file
    int nCount = m_InputControls.size();
    m_IsDefaultValue.resize(nCount);
    for (int i = 0; i < nCount; ++i) {
        m_IsDefaultValue[i] = false;
    }
}


//-----------------------------------------------------------------------------
// Finds the index of the input control with the specified name
//-----------------------------------------------------------------------------
ControlIndex_t CDmeCombinationOperator::FindControlIndex(const char* pControlName) {
    int nCount = m_InputControls.size();
    for (int i = 0; i < nCount; ++i) {
        if (!m_InputControls[i])
            continue;

        if (!Q_stricmp(pControlName, m_InputControls[i]->GetName()))
            return i;
    }
    return -1;
}


//-----------------------------------------------------------------------------
// Removes a control
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::RemoveControl(const char* pControlName) {
    ControlIndex_t nIndex = FindControlIndex(pControlName);
    if (nIndex >= 0) {
        DestroyElement(m_InputControls[nIndex]);
        m_InputControls.FastRemove(nIndex);
        m_ControlValues[COMBO_CONTROL_NORMAL].FastRemove(nIndex);
        m_ControlValues[COMBO_CONTROL_LAGGED].FastRemove(nIndex);
        m_IsDefaultValue.erase(m_IsDefaultValue.begin() + nIndex);

        Assert(m_InputControls.Count() == m_ControlValues[COMBO_CONTROL_NORMAL].Count());
        Assert(m_InputControls.Count() == m_ControlValues[COMBO_CONTROL_LAGGED].Count());
        Assert(m_InputControls.Count() == m_IsDefaultValue.Count());

        RebuildRawControlList();
    }
}


//-----------------------------------------------------------------------------
// Iterates remapped controls
//-----------------------------------------------------------------------------
int CDmeCombinationOperator::GetRawControlCount(ControlIndex_t nControl) const {
    CDmeCombinationInputControl* pInputControl = m_InputControls[nControl];
    Assert(pInputControl);
    return pInputControl->RawControlCount();
}

const char* CDmeCombinationOperator::GetRawControlName(ControlIndex_t nControl, int nIndex) const {
    CDmeCombinationInputControl* pInputControl = m_InputControls[nControl];
    Assert(pInputControl);
    return pInputControl->RawControlName(nIndex);
}


//-----------------------------------------------------------------------------
// Iterates a global list of output controls
//-----------------------------------------------------------------------------
int CDmeCombinationOperator::GetRawControlCount() const {
    return m_RawControlInfo.size();
}

std::string_view CDmeCombinationOperator::GetRawControlName(int nIndex) const {
    return m_RawControlInfo[nIndex].m_Name;
}


bool CDmeCombinationOperator::IsStereoControl(ControlIndex_t nControlIndex) const {
    return m_InputControls[nControlIndex]->IsStereo();
}

bool CDmeCombinationOperator::IsEyelidControl(ControlIndex_t nControlIndex) const {
    return m_InputControls[nControlIndex]->IsEyelid();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
const char* CDmeCombinationOperator::GetEyesUpDownFlexName(ControlIndex_t nControlIndex) const {
    return m_InputControls[nControlIndex]->GetEyesUpDownFlexName();
}


//-----------------------------------------------------------------------------
// Returns true if a control is a multi control (a control w/ 3 or more remappings)
//-----------------------------------------------------------------------------
bool CDmeCombinationOperator::IsMultiControl(ControlIndex_t nControlIndex) const {
    return m_InputControls[nControlIndex]->RawControlCount() >= 3 || IsEyelidControl(nControlIndex);
}


//-----------------------------------------------------------------------------
// Iterates controls
//-----------------------------------------------------------------------------
int CDmeCombinationOperator::GetControlCount() const {
    return m_InputControls.size();
}

const char* CDmeCombinationOperator::GetControlName(ControlIndex_t i) const {
    return m_InputControls[i]->GetName();
}


//-----------------------------------------------------------------------------
// Do we have a raw control?
//-----------------------------------------------------------------------------
bool CDmeCombinationOperator::HasRawControl(const char* pRawControlName) const {
    return FindRawControlIndex(pRawControlName) >= 0;
}


//-----------------------------------------------------------------------------
// Finds the index of the remapped control with the specified name
//-----------------------------------------------------------------------------
CDmeCombinationOperator::RawControlIndex_t CDmeCombinationOperator::FindRawControlIndex(
    const char* pControlName, bool bIgnoreDefaultControls) const {
    int nRawControlCount = m_RawControlInfo.size();
    for (int i = 0; i < nRawControlCount; ++i) {
        if (bIgnoreDefaultControls && m_RawControlInfo[i].m_bIsDefaultControl)
            continue;

        if (vstd::iequals(m_RawControlInfo[i].m_Name, pControlName))
            return i;
    }
    return -1;
}


//-----------------------------------------------------------------------------
// Would a particular delta state attached to this combination operator end up stereo?
//-----------------------------------------------------------------------------
bool CDmeCombinationOperator::IsDeltaStateStereo(const char* pDeltaStateName) {
    int* pTemp = (int*) _alloca(m_RawControlInfo.size() * sizeof(int));
    int nCount = ParseDeltaName(pDeltaStateName, pTemp);
    for (int i = 0; i < nCount; ++i) {
        CDmeCombinationInputControl* pInputControl = m_InputControls[m_RawControlInfo[pTemp[i]].m_InputControl];
        if (pInputControl->IsStereo())
            return true;
    }
    return false;
}


//-----------------------------------------------------------------------------
// Computes list of all remapped controls
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::RebuildRawControlList() {
    m_RawControlInfo.clear();

    int nControlCount = m_InputControls.size();
    for (int i = 0; i < nControlCount; ++i) {
        CDmeCombinationInputControl* pInputControl = m_InputControls[i];
        Assert(pInputControl);
        int nRemapCount = pInputControl->RawControlCount();

        const bool bIsEyelid = pInputControl->IsEyelid();

        float flStep = (nRemapCount > 2) ? 1.0f / (nRemapCount - 1) : 0.0f;
        for (int j = 0; j < nRemapCount; ++j) {
            RawControlInfo_t &info =m_RawControlInfo.emplace_back();

            info.m_Name = pInputControl->RawControlName(j);
            info.m_InputControl = i;
            info.m_bIsDefaultControl = false;
            info.m_flWrinkleScale = pInputControl->WrinkleScale(j);
            info.m_bLowerEyelid = false;

            if (bIsEyelid) {
                info.m_FilterRamp.Init(0.0f, 1.0f, 10.0f, 11.0f);
                // TODO: Right now it's implicit that the lower eyelid is first...
                if (j == 0) {
                    // Close Lower Lid
                    info.m_bLowerEyelid = true;
                }
                continue;
            }

            switch (nRemapCount) {
                case 1:
                    info.m_FilterRamp.Init(0.0f, 1.0f, 10.0f, 11.0f);
                    break;

                case 2:
                    if (j == 0) {
                        info.m_FilterRamp.Init(-11.0f, -10.0f, 0.0f, 0.5f);
                    }
                    else {
                        info.m_FilterRamp.Init(0.5f, 1.0f, 10.0f, 11.0f);
                    }
                    break;

                default: {
                    if (j == 0) {
                        info.m_FilterRamp.Init(-11.0f, -10.0f, 0, flStep);
                    }
                    else if (j == nRemapCount - 1) {
                        info.m_FilterRamp.Init(1.0f - flStep, 1.0f, 10.0f, 11.0f);
                    }
                    else {
                        float flPeak = j * flStep;
                        info.m_FilterRamp.Init(flPeak - flStep, flPeak, flPeak, flPeak + flStep);
                    }
                }
                break;
            }
        }
    }

    RebuildDominatorInfo();
}


//-----------------------------------------------------------------------------
// Computes lists of dominators and suppressors
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::RebuildDominatorInfo() {
    m_DominatorInfo.clear();
    int nCount = m_Dominators.size();
    int* pDominators = (int*) _alloca(m_RawControlInfo.size() * sizeof(int));
    int* pSuppressed = (int*) _alloca(m_RawControlInfo.size() * sizeof(int));
    for (int i = 0; i < nCount; ++i) {
        CDmeCombinationDominationRule* pRule = m_Dominators[i];
        bool bRuleOk = true;

        int nDominatorCount = pRule->DominatorCount();
        int nSuppressedCount = pRule->SuppressedCount();
        if ((nDominatorCount == 0) || (nSuppressedCount == 0))
            continue;

        for (int j = 0; j < nDominatorCount; ++j) {
            int nControlIndex = FindRawControlIndex(pRule->GetDominator(j));
            if (nControlIndex < 0) {
                bRuleOk = false;
                break;
            }
            pDominators[j] = nControlIndex;
        }

        for (int j = 0; j < nSuppressedCount; ++j) {
            int nControlIndex = FindRawControlIndex(pRule->GetSuppressed(j));
            if (nControlIndex < 0) {
                bRuleOk = false;
                break;
            }
            pSuppressed[j] = nControlIndex;
        }

        if (!bRuleOk)
            continue;

        auto& dominator_info = m_DominatorInfo.emplace_back();
        dominator_info.m_DominantIndices.insert(dominator_info.m_DominantIndices.end(), pDominators, pDominators+nDominatorCount);
        dominator_info.m_SuppressedIndices.insert(dominator_info.m_SuppressedIndices.end(), pSuppressed, pSuppressed+nSuppressedCount);
    }
}


//-----------------------------------------------------------------------------
// Removes a domination rule
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::RemoveDominationRule(int nIndex) {
    CDmeCombinationDominationRule* pRule = m_Dominators[nIndex];
    m_Dominators.Remove(nIndex);
    DestroyElement(pRule);
}

void CDmeCombinationOperator::RemoveDominationRule(CDmeCombinationDominationRule* pRule) {
    int nCount = m_Dominators.size();
    for (int i = 0; i < nCount; ++i) {
        if (m_Dominators[i] == pRule) {
            RemoveDominationRule(i);
            break;
        }
    }
}


//-----------------------------------------------------------------------------
// Iteration
//-----------------------------------------------------------------------------
int CDmeCombinationOperator::DominationRuleCount() const {
    return m_Dominators.size();
}

CDmeCombinationDominationRule* CDmeCombinationOperator::GetDominationRule(int i) {
    return m_Dominators[i];
}


//-----------------------------------------------------------------------------
// Determines which combination to use based on the deltaState name
//-----------------------------------------------------------------------------
int CDmeCombinationOperator::ParseDeltaName(const char* pDeltaStateName, int* pControlIndices) {
    char pBuf[512];
    Q_strncpy(pBuf, pDeltaStateName, sizeof(pBuf));

    int nComboCount = 0;
    char* pEnd;
    for (char* pName = pBuf; *pName; pName = pEnd) {
        pEnd = strchr(pName, '_');
        if (!pEnd) {
            pEnd = pName + Q_strlen(pName);
        }
        else {
            // Null-terminate
            *pEnd = 0;
            ++pEnd;
        }

        int nControlIndex = FindRawControlIndex(pName);
        if (nControlIndex < 0)
            return 0;

        pControlIndices[nComboCount++] = nControlIndex;
    }

    return nComboCount;
}


//-----------------------------------------------------------------------------
// Finds dominators
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::FindDominators(CombinationOperation_t &op) {
    // Dominators are sets of inputs, which, when set to 1, will
    // supress another set of inputs. Only combinations which contain *all*
    // dominators will suppress any combinatation that contains *all* suppressors
    auto nDominatorCount = m_DominatorInfo.size();
    for (int i = 0; i < nDominatorCount; ++i) {
        DominatorInfo_t &info = m_DominatorInfo[i];

        // Look for suppressor indices in the control indices list
        auto nCount = info.m_SuppressedIndices.size();
        int j;
        for (j = 0; j < nCount; ++j) {
            if (!std::ranges::contains(op.m_ControlIndices, info.m_SuppressedIndices[j]))
                break;
        }
        if (j != nCount)
            continue;

        op.m_DominatorIndices.emplace_back(i);
    }
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::ComputeCombinationInfo(int nIndex) {
    CleanUpCombinationInfo(nIndex);

    auto nCurrentCount = m_CombinationInfo.size();
    while (nIndex >= nCurrentCount) {
        CombinationInfo_t &info = m_CombinationInfo.emplace_back();
        for (auto & k : info.m_hDestAttribute) {
            k = DMATTRIBUTE_HANDLE_INVALID;
        }
        ++nCurrentCount;
    }

    CombinationInfo_t &info = m_CombinationInfo[nIndex];

    CDmElement* pSource = m_Targets[nIndex];
    if (!pSource)
        return;

    CDmrElementArray<> deltas(pSource, "deltaStates");
    if (!deltas.IsValid())
        return;

    CDmrArray<Vector2D> weights(pSource, "deltaStateWeights");
    if (!weights.IsValid())
        return;

    CDmrArray<Vector2D> weightsLagged(pSource, "deltaStateWeightsLagged");

    // This is an error state
    if (deltas.Count() > weights.size())
        return;

    // This is an error state
    if (weightsLagged.IsValid() && deltas.Count() > weightsLagged.size())
        return;

    info.m_hDestAttribute[COMBO_CONTROL_NORMAL] = weights.GetAttribute()->GetHandle();
    info.m_hDestAttribute[COMBO_CONTROL_LAGGED] = weightsLagged.IsValid()
                                                      ? weightsLagged.GetAttribute()->GetHandle()
                                                      : DMATTRIBUTE_HANDLE_INVALID;

    int* pTemp = (int*) _alloca(m_RawControlInfo.size() * sizeof(int));
    int nDeltaCount = deltas.Count();
    for (int i = 0; i < nDeltaCount; ++i) {
        CDmElement* pDeltaElement = deltas[i];
        if (!pDeltaElement)
            continue;

        int nControlCount = ParseDeltaName(pDeltaElement->GetName(), pTemp);
        if (nControlCount == 0)
            continue;

        CombinationOperation_t &op = info.m_Outputs.emplace_back();
        op.m_nDeltaStateIndex = i;
        op.m_ControlIndices.insert(op.m_ControlIndices.end(), pTemp, pTemp + nControlCount);

        // Find dominators
        FindDominators(op);
    }
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::ComputeCombinationInfo() {
    int nTargetCount = m_Targets.size();
    for (int i = 0; i < nTargetCount; ++i) {
        ComputeCombinationInfo(i);
    }
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::CleanUpCombinationInfo(int nIndex) {
    if (nIndex >= m_CombinationInfo.size())
        return;

    CombinationInfo_t &info = m_CombinationInfo[nIndex];
    for (int i = 0; i < COMBO_CONTROL_TYPE_COUNT; ++i) {
        info.m_hDestAttribute[i] = DMATTRIBUTE_HANDLE_INVALID;
    }
    info.m_Outputs.clear();
}


//-----------------------------------------------------------------------------
// helpers for finding corrector names
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::Resolve() {
    BaseClass::Resolve();

    if (m_InputControls.IsDirty() || m_Dominators.IsDirty()) {
        RebuildRawControlList();
        ComputeCombinationInfo();
    }
}


//-----------------------------------------------------------------------------
// Adds a target for the combination operator
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::AddTarget(CDmElement* pElement) {
    if (!pElement)
        return;

    int i = m_Targets.AddToTail(pElement);
    ComputeCombinationInfo(i);
}


float RemapValue(float flValue, const Vector4D &filterRamp) {
    if (flValue <= filterRamp.x || flValue >= filterRamp.w)
        return 0.0f;

    if (flValue < filterRamp.y)
        return RemapVal(flValue, filterRamp.x, filterRamp.y, 0.0f, 1.0f);

    if (flValue > filterRamp.z)
        return RemapVal(flValue, filterRamp.z, filterRamp.w, 1.0f, 0.0f);

    return 1.0f;
}

//-----------------------------------------------------------------------------
// Remaps non-stereo -> stereo, stereo ->left/right, also adds multilevel + filter
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::ComputeInternalControlValue(RawControlIndex_t nRawControlIndex,
                                                          CombinationControlType_t type, Vector2D &value) {
    const RawControlInfo_t &info = m_RawControlInfo[nRawControlIndex];
    const Vector &vecControlValue = m_ControlValues[type][info.m_InputControl];

    const bool bMultiControl = IsMultiControl(info.m_InputControl);

    if (IsEyelidControl(info.m_InputControl)) {
        float flMultiValue = RemapValue(vecControlValue.z, info.m_FilterRamp);
        if (info.m_bLowerEyelid) {
            value.x = (1.0f - flMultiValue) * vecControlValue.x;
            value.y = (1.0f - flMultiValue) * vecControlValue.y;
        }
        else {
            value.x = flMultiValue * vecControlValue.x;
            value.y = flMultiValue * vecControlValue.y;
        }
    }
    else if (bMultiControl) {
        float flMultiValue = RemapValue(vecControlValue.z, info.m_FilterRamp);
        value.x = flMultiValue * vecControlValue.x;
        value.y = flMultiValue * vecControlValue.y;
    }
    else {
        value.x = RemapValue(vecControlValue.x, info.m_FilterRamp);
        value.y = RemapValue(vecControlValue.y, info.m_FilterRamp);
    }
}


//-----------------------------------------------------------------------------
// Computes lagged input values from non-lagged input
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::ComputeLaggedInputValues() {
    if (!m_bSpecifyingLaggedData)
        return;

    float t = Plat_FloatTime();
    if (m_flLastLaggedComputationTime == FLT_MIN) {
        m_flLastLaggedComputationTime = t;
    }

    float dt = t - m_flLastLaggedComputationTime;
    m_flLastLaggedComputationTime = t;
    float flFactor = ExponentialDecay(0.8, 0.033, dt);

    int nCount = m_ControlValues[COMBO_CONTROL_NORMAL].size();
    for (int i = 0; i < nCount; ++i) {
        Vector vecLerp;
        VectorLerp(m_ControlValues[COMBO_CONTROL_NORMAL][i], m_ControlValues[COMBO_CONTROL_LAGGED][i], flFactor,
                   vecLerp);
        m_ControlValues[COMBO_CONTROL_LAGGED].Set(i, vecLerp);
    }
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::Operate() {
    ComputeLaggedInputValues();

    int nCount = m_CombinationInfo.size();
    for (int i = 0; i < nCount; ++i) {
        CombinationInfo_t &info = m_CombinationInfo[i];

        for (CombinationControlType_t type = COMBO_CONTROL_FIRST; type < COMBO_CONTROL_TYPE_COUNT;
             type = (CombinationControlType_t) (type + 1)) {
            if ((info.m_hDestAttribute[type] == DMATTRIBUTE_HANDLE_INVALID) || !g_pDataModel->IsAttributeHandleValid(
                    info.m_hDestAttribute[type]))
                continue;

            CDmAttribute* pAttribute = g_pDataModel->GetAttribute(info.m_hDestAttribute[type]);
            if (!pAttribute || pAttribute->GetType() != AT_VECTOR2_ARRAY)
                continue;

            CDmrArray<Vector2D> vec2D(pAttribute);

            CombinationControlType_t useType = m_bSpecifyingLaggedData ? type : COMBO_CONTROL_NORMAL;
            int nOutputCount = info.m_Outputs.size();
            for (int j = 0; j < nOutputCount; ++j) {
                CombinationOperation_t &op = info.m_Outputs[j];

                // Compute the core combination
                Vector2D vecValue(1.0f, 1.0f);
                int nCombinationCount = op.m_ControlIndices.size();
                for (int k = 0; k < nCombinationCount; ++k) {
                    Vector2D v;
                    ComputeInternalControlValue(op.m_ControlIndices[k], useType, v);
                    vecValue *= v;
                }


                // Compute the dominators
                auto nDominatorCount = op.m_DominatorIndices.size();
                for (int k = 0; k < nDominatorCount; ++k) {
                    const std::vector<RawControlIndex_t> &dominantIndices = m_DominatorInfo[op.m_DominatorIndices[k]].m_DominantIndices;
                    auto nDominantCount = dominantIndices.size();

                    Vector2D suppressor(-1.0f, -1.0f);
                    for (int l = 0; l < nDominantCount; ++l) {
                        Vector2D v;
                        ComputeInternalControlValue(dominantIndices[l], useType, v);
                        suppressor *= v;
                    }
                    suppressor.x += 1.0f;
                    suppressor.y += 1.0f;

                    vecValue *= suppressor;
                }

                vec2D.Set(op.m_nDeltaStateIndex, vecValue);
            }
        }
    }
}

void CDmeCombinationOperator::GetInputAttributes(std::vector<CDmAttribute*> &attrs) {
    for (int i = 0; i < COMBO_CONTROL_TYPE_COUNT; ++i) {
        attrs.emplace_back(m_ControlValues[i].GetAttribute());
    }
}

void CDmeCombinationOperator::GetOutputAttributes(std::vector<CDmAttribute*> &attrs) {
    int nCount = m_CombinationInfo.size();
    for (int i = 0; i < nCount; ++i) {
        CombinationInfo_t &info = m_CombinationInfo[i];
        for (int j = 0; j < COMBO_CONTROL_TYPE_COUNT; ++j) {
            if ((info.m_hDestAttribute[j] == DMATTRIBUTE_HANDLE_INVALID) || !g_pDataModel->IsAttributeHandleValid(
                    info.m_hDestAttribute[j]))
                continue;

            CDmAttribute* pAttribute = g_pDataModel->GetAttribute(info.m_hDestAttribute[j]);
            if (!pAttribute || pAttribute->GetType() != AT_VECTOR2_ARRAY)
                continue;

            attrs.emplace_back(pAttribute);
        }
    }
}


//-----------------------------------------------------------------------------
// Used by studiomdl to discover the various combination rules
//-----------------------------------------------------------------------------
int CDmeCombinationOperator::GetOperationTargetCount() const {
    return m_Targets.size();
}

CDmElement* CDmeCombinationOperator::GetOperationTarget(int nTargetIndex) {
    return m_Targets[nTargetIndex];
}

int CDmeCombinationOperator::GetOperationCount(int nTargetIndex) const {
    return m_CombinationInfo[nTargetIndex].m_Outputs.size();
}

CDmElement* CDmeCombinationOperator::GetOperationDeltaState(int nTargetIndex, int nOpIndex) {
    CDmElement* pElement = GetOperationTarget(nTargetIndex);
    const CDmrElementArray<> deltaArray(pElement, "deltaStates");
    if (!deltaArray.IsValid())
        return nullptr;

    int nDeltaStateIndex = m_CombinationInfo[nTargetIndex].m_Outputs[nOpIndex].m_nDeltaStateIndex;
    return deltaArray[nDeltaStateIndex];
}

const std::vector<int>& CDmeCombinationOperator::GetOperationControls(int nTargetIndex, int nOpIndex) const {
    return m_CombinationInfo[nTargetIndex].m_Outputs[nOpIndex].m_ControlIndices;
}

int CDmeCombinationOperator::GetOperationDominatorCount(int nTargetIndex, int nOpIndex) const {
    return m_CombinationInfo[nTargetIndex].m_Outputs[nOpIndex].m_DominatorIndices.size();
}

const std::vector<int>& CDmeCombinationOperator::GetOperationDominator(int nTargetIndex, int nOpIndex,
                                                                      int nDominatorIndex) const {
    int nIndex = m_CombinationInfo[nTargetIndex].m_Outputs[nOpIndex].m_DominatorIndices[nDominatorIndex];
    return m_DominatorInfo[nIndex].m_DominantIndices;
}


//-----------------------------------------------------------------------------
// Generates wrinkle deltas for the uncorrected controls
// NOTE: This is only being used because we have no authoring path for wrinkle data yet
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::GenerateWrinkleDeltas(
    CDmeShape* pShape,
    bool bOverwrite,
    bool bUseNormalForSign /* = false */,
    float flScale /* = 1.0f */) {
    auto* pMesh = CastElement<CDmeMesh>(pShape);
    CDmeVertexData* pBindState = pMesh ? pMesh->FindBaseState("bind") : nullptr;
    if (pBindState) {
        auto nRawControlCount = m_RawControlInfo.size();
        for (int i = 0; i < nRawControlCount; ++i) {
            CDmeVertexDeltaData* pDelta = pMesh->FindDeltaState(m_RawControlInfo[i].m_Name);
            if (pDelta) {
                if (bUseNormalForSign) {
                    pDelta->GenerateWrinkleDelta(pBindState, flScale, bOverwrite, bUseNormalForSign);
                }
                else {
                    pDelta->GenerateWrinkleDelta(pBindState, m_RawControlInfo[i].m_flWrinkleScale, bOverwrite,
                                                 bUseNormalForSign);
                }
            }
        }
    }
}


void CDmeCombinationOperator::GenerateWrinkleDeltas(bool bOverwrite /* = true */, bool bUseNormalForSign /* = false */,
                                                    float flScale /* = 1.0f */) {
    int nTargetCount = m_Targets.size();
    for (int i = 0; i < nTargetCount; ++i) {
        GenerateWrinkleDeltas(CastElement<CDmeShape>(m_Targets[i]), bOverwrite, bUseNormalForSign, flScale);
    }
}


//-----------------------------------------------------------------------------
// * Remove all controls which refer to raw controls which refer to delta
//   states which do not exist in any target
// * Remove all dominators which refer to states which no longer exist
//-----------------------------------------------------------------------------
void CDmeCombinationOperator::Purge() {
    bool bDelete = true;

    const int nTargetCount = m_Targets.size();
    for (int i = 0; i < nTargetCount; ++i) {
        CDmElement* pSource = m_Targets[i];
        if (!pSource)
            continue;

        CDmrElementArray<> deltas(pSource, "deltaStates");
        if (!deltas.IsValid())
            continue;

        const int nDeltaCount = deltas.Count();

        do {
            bDelete = false;

            const int nControlCount = GetControlCount();
            for (int j = 0; j < nControlCount; ++j) {
                bDelete = true;
                const int nRawControlCount = GetRawControlCount(j);
                for (int k = 0; bDelete && k < nRawControlCount; ++k) {
                    for (int l = 0; l < nDeltaCount; ++l) {
                        if (!Q_strcmp(GetRawControlName( j, k ), deltas[ l ]->GetName())) {
                            bDelete = false;
                            break;
                        }
                    }
                }

                if (bDelete) {
                    RemoveControl(GetControlName(j));
                    break;
                }
            }
        } while (bDelete);
    }

    do {
        bDelete = false;

        const int nDomCount = DominationRuleCount();
        for (int i = 0; i < nDomCount; ++i) {
            bDelete = true;
            CDmeCombinationDominationRule* pDom = GetDominationRule(i);

            const int nDCount = pDom->DominatorCount();
            for (int j = 0; j < nDCount; ++j) {
                if (HasRawControl(pDom->GetDominator(j))) {
                    bDelete = false;
                    break;
                }
            }

            if (!bDelete) {
                const int nSCount = pDom->SuppressedCount();
                for (int j = 0; j < nSCount; ++j) {
                    if (HasRawControl(pDom->GetSuppressed(j))) {
                        bDelete = false;
                        break;
                    }
                }
            }

            if (bDelete) {
                RemoveDominationRule(i);
                break;
            }
        }
    } while (bDelete);
}


//-----------------------------------------------------------------------------
//
// A class used to edit combination operators in Maya.. doesn't connect to targets
//
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeMayaCombinationOperator, CDmeMayaCombinationOperator);


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeMayaCombinationOperator::OnConstruction() {
    m_DeltaStates.Init(this, "deltaStates");
    m_DeltaStateWeights[COMBO_CONTROL_NORMAL].Init(this, "deltaStateWeights");
    m_DeltaStateWeights[COMBO_CONTROL_LAGGED].Init(this, "deltaStateWeightsLagged");
    AddTarget(this);
}

void CDmeMayaCombinationOperator::OnDestruction() {
}

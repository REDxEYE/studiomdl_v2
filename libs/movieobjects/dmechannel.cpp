//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "movieobjects/dmechannel.h"

#include "tier1/keyvalues.h"


// Paste data utility function declarations
KeyValues* FindLayerInPasteData(const CUtlVector<KeyValues*> &list, CDmeLog* log);

static int FindSpanningLayerAndSetIntensity(DmeLog_TimeSelection_t &ts, LayerSelectionData_t* data);


//-----------------------------------------------------------------------------
//
// CRecordingLayer
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Default constructor
//-----------------------------------------------------------------------------
CRecordingLayer::CRecordingLayer()
    : m_tHeadShotTime(DMETIME_INVALID)
      , m_ProceduralType(0)
      , m_OperationFlags(0)
      , m_RandomSeed(0)
      , m_flThreshold(0)
      , m_flIntensity(0)
      , m_RecordingMode(RECORD_PRESET)
      , m_pfnAddChannelCallback(nullptr)
      , m_pfnFinishChannelCallback(nullptr) {
}

//-----------------------------------------------------------------------------
// Destructor, releases any log layer data that may be held by the recording layer.
//-----------------------------------------------------------------------------
CRecordingLayer::~CRecordingLayer() {
    int nChannels = m_LayerChannels.size();
    for (int iChannel = 0; iChannel < nChannels; ++iChannel) {
        LayerChannelInfo_t &info = m_LayerChannels[iChannel];
        g_pDataModel->DestroyElement(info.m_hRawDataLayer);
    }
}


//-----------------------------------------------------------------------------
//
// CDmeChannelModificationLayer
//
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Default constructor
//-----------------------------------------------------------------------------
CDmeChannelModificationLayer::CDmeChannelModificationLayer()
    : m_bVisible(true) {
}


//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
CDmeChannelModificationLayer::~CDmeChannelModificationLayer() {
}


//-----------------------------------------------------------------------------
//
// CDmeChannelRecordingMgr
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
static CDmeChannelRecordingMgr s_ChannelRecordingMgr;
CDmeChannelRecordingMgr* g_pChannelRecordingMgr = &s_ChannelRecordingMgr;


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CDmeChannelRecordingMgr::CDmeChannelRecordingMgr() {
    m_bUseTimeSelection = false;
    m_bModificationLayerDirty = false;
    m_bModificationProcessing = false;
    m_bWantsToFinish = false;
    m_bFinishFlattenLayers = false;
    m_bModificationLayerEnabled = true;

    m_pActiveRecordingLayer = nullptr;
    m_pModificationLayer = nullptr;

    m_nProceduralType = PROCEDURAL_PRESET_NOT;
    m_pRevealTarget = nullptr;
    m_RandomSeed = 0;
    m_TransformWriteMode = TRANSFORM_WRITE_MODE_TRANSFORM;
}


//-----------------------------------------------------------------------------
// Purpose: Store the data of the provided attribute in the channel's recording
// data.
// Input  : nChannelIndex - Index of the channel within the recording layer's
//			array of active channels.
// Input  : fromAttr - flag indicating if the data of the from attribute is to
//			be stored ( true ) or the data of the to attribute is to be stored
//			( false ).
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::StoreChannelAttributeData(int nChannelIndex, bool fromAttr) {
    Assert(m_pActiveRecordingLayer);

    if (m_pActiveRecordingLayer) {
        LayerChannelInfo_t &info = m_pActiveRecordingLayer->m_LayerChannels[nChannelIndex];
        CDmeChannel* pChannel = info.m_Channel.Get();

        // The data from the provided attribute may be stored in either the to or from attribute
        // data for the channel, select the appropriate data buffer based on selection flag.
        CUtlBuffer* pBuffer = fromAttr ? &info.m_FromAttrData : &info.m_ToAttrData;
        CDmAttribute* pAttr = fromAttr ? pChannel->GetFromAttribute() : pChannel->GetToAttribute();

        if (pAttr) {
            // Clear the buffer, we only want to store a single value.
            pBuffer->Clear();

            // If storing the to attribute data, make sure it is current and has the
            // actual value of the log, not the control default value if the log is empty.
            if (!fromAttr) {
                pChannel->Play(true);
            }

            if (IsArrayType(pAttr->GetType())) {
                int nArrayIndex = fromAttr ? pChannel->GetFromArrayIndex() : pChannel->GetToArrayIndex();
                pAttr->SerializeElement(nArrayIndex, *pBuffer);
            }
            else {
                pAttr->Serialize(*pBuffer);
            }
        }
    }
}


//-----------------------------------------------------------------------------
// Computes time selection info in log time for a particular recorded channel
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::GetLocalTimeSelection(DmeLog_TimeSelection_t &selection, int nIndex) {
    Assert(m_bUseTimeSelection);
    Assert(m_pActiveRecordingLayer);

    if (m_pActiveRecordingLayer) {
        LayerChannelInfo_t &info = m_pActiveRecordingLayer->m_LayerChannels[nIndex];
        CDmeChannel* pChannel = info.m_Channel.Get();
        Assert(pChannel);
        if (pChannel == nullptr)
            return;


        selection = m_TimeSelection;
        for (int i = 0; i < TS_TIME_COUNT; ++i) {
            selection.m_nTimes[i] = info.m_ClipStack.ToChildMediaTime(selection.m_nTimes[i], false);
        }
        selection.m_pPresetValue = info.m_pPresetValue;
        selection.m_pPresetTimes = info.m_pPresetTimes;
        selection.m_tHeadPosition = info.m_HeadPosition;
        selection.m_TransformWriteMode = info.m_TransformWriteMode;
        selection.m_nComponentFlags = info.m_ComponentFlags;
        selection.m_bManipulateInFalloff = info.m_bManipulateInFalloff;


        // If the time selection is marked as infinite, make sure it extents past the ends of the log
        CDmeLog* pLog = pChannel->GetLog();
        if (pLog && selection.m_bInfinite[0]) {
            selection.m_nTimes[TS_LEFT_HOLD] = MIN(pLog->GetBeginTime(), selection.m_nTimes[ TS_LEFT_HOLD ]);
            selection.m_nTimes[TS_LEFT_FALLOFF] = selection.m_nTimes[TS_LEFT_HOLD];
        }

        if (pLog && selection.m_bInfinite[1]) {
            selection.m_nTimes[TS_RIGHT_HOLD] = MAX(pLog->GetEndTime(), selection.m_nTimes[ TS_RIGHT_HOLD ]);
            selection.m_nTimes[TS_RIGHT_FALLOFF] = selection.m_nTimes[TS_RIGHT_HOLD];
        }

        // If the modification layer is currently being processed set the pointer to the channel's to attribute
        // to be used as the old head position, this is required because the value of the head position now may
        // not be what it was when the recording first took place.
        if (m_bModificationProcessing) {
            CDmAttribute* pAttr = pChannel->GetToAttribute();

            if (pAttr) {
                selection.m_pOldHeadValue = pAttr;
                if (IsArrayType(pAttr->GetType())) {
                    selection.m_OldHeadValueIndex = pChannel->GetToArrayIndex();
                }
            }
        }
    }
}


bool CDmeChannelRecordingMgr::IsModificationLayerActive() const {
    return ((m_pModificationLayer != nullptr) && (m_bWantsToFinish == false));
}

bool CDmeChannelRecordingMgr::ShouldRecordUsingTimeSelection() const {
    return m_bUseTimeSelection;
}


int CDmeChannelRecordingMgr::GetProceduralType() const {
    return m_nProceduralType;
}

const CDmAttribute* CDmeChannelRecordingMgr::GetProceduralTarget() const {
    Assert(m_pRevealTarget);
    return m_pRevealTarget;
}

const CUtlVector<KeyValues*>& CDmeChannelRecordingMgr::GetPasteTarget() const {
    return m_PasteTarget;
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeChannel, CDmeChannel);


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDmeChannel::OnConstruction() {
    m_nRecordLayerIndex = -1;
    m_nNextCurveType = CURVE_DEFAULT;

    m_fromElement.Init(this, "fromElement", FATTRIB_HAS_CALLBACK | FATTRIB_NEVERCOPY);
    m_fromAttribute.Init(this, "fromAttribute", FATTRIB_TOPOLOGICAL | FATTRIB_HAS_CALLBACK);
    m_fromIndex.Init(this, "fromIndex", FATTRIB_TOPOLOGICAL);
    m_toElement.Init(this, "toElement", FATTRIB_HAS_CALLBACK | FATTRIB_NEVERCOPY);
    m_toAttribute.Init(this, "toAttribute", FATTRIB_TOPOLOGICAL | FATTRIB_HAS_CALLBACK);
    m_toIndex.Init(this, "toIndex", FATTRIB_TOPOLOGICAL);
    m_mode.InitAndSet(this, "mode", (int) CM_PASS);
    m_log.Init(this, "log");
    m_FromAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;
    m_ToAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;
}

void CDmeChannel::OnDestruction() {
}

int CDmeChannel::GetFromArrayIndex() const {
    return m_fromIndex;
}

int CDmeChannel::GetToArrayIndex() const {
    return m_toIndex;
}


void CDmeChannel::Play(bool useEmptyLog /*= false*/) {
    CDmAttribute* pToAttr = GetToAttribute();

    if (pToAttr == nullptr)
        return;

    CDmeLog* pLog = GetLog();
    if (!pLog)
        return;

    DmeTime_t time = GetCurrentTime();

    DmeTime_t t0 = pLog->GetBeginTime();
    DmeTime_t tn = pLog->GetEndTime();

    PlayMode_t pmode = PM_HOLD;
    switch (pmode) {
        case PM_HOLD:
            time = clamp(time, t0, tn);
            break;

        case PM_LOOP:
            if (tn == t0) {
                time = t0;
            }
            else {
                time -= t0;
                time = time % (tn - t0);
                time += t0;
            }
            break;
    }

    // We might not want to do it this way, but this makes empty logs not get in the way if there is a valid pFromAttr
    if (!useEmptyLog) {
        if (pLog->IsEmpty() && !pLog->HasDefaultValue() &&
            GetFromAttribute() != nullptr) {
            Pass();
            return;
        }
    }

    pLog->GetValue(time, pToAttr, m_toIndex.Get());
}

void CDmeChannel::Pass() {
    CDmAttribute* pFromAttr = GetFromAttribute();
    CDmAttribute* pToAttr = GetToAttribute();
    if (!pFromAttr || !pToAttr)
        return;

    if (pFromAttr == pToAttr)
        return;

    DmAttributeType_t type = pFromAttr->GetType();
    const void* pValue = nullptr;
    if (IsArrayType(type)) {
        CDmrGenericArray array(pFromAttr);
        pValue = array.GetUntyped(m_fromIndex.Get());
        type = ArrayTypeToValueType(type);
    }
    else {
        pValue = pFromAttr->GetValueUntyped();
    }

    if (IsArrayType(pToAttr->GetType())) {
        CDmrGenericArray array(pToAttr);
        array.Set(m_toIndex.Get(), type, pValue);
    }
    else {
        pToAttr->SetValue(type, pValue);
    }
}

//-----------------------------------------------------------------------------
// IsDirty - ie needs to operate
//-----------------------------------------------------------------------------
bool CDmeChannel::IsDirty() {
    if (BaseClass::IsDirty())
        return true;

    switch (GetMode()) {
        case CM_PLAY:
            return true;

        case CM_RECORD:
            if (m_nRecordLayerIndex != -1)
                return true;

        // NOTE: Fall through!
        case CM_PASS: {
            CDmAttribute* pFromAttr = GetFromAttribute();
            if (pFromAttr && pFromAttr->IsFlagSet(FATTRIB_OPERATOR_DIRTY))
                return true;
        }
        break;

        default:
            break;
    }
    return false;
}


void CDmeChannel::Operate() {
    switch (GetMode()) {
        case CM_OFF:
            return;

        case CM_PLAY:
            Play();
            return;

        case CM_RECORD:
            Record();
            return;

        case CM_PASS:
            Pass();
            return;
    }
}

void CDmeChannel::GetInputAttributes(std::vector<CDmAttribute*> &attrs) {
    ChannelMode_t mode = GetMode();
    if (mode == CM_OFF || mode == CM_PLAY)
        return; // off and play ignore inputs

    CDmAttribute* pAttr = GetFromAttribute();
    if (pAttr != nullptr) {
        attrs.push_back(pAttr);
    }
}

void CDmeChannel::GetOutputAttributes(std::vector<CDmAttribute*> &attrs) {
    ChannelMode_t mode = GetMode();
    if (mode == CM_OFF)
        return; // off ignores inputs

    if (mode == CM_RECORD || mode == CM_PASS) {
        if (GetFromAttribute() == GetToAttribute())
            return; // record/pass from and to the same attribute doesn't write anything
    }

    CDmAttribute* pAttr = GetToAttribute();
    if (pAttr != nullptr) {
        attrs.push_back(pAttr);
    }
}


//-----------------------------------------------------------------------------
// accessors
//-----------------------------------------------------------------------------
CDmElement* CDmeChannel::GetFromElement() const {
    return m_fromElement;
}

CDmElement* CDmeChannel::GetToElement() const {
    return m_toElement;
}

CDmeLog* CDmeChannel::CreateLog(DmAttributeType_t type) {
    CDmeLog* log = CDmeLog::CreateLog(type, GetFileId());
    m_log.Set(log);
    return log;
}

// HACK:  This is an evil hack since the element and attribute change sequentially, but they really need to change in lockstep or else you're looking
//  up an attribute from some other element or vice versa.


void CDmeChannel::SetOutput(CDmElement* pElement, const char* pAttribute, int index) {
    m_toElement.Set(pElement);
    m_toAttribute.Set(pAttribute);
    m_toIndex.Set(index);
    SetupToAttribute();
}


ChannelMode_t CDmeChannel::GetMode() {
    return static_cast<ChannelMode_t>(m_mode.Get());
}

void CDmeChannel::SetMode(ChannelMode_t mode) {
    if (mode != m_mode) {
        m_mode.Set(static_cast<int>(mode));
        m_TimeState.m_tPreviousTime = DMETIME_INVALID;
    }
}

CDmeLog* CDmeChannel::GetLog() {
    if (!m_log.GetElement() && (m_FromAttributeHandle == DMATTRIBUTE_HANDLE_INVALID)) {
        // NOTE: This will generate a new log based on the from attribute
        SetupFromAttribute();
    }
    return m_log.GetElement();
}


//-----------------------------------------------------------------------------
// Used to cache off handles to attributes
//-----------------------------------------------------------------------------
CDmAttribute* CDmeChannel::SetupFromAttribute() {
    m_FromAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;

    CDmElement* pObject = m_fromElement.GetElement();
    const char* pName = m_fromAttribute.Get();
    if (pObject == nullptr || pName == nullptr || !pName[0])
        return nullptr;

    CDmAttribute* pAttr = pObject->GetAttribute(pName);
    if (!pAttr)
        return nullptr;

    m_FromAttributeHandle = pAttr->GetHandle();

    DmAttributeType_t fromType = pAttr->GetType();
    if (IsArrayType(fromType)) {
        fromType = ArrayTypeToValueType(fromType);
    }

    CDmeLog* pLog = m_log.GetElement();
    if (pLog == nullptr) {
        CreateLog(fromType);
        return pAttr;
    }

    DmAttributeType_t logType = pLog->GetDataType();
    if (IsArrayType(logType)) {
        logType = ArrayTypeToValueType(logType);
    }

    if (logType != fromType) {
        // NOTE: This will release the current log
        CreateLog(fromType);
    }

    return pAttr;
}

CDmAttribute* CDmeChannel::SetupToAttribute() {
    m_ToAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;

    CDmElement* pObject = m_toElement.GetElement();
    const char* pName = m_toAttribute.Get();
    if (pObject == nullptr || pName == nullptr || !pName[0])
        return nullptr;

    CDmAttribute* pAttr = pObject->GetAttribute(pName);
    if (!pAttr)
        return nullptr;

    m_ToAttributeHandle = pAttr->GetHandle();
    return pAttr;
}


//-----------------------------------------------------------------------------
// This function gets called whenever an attribute changes
//-----------------------------------------------------------------------------
void CDmeChannel::OnAttributeChanged(CDmAttribute* pAttribute) {
    if ((pAttribute == m_fromElement.GetAttribute()) ||
        (pAttribute == m_fromAttribute.GetAttribute())) {
        // NOTE: This will force a recache of the attribute handle
        m_FromAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;
        return;
    }

    if ((pAttribute == m_toElement.GetAttribute()) ||
        (pAttribute == m_toAttribute.GetAttribute())) {
        m_ToAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;
        return;
    }

    BaseClass::OnAttributeChanged(pAttribute);
}


DmeTime_t CDmeChannel::GetCurrentTime() const {
    return m_TimeState.m_tCurrentTime;
}

//-----------------------------------------------------------------------------
// Simple version. Only works if multiple active channels clips
// do not reference the same channels
//-----------------------------------------------------------------------------
void CDmeChannel::SetCurrentTime(DmeTime_t time) {
    m_TimeState.m_tPreviousTime = m_TimeState.m_tCurrentTime;
    m_TimeState.m_tCurrentTime = time;
    m_TimeState.m_timeOutsideTimeframe = DMETIME_ZERO;
}

CDmeLogLayer* FindLayerInSnapshot(const CDmrElementArray<CDmElement> &snapshotArray, CDmeLog* origLog) {
    if (!snapshotArray.IsValid())
        return nullptr;

    int c = snapshotArray.Count();
    for (int i = 0; i < c; ++i) {
        CDmeLogLayer* layer = CastElement<CDmeLogLayer>(snapshotArray[i]);
        if (!layer)
            continue;

        CDmeLog* pLog = layer->GetValueElement<CDmeLog>("origLog");
        if (!pLog) {
            Assert(0);
            continue;
        }

        if (pLog == origLog)
            return layer;
    }

    return nullptr;
}

KeyValues* FindLayerInPasteData(const CUtlVector<KeyValues*> &list, CDmeLog* log) {
    int c = list.size();
    for (int i = 0; i < c; ++i) {
        KeyValues* kv = list[i];
        Assert(kv);

        if (!vstd::iequals(kv->GetName(), "ControlLayers"))
            continue;

        LayerSelectionData_t* data = reinterpret_cast<LayerSelectionData_t*>(kv->GetPtr("LayerData"));
        if (!data)
            continue;

        CDmeChannel* ch = data->m_hChannel;
        if (!ch)
            continue;

        CDmeLog* chLog = ch->GetLog();
        if (chLog == log)
            return kv;
    }

    return nullptr;
}

static int FindSpanningLayerAndSetIntensity(DmeLog_TimeSelection_t &ts, LayerSelectionData_t* data) {
    Assert(data->m_vecData.Count() >= 2);

    float frac = ts.m_flIntensity;
    int i = 0;
    for (; i < data->m_vecData.size() - 1; ++i) {
        LayerSelectionData_t::DataLayer_t* current = &data->m_vecData[i];
        LayerSelectionData_t::DataLayer_t* next = &data->m_vecData[i + 1];

        if (frac >= current->m_flStartFraction && frac <= next->m_flStartFraction) {
            frac = RemapVal(frac, current->m_flStartFraction, next->m_flStartFraction, 0.0f, 1.0f);
            ts.m_flIntensity = frac;
            break;
        }
    }

    return i;
}


void CDmeChannel::Record() {
    CDmElement* pElement = GetFromElement();
    CDmAttribute* pFromAttr = GetFromAttribute();
    if (pFromAttr == nullptr)
        return; // or clear out the log?

    CDmeLog* pLog = GetLog();
    DmeTime_t time = GetCurrentTime();
    if (m_TimeState.m_tPreviousTime == DMETIME_INVALID) {
        m_TimeState.m_tPreviousTime = time;
    }

    if (g_pChannelRecordingMgr->ShouldRecordUsingTimeSelection()) {
        Assert(m_nRecordLayerIndex != -1);
        if (m_nRecordLayerIndex == -1)
            return;

        // Computes local times for the time selection
        DmeLog_TimeSelection_t timeSelection;
        g_pChannelRecordingMgr->GetLocalTimeSelection(timeSelection, m_nRecordLayerIndex);

        int nType = g_pChannelRecordingMgr->GetProceduralType();

        enum { PROCEDURAL_PRESET_ANIMATED = NUM_PROCEDURAL_PRESET_TYPES + 1 };
        if (nType == PROCEDURAL_PRESET_NOT && timeSelection.m_pPresetTimes) {
            nType = PROCEDURAL_PRESET_ANIMATED; // this is an animated preset, which is blended in like paste/spline/etc
        }

        switch (nType) {
            default:
            case PROCEDURAL_PRESET_NOT: {
                DmeLogTransformParams_t transformParams;
                transformParams.m_nProceduralType = nType;

                // Only transform controls (SFM, removed) can have manipulation in falloff
                timeSelection.m_bManipulateInFalloff = false;

                // Stamp the key at the current head position, re-sampling the current time selection if time is not moving or writing
                // a key at the current time if time is moving. If the modification layer is active and the time is moving forward the
                // key will be recorded without respect to the current time selection as the time selection will be applied later.
                g_pChannelRecordingMgr->StoreChannelAttributeData(m_nRecordLayerIndex, true);
                pLog->StampKeyAtHead(time, m_TimeState.m_tPreviousTime, timeSelection, transformParams, pFromAttr,
                                     m_fromIndex.Get(), !g_pChannelRecordingMgr->IsModificationLayerActive());
            }
                break;
            case PROCEDURAL_PRESET_REVEAL: {
                // Find the matching layer in the "target" data array
                const CDmrElementArray<CDmElement> snapshotArray = const_cast<CDmAttribute*>(g_pChannelRecordingMgr->
                    GetProceduralTarget());
                CDmeLogLayer* snapshotLayer = FindLayerInSnapshot(snapshotArray, pLog);
                if (snapshotLayer) {
                    Assert(pLog);
                    pLog->RevealUsingTimeSelection(timeSelection, snapshotLayer);
                }
            }
            break;
            case PROCEDURAL_PRESET_DROP_LAYER: {
                int nLayers = pLog->GetNumLayers();

                // There must be at least 3 layers in order for the drop to operate:
                // the base layer, the modification layer and the active recording layer.
                if (nLayers >= 3) {
                    CDmeLogLayer* pRecLayer = pLog->GetLayer(nLayers - 1);
                    CDmeLogLayer* pModLayer = pLog->GetLayer(nLayers - 2);
                    CDmeLogLayer* pBaseLayer = pLog->GetLayer(nLayers - 3);

                    pLog->BlendLayersUsingTimeSelection(pBaseLayer, pModLayer, pRecLayer, timeSelection, true, false,
                                                        false, DMETIME_ZERO);
                }
            }
            break;
            case PROCEDURAL_PRESET_INOUT:
            case PROCEDURAL_PRESET_JITTER:
            case PROCEDURAL_PRESET_SMOOTH:
            case PROCEDURAL_PRESET_SHARPEN:
            case PROCEDURAL_PRESET_SOFTEN:
            case PROCEDURAL_PRESET_STAGGER:
            case PROCEDURAL_PRESET_HOLD:
            case PROCEDURAL_PRESET_RELEASE:
            case PROCEDURAL_PRESET_STEADY:
            case PROCEDURAL_PRESET_PASTE:
            case PROCEDURAL_PRESET_SPLINE:
            case PROCEDURAL_PRESET_ANIMATED: {
                const CUtlVector<KeyValues*> &pasteTarget = g_pChannelRecordingMgr->GetPasteTarget();
                KeyValues* layer = FindLayerInPasteData(pasteTarget, pLog);
                if (layer) {
                    LayerSelectionData_t* data = reinterpret_cast<LayerSelectionData_t*>(layer->GetPtr("LayerData"));
                    Assert(data);

                    DmeLog_TimeSelection_t blendTimeSelction = timeSelection;
                    int iSourceLayer = FindSpanningLayerAndSetIntensity(blendTimeSelction, data);

                    CDmeLogLayer* sourceLayer = data->m_vecData[iSourceLayer].m_hData.Get();
                    CDmeLogLayer* targetLayer = data->m_vecData[iSourceLayer + 1].m_hData.Get();
                    if (sourceLayer && sourceLayer->GetKeyCount() > 0 &&
                        targetLayer && targetLayer->GetKeyCount() > 0 &&
                        sourceLayer->GetKeyCount() == targetLayer->GetKeyCount()) {
                        Assert(pLog->GetNumLayers() >= 2);
                        CDmeLogLayer* outputLayer = pLog->GetLayer(pLog->GetTopmostLayer());
                        if (IsPresetTimeOperation(nType)) // stagger and steady operate on sample times mostly
                        {
                            bool bFeatherBlendInFalloff =
                                    nType == PROCEDURAL_PRESET_STAGGER || nType == PROCEDURAL_PRESET_STEADY;
                            pLog->BlendTimesUsingTimeSelection(sourceLayer, targetLayer, outputLayer, blendTimeSelction,
                                                               data->m_nTimes[TS_LEFT_FALLOFF], bFeatherBlendInFalloff);
                        }
                        else if (iSourceLayer != 0) {
                            CDmeLogLayer* baseLayer = data->m_vecData[0].m_hData.Get();
                            pLog->BlendLayersUsingTimeSelection(baseLayer, sourceLayer, targetLayer, outputLayer,
                                                                blendTimeSelction, false,
                                                                data->m_nTimes[TS_LEFT_FALLOFF]);
                        }
                        else {
                            pLog->BlendLayersUsingTimeSelection(sourceLayer, targetLayer, outputLayer,
                                                                blendTimeSelction, false, true, true,
                                                                data->m_nTimes[TS_LEFT_FALLOFF]);
                        }
                    }
                }
            }
            break;
        }
    }
    else {
        int bestLayer = pLog->GetTopmostLayer();
        SegmentInterpolation_t interpSetting = (bestLayer == -1)
                                                   ? SEGMENT_INTERPOLATE
                                                   : pLog->GetLayer(bestLayer)->GetSegmentInterpolationSetting(
                                                       m_TimeState.m_tPreviousTime, DMETIME_INVALID, false);

        if (interpSetting == SEGMENT_INTERPOLATE) //use existing behavior
        {
            if (m_TimeState.m_tPreviousTime != time) {
                pLog->SetDuplicateKeyAtTime(m_TimeState.m_tPreviousTime);
            }
            pLog->SetKey(time, pFromAttr, m_fromIndex.Get(), SEGMENT_INTERPOLATE, m_nNextCurveType);
        }
        else {
            //attempt to preserve the non-interpolated segment as it is. I believe that this will also work for the general case above. But I'm not 100% sure at the moment. Best not to break existing behavior
            if (m_TimeState.m_tPreviousTime != time) {
                pLog->InsertKeyAtTime(m_TimeState.m_tPreviousTime);
            }
            pLog->SetKey(time, pFromAttr, m_fromIndex.Get(),
                         pLog->GetLayer(bestLayer)->GetSegmentInterpolationSetting(time), m_nNextCurveType);
        }
        m_nNextCurveType = CURVE_DEFAULT;
    }

    // Output the data that's in the log
    Play();
}


//-----------------------------------------------------------------------------
// Builds a clip stack that passes through root + shot
// Returns true if it succeeded
//-----------------------------------------------------------------------------
bool CDmeChannel::BuildClipStack(DmeClipStack_t* pClipStack, CDmeClip* pRoot, CDmeClip* pShot) const {
    DmAttributeReferenceIterator_t it;
    for (it = g_pDataModel->FirstAttributeReferencingElement(GetHandle());
         it != DMATTRIBUTE_REFERENCE_ITERATOR_INVALID;
         it = g_pDataModel->NextAttributeReferencingElement(it)) {
        CDmAttribute* pAttribute = g_pDataModel->GetAttribute(it);
        CDmElement* pElement = pAttribute->GetOwner();
        CDmeChannelsClip* pChannelsClip = CastElement<CDmeChannelsClip>(pElement);
        if (!pChannelsClip)
            continue;

        if (pChannelsClip->BuildClipStack(pClipStack, pRoot, pShot))
            return true;
    }
    return false;
}


void CDmeChannel::ScaleSampleTimes(float scale) {
    m_log->ScaleSampleTimes(scale);
}

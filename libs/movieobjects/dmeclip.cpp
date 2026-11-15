//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#include "movieobjects/dmeclip.h"

#include "movieobjects/dmetimeframe.h"
#include "movieobjects/dmechannel.h"


//-----------------------------------------------------------------------------
// CDmeClip - common base class for channel clips
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeClip, CDmeClip);

void CDmeClip::OnConstruction() {
    m_TimeFrame.InitAndCreate(this, "timeFrame");
    m_ClipColor.InitAndSet(this, "color", Color(0, 0, 0, 0));
    m_ClipText.Init(this, "text");
    m_bMute.Init(this, "mute");
    m_flDisplayScale.InitAndSet(this, "displayScale", 1.0f);
}

void CDmeClip::OnDestruction() {
}


DmeTime_t CDmeClip::ToChildMediaTime(DmeTime_t t, bool bClamp) const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->ToChildMediaTime(t, bClamp) : DmeTime_t(0);
}

DmeTime_t CDmeClip::FromChildMediaTime(DmeTime_t t, bool bClamp) const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->FromChildMediaTime(t, bClamp) : DmeTime_t(0);
}

DmeTime_t CDmeClip::FromChildMediaDuration(DmeTime_t dt) const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->FromChildMediaDuration(dt) : DmeTime_t(0);
}

DmeTime_t CDmeClip::GetTimeOffset() const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->GetTimeOffset() : DmeTime_t(0);
}

float CDmeClip::GetTimeScale() const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->GetTimeScale() : 0.0f;
}

DmeTime_t CDmeClip::GetStartTime() const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->GetStartTime() : DmeTime_t(0);
}

DmeTime_t CDmeClip::GetEndTime() const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->GetStartTime() + tf->GetDuration() : DmeTime_t(0);
}

DmeTime_t CDmeClip::GetDuration() const {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    return tf ? tf->GetDuration() : DmeTime_t(0);
}


void CDmeClip::BakeTimeScale(float scale /*= 1.0f*/) {
    CDmeTimeFrame *tf = m_TimeFrame.GetElement();
    Assert(tf);
    if (!tf)
        return;

    tf->SetTimeScale(1.0f);

    if (scale != 1.0f) {
        tf->SetStartTime(tf->GetStartTime() / scale);
        tf->SetDuration(tf->GetDuration() / scale);
        tf->SetTimeOffset(tf->GetTimeOffset() / scale);
    }
}


bool CDmeClip::BuildClipStack(DmeClipStack_t *pStack, const CDmeClip *pMovie, CDmeClip *pShot /*=NULL*/) {
    return pStack->BuildClipStack(pMovie, pShot, this);
}


//-----------------------------------------------------------------------------
// Clip stack
//-----------------------------------------------------------------------------
bool DmeClipStack_t::BuildClipStack(const CDmeClip *pMovie, const CDmeClip *pShot, const CDmeClip *pClip) {
    RemoveAll();

    // Clips have no parent clips (the film clip / track group hierarchy is gone),
    // so the only stack that reaches the root through the shot is the root itself
    if (!pClip || pClip != pMovie || (pShot && pShot != pClip))
        return false;

    AddClipToHead(pClip);
    return true;
}

int DmeClipStack_t::AddClipToHead(const CDmeClip *pClip) {
    if (!pClip)
        return m_clips.InvalidIndex();

    m_bOptimized = false;
    return m_clips.AddToHead(pClip->GetHandle());
}

void DmeClipStack_t::Optimize() const {
    m_bOptimized = true;

    int nClips = m_clips.size();
    if (nClips == 0) {
        m_tStart = m_tOffset = DMETIME_MINTIME + DMETIME_MAXTIME / 2; // HACK
        m_tDuration = DMETIME_MAXTIME;
        m_flScale = 1.0f;
        return;
    }

    // 0 = global, n-1 = local
    const CDmeClip *pClip = m_clips[nClips - 1];
    m_tStart = pClip->GetStartTime();
    m_tOffset = pClip->GetTimeOffset();
    m_tDuration = pClip->GetDuration();
    m_flScale = pClip->GetTimeScale();

    for (int i = nClips - 2; i > 0; --i) {
        const CDmeClip *pClip = m_clips[i];

        DmeTime_t tChildStartInParentTime = pClip->FromChildMediaTime(m_tStart, false);
        DmeTime_t tChildOffsetInParentTime = pClip->FromChildMediaDuration(m_tOffset);
        DmeTime_t tChildDurationInParentTime = pClip->FromChildMediaDuration(m_tDuration);
        DmeTime_t tChildEndInParentTime = tChildStartInParentTime + tChildDurationInParentTime;
        m_flScale = pClip->GetTimeScale() * m_flScale;

        m_tStart = MAX(tChildStartInParentTime, pClip->GetStartTime());
        DmeTime_t tDiff = tChildStartInParentTime - m_tStart;
        m_tOffset = tChildOffsetInParentTime - tDiff;
        DmeTime_t tEnd = MIN(tChildEndInParentTime, pClip->GetEndTime());
        m_tDuration = MAX(DMETIME_ZERO, tEnd - m_tStart);
    }
}

DmeTime_t DmeClipStack_t::ToChildMediaTime(DmeTime_t t, bool bClamp /*=true*/) const {
    if (!m_bOptimized) {
        Optimize();
    }

    t -= m_tStart;
    if (bClamp) {
        t.Clamp(DMETIME_ZERO, m_tDuration);
    }
    return (t + m_tOffset) * m_flScale;
}


//-----------------------------------------------------------------------------
// CDmeChannelsClip - timeframe view into a set of channels
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeChannelsClip, CDmeChannelsClip);

void CDmeChannelsClip::OnConstruction() {
    m_Channels.Init(this, "channels");
}

void CDmeChannelsClip::OnDestruction() {
}

void CDmeChannelsClip::BakeTimeScale(float scale /*= 1.0f*/) {
    float flNewScale = scale * GetTimeScale();

    int nChannels = m_Channels.size();
    for (int i = 0; i < nChannels; ++i) {
        CDmeChannel *pChannel = m_Channels[i];
        if (!pChannel)
            continue;
        pChannel->ScaleSampleTimes(1.0f / flNewScale);
    }

    BaseClass::BakeTimeScale(scale);
}





//-----------------------------------------------------------------------------
// Purpose: Find the channel within targeting the specified element and
// attribute.
//-----------------------------------------------------------------------------
CDmeChannel *FindChannelTargetingElement(CDmElement *pElement, const char *pAttributeName) {
    std::vector<CDmeChannel *> channels(8);
    FindAncestorsReferencingElement(pElement, channels);

    int nChannels = channels.size();
    for (int i = 0; i < nChannels; ++i) {
        CDmeChannel *pChannel = channels[i];
        if (!pChannel)
            continue;

        CDmElement *toElement = pChannel->GetToElement();
        if (toElement != pElement)
            continue;

        if (pAttributeName && (Q_stricmp(pChannel->GetToAttribute()->GetName(), pAttributeName) != 0))
            continue;

        return pChannel;
    }

    return nullptr;
}



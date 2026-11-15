//===== Copyright (c) 1996-2009, Valve Corporation, All rights reserved. ====
//
// Animation/Sequence events
//
//==========================================================================


// Valve includes
#include "mdlobjects/dmemotioncontrol.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "studio.h"


//-----------------------------------------------------------------------------
// Expose this class to the scene database
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY(DmeMotionControl, CDmeMotionControl);


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMotionControl::OnConstruction() {
    m_bX.Init(this, "X");
    m_bY.Init(this, "Y");
    m_bZ.Init(this, "Z");
    m_bXR.Init(this, "XR");
    m_bYR.Init(this, "YR");
    m_bZR.Init(this, "ZR");
    m_bLX.Init(this, "LX");
    m_bLY.Init(this, "LY");
    m_bLZ.Init(this, "LZ");
    m_bLXR.Init(this, "LXR");
    m_bLYR.Init(this, "LYR");
    m_bLZR.Init(this, "LZR");
    m_bLM.Init(this, "LM");
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CDmeMotionControl::OnDestruction() {
}



//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
StudioMotionFlags CDmeMotionControl::GetStudioMotionControl() const {
    return (m_bX ? StudioMotionFlags::X : StudioMotionFlags::None) |
           (m_bY ? StudioMotionFlags::Y : StudioMotionFlags::None) |
           (m_bZ ? StudioMotionFlags::Z : StudioMotionFlags::None) |
           (m_bXR ? StudioMotionFlags::XR : StudioMotionFlags::None) |
           (m_bYR ? StudioMotionFlags::YR : StudioMotionFlags::None) |
           (m_bZR ? StudioMotionFlags::ZR : StudioMotionFlags::None) |
           (m_bLX ? StudioMotionFlags::LX : StudioMotionFlags::None) |
           (m_bLY ? StudioMotionFlags::LY : StudioMotionFlags::None) |
           (m_bLZ ? StudioMotionFlags::LZ : StudioMotionFlags::None) |
           (m_bLXR ? StudioMotionFlags::LXR : StudioMotionFlags::None) |
           (m_bLYR ? StudioMotionFlags::LYR : StudioMotionFlags::None) |
           (m_bLZR ? StudioMotionFlags::LZR : StudioMotionFlags::None) |
           (m_bLM ? StudioMotionFlags::Linear : StudioMotionFlags::None);
}

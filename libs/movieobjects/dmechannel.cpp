//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "movieobjects/dmechannel.h"

#include "tier1/keyvalues.h"




// Paste data utility function declarations
KeyValues *FindLayerInPasteData( const CUtlVector< KeyValues * > &list, CDmeLog *log );
static int FindSpanningLayerAndSetIntensity( DmeLog_TimeSelection_t &ts, LayerSelectionData_t *data );
void CopyPasteData( CUtlVector< KeyValues * > &dstList, const CUtlVector< KeyValues * > &srcList );
void DestroyPasteData( CUtlVector< KeyValues * > &list );



//-----------------------------------------------------------------------------
//
// CRecordingLayer
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Default constructor
//-----------------------------------------------------------------------------
CRecordingLayer::CRecordingLayer() 
	: m_pPresetValuesDict( nullptr )
	, m_tHeadShotTime( DMETIME_INVALID )
	, m_ProceduralType( 0 )
	, m_OperationFlags( 0 )
	, m_RandomSeed( 0 )
	, m_flThreshold( 0 ) 
	, m_flIntensity( 0 )
	, m_RecordingMode( RECORD_PRESET )
	, m_pfnAddChannelCallback(nullptr)
    , m_pfnFinishChannelCallback(nullptr)
{

}

//-----------------------------------------------------------------------------
// Destructor, releases any log layer data that may be held by the recording layer.
//-----------------------------------------------------------------------------
CRecordingLayer::~CRecordingLayer()
{
	int nChannels = m_LayerChannels.Count();
	for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
	{
		LayerChannelInfo_t &info = m_LayerChannels[ iChannel ];		
		g_pDataModel->DestroyElement( info.m_hRawDataLayer );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Determine if the specified channel is in the list of modify 
// channels, regardless of component flags.
//-----------------------------------------------------------------------------
bool ModifyChannel::IsChannelInList( const CUtlVector< ModifyChannel > &modifyList, CDmeChannel *pChannel )
{
	int nChannels = modifyList.Count();
	for ( int i = 0; i < nChannels; ++i )	
	{
		if ( modifyList[ i ].m_pChannel == pChannel )
		{
			return true;
		}
	}
	return false;
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
: m_bVisible( true )
{

}


//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
CDmeChannelModificationLayer::~CDmeChannelModificationLayer()
{

}


//-----------------------------------------------------------------------------
// Purpose: Add a channel to the modification layer if it is not already 
// present, adding the channel to the modification layer causes a new log layer 
// to be added to the channel to which modifications will be made.
// Input  : pChannel - Pointer to the channel to be added.
// Output : Returns the index of the location in the modification layer's array
//			of channels where the information for the channel was stored.
//-----------------------------------------------------------------------------
int CDmeChannelModificationLayer::AddChannel( CDmeChannel *pChannel )
{
	// Make sure the channel is valid and that is has a valid log.
	Assert( pChannel );
	if ( pChannel == nullptr)
		return -1;

	CDmeLog* pLog = pChannel->GetLog();
	if ( pLog == nullptr)
		return -1; 
	
	// Check to see if the channel is already in the modification layer,
	// if so just update the reference count of the channel and return.
	int nChannels = m_ActiveChannels.Count();
	int availableSlot = -1;

	for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
	{
		if ( m_ActiveChannels[ iChannel ].m_Channel == pChannel->GetHandle() )
		{
			++m_ActiveChannels[ iChannel ].m_RefCount;
			return iChannel;
		}

		if ( ( availableSlot < 0 ) && ( m_ActiveChannels[ iChannel ].m_Channel.Get() == nullptr) )
		{
			Assert( m_ActiveChannels[ iChannel ].m_RefCount == 0 );
			availableSlot = iChannel;
		}
	}

	// Add the new channel to the modification layer's list in the
	// available slot or at the end if there is not an available slot.
	if ( availableSlot < 0 )
	{
		availableSlot = m_ActiveChannels.AddToTail();
	}
	m_ActiveChannels[ availableSlot ].m_Channel = pChannel->GetHandle();
	m_ActiveChannels[ availableSlot ].m_RefCount = 1;

	
	// Store the index of the current topmost layer, this is the layer which will be copied.
	int sourceLayerIndex = pLog->GetTopmostLayer();

	// If there is only one layer in the log add a new log layer, this is the layer upon which modifications
	// within the modification layer will be applied to until the modification layer is completed.
	if ( sourceLayerIndex == 0 )
	{		
		pLog->GetLayer( sourceLayerIndex );

		CDmeLogLayer *pLayer = pLog->AddNewLayer();

		if ( ( pLayer ) && ( sourceLayerIndex >= 0 ) )
		{
			CDmeLogLayer *pSrcLayer = pLog->GetLayer( sourceLayerIndex );

			if ( pSrcLayer )
			{
				pLayer->CopyLayer( pSrcLayer );
			}

			// The modification layer is always considered infinite so
			// as long as it is active it will be used at any time.
			pLayer->SetInfinite( true, true );			
		}
	}
	
	// Return the index of the location in the array where the channel was added.
	return availableSlot;
}


//-----------------------------------------------------------------------------
// Purpose: Flatten the log layers of all of the active channels onto the base 
// layer.
// Input  : bSaveChanges - Flag indicating if the changes recorded on the 
//			modification layer should be applied to the base layer ( true ) or
//			completely discarded ( false ).
// Input  :	bFlattenLayers - Flag indicating if the modification layers of the
//			logs should be flattened onto the base layer of the logs. Ignored 
//			if bSaveChanges is false.
//-----------------------------------------------------------------------------
void CDmeChannelModificationLayer::Finish( bool bSaveChanges, bool bFlattenLayers, bool bRunChannelCallbacks )
{
	int nChannels = m_ActiveChannels.Count();

	if ( ( bSaveChanges == false ) && ( bFlattenLayers == true ) )
	{
		// If not saving the changes just remove all by the layers from the log except the base layer.
		for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
		{
			CDmeChannel *pChannel = m_ActiveChannels[ iChannel ].m_Channel.Get();

			if ( pChannel )
			{
				CDmeLog *pLog = pChannel->GetLog();

				if ( pLog )
				{
					while ( pLog->GetNumLayers() > 1 )
					{
						pLog->RemoveLayerFromTail();
					}
				}
			}
		}
	}
	else if ( bFlattenLayers )
	{
		// If requested flatten the logs of the active channels. This flattens the logs completely,
		// so if there are any layers above the modification layer they will be flattened too.
		for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
		{
			CDmeChannel *pChannel = m_ActiveChannels[ iChannel ].m_Channel.Get();

			if ( pChannel )
			{
				CDmeLog *pLog = pChannel->GetLog();

				if ( pLog )
				{
					pLog->FlattenLayers( 0.0f, 0 );
				}
			}
		}
	}

	if ( bRunChannelCallbacks )
	{		
		int nLayers = m_RecordingLayerStack.Count();
		for ( int iLayer = 0; iLayer < nLayers; ++iLayer )
		{
			CDmeChannelRecordingMgr::RunFinishCallbacksOnRecordingLayer( &m_RecordingLayerStack[ iLayer ] );
		}
	}

	// Remove all recording layers
	m_RecordingLayerStack.Purge();

	// Remove all channels
	m_ActiveChannels.Purge();
}


//-----------------------------------------------------------------------------
// Purpose: Add a recording layer to the modification layer.
// Output : Returns a pointer to the recording layer which was added.
//-----------------------------------------------------------------------------
CRecordingLayer* CDmeChannelModificationLayer::AddRecordingLayer()
{
	int index = m_RecordingLayerStack.AddToTail();
	return &m_RecordingLayerStack[ index ];
}


//-----------------------------------------------------------------------------
// Purpose: Remove the last recording layer from the modification layer.
//-----------------------------------------------------------------------------
void CDmeChannelModificationLayer::RemoveLastRecordingLayer()
{
	// Iterate through the channels in the recording layer and update the reference count of the
	// channel in the modification layer. If the channel is no longer referenced by any of the 
	// recording layers in the modification layer it will be removed from the modification layer.
	CRecordingLayer &recLayer = m_RecordingLayerStack.Tail();

	int nChannels = recLayer.m_LayerChannels.Count();

	for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
	{
		int index = recLayer.m_LayerChannels[ iChannel ].m_ModLayerIndex;
		Assert( index >= 0 );
		Assert( index < m_ActiveChannels.Count() );

		if ( index >= 0 && index < m_ActiveChannels.Count() )
		{
			--m_ActiveChannels[ index ].m_RefCount;

			if ( m_ActiveChannels[ index ].m_RefCount < 1 )
			{
				Assert( m_ActiveChannels[ index ].m_RefCount == 0 );
				m_ActiveChannels[ index ].m_Channel = nullptr;
			}
		}
	}	

	m_RecordingLayerStack.RemoveMultipleFromTail( 1 );
}


//-----------------------------------------------------------------------------
// Purpose: Restore the modification log layer of each of the channels in the
// modification layer to their original state before any recording layers were
// applied by copying the base layer.
//-----------------------------------------------------------------------------
void CDmeChannelModificationLayer::WipeChannelModifications()
{
	int nChannels = m_ActiveChannels.Count();

	for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
	{
		CDmeChannel *pChannel = m_ActiveChannels[ iChannel ].m_Channel.Get();

		if ( pChannel )
		{
			// Get the log from the channel
			CDmeLog *pLog = pChannel->GetLog();
			if ( pLog == nullptr)
				continue;
				
			// Get the number of layers, it must be at least two,
			// the base layer and the modification layer.
			int numLayers = pLog->GetNumLayers();
			if ( numLayers < 2 )
			{
				Assert( numLayers >= 2 );
				continue;
			}

			// Get the modification layer and the base layer and then copy the base layer into the
			// modification layer, overwriting anything that is currently in the modification layer.
			CDmeLogLayer *pModLayer = pLog->GetLayer( numLayers - 1 );
			CDmeLogLayer *pBaseLayer = pLog->GetLayer( numLayers - 2 );
			
			pModLayer->CopyLayer( pBaseLayer );
			
			// Modification layer is always infinite, while 
			// it is active no layers beneath it matter.
			pModLayer->SetInfinite( true, true );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Get the number of recording layers in the modification layer
//-----------------------------------------------------------------------------
int CDmeChannelModificationLayer::NumRecordingLayers() const
{
	return m_RecordingLayerStack.Count();	
}


//-----------------------------------------------------------------------------
// Purpose: Get a reference to the specified recording layer
//-----------------------------------------------------------------------------
CRecordingLayer &CDmeChannelModificationLayer::GetRecordingLayer( int index )
{
	return m_RecordingLayerStack[ index ];
}


//-----------------------------------------------------------------------------
// Purpose: Return if the modification layer visible to the user
//-----------------------------------------------------------------------------
bool CDmeChannelModificationLayer::IsVisible() const
{
	return m_bVisible;
}


//-----------------------------------------------------------------------------
// Apply the specified transform write mode all of the active channels in each 
// of the recording layers
//-----------------------------------------------------------------------------
void CDmeChannelModificationLayer::UpdateTransformWriteMode( TransformWriteMode_t mode )
{
	int nLayers = m_RecordingLayerStack.Count();

	for ( int iLayer = 0; iLayer < nLayers; ++iLayer )
	{
		CRecordingLayer &layer = m_RecordingLayerStack[ iLayer ];
		int nChannels = layer.m_LayerChannels.Count();
		for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
		{
			LayerChannelInfo_t &channelInfo = layer.m_LayerChannels[ iChannel ];
			channelInfo.m_TransformWriteMode = mode;
		}
	}
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
CDmeChannelRecordingMgr *g_pChannelRecordingMgr = &s_ChannelRecordingMgr;


//-----------------------------------------------------------------------------
// Constructor 
//-----------------------------------------------------------------------------
CDmeChannelRecordingMgr::CDmeChannelRecordingMgr()
{
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
// Purpose: Start a new recording layer, may not be done when there is already
// and active recording layer.
// Input  : pPresetValuesDict - Preset values for the recording layer.
// Input  : operationType - Specification of the general operation which the
//			recording layer will perform.
// Input  : proceduralType - If the recording operation is a procedural
//			operation this may specify the specific type of procedural operation.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::StartLayerRecording( AttributeDict_t *pPresetValuesDict /*= NULL*/, DmeTime_t tHeadShotTime /*= DMETIME_INVALID*/, int proceduralType /*=PROCEDURAL_PRESET_NOT*/, int nFlags /* = 0 */, FnRecordChannelCallback pfnAddChannel /*= NULL*/, FnRecordChannelCallback pfnFinishChannel /*= NULL*/ )
{
	Assert( m_pActiveRecordingLayer == NULL );

	if ( m_pModificationLayer )
	{
		m_pActiveRecordingLayer = m_pModificationLayer->AddRecordingLayer();
	}
	else
	{
		m_pActiveRecordingLayer = new CRecordingLayer;
	}

	m_pActiveRecordingLayer->m_pPresetValuesDict = CopyAttributeDict( pPresetValuesDict );
	m_pActiveRecordingLayer->m_tHeadShotTime = tHeadShotTime;
	m_pActiveRecordingLayer->m_ProceduralType = proceduralType;
	m_pActiveRecordingLayer->m_OperationFlags = nFlags;
	m_pActiveRecordingLayer->m_pfnAddChannelCallback = pfnAddChannel;
	m_pActiveRecordingLayer->m_pfnFinishChannelCallback = pfnFinishChannel;
	m_pActiveRecordingLayer->m_OriginalTimes = m_TimeSelection.m_nTimes;
	m_pActiveRecordingLayer->m_BaseTimes = m_TimeSelection.m_nTimes;
}

//-----------------------------------------------------------------------------
// Purpose: Complete the current recording layer, there must be an active 
// recording layer.
// Input  : flThreshold - threshold value used in flattening the log layers from
//			the recording layer on to their proceeding log layers.
// Input  : bFlattenLayers - Flag indicating if the log layers resulting from
//			the record operation should be flattened on to the proceeding layer.
// Input  : bAllowFinishModification - Flag indicating if the recording layer is
//			allowed to finish the modification layer. Most recording operations
//			will not finish the modification layer, but some such as the drop 
//			operation will finish the modification layer if this flag is true.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::FinishLayerRecording( float flThreshhold, bool bFlattenLayers /*=true*/, bool bAllowFinishModification /*= true*/ )
{
	Assert( m_pActiveRecordingLayer );

	bool finishModification = false;
		
	if ( m_pActiveRecordingLayer )
	{
		m_pActiveRecordingLayer->m_flThreshold = flThreshhold;
		m_pActiveRecordingLayer->m_flIntensity = m_TimeSelection.m_flIntensity;
		m_pActiveRecordingLayer->m_RecordingMode = m_TimeSelection.GetRecordingMode();
		m_pActiveRecordingLayer->m_RandomSeed = m_RandomSeed;

		if ( m_pActiveRecordingLayer->m_ProceduralType == PROCEDURAL_PRESET_NOT )
		{
			m_pActiveRecordingLayer->m_ProceduralType = GetProceduralType();
		}

		// Set the flag to for the modification layer to be completed if the operation is to drop the layer.
		if ( bAllowFinishModification && ( m_pActiveRecordingLayer->m_ProceduralType == PROCEDURAL_PRESET_DROP_LAYER ) )
		{
			finishModification = true;
		}

		// If time is advancing add an additional layer which will hold 
		// the original recorded values not filtered by the time selection.
		if ( IsTimeAdvancing() && IsModificationLayerActive() )
		{
			int c = m_pActiveRecordingLayer->m_LayerChannels.Count();
			for ( int i = 0 ; i < c; ++i )
			{
				LayerChannelInfo_t &channelInfo = m_pActiveRecordingLayer->m_LayerChannels[ i ];

				if ( channelInfo.m_hRawDataLayer.Get() == nullptr)
				{
					CDmeChannel *pChannel = channelInfo.m_Channel.Get();
					if ( !pChannel )
						continue;

					CDmeLog *pLog = pChannel->GetLog();
					Assert( pLog );
					if ( !pLog )
						continue;

					CDmeLogLayer *pTopLayer = pLog->GetLayer( pLog->GetTopmostLayer() );
					channelInfo.m_hRawDataLayer = pLog->AddNewLayer();	
					channelInfo.m_hRawDataLayer->CopyLayer( pTopLayer );
					pLog->RemoveLayerFromTail();
				}
			}
		}

		// Finalize the recording for all channels in the layer and detach the channels from the 
		// recording layer. The channels will no longer know where they are within the recording
		// layer, but the recording layer will retain the list of channels it operated on.
		RemoveAllChannelsFromRecordingLayer( m_pActiveRecordingLayer, false );

		if ( bFlattenLayers )
		{
			FlattenLayers( m_pActiveRecordingLayer );
		}
				
		// If there is no modification layer then the recording layer was allocated and should be destroyed.
		if ( m_pModificationLayer == nullptr)
		{
			RunFinishCallbacksOnRecordingLayer( m_pActiveRecordingLayer );
			delete m_pActiveRecordingLayer;
		}

	}

	m_pActiveRecordingLayer = nullptr;
	m_nProceduralType = PROCEDURAL_PRESET_NOT;
	m_pRevealTarget = nullptr;
	m_PasteTarget.RemoveAll();

	
	// If the finish modification layer flag was set, finish the modification layer. Some
	// recording operations such as the drop preset force the completion of the layer.
	if ( finishModification )	
	{
		FinishModificationLayer();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Cancel the current recording layer, losing all of its changes.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::CancelLayerRecording()
{
	if ( m_pActiveRecordingLayer )
	{	
		RunFinishCallbacksOnRecordingLayer( m_pActiveRecordingLayer );

		// Detach all of the channels from the recording layer and and delete the associate log layers.
		RemoveAllChannelsFromRecordingLayer( m_pActiveRecordingLayer, true );

		if ( m_pModificationLayer )
		{	
			m_pActiveRecordingLayer = nullptr;

			// If the modification layer is now empty, destroy it completely.
			if ( m_pModificationLayer->NumRecordingLayers() < 2 )
			{
				FinishModificationLayer( false );
			}
			else
			{		
				// Remove the recording layer from the modification layer
				m_pModificationLayer->RemoveLastRecordingLayer();
			}
		}
		else
		{
			delete m_pActiveRecordingLayer;
			m_pActiveRecordingLayer = nullptr;	
		}
		
		m_nProceduralType = PROCEDURAL_PRESET_NOT;
		m_pRevealTarget = nullptr;
		m_PasteTarget.RemoveAll();
	}
}


//-----------------------------------------------------------------------------
// Purpose:  Creates a modification layer on which all further editing will 
// take place until EndModificationLayer() is called. If there is already an
// active modification layer then the time selection of the active modification
// layer will be updated, but a new modification layer will not be started.
// Input  : pTimeSelection - pointer to the active time selection which the 
//			modification layer is to apply to.
// Input  : createLayer - flag indicating if a modification layer should be 
//			created, if false the function may be used to update the current
//			time selection.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::StartModificationLayer( const DmeLog_TimeSelection_t *pTimeSelection, bool createLayer /*= true*/ )
{
	//  If the modification layer has already been started just update the time parameters.
	if ( m_pModificationLayer != nullptr)
	{
		if ( pTimeSelection )
		{
			m_TimeSelection = *pTimeSelection;
			m_TimeSelection.ResetTimeAdvancing();
		}
		return;
	}

	// Create the modification layer unless create flag is false.
	if ( createLayer && m_bModificationLayerEnabled )
	{
		m_pModificationLayer = new CDmeChannelModificationLayer();
	}

	// Set the time selection to provided time selection or reset the time selection
	// to the default values and disable its use if no time selection was provided.
	if ( pTimeSelection )
	{
		m_TimeSelection = *pTimeSelection;
		m_bUseTimeSelection = true;
	}
	else
	{
		m_TimeSelection = DmeLog_TimeSelection_t();
		m_bUseTimeSelection = false;
	}
	m_TimeSelection.ResetTimeAdvancing();
}


//-----------------------------------------------------------------------------
// Purpose: Complete the current modification layer, applying the modifications
// to the base log layers of the effected channels and destroying all of the 
// recording layer data contained within the modification layer.
// Input  : bSaveChanges - flag indicating if the changes within the 
//			modification layer should be applied or discarded.
// Input  : bFlattenLayers - flag indicating if the log layers of the 
//			modification should be flattened onto the base layer.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::FinishModificationLayer( bool bSaveChanges, bool bFlattenLayers )
{
	// Should never call finish modification layer while there is an active 
	// recording layer. FinishLayerRecording() should be called first.
	Assert( m_pActiveRecordingLayer == NULL );

	if ( m_pActiveRecordingLayer != nullptr)
	{
		return;
	}

	// If the modification layer has already been finished
	// is is already waiting to finish, ignore the request.
	if ( IsModificationLayerActive() == false )
	{
		m_bUseTimeSelection = false;
		m_TimeSelection.ResetTimeAdvancing();
		return;
	}

	// Make sure the modification layer is not dirty. If it is set the flag which specifies the
	// modification layer should be finished once the processing is complete. If we are not 
	// going to save the changes ignore the fact that the layer is dirty and just finish it.
	if ( m_bModificationLayerDirty && bSaveChanges )
	{
		// If this assert is hit finish modification layer has been called twice without the
		// modification layer being processed or the flags have not been properly cleared.
		Assert( m_bWantsToFinish == false );
		m_bWantsToFinish = true;
		m_bFinishFlattenLayers = bFlattenLayers;
	}
	else
	{
		// Destroy the modification layer and the recording layers it contains.
		if ( m_pModificationLayer )
		{
			// Complete the modification layer and flatten the channel log layers if requested.
			m_pModificationLayer->Finish( bSaveChanges, bFlattenLayers, true );

			delete m_pModificationLayer;
			m_pModificationLayer = nullptr;
		}

		// Reset the time selection information
		m_bUseTimeSelection = false;
		m_TimeSelection.ResetTimeAdvancing();

		// Clear the finish flags
		m_bWantsToFinish = false;
		m_bFinishFlattenLayers = false;
		m_bModificationLayerDirty = false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Enable or disable use of the modification layer.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::EnableModificationLayer( bool enable )
{
	if ( enable )	
	{
		m_bModificationLayerEnabled = true;
	}
	else
	{
		// Finish any active modification layer before disabling.
		FinishModificationLayer();
		m_bModificationLayerEnabled = false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Get the current time selection
// Output : Returns a reference to the current time selection structure
//-----------------------------------------------------------------------------
const DmeLog_TimeSelection_t &CDmeChannelRecordingMgr::GetTimeSelection() const
{
	return m_TimeSelection;
}


//-----------------------------------------------------------------------------
// Purpose: Set the time selection for the modification layer and re-apply all
// recording layers that are on the current modification layer with the new  
// time selection. Must be done with active modification layer, but without an
// active recording layer.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::SetTimeSelection( const DmeLog_TimeSelection_t &newTS, bool bUpdateBaseTimes )
{
	// Can only set the time selection with the modification layer active but without any active recording layers.
	if ( ( m_pModificationLayer == nullptr) || ( m_pActiveRecordingLayer != nullptr) )
	{
		Assert( m_pModificationLayer != NULL );
		Assert( m_pActiveRecordingLayer == NULL );
		return;
	}


	int nNumRecordingLayers = m_pModificationLayer->NumRecordingLayers();
		
	for ( int iRecordingLayer = 0; iRecordingLayer < nNumRecordingLayers; ++iRecordingLayer )
	{	
		CRecordingLayer &recordingLayer = m_pModificationLayer->GetRecordingLayer( iRecordingLayer );
		TimeSelection_t newBaseTimes = recordingLayer.m_BaseTimes;
		
		Assert( newBaseTimes[ TS_LEFT_HOLD ] >= newBaseTimes[ TS_LEFT_FALLOFF ] );
		Assert( newBaseTimes[ TS_RIGHT_HOLD ] >= newBaseTimes[ TS_LEFT_HOLD ] );
		Assert( newBaseTimes[ TS_RIGHT_FALLOFF ] >= newBaseTimes[ TS_RIGHT_HOLD ] );

		// Update the base times by the same amount the time selection is to be changed.
		if ( bUpdateBaseTimes )
		{		
			// As long as the base times are being updated, the ratio of the duration of each section of the base time selection compared to 
			// the visible time selection should remain the same. Additionally movements of the hold region should operate such the source sample
			// data appears static. This means for a change applied to the visible hold region, the change applied to the base time selection 
			// needs to be multiplied by the hold region ration. Although it would be desirable for the falloff regions to behave this way, if
			// both the falloff time and the hold times are being modified this is not possible, so the hold region takes precedence.
			DmeTime_t baseLeftTime = newBaseTimes[ TS_LEFT_HOLD ] - newBaseTimes[ TS_LEFT_FALLOFF ];
			DmeTime_t baseHoldTime = newBaseTimes[ TS_RIGHT_HOLD ] - newBaseTimes[ TS_LEFT_HOLD ];
			DmeTime_t baseRightTime = newBaseTimes[ TS_RIGHT_FALLOFF ] - newBaseTimes[ TS_RIGHT_HOLD ];

			DmeTime_t leftTime = m_TimeSelection.m_nTimes[ TS_LEFT_HOLD ] - m_TimeSelection.m_nTimes[ TS_LEFT_FALLOFF ];
			DmeTime_t holdTime = m_TimeSelection.m_nTimes[ TS_RIGHT_HOLD ] - m_TimeSelection.m_nTimes[ TS_LEFT_HOLD ];
			DmeTime_t rightTime = m_TimeSelection.m_nTimes[ TS_RIGHT_FALLOFF ] - m_TimeSelection.m_nTimes[ TS_RIGHT_HOLD ];

			float flLeftRatio = ( ( leftTime > DMETIME_ZERO ) && ( baseLeftTime > DMETIME_ZERO ) ) ? ( baseLeftTime / leftTime ) : 1.0f;
			float flHoldRatio = ( ( holdTime > DMETIME_ZERO ) && ( baseHoldTime > DMETIME_ZERO ) ) ? ( baseHoldTime / holdTime ) : 1.0f;
			float flRightRatio = ( ( rightTime > DMETIME_ZERO ) && ( baseRightTime > DMETIME_ZERO ) ) ? ( baseRightTime / rightTime ) : 1.0f;

			// Apply the ratio adjusted delta time to the left and right hold times
			DmeTime_t leftHoldDT = newTS.m_nTimes[ TS_LEFT_HOLD ] - m_TimeSelection.m_nTimes[ TS_LEFT_HOLD ];
			DmeTime_t rightHoldDT = newTS.m_nTimes[ TS_RIGHT_HOLD ] - m_TimeSelection.m_nTimes[ TS_RIGHT_HOLD ];
			newBaseTimes[ TS_LEFT_HOLD ] += ( leftHoldDT * flHoldRatio );
			newBaseTimes[ TS_RIGHT_HOLD ] += ( rightHoldDT * flHoldRatio );
		
			// Update the falloff times so that the ratios are preserved.
			newBaseTimes[ TS_LEFT_FALLOFF ] = newBaseTimes[ TS_LEFT_HOLD ] - ( ( newTS.m_nTimes[ TS_LEFT_HOLD ] - newTS.m_nTimes[ TS_LEFT_FALLOFF ] ) * flLeftRatio );
			newBaseTimes[ TS_RIGHT_FALLOFF ] = newBaseTimes[ TS_RIGHT_HOLD ] + ( ( newTS.m_nTimes[ TS_RIGHT_FALLOFF ] - newTS.m_nTimes[ TS_RIGHT_HOLD ] ) * flRightRatio );
			
			// Make sure the time selection times remain in order.
			newBaseTimes[ TS_LEFT_FALLOFF ] = MIN( newBaseTimes[ TS_LEFT_FALLOFF ], newBaseTimes[ TS_LEFT_HOLD ] - DMETIME_MINDELTA );
			newBaseTimes[ TS_RIGHT_HOLD ] = MAX( newBaseTimes[ TS_RIGHT_HOLD ], newBaseTimes[ TS_LEFT_HOLD ] );
			newBaseTimes[ TS_RIGHT_FALLOFF ] = MAX( newBaseTimes[ TS_RIGHT_FALLOFF ], newBaseTimes[ TS_RIGHT_HOLD ] + DMETIME_MINDELTA );
		}
		
		recordingLayer.m_BaseTimes = newBaseTimes;
	}

	// Set the new time selection for which the effects of the recording layers are to be applied.
	m_TimeSelection.m_nTimes = newTS.m_nTimes;
	m_TimeSelection.m_nFalloffInterpolatorTypes[ 0 ] = newTS.m_nFalloffInterpolatorTypes[ 0 ];
	m_TimeSelection.m_nFalloffInterpolatorTypes[ 1 ] = newTS.m_nFalloffInterpolatorTypes[ 1 ];
	m_TimeSelection.m_bInfinite[ 0 ] = newTS.m_bInfinite[ 0 ];
	m_TimeSelection.m_bInfinite[ 1 ] = newTS.m_bInfinite[ 1 ];

	m_bUseTimeSelection = true;
	m_bModificationLayerDirty = true;
}


//-----------------------------------------------------------------------------
// Purpose: Start or continue processing of the recording layers within the 
// modification layer.
// In/Out : recordingLayer - Input with the index of the recording layer to 
//			process, returns the index of the next recording layer to be 
//			processed.
// Output : proceduralType - returns the specific procedural preset type for 
//			procedural recording operations.
// Output : operationFlags - returns flags providing information about the 
//			operation of the recording layer.
// Output : randomSeed - returns the random seed used by the recording layer 
//			for procedural operations which depend on random values.
// Output : flIntensity - returns the intensity parameter of the recording layer
//			which was processed.
// Output : Returns true if recording layer index is valid and the recording
//			layer was processed, returns false if the recording layer index was
//			out of range, which occurs when processing of all layers has been
//			completed.
//-----------------------------------------------------------------------------
CRecordingLayer *CDmeChannelRecordingMgr::ProcessModificationLayer( int &recordingLayer )
{
	// Do nothing if the modification layer does not require an update.
	if ( !m_bModificationLayerDirty )
		return nullptr;

	// Obviously the modification layer must exist in order to process it.
	if ( m_pModificationLayer == nullptr)
	{
		Assert( m_pModificationLayer );
		return nullptr;
	}

	// If all of the recording layers have been processed clear the dirty
	// flag and return false to indicate no further processing is required.
	if ( recordingLayer >= m_pModificationLayer->NumRecordingLayers() )
	{
		m_bModificationLayerDirty = false;
		m_bModificationProcessing = false;

		// If the flag specifying that the modification layer should be finished is set, call 
		// the finish function to flatten the layers and complete the modification layer.
		if ( m_bWantsToFinish )
		{
			m_bWantsToFinish = false;
			FinishModificationLayer( m_bFinishFlattenLayers );
		}

		return nullptr;
	}


	// Perform initialization actions
	if ( recordingLayer == 0 )
	{
		// Set the flag to indicate that the modification layer is currently being processed.
		m_bModificationProcessing = true;

		// Restore the modification log layer of each of the channels in the modification layer to their
		// original state before any recording layers were applied by copying the base layer.
		m_pModificationLayer->WipeChannelModifications();		
	}

	// Get the current recording layer and perform the initial update for applying it to the modification layer.
	CRecordingLayer &recLayer = m_pModificationLayer->GetRecordingLayer( recordingLayer++ );
	ApplyRecordingLayer( recLayer );

	return &recLayer;
}


//-----------------------------------------------------------------------------
// Purpose: Complete the modification layer processing of the current recording 
// layer.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::CompleteModificationProcessing()
{
	Assert( m_pActiveRecordingLayer );

	// Record the channel operations if the operation mode requires it. Paste does not
	// as the values are applied to the channel outside of the record mechanism.
	if ( m_pActiveRecordingLayer->m_ProceduralType != PROCEDURAL_PRESET_PASTE )
	{
		int c = m_pActiveRecordingLayer->m_LayerChannels.Count();

		for ( int i = 0 ; i < c; ++i )
		{
			LayerChannelInfo_t &channelInfo = m_pActiveRecordingLayer->m_LayerChannels[ i ];

			CDmeChannel *pChannel = channelInfo.m_Channel.Get();

			if ( pChannel )
			{
				pChannel->Operate();
			}
		}
	}

	// Finish the record operation by detaching the channels from the record 
	// layer, returning them to play mode, and flattening the newly created layer.
	RemoveAllChannelsFromRecordingLayer( m_pActiveRecordingLayer, false );
	FlattenLayers( m_pActiveRecordingLayer );

	// Reset the active recording layer to NULL since the recording
	// layer being applied is not actually being recorded.
	m_pActiveRecordingLayer = nullptr;
	m_nProceduralType = PROCEDURAL_PRESET_NOT;
	m_pRevealTarget = nullptr;
	m_PasteTarget.RemoveAll();
}


//-----------------------------------------------------------------------------
// Purpose: Apply the effects of the recording layer to its channels with the
// active time selection.
// Input  : recordingLayer - Reference to the recording layer to apply.
// Output : Returns the procedural type of the recording layer.
//-----------------------------------------------------------------------------
bool CDmeChannelRecordingMgr::ApplyRecordingLayer( CRecordingLayer &recordingLayer )
{
	// An existing recording layer cannot be applied to the the modification
	// layer while there is currently an active recording layer.
	if ( m_pActiveRecordingLayer != nullptr)
	{
		Assert( m_pActiveRecordingLayer == NULL );
		return false;
	}

	// Temporarily set the recording layer being re-applied as the active recording layer, this
	// is done because the channel Operate() function assumes an active recording channel.
	m_pActiveRecordingLayer = &recordingLayer;
	m_nProceduralType = recordingLayer.m_ProceduralType;
	m_RandomSeed = recordingLayer.m_RandomSeed;
	m_TimeSelection.m_flIntensity = recordingLayer.m_flIntensity;
	m_TimeSelection.SetRecordingMode( recordingLayer.m_RecordingMode );

	// Iterate through the channels in the recording layer, add a new layer to 
	// the log of each channel and put the channel in recording mode so that 
	// the operation of the layer can be recorded into the log.
	int c = recordingLayer.m_LayerChannels.Count();

	for ( int i = 0 ; i < c; ++i )
	{
		LayerChannelInfo_t &channelInfo = recordingLayer.m_LayerChannels[ i ];

		CDmeChannel *pChannel = channelInfo.m_Channel.Get();
		if ( !pChannel )
			continue;

		CDmeLog *pLog = pChannel->GetLog();
		Assert( pLog );
		if ( !pLog )
			continue;


		// Add the new layer to the log and assign the channel
		// its location within the current recoding layer.
		CDmeLogLayer* pNewLayer = pLog->AddNewLayer();
		if ( pNewLayer == nullptr)
			continue;
		
		pNewLayer->SetInfinite( m_TimeSelection.m_bInfinite[ 0 ], m_TimeSelection.m_bInfinite[ 1 ] );
		pChannel->SetRecordLayerIndex( i );

		// If the recording operation was an attribute over time, use the stored recorded
		// log data to re-apply the recording operation for the current time frame.
		if ( channelInfo.m_hRawDataLayer.Get() )
		{
			DmeLog_TimeSelection_t localTimeSelection = m_TimeSelection;
			localTimeSelection.m_flIntensity = 1.0f;
			for ( int i = 0; i < TS_TIME_COUNT; ++i )
			{
				localTimeSelection.m_nTimes[i] = channelInfo.m_ClipStack.ToChildMediaTime( localTimeSelection.m_nTimes[i], false );
			}

			pLog->BlendLayersUsingTimeSelection( pLog->GetLayer( channelInfo.m_BaseLayer ), channelInfo.m_hRawDataLayer, pNewLayer, localTimeSelection, true, true, true, DMETIME_ZERO );

			// Do not put the channel into record mode.
			continue;
		}		


		if ( recordingLayer.m_ProceduralType == PROCEDURAL_PRESET_NOT )
		{
			// If the recording layer's operation was not a procedural operation, restore the value of 
			// the source attribute to the recording value before re-recording with the new time frame.
			CDmAttribute *pFromAttr = pChannel->GetFromAttribute();

			if ( pFromAttr )
			{
				channelInfo.m_FromAttrData.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );

				if ( IsArrayType( pFromAttr->GetType() ) )
				{
					pFromAttr->UnserializeElement( pChannel->GetFromArrayIndex(), channelInfo.m_FromAttrData );
				}
				else
				{
					pFromAttr->Unserialize( channelInfo.m_FromAttrData );
				}
			}

			CDmAttribute *pToAttr = pChannel->GetToAttribute();

			if ( pToAttr )
			{
				channelInfo.m_ToAttrData.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );

				if ( IsArrayType( pToAttr->GetType() ) )
				{
					pToAttr->UnserializeElement( pChannel->GetToArrayIndex(), channelInfo.m_ToAttrData );
				}
				else
				{
					pToAttr->Unserialize( channelInfo.m_ToAttrData );
				}
			}
		}

		// Record the operation.
		pChannel->SetMode( CM_RECORD );
	}

	return true;
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
void CDmeChannelRecordingMgr::StoreChannelAttributeData( int nChannelIndex, bool fromAttr )
{
	Assert( m_pActiveRecordingLayer );

	if ( m_pActiveRecordingLayer )
	{	
		LayerChannelInfo_t& info = m_pActiveRecordingLayer->m_LayerChannels[ nChannelIndex ];	
		CDmeChannel *pChannel = info.m_Channel.Get();

		// The data from the provided attribute may be stored in either the to or from attribute 
		// data for the channel, select the appropriate data buffer based on selection flag.
		CUtlBuffer *pBuffer = fromAttr ? &info.m_FromAttrData : &info.m_ToAttrData;
		CDmAttribute *pAttr = fromAttr ? pChannel->GetFromAttribute() : pChannel->GetToAttribute();

		if ( pAttr )
		{	
			// Clear the buffer, we only want to store a single value.
			pBuffer->Clear();

			// If storing the to attribute data, make sure it is current and has the 
			// actual value of the log, not the control default value if the log is empty.
			if ( !fromAttr )
			{
				pChannel->Play( true );
			}

			if ( IsArrayType( pAttr->GetType() ) )
			{
				int nArrayIndex = fromAttr ? pChannel->GetFromArrayIndex() : pChannel->GetToArrayIndex();
				pAttr->SerializeElement( nArrayIndex, *pBuffer );
			}
			else
			{
				pAttr->Serialize( *pBuffer );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Adds a channel to the recording layer
// Input  : pChannel - pointer to the channel to be added to the recording 
//			layer.
// Input  : componentFlags - flags specifying which components of the channel
//			are to be recorded.
// Input  : pRoot - pointer to the current root clip ( movie )
// Input  : pShot - pointer to the current shot 
// Output : Returns - Index of the location in the recording layer's array of 
//			channel information to which the new channel was assigned.
//-----------------------------------------------------------------------------
int CDmeChannelRecordingMgr::AddChannelToRecordingLayer( CDmeChannel *pChannel, LogComponents_t componentFlags, CDmeClip *pRoot, CDmeClip *pShot )
{
	Assert( pChannel );
	if ( pChannel == nullptr)
		return -1;

	// The channel must not already be in an active recording layer.
	Assert( pChannel->GetRecordLayerIndex() == -1 );

	// There must be an active recording layer before channels may be added.
	Assert( m_pActiveRecordingLayer );
	if ( m_pActiveRecordingLayer == nullptr) 
		return -1;

	CDmeLog *pLog = pChannel->GetLog();
	if ( !pLog )
		return -1;

	// If specified call the add channel callback before adding the channel to the recording layer.
	if ( m_pActiveRecordingLayer->m_pfnAddChannelCallback )
	{
		m_pActiveRecordingLayer->m_pfnAddChannelCallback( pChannel );
	}

	int nRecordLayerIndex = m_pActiveRecordingLayer->m_LayerChannels.AddToTail();
	LayerChannelInfo_t& info = m_pActiveRecordingLayer->m_LayerChannels[nRecordLayerIndex];
	info.m_Channel = pChannel;
	info.m_pRoot = pRoot;
	info.m_pShot = pShot;
	info.m_ComponentFlags = componentFlags;
	info.m_HeadPosition = pChannel->GetCurrentTime();
	info.m_TransformWriteMode = m_TransformWriteMode;
	info.m_bManipulateInFalloff = m_TimeSelection.m_bManipulateInFalloff;

	if ( pLog->IsEmpty() )
	{
		pLog->SetKey( pChannel->GetCurrentTime(), pChannel->GetFromAttribute(), pChannel->GetFromArrayIndex() );
	}

	// Add the channel to the modification layer, which results in a new layer being
	// added to the log on which subsequent modifications to the log will take place.
	if ( m_pModificationLayer )
	{
		info.m_ModLayerIndex = m_pModificationLayer->AddChannel( pChannel );
	}
	
	// Store the index of the current top most layer, this is the modification layer 
	// which was just added. When flatting the recording layer it will be flattened 
	// only to the modification layer, not all the way to the base layer of the log.
	info.m_BaseLayer = pLog->GetTopmostLayer();

	if ( pRoot )
	{
		if ( !pChannel->BuildClipStack( &info.m_ClipStack, pRoot, pShot ) )
		{
			m_pActiveRecordingLayer->m_LayerChannels.Remove( nRecordLayerIndex );
			return -1;
		}
	}

	// Store the index of the the channel in the recording layer for obtaining
	// access to the channel info in the recording layer from the channel.
	pChannel->SetRecordLayerIndex( nRecordLayerIndex );

	// Store the value of the To attribute before recording starts
	// to use as a reference value when re-applying the operation.
	StoreChannelAttributeData( nRecordLayerIndex, false );

	CDmeLogLayer *pNewLayer = pLog->AddNewLayer();
	if ( pNewLayer )
	{
		pNewLayer->SetInfinite( m_TimeSelection.m_bInfinite[ 0 ], m_TimeSelection.m_bInfinite[ 1 ] );
	}

	pChannel->SetMode( CM_RECORD );

	return nRecordLayerIndex;
}


//-----------------------------------------------------------------------------
// Purpose: Removes all channels from the recording layer
// Input  : pRecordingLayer - Pointer to the recording layer from which all the
//			channel information is to be removed.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::RemoveAllChannelsFromRecordingLayer( CRecordingLayer *pRecordingLayer, bool destroyLogLayers )
{
	Assert( pRecordingLayer );
	if ( pRecordingLayer == nullptr)
		return;

	int c = pRecordingLayer->m_LayerChannels.Count();
	for ( int i = 0 ; i < c; ++i )
	{
		CDmeChannel *pChannel = pRecordingLayer->m_LayerChannels[ i ].m_Channel.Get();
		if ( !pChannel )
			continue;

		CDmeLog *pLog = pChannel->GetLog();
		if ( pLog  )
		{
			if ( IsUsingTimeSelection() )
			{
				// Computes local times for the time selection
				DmeLog_TimeSelection_t timeSelection;
				GetLocalTimeSelection( timeSelection, pChannel->GetRecordLayerIndex() );
				pLog->FinishTimeSelection( pChannel->GetCurrentTime(), timeSelection );
			}
	
			// Release recording layer from the log.
			if ( destroyLogLayers )
			{
				pLog->RemoveLayerFromTail();
			}
		}
		pChannel->SetRecordLayerIndex( -1 );
		pChannel->SetMode( CM_PLAY );
	}
}


//-----------------------------------------------------------------------------
// Flattens recorded layers into the base layer
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::FlattenLayers( CRecordingLayer *pRecordingLayer )
{
	Assert( pRecordingLayer );
	if ( pRecordingLayer == nullptr)
		return;

	int nFlags = 0;
	if ( IsUsingDetachedTimeSelection() && IsTimeAdvancing() )
	{
		nFlags |= CDmeLog::FLATTEN_NODISCONTINUITY_FIXUP;
	}

	int c = pRecordingLayer->m_LayerChannels.Count();
	for ( int i = 0 ; i < c; ++i )
	{
		LayerChannelInfo_t &channelInfo = pRecordingLayer->m_LayerChannels[ i ];

		CDmeChannel *pChannel = channelInfo.m_Channel.Get();
		if ( !pChannel )
			continue;

		CDmeLog *pLog = pChannel->GetLog();
		Assert( pLog );
		if ( !pLog )
			continue;

		if ( channelInfo.m_BaseLayer < 0 )
			continue;

		pLog->FlattenLayers( pRecordingLayer->m_flThreshold, nFlags, channelInfo.m_BaseLayer );
	}
}


//-----------------------------------------------------------------------------
// Calls the callback for all channels in the specified recording layer
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::RunFinishCallbacksOnRecordingLayer( CRecordingLayer *pRecordingLayer )
{
	if ( pRecordingLayer == nullptr)
		return;

	if ( pRecordingLayer->m_pfnFinishChannelCallback == nullptr)
		return;

	int nNumChannels = pRecordingLayer->m_LayerChannels.Count();
	for ( int iChannel = 0; iChannel < nNumChannels; ++iChannel )
	{
		LayerChannelInfo_t &channelInfo = pRecordingLayer->m_LayerChannels[ iChannel ];
		CDmeChannel *pChannel = channelInfo.m_Channel.Get();
		if ( pChannel == nullptr)
			continue;
		
		pRecordingLayer->m_pfnFinishChannelCallback( pChannel );
	}
}


//-----------------------------------------------------------------------------
// Used to iterate over all channels currently being recorded
//-----------------------------------------------------------------------------
int CDmeChannelRecordingMgr::GetLayerRecordingChannelCount()
{
	if ( m_pActiveRecordingLayer )
	{
		return m_pActiveRecordingLayer->m_LayerChannels.Count();
	}

	return 0;
}

CDmeChannel* CDmeChannelRecordingMgr::GetLayerRecordingChannel( int nIndex )
{
	if ( m_pActiveRecordingLayer )
	{
		Assert( nIndex < m_pActiveRecordingLayer->m_LayerChannels.Count() );
		if ( nIndex < m_pActiveRecordingLayer->m_LayerChannels.Count() )
		{
			return m_pActiveRecordingLayer->m_LayerChannels[ nIndex ].m_Channel.Get();	
		}
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// Computes time selection info in log time for a particular recorded channel
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::GetLocalTimeSelection( DmeLog_TimeSelection_t& selection, int nIndex )
{
	Assert( m_bUseTimeSelection );
	Assert( m_pActiveRecordingLayer );

	if ( m_pActiveRecordingLayer )
	{		
		LayerChannelInfo_t& info = m_pActiveRecordingLayer->m_LayerChannels[nIndex];
		CDmeChannel *pChannel = info.m_Channel.Get();
		Assert( pChannel );
		if ( pChannel == nullptr)
			return;
		

		selection = m_TimeSelection;
		for ( int i = 0; i < TS_TIME_COUNT; ++i )
		{
			selection.m_nTimes[i] = info.m_ClipStack.ToChildMediaTime( selection.m_nTimes[i], false );
		}
		selection.m_pPresetValue = info.m_pPresetValue;
		selection.m_pPresetTimes = info.m_pPresetTimes;
		selection.m_tHeadPosition = info.m_HeadPosition;
		selection.m_TransformWriteMode = info.m_TransformWriteMode;
		selection.m_nComponentFlags = info.m_ComponentFlags;
		selection.m_bManipulateInFalloff = info.m_bManipulateInFalloff;

		
		// If the time selection is marked as infinite, make sure it extents past the ends of the log
		CDmeLog *pLog = pChannel->GetLog();
		if ( pLog && selection.m_bInfinite[ 0 ] )
		{		
			selection.m_nTimes[ TS_LEFT_HOLD ] = MIN( pLog->GetBeginTime() , selection.m_nTimes[ TS_LEFT_HOLD ] );
			selection.m_nTimes[ TS_LEFT_FALLOFF ] = selection.m_nTimes[ TS_LEFT_HOLD ];
		}

		if ( pLog && selection.m_bInfinite[ 1 ] )
		{
			selection.m_nTimes[ TS_RIGHT_HOLD ] = MAX( pLog->GetEndTime(), selection.m_nTimes[ TS_RIGHT_HOLD ] );
			selection.m_nTimes[ TS_RIGHT_FALLOFF ] = selection.m_nTimes[ TS_RIGHT_HOLD ];
		}

		// If the modification layer is currently being processed set the pointer to the channel's to attribute
		// to be used as the old head position, this is required because the value of the head position now may 
		// not be what it was when the recording first took place.
		if ( m_bModificationProcessing )
		{
			CDmAttribute *pAttr = pChannel->GetToAttribute();

			if ( pAttr )
			{
				selection.m_pOldHeadValue = pAttr;
				if ( IsArrayType( pAttr->GetType() ) )
				{
					selection.m_OldHeadValueIndex = pChannel->GetToArrayIndex();
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Get the index of the modification base layer for the specified channel 
// within the current recording layer.
//-----------------------------------------------------------------------------
int CDmeChannelRecordingMgr::GetModificationBaseLayer( int nIndex )
{
	Assert( m_pActiveRecordingLayer );

	if ( m_pActiveRecordingLayer )
	{
		LayerChannelInfo_t& info = m_pActiveRecordingLayer->m_LayerChannels[ nIndex ];
		return info.m_BaseLayer;
	}

	return -1;
}


//-----------------------------------------------------------------------------
// Methods which control various aspects of recording
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::UpdateTimeAdvancing( bool bPaused, DmeTime_t tCurTime )
{
	Assert( m_bUseTimeSelection );
	Assert( m_pActiveRecordingLayer );

	if ( !bPaused && !m_TimeSelection.IsTimeAdvancing() && m_pActiveRecordingLayer )
	{
		m_TimeSelection.StartTimeAdvancing();

		// blow away logs after curtime
		int nCount = m_pActiveRecordingLayer->m_LayerChannels.Count();
		for ( int i = 0; i < nCount; ++i )
		{
			LayerChannelInfo_t& info = m_pActiveRecordingLayer->m_LayerChannels[i];
			DmeTime_t t = info.m_ClipStack.ToChildMediaTime( tCurTime, false );
			info.m_Channel->GetLog()->RemoveKeys( t, DMETIME_MAXTIME );
		}
	}
}


//-----------------------------------------------------------------------------
// Update the head positions of all of the channels in the active recording 
// layer. The input time should be a global time which will be converted to the
// local time for each channel.
//-----------------------------------------------------------------------------
void CDmeChannelRecordingMgr::UpdateRecordingChannelHeadPositions( DmeTime_t tCurTime )
{
	Assert( m_pActiveRecordingLayer );

	if ( m_pActiveRecordingLayer )
	{
		int nChannels = m_pActiveRecordingLayer->m_LayerChannels.Count();
		for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
		{
			LayerChannelInfo_t &info = m_pActiveRecordingLayer->m_LayerChannels[ iChannel ];
			DmeTime_t channelTime = info.m_ClipStack.ToChildMediaTime( tCurTime, false );

			if ( info.m_HeadPosition != channelTime )
			{				
				info.m_HeadPosition = channelTime;
				
				CDmeChannel *pChannel = info.m_Channel.Get();
				if ( pChannel )
				{
					pChannel->SetCurrentTime( channelTime );
				}

				// Update the stored To attribute value to match the value at the new head position.
				StoreChannelAttributeData( iChannel, false );
			}
		}
	}
}


void CDmeChannelRecordingMgr::UpdateRecordingTimeSelectionTimes( const DmeLog_TimeSelection_t& timeSelection )
{
	Assert( m_pActiveRecordingLayer );

	m_TimeSelection.m_nTimes = timeSelection.m_nTimes;

	for ( int i = 0; i < 2; ++i )
	{
		m_TimeSelection.m_nFalloffInterpolatorTypes[ i ] = timeSelection.m_nFalloffInterpolatorTypes[ i ];
		m_TimeSelection.m_bInfinite[ i ] = timeSelection.m_bInfinite[ i ];
	}

	m_TimeSelection.m_nResampleInterval = timeSelection.m_nResampleInterval;
}

void CDmeChannelRecordingMgr::SetIntensityOnAllLayers( float flIntensity )
{
	m_TimeSelection.m_flIntensity = flIntensity;
}

void CDmeChannelRecordingMgr::SetRecordingMode( RecordingMode_t mode )
{
	m_TimeSelection.SetRecordingMode( mode );
}

void CDmeChannelRecordingMgr::SetPresetValue( CDmeChannel* pChannel, const CDmAttribute *pPresetValue, const CDmAttribute *pPresetTimes )
{
	Assert( m_pActiveRecordingLayer );
	if ( m_pActiveRecordingLayer == nullptr)
		return; 

	Assert( pChannel->GetRecordLayerIndex() != -1 );
	if ( pChannel->GetRecordLayerIndex() == -1 )
		return;

	LayerChannelInfo_t &info = m_pActiveRecordingLayer->m_LayerChannels[ pChannel->GetRecordLayerIndex() ];
	info.m_pPresetValue = pPresetValue;
	info.m_pPresetTimes = pPresetTimes;
}


//-----------------------------------------------------------------------------
// Methods to query aspects of recording
//-----------------------------------------------------------------------------
bool CDmeChannelRecordingMgr::IsUsingDetachedTimeSelection() const
{
	Assert( m_pActiveRecordingLayer );
	return !m_TimeSelection.m_bAttachedMode;
}

bool CDmeChannelRecordingMgr::IsTimeAdvancing() const
{
	Assert( m_pActiveRecordingLayer );
	return m_TimeSelection.IsTimeAdvancing();
}

bool CDmeChannelRecordingMgr::IsUsingTimeSelection() const
{
	return m_bUseTimeSelection;
}

bool CDmeChannelRecordingMgr::IsRecordingLayerActive() const
{
	return ( m_pActiveRecordingLayer != nullptr);
}

bool CDmeChannelRecordingMgr::IsModificationLayerActive() const
{
	return ( ( m_pModificationLayer != nullptr) && ( m_bWantsToFinish == false ) );
}

bool CDmeChannelRecordingMgr::IsModificationLayerVisible() const
{
	if ( !IsModificationLayerActive() )
		return false;
	
	return m_pModificationLayer->IsVisible();
}

bool CDmeChannelRecordingMgr::IsProcessingModifications() const
{
	return m_bModificationProcessing;
}

bool CDmeChannelRecordingMgr::IsModificationLayerEnabled() const
{

	return m_bModificationLayerEnabled;
}

bool CDmeChannelRecordingMgr::ShouldRecordUsingTimeSelection() const
{
	return m_bUseTimeSelection;
}


void CDmeChannelRecordingMgr::SetProceduralTarget( int nProceduralMode, const CDmAttribute *pTarget )
{
	m_nProceduralType = nProceduralMode;
	m_pRevealTarget = pTarget;
	m_PasteTarget.RemoveAll();
	m_RandomSeed = 0;
}

void CDmeChannelRecordingMgr::SetProceduralTarget( int nProceduralMode, const CUtlVector< KeyValues * >& list, int randomSeed )
{
	m_nProceduralType = nProceduralMode;
	m_pRevealTarget = nullptr;
	m_PasteTarget.RemoveAll();
	for ( int i = 0; i < list.Count(); ++i )
	{
		m_PasteTarget.AddToTail( list[ i ] );
	}
	m_RandomSeed = randomSeed;
}

int CDmeChannelRecordingMgr::GetProceduralType() const
{
	return m_nProceduralType;
}

const CDmAttribute *CDmeChannelRecordingMgr::GetProceduralTarget() const
{
	Assert( m_pRevealTarget );
	return m_pRevealTarget;
}

const CUtlVector< KeyValues * > &CDmeChannelRecordingMgr::GetPasteTarget() const
{
	return m_PasteTarget;
}

void CDmeChannelRecordingMgr::SetModificationLayerDirty()
{
	if ( m_pModificationLayer )
	{
		m_bModificationLayerDirty = true;
	}
}

CDmeChannelModificationLayer *CDmeChannelRecordingMgr::GetModificationLayer()
{
	return m_pModificationLayer;
}

void CDmeChannelRecordingMgr::SetTransformWriteMode( TransformWriteMode_t mode )
{
	m_TransformWriteMode = mode;

	if ( m_pModificationLayer )
	{
		m_pModificationLayer->UpdateTransformWriteMode( mode );
		SetModificationLayerDirty();
	}
}

TransformWriteMode_t CDmeChannelRecordingMgr::GetTransformWriteMode() const
{
	return m_TransformWriteMode;
}


void CDmeChannelRecordingMgr::UpdateActiveLayerManipulateInFalloff( bool bManipulateInFalloff )
{	
	if ( m_pActiveRecordingLayer )
	{
		int nChannels = m_pActiveRecordingLayer->m_LayerChannels.Count();
		for ( int iChannel = 0; iChannel < nChannels; ++iChannel )
		{
			LayerChannelInfo_t &channelInfo = m_pActiveRecordingLayer->m_LayerChannels[ iChannel ];
			channelInfo.m_bManipulateInFalloff = bManipulateInFalloff;
		}
	}
}

DmeTime_t GetGlobalKeyTime( const DmeClipStack_t &clipStack, CDmeLog *pLog, int idx )
{
	return pLog && idx < pLog->GetKeyCount() ? clipStack.FromChildMediaTime( pLog->GetKeyTime( idx ), false ) : DMETIME_MAXTIME;
}


//-----------------------------------------------------------------------------
// Expose this class to the scene database 
//-----------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeChannel, CDmeChannel );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CDmeChannel::OnConstruction()
{
	m_nRecordLayerIndex		= -1;
	m_nNextCurveType		= CURVE_DEFAULT;

	m_fromElement	.Init( this, "fromElement",		FATTRIB_HAS_CALLBACK | FATTRIB_NEVERCOPY );
	m_fromAttribute	.Init( this, "fromAttribute",	FATTRIB_TOPOLOGICAL | FATTRIB_HAS_CALLBACK );
	m_fromIndex		.Init( this, "fromIndex",		FATTRIB_TOPOLOGICAL );
	m_toElement		.Init( this, "toElement",		FATTRIB_HAS_CALLBACK | FATTRIB_NEVERCOPY );
	m_toAttribute	.Init( this, "toAttribute",		FATTRIB_TOPOLOGICAL | FATTRIB_HAS_CALLBACK );
	m_toIndex		.Init( this, "toIndex",			FATTRIB_TOPOLOGICAL );
	m_mode			.InitAndSet( this, "mode", (int)CM_PASS );
	m_log			.Init( this, "log" );
	m_FromAttributeHandle	= DMATTRIBUTE_HANDLE_INVALID;
	m_ToAttributeHandle		= DMATTRIBUTE_HANDLE_INVALID;
}

void CDmeChannel::OnDestruction()
{
}

int CDmeChannel::GetFromArrayIndex() const
{
	return m_fromIndex;
}

int CDmeChannel::GetToArrayIndex() const
{
	return m_toIndex;
}

int CDmeChannel::GetRecordLayerIndex() const
{
	return m_nRecordLayerIndex;
}

void CDmeChannel::SetRecordLayerIndex( int layerIndex )
{
	// If this assert is hit it most likely means that the 
	// channel is a being assigned to two record layers at once.
	Assert( ( layerIndex == -1 ) || ( m_nRecordLayerIndex == -1 ) );
	m_nRecordLayerIndex = layerIndex;
}


void CDmeChannel::Play( bool useEmptyLog /*= false*/ )
{
	CDmAttribute *pToAttr = GetToAttribute();

	if ( pToAttr == nullptr)
		return;

	CDmeLog *pLog = GetLog();
	if ( !pLog )
		return;

	DmeTime_t time = GetCurrentTime();

	DmeTime_t t0 = pLog->GetBeginTime();
	DmeTime_t tn = pLog->GetEndTime();

	PlayMode_t pmode = PM_HOLD;
	switch ( pmode )
	{
	case PM_HOLD:
		time = clamp( time, t0, tn );
		break;

	case PM_LOOP:
		if ( tn == t0 )
		{
			time = t0;
		}
		else
		{
			time -= t0;
			time = time % ( tn - t0 );
			time += t0;
		}
		break;
	}

	// We might not want to do it this way, but this makes empty logs not get in the way if there is a valid pFromAttr
	if ( !useEmptyLog )
	{
		if ( pLog->IsEmpty() && !pLog->HasDefaultValue() &&
			GetFromAttribute() != nullptr)
		{
			Pass();
			return;
		}
	}	

	pLog->GetValue( time, pToAttr, m_toIndex.Get() );
}

void CDmeChannel::Pass()
{
	CDmAttribute *pFromAttr = GetFromAttribute();
	CDmAttribute *pToAttr = GetToAttribute();
	if ( !pFromAttr || !pToAttr )
		return;

	if ( pFromAttr == pToAttr )
		return;

	DmAttributeType_t type = pFromAttr->GetType();
	const void *pValue = nullptr;
	if ( IsArrayType( type ) )
	{
		CDmrGenericArray array( pFromAttr );
		pValue = array.GetUntyped( m_fromIndex.Get() );
		type = ArrayTypeToValueType( type );
	}
	else
	{
		pValue = pFromAttr->GetValueUntyped();
	}

	if ( IsArrayType( pToAttr->GetType() ) )
	{
		CDmrGenericArray array( pToAttr );
		array.Set( m_toIndex.Get(), type, pValue );
	}
	else
	{
		pToAttr->SetValue( type, pValue );
	}
}

//-----------------------------------------------------------------------------
// IsDirty - ie needs to operate
//-----------------------------------------------------------------------------
bool CDmeChannel::IsDirty()
{
	if ( BaseClass::IsDirty() )
		return true;

	switch( GetMode() )
	{
	case CM_PLAY:
		return true;

	case CM_RECORD:
		if ( m_nRecordLayerIndex != -1 )
			return true;

		// NOTE: Fall through!
	case CM_PASS:
		{
			CDmAttribute *pFromAttr = GetFromAttribute();
			if ( pFromAttr && pFromAttr->IsFlagSet( FATTRIB_OPERATOR_DIRTY ) )
				return true;
		}
		break;

	default:
		break;
	}
	return false;
}


void CDmeChannel::Operate()
{


	switch ( GetMode() )
	{
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

void CDmeChannel::GetInputAttributes( CUtlVector< CDmAttribute * > &attrs )
{
	ChannelMode_t mode = GetMode();
	if ( mode == CM_OFF || mode == CM_PLAY )
		return; // off and play ignore inputs

	CDmAttribute *pAttr = GetFromAttribute();
	if ( pAttr != nullptr)
	{
		attrs.AddToTail( pAttr );
	}
}

void CDmeChannel::GetOutputAttributes( CUtlVector< CDmAttribute * > &attrs )
{
	ChannelMode_t mode = GetMode();
	if ( mode == CM_OFF )
		return; // off ignores inputs

	if ( mode == CM_RECORD || mode == CM_PASS )
	{
		if ( GetFromAttribute() == GetToAttribute() )
			return; // record/pass from and to the same attribute doesn't write anything
	}

	CDmAttribute *pAttr = GetToAttribute();
	if ( pAttr != nullptr)
	{
		attrs.AddToTail( pAttr );
	}
}


//-----------------------------------------------------------------------------
// accessors
//-----------------------------------------------------------------------------
CDmElement *CDmeChannel::GetFromElement() const
{
	return m_fromElement;
}

CDmElement *CDmeChannel::GetToElement() const
{
	return m_toElement;
}

void CDmeChannel::SetLog( CDmeLog *pLog )
{
	m_log = pLog;
}

CDmeLog *CDmeChannel::CreateLog( DmAttributeType_t type )
{
	CDmeLog *log = CDmeLog::CreateLog( type, GetFileId() );
	m_log.Set( log );
	return log;
}

// HACK:  This is an evil hack since the element and attribute change sequentially, but they really need to change in lockstep or else you're looking
//  up an attribute from some other element or vice versa.


void CDmeChannel::SetInput( CDmElement* pElement, const char* pAttribute, int index )
{
	m_fromElement.Set( pElement );
	m_fromAttribute.Set( pAttribute );
	m_fromIndex.Set( index );
	SetupFromAttribute();
}

void CDmeChannel::SetOutput( CDmElement* pElement, const char* pAttribute, int index )
{
	m_toElement.Set( pElement );
	m_toAttribute.Set( pAttribute );
	m_toIndex.Set( index );
	SetupToAttribute();
}

void CDmeChannel::SetInput( CDmAttribute *pAttribute, int index )
{
	if ( pAttribute )
	{
		SetInput( pAttribute->GetOwner(), pAttribute->GetName(), index );
	}
	else
	{
		SetInput(nullptr, "", index );
	}
}

void CDmeChannel::SetOutput( CDmAttribute *pAttribute, int index )
{
	if ( pAttribute )
	{
		SetOutput( pAttribute->GetOwner(), pAttribute->GetName(), index );
	}
	else
	{
		SetOutput(nullptr, "", index );
	}
}


ChannelMode_t CDmeChannel::GetMode()
{
	return static_cast< ChannelMode_t >( m_mode.Get() );
}

void CDmeChannel::SetMode( ChannelMode_t mode )
{
	if ( mode != m_mode )
	{
		m_mode.Set( static_cast< int >( mode ) );
		m_TimeState.m_tPreviousTime = DMETIME_INVALID;
	}
}

void CDmeChannel::ClearLog()
{
	GetLog()->ClearKeys();
}

CDmeLog *CDmeChannel::GetLog()
{
	if ( !m_log.GetElement() && ( m_FromAttributeHandle == DMATTRIBUTE_HANDLE_INVALID ) )
	{
		// NOTE: This will generate a new log based on the from attribute
		SetupFromAttribute();
	}
	return m_log.GetElement();
}


//-----------------------------------------------------------------------------
// Used to cache off handles to attributes
//-----------------------------------------------------------------------------
CDmAttribute *CDmeChannel::SetupFromAttribute()
{
	m_FromAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;

	CDmElement *pObject = m_fromElement.GetElement();
	const char *pName = m_fromAttribute.Get();
	if ( pObject == nullptr || pName == nullptr || !pName[0] )
		return nullptr;

	CDmAttribute *pAttr = pObject->GetAttribute( pName );
	if ( !pAttr )
		return nullptr;

	m_FromAttributeHandle = pAttr->GetHandle();

	DmAttributeType_t fromType = pAttr->GetType();
	if ( IsArrayType( fromType ) )
	{
		fromType = ArrayTypeToValueType( fromType );
	}

	CDmeLog *pLog = m_log.GetElement();
	if ( pLog == nullptr)
	{
		CreateLog( fromType );
		return pAttr;
	}

	DmAttributeType_t logType = pLog->GetDataType();
	if ( IsArrayType( logType ) )
	{
		logType = ArrayTypeToValueType( logType );
	}

	if ( logType != fromType )
	{
		// NOTE: This will release the current log
		CreateLog( fromType );
	}

	return pAttr;
}

CDmAttribute *CDmeChannel::SetupToAttribute()
{
	m_ToAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;

	CDmElement *pObject = m_toElement.GetElement();
	const char *pName = m_toAttribute.Get();
	if ( pObject == nullptr || pName == nullptr || !pName[0] )
		return nullptr;

	CDmAttribute *pAttr = pObject->GetAttribute( pName );
	if ( !pAttr )
		return nullptr;

	m_ToAttributeHandle = pAttr->GetHandle();
	return pAttr;
}


//-----------------------------------------------------------------------------
// This function gets called whenever an attribute changes
//-----------------------------------------------------------------------------
void CDmeChannel::OnAttributeChanged( CDmAttribute *pAttribute )
{
	if ( ( pAttribute == m_fromElement  .GetAttribute() ) ||
		( pAttribute == m_fromAttribute.GetAttribute() ) )
	{
		// NOTE: This will force a recache of the attribute handle
		m_FromAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;
		return;
	}

	if ( ( pAttribute == m_toElement  .GetAttribute() ) ||
		( pAttribute == m_toAttribute.GetAttribute() ) )
	{
		m_ToAttributeHandle = DMATTRIBUTE_HANDLE_INVALID;
		return;
	}

	BaseClass::OnAttributeChanged( pAttribute );
}


DmeTime_t CDmeChannel::GetCurrentTime() const
{
	return m_TimeState.m_tCurrentTime;
}

//-----------------------------------------------------------------------------
// Simple version. Only works if multiple active channels clips
// do not reference the same channels
//-----------------------------------------------------------------------------
void CDmeChannel::SetCurrentTime( DmeTime_t time )
{
	m_TimeState.m_tPreviousTime = m_TimeState.m_tCurrentTime;
	m_TimeState.m_tCurrentTime = time;
	m_TimeState.m_timeOutsideTimeframe = DMETIME_ZERO;
}

const TimeState_t &CDmeChannel::GetTimeState() const
{
	return m_TimeState;
}

void CDmeChannel::SetTimeState( TimeState_t &TimeState )
{
	m_TimeState = TimeState;
}

//-----------------------------------------------------------------------------
// SetCurrentTime sets the current time on the clip,
// choosing the time closest to (and after) a timeframe if multiple sets in a frame
//-----------------------------------------------------------------------------
void CDmeChannel::SetCurrentTime( DmeTime_t time, DmeTime_t start, DmeTime_t end )
{ 
	m_TimeState.m_tPreviousTime = m_TimeState.m_tCurrentTime;

	DmeTime_t dt( 0 );
	if ( time < start )
	{
		dt = time - start;
		time = start;
	}
	else if ( time >= end )
	{
		dt = time - end;
		time = end;
	}
	DmeTime_t totf = m_TimeState.m_timeOutsideTimeframe;

	const DmeTime_t t0( 0 );
	if ( ( dt < t0 && totf < t0 && dt < totf ) ||	// both prior to clip, old totf closer
		( dt < t0 && totf >= t0 ) ||				// new dt prior to clip, old totf in or after
		( dt >= t0 && totf >= t0 && dt > totf ) )	// both after clip, old totf closer
		return; // if old todt is a better match, don't update channel time

	m_TimeState.m_tCurrentTime = time;
	m_TimeState.m_timeOutsideTimeframe = dt;
}

//-----------------------------------------------------------------------------
// ClearTimeMetric marks m_timeOutsideTimeframe invalid (-inf is the worst possible match)
//-----------------------------------------------------------------------------
void CDmeChannel::ClearTimeMetric()
{
	m_TimeState.m_timeOutsideTimeframe = DmeTime_t::MinTime();
}

void CDmeChannel::SetChannelToPlayToSelf( const char *outputAttributeName, float defaultValue, bool force /*= false*/ )
{
	if ( !HasAttribute( outputAttributeName ) )
	{
		AddAttribute( outputAttributeName, AT_FLOAT );
	}

	CDmeTypedLog< bool > *log = static_cast< CDmeTypedLog< bool >* >( GetLog() );
	// Usually we won't put it into playback if it's empty, we'll just read the default value continously
	if ( force || ( log && !log->IsEmpty() && !log->HasDefaultValue() ) )
	{
		SetMode( CM_PLAY );
		SetOutput( this, outputAttributeName );
	}
	SetValue( outputAttributeName, defaultValue );
}

void CDmeChannel::SetNextKeyCurveType( int nCurveType )
{
	m_nNextCurveType = nCurveType;
}

CDmeLogLayer *FindLayerInSnapshot( const CDmrElementArray<CDmElement>& snapshotArray, CDmeLog *origLog )
{
	if ( !snapshotArray.IsValid() )
		return nullptr;

	int c = snapshotArray.Count();
	for ( int i = 0; i < c; ++i )
	{
		CDmeLogLayer *layer = CastElement< CDmeLogLayer >( snapshotArray[ i ] );
		if ( !layer )
			continue;

		CDmeLog *pLog = layer->GetValueElement< CDmeLog >( "origLog" );
		if ( !pLog )
		{
			Assert( 0 );
			continue;
		}

		if ( pLog == origLog )
			return layer;
	}

	return nullptr;
}

KeyValues *FindLayerInPasteData( const CUtlVector< KeyValues * > &list, CDmeLog *log )
{
	int c = list.Count();
	for ( int i = 0; i  < c; ++i )
	{
		KeyValues *kv = list[ i ];
		Assert( kv );

		if ( Q_stricmp( kv->GetName(), "ControlLayers" ) )
			continue;

		LayerSelectionData_t *data = reinterpret_cast< LayerSelectionData_t * >( kv->GetPtr( "LayerData" ) );
		if ( !data )
			continue;

		CDmeChannel *ch = data->m_hChannel;
		if ( !ch )
			continue;

		CDmeLog *chLog = ch->GetLog();
		if ( chLog == log )
			return kv;
	}

	return nullptr;
}

static int FindSpanningLayerAndSetIntensity( DmeLog_TimeSelection_t &ts, LayerSelectionData_t *data )
{
	Assert( data->m_vecData.Count() >= 2 );

	float frac = ts.m_flIntensity;
	int i = 0;
	for ( ; i < data->m_vecData.Count() - 1; ++i )
	{
		LayerSelectionData_t::DataLayer_t *current = &data->m_vecData[ i ];
		LayerSelectionData_t::DataLayer_t *next = &data->m_vecData[ i + 1 ];

		if ( frac >= current->m_flStartFraction && frac <= next->m_flStartFraction )
		{
			frac = RemapVal( frac, current->m_flStartFraction, next->m_flStartFraction, 0.0f, 1.0f );
			ts.m_flIntensity = frac;
			break;
		}
	}

	return i;
}


//-----------------------------------------------------------------------------
// Purpose: Copy the paste data from the source key values list into the 
// destination  key values list. This makes a complete copy of all data so the 
// destination can be safely referenced when the source has been modified or 
// destroyed.
// Input  : srcList - Reference to the source list of key values to be copied
// Output : dstList - Reference to the list to which the key values of the the
//			source list are to be copied. 
//-----------------------------------------------------------------------------
void CopyPasteData( CUtlVector< KeyValues * > &dstList, const CUtlVector< KeyValues * > &srcList )
{
	// Assuming the list is empty, if not we could be improperly accumulating data.
	Assert( dstList.Count() == 0 );

	int nKeys = srcList.Count();
	for ( int iKey = 0; iKey < nKeys; ++iKey )
	{
		KeyValues *pKeyValue = srcList[ iKey ];
		
		// Skip null key values, but there should not be any.
		if ( pKeyValue == nullptr)
		{
			Assert( pKeyValue );
			continue;
		}

		// Skip any unknown key value sets
		if ( V_stricmp( pKeyValue->GetName(), "ControlLayers" ) )
		{		
			continue;
		}

		// Get the layer selection data from the key value, if there is no LayerData entry, then skip the key.
		LayerSelectionData_t *pSrcLayerData = reinterpret_cast< LayerSelectionData_t * >( pKeyValue->GetPtr( "LayerData" ) );
		if ( pSrcLayerData == nullptr)
		{
			continue;
		}
	
		// Get the pointer to the log to which the layer data is for.
		CDmeLog *pLog = pSrcLayerData->m_hLog.Get();

		if ( pLog == nullptr)
		{
			Assert( pLog );
			continue;
		}

		// Create the new layer selection structure and create a new 
		// key value set for it and place it in the destination list.
		LayerSelectionData_t *pDstLayerData = new LayerSelectionData_t;
		KeyValues *pDstKeyValues = new KeyValues( "ControlLayers" );
		dstList.AddToTail( pDstKeyValues );

		// Add the layer data structure pointer to the key value set.
		pDstKeyValues->SetPtr( "LayerData", reinterpret_cast< void * >( pDstLayerData ) );
		
		// Copy the simple data element of the layer selection data
		pDstLayerData->m_hChannel		= pSrcLayerData->m_hChannel;
		pDstLayerData->m_hOwner			= pSrcLayerData->m_hOwner;
		pDstLayerData->m_hLog			= pSrcLayerData->m_hLog;
		pDstLayerData->m_DataType		= pSrcLayerData->m_DataType;
		pDstLayerData->m_nTimes[ 0 ]	= pSrcLayerData->m_nTimes[ 0 ];
		pDstLayerData->m_nTimes[ 1 ]	= pSrcLayerData->m_nTimes[ 1 ];
		pDstLayerData->m_nTimes[ 2 ]	= pSrcLayerData->m_nTimes[ 2 ];
		pDstLayerData->m_nTimes[ 3 ]	= pSrcLayerData->m_nTimes[ 3 ];
						
		// Copy each of the log layers in the layer selection data.
		int nLayers = pSrcLayerData->m_vecData.Count();
		for ( int iLayer = 0; iLayer < nLayers; ++iLayer )
		{
			float srcFrac = pSrcLayerData->m_vecData[ iLayer ].m_flStartFraction;
			CDmeLogLayer *pSrcLayer = pSrcLayerData->m_vecData[ iLayer ].m_hData.Get();


			if ( pSrcLayer )
			{
				// Create a new layer and copy the source layer
				CDmeLogLayer* pNewLayer = pLog->AddNewLayer();
				pNewLayer->CopyLayer( pSrcLayer );

				// Add the new layer to the layer selection data
				LayerSelectionData_t::DataLayer_t dataLayer( srcFrac, pNewLayer );
				pDstLayerData->m_vecData.AddToTail( dataLayer );

				// Disconnect the new layer from log
				pLog->RemoveLayerFromTail();
			}
		}
	
	} // For iKey
}


//-----------------------------------------------------------------------------
// Purpose: Destroy the contents of the provided paste data list, freeing all 
// associated resources.
// Input  : list - Paste data key value list to be destroyed.
//-----------------------------------------------------------------------------
void DestroyPasteData( CUtlVector< KeyValues * > &list )
{
	int c = list.Count();
	for ( int i = 0; i < c; ++i )
	{
		KeyValues *kv = list[ i ];
		Assert( kv );
		if ( !kv )
			continue;

		LayerSelectionData_t *data = reinterpret_cast< LayerSelectionData_t * >( kv->GetPtr( "LayerData" ) );
		if ( !data )
			continue;

		data->Release();
		delete data;
	}
}


void CDmeChannel::Record()
{


	CDmElement* pElement = GetFromElement();
	CDmAttribute *pFromAttr = GetFromAttribute();
	if ( pFromAttr == nullptr)
		return; // or clear out the log?

	CDmeLog *pLog = GetLog();
	DmeTime_t time = GetCurrentTime();
	if ( m_TimeState.m_tPreviousTime == DMETIME_INVALID )
	{
		m_TimeState.m_tPreviousTime = time;
	}

	if ( g_pChannelRecordingMgr->ShouldRecordUsingTimeSelection() )
	{
		Assert( m_nRecordLayerIndex != -1 );
		if ( m_nRecordLayerIndex == -1 )
			return;

		// Computes local times for the time selection
		DmeLog_TimeSelection_t timeSelection;
		g_pChannelRecordingMgr->GetLocalTimeSelection( timeSelection, m_nRecordLayerIndex );

		int nType = g_pChannelRecordingMgr->GetProceduralType();

		enum { PROCEDURAL_PRESET_ANIMATED = NUM_PROCEDURAL_PRESET_TYPES + 1 };
		if ( nType == PROCEDURAL_PRESET_NOT && timeSelection.m_pPresetTimes )
		{
			nType = PROCEDURAL_PRESET_ANIMATED; // this is an animated preset, which is blended in like paste/spline/etc
		}

		switch ( nType )
		{
		default:
		case PROCEDURAL_PRESET_NOT:
			{
				DmeLogTransformParams_t transformParams;
 				transformParams.m_nProceduralType = nType;

				// Only transform controls (SFM, removed) can have manipulation in falloff
				timeSelection.m_bManipulateInFalloff = false;

				// Stamp the key at the current head position, re-sampling the current time selection if time is not moving or writing
				// a key at the current time if time is moving. If the modification layer is active and the time is moving forward the
				// key will be recorded without respect to the current time selection as the time selection will be applied later.
				g_pChannelRecordingMgr->StoreChannelAttributeData( m_nRecordLayerIndex, true );
				pLog->StampKeyAtHead( time, m_TimeState.m_tPreviousTime, timeSelection, transformParams, pFromAttr, m_fromIndex.Get(), !g_pChannelRecordingMgr->IsModificationLayerActive() );				
			}
			break;
		case PROCEDURAL_PRESET_REVEAL:
			{
				// Find the matching layer in the "target" data array
				const CDmrElementArray<CDmElement> snapshotArray = const_cast< CDmAttribute * >( g_pChannelRecordingMgr->GetProceduralTarget() );
				CDmeLogLayer *snapshotLayer = FindLayerInSnapshot( snapshotArray, pLog );
				if ( snapshotLayer )
				{
					Assert( pLog );
					pLog->RevealUsingTimeSelection( timeSelection, snapshotLayer );
				}
			}
			break;
		case PROCEDURAL_PRESET_DROP_LAYER:
			{
				int nLayers = pLog->GetNumLayers();
				
				// There must be at least 3 layers in order for the drop to operate:
				// the base layer, the modification layer and the active recording layer.
				if ( nLayers >= 3 )
				{
					CDmeLogLayer* pRecLayer = pLog->GetLayer( nLayers - 1 );
					CDmeLogLayer* pModLayer = pLog->GetLayer( nLayers - 2 );
					CDmeLogLayer* pBaseLayer = pLog->GetLayer( nLayers - 3 );
	
					pLog->BlendLayersUsingTimeSelection( pBaseLayer, pModLayer, pRecLayer, timeSelection, true, false, false, DMETIME_ZERO );
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
		case PROCEDURAL_PRESET_ANIMATED:
			{
				const CUtlVector< KeyValues * > &pasteTarget = g_pChannelRecordingMgr->GetPasteTarget();
				KeyValues *layer = FindLayerInPasteData( pasteTarget, pLog );
				if ( layer )
				{
					LayerSelectionData_t *data = reinterpret_cast< LayerSelectionData_t * >( layer->GetPtr( "LayerData" ) );
					Assert( data );

					DmeLog_TimeSelection_t blendTimeSelction = timeSelection;
					int iSourceLayer = FindSpanningLayerAndSetIntensity( blendTimeSelction, data );

					CDmeLogLayer *sourceLayer = data->m_vecData[ iSourceLayer ].m_hData.Get();
					CDmeLogLayer *targetLayer = data->m_vecData[ iSourceLayer + 1 ].m_hData.Get();
					if ( sourceLayer && sourceLayer->GetKeyCount() > 0 &&
						 targetLayer && targetLayer->GetKeyCount() > 0 &&
						 sourceLayer->GetKeyCount() == targetLayer->GetKeyCount() )
					{
						Assert( pLog->GetNumLayers() >= 2 );
						CDmeLogLayer *outputLayer = pLog->GetLayer( pLog->GetTopmostLayer() );
						if ( IsPresetTimeOperation( nType ) )  // stagger and steady operate on sample times mostly
						{
							bool bFeatherBlendInFalloff = nType == PROCEDURAL_PRESET_STAGGER || nType == PROCEDURAL_PRESET_STEADY;
							pLog->BlendTimesUsingTimeSelection( sourceLayer, targetLayer, outputLayer, blendTimeSelction, data->m_nTimes[ TS_LEFT_FALLOFF ], bFeatherBlendInFalloff );
						}
						else if ( iSourceLayer != 0 )
						{
							CDmeLogLayer *baseLayer = data->m_vecData[ 0 ].m_hData.Get();
							pLog->BlendLayersUsingTimeSelection( baseLayer, sourceLayer, targetLayer, outputLayer, blendTimeSelction, false, data->m_nTimes[ TS_LEFT_FALLOFF ] );							
						}
						else
						{
							pLog->BlendLayersUsingTimeSelection( sourceLayer, targetLayer, outputLayer, blendTimeSelction, false, true, true, data->m_nTimes[ TS_LEFT_FALLOFF ] );
						}
					}
				}
			}
			break;
		}
	}
	else
	{
		int bestLayer = pLog->GetTopmostLayer();
		SegmentInterpolation_t interpSetting = (bestLayer == -1) ? SEGMENT_INTERPOLATE : pLog->GetLayer( bestLayer )->GetSegmentInterpolationSetting( m_TimeState.m_tPreviousTime, DMETIME_INVALID, false );

		if( interpSetting == SEGMENT_INTERPOLATE ) //use existing behavior
		{
			if ( m_TimeState.m_tPreviousTime != time )
			{
				pLog->SetDuplicateKeyAtTime( m_TimeState.m_tPreviousTime );
			}
			pLog->SetKey( time, pFromAttr, m_fromIndex.Get(), SEGMENT_INTERPOLATE, m_nNextCurveType );
		}
		else
		{
			//attempt to preserve the non-interpolated segment as it is. I believe that this will also work for the general case above. But I'm not 100% sure at the moment. Best not to break existing behavior
			if ( m_TimeState.m_tPreviousTime != time )
			{
				pLog->InsertKeyAtTime( m_TimeState.m_tPreviousTime );
			}
			pLog->SetKey( time, pFromAttr, m_fromIndex.Get(), pLog->GetLayer( bestLayer )->GetSegmentInterpolationSetting( time ), m_nNextCurveType );
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
bool CDmeChannel::BuildClipStack( DmeClipStack_t *pClipStack, CDmeClip *pRoot, CDmeClip *pShot ) const
{
	DmAttributeReferenceIterator_t it;
	for ( it = g_pDataModel->FirstAttributeReferencingElement( GetHandle() );
		it != DMATTRIBUTE_REFERENCE_ITERATOR_INVALID;
		it = g_pDataModel->NextAttributeReferencingElement( it ) )
	{
		CDmAttribute *pAttribute = g_pDataModel->GetAttribute( it );
		CDmElement *pElement = pAttribute->GetOwner();
		CDmeChannelsClip *pChannelsClip = CastElement< CDmeChannelsClip >( pElement );
		if ( !pChannelsClip )
			continue;

		if ( pChannelsClip->BuildClipStack( pClipStack, pRoot, pShot ) )
			return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Finds the owner clip for a channel which passes through the root
//-----------------------------------------------------------------------------
CDmeClip* CDmeChannel::FindOwnerClipForChannel( CDmeClip *pRoot )
{
	DmeClipStack_t stack;
	if ( BuildClipStack( &stack, pRoot, pRoot ) )
		return stack.GetClip( stack.GetClipCount() - 1 );
	return nullptr;
}

void CDmeChannel::ScaleSampleTimes( float scale )
{
	m_log->ScaleSampleTimes( scale );
}

void CDmeChannel::ClearAndAddSampleAtTime( DmeTime_t time )
{
	m_log->ClearAndAddSampleAtTime( time );
}


void RemapFloatLogValues( CDmeChannel *pChannel, float flBias, float flScale )
{
	if ( !pChannel )
		return;

	CDmeLog *pLog = pChannel->GetLog();
	if ( !pLog )
		return;

	CDmeTypedLogLayer< float > *pLogLayer = CastElement< CDmeTypedLogLayer< float > >( pLog->GetLayer( pLog->GetTopmostLayer() ) );
	if ( !pLogLayer )
		return;

	int nKeys = pLogLayer->GetKeyCount();
	for ( int i = 0; i < nKeys; ++i )
	{
		float flValue = pLogLayer->GetKeyValue( i );
		pLogLayer->SetKeyValue( i, flValue * flScale + flBias );
	}
}

CDmeChannel *FindChannelTargetingAttribute( CDmAttribute *pTargetAttr )
{
	if ( !pTargetAttr )
		return nullptr;

	CDmElement *pTarget = pTargetAttr->GetOwner();
	if ( !pTarget )
		return nullptr;

	for ( DmAttributeReferenceIterator_t it = g_pDataModel->FirstAttributeReferencingElement( pTarget->GetHandle() );
		it != DMATTRIBUTE_REFERENCE_ITERATOR_INVALID;
		it = g_pDataModel->NextAttributeReferencingElement( it ) )
	{
		CDmAttribute *pAttr = g_pDataModel->GetAttribute( it );
		Assert( pAttr );
		CDmeChannel *pChannel = CastElement< CDmeChannel >( pAttr->GetOwner() );
		if ( pChannel && pChannel->GetToAttribute() == pTargetAttr )
			return pChannel;
	}
	return nullptr;
}

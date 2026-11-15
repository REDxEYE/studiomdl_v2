//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Operators that generate combinations
//
//=============================================================================

#pragma once


#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "movieobjects/dmeoperator.h"

#include <string>
#include <vector>


//-----------------------------------------------------------------------------
// Expression operator
//-----------------------------------------------------------------------------
class CDmeChannel;
class CDmeDag;
class CDmElement;
class CDmeChannelsClip;
class CDmeShape;


//-----------------------------------------------------------------------------
// Control handles
//-----------------------------------------------------------------------------
typedef int ControlIndex_t;


//-----------------------------------------------------------------------------
// Basic version..
//-----------------------------------------------------------------------------
class CDmeCombinationInputControl : public CDmElement
{
	DEFINE_ELEMENT( CDmeCombinationInputControl, CDmElement );

public:
    void OnElementUnserialized() override;

	// Removes controls
	// returns true if remapped control lists need updating

	// Iterates remapped controls
	int RawControlCount() const;
	const char *RawControlName( int nIndex ) const;

	// Reordering controls

	// Is this control a stereo control?
	bool IsStereo() const;

	// Is this control an eyelid control?
	bool IsEyelid() const;

	// Returns the name of the eyeball
	const char *GetEyesUpDownFlexName() const;

	// Returns the wrinkle scale for a particular control
	float WrinkleScale( int nIndex );


private:

	CDmaStringArray m_RawControlNames;
	CDmaVar<bool> m_bIsStereo;
	CDmaVar< bool > m_bIsEyelid;

	// FIXME! Remove soon! Used to autogenerate wrinkle deltas
	CDmaArray< float > m_WrinkleScales;
};


//-----------------------------------------------------------------------------
// Basic version..
//-----------------------------------------------------------------------------
class CDmeCombinationDominationRule : public CDmElement
{
	DEFINE_ELEMENT( CDmeCombinationDominationRule, CDmElement );

public:
	// Methods of IDmElement
    void	OnAttributeChanged( CDmAttribute *pAttribute ) override;

	// Remove all dominatior + suppressed controls

	// Iteration
	int DominatorCount() const;
	const char *GetDominator( int i ) const;

	int SuppressedCount() const;
	const char *GetSuppressed( int i ) const;

	// Search

private:

	CDmaStringArray m_Dominators;
	CDmaStringArray m_Suppressed;
};


//-----------------------------------------------------------------------------
// Basic version.. needs channels to copy the data out of its output attributes
//-----------------------------------------------------------------------------
enum CombinationControlType_t
{
	COMBO_CONTROL_FIRST = 0,

	COMBO_CONTROL_NORMAL = 0,
	COMBO_CONTROL_LAGGED,

	COMBO_CONTROL_TYPE_COUNT,
};

class CDmeCombinationOperator : public CDmeOperator
{
	DEFINE_ELEMENT( CDmeCombinationOperator, CDmeOperator );

public:
	// Methods of IDmElement
    void	OnAttributeChanged( CDmAttribute *pAttribute ) override;

    void OnElementUnserialized() override;

	// Finds the index of the control with the specified name
	ControlIndex_t FindControlIndex( const char *pControlName );

	// Removes a control
	void RemoveControl( const char *pControlName );

	// Removes an remapped control from a control

	// Iterates output controls associated with an input control
	int GetRawControlCount( ControlIndex_t nControl ) const;
	const char *GetRawControlName( ControlIndex_t nControl, int nIndex ) const;

	// Iterates a global list of output controls
	int GetRawControlCount( ) const;

    std::string_view GetRawControlName(int nIndex) const;

	// Gets Input Control Default & Base Values

	// Do we have a raw control?
	bool HasRawControl( const char *pRawControlName ) const;

	// Sets the value of a stereo control

	// Returns true if a control is a stereo control
	bool IsStereoControl( ControlIndex_t nControlIndex ) const;

	// Sets the level of a control (only used by controls w/ 3 or more remappings)

	// Reordering controls


	// Returns true if a control is a multi control (a control w/ 3 or more remappings)
	bool IsMultiControl( ControlIndex_t nControlIndex ) const;

	// Returns true if a control is a multi-control which controls eyelids
	bool IsEyelidControl( ControlIndex_t nControlIndex ) const;
	const char *GetEyesUpDownFlexName( ControlIndex_t nControlIndex ) const;

	// Sets the value of a control

	// Iterates controls
	int GetControlCount() const;
	const char *GetControlName( ControlIndex_t i ) const;

	// Adds a domination rule. Domination rules are specified using raw control names

	// Removes a domination rule
	void RemoveDominationRule( int nIndex );
	void RemoveDominationRule( CDmeCombinationDominationRule *pRule );

	// Iteration
	int DominationRuleCount() const;
	CDmeCombinationDominationRule *GetDominationRule( int i );

	// Rule reordering

	// Indicates we're using lagged control values
	void UsingLaggedData( bool bEnable );
	bool IsUsingLaggedData() const;

	// Adds a target model/arbitrary element to perform the combinations on
	// The arbitrary element must have two attributes
	// "deltaStates", which is an array of elements
	// "deltaStateWeights", which is an array of floats
	// In the case of the model, it will look for all shapes in the dag hierarchy
	// and attempt to add that shape as a target
	// NOTE: Targets are not saved
	void AddTarget( CDmElement *pElement );

	// Used by studiomdl to discover the various combination rules
	int GetOperationTargetCount() const;
	CDmElement *GetOperationTarget( int nTargetIndex );
	int GetOperationCount( int nTargetIndex ) const;
	CDmElement *GetOperationDeltaState( int nTargetIndex, int nOpIndex );
	const std::vector< int > &GetOperationControls( int nTargetIndex, int nOpIndex ) const;
	int GetOperationDominatorCount( int nTargetIndex, int nOpIndex ) const;
	const std::vector< int > &GetOperationDominator( int nTargetIndex, int nOpIndex, int nDominatorIndex ) const;

    void Operate() override;

    void Resolve() override;

	void GetInputAttributes ( std::vector< CDmAttribute * > &attrs ) override;
	void GetOutputAttributes( std::vector< CDmAttribute * > &attrs ) override;

	// Would a particular delta state attached to this combination operator end up stero?
	bool IsDeltaStateStereo( const char *pDeltaStateName );

	void GenerateWrinkleDeltas( bool bOverwrite, bool bUseNormalForSign = false, float flScale = 1.0f );

	// Remove all controls and domination rules which are not referring to anything
	void Purge();

protected:
	void ComputeCombinationInfo( int nIndex );

private:
	typedef int RawControlIndex_t;

	struct DominatorInfo_t
	{
		std::vector< RawControlIndex_t > m_DominantIndices;
		std::vector< RawControlIndex_t > m_SuppressedIndices;
	};

	struct CombinationOperation_t
	{
		int m_nDeltaStateIndex;
		std::vector< RawControlIndex_t > m_ControlIndices;
		std::vector< RawControlIndex_t > m_DominatorIndices;
	};

	struct CombinationInfo_t
	{
		DmAttributeHandle_t m_hDestAttribute[COMBO_CONTROL_TYPE_COUNT];
		std::vector< CombinationOperation_t > m_Outputs;
	};

	struct RawControlInfo_t
	{
		std::string m_Name;
		bool m_bIsDefaultControl;
		ControlIndex_t m_InputControl;
		float m_flWrinkleScale;
		bool m_bLowerEyelid;
		Vector4D m_FilterRamp;	// [0] = point at which ramp starts going up
								// [1] = point at which ramp hits 1.0
								// [2] = point at which ramp stops holding at 1.0
								// [3] = point at which ramp starts going down
	};

	void ComputeCombinationInfo();
	void CleanUpCombinationInfo( int nIndex );

	// Determines which combination to use based on the deltaState name
	int ParseDeltaName( const char *pDeltaStateName, int *pControlIndices );

	// Finds dominators
	void FindDominators( CombinationOperation_t& op );

	// Computes lists of dominators and suppressors
	void RebuildDominatorInfo();

	// Computes list of all remapped controls
	void RebuildRawControlList();

	// Remaps non-stereo -> stereo, stereo ->left/right
	void ComputeInternalControlValue( RawControlIndex_t nRawControlIndex, CombinationControlType_t type, Vector2D &value );

	// Computes lagged input values from non-lagged input
	void ComputeLaggedInputValues();

	// Finds the index of the remapped control with the specified name
	RawControlIndex_t FindRawControlIndex( const char *pControlName, bool bIgnoreDefaultControls = false ) const;

	// Generates wrinkle deltas for a dag hierarchy
	void GenerateWrinkleDeltas( CDmeShape *pShape, bool bOverwrite, bool bUseNormalForSign, float flScale = 1.0f );

	CDmaElementArray< CDmeCombinationInputControl > m_InputControls;
	CDmaArray< Vector > m_ControlValues[COMBO_CONTROL_TYPE_COUNT];
	CDmaVar< bool > m_bSpecifyingLaggedData;

	CDmaElementArray< CDmeCombinationDominationRule > m_Dominators;

	CDmaElementArray< CDmElement > m_Targets;

	std::vector< bool > m_IsDefaultValue;	// one per control value
	std::vector< RawControlInfo_t > m_RawControlInfo;
	std::vector< CombinationInfo_t > m_CombinationInfo;
	std::vector< DominatorInfo_t > m_DominatorInfo;

	float m_flLastLaggedComputationTime;
};


//-----------------------------------------------------------------------------
// Indicates we're using lagged control values
//-----------------------------------------------------------------------------
inline void CDmeCombinationOperator::UsingLaggedData( bool bEnable )
{
	m_bSpecifyingLaggedData = bEnable;
}

inline bool CDmeCombinationOperator::IsUsingLaggedData() const
{
	return m_bSpecifyingLaggedData;
}



//-----------------------------------------------------------------------------
//
// A class used to edit combination operators in Maya.. doesn't connect to targets
//
//-----------------------------------------------------------------------------
class CDmeMayaCombinationOperator : public CDmeCombinationOperator
{
	DEFINE_ELEMENT( CDmeMayaCombinationOperator, CDmeCombinationOperator );

public:

private:
	CDmaElementArray< CDmElement > m_DeltaStates;
	CDmaArray< Vector2D > m_DeltaStateWeights[COMBO_CONTROL_TYPE_COUNT];
};



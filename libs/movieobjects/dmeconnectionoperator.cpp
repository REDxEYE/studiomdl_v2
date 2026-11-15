//====== Copyright � 1996-2009, Valve Corporation, All rights reserved. =======
// 
// Purpose: Declaration of the CDmeConnectionOperator class, a CDmeOperator 
// which copies one attribute value to another, providing similar functionality
// to CDmeChannel, but does not store a log and is not effected by the 
// recording mode.
//
//=============================================================================
#include "movieobjects/dmeconnectionoperator.h"
#include "datamodel/dmelementfactoryhelper.h"





//-------------------------------------------------------------------------------------------------
// Expose this class to the scene database 
//-------------------------------------------------------------------------------------------------
IMPLEMENT_ELEMENT_FACTORY( DmeConnectionOperator, CDmeConnectionOperator );

//-------------------------------------------------------------------------------------------------
// Purpose: Constructor, initializes attributes, create the embedded target
//-------------------------------------------------------------------------------------------------
void CDmeConnectionOperator::OnConstruction()
{
	m_Input.InitAndCreate( this, "input" );
	m_Outputs.Init( this, "outputs" );
}


//-------------------------------------------------------------------------------------------------
// Purpose: Perform destruction tasks, destroy the internal elements of the constraint.
//-------------------------------------------------------------------------------------------------
void CDmeConnectionOperator::OnDestruction()
{
	g_pDataModel->DestroyElement( m_Input.GetHandle() );

	int nOutputs = m_Outputs.size();
	for ( int i = 0 ;i < nOutputs; ++i )
	{
		if ( m_Outputs[ i ] )
		{
			g_pDataModel->DestroyElement( m_Outputs[ i ]->GetHandle() );
		}
	}

	m_Outputs.RemoveAll();
}


//-------------------------------------------------------------------------------------------------
// Purpose: Run the operator, which copies the value from the source attribute to the destination 
// attributes.
//-------------------------------------------------------------------------------------------------
void CDmeConnectionOperator::Operate()
{
	if ( !m_Input->IsValid() )
		return;

	int nOutputs = m_Outputs.size();
	if ( nOutputs == 0 )
		return;
			
	DmAttributeType_t inputType = AT_UNKNOWN;
	const void *pValue = m_Input->GetAttributeValue( inputType );

	for ( int iOutput = 0; iOutput < nOutputs; ++iOutput )
	{
		m_Outputs[ iOutput ]->SetAttributeValue( pValue, inputType );
	}
}


//-------------------------------------------------------------------------------------------------
// Purpose: Determine if data has changed and the operator needs to be updated
//-------------------------------------------------------------------------------------------------
bool CDmeConnectionOperator::IsDirty()
{
	CDmAttribute* pAttr = m_Input->GetReferencedAttribute();

	if ( pAttr )
	{
		return pAttr->IsFlagSet( FATTRIB_DIRTY );
	}

	return false;
}


//-------------------------------------------------------------------------------------------------
// Purpose: Add the input attribute used by the operator to the provided list of attributes, This 
// is generally used by the evaluation process to find the attributes an operator is dependent on.
//-------------------------------------------------------------------------------------------------
void CDmeConnectionOperator::GetInputAttributes( std::vector< CDmAttribute * > &attrs )
{
	CDmAttribute *pInputAttr = m_Input->GetReferencedAttribute();
	if ( pInputAttr )
	{
		attrs.push_back( pInputAttr );
	}
}


//-------------------------------------------------------------------------------------------------
// Purpose: Add each of attributes the connection operator outputs to the provided list of 
// attributes. This is generally used by the evaluation process to find out what attributes are 
// written by the operator in order to determine what other operators are dependent on this 
// operator.
//-------------------------------------------------------------------------------------------------
void CDmeConnectionOperator::GetOutputAttributes( std::vector< CDmAttribute * > &attrs )
{
	int nOutputs = m_Outputs.size();
	for ( int iOutput = 0; iOutput < nOutputs; ++iOutput )
	{
		CDmAttribute *pOutputAttr = m_Outputs[ iOutput ]->GetReferencedAttribute();
		if ( pOutputAttr )
		{
			attrs.push_back( pOutputAttr );
		}
	}
}







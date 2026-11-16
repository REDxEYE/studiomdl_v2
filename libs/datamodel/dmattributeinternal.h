//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once


#include "datamodel/dmattribute.h"


//-----------------------------------------------------------------------------
// Forward declarations: 
//-----------------------------------------------------------------------------
class IDataModelFactory;
class CUtlBuffer;
class Vector;
class Color;
class CUtlCharConversion;
class CDmElement;


//-----------------------------------------------------------------------------
// For serialization, set the delimiter rules
//-----------------------------------------------------------------------------
void SetSerializationDelimiter( CUtlCharConversion *pConv );
void SetSerializationArrayDelimiter( const char *pDelimiter );


//-----------------------------------------------------------------------------
// Skip unserialization for an attribute type (unserialize into a dummy variable)
//-----------------------------------------------------------------------------
bool SkipUnserialize( CUtlBuffer &buf, DmAttributeType_t type );


//-----------------------------------------------------------------------------
// Attribute names/types
//-----------------------------------------------------------------------------
const char *AttributeTypeName( DmAttributeType_t type );
DmAttributeType_t AttributeType( const char *pAttributeType );


//-----------------------------------------------------------------------------
// returns the number of attributes currently allocated
//-----------------------------------------------------------------------------
int GetAllocatedAttributeCount();


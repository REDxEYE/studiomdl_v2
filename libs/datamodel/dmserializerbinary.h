//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: Outputs directly into a binary format
//
//=============================================================================

#pragma once

#ifdef _WIN32
#pragma once
#endif


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class IDataModel;


//-----------------------------------------------------------------------------
// Installation methods for standard serializers
//-----------------------------------------------------------------------------
void InstallBinarySerializer( IDataModel *pFactory );



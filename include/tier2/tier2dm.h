//===== Copyright © 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Startup/shutdown of the datamodel libraries (datamodel, element framework, dmserializers).
//
//===========================================================================//

#pragma once

// Installs the DME element factories, then initializes the datamodel, the element framework and dmserializers.
// On failure the libraries initialized so far are shut down again.
bool InitDataModel();

// Reverse order of InitDataModel
void ShutdownDataModel();

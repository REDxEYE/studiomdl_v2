//===== Copyright © 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Global library instances shared across the libraries.
//
//===========================================================================//

#pragma once

class IFileSystem;

// The filesystem (CFileSystem_Stdio, libs/filesystem/filesystem_stdio.cpp)
extern IFileSystem *g_pFullFileSystem;

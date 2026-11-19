//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
	   
#pragma once



#include "tier0/platform.h"

//-----------------------------------------------------------------------------
// dll export stuff
//-----------------------------------------------------------------------------
#if defined(STATIC_TIER0)
// linked statically (the only configuration built): nothing is imported or exported. __declspec(dllexport) on a class
// makes MSVC emit its vtable and every member, including ones that are declared but never defined
#define VSTDLIB_INTERFACE	DLL_IMPORT
#define VSTDLIB_OVERLOAD
#define VSTDLIB_CLASS
#define VSTDLIB_GLOBAL		extern
#elif defined(VSTDLIB_DLL_EXPORT)
#define VSTDLIB_INTERFACE	DLL_EXPORT
#define VSTDLIB_OVERLOAD	DLL_GLOBAL_EXPORT
#define VSTDLIB_CLASS		DLL_CLASS_EXPORT
#define VSTDLIB_GLOBAL		DLL_GLOBAL_EXPORT
#else
#define VSTDLIB_INTERFACE	DLL_IMPORT
#define VSTDLIB_OVERLOAD	DLL_GLOBAL_IMPORT
#define VSTDLIB_CLASS		DLL_CLASS_IMPORT
#define VSTDLIB_GLOBAL		DLL_GLOBAL_IMPORT
#endif
 

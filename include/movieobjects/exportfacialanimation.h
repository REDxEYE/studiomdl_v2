//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#ifdef _WIN32
#pragma once
#endif


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CDmeFilmClip;
class CDmeAnimationSet;


//-----------------------------------------------------------------------------
// Exports an .fac file
//-----------------------------------------------------------------------------
bool ExportFacialAnimation( const char *pFileName, CDmeFilmClip *pMovie, CDmeFilmClip *pShot, CDmeAnimationSet *pAnimationSet );



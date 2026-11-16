//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
#pragma once
#include "mathlib/vector.h"

class CDmElement;
class ScriptTokenizer;
struct s_source_t;
struct StudioMdlState;

int DoCollisionModel(StudioMdlState &ctx, ScriptTokenizer &qc, bool separateJoints );

#ifdef MDLCOMPILE
int DoCollisionModel( StudioMdlState &ctx, s_source_t *pSource, CDmElement *pInfo, bool bStaticProp );
void LoadCollisionText( StudioMdlState &ctx, const char *pszCollisionText );
#endif // MDLCOMPILE

// execute after simplification, before writing
void CollisionModel_Build( StudioMdlState &ctx );
// execute during writing
extern void CollisionModel_Write( StudioMdlState &ctx, long checkSum );


//====== Copyright © 1996-2008, Valve Corporation, All rights reserved. =====//
//
// Describes the way to compile a MDL file (eventual replacement for qc)
//
//===========================================================================//

#pragma once



#include "movieobjects/dmemakefile.h"



//-----------------------------------------------------------------------------
// Describes a skin source for MDL makefiles
//-----------------------------------------------------------------------------
class CDmeSourceSkin : public CDmeSource
{
	DEFINE_ELEMENT( CDmeSourceSkin, CDmeSource );

public:
	// These can be built from DCC makefiles
    const char **GetSourceMakefileTypes() override;

	CDmaString m_SkinName;
	CDmaVar<bool> m_bFlipTriangles;
	CDmaVar<bool> m_bQuadSubd;
	CDmaVar<float> m_flScale;
};


//-----------------------------------------------------------------------------
// Describes a skin source for MDL makefiles
//-----------------------------------------------------------------------------
class CDmeSourceCollisionModel : public CDmeSource
{
	DEFINE_ELEMENT( CDmeSourceCollisionModel, CDmeSource );

public:
	// These can be built from DCC makefiles
    const char **GetSourceMakefileTypes() override;

private:
};


//-----------------------------------------------------------------------------
// Describes an animation source for MDL makefiles
//-----------------------------------------------------------------------------
class CDmeSourceAnimation : public CDmeSource
{
	DEFINE_ELEMENT( CDmeSourceAnimation, CDmeSource );

public:
	// These can be built from DCC makefiles
    const char **GetSourceMakefileTypes() override;

	CDmaString m_AnimationName;
	CDmaString m_SourceAnimationName;	// Name in the source file

private:
};


//-----------------------------------------------------------------------------
// Describes a MDL asset: something that is compiled from sources 
//-----------------------------------------------------------------------------
class CDmeMDLMakefile : public CDmeMakefile
{
	DEFINE_ELEMENT( CDmeMDLMakefile, CDmeMakefile );

public:
    DmeMakefileType_t *GetMakefileType() override;

    DmeMakefileType_t* GetSourceTypes() override;

    void GetOutputs( CUtlVector<CUtlString> &fullPaths ) override;

private:
	// Inherited classes should re-implement these methods
    CDmElement *CreateOutputElement( ) override;

    void DestroyOutputElement( CDmElement *pOutput ) override;

    const char *GetOutputDirectoryID() override { return "makefilegamedir:.."; }

	bool m_bFlushMDL;
};



//
// Created by red_eye on 11/5/26.
//

#pragma once
#include "movieobjects/dmeanimationlist.h"
#include "movieobjects/dmecombinationoperator.h"
#include "movieobjects/dmemodel.h"

struct s_model_t;
struct BoneTransformMap_t;
struct s_source_t;

bool LoadPreprocessedFile( const char *pFileName, float flScale );

// Prints the root "makefile.sources" of a DMX (Valve scheduled them for Perforce check-in)
void LoadModelInfo(CDmElement *pRoot, char const *pFullPath);

bool LoadModelAndSkeleton(s_source_t *pSource, BoneTransformMap_t &boneMap, CDmeDag *pSkeleton, CDmeModel *pModel,
                     CDmeCombinationOperator *pCombinationOperator, bool bStaticProp);

void LoadQcModelElements(s_source_t *pSource, s_model_t *pModel, CDmeModel *pDmeModel);

void LoadEyeballs(s_source_t *pSource, s_model_t *pModel, CDmrElementArray<CDmElement> &elementArray);

void LoadAnimations(s_source_t *pSource, CDmeAnimationList *pAnimationList, float flScale, BoneTransformMap_t &boneMap);
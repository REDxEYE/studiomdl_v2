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
struct StudioMdlState;

bool LoadPreprocessedFile(StudioMdlState &ctx, std::string_view fileName, float flScale);

// Compiles an .mpp (DMX model description, Valve's mdlcompile input): loads the model from the DMX root and sets the
// output name if the root has no "mdlPath"
bool ParsePreprocessedFile(StudioMdlState &ctx, const std::string &fullPath);

// Prints the root "makefile.sources" of a DMX (Valve scheduled them for Perforce check-in)
void LoadModelInfo(CDmElement *pRoot, char const *pFullPath);

bool LoadModelAndSkeleton(StudioMdlState &ctx, s_source_t *pSource, BoneTransformMap_t &boneMap, CDmeDag *pSkeleton,
                          CDmeModel *pModel, CDmeCombinationOperator *pCombinationOperator, bool bStaticProp);

void LoadQcModelElements(StudioMdlState &ctx, s_source_t *pSource, s_model_t *pModel, CDmeModel *pDmeModel);

void LoadEyeballs(StudioMdlState &ctx, s_source_t *pSource, s_model_t *pModel, CDmrElementArray<CDmElement> &elementArray);

void LoadAnimations(StudioMdlState &ctx, s_source_t *pSource, CDmeAnimationList *pAnimationList, float flScale,
                    BoneTransformMap_t &boneMap);
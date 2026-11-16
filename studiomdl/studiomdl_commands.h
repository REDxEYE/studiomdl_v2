//
// Created by RED on 15.05.2024.
//

#pragma once

#include "studiomdl/studiomdl.h"
#include "common/scriplib.h"

s_source_t *FindCachedSource(StudioMdlState &ctx, const char *name, const char *xext);
void AddBodyFlexData(StudioMdlState &ctx, s_source_t *pSource, int imodel, bool bAddControllers = true);
void AddBodyAttachments(StudioMdlState &ctx, s_source_t *pSource);
void AddBodyFlexRules(StudioMdlState &ctx, s_source_t *pSource);
void Option_Flexrule(StudioMdlState &ctx, ScriptTokenizer &qc, s_model_t * pmodel , const char *name);
int ParseAnimation(StudioMdlState &ctx, ScriptTokenizer &qc, s_animation_t *panim, bool isAppend);
int ParseEmpty(ScriptTokenizer &qc);
int ParseSequence(StudioMdlState &ctx, ScriptTokenizer &qc, s_sequence_t *pseq, bool isAppend);

struct MDLCommand_t {
    const char *m_pName;
    void (*m_pCmd)(StudioMdlState &ctx, ScriptTokenizer &qc);
};

// The QC command table (constant, defined in studiomdl_commands.cpp)
extern const MDLCommand_t g_Commands[];
extern const int g_nMDLCommandCount;

void ProcessSequence(s_sequence_t *pseq, int numblends, s_animation_t **animations, bool isAppend);
void ProcessStaticProp(StudioMdlState &ctx);
s_sequence_t *ProcessCmdSequence(StudioMdlState &ctx, const char *pSequenceName);
s_animation_t *ProcessImpliedAnimation(StudioMdlState &ctx, s_sequence_t *psequence, const char *filename);
//
// Created by RED on 15.05.2024.
//

#pragma once

#include "studiomdl/studiomdl.h"
#include "common/scriplib.h"

s_source_t *FindCachedSource(const char *name, const char *xext);
void AddBodyFlexData(s_source_t *pSource, int imodel, bool bAddControllers = true);
void AddBodyAttachments(s_source_t *pSource);
void AddBodyFlexRules(s_source_t *pSource);
void Option_Flexrule(ScriptTokenizer &qc, s_model_t * pmodel , const char *name);
int ParseAnimation(ScriptTokenizer &qc, s_animation_t *panim, bool isAppend);
int ParseEmpty(ScriptTokenizer &qc);
int ParseSequence(ScriptTokenizer &qc, s_sequence_t *pseq, bool isAppend);

struct MDLCommand_t {
    const char *m_pName;
    void (*m_pCmd)(ScriptTokenizer &qc);
};

extern MDLCommand_t g_Commands[];
extern int g_nMDLCommandCount;

static MDLCommand_t *g_pMDLCommands = g_Commands;

void ProcessSequence(s_sequence_t *pseq, int numblends, s_animation_t **animations, bool isAppend);
void ProcessStaticProp();
s_sequence_t *ProcessCmdSequence(const char *pSequenceName);
s_animation_t *ProcessImpliedAnimation(s_sequence_t *psequence, const char *filename);
//
// Created by RED on 15.05.2024.
//

#ifndef STUDIOMDL_V2_STUDIOMDL_COMMANDS_H
#define STUDIOMDL_V2_STUDIOMDL_COMMANDS_H

#include "studiomdl/studiomdl.h"

s_source_t *FindCachedSource(const char *name, const char *xext);
void AddBodyFlexData(s_source_t *pSource, int imodel);
void AddBodyAttachments(s_source_t *pSource);
void AddBodyFlexRules(s_source_t *pSource);
void Option_Flexrule(s_model_t * pmodel , const char *name);
int ParseAnimation(s_animation_t *panim, bool isAppend);
int ParseEmpty(void);
int ParseSequence(s_sequence_t *pseq, bool isAppend);

struct MDLCommand_t {
    char *m_pName;
    void (*m_pCmd)();
};

extern MDLCommand_t g_Commands[];
extern int g_nMDLCommandCount;

static MDLCommand_t *g_pMDLCommands = g_Commands;

#endif //STUDIOMDL_V2_STUDIOMDL_COMMANDS_H

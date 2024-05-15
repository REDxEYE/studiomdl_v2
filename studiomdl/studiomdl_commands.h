//
// Created by RED on 15.05.2024.
//

#ifndef STUDIOMDL_V2_STUDIOMDL_COMMANDS_H
#define STUDIOMDL_V2_STUDIOMDL_COMMANDS_H

#include "studiomdl/studiomdl.h"

s_source_t *FindCachedSource(const char *name, const char *xext);

struct MDLCommand_t {
    char *m_pName;
    void (*m_pCmd)();
};

extern MDLCommand_t g_Commands[];
extern int g_nMDLCommandCount;

static MDLCommand_t *g_pMDLCommands = g_Commands;

#endif //STUDIOMDL_V2_STUDIOMDL_COMMANDS_H

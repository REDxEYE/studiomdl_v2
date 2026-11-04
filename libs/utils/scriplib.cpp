//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

// scriplib.c

#include "tier1/strtools.h"
#include "tier2/tier2.h"
#include "cmdlib.h"
#include "scriplib.h"
#include "tier0/platform.h"

#include <array>
#include <string>
#include <vector>
#if (defined(WIN32) | defined(_WIN32))
#include "windows.h"
#elif defined(POSIX)
#include <sys/stat.h>
#endif
/*
=============================================================================

						PARSING STUFF

=============================================================================
*/

typedef struct {
    std::string filename; // UTF-8 path of the script, the macro name for macros, or "memory buffer"
    char *buffer, *script_p, *end_p;
    int line;

    char macrobuffer[4096];
    char *macroparam[64];
    char *macrovalue[64];
    int nummacroparams;
} script_t;

constexpr uint32_t MAX_INCLUDES = 16;
std::array<script_t, MAX_INCLUDES> scriptstack;
script_t *script = nullptr;
int scriptline;

char token[MAXTOKEN]{};
qboolean endofscript;
qboolean tokenready; // only true if UnGetToken was just called

typedef struct {
    char *param;
    char *value;
    char *param_lcase;
} variable_t;

CUtlVector<variable_t> g_definevariable;

/*
Callback stuff
*/

void DefaultScriptLoadedCallback(char const *pFilenameLoaded, char const *pIncludedFromFileName,
                                 int nIncludeLineNumber) {
    NULL;
}

SCRIPT_LOADED_CALLBACK g_pfnCallback = DefaultScriptLoadedCallback;

SCRIPT_LOADED_CALLBACK SetScriptLoadedCallback(SCRIPT_LOADED_CALLBACK pfnNewScriptLoadedCallback) {
    SCRIPT_LOADED_CALLBACK pfnCallback = g_pfnCallback;
    g_pfnCallback = pfnNewScriptLoadedCallback;
    return pfnCallback;
}

/*
==============
AddScriptToStack
==============
*/
void AddScriptToStack(const char *filename, ScriptPathMode_t pathMode = SCRIPT_USE_ABSOLUTE_PATH) {
    int size;

    script++;
    if (script == scriptstack.data() + MAX_INCLUDES)
        Error("script file exceeded MAX_INCLUDES");

    if (pathMode == SCRIPT_USE_RELATIVE_PATH)
        script->filename = filename;
    else
        script->filename = ExpandPath(filename);

    size = LoadFile(script->filename.c_str(), (void **) &script->buffer);

    // printf ("entering %s\n", script->filename);
    if (g_pfnCallback) {
        if (script == scriptstack.data() + 1)
            g_pfnCallback(script->filename.c_str(), nullptr, 0);
        else
            g_pfnCallback(script->filename.c_str(), script[-1].filename.c_str(), script[-1].line);
    }

    script->line = 1;

    script->script_p = script->buffer;
    script->end_p = script->buffer + size;
}


/*
==============
LoadScriptFile
==============
*/
void LoadScriptFile(const char *filename, ScriptPathMode_t pathMode) {
    script = &scriptstack[0];
    AddScriptToStack(filename, pathMode);

    endofscript = false;
    tokenready = false;
}


/*
==============
==============
*/

constexpr uint32_t MAX_MACROS = 128;
std::vector<script_t *> macrolist;

void DefineMacro(char *macroname) {
    script_t *pmacro = new script_t();

    pmacro->filename = macroname;
    pmacro->line = script->line;
    pmacro->nummacroparams = 0;

    char *mp = pmacro->macrobuffer;
    char *cp = script->script_p;

    while (TokenAvailable()) {
        GetToken(false);

        if (token[0] == '\\' && token[1] == '\\') {
            break;
        }
        cp = script->script_p;

        pmacro->macroparam[pmacro->nummacroparams++] = mp;

        strcpy(mp, token);
        mp += strlen(token) + 1;

        if (mp >= pmacro->macrobuffer + sizeof(pmacro->macrobuffer))
            Error("Macro buffer overflow\n");
    }
    // roll back script_p to previous valid location
    script->script_p = cp;

    // find end of macro def
    while (*cp && *cp != '\n') {
        //Msg("%d ", *cp );
        if (*cp == '\\' && *(cp + 1) == '\\') {
            // skip till end of line
            while (*cp && *cp != '\n') {
                *cp = ' '; // replace with spaces
                cp++;
            }

            if (*cp) {
                cp++;
            }
        }
        else {
            cp++;
        }
    }

    int size = (cp - script->script_p);

    pmacro->buffer = (char *) malloc(size + 1);
    memcpy(pmacro->buffer, script->script_p, size);
    pmacro->buffer[size] = '\0';
    pmacro->end_p = &pmacro->buffer[size];

    macrolist.emplace_back(pmacro);
    if (macrolist.size() == MAX_MACROS)
        Error("script file exceeded MAX_MACROS");

    script->script_p = cp;
}


void DefineVariable(char *variablename) {
    variable_t v;

    v.param = strdup(variablename);

    GetToken(false);

    v.value = strdup(token);

    v.param_lcase = strlwr(strdup(v.param));

    for (int i = 0; i < g_definevariable.Count(); i++) {
        if (!V_strcmp(g_definevariable[i].value, v.value)) {
            Warning("\"$definevariable %s %s\" already exists as \"%s\".", v.param_lcase, v.value,
                    g_definevariable[i].param_lcase);
        }
    }

    g_definevariable.AddToTail(v);
}

void RedefineVariable(char *variablename) {
    variable_t v;

    v.param = strdup(variablename);

    GetToken(false);

    v.value = strdup(token);

    int nIdx = -1;
    for (int i = 0; i < g_definevariable.Count(); i++) {
        if (!V_strcmp(g_definevariable[i].param, v.param)) {
            nIdx = i;
            break;
        }
    }

    if (nIdx >= 0) {
        g_definevariable[nIdx] = v;
    }
    else {
        Error("Cannot redefine undefined variable \"%s\". Use $definevariable instead.\n", v.param);
    }
}

/*
==============
==============
*/
bool AddMacroToStack(char *macroname) {
    // lookup macro
    if (macroname[0] != '$')
        return false;

    int i;
    for (i = 0; i < macrolist.size(); i++) {
        if (strcmpi(macrolist[i]->filename.c_str(), &macroname[1]) == 0) {
            break;
        }
    }
    if (i == macrolist.size())
        return false;

    script_t *pmacro = macrolist[i];

    // get tokens
    script_t *pnext = script + 1;

    pnext++;
    if (pnext == scriptstack.data() + MAX_INCLUDES)
        Error("script file exceeded MAX_INCLUDES");

    // get tokens
    char *cp = pnext->macrobuffer;

    pnext->nummacroparams = pmacro->nummacroparams;

    for (i = 0; i < pnext->nummacroparams; i++) {
        GetToken(false);

        strcpy(cp, token);
        pnext->macroparam[i] = pmacro->macroparam[i];
        pnext->macrovalue[i] = cp;

        cp += strlen(token) + 1;

        if (cp >= pnext->macrobuffer + sizeof(pnext->macrobuffer))
            Error("Macro buffer overflow\n");
    }

    script = pnext;
    script->filename = pmacro->filename;

    int size = pmacro->end_p - pmacro->buffer;
    script->buffer = (char *) malloc(size + 1);
    memcpy(script->buffer, pmacro->buffer, size);
    pmacro->buffer[size] = '\0';
    script->script_p = script->buffer;
    script->end_p = script->buffer + size;
    script->line = pmacro->line;

    return true;
}


bool ExpandSubMacroToken(char *&token_p) {
    if (*token_p == '^') {
        token_p++;

        char *szPotentialVar = token_p;

        while (*token_p != '^') {
            token_p++;
        }

        *token_p = '\0';

        token_p = szPotentialVar;

        int index;
        for (index = 0; index < g_definevariable.Count(); index++) {
            if (!Q_strcmp(g_definevariable[index].param, szPotentialVar)) {
                strcpy(token, g_definevariable[index].value);
                return true;
            }
        }
    }
    return false;
}

bool ExpandMacroToken(char *&token_p) {
    if (script->nummacroparams && *script->script_p == '$') {
        char *cp = script->script_p + 1;

        while (*cp > 32 && *cp != '$') {
            cp++;
        }

        // found a word with $'s on either end?
        if (*cp != '$')
            return false;

        // get token pointer
        char *tp = script->script_p + 1;
        int len = (cp - tp);
        *(tp + len) = '\0';

        // lookup macro parameter
        int index = 0;
        for (index = 0; index < script->nummacroparams; index++) {
            if (stricmp(script->macroparam[index], tp) == 0)
                break;
        }
        if (index >= script->nummacroparams) {
            Error("unknown macro token \"%s\" in %s\n", tp, script->filename.c_str());
        }

        // paste token into
        len = strlen(script->macrovalue[index]);
        strcpy(token_p, script->macrovalue[index]);
        token_p += len;

        script->script_p = cp + 1;

        if (script->script_p >= script->end_p)
            Error("Macro expand overflow\n");

        if (token_p >= &token[MAXTOKEN])
            Error("Token too large on line %i\n", scriptline);

        return true;
    }
    return false;
}


/*
==============
==============
*/
// FIXME: this should create a new script context so the individual tokens in the variable can be parsed
bool ExpandVariableToken(char *&token_p) {
    if (*script->script_p == '$') {
        char *cp = script->script_p + 1;

        while (*cp > 32 && *cp != '$') {
            cp++;
        }

        // found a word with $'s on either end?
        if (*cp != '$')
            return false;

        // get token pointer
        char *tp = script->script_p + 1;
        int len = (cp - tp);
        *(tp + len) = '\0';

        // lookup macro parameter

        int index;
        for (index = 0; index < g_definevariable.Count(); index++) {
            // [wills] just strcmp here, this was doing nearest partial comparison before which could result in a false positive variable name match. Bad!
            if (!Q_strcmp(g_definevariable[index].param, tp))
                break;
        }

        // if we can't find the variable, try again without case sensitivity, then complain loudly if we find anything
        if (index >= g_definevariable.Count()) {
            for (index = 0; index < g_definevariable.Count(); index++) {
                char *tp_lower = strlwr(strdup(tp));
                if (!Q_strcmp(g_definevariable[index].param_lcase, tp_lower)) {
                    Warning(
                        "Unknown variable token fell back to case-insensitive match ( found: \"%s\", matched it to: \"%s\" ) in %s\n",
                        tp, g_definevariable[index].param, script->filename.c_str());
                    break;
                }
            }
        }

        if (index >= g_definevariable.Count()) {
            Error("unknown variable token \"%s\" in %s\n", tp, script->filename.c_str());
        }

        // paste token into
        len = strlen(g_definevariable[index].value);
        strcpy(token_p, g_definevariable[index].value);
        token_p += len;

        script->script_p = cp + 1;

        if (script->script_p >= script->end_p)
            Error("Macro expand overflow\n");

        if (token_p >= &token[MAXTOKEN])
            Error("Token too large on line %i\n", scriptline);

        return true;
    }
    return false;
}


/*
==============
ParseFromMemory
==============
*/
void ParseFromMemory(char *buffer, int size) {
    script = &scriptstack[0];
    script++;
    if (script == scriptstack.data() + MAX_INCLUDES)
        Error("script file exceeded MAX_INCLUDES");
    script->filename = "memory buffer";

    script->buffer = buffer;
    script->line = 1;
    script->script_p = script->buffer;
    script->end_p = script->buffer + size;

    endofscript = false;
    tokenready = false;
}


//-----------------------------------------------------------------------------
// Used instead of ParseFromMemory to temporarily add a memory buffer
// to the script stack.  ParseFromMemory just blows away the stack.
//-----------------------------------------------------------------------------
void PushMemoryScript(char *pszBuffer, const int nSize) {
    if (script == nullptr) {
        script = &scriptstack[0];
    }
    script++;
    if (script == scriptstack.data() + MAX_INCLUDES) {
        Error("script file exceeded MAX_INCLUDES");
    }
    script->filename = "memory buffer";

    script->buffer = pszBuffer;
    script->line = 1;
    script->script_p = script->buffer;
    script->end_p = script->buffer + nSize;

    endofscript = false;
    tokenready = false;
}


//-----------------------------------------------------------------------------
// Used after calling PushMemoryScript to clean up the memory buffer
// added to the script stack.  The normal end of script terminates
// all parsing at the end of a memory buffer even if there are more scripts
// remaining on the script stack
//-----------------------------------------------------------------------------
bool PopMemoryScript() {
    if (script->filename != "memory buffer")
        return false;

    if (script == &scriptstack[0]) {
        endofscript = true;
        return false;
    }
    script--;
    scriptline = script->line;

    endofscript = false;

    return true;
}


/*
==============
UnGetToken

Signals that the current token was not used, and should be reported
for the next GetToken.  Note that

GetToken (true);
UnGetToken ();
GetToken (false);

could cross a line boundary.
==============
*/
void UnGetToken() {
    tokenready = true;
}


qboolean EndOfScript(qboolean crossline) {
    if (!crossline)
        Error("Line %i is incomplete\n", scriptline);

    if (script->filename == "memory buffer") {
        endofscript = true;
        return false;
    }

    free(script->buffer);
    script->buffer = nullptr;
    if (script == scriptstack.data() + 1) {
        endofscript = true;
        return false;
    }
    script--;
    scriptline = script->line;
    // printf ("returning to %s\n", script->filename);
    return GetToken(crossline);
}

void AttemptConditionalInclude(void) {
    // Look for additional $include parameters, and only perform the include if the condition succeeds.
    // Right now, there's only a check for a file existing. This is a hacky way to get some logical
    // processing into qc, short of giving it full language-like arbitrary expression evaluation.

    GetToken(false);

    const std::string savedPath = token;

    // check for a conditional flag

    bool bConditionSuccess = true;

    if (TokenAvailable()) {
        GetToken(false);
        if (!stricmp(token, "iffileexists")) {
            bConditionSuccess = false;

            if (TokenAvailable()) {
                GetToken(false);
                bConditionSuccess = g_pFullFileSystem->FileExists(token) != 0;
            }

            printf("Condition 'iffileexists' for input '%s' is %s.\n", token, bConditionSuccess ? "True" : "False");
        }
        else {
            Error("Unknown $include parameter %s\n", token);
        }
    }

    printf("%s: %s\n", bConditionSuccess ? "Including" : "SKIPPING", savedPath.c_str());
    if (bConditionSuccess)
        AddScriptToStack(savedPath.c_str());
}

/*
==============
GetToken
==============
*/
qboolean GetToken(qboolean crossline) {
    char *token_p;

    if (tokenready) // is a token allready waiting?
    {
        tokenready = false;
        return true;
    }

    // printf("script_p %x (%x)\n", script->script_p, script->end_p ); fflush( stdout );

    if (script->script_p >= script->end_p) {
        return EndOfScript(crossline);
    }

    tokenready = false;

    // skip space, ctrl chars
skipspace:
    while (*script->script_p <= 32) {
        if (script->script_p >= script->end_p) {
            return EndOfScript(crossline);
        }
        if (*(script->script_p++) == '\n') {
            if (!crossline) {
                Error("Line %i is incomplete\n", scriptline);
            }
            scriptline = ++script->line;
        }
    }

    if (script->script_p >= script->end_p) {
        return EndOfScript(crossline);
    }

    // strip single line comments
    if (*script->script_p == ';' || *script->script_p == '#' || // semicolon and # is comment field
        (*script->script_p == '/' && *((script->script_p) + 1) == '/')) // also make // a comment field
    {
        if (!crossline)
            Error("Line %i is incomplete\n", scriptline);
        while (*script->script_p++ != '\n') {
            if (script->script_p >= script->end_p) {
                return EndOfScript(crossline);
            }
        }
        scriptline = ++script->line;
        goto skipspace;
    }

    //  strip out matching /* */ comments
    if (*script->script_p == '/' && *((script->script_p) + 1) == '*') {
        script->script_p += 2;
        while (*script->script_p != '*' || *((script->script_p) + 1) != '/') {
            if (*script->script_p++ != '\n') {
                if (script->script_p >= script->end_p) {
                    return EndOfScript(crossline);
                }

                scriptline = ++script->line;
            }
        }
        script->script_p += 2;
        goto skipspace;
    }

    // copy token to buffer
    token_p = token;

    if (*script->script_p == '"') {
        // quoted token
        script->script_p++;
        while (*script->script_p != '"') {
            *token_p++ = *script->script_p++;
            if (script->script_p == script->end_p)
                break;
            if (token_p == &token[MAXTOKEN])
                Error("Token too large on line %i\n", scriptline);
        }
        script->script_p++;
    }
    else // regular token
        while (*script->script_p > 32 && *script->script_p != ';') {
            if (!ExpandMacroToken(token_p)) {
                if (!ExpandVariableToken(token_p)) {
                    *token_p++ = *script->script_p++;
                    if (script->script_p == script->end_p)
                        break;
                    if (token_p == &token[MAXTOKEN])
                        Error("Token too large on line %i\n", scriptline);
                }
            }
        }

    // add null to end of token
    *token_p = 0;

    // quick hack: check for submacro variables
    char *token_submacro = token;
    if (ExpandSubMacroToken(token_submacro)) {
        return true;
    }

    // check for other commands
    if (!stricmp(token, "$include")) {
        AttemptConditionalInclude();
        return GetToken(crossline);
    }
    else if (!stricmp(token, "$definemacro")) {
        GetToken(false);
        DefineMacro(token);
        return GetToken(crossline);
    }
    else if (!stricmp(token, "$definevariable")) {
        GetToken(false);
        DefineVariable(token);
        return GetToken(crossline);
    }
    else if (!stricmp(token, "$redefinevariable")) {
        GetToken(false);
        RedefineVariable(token);
        return GetToken(crossline);
    }
    else if (AddMacroToStack(token)) {
        return GetToken(crossline);
    }

    return true;
}


/*
==============
GetExprToken - use C mathematical operator parsing rules to split tokens instead of whitespace
==============
*/
qboolean GetExprToken(qboolean crossline) {
    char *token_p;

    if (tokenready) // is a token allready waiting?
    {
        tokenready = false;
        return true;
    }

    if (script->script_p >= script->end_p)
        return EndOfScript(crossline);

    tokenready = false;

    //
    // skip space
    //
skipspace:
    while (*script->script_p <= 32) {
        if (script->script_p >= script->end_p)
            return EndOfScript(crossline);
        if (*script->script_p++ == '\n') {
            if (!crossline)
                Error("Line %i is incomplete\n", scriptline);
            scriptline = ++script->line;
        }
    }

    if (script->script_p >= script->end_p)
        return EndOfScript(crossline);

    if (*script->script_p == ';' || *script->script_p == '#' || // semicolon and # is comment field
        (*script->script_p == '/' && *((script->script_p) + 1) == '/')) // also make // a comment field
    {
        if (!crossline)
            Error("Line %i is incomplete\n", scriptline);
        while (*script->script_p++ != '\n')
            if (script->script_p >= script->end_p)
                return EndOfScript(crossline);
        goto skipspace;
    }

    //
    // copy token
    //
    token_p = token;

    if (*script->script_p == '"') {
        // quoted token
        script->script_p++;
        while (*script->script_p != '"') {
            *token_p++ = *script->script_p++;
            if (script->script_p == script->end_p)
                break;
            if (token_p == &token[MAXTOKEN])
                Error("Token too large on line %i\n", scriptline);
        }
        script->script_p++;
    }
    else {
        if (V_isalpha(*script->script_p) || *script->script_p == '_') {
            // regular token
            while (V_isalnum(*script->script_p) || *script->script_p == '_') {
                *token_p++ = *script->script_p++;
                if (script->script_p == script->end_p)
                    break;
                if (token_p == &token[MAXTOKEN])
                    Error("Token too large on line %i\n", scriptline);
            }
        }
        else if (V_isdigit(*script->script_p) || *script->script_p == '.') {
            // regular token
            while (V_isdigit(*script->script_p) || *script->script_p == '.') {
                *token_p++ = *script->script_p++;
                if (script->script_p == script->end_p)
                    break;
                if (token_p == &token[MAXTOKEN])
                    Error("Token too large on line %i\n", scriptline);
            }
        }
        else {
            // single char
            *token_p++ = *script->script_p++;
        }
    }

    *token_p = 0;

    if (!stricmp(token, "$include")) {
        AttemptConditionalInclude();
        return GetToken(crossline);
    }

    return true;
}


/*
==============
TokenAvailable

Returns true if there is another token on the line
==============
*/
qboolean TokenAvailable() {
    char *search_p;

    if (tokenready) // is a token allready waiting?
    {
        return true;
    }

    search_p = script->script_p;

    if (search_p >= script->end_p)
        return false;

    while (*search_p <= 32) {
        if (*search_p == '\n')
            return false;
        if (*search_p == '\r' && *(search_p + 1) == '\n')
            return false;
        search_p++;
        if (search_p == script->end_p)
            return false;
    }

    if (*search_p == ';' || *search_p == '#' || // semicolon and # is comment field
        (*search_p == '/' && *((search_p) + 1) == '/')) // also make // a comment field
        return false;

    return true;
}

qboolean GetTokenizerStatus(char **pFilename, int *pLine) {
    // is this the default state?
    if (!script)
        return false;

    if (script->script_p >= script->end_p)
        return false;

    if (pFilename) {
        *pFilename = script->filename.data();
    }
    if (pLine) {
        *pLine = script->line;
    }
    return true;
}


#include <stdio.h>
#include <stdlib.h>
#include "tier1/utlbuffer.h"
#include "filesystem/stdfile.h"

class CScriptLib : public IScriptLib {
public:
    virtual bool ReadFileToBuffer(const char *pSourceName, CUtlBuffer &buffer, bool bText = false,
                                  bool bNoOpenFailureWarning = false);

    virtual bool WriteBufferToFile(const char *pTargetName, CUtlBuffer &buffer, DiskWriteMode_t writeMode);

    virtual int FindFiles(char *pFileMask, bool bRecurse, CUtlVector<fileList_t> &fileList);

    virtual char *MakeTemporaryFilename(char const *pchModPath, char *pPath, int pathSize);

    virtual void DeleteTemporaryFiles(const char *pFileMask);

    virtual int CompareFileTime(const char *pFilenameA, const char *pFilenameB);

    virtual bool DoesFileExist(const char *pFilename);

private:
    int GetFileList(const char *pDirPath, const char *pPattern, CUtlVector<fileList_t> &fileList);

    void RecurseFileTree_r(const char *pDirPath, int depth, CUtlVector<CUtlString> &dirList);
};

static CScriptLib g_ScriptLib;
IScriptLib *scriptlib = &g_ScriptLib;
IScriptLib *g_pScriptLib = &g_ScriptLib;

//-----------------------------------------------------------------------------
// Existence check
//-----------------------------------------------------------------------------
bool CScriptLib::DoesFileExist(const char *pFilename) {
    return g_pFullFileSystem->FileExists(pFilename);
}

//-----------------------------------------------------------------------------
// Purpose: Helper utility, read file into buffer
//-----------------------------------------------------------------------------
bool CScriptLib::ReadFileToBuffer(const char *pSourceName, CUtlBuffer &buffer, bool bText, bool bNoOpenFailureWarning) {
    bool bSuccess = true;

    if (!g_pFullFileSystem->ReadFile(pSourceName, nullptr, buffer)) {
        if (!bNoOpenFailureWarning) {
            Msg("ReadFileToBuffer(): Error opening %s: %s\n", pSourceName, strerror(errno));
        }
        return false;
    }

    if (bText) {
        // force it into text mode
        buffer.SetBufferType(true, true);
    }
    else {
        buffer.SetBufferType(false, false);
    }

    return bSuccess;
}

//-----------------------------------------------------------------------------
// Purpose: Helper utility, Write buffer to file
//-----------------------------------------------------------------------------
bool CScriptLib::WriteBufferToFile(const char *pTargetName, CUtlBuffer &buffer, DiskWriteMode_t writeMode) {
    char *ptr;
    char dirPath[MAX_PATH]{0};

    bool bSuccess = true;

    // create path
    // prime and skip to first seperator
    strcpy(dirPath, pTargetName);
    ptr = strchr(dirPath, '\\');
    while (ptr) {
        ptr = strchr(ptr + 1, '\\');
        if (ptr) {
            *ptr = '\0';
            Q_mkdir(dirPath);
            *ptr = '\\';
        }
    }

    bool bDoWrite = false;
    if (writeMode == WRITE_TO_DISK_ALWAYS) {
        bDoWrite = true;
    }
    else if (writeMode == WRITE_TO_DISK_UPDATE) {
        if (DoesFileExist(pTargetName)) {
            bDoWrite = true;
        }
    }

    if (bDoWrite) {
        bSuccess = g_pFullFileSystem->WriteFile(pTargetName, nullptr, buffer);
    }

    return bSuccess;
}

//-----------------------------------------------------------------------------
// Returns -1, 0, or 1.
//-----------------------------------------------------------------------------
int CScriptLib::CompareFileTime(const char *pFilenameA, const char *pFilenameB) {
    int timeA = g_pFullFileSystem->GetFileTime((char *) pFilenameA);
    int timeB = g_pFullFileSystem->GetFileTime((char *) pFilenameB);

    if (timeA == -1) {
        // file a not exist
        timeA = 0;
    }
    if (timeB == -1) {
        // file b not exist
        timeB = 0;
    }

    if ((unsigned int) timeA < (unsigned int) timeB) {
        return -1;
    }
    else if ((unsigned int) timeA > (unsigned int) timeB) {
        return 1;
    }

    return 0;
}

//-----------------------------------------------------------------------------
// Make a temporary filename
//-----------------------------------------------------------------------------
char *CScriptLib::MakeTemporaryFilename(char const *pchModPath, char *pPath, int pathSize) {
    char *pBuffer = _tempnam(pchModPath, "mgd_");
    if (pBuffer[0] == '\\') {
        pBuffer++;
    }
    if (pBuffer[strlen(pBuffer) - 1] == '.') {
        pBuffer[strlen(pBuffer) - 1] = '\0';
    }
    V_snprintf(pPath, pathSize, "%s.tmp", pBuffer);

    free(pBuffer);

    return pPath;
}

//-----------------------------------------------------------------------------
// Delete temporary files
//-----------------------------------------------------------------------------
void CScriptLib::DeleteTemporaryFiles(const char *pFileMask) {
    const char *pEnv = getenv("temp");
    if (!pEnv) {
        pEnv = getenv("tmp");
    }

    if (pEnv) {
        char tempPath[MAX_PATH]{0};
        strcpy(tempPath, pEnv);
        V_AppendSlash(tempPath, sizeof(tempPath));
        strcat(tempPath, pFileMask);

        CUtlVector<fileList_t> fileList;
        FindFiles(tempPath, false, fileList);
        for (int i = 0; i < fileList.Count(); i++) {
            FS_RemoveFile(fileList[i].fileName.String());
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: Get list of files from current path that match pattern
//-----------------------------------------------------------------------------
int CScriptLib::GetFileList(const char *pDirPath, const char *pPattern, CUtlVector<fileList_t> &fileList) {
    char sourcePath[MAX_PATH]{0};
    char fullPath[MAX_PATH]{0};
    bool bFindDirs;

    fileList.Purge();

    strcpy(sourcePath, pDirPath);
    int len = (int) strlen(sourcePath);
    if (!len) {
        strcpy(sourcePath, ".\\");
    }
    else if (sourcePath[len - 1] != '\\') {
        sourcePath[len] = '\\';
        sourcePath[len + 1] = '\0';
    }

    strcpy(fullPath, sourcePath);
    if (pPattern[0] == '\\' && pPattern[1] == '\0') {
        // find directories only
        bFindDirs = true;
        strcat(fullPath, "*");
    }
    else {
        // find files, use provided pattern
        bFindDirs = false;
        strcat(fullPath, pPattern);
    }

    StdFindData_t findData;
    CStdFileFind *hFind = FS_FindFirst(fullPath, findData);
    if (!hFind) {
        return 0;
    }

    do {
        // the "\\" pattern lists directories only, every other pattern files only
        if (findData.bDirectory != bFindDirs)
            continue;

        char fileName[MAX_PATH]{0};
        strcpy(fileName, sourcePath);
        strcat(fileName, findData.cFileName);

        int j = fileList.AddToTail();
        fileList[j].fileName.Set(fileName);
        FileStat_t stat;
        fileList[j].timeWrite = FS_Stat(fileName, stat) ? (time_t) stat.nModifyTime : 0;
    } while (FS_FindNext(hFind, findData));

    FS_FindClose(hFind);


    return fileList.Count();
}

//-----------------------------------------------------------------------------
// Purpose: Recursively determine directory tree
//-----------------------------------------------------------------------------
void CScriptLib::RecurseFileTree_r(const char *pDirPath, int depth, CUtlVector<CUtlString> &dirList) {
    // recurse from source directory, get directories only
    CUtlVector<fileList_t> fileList;
    int dirCount = GetFileList(pDirPath, "\\", fileList);
    if (!dirCount) {
        // add directory name to search tree
        int j = dirList.AddToTail();
        dirList[j].Set(pDirPath);
        return;
    }

    for (int i = 0; i < dirCount; i++) {
        // form new path name, recurse into
        RecurseFileTree_r(fileList[i].fileName.String(), depth + 1, dirList);
    }

    int j = dirList.AddToTail();
    dirList[j].Set(pDirPath);
}

//-----------------------------------------------------------------------------
// Purpose: Generate a list of file matching mask
//-----------------------------------------------------------------------------
int CScriptLib::FindFiles(char *pFileMask, bool bRecurse, CUtlVector<fileList_t> &fileList) {
    char dirPath[MAX_PATH]{0};
    char pattern[MAX_PATH];
    char extension[MAX_PATH];

    // get path only
    strcpy(dirPath, pFileMask);
    V_StripFilename(dirPath);

    // get pattern only
    V_FileBase(pFileMask, pattern, sizeof(pattern));
    V_ExtractFileExtension(pFileMask, extension, sizeof(extension));
    if (extension[0]) {
        strcat(pattern, ".");
        strcat(pattern, extension);
    }

    if (!bRecurse) {
        GetFileList(dirPath, pattern, fileList);
    }
    else {
        // recurse and get the tree
        CUtlVector<fileList_t> tempList;
        CUtlVector<CUtlString> dirList;
        RecurseFileTree_r(dirPath, 0, dirList);
        for (int i = 0; i < dirList.Count(); i++) {
            // iterate each directory found
            tempList.Purge();
            tempList.EnsureCapacity(dirList.Count());

            GetFileList(dirList[i].String(), pattern, tempList);

            int start = fileList.AddMultipleToTail(tempList.Count());
            for (int j = 0; j < tempList.Count(); j++) {
                fileList[start + j] = tempList[j];
            }
        }
    }

    return fileList.Count();
}

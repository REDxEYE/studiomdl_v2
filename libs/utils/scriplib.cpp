//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#include "tier1/strtools.h"
#include "tier2/tier2.h"
#include "cmdlib.h"
#include "scriplib.h"
#include "tier0/platform.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include "tier1/utlbuffer.h"
#include "filesystem/stdfile.h"

/*
=============================================================================

						PARSING STUFF

=============================================================================
*/

// $include and macro calls nest at most this deep (guards against recursive includes).
constexpr size_t MAX_INCLUDES = 16;
static constexpr std::string_view MEMORY_SCRIPT_NAME = "memory buffer";

struct MacroArg {
    std::string name;
    std::string value;
};

struct ScriptTokenizer::Script {
    std::string filename; // UTF-8 path of the script, the macro name for macros, or "memory buffer"
    std::string owned;    // contents of files and expanded macros; memory scripts point into the caller's buffer
    char *buffer = nullptr, *script_p = nullptr, *end_p = nullptr;
    int line = 1;
    bool isMemory = false;
    std::vector<MacroArg> macroArgs; // arguments of an expanded macro, matched by "$name$" in its body
};

struct ScriptTokenizer::Macro {
    std::string name;
    std::vector<std::string> params;
    std::string body;
    int line = 0;
};

struct ScriptTokenizer::Variable {
    std::string name;
    std::string value;
};

// Bytes >= 0x80 are UTF-8 sequence bytes and belong to tokens; plain char is signed on most targets.
static bool IsSpace(char c) {
    return static_cast<unsigned char>(c) <= ' ';
}

static bool IsLineComment(const char *p, const char *end) {
    return *p == ';' || *p == '#' || (*p == '/' && p + 1 < end && p[1] == '/');
}

static bool IsBlockComment(const char *p, const char *end) {
    return *p == '/' && p + 1 < end && p[1] == '*';
}

// p points at "/*"; returns the position just past the matching "*/", or end if the comment is unterminated.
static char *SkipBlockComment(char *p, char *end) {
    for (p += 2; p + 1 < end; ++p) {
        if (p[0] == '*' && p[1] == '/')
            return p + 2;
    }
    return end;
}

static bool EqualsNoCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && (a.empty() || !V_strnicmp(a.data(), b.data(), a.size()));
}

ScriptTokenizer::ScriptTokenizer() = default;
ScriptTokenizer::~ScriptTokenizer() = default;

void ScriptTokenizer::SetLoadedCallback(LoadedCallback callback) {
    m_loadedCallback = std::move(callback);
}

void ScriptTokenizer::SetErrorHandler(ErrorHandler handler) {
    m_errorHandler = std::move(handler);
}

void ScriptTokenizer::ErrorV(std::string_view fmt, std::format_args args) const {
    std::string message = std::vformat(fmt, args);
    if (!m_stack.empty()) {
        const Script &s = Current();
        message = std::format("{}({}): - {}", s.filename, s.line, message);
    }
    if (!message.ends_with('\n'))
        message += '\n';

    if (m_errorHandler)
        m_errorHandler(message);
    ::Error("%s", message.c_str());
    exit(-1);
}

void ScriptTokenizer::BadNumber(std::string_view kind, std::string_view what) const {
    if (what.empty())
        Error("expected {}, got \"{}\"", kind, token);
    Error("{}: expected {}, got \"{}\"", what, kind, token);
}

void ScriptTokenizer::CheckTokenFits(std::string_view what, size_t maxLength) const {
    if (token.size() > maxLength)
        Error("{} \"{}\" is too long (max {} characters)", what.empty() ? "token" : what, token, maxLength);
}

bool ScriptTokenizer::GetPosition(std::string_view &filename, int &line) const {
    if (m_stack.empty())
        return false;
    filename = Current().filename;
    line = Current().line;
    return true;
}

ScriptTokenizer::Script &ScriptTokenizer::PushScript(std::string filename) {
    if (m_stack.size() == MAX_INCLUDES)
        Error("too many nested $include files and macros (max {})", MAX_INCLUDES - 1);
    auto &script = m_stack.emplace_back(std::make_unique<Script>());
    script->filename = std::move(filename);
    return *script;
}

void ScriptTokenizer::Close() {
    m_stack.clear();
    m_endOfScript = true;
    m_tokenReady = false;
    token.clear();
}

/*
==============
AddScriptToStack
==============
*/
void ScriptTokenizer::AddScriptToStack(const char *filename, ScriptPathMode_t pathMode) {
    std::string path = pathMode == SCRIPT_USE_RELATIVE_PATH ? std::string(filename) : std::string(ExpandPath(filename));

    char *data = nullptr;
    const int size = ::LoadFile(path.c_str(), (void **) &data);

    const std::string includedFrom = m_stack.empty() ? std::string() : Current().filename;
    const int includeLine = m_stack.empty() ? 0 : Current().line;

    Script &script = PushScript(std::move(path));
    script.owned.assign(data, size);
    free(data);
    script.buffer = script.script_p = script.owned.data();
    script.end_p = script.buffer + size;

    if (m_loadedCallback)
        m_loadedCallback(script.filename.c_str(), includedFrom.empty() ? nullptr : includedFrom.c_str(), includeLine);
}

void ScriptTokenizer::LoadFile(const char *filename, ScriptPathMode_t pathMode) {
    Close();
    AddScriptToStack(filename, pathMode);
    m_endOfScript = false;
}

void ScriptTokenizer::LoadMemory(std::string &buffer) {
    Close();
    Script &script = PushScript(std::string(MEMORY_SCRIPT_NAME));
    script.isMemory = true;
    script.buffer = script.script_p = buffer.data();
    script.end_p = script.buffer + buffer.size();
    m_endOfScript = false;
}

/*
==============
$definemacro <name> [params...] [\\]
    <body; continues on the next line while a line ends in "\\">
==============
*/
void ScriptTokenizer::DefineMacro(const std::string &name) {
    Script &script = Current();
    Macro macro;
    macro.name = name;
    macro.line = script.line;

    char *cp = script.script_p;
    while (TokenAvailable()) {
        GetToken(false);
        if (token[0] == '\\' && token[1] == '\\')
            break;
        cp = script.script_p;
        macro.params.emplace_back(token.view());
    }
    // roll back script_p to previous valid location
    script.script_p = cp;

    // find end of macro def
    while (cp < script.end_p && *cp != '\n') {
        if (*cp == '\\' && cp + 1 < script.end_p && *(cp + 1) == '\\') {
            // skip till end of line
            while (cp < script.end_p && *cp != '\n') {
                *cp = ' '; // replace with spaces
                cp++;
            }
            if (cp < script.end_p)
                cp++;
        }
        else {
            cp++;
        }
    }

    macro.body.assign(script.script_p, cp);
    // the continuation lines were consumed as part of the body
    script.line += static_cast<int>(std::count(script.script_p, cp, '\n'));
    script.script_p = cp;

    m_macros.push_back(std::move(macro));
}

void ScriptTokenizer::DefineVariable(const std::string &name, bool redefine) {
    if (!GetToken(false))
        Error("{} {}: missing value", redefine ? "$redefinevariable" : "$definevariable", name);

    auto existing = std::ranges::find(m_variables, name, &Variable::name);
    if (redefine) {
        if (existing == m_variables.end())
            Error("Cannot redefine undefined variable \"{}\". Use $definevariable instead.", name);
        existing->value = token.view();
        return;
    }

    for (const Variable &variable: m_variables) {
        if (variable.value == token.view()) {
            Warning("\"$definevariable %s %s\" already exists as \"%s\".\n", name.c_str(), token.c_str(),
                    variable.name.c_str());
        }
    }
    m_variables.push_back({name, std::string(token.view())});
}

/*
==============
A "$name" token naming a $definemacro: reads its arguments from the rest of the line and starts reading the body.
==============
*/
bool ScriptTokenizer::AddMacroToStack(const std::string &name) {
    if (name.size() < 2 || name[0] != '$')
        return false;

    const std::string_view macroName = std::string_view(name).substr(1);
    const auto macro = std::ranges::find_if(m_macros, [&](const Macro &m) { return EqualsNoCase(m.name, macroName); });
    if (macro == m_macros.end())
        return false;

    const size_t depth = m_stack.size();
    std::vector<MacroArg> args;
    args.reserve(macro->params.size());
    for (const std::string &param: macro->params) {
        if (!GetToken(false))
            Error("macro {}: missing argument for parameter \"{}\"", name, param);
        if (m_stack.size() != depth)
            Error("macro {}: arguments cannot expand macros or $include files", name);
        args.push_back({param, std::string(token.view())});
    }

    Script &script = PushScript(macro->name);
    script.owned = macro->body;
    script.buffer = script.script_p = script.owned.data();
    script.end_p = script.buffer + script.owned.size();
    script.line = macro->line;
    script.macroArgs = std::move(args);
    return true;
}

// "^name^" at the start of a token is replaced by the whole value of $definevariable name.
bool ScriptTokenizer::ExpandSubMacroToken() {
    if (token[0] != '^')
        return false;

    const char *close = strchr(token.c_str() + 1, '^');
    if (close == nullptr)
        return false;

    const std::string_view name(token.c_str() + 1, close - token.c_str() - 1);
    const auto variable = std::ranges::find(m_variables, name, &Variable::name);
    if (variable == m_variables.end())
        return false;
    token = variable->value;
    return true;
}

// Copies an expansion into the token being built; token_end is the last usable byte (kept for the terminator).
void ScriptTokenizer::PasteIntoToken(char *&token_p, const char *token_end, std::string_view value) {
    if (token_p + value.size() > token_end)
        Error("token too long (max {} characters)", MAXTOKEN - 1);
    memcpy(token_p, value.data(), value.size());
    token_p += value.size();
}

// script_p points at '$'; returns the "name" of a "$name$" word, or an empty view if the word has no closing '$'.
static std::string_view DollarWord(char *script_p, char *end_p, char *&close) {
    char *cp = script_p + 1;
    while (cp < end_p && !IsSpace(*cp) && *cp != '$')
        cp++;
    if (cp >= end_p || *cp != '$')
        return {};
    close = cp;
    return {script_p + 1, static_cast<size_t>(cp - script_p - 1)};
}

// "$name$" inside a macro body is replaced by the argument passed for that parameter.
bool ScriptTokenizer::ExpandMacroToken(char *&token_p, const char *token_end) {
    Script &script = Current();
    if (script.macroArgs.empty() || *script.script_p != '$')
        return false;

    char *close = nullptr;
    const std::string_view name = DollarWord(script.script_p, script.end_p, close);
    if (name.empty())
        return false;

    for (const MacroArg &arg: script.macroArgs) {
        if (EqualsNoCase(arg.name, name)) {
            PasteIntoToken(token_p, token_end, arg.value);
            script.script_p = close + 1;
            return true;
        }
    }
    // not a parameter: may still be a $definevariable
    return false;
}

// "$name$" is replaced by the value of $definevariable name.
// FIXME: this should create a new script context so the individual tokens in the variable can be parsed
bool ScriptTokenizer::ExpandVariableToken(char *&token_p, const char *token_end) {
    Script &script = Current();
    if (*script.script_p != '$')
        return false;

    char *close = nullptr;
    const std::string_view name = DollarWord(script.script_p, script.end_p, close);
    if (name.empty())
        return false;

    // [wills] exact match first; a nearest partial comparison could give a false positive variable name match.
    auto variable = std::ranges::find(m_variables, name, &Variable::name);
    if (variable == m_variables.end()) {
        // try again without case sensitivity, then complain loudly if we find anything
        variable = std::ranges::find_if(m_variables, [&](const Variable &v) { return EqualsNoCase(v.name, name); });
        if (variable == m_variables.end()) {
            Error("unknown variable \"${}$\"{}", name,
                  script.macroArgs.empty() ? "" : " (not a parameter of this macro either)");
        }
        Warning("%s(%d): unknown variable token fell back to case-insensitive match ( found: \"%.*s\", matched it to: \"%s\" )\n",
                script.filename.c_str(), script.line, static_cast<int>(name.size()), name.data(),
                variable->name.c_str());
    }

    PasteIntoToken(token_p, token_end, variable->value);
    script.script_p = close + 1;
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
void ScriptTokenizer::UnGetToken() {
    m_tokenReady = true;
}

// End of the current buffer while crossing lines: return to the including script, or flag the end of all input.
// The root stays loaded so errors at the end of the file still report it.
bool ScriptTokenizer::PopFinishedScript() {
    if (m_stack.size() == 1 || Current().isMemory) {
        m_endOfScript = true;
        return false;
    }
    m_stack.pop_back();
    return true;
}

// Skips whitespace and comments up to the next token. Returns false if there is none: at the end of the current
// line when !crossline (the newline or comment is left unread, so a later GetToken(true) still sees it), or at the
// end of all input.
bool ScriptTokenizer::SkipToToken(bool crossline) {
    if (m_stack.empty())
        return false;

    while (true) {
        Script &script = Current();
        if (script.script_p >= script.end_p) {
            if (!crossline || !PopFinishedScript())
                return false;
            continue;
        }

        const char c = *script.script_p;
        if (c == '\n') {
            if (!crossline)
                return false;
            script.script_p++;
            script.line++;
        }
        else if (IsSpace(c)) {
            script.script_p++;
        }
        else if (IsLineComment(script.script_p, script.end_p)) {
            if (!crossline)
                return false;
            while (script.script_p < script.end_p && *script.script_p != '\n')
                script.script_p++;
        }
        else if (IsBlockComment(script.script_p, script.end_p)) {
            char *after = SkipBlockComment(script.script_p, script.end_p);
            const auto newlines = std::count(script.script_p, after, '\n');
            if (!crossline && newlines > 0)
                return false;
            script.line += static_cast<int>(newlines);
            script.script_p = after;
        }
        else {
            return true;
        }
    }
}

// Reads a quoted token; script_p points at the opening quote.
void ScriptTokenizer::ReadQuotedToken(char *&token_p, const char *token_end) {
    Script &script = Current();
    const int startLine = script.line;
    script.script_p++;
    while (script.script_p < script.end_p && *script.script_p != '"') {
        if (*script.script_p == '\n')
            script.line++;
        if (token_p == token_end)
            Error("token too long (max {} characters)", MAXTOKEN - 1);
        *token_p++ = *script.script_p++;
    }
    if (script.script_p >= script.end_p)
        Error("unterminated quoted string starting on line {}", startLine);
    script.script_p++; // closing quote
}

void ScriptTokenizer::AttemptConditionalInclude() {
    // Look for additional $include parameters, and only perform the include if the condition succeeds.
    // Right now, there's only a check for a file existing. This is a hacky way to get some logical
    // processing into qc, short of giving it full language-like arbitrary expression evaluation.

    if (!GetToken(false))
        Error("$include: missing file name");

    const std::string savedPath(token.view());

    // check for a conditional flag
    bool bConditionSuccess = true;

    if (GetToken(false)) {
        if (!stricmp(token, "iffileexists")) {
            bConditionSuccess = false;

            if (GetToken(false)) {
                bConditionSuccess = g_pFullFileSystem->FileExists(token) != 0;
            }

            printf("Condition 'iffileexists' for input '%s' is %s.\n", token.c_str(), bConditionSuccess ? "True" : "False");
        }
        else {
            Error("Unknown $include parameter {}", token);
        }
    }

    printf("%s: %s\n", bConditionSuccess ? "Including" : "SKIPPING", savedPath.c_str());
    if (bConditionSuccess)
        AddScriptToStack(savedPath.c_str(), SCRIPT_USE_ABSOLUTE_PATH);
}

// Script directives handled inside the tokenizer. Returns true (with the next token's result) if token was one.
bool ScriptTokenizer::HandleDirective(bool crossline, bool &result) {
    if (!stricmp(token, "$include")) {
        AttemptConditionalInclude();
    }
    else if (!stricmp(token, "$definemacro")) {
        if (!GetToken(false))
            Error("$definemacro: missing macro name");
        DefineMacro(std::string(token.view()));
    }
    else if (!stricmp(token, "$definevariable") || !stricmp(token, "$redefinevariable")) {
        const bool redefine = !stricmp(token, "$redefinevariable");
        if (!GetToken(false))
            Error("{}: missing variable name", redefine ? "$redefinevariable" : "$definevariable");
        DefineVariable(std::string(token.view()), redefine);
    }
    else if (!AddMacroToStack(std::string(token.view()))) {
        return false;
    }
    result = GetToken(crossline);
    return true;
}

/*
==============
GetToken
==============
*/
bool ScriptTokenizer::GetToken(bool crossline) {
    if (m_tokenReady) // is a token allready waiting?
    {
        m_tokenReady = false;
        return true;
    }

    if (!SkipToToken(crossline)) {
        token.clear();
        return false;
    }

    // copy token to buffer
    char *token_p = token;
    const char *const token_end = token.data() + MAXTOKEN - 1;

    if (*Current().script_p == '"') {
        ReadQuotedToken(token_p, token_end);
    }
    else // regular token
    {
        while (Current().script_p < Current().end_p && !IsSpace(*Current().script_p) && *Current().script_p != ';') {
            if (ExpandMacroToken(token_p, token_end) || ExpandVariableToken(token_p, token_end))
                continue;
            if (token_p == token_end)
                Error("token too long (max {} characters)", MAXTOKEN - 1);
            *token_p++ = *Current().script_p++;
        }
    }

    // add null to end of token
    *token_p = 0;

    // quick hack: check for submacro variables
    if (ExpandSubMacroToken())
        return true;

    bool result = true;
    if (HandleDirective(crossline, result))
        return result;
    return true;
}

/*
==============
GetExprToken - use C mathematical operator parsing rules to split tokens instead of whitespace
==============
*/
bool ScriptTokenizer::GetExprToken(bool crossline) {
    if (m_tokenReady) // is a token allready waiting?
    {
        m_tokenReady = false;
        return true;
    }

    if (!SkipToToken(crossline)) {
        token.clear();
        return false;
    }

    Script &script = Current();
    char *token_p = token;
    const char *const token_end = token.data() + MAXTOKEN - 1;

    const auto copyWhile = [&](auto predicate) {
        while (script.script_p < script.end_p && predicate(*script.script_p)) {
            if (token_p == token_end)
                Error("token too long (max {} characters)", MAXTOKEN - 1);
            *token_p++ = *script.script_p++;
        }
    };

    const char c = *script.script_p;
    if (c == '"') {
        ReadQuotedToken(token_p, token_end);
    }
    else if (V_isalpha(c) || c == '_') {
        copyWhile([](char ch) { return V_isalnum(ch) || ch == '_'; });
    }
    else if (V_isdigit(c) || c == '.') {
        copyWhile([](char ch) { return V_isdigit(ch) || ch == '.'; });
    }
    else {
        // single char
        *token_p++ = *script.script_p++;
    }

    *token_p = 0;
    return true;
}

/*
==============
TokenAvailable

Returns true if there is another token on the line, i.e. GetToken(false) would succeed
==============
*/
bool ScriptTokenizer::TokenAvailable() const {
    if (m_tokenReady) // is a token allready waiting?
        return true;
    if (m_stack.empty())
        return false;

    const Script &script = Current();
    char *search_p = script.script_p;
    while (search_p < script.end_p) {
        if (*search_p == '\n' || IsLineComment(search_p, script.end_p))
            return false;
        if (IsBlockComment(search_p, script.end_p)) {
            char *after = SkipBlockComment(search_p, script.end_p);
            if (std::find(search_p, after, '\n') != after)
                return false;
            search_p = after;
            continue;
        }
        if (!IsSpace(*search_p))
            return true;
        search_p++;
    }
    return false;
}

void ScriptTokenizer::ExpectToken(std::string_view what) {
    const std::string previous(token.view()); // a failed read clears token
    if (GetToken(false))
        return;
    if (what.empty())
        Error("missing argument after \"{}\"", previous);
    Error("expected {} after \"{}\"", what, previous);
}

class CScriptLib : public IScriptLib {
public:
    bool ReadFileToBuffer(const char *pSourceName, CUtlBuffer &buffer, bool bText = false,
                          bool bNoOpenFailureWarning = false) override;

    bool WriteBufferToFile(const char *pTargetName, CUtlBuffer &buffer, DiskWriteMode_t writeMode) override;

    int FindFiles(const char *pFileMask, bool bRecurse, CUtlVector<fileList_t> &fileList) override;

    void DeleteTemporaryFiles(const char *pFileMask) override;

    int CompareFileTime(const char *pFilenameA, const char *pFilenameB) override;

    bool DoesFileExist(const char *pFilename) override;

private:
    int GetFileList(const char *pDirPath, const char *pPattern, CUtlVector<fileList_t> &fileList);

    void RecurseFileTree_r(const char *pDirPath, int depth, CUtlVector<CUtlString> &dirList);
};

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
            FS_RemoveFile(fileList[i].fileName.c_str());
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
        fileList[j].fileName = fileName;
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
        RecurseFileTree_r(fileList[i].fileName.c_str(), depth + 1, dirList);
    }

    int j = dirList.AddToTail();
    dirList[j].Set(pDirPath);
}

//-----------------------------------------------------------------------------
// Purpose: Generate a list of file matching mask
//-----------------------------------------------------------------------------
int CScriptLib::FindFiles(const char *pFileMask, bool bRecurse, CUtlVector<fileList_t> &fileList) {
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

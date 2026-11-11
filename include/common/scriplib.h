//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "tier0/basetypes.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include "tier1_v2/fixed_string.hpp"

enum ScriptPathMode_t {
    SCRIPT_USE_ABSOLUTE_PATH,
    SCRIPT_USE_RELATIVE_PATH
};


// scriplib.h

#define	MAXTOKEN	1024

//-----------------------------------------------------------------------------
// Tokenizer for QC files and other scripts. All parse state (include/macro stack, $definemacro and
// $definevariable tables, current token) lives in the object; create one per parse and pass it to the parsers.
//
// Tokens are separated by whitespace; "quoted" tokens may contain whitespace. ';', '#' and "//" start line
// comments, "/* */" block comments. $include, $definemacro, $definevariable, $redefinevariable and macro calls are
// expanded inside GetToken, so callers never see them.
//
// Errors (malformed scripts and the Expect* helpers) go to the error handler as "<file>(<line>): - <message>".
//-----------------------------------------------------------------------------
class ScriptTokenizer {
public:
    // Called after a script file is loaded: (loaded path, including file or nullptr for the root, line of the $include
    // or 0). Paths are UTF-8.
    using LoadedCallback = std::function<void(const char *loaded, const char *includedFrom, int includeLine)>;
    // Receives the complete message. Must not return; if it does, the process exits.
    using ErrorHandler = std::function<void(const std::string &message)>;

    // The current token. Callers may modify it in place (strlwr, ...).
    vstd::FixedString<MAXTOKEN> token;

    ScriptTokenizer();
    ~ScriptTokenizer();
    ScriptTokenizer(const ScriptTokenizer &) = delete;
    ScriptTokenizer &operator=(const ScriptTokenizer &) = delete;

    void SetLoadedCallback(LoadedCallback callback);
    void SetErrorHandler(ErrorHandler handler);

    // If pathMode is SCRIPT_USE_ABSOLUTE_PATH, then it uses ExpandPath() on the filename before
    // trying to open it. Otherwise, it passes the filename straight into the filesystem
    // (so you can leave it as a relative path). filename is UTF-8.
    void LoadFile(const char *filename, ScriptPathMode_t pathMode = SCRIPT_USE_ABSOLUTE_PATH);
    // Parses buffer, which must outlive the parse ($definemacro edits it in place). Errors report it as "memory buffer".
    void LoadMemory(std::string &buffer);
    // Releases every loaded script; GetPosition reports nothing afterwards. Macros and variables are kept.
    void Close();

    // Reads the next token into `token`.
    // crossline == false: only the rest of the current line is searched. At the end of the line (newline, comment or
    //   end of file) it returns false and leaves the line end unread.
    // crossline == true: lines, comments and the ends of $include files are skipped. Returns false (and EndOfScript()
    //   becomes true) at the end of the input.
    // On false `token` is empty.
    bool GetToken(bool crossline);
    // GetToken with C operator splitting ("a+b" is three tokens); used for flex rule expressions. Same contract.
    bool GetExprToken(bool crossline);
    // The current token is returned again by the next GetToken/GetExprToken. Note that GetToken(true); UnGetToken();
    // GetToken(false) can return a token from a later line.
    void UnGetToken();
    // True if GetToken(false) would return a token.
    bool TokenAvailable() const;
    bool EndOfScript() const { return m_endOfScript; }

    // GetToken(false) for a required argument. If the line has no more tokens: Error
    // "missing argument after "<previous token>"", or "expected <what> after "<previous token>"".
    void ExpectToken(std::string_view what = {});

    // ExpectToken, then the token converted to the storage type. A token that is not a whole number of that type (or out
    // of its range) is an Error naming `what`.
    template<std::integral T>
    void ExpectIntToken(const std::string_view what, T &out) {
        ExpectToken(what);
        out = TokenInt<T>(what);
    }
    template<std::integral T>
    void ExpectIntToken(T &out) { ExpectIntToken({}, out); }

    template<std::integral T>
    T ExpectIntToken(const std::string_view what) {
        ExpectToken(what);
        T out = TokenInt<T>(what);
        return out;
    }

    template<std::integral T>
    T ExpectIntToken() { T out; ExpectIntToken({}, out); return out; }


    template<std::floating_point T>
    void ExpectFloatToken(const std::string_view what, T &out) {
        ExpectToken(what);
        out = TokenFloat<T>(what);
    }
    template<std::floating_point T>
    void ExpectFloatToken(T &out) { ExpectFloatToken({}, out); }

    template<std::floating_point T>
    T ExpectFloatToken(const std::string_view what) {
        ExpectToken(what);
        T out = TokenFloat<T>(what);
        return out;
    }

    template<std::floating_point T>
    T ExpectFloatToken() { T out; ExpectFloatToken({}, out); return out; }

    // A token longer than the storage is an Error (never silently truncated).
    void ExpectStringToken(const std::string_view what, std::string &out) {
        ExpectToken(what);
        out = token.view();
    }

    void ExpectStringToken(std::string &out) { ExpectStringToken({}, out); }

    template<size_t N>
    void ExpectStringToken(const std::string_view what, vstd::FixedString<N> &out) {
        ExpectToken(what);
        CheckTokenFits(what, N - 1);
        out = token.view();
    }
    template<size_t N>
    void ExpectStringToken(vstd::FixedString<N> &out) { ExpectStringToken({}, out); }

    template<size_t N>
    void ExpectStringToken(const std::string_view what, char (&out)[N]) {
        ExpectToken(what);
        CheckTokenFits(what, N - 1);
        std::memcpy(out, token.c_str(), token.size() + 1);
    }
    template<size_t N>
    void ExpectStringToken(char (&out)[N]) { ExpectStringToken({}, out); }

    bool OptionalFlagToken(const std::string_view what, bool &out) {
        if (token.iequals(what)) {
            out = true;
            GetToken(false);
            return true;
        }
        return false;
    }

    bool MatchToken(const std::string_view what) {
        if (token.iequals(what)) {
            GetToken(false);
            return true;
        }
        return false;
    }

    // The current token as a number; anything else is an Error naming `what`.
    template<std::integral T = int>
    T TokenInt(const std::string_view what = {}) const {
        T value{};
        const std::string_view text = token.view();
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || ec != std::errc{} || end != text.data() + text.size())
            BadNumber("integer", what);
        return value;
    }

    template<std::floating_point T = float>
    T TokenFloat(const std::string_view what = {}) const {
        T value{};
        const std::string_view text = token.view();
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
            BadNumber("number", what);
        return value;
    }

    // Fatal error at the current position: "<file>(<line>): - <message>". std::format syntax.
    template<typename... Args>
    [[noreturn]] void Error(std::format_string<Args...> fmt, Args &&...args) const {
        ErrorV(fmt.get(), std::make_format_args(args...));
    }
    [[noreturn]] void ErrorV(std::string_view fmt, std::format_args args) const;

    // Position of the script being read (the innermost $include or macro). False if nothing is loaded.
    bool GetPosition(std::string_view &filename, int &line) const;

private:
    struct Script;
    struct Macro;
    struct Variable;

    [[noreturn]] void BadNumber(std::string_view kind, std::string_view what) const;
    void CheckTokenFits(std::string_view what, size_t maxLength) const;

    Script &Current() const { return *m_stack.back(); }
    Script &PushScript(std::string filename);
    void AddScriptToStack(const char *filename, ScriptPathMode_t pathMode);
    bool PopFinishedScript();
    bool SkipToToken(bool crossline);
    void ReadQuotedToken(char *&token_p, const char *token_end);
    bool ExpandMacroToken(char *&token_p, const char *token_end);
    bool ExpandVariableToken(char *&token_p, const char *token_end);
    bool ExpandSubMacroToken();
    void PasteIntoToken(char *&token_p, const char *token_end, std::string_view value);
    bool HandleDirective(bool crossline, bool &result);
    void AttemptConditionalInclude();
    void DefineMacro(const std::string &name);
    void DefineVariable(const std::string &name, bool redefine);
    bool AddMacroToStack(const std::string &name);

    std::vector<std::unique_ptr<Script>> m_stack; // [0] is the root script, back() the one being read
    std::vector<Macro> m_macros;
    std::vector<Variable> m_variables;
    bool m_tokenReady = false; // only true if UnGetToken was just called
    bool m_endOfScript = true;
    LoadedCallback m_loadedCallback;
    ErrorHandler m_errorHandler;
};


class CUtlBuffer;

enum DiskWriteMode_t {
    WRITE_TO_DISK_NEVER,
    WRITE_TO_DISK_ALWAYS,
    WRITE_TO_DISK_UPDATE, // file must exist
};

struct fileList_t {
    std::string fileName;
    time_t timeWrite;
};

class IScriptLib {
public:
    virtual ~IScriptLib() = default;

    virtual bool ReadFileToBuffer(const char *pSourceName, CUtlBuffer &buffer, bool bText = false,
                                  bool bNoOpenFailureWarning = false) = 0;

    virtual bool WriteBufferToFile(const char *pTargetName, CUtlBuffer &buffer, DiskWriteMode_t writeMode) = 0;

    virtual int FindFiles(const char *pFileMask, bool bRecurse, CUtlVector<fileList_t> &fileList) = 0;

    virtual void DeleteTemporaryFiles(const char *pFileMask) = 0;

    virtual int CompareFileTime(const char *pFilenameA, const char *pFilenameB) = 0;

    virtual bool DoesFileExist(const char *pFilename) = 0;
};

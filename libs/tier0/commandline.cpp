//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//===========================================================================//

#include "pch_tier0.h"
#include "tier0/icommandline.h"
#include "tier1_v2/string_utils.hpp"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <vector>
#include <string_view>

#include "tier1/strtools.h" // this is included for the definition of V_isspace()
#include "dbg.h"

//-----------------------------------------------------------------------------
// Purpose: Implements ICommandLine
//-----------------------------------------------------------------------------
class CCommandLine : public ICommandLine {
public:
    // Construction
    CCommandLine();

    virtual ~CCommandLine();

    // Implements ICommandLine
    void CreateCmdLine(const char* commandline) override;

    void CreateCmdLine(int argc, char** argv) override;

    std::string_view GetCmdLine() const override;

    std::pair<std::string_view, std::string_view> CheckParm(std::string_view key, bool extract_value) const override;

    // A bool return of whether param exists, useful for just checking if param that is just a flag is set
    bool HasParm(const char* psz) const override;

    void RemoveParm(std::string_view parm) override;

    void AppendParm(std::string_view key, std::string_view values) override;

    uint32_t ParmCount() const override;

    std::optional<uint32_t> FindParm(std::string_view psz) const override;

    std::optional<std::string_view> GetParm(uint32_t index) const override;

    std::optional<std::string_view> ParmValue(std::string_view key) const override;

    int ParmValue(std::string_view key, int default_value) const override;

    float ParmValue(std::string_view key, float default_value) const override;

    void SetParm(uint32_t index, std::string_view param) override;

    const std::vector<std::string_view>& GetParms() const override { return m_ppParms; }

private:
    enum {
        MAX_PARAMETER_LEN = 128,
        MAX_PARAMETERS = 256,
    };

    // When the commandline contains @name, it reads the parameters from that file
    void LoadParametersFromFile(const char*&pSrc, char*&pDst, intptr_t maxDestLen, bool bInQuotes);

    // Parse command line...
    void ParseCommandLine();

    // Frees the command line arguments
    void CleanUpParms();

    // Adds an argument..
    void AddArgument(std::string_view arg);

    // Copy of actual command line
    std::string m_cmdLine;

    std::vector<std::string> m_addedParamStorage;

    // Pointers to each argument...
    std::vector<std::string_view> m_ppParms;
};


//-----------------------------------------------------------------------------
// Instance singleton and expose interface to rest of code
//-----------------------------------------------------------------------------
static CCommandLine g_CmdLine;

ICommandLine* CommandLine() {
    return &g_CmdLine;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CCommandLine::CCommandLine() {
    m_cmdLine.clear();
    m_ppParms.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CCommandLine::~CCommandLine() {
    CleanUpParms();
    m_cmdLine.clear();
}


//-----------------------------------------------------------------------------
// Read commandline from file instead...
//-----------------------------------------------------------------------------
void CCommandLine::LoadParametersFromFile(const char*&pSrc, char*&pDst, intptr_t maxDestLen, bool bInQuotes) {
    // Suck out the file name
    char szFileName[MAX_PATH];
    char* pOut;
    char* pDestStart = pDst;

    if (maxDestLen < 3)
        return;

    // Skip the @ sign
    pSrc++;

    pOut = szFileName;

    char terminatingChar = ' ';
    if (bInQuotes)
        terminatingChar = '\"';

    while (*pSrc && *pSrc != terminatingChar) {
        *pOut++ = *pSrc++;
        if ((pOut - szFileName) >= (MAX_PATH - 1))
            break;
    }

    *pOut = '\0';

    // Skip the space after the file name
    if (*pSrc)
        pSrc++;

    // Now read in parameters from file
    FILE* fp = fopen(szFileName, "r");
    if (fp) {
        char c;
        c = (char) fgetc(fp);
        while (c != EOF) {
            // Turn return characters into spaces
            if (c == '\n')
                c = ' ';

            *pDst++ = c;

            // Don't go past the end, and allow for our terminating space character AND a terminating null character.
            if ((pDst - pDestStart) >= (maxDestLen - 2))
                break;

            // Get the next character, if there are more
            c = (char) fgetc(fp);
        }

        // Add a terminating space character
        *pDst++ = ' ';

        fclose(fp);
    }
    else {
        printf("Parameter file '%s' not found, skipping...", szFileName);
    }
}


//-----------------------------------------------------------------------------
// Creates a command line from the arguments passed in
//-----------------------------------------------------------------------------
void CCommandLine::CreateCmdLine(int argc, char** argv) {
    char cmdline[8192];
    cmdline[0] = 0;
    const int MAX_CHARS = sizeof(cmdline) - 1;
    cmdline[MAX_CHARS] = 0;
    for (int i = 0; i < argc; ++i) {
        strncat(cmdline, "\"", MAX_CHARS);
        strncat(cmdline, argv[i], MAX_CHARS);
        strncat(cmdline, "\"", MAX_CHARS);
        strncat(cmdline, " ", MAX_CHARS);
    }

    CreateCmdLine(cmdline);
}


//-----------------------------------------------------------------------------
// Purpose: Create a command line from the passed in string
//  Note that if you pass in a @filename, then the routine will read settings
//  from a file instead of the command line
//-----------------------------------------------------------------------------
void CCommandLine::CreateCmdLine(const char* commandline) {
    const bool bNoAutoArgs = (Plat_GetEnv("autoargs")) == nullptr;
    if (!m_cmdLine.empty()) {
        m_cmdLine.clear();
    }

    char szFull[4096];

    char* pDst = szFull;
    const char* pSrc = commandline;

    bool bInQuotes = false;
    const char* pInQuotesStart = nullptr;
    while (*pSrc) {
        // Is this an unslashed quote?
        if (*pSrc == '"') {
            if (pSrc == commandline || (pSrc[-1] != '/' && pSrc[-1] != '\\')) {
                bInQuotes = !bInQuotes;
                pInQuotesStart = pSrc + 1;
            }
        }

        if (!bNoAutoArgs && *pSrc == '@') {
            if (pSrc == commandline || (!bInQuotes && V_isspace(pSrc[-1])) || (bInQuotes && pSrc == pInQuotesStart)) {
                LoadParametersFromFile(pSrc, pDst, sizeof(szFull) - (pDst - szFull), bInQuotes);
                continue;
            }
        }

        // Don't go past the end.
        if ((pDst - szFull) >= (sizeof(szFull) - 1))
            break;

        *pDst++ = *pSrc++;
    }

    *pDst = '\0';

    m_cmdLine = std::string(szFull, strlen(szFull) + 1);

    ParseCommandLine();
}


//-----------------------------------------------------------------------------
// Purpose: Remove specified string ( and any args attached to it ) from command line
// Input  : *pszParm -
//-----------------------------------------------------------------------------
void CCommandLine::RemoveParm(std::string_view pszParm) {
    if (m_cmdLine.empty() || pszParm.empty())
        return;

    const size_t parmLen = pszParm.size();
    ptrdiff_t pos = 0;

    while (pos < m_cmdLine.size()) {
        auto found = std::search(
            m_cmdLine.begin() + pos, m_cmdLine.end(), pszParm.begin(), pszParm.end(),
            [](const uint8_t a, const uint8_t b) {
                return std::tolower(a) == std::tolower(b);
            }
        );

        if (found == m_cmdLine.end())
            break;

        size_t start = found - m_cmdLine.begin();
        size_t next = start + 1;
        const bool hadQuote = start > 0 && m_cmdLine[start - 1] == '"';

        next = m_cmdLine.find_first_of(" \"", next);
        if (next == std::string::npos)
            next = m_cmdLine.size();

        if (next - start > parmLen) {
            pos = static_cast<ptrdiff_t>(next);
            continue;
        }

        next = m_cmdLine.find_first_of("-+", next);
        if (next == std::string::npos)
            next = m_cmdLine.size();

        if (hadQuote)
            --start;

        m_cmdLine.erase(start, next - start);
        pos = start;
    }

    const size_t last = m_cmdLine.find_last_not_of(' ');
    m_cmdLine.erase(last == std::string::npos ? 0 : last + 1);

    ParseCommandLine();
}

//-----------------------------------------------------------------------------
// Purpose: Append parameter and argument values to command line
// Input  : *pszParm -
//			*pszValues -
//-----------------------------------------------------------------------------
void CCommandLine::AppendParm(std::string_view key, std::string_view values) {
    size_t nNewLength = 0;
    char* pCmdString;

    nNewLength = key.size(); // Parameter.
    if (!values.empty())
        nNewLength += values.size() + 1; // Values + leading space character.
    nNewLength++; // Terminal 0;

    if (m_cmdLine.empty()) {
        m_cmdLine = std::string(key);
        if (!values.empty()) {
            m_cmdLine += ' ';
            m_cmdLine += values;
        }

        ParseCommandLine();
        return;
    }

    // Remove any remnants from the current Cmd Line.
    RemoveParm(key);

    nNewLength += m_cmdLine.size() + 1 + 1;

    pCmdString = new char[nNewLength];
    memset(pCmdString, 0, nNewLength);
    m_cmdLine += ' ';
    m_cmdLine += key;
    if (!values.empty()) {
        m_cmdLine += ' ';
        m_cmdLine += values;
    }

    ParseCommandLine();
}


//-----------------------------------------------------------------------------
// Purpose: Return current command line
// Output : std::string_view
//-----------------------------------------------------------------------------
std::string_view CCommandLine::GetCmdLine() const {
    return m_cmdLine;
}


//-----------------------------------------------------------------------------
// Purpose: Search for the parameter in the current commandline
// Input  : key - param name to look for
//			extract_value - whether to extract the value
// Output : pair of param and it's value
//-----------------------------------------------------------------------------
std::pair<std::string_view, std::string_view> CCommandLine::CheckParm(const std::string_view key,
                                                                      const bool extract_value) const {
    if (const auto i = FindParm(key)) {
        if (extract_value) {
            if (*i + 1 >= m_ppParms.size()) {
                return {m_ppParms[*i], ""};
            }
            return {m_ppParms[*i], m_ppParms[*i + 1]};
        }

        return {m_ppParms[*i], ""};
    }
    return {};
}


//-----------------------------------------------------------------------------
// Adds an argument..
//-----------------------------------------------------------------------------
void CCommandLine::AddArgument(const std::string_view arg) {
    if (arg.empty())
        return;

    if (m_ppParms.size() >= MAX_PARAMETERS)
        Error("CCommandLine::AddArgument: exceeded %d parameters", MAX_PARAMETERS);

    m_ppParms.emplace_back(arg);
}


//-----------------------------------------------------------------------------
// Parse command line...
//-----------------------------------------------------------------------------
void CCommandLine::ParseCommandLine() {
    CleanUpParms();
    if (m_cmdLine.empty())
        return;

    const char* pChar = m_cmdLine.c_str();
    while (*pChar && V_isspace(*pChar)) {
        ++pChar;
    }

    bool bInQuotes = false;
    const char* pFirstLetter = nullptr;
    for (; *pChar; ++pChar) {
        if (bInQuotes) {
            if (*pChar != '\"')
                continue;
            const intptr_t size = pChar - pFirstLetter;
            AddArgument(std::string_view(pFirstLetter, size));
            pFirstLetter = nullptr;
            bInQuotes = false;
            continue;
        }

        // Haven't started a word yet...
        if (!pFirstLetter) {
            if (*pChar == '\"') {
                bInQuotes = true;
                pFirstLetter = pChar + 1;
                continue;
            }

            if (V_isspace(*pChar))
                continue;

            pFirstLetter = pChar;
            continue;
        }

        // Here, we're in the middle of a word. Look for the end of it.
        if (V_isspace(*pChar)) {
            intptr_t size = pChar - pFirstLetter;
            AddArgument(std::string_view(pFirstLetter, size));
            pFirstLetter = nullptr;
        }
    }

    if (pFirstLetter) {
        intptr_t size = strlen(pFirstLetter);
        AddArgument(std::string_view(pFirstLetter, size));
    }
}


//-----------------------------------------------------------------------------
// Individual command line arguments
//-----------------------------------------------------------------------------
void CCommandLine::CleanUpParms() {
    m_ppParms.clear();
    m_addedParamStorage.clear();
}


//-----------------------------------------------------------------------------
// Returns individual command line arguments
//-----------------------------------------------------------------------------
uint32_t CCommandLine::ParmCount() const {
    return m_ppParms.size();
}

std::optional<uint32_t> CCommandLine::FindParm(const std::string_view psz) const {
    // Start at 1 so as to not search the exe name
    for (int i = 1; i < m_ppParms.size(); ++i) {
        if (vstd::iequals(psz, m_ppParms[i]))
            return i;
    }
    return std::nullopt;
}

bool CCommandLine::HasParm(const char* psz) const {
    return (FindParm(psz) != 0);
}

std::optional<std::string_view> CCommandLine::GetParm(const uint32_t index) const {
    Assert((nIndex >= 0) && (nIndex < m_ppParms.size()));
    if (index < 0 || (index >= m_ppParms.size()))
        return std::nullopt;
    return m_ppParms[index];
}

void CCommandLine::SetParm(const uint32_t index, const std::string_view param) {
    if (!param.empty()) {
        Assert((index >= 0) && (index < m_ppParms.size()));
        if (index < m_ppParms.size()) {
            m_ppParms[index] = m_addedParamStorage.emplace_back(param);
        }
    }
}


//-----------------------------------------------------------------------------
// Returns the argument after the one specified, or the default if not found
//-----------------------------------------------------------------------------
std::optional<std::string_view> CCommandLine::ParmValue(const std::string_view key) const {
    if (const auto index = FindParm(key)) {
        // Probably another cmdline parameter instead of a valid arg if it starts with '+' or '-'
        const auto &param = m_ppParms[*index + 1];
        if (param[0] == '-' || param[0] == '+')
            return std::nullopt;

        return {param};
    }
    return std::nullopt;
}

int CCommandLine::ParmValue(const std::string_view key, const int default_value) const {
    if (const auto index = FindParm(key)) {
        // Probably another cmdline parameter instead of a valid arg if it starts with '+' or '-'
        const auto &param = m_ppParms[*index + 1];
        if (param[0] == '-' || param[0] == '+')
            return default_value;

        return vstd::str_to_int<int>(param);
    }
    return default_value;
}

float CCommandLine::ParmValue(const std::string_view key, float default_value) const {
    if (const auto index = FindParm(key)) {
        // Probably another cmdline parameter instead of a valid arg if it starts with '+' or '-'
        const auto &param = m_ppParms[*index + 1];
        if (param[0] == '-' || param[0] == '+')
            return default_value;

        return vstd::str_to_float<float>(param);
    }
    return default_value;
}

//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#pragma once
#include <optional>
#include <string_view>
#include <vector>
#include "platform.h"


//-----------------------------------------------------------------------------
// Purpose: Interface to engine command line
//-----------------------------------------------------------------------------
class ICommandLine {
public:
    virtual ~ICommandLine() = default;

    virtual void CreateCmdLine(const char* commandline) = 0;

    virtual void CreateCmdLine(int argc, char** argv) = 0;

    virtual std::string_view GetCmdLine() const = 0;

    // Check whether a particular parameter exists
    virtual std::pair<std::string_view, std::string_view> CheckParm(std::string_view key, bool extract_value) const
    = 0;

    virtual std::string_view CheckParm(const std::string_view key) const { return CheckParm(key, false).first; };

    // A bool return of whether param exists, useful for just checking if param that is just a flag is set
    virtual bool HasParm(const char* psz) const = 0;

    virtual void RemoveParm(std::string_view parm) = 0;

    virtual void AppendParm(std::string_view key, std::string_view values) = 0;

    // Returns the argument after the one specified, or the default if not found
    virtual std::optional<std::string_view> ParmValue(std::string_view key) const = 0;

    virtual std::string_view ParmValue(const std::string_view key, std::string_view default_value) const {
        return ParmValue(key).value_or(default_value);
    };


    virtual int ParmValue(std::string_view psz, int nDefaultVal) const = 0;

    virtual float ParmValue(std::string_view key, float default_value) const = 0;

    // Gets at particular parameters
    virtual uint32_t ParmCount() const = 0;

    virtual std::optional<uint32_t> FindParm(std::string_view psz) const = 0; // Returns std::nullopt if not found.
    virtual std::optional<std::string_view> GetParm(uint32_t nIndex) const = 0;

    // copies the string passed
    virtual void SetParm(uint32_t index, std::string_view param) =0;

    virtual const std::vector<std::string_view>& GetParms() const = 0;
};

//-----------------------------------------------------------------------------
// Gets a singleton to the commandline interface
// NOTE: The #define trickery here is necessary for backwards compat:
// this interface used to lie in the vstdlib library.
//-----------------------------------------------------------------------------
PLATFORM_INTERFACE ICommandLine* CommandLine();

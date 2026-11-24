#include "harness.hpp"
#include "tier1/utlsymbol.h"
#include "tier1/utlsymbollarge.h"

#include <cstring>
#include <string>

SMDL_TEST(symbol_small_table_interning) {
    CUtlSymbolTable table(0, 16, true);
    const auto first = table.AddString("Alpha");
    const char *firstString = table.String(first);
    Require(table.Find("aLPHa") == first && table.AddString(std::string_view("ALPHA")) == first,
            "case-insensitive small symbols must intern to the same handle");

    for (int index = 0; index < 300; ++index)
        table.AddString("entry_" + std::to_string(index));

    Require(table.GetNumStrings() == 301 && table.String(first) == firstString &&
            std::strcmp(firstString, "Alpha") == 0,
            "small symbol handles and strings must survive pool and lookup growth");
    table.RemoveAll();
    Require(!table.Find("Alpha").IsValid() && table.GetNumStrings() == 0,
            "removing small symbols must clear lookup and pool");
    CUtlSymbolTable caseSensitive;
    Require(caseSensitive.AddString("Alpha") != caseSensitive.AddString("alpha"),
            "case-sensitive small symbols must remain distinct");
    Require(!caseSensitive.AddString(nullptr).IsValid() &&
            !caseSensitive.AddString(std::string_view{}).IsValid() &&
            caseSensitive.AddString("").IsValid(),
            "small symbol overloads must distinguish absent and empty C strings");
}

SMDL_TEST(symbol_large_table_interning) {
    CUtlSymbolTableLarge_CI table;
    const auto first = table.AddString("Alpha");
    const char *firstString = first.String();
    Require(table.Find("aLPHa") == first && table.AddString("ALPHA") == first,
            "case-insensitive large symbols must intern to the same pointer");

    for (int index = 0; index < 300; ++index)
        table.AddString("entry_" + std::to_string(index));

    CUtlSymbolLarge elements[1];
    Require(table.GetNumStrings() == 301 && table.GetElements(0, 1, elements) == 1 && elements[0] == first &&
            table.GetElementString(0) == firstString && std::strcmp(firstString, "Alpha") == 0,
            "large symbol pointers and enumeration must survive pool and lookup growth");
    Require(table.GetElements(1, 1, elements) == 1 && elements[0] == table.Find("entry_0"),
            "large symbol enumeration must start at the requested index");
    table.RemoveAll();
    Require(!table.Find("Alpha").IsValid() && table.GetNumStrings() == 0,
            "removing large symbols must clear lookup and pool");
    CUtlSymbolTableLarge caseSensitive;
    Require(caseSensitive.AddString("Alpha") != caseSensitive.AddString("alpha"),
            "case-sensitive large symbols must remain distinct");
    Require(caseSensitive.AddString({}).IsValid() && caseSensitive.Find({}).IsValid(),
            "large tables must intern and find an empty view");
}

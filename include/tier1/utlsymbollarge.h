//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Defines a large symbol table (intp sized handles, can store more than 64k strings)
//
// $Header: $
// $NoKeywords: $
//===========================================================================//

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>
#include "tier1/generichash.h"
#include "tier1/strtools.h"
//-----------------------------------------------------------------------------
// CUtlSymbolTableLarge:
// description:
//    This class defines a symbol table, which allows us to perform mappings
//    of strings to symbols and back. 
// 
//    This class stores the strings in a series of string pools. The returned CUtlSymbolLarge is just a pointer
//     to the string data, the hash precedes it in memory and is used to speed up searching, etc.
//-----------------------------------------------------------------------------

using UtlSymLargeId_t = intptr_t;

#define UTL_INVAL_SYMBOL_LARGE  ((UtlSymLargeId_t)~0)

class CUtlSymbolLarge {
public:
    // constructor, destructor
    CUtlSymbolLarge() {
        u.m_Id = UTL_INVAL_SYMBOL_LARGE;
    }

    CUtlSymbolLarge(const UtlSymLargeId_t id) {
        u.m_Id = id;
    }

    CUtlSymbolLarge(CUtlSymbolLarge const &sym) {
        u.m_Id = sym.u.m_Id;
    }

    // operator=
    CUtlSymbolLarge& operator=(CUtlSymbolLarge const &src) {
        u.m_Id = src.u.m_Id;
        return *this;
    }

    // operator==
    bool operator==(CUtlSymbolLarge const &src) const {
        return u.m_Id == src.u.m_Id;
    }

    // operator==
    bool operator==(UtlSymLargeId_t const &src) const {
        return u.m_Id == src;
    }

    // operator==
    bool operator!=(CUtlSymbolLarge const &src) const {
        return u.m_Id != src.u.m_Id;
    }

    // operator==
    bool operator!=(UtlSymLargeId_t const &src) const {
        return u.m_Id != src;
    }

    // Gets at the symbol
    operator UtlSymLargeId_t const() const {
        return u.m_Id;
    }

    // Gets the string associated with the symbol
    const char* String() const {
        if (u.m_Id == UTL_INVAL_SYMBOL_LARGE)
            return "";
        return u.m_pAsString;
    }

    std::string_view view() const {
        if (u.m_Id == UTL_INVAL_SYMBOL_LARGE)
            return "";
        return u.m_pAsString;
    }

    bool IsValid() const {
        return u.m_Id != UTL_INVAL_SYMBOL_LARGE;
    }

    // Disallowed
    CUtlSymbolLarge(const char* pStr) = delete; // they need to go through the table to assign the ptr
    CUtlSymbolLarge(std::string_view pStr) = delete; // they need to go through the table to assign the ptr
    bool operator==(const char* pStr) const = delete;

    // disallow since we don't know if the table this is from was case sensitive or not... maybe we don't care

    union {
        UtlSymLargeId_t m_Id;
        char const* m_pAsString;
    } u{UTL_INVAL_SYMBOL_LARGE};
};

#define MIN_STRING_POOL_SIZE	2048

inline uint32_t CUtlSymbolLarge_Hash(bool CASEINSENSITIVE, const std::string_view pString) {
    return (CASEINSENSITIVE ? HashStringCaseless(pString) : HashString(pString));
}

typedef uint32_t LargeSymbolTableHashDecoration_t;

// The structure consists of the hash immediately followed by the string data
struct CUtlSymbolTableLargeBaseTreeEntry_t {
    LargeSymbolTableHashDecoration_t m_Hash;
    // Variable length string data
    char m_String[1];

    char const* String() const {
        return (const char*) &m_String[0];
    }

    CUtlSymbolLarge ToSymbol() const {
        return reinterpret_cast<UtlSymLargeId_t>(String());
    }
};

// Base class for the case-sensitive and case-insensitive tables
template<bool CASEINSENSITIVE, size_t POOL_SIZE = MIN_STRING_POOL_SIZE>
class CUtlSymbolTableLargeBase {
public:
    // Finds and/or creates a symbol based on the string
    CUtlSymbolLarge AddString(std::string_view pString);

    // Finds the symbol for pString
    CUtlSymbolLarge Find(std::string_view pString) const;

    // Remove all symbols in the table.
    void RemoveAll();

    int GetNumStrings(void) const {
        return static_cast<int>(m_Elements.size());
    }

    void Commit() {
        // Nothing, only matters for thread-safe tables
    }

    // Returns elements in the table, in insertion order; returns the number copied
    int GetElements(int nFirstElement, int nCount, CUtlSymbolLarge* pElements) const {
        if (nFirstElement < 0 || nFirstElement >= GetNumStrings() || nCount <= 0)
            return 0;
        const int count = std::min(nCount, GetNumStrings() - nFirstElement);
        for (int i = 0; i < count; ++i) {
            pElements[i] = m_Elements[nFirstElement + i]->ToSymbol();
        }
        return count;
    }

    const char* GetElementString(int nElement) const {
        return m_Elements[nElement]->String();
    }

    uint64_t GetMemoryUsage() const {
        uint64_t unBytesUsed = 0u;

        for (const StringPool_t& pool : m_StringPools) {
            unBytesUsed += (uint64_t) pool.m_TotalLen;
        }
        return unBytesUsed;
    }

private:
    using Entry_t = CUtlSymbolTableLargeBaseTreeEntry_t;

    // Heterogeneous lookup key, so Find never allocates; m_Str is already cut at the first NUL
    struct LookupKey_t {
        LargeSymbolTableHashDecoration_t m_Hash;
        std::string_view m_Str;
    };

    static bool SameString(std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size())
            return false;
        if constexpr (!CASEINSENSITIVE)
            return lhs == rhs;
        if (lhs.empty())
            return true;
        return V_strnicmp(lhs.data(), rhs.data(), static_cast<int>(lhs.size())) == 0;
    }

    // The entry already holds its hash, so there is nothing to recompute; the string is only
    // compared when hashes collide.
    struct EntryHash_t {
        using is_transparent = void;

        size_t operator()(const Entry_t* entry) const { return entry->m_Hash; }
        size_t operator()(const LookupKey_t& key) const { return key.m_Hash; }
    };

    struct EntryEqual_t {
        using is_transparent = void;

        bool operator()(const Entry_t* lhs, const Entry_t* rhs) const {
            return lhs->m_Hash == rhs->m_Hash && SameString(lhs->String(), rhs->String());
        }

        bool operator()(const Entry_t* lhs, const LookupKey_t& rhs) const {
            return lhs->m_Hash == rhs.m_Hash && SameString(lhs->String(), rhs.m_Str);
        }

        bool operator()(const LookupKey_t& lhs, const Entry_t* rhs) const {
            return (*this)(rhs, lhs);
        }
    };

    struct StringPool_t {
        std::unique_ptr<char[]> m_Data;
        int m_TotalLen; // usable bytes in m_Data
        int m_SpaceUsed;
    };

    // Bytes of bookkeeping the pre-STL pool header charged against each pool's size
    static constexpr size_t POOL_HEADER_SIZE = 2 * sizeof(int);

    static LookupKey_t MakeKey(std::string_view pString) {
        pString = pString.substr(0, pString.find('\0'));
        return {CUtlSymbolLarge_Hash(CASEINSENSITIVE, pString), pString};
    }

    // Lookup by hash+string; entries point into m_StringPools
    std::unordered_set<const Entry_t*, EntryHash_t, EntryEqual_t> m_Lookup;

    // Entries in insertion order; the index is the element index
    std::vector<const Entry_t*> m_Elements;

    // stores the string data
    std::vector<StringPool_t> m_StringPools;

    int FindPoolWithSpace(int len) const;
};

template<bool CASEINSENSITIVE, size_t POOL_SIZE>
CUtlSymbolLarge CUtlSymbolTableLargeBase<CASEINSENSITIVE, POOL_SIZE>::Find(const std::string_view pString) const {
    const auto it = m_Lookup.find(MakeKey(pString));
    if (it == m_Lookup.end())
        return UTL_INVAL_SYMBOL_LARGE;

    return (*it)->ToSymbol();
}

template<bool CASEINSENSITIVE, size_t POOL_SIZE>
int CUtlSymbolTableLargeBase<CASEINSENSITIVE, POOL_SIZE>::FindPoolWithSpace(int len) const {
    for (size_t i = 0; i < m_StringPools.size(); i++) {
        const StringPool_t& pool = m_StringPools[i];

        if ((pool.m_TotalLen - pool.m_SpaceUsed) >= len) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

//-----------------------------------------------------------------------------
// Finds and/or creates a symbol based on the string
//-----------------------------------------------------------------------------
template<bool CASEINSENSITIVE, size_t POOL_SIZE>
CUtlSymbolLarge CUtlSymbolTableLargeBase<CASEINSENSITIVE, POOL_SIZE>::AddString(const std::string_view pString) {
    const LookupKey_t key = MakeKey(pString);
    if (const auto it = m_Lookup.find(key); it != m_Lookup.end())
        return (*it)->ToSymbol();

    const int lenString = static_cast<int>(pString.size()) + 1; // length of just the string
    // and with its hash decoration, aligned so the hashes will read correctly
    constexpr int align = sizeof(LargeSymbolTableHashDecoration_t);
    const int lenDecorated = (lenString + align + align - 1) / align * align;

    // Find a pool with space for this string, or allocate a new one.
    int iPool = FindPoolWithSpace(lenDecorated);
    if (iPool == -1) {
        // Add a new pool; strings bigger than POOL_SIZE get their own.
        const size_t newPoolSize = std::max<size_t>(lenDecorated + POOL_HEADER_SIZE, POOL_SIZE);
        const int totalLen = static_cast<int>(newPoolSize - POOL_HEADER_SIZE);
        m_StringPools.push_back({std::make_unique_for_overwrite<char[]>(totalLen), totalLen, 0});
        iPool = static_cast<int>(m_StringPools.size()) - 1;
    }

    // Copy the string in.
    StringPool_t& pool = m_StringPools[iPool];
    auto* entry = reinterpret_cast<Entry_t*>(&pool.m_Data[pool.m_SpaceUsed]);
    pool.m_SpaceUsed += lenDecorated;

    entry->m_Hash = key.m_Hash;
    if (!pString.empty())
        std::memcpy(entry->m_String, pString.data(), pString.size());
    entry->m_String[pString.size()] = '\0';

    m_Lookup.insert(entry);
    m_Elements.push_back(entry);
    return entry->ToSymbol();
}

//-----------------------------------------------------------------------------
// Remove all symbols in the table.
//-----------------------------------------------------------------------------
template<bool CASEINSENSITIVE, size_t POOL_SIZE>
void CUtlSymbolTableLargeBase<CASEINSENSITIVE, POOL_SIZE>::RemoveAll() {
    m_Lookup.clear();
    m_Elements.clear();
    m_StringPools.clear();
}

// Case-sensitive
typedef CUtlSymbolTableLargeBase<false> CUtlSymbolTableLarge;
// Case-insensitive
typedef CUtlSymbolTableLargeBase<true> CUtlSymbolTableLarge_CI;

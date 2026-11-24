//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once


#ifndef NULL
#ifdef __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif
#endif

#include <string_view>
#include "utlvector.h"
#include "color.h"
#include "exprevaluator.h"

class IBaseFileSystem;
class CUtlBuffer;
class Color;
class CKeyValuesTokenReader;
class KeyValues;
class IKeyValuesDumpContext;
using FileHandle_t = void*;

#define FOR_EACH_SUBKEY( kvRoot, kvSubKey ) \
	for ( KeyValues * kvSubKey = kvRoot->GetFirstSubKey(); kvSubKey != NULL; kvSubKey = kvSubKey->GetNextKey() )

#define FOR_EACH_TRUE_SUBKEY( kvRoot, kvSubKey ) \
	for ( KeyValues * kvSubKey = kvRoot->GetFirstTrueSubKey(); kvSubKey != NULL; kvSubKey = kvSubKey->GetNextTrueSubKey() )

#define FOR_EACH_VALUE( kvRoot, kvValue ) \
	for ( KeyValues * kvValue = kvRoot->GetFirstValue(); kvValue != NULL; kvValue = kvValue->GetNextValue() )


//-----------------------------------------------------------------------------
// Purpose: Simple recursive data access class
//			Used in vgui for message parameters and resource files
//			Destructor deletes all child KeyValues nodes
//			Data is stored in key (string names) - (string/int/float)value pairs called nodes.
//
//	About KeyValues Text File Format:

//	It has 3 control characters '{', '}' and '"'. Names and values may be quoted or
//	not. The quote '"' character must not be used within name or values, only for
//	quoting whole tokens. You may use escape sequences wile parsing and add within a
//	quoted token a \" to add quotes within your name or token. When using Escape
//	Sequence the parser must now that by setting KeyValues::UsesEscapeSequences( true ),
//	which it's off by default. Non-quoted tokens ends with a whitespace, '{', '}' and '"'.
//	So you may use '{' and '}' within quoted tokens, but not for non-quoted tokens.
//  An open bracket '{' after a key name indicates a list of subkeys which is finished
//  with a closing bracket '}'. Subkeys use the same definitions recursively.
//  Whitespaces are space, return, newline and tabulator. Allowed Escape sequences
//	are \n, \t, \\, \n and \". The number character '#' is used for macro purposes
//	(eg #include), don't use it as first character in key names.
//-----------------------------------------------------------------------------
class KeyValues {
    friend class CKeyValuesTokenReader;

public:
    explicit KeyValues();

    explicit KeyValues(std::string_view setName);

    //
    // AutoDelete class to automatically free the keyvalues.
    // Simply construct it with the keyvalues you allocated and it will free them when falls out of scope.
    // When you decide that keyvalues shouldn't be deleted call Assign(NULL) on it.
    // If you constructed AutoDelete(NULL) you can later assign the keyvalues to be deleted with Assign(pKeyValues).
    //
    class AutoDelete {
    public:
        explicit AutoDelete(KeyValues* pKeyValues) : m_pKeyValues(pKeyValues) {
        }

        explicit AutoDelete(std::string_view kvName) : m_pKeyValues(new KeyValues(kvName)) {
        }

        ~AutoDelete() { delete m_pKeyValues; }
        void Assign(KeyValues* pKeyValues) { m_pKeyValues = pKeyValues; }
        /// behaves more like an auto pointer detach ( flags itself to not delete the contained object, and returns a pointer to it)
        KeyValues* Detach() {
            KeyValues* retval = m_pKeyValues;
            Assign(nullptr);
            return retval;
        }

        KeyValues* operator->() const { return m_pKeyValues; }
        operator KeyValues*() const { return m_pKeyValues; }

        AutoDelete(AutoDelete const &x) = delete; // forbid
        AutoDelete& operator=(AutoDelete const &x) = delete; // forbid
    protected:
        KeyValues* m_pKeyValues;
    };

    //
    // AutoDeleteInline is useful when you want to hold your keyvalues object inside
    // and delete it right after using.
    // You can also pass temporary KeyValues object as an argument to a function by wrapping it into KeyValues::AutoDeleteInline
    // instance:   call_my_function( KeyValues::AutoDeleteInline( new KeyValues( "test" ) ) )
    //
    class AutoDeleteInline : public AutoDelete {
    public:
        explicit AutoDeleteInline(KeyValues* pKeyValues) : AutoDelete(pKeyValues) {
        }

        operator KeyValues*() const { return m_pKeyValues; }
        KeyValues* Get() const { return m_pKeyValues; }
    };

    // Quick setup constructors
    KeyValues(std::string_view setName, std::string_view firstKey, std::string_view firstValue);

    KeyValues(std::string_view setName, std::string_view firstKey, const wchar_t* firstValue);

    KeyValues(std::string_view setName, std::string_view firstKey, int firstValue);

    KeyValues(std::string_view setName, std::string_view firstKey, std::string_view firstValue,
              std::string_view secondKey, std::string_view secondValue);

    KeyValues(std::string_view setName, std::string_view firstKey, int firstValue, std::string_view secondKey,
              int secondValue);

    // destruct
    ~KeyValues();

    // Section name. The view points into the key name symbol table (NUL-terminated, lives as long as the process).
    std::string_view GetName() const;

    void SetName(std::string_view setName);

    // gets the name as a unique int
    int GetNameSymbol() const;

    int GetNameSymbolCaseSensitive() const;

    // File access. Set UsesEscapeSequences true, if resource file/buffer uses Escape Sequences (eg \n, \t)
    void UsesEscapeSequences(bool state); // default false
    bool LoadFromFile(IBaseFileSystem* filesystem, const char* resourceName, const char* pathID = nullptr,
                      GetSymbolProc_t pfnEvaluateSymbolProc = nullptr);

    bool SaveToFile(IBaseFileSystem* filesystem, const char* resourceName, const char* pathID = nullptr,
                    bool bWriteEmptySubkeys = false);

    // Read from a buffer...  Note that the buffer must be null terminated
    bool LoadFromBuffer(char const* resourceName, const char* pBuffer, IBaseFileSystem* pFileSystem = nullptr,
                        const char* pPathID = nullptr, GetSymbolProc_t pfnEvaluateSymbolProc = nullptr);

    // Read from a utlbuffer...
    bool LoadFromBuffer(char const* resourceName, CUtlBuffer &buf, IBaseFileSystem* pFileSystem = nullptr,
                        const char* pPathID = nullptr, GetSymbolProc_t pfnEvaluateSymbolProc = nullptr);

    // Find a keyValue, create it if it is not found. An empty name (the default) returns this key.
    // Set bCreate to true to create the key if it doesn't already exist (which ensures a valid pointer will be returned)
    KeyValues* FindKey(std::string_view keyName, bool bCreate = false);

    KeyValues* FindKey(int keySymbol) const;

    // creates a new key, with an autogenerated name.  name is guaranteed to be an integer, of value 1 higher than the highest other integer key name
    void AddSubKey(KeyValues* pSubkey); // Adds a subkey. Make sure the subkey isn't a child of some other keyvalues
    void RemoveSubKey(KeyValues* subKey); // removes a subkey from the list, DOES NOT DELETE IT

    // Removes a subkey but inserts all of its children in its place, in-order (flattens a tree, like firing a manager!)

    // Key iteration.
    //
    // NOTE: GetFirstSubKey/GetNextKey will iterate keys AND values. Use the functions
    // below if you want to iterate over just the keys or just the values.
    //
    KeyValues* GetFirstSubKey() const; // returns the first subkey in the list
    KeyValues* GetNextKey() const; // returns the next subkey
    void SetNextKey(KeyValues* pDat);

    KeyValues* FindLastSubKey();

    // returns the LAST subkey in the list.  This requires a linked list iteration to find the key.  Returns NULL if we don't have any children

    bool BInteriorNode() const {
        return GetFirstSubKey() != nullptr;
    }

    bool BLeafNode() const {
        return GetFirstSubKey() == nullptr;
    }

    //
    // These functions can be used to treat it like a true key/values tree instead of
    // confusing values with keys.
    //
    // So if you wanted to iterate all subkeys, then all values, it would look like this:
    //     for ( KeyValues *pKey = pRoot->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey() )
    //     {
    //		   Msg( "Key name: {}\n", pKey->GetName() );
    //     }
    //     for ( KeyValues *pValue = pRoot->GetFirstValue(); pKey; pKey = pKey->GetNextValue() )
    //     {
    //         Msg( "Int value: {:d}\n", pValue->GetInt() );  // Assuming pValue->GetDataType() == TYPE_INT...
    //     }
    KeyValues* GetFirstTrueSubKey();

    KeyValues* GetNextTrueSubKey();

    KeyValues* GetFirstValue(); // When you get a value back, you can use GetX and pass in NULL to get the value.
    KeyValues* GetNextValue();


    // Data access. An empty keyName (the default) reads this key's own value.
    int GetInt(std::string_view keyName = {}, int defaultValue = 0);

    uint64_t GetUint64(std::string_view keyName = {}, uint64_t defaultValue = 0);

    float GetFloat(std::string_view keyName = {}, float defaultValue = 0.0f);

    // The result views this KeyValues' own NUL-terminated storage when the key exists (valid until that key is
    // modified or destroyed; non-string values are converted to a string value first), otherwise defaultValue.
    std::string_view GetString(std::string_view keyName = {}, std::string_view defaultValue = "");

    const wchar_t* GetWString(std::string_view keyName = {}, const wchar_t* defaultValue = L"");

    void* GetPtr(std::string_view keyName = {}, void* defaultValue = nullptr);

    Color GetColor(std::string_view keyName = {}, const Color &defaultColor = Color(0, 0, 0, 0));

    bool GetBool(std::string_view keyName = {}, bool defaultValue = false) {
        return GetInt(keyName, defaultValue ? 1 : 0) ? true : false;
    }

    bool IsEmpty(std::string_view keyName = {});

    // Key writing
    void SetWString(std::string_view keyName, const wchar_t* value);

    void SetString(std::string_view keyName, std::string_view value);

    void SetInt(std::string_view keyName, int value);

    void SetUint64(std::string_view keyName, uint64_t value);

    void SetFloat(std::string_view keyName, float value);

    void SetPtr(std::string_view keyName, void* value);

    void SetBool(std::string_view keyName, bool value) { SetInt(keyName, value ? 1 : 0); }

    // Memory allocation (optimized)
    void* operator new(size_t iAllocSize);

    void* operator new(size_t iAllocSize, int nBlockUse, const char* pFileName, int nLine);

    void operator delete(void* pMem);

    void operator delete(void* pMem, int nBlockUse, const char* pFileName, int nLine);

    KeyValues& operator=(KeyValues &src);

    void RecursiveSaveToFile(CUtlBuffer &buf, int indentLevel);


    // Same as the other binary functions, but filter out and remove empty keys (like when seralizing to a file )

    // Allocate & create a new copy of the keys
    KeyValues* MakeCopy(void) const;

    // Make a new copy of all subkeys, add them all to the passed-in keyvalues
    void CopySubkeys(KeyValues* pParent) const;

    // Clear out all subkeys, and the current value
    void Clear(void);

    // Data type
    enum types_t {
        TYPE_NONE = 0,
        TYPE_STRING,
        TYPE_INT,
        TYPE_FLOAT,
        TYPE_PTR,
        TYPE_WSTRING,
        TYPE_COLOR,
        TYPE_UINT64,
        TYPE_COMPILED_INT_BYTE, // hack to collapse 1 byte ints in the compiled format
        TYPE_COMPILED_INT_0, // hack to collapse 0 in the compiled format
        TYPE_COMPILED_INT_1, // hack to collapse 1 in the compiled format
        TYPE_NUMTYPES,
    };

    types_t GetDataType(std::string_view keyName);

    types_t GetDataType() const;

    // for backward compat
    void deleteThis();

    void SetStringValue(std::string_view strValue);

    // Dump this key recursively. Use KeyValuesDumpAsDevMsg for a nullable root.
    bool Dump(IKeyValuesDumpContext* pDump, int nIndentLevel = 0);

    // Merge operations describing how two keyvalues can be combined
    enum MergeKeyValuesOp_t {
        MERGE_KV_ALL,
        MERGE_KV_UPDATE, // update values are copied into storage, adding new keys to storage or updating existing ones
        MERGE_KV_DELETE, // update values specify keys that get deleted from storage
        MERGE_KV_BORROW,
        // update values only update existing keys in storage, keys in update that do not exist in storage are discarded
    };

    /// Create a child key, given that we know which child is currently the last child.
    /// This avoids the O(N^2) behaviour when adding children in sequence to KV,
    /// when CreateKey() wil have to re-locate the end of the list each time.  This happens,
    /// for example, every time we load any KV file whatsoever.

    KeyValues* CreateKeyUsingKnownLastChild(std::string_view keyName, KeyValues* pLastChild);

    void AddSubkeyUsingKnownLastChild(KeyValues* pSubKey, KeyValues* pLastChild);

    KeyValues(KeyValues &) = delete;

private:
    void RecursiveCopyKeyValues(KeyValues &src);

    void RemoveEverything();

    //	void RecursiveSaveToFile( IBaseFileSystem *filesystem, CUtlBuffer &buffer, int indentLevel );
    //	void WriteConvertedString( CUtlBuffer &buffer, const char *pszString );

    // NOTE: If both filesystem and pBuf are non-null, it'll save to both of them.
    // If filesystem is null, it'll ignore f.
    void RecursiveSaveToFile(IBaseFileSystem* filesystem, FileHandle_t f, CUtlBuffer* pBuf, int indentLevel,
                             bool bWriteEmptySubkeys = false);

    void WriteConvertedString(IBaseFileSystem* filesystem, FileHandle_t f, CUtlBuffer* pBuf,
                              std::string_view pszString);

    void RecursiveLoadFromBuffer(char const* resourceName, CKeyValuesTokenReader &buf,
                                 GetSymbolProc_t pfnEvaluateSymbolProc);

    // for handling #include "filename"
    void AppendIncludedKeys(CUtlVector<KeyValues*> &includedKeys);

    void ParseIncludedKeys(char const* resourceName, const char* filetoinclude,
                           IBaseFileSystem* pFileSystem, const char* pPathID, CUtlVector<KeyValues*> &includedKeys,
                           GetSymbolProc_t pfnEvaluateSymbolProc);

    // For handling #base "filename"
    void MergeBaseKeys(CUtlVector<KeyValues*> &baseKeys);

    void RecursiveMergeKeyValues(KeyValues* baseKV);

    // NOTE: If both filesystem and pBuf are non-null, it'll save to both of them.
    // If filesystem is null, it'll ignore f.
    void InternalWrite(IBaseFileSystem* filesystem, FileHandle_t f, CUtlBuffer* pBuf, const void* pData, int len);

    void Init();

    void WriteIndents(IBaseFileSystem* filesystem, FileHandle_t f, CUtlBuffer* pBuf, int indentLevel);


    bool EvaluateConditional(const char* pExpressionString, GetSymbolProc_t pfnEvaluateSymbolProc);

    uint32_t m_iKeyName: 24; // keyname is a symbol defined in KeyValuesSystem
    uint32_t m_iKeyNameCaseSensitive1: 8; // 1st part of case sensitive symbol defined in KeyValueSystem

    // These are needed out of the union because the API returns string pointers
    char* m_sValue;
    wchar_t* m_wsValue;

    // we don't delete these
    union {
        int m_iValue;
        float m_flValue;
        void* m_pValue;
        unsigned char m_Color[4];
    };

    char m_iDataType;
    char m_bHasEscapeSequences; // true, if while parsing this KeyValue, Escape Sequences are used (default false)
    uint16_t m_iKeyNameCaseSensitive2; // 2nd part of case sensitive symbol defined in KeyValueSystem;

    KeyValues* m_pPeer; // pointer to next key in list
    KeyValues* m_pSub; // pointer to Start of a new sub key list
    KeyValues* m_pChain; // Search here if it's not in our list

    GetSymbolProc_t m_pExpressionGetSymbolProc;
};

//
// KeyValuesDumpContext and generic implementations
//

class IKeyValuesDumpContext {
public:
    virtual ~IKeyValuesDumpContext() = default;

    virtual bool KvBeginKey(KeyValues* pKey, int nIndentLevel) = 0;

    virtual bool KvWriteValue(KeyValues* pValue, int nIndentLevel) = 0;

    virtual bool KvEndKey(KeyValues* pKey, int nIndentLevel) = 0;
};

class IKeyValuesDumpContextAsText : public IKeyValuesDumpContext {
public:
    bool KvBeginKey(KeyValues* pKey, int nIndentLevel) override;

    bool KvWriteValue(KeyValues* pValue, int nIndentLevel) override;

    bool KvEndKey(KeyValues* pKey, int nIndentLevel) override;

    virtual bool KvWriteIndent(int nIndentLevel);

    virtual bool KvWriteText(std::string_view szText) = 0;
};

class CKeyValuesDumpContextAsDevMsg : public IKeyValuesDumpContextAsText {
public:
    // Overrides developer level to dump in DevMsg, zero to dump as Msg
    CKeyValuesDumpContextAsDevMsg(int nDeveloperLevel = 1) : m_nDeveloperLevel(nDeveloperLevel) {
    }

    bool KvBeginKey(KeyValues* pKey, int nIndentLevel) override;

    bool KvWriteText(std::string_view szText) override;

protected:
    int m_nDeveloperLevel;
};

inline bool KeyValuesDumpAsDevMsg(KeyValues* pKeyValues, int nIndentLevel = 0, int nDeveloperLevel = 1) {
    CKeyValuesDumpContextAsDevMsg ctx(nDeveloperLevel);
    if (pKeyValues)
        return pKeyValues->Dump(&ctx, nIndentLevel);
    return ctx.KvBeginKey(nullptr, nIndentLevel) && ctx.KvEndKey(nullptr, nIndentLevel);
}

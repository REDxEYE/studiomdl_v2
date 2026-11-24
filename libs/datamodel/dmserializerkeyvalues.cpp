//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "dmserializerkeyvalues.h"
#include "datamodel/idatamodel.h"
#include "datamodel.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattributevar.h"
#include "tier1/keyvalues.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlvector.h"
#include "DmElementFramework.h"

#include <charconv>
#include "tier1_v2/charconv_float.hpp"
#include <cstring>
#include <string>


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CUtlBuffer;
class CBaseSceneObject;


//-----------------------------------------------------------------------------
// Serialization class for Key Values
//-----------------------------------------------------------------------------
class CDmSerializerKeyValues : public IDmSerializer {
public:
    // Inherited from IDMSerializer
    const char* GetName() const override { return "keyvalues"; }
    const char* GetDescription() const override { return "KeyValues"; }
    bool StoresVersionInFile() const override { return false; }
    bool IsBinaryFormat() const override { return false; }
    int GetCurrentVersion() const override { return 0; } // doesn't store a version
    int GetImportedVersion() const override { return 1; }

    bool Serialize(CUtlBuffer &buf, CDmElement* pRoot) override;

    bool Unserialize(CUtlBuffer &buf, const char* pEncodingName, int nEncodingVersion,
                     const char* pSourceFormatName, int nSourceFormatVersion,
                     DmFileId_t fileid, DmConflictResolution_t idConflictResolution, CDmElement** ppRoot) override;

    const char* GetImportedFormat() const override { return "dmx"; }

private:
    // Methods related to serialization
    void SerializeSubKeys(CUtlBuffer &buf, CDmAttribute* pSubKeys);

    bool SerializeAttributes(CUtlBuffer &buf, CDmElement* pElement);

    bool SerializeElement(CUtlBuffer &buf, CDmElement* pElement);

    // Methods related to unserialization
    DmElementHandle_t UnserializeElement(KeyValues* pKeyValues, int iNestingLevel);

    void UnserializeAttribute(CDmElement* pElement, KeyValues* pKeyValues);

    DmElementHandle_t CreateDmElement(std::string_view pElementType, std::string_view pElementName);

    CDmElement* UnserializeFromKeyValues(KeyValues* pKeyValues);

    // Deterimines the attribute type of a keyvalue
    DmAttributeType_t DetermineAttributeType(KeyValues* pKeyValues);

    // For unserialization
    CUtlVector<DmElementHandle_t> m_ElementList;
    DmElementHandle_t m_hRoot;
    DmFileId_t m_fileid;
};


//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
static CDmSerializerKeyValues s_DMSerializerKeyValues;

void InstallKeyValuesSerializer(IDataModel* pFactory) {
    pFactory->AddSerializer(&s_DMSerializerKeyValues);
}


//-----------------------------------------------------------------------------
// Serializes a single element attribute
//-----------------------------------------------------------------------------
void CDmSerializerKeyValues::SerializeSubKeys(CUtlBuffer &buf, CDmAttribute* pSubKeys) {
    CDmrElementArray<> array(pSubKeys);
    int c = array.Count();
    for (int i = 0; i < c; ++i) {
        CDmElement* pChild = array[i];
        if (pChild) {
            SerializeElement(buf, pChild);
        }
    }
}


//-----------------------------------------------------------------------------
// Serializes all attributes in an element
//-----------------------------------------------------------------------------
bool CDmSerializerKeyValues::SerializeAttributes(CUtlBuffer &buf, CDmElement* pElement) {
    // Collect the attributes to be written
    CDmAttribute** ppAttributes = (CDmAttribute**) _alloca(pElement->AttributeCount() * sizeof( CDmAttribute* ));
    int nAttributes = 0;
    for (CDmAttribute* pAttribute = pElement->FirstAttribute(); pAttribute; pAttribute = pAttribute->NextAttribute()) {
        if (pAttribute->IsStandard() || pAttribute->IsFlagSet(FATTRIB_DONTSAVE))
            continue;

        ppAttributes[nAttributes++] = pAttribute;
    }

    // Now write them all out in reverse order, since FirstAttribute is actually the *last* attribute for perf reasons
    for (int i = nAttributes - 1; i >= 0; --i) {
        CDmAttribute* pAttribute = ppAttributes[i];
        Assert(pAttribute);

        const char* pName = pAttribute->GetName();

        // Rename "_name" dme attribute back to "name" keyvalues field
        if (!Q_stricmp(pName, "_name")) {
            pName = "name";
        }

        DmAttributeType_t nAttrType = pAttribute->GetType();
        if ((nAttrType == AT_ELEMENT_ARRAY) && !Q_stricmp(pName, "subkeys")) {
            SerializeSubKeys(buf, pAttribute);
            continue;
        }

        buf.Printf("\"%s\" ", pName);

        switch (nAttrType) {
            case AT_VOID:
            case AT_STRING_ARRAY:
            case AT_VOID_ARRAY:
            case AT_ELEMENT:
            case AT_ELEMENT_ARRAY:
                Warning("KeyValues: Can't serialize attribute of type {} into KeyValues files!\n",
                        g_pDataModel->GetAttributeNameForType(nAttrType));
                buf.PutChar('\"');
                buf.PutChar('\"');
                break;

            case AT_FLOAT:
            case AT_INT:
            case AT_BOOL:
                pAttribute->Serialize(buf);
                break;

            case AT_VECTOR4:
            case AT_VECTOR3:
            case AT_VECTOR2:
            case AT_STRING:
            default:
                buf.PutChar('\"');
                buf.PushTab();
                pAttribute->Serialize(buf);
                buf.PopTab();
                buf.PutChar('\"');
                break;
        }

        buf.PutChar('\n');
    }

    return true;
}

bool CDmSerializerKeyValues::SerializeElement(CUtlBuffer &buf, CDmElement* pElement) {
    buf.Printf("\"%s\"\n{\n", pElement->GetName().data());
    buf.PushTab();
    SerializeAttributes(buf, pElement);
    buf.PopTab();
    buf.Printf("}\n");
    return true;
}

bool CDmSerializerKeyValues::Serialize(CUtlBuffer &outBuf, CDmElement* pRoot) {
    if (!pRoot)
        return true;

    CDmAttribute* pSubKeys = pRoot->GetAttribute("subkeys");
    if (!pSubKeys)
        return true;

    //SetSerializationDelimiter( GetCStringCharConversion() );
    SerializeSubKeys(outBuf, pSubKeys);
    //SetSerializationDelimiter( NULL );
    return true;
}


//-----------------------------------------------------------------------------
// Creates a scene object, adds it to the element dictionary
//-----------------------------------------------------------------------------
DmElementHandle_t CDmSerializerKeyValues::CreateDmElement(const std::string_view pElementType,
                                                          const std::string_view pElementName) {
    // See if we can create an element of that type
    DmElementHandle_t hElement = g_pDataModel->CreateElement(pElementType, pElementName, m_fileid);
    if (hElement == DMELEMENT_HANDLE_INVALID) {
        Warning("KeyValues: Element uses unknown element type {}\n", pElementType);
        return DMELEMENT_HANDLE_INVALID;
    }

    m_ElementList.AddToTail(hElement);

    CDmElement* pElement = g_pDataModel->GetElement(hElement);
    pElement->DisableOnChangedCallbacks();
    return hElement;
}

//-----------------------------------------------------------------------------
// sscanf( p, "%f %f ..." ) as Valve's MSVC CRT reads it: decimal numbers only. glibc's %f also accepts "inf", "nan"
// and hex floats, which typed strings like "info_target" as float infinity.
//-----------------------------------------------------------------------------
static int ScanFloats(const char* p, float* pOut, int nMax) {
    int n = 0;
    for (; n < nMax; ++n) {
        while (V_isspace(*p))
            ++p;
        const char* pNumber = *p == '+' ? p + 1 : p; // from_chars takes '-' but not '+'
        const char* pDigits = *pNumber == '-' ? pNumber + 1 : pNumber;
        if (!V_isdigit(*pDigits) && !(*pDigits == '.' && V_isdigit(pDigits[1])))
            break;
        const auto [ptr, ec] = vstd::from_chars_float(pNumber, pNumber + strlen(pNumber), pOut[n], std::chars_format::general);
        if (ec == std::errc::invalid_argument)
            break;
        p = ptr; // out of range still consumes the number, as sscanf does
    }
    return n;
}



//-----------------------------------------------------------------------------
// Deterimines the attribute type of a keyvalue
//-----------------------------------------------------------------------------
DmAttributeType_t CDmSerializerKeyValues::DetermineAttributeType(KeyValues* pKeyValues) {
    // FIXME: Add detection of vectors/matrices?
    switch (pKeyValues->GetDataType()) {
        default:
        case KeyValues::TYPE_NONE:
            Assert(0);
            return AT_UNKNOWN;

        case KeyValues::TYPE_STRING: {
            // NUL-terminated copy for sscanf/strchr
            const std::string value(pKeyValues->GetString());
            const char* pValue = value.c_str();
            float f[4];
            const int nFloats = ScanFloats(pValue, f, 4);
            if (nFloats == 4)
                return AT_VECTOR4;
            if (nFloats == 3)
                return AT_VECTOR3;
            if (nFloats == 2)
                return AT_VECTOR2;

            int i = pKeyValues->GetInt({}, std::numeric_limits<int>::max());
            if ((sscanf(pValue, "%d", &i) == 1) &&
                (!strchr(pValue, '.')))
                return AT_INT;

            if (nFloats == 1)
                return AT_FLOAT;

            return AT_STRING;
        }

        case KeyValues::TYPE_INT:
            return AT_INT;

        case KeyValues::TYPE_FLOAT:
            return AT_FLOAT;

        case KeyValues::TYPE_PTR:
            return AT_VOID;

        case KeyValues::TYPE_COLOR:
            return AT_COLOR;
    }
}


//-----------------------------------------------------------------------------
// Reads an attribute for an element
//-----------------------------------------------------------------------------
void CDmSerializerKeyValues::UnserializeAttribute(CDmElement* pElement, KeyValues* pKeyValues) {
    // It's an attribute
    const std::string_view pAttributeName = pKeyValues->GetName();
    const std::string_view pAttributeValue = pKeyValues->GetString();

    // Convert to lower case
    std::string pLowerName(pAttributeName);
    vstd::make_lower_ascii(pLowerName);

    // Rename "name" keyvalues field to "_name" dme attribute to avoid name collision
    if (vstd::iequals(pLowerName, "name")) {
        pLowerName = "_name";
    }

    // Element types are stored out by GUID, we need to hang onto the guid and
    // link it back up once all elements have been loaded from the file
    DmAttributeType_t type = DetermineAttributeType(pKeyValues);

    // In this case, we have an inlined element or element array attribute
    if (type == AT_UNKNOWN) {
        // Assume this is an empty attribute or attribute array element
        Warning("Dm Unserialize: Attempted to read an attribute (\"{}\") of an inappropriate type!\n",
                pLowerName);
        return;
    }

    CDmAttribute* pAttribute = pElement->AddAttribute(pLowerName, type);
    if (!pAttribute) {
        Warning("Dm Unserialize: Attempted to read an attribute (\"{}\") of an inappropriate type!\n",
                pLowerName);
        return;
    }

    switch (type) {
        case AT_STRING: {
            // Strings have different delimiter rules for KeyValues,
            // so let's just directly copy the string instead of going through unserialize
            pAttribute->SetValue(g_pDataModel->GetSymbol(pAttributeValue));
        }
        break;

        default: {
            auto nLen = pAttributeValue.size();
            CUtlBuffer buf(pAttributeValue.data(), nLen, CUtlBuffer::TEXT_BUFFER | CUtlBuffer::READ_ONLY);
            pAttribute->Unserialize(buf);
        }
        break;
    }
}


//-----------------------------------------------------------------------------
// Reads a single element
//-----------------------------------------------------------------------------
DmElementHandle_t CDmSerializerKeyValues::UnserializeElement(KeyValues* pKeyValues, int iNestingLevel) {
    const std::string_view pElementName = pKeyValues->GetName();
    const auto pszKeyValuesElement = g_pDataModel->GetKeyValuesElementName(pElementName, iNestingLevel).value_or("DmElement");

    DmElementHandle_t handle = CreateDmElement(pszKeyValuesElement, pElementName);
    Assert(handle != DMELEMENT_HANDLE_INVALID);

    iNestingLevel++;

    CDmElement* pElement = g_pDataModel->GetElement(handle);
    CDmrElementArray<> subKeys;
    for (KeyValues* pSub = pKeyValues->GetFirstSubKey(); pSub != nullptr; pSub = pSub->GetNextKey()) {
        // Read in a subkey
        if (pSub->GetDataType() == KeyValues::TYPE_NONE) {
            if (!subKeys.IsValid()) {
                subKeys.Init(pElement->AddAttribute("subkeys", AT_ELEMENT_ARRAY));
            }

            DmElementHandle_t hChild = UnserializeElement(pSub, iNestingLevel);
            if (hChild != DMELEMENT_HANDLE_INVALID) {
                subKeys.AddToTail(hChild);
            }
        }
        else {
            UnserializeAttribute(pElement, pSub);
        }
    }

    return handle;
}


//-----------------------------------------------------------------------------
// Main entry point for the unserialization
//-----------------------------------------------------------------------------
CDmElement* CDmSerializerKeyValues::UnserializeFromKeyValues(KeyValues* pKeyValues) {
    m_ElementList.RemoveAll();

    m_hRoot = CreateDmElement("DmElement", "root");
    CDmElement* pRoot = g_pDataModel->GetElement(m_hRoot);
    CDmrElementArray<> subkeys(pRoot->AddAttribute("subkeys", AT_ELEMENT_ARRAY));

    int iNestingLevel = 0;

    for (KeyValues* pElementKey = pKeyValues; pElementKey != nullptr; pElementKey = pElementKey->GetNextKey()) {
        DmElementHandle_t hChild = UnserializeElement(pElementKey, iNestingLevel);
        if (hChild != DMELEMENT_HANDLE_INVALID) {
            subkeys.AddToTail(hChild);
        }
    }

    // mark all unserialized elements as done unserializing, and call Resolve()
    int c = m_ElementList.size();
    for (int i = 0; i < c; ++i) {
        CDmElement* pElement = g_pDataModel->GetElement(m_ElementList[i]);
        pElement->EnableOnChangedCallbacks();
        pElement->FinishUnserialization();
    }

    g_pDmElementFrameworkImp->RemoveCleanElementsFromDirtyList();
    m_ElementList.RemoveAll();
    return pRoot;
}


//-----------------------------------------------------------------------------
// Main entry point for the unserialization
//-----------------------------------------------------------------------------
bool CDmSerializerKeyValues::Unserialize(CUtlBuffer &buf, const char* pEncodingName, int nEncodingVersion,
                                         const char* pSourceFormatName, int nSourceFormatVersion,
                                         DmFileId_t fileid, DmConflictResolution_t idConflictResolution,
                                         CDmElement** ppRoot) {
    Assert(!V_stricmp( pEncodingName, "keyvalues" ));

    *ppRoot = nullptr;

    KeyValues* kv = new KeyValues("keyvalues file");
    if (!kv)
        return false;

    m_fileid = fileid;

    bool bOk = kv->LoadFromBuffer("keyvalues file", buf);
    if (bOk) {
        //SetSerializationDelimiter( GetCStringCharConversion() );
        *ppRoot = UnserializeFromKeyValues(kv);
        //SetSerializationDelimiter( NULL );
    }

    m_fileid = DMFILEID_INVALID;

    kv->deleteThis();
    return bOk;
}

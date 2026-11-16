//====== Copyright � 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "datamodel/idatamodel.h"
#include "datamodel/dmattributevar.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "datamodel.h"
#include "dmattributeinternal.h"
#include "dmserializerkeyvalues.h"
#include "dmserializerkeyvalues2.h"
#include "dmserializerbinary.h"
#include "notifymanager.h"
#include "tier2/utlstreambuffer.h"
#include "tier2/fileutils.h"
#include <array>
#include <charconv>
#include <ctime>
#include <string_view>

#include "tier1_v2/function_timing.hpp"


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CUtlBuffer;

class IDmEditMessage;

class KeyValues;

#define UNNAMED_ELEMENT_NAME    "unnamed"



//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
static CDataModel g_DataModel;
CDataModel *g_pDataModelImp = &g_DataModel;
IDataModel *g_pDataModel = &g_DataModel;


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CDataModel::CDataModel() :
        m_elementIds(4096),
        m_unloadedIdElementMap(16, 0, 0, ElementIdHandlePair_t::Compare, ElementIdHandlePair_t::HashKey) {
    m_bOnlyCreateUntypedElements = false;
    m_bUnableToCreateOnlyUntypedElements = false;
    m_pKeyvaluesCallbackInterface = nullptr;
    m_nElementsAllocatedSoFar = 0;
    m_nMaxNumberOfElements = 0;
    m_bIsUnserializing = false;
    m_bDeleteOrphanedElements = false;
}

CDataModel::~CDataModel() {
    if (GetAllocatedElementCount() > 0) {
        Warning("Leaking {:d} elements\n", GetAllocatedElementCount());
    }
}


//-----------------------------------------------------------------------------
// Installs the built-in serializers (called by InitDataModel)
//-----------------------------------------------------------------------------
bool CDataModel::Init() {
    InstallKeyValuesSerializer(this);
    InstallKeyValues2Serializer(this);
    InstallBinarySerializer(this);

    return true;
}


//#define _ELEMENT_HISTOGRAM_
#ifdef _ELEMENT_HISTOGRAM_
std::map< CUtlSymbolLarge, int > g_typeHistogram;
#endif _ELEMENT_HISTOGRAM_


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDataModel::Shutdown() {
#ifdef _ELEMENT_HISTOGRAM_
                                                                                                                            Msg( "element type histogram for {:d} elements allocated so far:\n", GetElementsAllocatedSoFar() );
	for ( const auto &[typeSym, count] : g_typeHistogram )
	{
		Msg( "{:d}\t{}\n", count, typeSym.String() );
	}
	Msg( "\n" );
#endif _ELEMENT_HISTOGRAM_

    int c = GetAllocatedElementCount();
    if (c > 0) {
        Warning("CDataModel:  {:d} elements left in memory!!!\n", c);
    }

    m_ElementTypes.clear();
    m_UnknownElementTypeName = {};
    m_Serializers.Purge();
}


//-----------------------------------------------------------------------------
// force creation of untyped elements, ignoring type
//-----------------------------------------------------------------------------
void CDataModel::OnlyCreateUntypedElements(bool bEnable) {
    if (m_bUnableToCreateOnlyUntypedElements) {
        Assert(0);
        return;
    }

    m_bOnlyCreateUntypedElements = bEnable;
}

int CDataModel::GetElementsAllocatedSoFar() {
    return m_nElementsAllocatedSoFar;
}

int CDataModel::GetMaxNumberOfElements() {
    return m_nMaxNumberOfElements;
}

int CDataModel::GetAllocatedAttributeCount() {
    return ::GetAllocatedAttributeCount();
}


//-----------------------------------------------------------------------------
// Returns the total number of elements allocated at the moment
//-----------------------------------------------------------------------------
int CDataModel::GetAllocatedElementCount() {
    return (int) m_Handles.GetValidHandleCount();
}

DmElementHandle_t CDataModel::FirstAllocatedElement() {
    int nHandles = (int) m_Handles.GetHandleCount();
    for (int i = 0; i < nHandles; ++i) {
        DmElementHandle_t hElement = (DmElementHandle_t) m_Handles.GetHandleFromIndex(i);
        if (CDmElement *pElement = GetElement(hElement))
            return hElement;
    }
    return DMELEMENT_HANDLE_INVALID;
}

DmElementHandle_t CDataModel::NextAllocatedElement(DmElementHandle_t hElement) {
    int nHandles = (int) m_Handles.GetHandleCount();
    for (int i = m_Handles.GetIndexFromHandle(hElement) + 1; i < nHandles; ++i) {
        DmElementHandle_t hElement = (DmElementHandle_t) m_Handles.GetHandleFromIndex(i);
        if (CDmElement *pElement = GetElement(hElement))
            return hElement;
    }

    return DMELEMENT_HANDLE_INVALID;
}


//-----------------------------------------------------------------------------
// estimate memory overhead
//-----------------------------------------------------------------------------
int CDataModel::EstimateMemoryOverhead() const {
    int nHandlesOverhead = sizeof(int) + sizeof(CDmElement *); // m_Handles
    int nElementIdsOverhead = sizeof(DmElementHandle_t); // this also has a 80k static overhead, since hash tables can't grow
    return nHandlesOverhead + nElementIdsOverhead;
}

static bool HandleCompare(const DmElementHandle_t &a, const DmElementHandle_t &b) {
    return a == b;
}

static unsigned int HandleHash(const DmElementHandle_t &h) {
    return (unsigned int) h;
}

int CDataModel::EstimateMemoryUsage(DmElementHandle_t hElement, TraversalDepth_t depth) {
    CUtlHash<DmElementHandle_t> visited(1024, 0, 0, HandleCompare, HandleHash);
    CDmElement *pElement = m_Handles.GetHandle(hElement);
    if (!pElement)
        return 0;

    return pElement->EstimateMemoryUsage(visited, depth, nullptr);
}


//-----------------------------------------------------------------------------
// Displays stats for datamodel
//-----------------------------------------------------------------------------
struct DmMemoryInfo_t {
    int m_nCount;
    int m_nSize;
    int m_pCategories[MEMORY_CATEGORY_COUNT];
};

struct DmMemorySortInfo_t {
    const std::pair<const CUtlSymbolLarge, DmMemoryInfo_t> *m_pEntry;
    int m_nTotalSize;
};

int DmMemorySortFunc(const void *lhs, const void *rhs) {
    DmMemorySortInfo_t &info1 = *(DmMemorySortInfo_t *) lhs;
    DmMemorySortInfo_t &info2 = *(DmMemorySortInfo_t *) rhs;
    return info1.m_nTotalSize - info2.m_nTotalSize;
}

// this method assumes that all elements that should be traversed have a parity of !bParity
static void
GatherElements_R(CDmElement *pElement, CUtlVector<DmElementHandle_t> &list, CUtlHash<DmElementHandle_t> &visited) {
    DmElementHandle_t h = pElement->GetHandle();

    if (visited.Find(h) != visited.InvalidHandle())
        return;
    visited.Insert(h);

    list.AddToTail(h);

    for (const CDmAttribute *pAttr = pElement->FirstAttribute(); pAttr != nullptr; pAttr = pAttr->NextAttribute()) {
        if (pAttr->GetType() == AT_ELEMENT) {
            CDmElement *pChild = pAttr->GetValueElement<CDmElement>();
            if (!pChild)
                continue;
            GatherElements_R(pChild, list, visited);
        } else if (pAttr->GetType() == AT_ELEMENT_ARRAY) {
            const CDmrElementArrayConst<> elementArrayAttr(pAttr);
            int nChildren = elementArrayAttr.Count();
            for (int i = 0; i < nChildren; ++i) {
                CDmElement *pChild = elementArrayAttr[i];
                if (!pChild)
                    continue;
                GatherElements_R(pChild, list, visited);
            }
        }
    }
}

void CDataModel::BuildHistogramForHandles(std::map<CUtlSymbolLarge, DmMemoryInfo_t> &typeHistogram,
                                          CUtlVector<DmElementHandle_t> &handles) {
    CUtlHash<DmElementHandle_t> visited(1024, 0, 0, HandleCompare, HandleHash);

    int c = handles.size();
    for (int i = 0; i < c; ++i) {
        DmElementHandle_t h = handles[i];
        if (h == DMELEMENT_HANDLE_INVALID)
            continue;

        CDmElement *pElement = m_Handles.GetHandle(h);
        if (!pElement)
            continue;

        // Value-initialized (zeroed) on first use
        DmMemoryInfo_t &info = typeHistogram[pElement->GetType()];

        int nMemory = pElement->EstimateMemoryUsage(visited, TD_NONE, info.m_pCategories);

        ++info.m_nCount;
        info.m_nSize += nMemory;
    }
}

void CDataModel::DisplayMemoryStats(DmElementHandle_t hElement /*= DMELEMENT_HANDLE_INVALID*/ ) {
    std::map<CUtlSymbolLarge, DmMemoryInfo_t> typeHistogram;
    CUtlHash<DmElementHandle_t> visited(1024, 0, 0, HandleCompare, HandleHash);

    CUtlVector<DmElementHandle_t> handles;

    if (hElement == DMELEMENT_HANDLE_INVALID) {
        int c = (int) m_Handles.GetHandleCount();
        for (int i = 0; i < c; ++i) {
            DmElementHandle_t h = (DmElementHandle_t) m_Handles.GetHandleFromIndex(i);
            if (!m_Handles.IsHandleValid(h))
                continue;
            handles.AddToTail(h);
        }
    } else {
        // Do a recursive build
        if (!m_Handles.IsHandleValid(hElement)) {
            Msg("CDataModel::DisplayMemoryStats with invalid handle {:d}\n", static_cast<unsigned>(hElement));
            return;
        }

        CDmElement *pElement = m_Handles.GetHandle(hElement);
        if (pElement) {
            GatherElements_R(pElement, handles, visited);
        }
    }

    BuildHistogramForHandles(typeHistogram, handles);

    // Sort
    DmMemorySortInfo_t *pSortInfo = (DmMemorySortInfo_t *) _alloca(typeHistogram.size() * sizeof(DmMemorySortInfo_t));
    int nCount = 0;
    for (const auto &entry : typeHistogram) {
        pSortInfo[nCount].m_pEntry = &entry;
        pSortInfo[nCount].m_nTotalSize = entry.second.m_nSize;
        ++nCount;
    }
    qsort(pSortInfo, nCount, sizeof(DmMemorySortInfo_t), DmMemorySortFunc);

    int pTotals[MEMORY_CATEGORY_COUNT] = {0};
    int nLastTwoSize = 0;
    int nTotalSize = 0;
    int nTotalCount = 0;
    int nTotalData = 0;
    ConMsg("Dm Memory usage: type\t\t\t\tcount\ttotalsize\twastage %\touter\t\tinner\t\tdatamodel\trefs\t\ttree\t\tatts\t\tdata\t(att count)\n");
    for (int i = 0; i < nCount; ++i) {
        const DmMemoryInfo_t &info = pSortInfo[i].m_pEntry->second;
        float flPercentOverhead = 1.0f -
                                  ((info.m_nSize != 0) ? ((float) info.m_pCategories[MEMORY_CATEGORY_ATTRIBUTE_DATA] /
                                                          (float) info.m_nSize) : 0.0f);
        flPercentOverhead *= 100.0f;

        ConMsg("{:<40}\t{:6d}\t{:9d}\t\t{:5.2f}", pSortInfo[i].m_pEntry->first.String(),
               info.m_nCount, info.m_nSize, flPercentOverhead);
        int nTotal = 0;
        for (int j = 0; j < MEMORY_CATEGORY_COUNT; ++j) {
            ConColorMsg(Color(255, 192, 0, 255), "\t{:8d}", info.m_pCategories[j]);
            if (j != MEMORY_CATEGORY_ATTRIBUTE_COUNT) {
                nTotal += info.m_pCategories[j];
            }
            pTotals[j] += info.m_pCategories[j];
        }
        ConMsg("\n");
        Assert(nTotal == info.m_nSize);
        nTotalSize += info.m_nSize;
        nTotalCount += info.m_nCount;
        nTotalData += info.m_pCategories[MEMORY_CATEGORY_ATTRIBUTE_DATA];

        if (i >= nCount - 2) {
            nLastTwoSize += info.m_nSize;
        }
    }

    ConMsg("\n");
    ConMsg("{:<40}\t{:6d}\t{:9d}\t\t{:5.2f}", "Totals", nTotalCount, nTotalSize,
           100.0f * (1.0f - (float) nTotalData / (float) nTotalSize));
    for (int j = 0; j < MEMORY_CATEGORY_COUNT; ++j) {
        ConColorMsg(Color(255, 192, 0, 255), "\t{:8d}", pTotals[j]);
    }

    ConMsg("\n");

    float flPercent = 0.0f;
    if (nTotalSize > 0) {
        flPercent = 100.0 * ((float) nLastTwoSize / (float) nTotalSize);
    }

    ConMsg("Last two rows as percentage of total size {:f} %:\n", flPercent);

    uint64_t symbolBytes = m_SymbolTable.GetMemoryUsage();
    int symbolCount = m_SymbolTable.GetNumStrings();

    // Estimate string table usage
    ConMsg("Symbol table:  {:d} entries {:d} bytes\n", symbolCount, symbolBytes);
}


void IncrementSymbolRefCount(CUtlSymbolLarge symbol, std::map<CUtlSymbolLarge, int> &refCountMap) {
    ++refCountMap[symbol];
}

int CompareSymbolsAcending(const CUtlSymbolLarge *symA, const CUtlSymbolLarge *symB) {
    return V_stricmp(symA->String(), symB->String());
}

// Dump the symbol table to the console
void CDataModel::DumpSymbolTable() {
    uint64_t symbolBytes = m_SymbolTable.GetMemoryUsage();
    int symbolCount = m_SymbolTable.GetNumStrings();

    // Estimate string table usage
    ConMsg("Datamodel symbol table: {:d} entries {:d} bytes\n", symbolCount, symbolBytes);

    std::map<CUtlSymbolLarge, int> symbolRefCountMap;

    int nNumElements = (int) m_Handles.GetHandleCount();
    for (int iElement = 0; iElement < nNumElements; ++iElement) {
        CDmElement *pElement = m_Handles.GetHandle(iElement);
        if (pElement == nullptr)
            continue;

        IncrementSymbolRefCount(pElement->GetType(), symbolRefCountMap);

        for (CDmAttribute *pAttr = pElement->FirstAttribute(); pAttr; pAttr = pAttr->NextAttribute()) {
            IncrementSymbolRefCount(pAttr->GetNameSymbol(), symbolRefCountMap);

            if (pAttr->GetType() == AT_STRING) {
                IncrementSymbolRefCount(pAttr->GetValue<CUtlSymbolLarge>(), symbolRefCountMap);
            }
        }
    }

    CUtlBuffer buf(0, symbolBytes + (symbolCount * 32), CUtlBuffer::TEXT_BUFFER);

    buf.Printf("Datamodel symbol table: %d entries %I64u bytes\n", symbolCount, symbolBytes);
    CUtlVector<CUtlSymbolLarge> elements;
    elements.EnsureCount(symbolCount);
    m_SymbolTable.GetElements(0, symbolCount, elements.data());
    elements.Sort(CompareSymbolsAcending);


    buf.Printf("Index      Address       References   String\n");
    for (int i = 0; i < symbolCount; ++i) {
        CUtlSymbolLarge symbol = elements[i];
        const auto it = symbolRefCountMap.find(symbol);
        int nRefCount = (it != symbolRefCountMap.end()) ? it->second : 0;

        const char *pString = symbol.String();
        buf.Printf("%05d,     0x%p,     %5d,      '%s'\n", i, pString, nRefCount, pString);
    }

    int nFileIndex = 0;
    int nMaxFileIndex = 1000;
    const char baseFilename[] = "DatamodelSymbols";
    CUtlString filename;

    char directory[256];
    g_pFullFileSystem->GetCurrentDirectory(directory, sizeof(directory));

    while (nFileIndex < nMaxFileIndex) {
        filename = vstd::fx_formatted("{}\\{}{}.txt", directory, baseFilename, nFileIndex);
        if (!g_pFullFileSystem->FileExists(filename))
            break;
        ++nFileIndex;
    }

    if (nFileIndex < nMaxFileIndex) {
        g_pFullFileSystem->WriteFile(filename, IBaseFileSystem::PathID::Unspecified, buf);
        Msg("Wrote symbol stats to file: {}", filename.Get());
    }
}

//-----------------------------------------------------------------------------
// Global symbol table for the datamodel system
//-----------------------------------------------------------------------------
CUtlSymbolLarge CDataModel::GetSymbol(const char *pString) {
    return m_SymbolTable.AddString(pString);
}


//-----------------------------------------------------------------------------
// file format methods
//-----------------------------------------------------------------------------
const char *CDataModel::GetFormatExtension(const char *pFormatName) {
    IDmFormatUpdater *pUpdater = FindFormatUpdater(pFormatName);
    Assert(pUpdater);
    if (!pUpdater)
        return nullptr;

    return pUpdater->GetExtension();
}

const char *CDataModel::GetFormatDescription(const char *pFormatName) {
    IDmFormatUpdater *pUpdater = FindFormatUpdater(pFormatName);
    Assert(pUpdater);
    if (!pUpdater)
        return nullptr;

    return pUpdater->GetDescription();
}

int CDataModel::GetFormatCount() const {
    return m_FormatUpdaters.size();
}

const char *CDataModel::GetFormatName(int i) const {
    IDmFormatUpdater *pUpdater = m_FormatUpdaters[i];
    if (!pUpdater)
        return nullptr;

    return pUpdater->GetName();
}

const char *CDataModel::GetDefaultEncoding(const char *pFormatName) {
    IDmFormatUpdater *pUpdater = FindFormatUpdater(pFormatName);
    if (!pUpdater)
        return nullptr;

    return pUpdater->GetDefaultEncoding();
}

//-----------------------------------------------------------------------------
// Adds various serializers
//-----------------------------------------------------------------------------
void CDataModel::AddSerializer(IDmSerializer *pSerializer) {
    Assert(Q_strlen(pSerializer->GetName()) <= DMX_MAX_FORMAT_NAME_MAX_LENGTH);

    if (FindSerializer(pSerializer->GetName())) {
        Warning("Attempted to add two serializers with the same file encoding ({})!\n", pSerializer->GetName());
        return;
    }
    m_Serializers.AddToTail(pSerializer);
}

void CDataModel::AddLegacyUpdater(IDmLegacyUpdater *pUpdater) {
    Assert(Q_strlen(pUpdater->GetName()) <= DMX_MAX_FORMAT_NAME_MAX_LENGTH);

    if (FindLegacyUpdater(pUpdater->GetName())) {
        Warning("Attempted to add two legacy updaters with the same file format ({})!\n", pUpdater->GetName());
        return;
    }

    m_LegacyUpdaters.AddToTail(pUpdater);
}

void CDataModel::AddFormatUpdater(IDmFormatUpdater *pUpdater) {
    Assert(Q_strlen(pUpdater->GetName()) <= DMX_MAX_FORMAT_NAME_MAX_LENGTH);

    if (FindFormatUpdater(pUpdater->GetName())) {
        Warning("Attempted to add two format updaters with the same file format ({})!\n", pUpdater->GetName());
        return;
    }

    m_FormatUpdaters.AddToTail(pUpdater);
}

//-----------------------------------------------------------------------------
// encoding-related methods
//-----------------------------------------------------------------------------
int CDataModel::GetEncodingCount() const {
    return m_Serializers.size();
}

const char *CDataModel::GetEncodingName(int i) const {
    return m_Serializers[i]->GetName();
}

bool CDataModel::IsEncodingBinary(const char *pEncodingName) const {
    IDmSerializer *pSerializer = FindSerializer(pEncodingName);
    if (!pSerializer) {
        Warning("Serialize: File encoding {} is undefined!\n", pEncodingName);
        return false;
    }
    return pSerializer->IsBinaryFormat();
}

bool CDataModel::DoesEncodingStoreVersionInFile(const char *pEncodingName) const {
    IDmSerializer *pSerializer = FindSerializer(pEncodingName);
    if (!pSerializer) {
        Warning("Serialize: File encoding {} is undefined!\n", pEncodingName);
        return false;
    }
    return pSerializer->StoresVersionInFile();
}


IDmSerializer *CDataModel::FindSerializer(const char *pEncodingName) const {
    int nSerializers = m_Serializers.size();
    for (int i = 0; i < nSerializers; ++i) {
        IDmSerializer *pSerializer = m_Serializers[i];
        Assert(pSerializer);
        if (!pSerializer)
            continue;

        if (!V_strcmp(pEncodingName, pSerializer->GetName()))
            return pSerializer;
    }

    return nullptr;
}

IDmLegacyUpdater *CDataModel::FindLegacyUpdater(const char *pLegacyFormatName) const {
    int nUpdaters = m_LegacyUpdaters.size();
    for (int i = 0; i < nUpdaters; ++i) {
        IDmLegacyUpdater *pUpdater = m_LegacyUpdaters[i];
        Assert(pUpdater);
        if (!pUpdater)
            continue;

        if (!V_strcmp(pLegacyFormatName, pUpdater->GetName()))
            return pUpdater;
    }

    return nullptr;
}

IDmFormatUpdater *CDataModel::FindFormatUpdater(const char *pFormatName) const {
    int nUpdaters = m_FormatUpdaters.size();
    for (int i = 0; i < nUpdaters; ++i) {
        IDmFormatUpdater *pUpdater = m_FormatUpdaters[i];
        Assert(pUpdater);
        if (!pUpdater)
            continue;

        if (!V_strcmp(pFormatName, pUpdater->GetName()))
            return pUpdater;
    }

    return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Sets the name of the DME element to create in keyvalues serialization
//-----------------------------------------------------------------------------
void CDataModel::SetKeyValuesElementCallback(IElementForKeyValueCallback *pCallbackInterface) {
    m_pKeyvaluesCallbackInterface = pCallbackInterface;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const char *CDataModel::GetKeyValuesElementName(const char *pszKeyName, int iNestingLevel) {
    if (m_pKeyvaluesCallbackInterface)
        return m_pKeyvaluesCallbackInterface->GetElementForKeyValue(pszKeyName, iNestingLevel);

    return nullptr;
}


//-----------------------------------------------------------------------------
// For serialization, set the delimiter rules
//-----------------------------------------------------------------------------
void CDataModel::SetSerializationDelimiter(CUtlCharConversion *pConv) {
    ::SetSerializationDelimiter(pConv);
}

void CDataModel::SetSerializationArrayDelimiter(const char *pDelimiter) {
    ::SetSerializationArrayDelimiter(pDelimiter);
}

bool
CDataModel::SaveToFile(char const *pFileName, char const *pPathID, const char *pEncodingName, const char *pFormatName,
                       CDmElement *pRoot) {
    // NOTE: This guarantees full path names for pathids
    char pFullPath[MAX_PATH]{0};
    if (!GenerateFullPath(pFileName, pPathID, pFullPath, sizeof(pFullPath))) {
        Warning("CDataModel: Unable to generate full path for file {}\n", pFileName);
        return false;
    }

    if (g_pFullFileSystem->FileExists(pFullPath, IBaseFileSystem::ToPathID(pPathID))) {
        if (!g_pFullFileSystem->IsFileWritable(pFullPath, IBaseFileSystem::ToPathID(pPathID))) {
            Warning("CDataModel: Unable to overwrite readonly file {}\n", pFullPath);
            return false;
        }
    }

    if (!pEncodingName) {
        pEncodingName = GetDefaultEncoding(pFormatName);
    }

    bool bIsBinary = IsEncodingBinary(pEncodingName);
    if (bIsBinary) {
        CUtlStreamBuffer buf(pFullPath, pPathID, 0, true);
        if (!buf.IsValid()) {
            Warning("CDataModel: Unable to open file \"{}\"\n", pFullPath);
            return false;
        }

        bool bOk = Serialize(buf, pEncodingName, pFormatName, pRoot->GetHandle());
        if (!bOk)
            return false;

    } else {
        CUtlBuffer buf(0, 0, CUtlBuffer::TEXT_BUFFER);

        bool bOk = Serialize(buf, pEncodingName, pFormatName, pRoot->GetHandle());
        if (!bOk)
            return false;

        if (!g_pFullFileSystem->WriteFile(pFullPath, IBaseFileSystem::ToPathID(pPathID), buf)) {
            Warning("CDataModel: Unable to open file \"{}\"\n", pFullPath);
            return false;
        }
    }

    DmFileId_t fileid = GetFileId(pFullPath);
    if (fileid != DMFILEID_INVALID) {
        SetFileModificationUTCTime(fileid, GetCurrentUTCTime());
    }

    return true;
}

DmFileId_t
CDataModel::RestoreFromFile(char const *pFileName, char const *pPathID, const char *pEncodingHint, CDmElement **ppRoot,
                            DmConflictResolution_t idConflictResolution /*= CR_DELETE_NEW*/,
                            DmxHeader_t *pHeaderOut /*= NULL*/ ) {
    vstd::FunctionTimingRecorder _("DMX");
    // NOTE: This guarantees full path names for pathids
    char pFullPath[MAX_PATH]{0};
    if (!GenerateFullPath(pFileName, pPathID, pFullPath, sizeof(pFullPath))) {
        Warning("CDataModel: Unable to generate full path for file {}\n", pFileName);
        return DMFILEID_INVALID;
    }

    char *pTemp = (char *) _alloca(DMX_MAX_HEADER_LENGTH + 1);
    CUtlBuffer typeBuf(pTemp, DMX_MAX_HEADER_LENGTH);
    if (!g_pFullFileSystem->ReadFile(pFullPath, IBaseFileSystem::ToPathID(pPathID), typeBuf, DMX_MAX_HEADER_LENGTH)) {
        Warning("CDataModel: Unable to open file {}\n", pFullPath);
        return DMFILEID_INVALID;
    }

    DmxHeader_t _header;
    DmxHeader_t *pHeader = pHeaderOut ? pHeaderOut : &_header;
    bool bSuccess = ReadDMXHeader(typeBuf, pHeader);
    if (!bSuccess) {
        if (!pEncodingHint) {
            Warning("CDataModel: Unable to determine DMX encoding for file {}\n", pFullPath);
            return DMFILEID_INVALID;
        }

        if (!IsValidNonDMXFormat(pEncodingHint)) {
            Warning("CDataModel: Invalid DMX encoding hint '{}' for file {}\n", pEncodingHint, pFullPath);
            return DMFILEID_INVALID;
        }

        // non-dmx file importers don't have versions or formats, just encodings
        V_strncpy(pHeader->encodingName, pEncodingHint, sizeof(pHeader->encodingName));
        pHeader->formatName[0] = 0;
    }

    DmElementHandle_t hRootElement;
    bool bIsBinary = IsEncodingBinary(pHeader->encodingName);
    if (bIsBinary) {
        CUtlStreamBuffer buf(pFullPath, pPathID, CUtlBuffer::READ_ONLY);
        if (!buf.IsValid()) {
            Warning("CDataModel: Unable to open file '{}'\n", pFullPath);
            return DMFILEID_INVALID;
        }

        if (!Unserialize(buf, pHeader->encodingName, pHeader->formatName, pEncodingHint, pFullPath,
                         idConflictResolution, hRootElement))
            return DMFILEID_INVALID;

    } else {
        CUtlBuffer buf(0, 0, CUtlBuffer::READ_ONLY | CUtlBuffer::TEXT_BUFFER);

        if (!g_pFullFileSystem->ReadFile(pFullPath, IBaseFileSystem::ToPathID(pPathID), buf)) {
            Warning("CDataModel: Unable to open file '{}'\n", pFullPath);
            return DMFILEID_INVALID;
        }

        if (!Unserialize(buf, pHeader->encodingName, pHeader->formatName, pEncodingHint, pFullPath,
                         idConflictResolution, hRootElement))
            return DMFILEID_INVALID;
    }


    *ppRoot = g_pDataModel->GetElement(hRootElement);

    DmFileId_t fileid = g_pDataModel->GetFileId(pFullPath);
    Assert(fileid != DMFILEID_INVALID);
    if (fileid != DMFILEID_INVALID) {
        SetFileModificationUTCTime(fileid, g_pFullFileSystem->GetFileTime(pFullPath));
    }

    return fileid;
}


//-----------------------------------------------------------------------------
// Is this a DMX file format?
//-----------------------------------------------------------------------------
bool CDataModel::IsDMXFormat(CUtlBuffer &buf) const {
    DmxHeader_t header;
    bool bSuccess = ReadDMXHeader(buf, &header);
    buf.SeekGet(CUtlBuffer::SEEK_HEAD, 0);
    return bSuccess;
}


//-----------------------------------------------------------------------------
// Serialization of a element tree into a utlbuffer
//-----------------------------------------------------------------------------
bool
CDataModel::Serialize(CUtlBuffer &outBuf, const char *pEncodingName, const char *pFormatName, DmElementHandle_t hRoot) {
    if (!pEncodingName) {
        pEncodingName = outBuf.IsText() ? "keyvalues2" : "binary";
    }

    // Find a serializer appropriate for the file format.
    IDmSerializer *pSerializer = FindSerializer(pEncodingName);
    if (!pSerializer) {
        Warning("Serialize: File encoding '{}' is undefined!\n", pEncodingName);
        return false;
    }

    // Ensure the utlbuffer is in the appropriate format (binary/text)
    bool bIsText = outBuf.IsText();
    bool bIsCRLF = outBuf.ContainsCRLF();

    CUtlBuffer outTextBuffer(0, 0, CUtlBuffer::TEXT_BUFFER);
    CUtlBuffer *pActualOutBuf = &outBuf;

    if (pSerializer->IsBinaryFormat()) {
        if (outBuf.IsText()) {
            if (!outBuf.ContainsCRLF()) {
                Warning("Serialize: Format {} expects to be written to a binary format, but the buffer is a text-format buffer\n",
                        pFormatName);
                return false;
            }
            outBuf.SetBufferType(false, false);
        }
    } else {
        // If we want text, but the binbuf is binary; recast it to a text buffer w/ CRLF
        if (!outBuf.IsText()) {
            outBuf.SetBufferType(true, true);
        }

        if (outBuf.ContainsCRLF()) {
            // If we want text, but the binbuf expects CRLF, then we must do a conversion pass
            pActualOutBuf = &outTextBuffer;
        }
    }

    if (pSerializer->StoresVersionInFile()) {
        // Write the format name into the file using XML format so that
        // 3rd-party XML readers can read the file without fail

        pActualOutBuf->Printf("%s encoding %s %d format %s %d %s\n",
                              DMX_VERSION_STARTING_TOKEN.c_str(), pEncodingName, pSerializer->GetCurrentVersion(),
                              pFormatName, GetCurrentFormatVersion(pFormatName), DMX_VERSION_ENDING_TOKEN.c_str());
    }

    // Now write the file using the appropriate format
    CDmElement *pRoot = GetElement(hRoot);
    bool bOk = pSerializer->Serialize(*pActualOutBuf, pRoot);
    if (bOk) {
        if (pActualOutBuf == &outTextBuffer) {
            outTextBuffer.ConvertCRLF(outBuf);
        }
    }

    outBuf.SetBufferType(bIsText, bIsCRLF);
    return bOk;
}


//-----------------------------------------------------------------------------
// Parses the text between the DMX header tokens: "encoding <name> <int> format <name> <int>". Names longer than
// the header fields fail instead of overflowing them.
//-----------------------------------------------------------------------------
static bool ParseDmxHeaderFields(const std::string_view text, DmxHeader_t &header) {
    constexpr std::string_view kWhitespace = " \t\r\n";
    std::array<std::string_view, 6> fields;
    size_t count = 0;
    for (size_t pos = 0; count < fields.size();) {
        pos = text.find_first_not_of(kWhitespace, pos);
        if (pos == std::string_view::npos)
            break;
        const size_t end = text.find_first_of(kWhitespace, pos);
        fields[count++] = text.substr(pos, end - pos);
        pos = end;
    }
    if (count < fields.size() || fields[0] != "encoding" || fields[3] != "format")
        return false;

    const auto parseInt = [](std::string_view field, int &value) {
        if (field.starts_with('+'))
            field.remove_prefix(1);
        const auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
        return ec == std::errc() && end == field.data() + field.size();
    };
    return header.encodingName.assign(fields[1]) && parseInt(fields[2], header.nEncodingVersion) &&
           header.formatName.assign(fields[4]) && parseInt(fields[5], header.nFormatVersion);
}


//-----------------------------------------------------------------------------
// Read the header, return the version (or false if it's not a DMX file)
//-----------------------------------------------------------------------------
bool CDataModel::ReadDMXHeader(CUtlBuffer &inBuf, DmxHeader_t *pHeader) const {
    Assert(pHeader);
    if (!pHeader)
        return false;

    // Make the buffer capable of being read as text
    bool bIsText = inBuf.IsText();
    bool bHasCRLF = inBuf.ContainsCRLF();
    inBuf.SetBufferType(true, !bIsText || bHasCRLF);

    char headerStr[DMX_MAX_HEADER_LENGTH];
    bool bOk = inBuf.ParseToken(DMX_VERSION_STARTING_TOKEN.c_str(), DMX_VERSION_ENDING_TOKEN.c_str(), headerStr, sizeof(headerStr));
    if (bOk) {
        bOk = ParseDmxHeaderFields(headerStr, *pHeader);
    }

    if (!bOk) {
        inBuf.SeekGet(CUtlBuffer::SEEK_HEAD, 0);
        bOk = inBuf.ParseToken(DMX_LEGACY_VERSION_STARTING_TOKEN.c_str(), DMX_LEGACY_VERSION_ENDING_TOKEN.c_str(), pHeader->formatName,
                               DMX_MAX_FORMAT_NAME_MAX_LENGTH);
        if (bOk) {
            const char *pEncoding = GetEncodingFromLegacyFormat(pHeader->formatName);
            if (pEncoding) {
                V_strncpy(pHeader->encodingName, pEncoding, DMX_MAX_FORMAT_NAME_MAX_LENGTH);
                pHeader->nEncodingVersion = 0; // the first encoding version
                pHeader->nFormatVersion = -1; // this value is ignored for legacy formats
            } else {
                bOk = false;
            }
        }
    }

    inBuf.SetBufferType(bIsText, bHasCRLF);
    return bOk;
}

const char *CDataModel::GetEncodingFromLegacyFormat(const char *pLegacyFormatName) const {
    if (StringHasPrefixCaseSensitive(pLegacyFormatName, "binary_v"))
        return "binary";
    if (StringHasPrefixCaseSensitive(pLegacyFormatName, "sfm_v"))
        return "binary";
    if (StringHasPrefixCaseSensitive(pLegacyFormatName, "keyvalues2_v"))
        return "keyvalues2";
    if (StringHasPrefixCaseSensitive(pLegacyFormatName, "keyvalues2_flat_v"))
        return "keyvalues2_flat";
    return nullptr;
}

bool CDataModel::IsLegacyFormat(const char *pFormatName) const {
    return GetEncodingFromLegacyFormat(pFormatName) != nullptr;
}

bool CDataModel::IsValidNonDMXFormat(const char *pEncodingName) const {
    IDmSerializer *pSerializer = FindSerializer(pEncodingName);
    return pSerializer && !pSerializer->StoresVersionInFile();
}


// used to skip auto-creation of child elements during unserialization
bool CDataModel::IsUnserializing() {
    return m_bIsUnserializing;
}

int CDataModel::GetCurrentFormatVersion(const char *pFormatName) {
    IDmFormatUpdater *pUpdater = FindFormatUpdater(pFormatName);
    if (!pUpdater)
        return -1; // invalid version #

    return pUpdater->GetCurrentVersion();
}

//-----------------------------------------------------------------------------
// Unserializes, returns the root of the unserialized tree in ppRoot
//-----------------------------------------------------------------------------
bool
CDataModel::Unserialize(CUtlBuffer &inBuf, const char *pEncodingName, const char *pRequiredFormat, const char *pUnused,
                        const char *pFileName, DmConflictResolution_t idConflictResolution, DmElementHandle_t &hRoot) {
    vstd::FunctionTimingRecorder _("DMX");
    m_bDeleteOrphanedElements = true; // next time we delete unreferenced elements, delete orphaned subtrees as well

    if (!pEncodingName) {
        pEncodingName = inBuf.IsText() ? "keyvalues2" : "binary";
    }

    // Find a serializer appropriate for the file format.
    IDmSerializer *pSerializer = FindSerializer(pEncodingName);
    if (!pSerializer) {
        Warning("Unerialize: DMX file encoding {} is undefined!\n", pEncodingName);
        return false;
    }

    DmxHeader_t header;
    bool bStoresVersionInFile = pSerializer->StoresVersionInFile();
    bool bIsCurrentVersion = false; // for formats that don't store a format, files are currently always *not*
    // at the current version since they need to be converted to dmx
    const char *pDestFormatName;
    if (bStoresVersionInFile) {
        bool bOk = ReadDMXHeader(inBuf, &header);
        if (!bOk) {
            Warning("Unserialize: unable to read DMX header!\n");
            return false;
        }

        int nCurrentFormatVersion = GetCurrentFormatVersion(header.formatName);
        if (header.nFormatVersion > nCurrentFormatVersion) {
            Warning("Unserialize: file format newer than executable's!\n");
            return false;
        }

        bIsCurrentVersion = !IsLegacyFormat(header.formatName) && nCurrentFormatVersion == header.nFormatVersion;
        pDestFormatName = IsLegacyFormat(header.formatName) ? GENERIC_DMX_FORMAT : header.formatName;
    } else {
        pDestFormatName = pSerializer->GetImportedFormat();
        Q_strncpy(header.formatName, pDestFormatName, sizeof(header.formatName));
        header.nFormatVersion = pSerializer->GetImportedVersion();
        header.nEncodingVersion = pSerializer->GetCurrentVersion();
        Q_strncpy(header.encodingName, pSerializer->GetName(), sizeof(header.encodingName));
    }



//     if we're not in dmxconvert, and we're not at the latest version, call dmxconvert and unserialize from the converted file
    if (!m_bOnlyCreateUntypedElements && !bIsCurrentVersion) {
        CDataModel tmp;
        for (const auto &item: m_FormatUpdaters) {
            tmp.AddFormatUpdater(item);
        }
        for (const auto &item: m_LegacyUpdaters) {
            tmp.AddLegacyUpdater(item);
        }
        tmp.Init();
        auto orig = g_pDataModel;
        g_pDataModelImp = &tmp;
        g_pDataModel = &tmp;

        tmp.OnlyCreateUntypedElements(true);
        DmFileId_t fileid = tmp.FindOrCreateFileId("tmp_convert.dmx");
        tmp.SetFileName(fileid, "tmp_convert.dmx");

        auto hTmpRoot = tmp.CreateElement(CDmElement::GetStaticTypeSymbol(), "root",
                                          fileid, nullptr);
        inBuf.SeekGet(CUtlBuffer::SEEK_HEAD, 0);
        auto res = tmp.Unserialize(inBuf, pEncodingName, header.formatName, nullptr, pFileName,
                                   DmConflictResolution_t::CR_DELETE_OLD, hTmpRoot);
        CUtlBuffer outBuf;
        tmp.Serialize(outBuf, "binary", header.formatName, hTmpRoot);
        for (int i = 0; i < tmp.NumFileIds(); ++i)
            tmp.UnloadFile(tmp.GetFileId(i), true);

        outBuf.SeekGet(CUtlBuffer::SEEK_HEAD, 0);
        g_pDataModelImp = dynamic_cast<CDataModel *>(orig);
        g_pDataModel = orig;
        auto bSuccess = Unserialize(outBuf, "binary", header.formatName, header.formatName, pFileName,
                                    idConflictResolution, hRoot);
        return bSuccess;
    }


    // advance the buffer the the end of the header
    if (bStoresVersionInFile) {
        if (V_strcmp(pEncodingName, header.encodingName) != 0)
            return false;

        if (pRequiredFormat && pRequiredFormat[0]) {
            if (V_strcmp(pRequiredFormat, header.formatName) != 0) {
                Warning("Unexpected format '{}' reading file '{}' - expected format '{}'\n", pRequiredFormat, pFileName,
                        header.formatName);
            }
        }

        if (pSerializer->IsBinaryFormat()) {
            // For binary formats, we gotta keep reading until we hit the string terminator
            // that occurred after the version line.
            while (inBuf.GetChar() != 0) {
                if (!inBuf.IsValid())
                    break;
            }
        }
    }

    m_bIsUnserializing = true;

    DmFileId_t fileid = FindOrCreateFileId(pFileName);

    // Now read the file using the appropriate format
    CDmElement *pRoot;
    bool bOk = pSerializer->Unserialize(inBuf, pEncodingName, header.nEncodingVersion, header.formatName,
                                        header.nFormatVersion,
                                        fileid, idConflictResolution, &pRoot);
    if (bOk) {
        hRoot = pRoot ? pRoot->GetHandle() : DMELEMENT_HANDLE_INVALID;

        if (!bIsCurrentVersion) {
            auto updater = g_pDataModel->FindFormatUpdater(header.formatName);
            updater->Update(&pRoot, header.nFormatVersion);
            header.nFormatVersion = updater->GetCurrentVersion();
        }

        SetFileModificationUTCTime(fileid, 0);
        SetFileFormat(fileid, header.formatName);
        SetFileRoot(fileid, hRoot);
    } else {
        RemoveFileId(fileid);
    }

    m_bIsUnserializing = false;
    return bOk;
}

bool CDataModel::UpdateUnserializedElements(const char *pSourceFormatName, int nSourceFormatVersion,
                                            DmFileId_t fileid, DmConflictResolution_t idConflictResolution,
                                            CDmElement **ppRoot) {
    if (IsLegacyFormat(pSourceFormatName)) {
        IDmLegacyUpdater *pLegacyUpdater = FindLegacyUpdater(pSourceFormatName);
        if (pLegacyUpdater) {
            if (!pLegacyUpdater->Update(ppRoot))
                return false;
        }

        // if there's no legacy updater found, then this is already the latest legacy format
        pSourceFormatName = GENERIC_DMX_FORMAT.c_str();
    }

    IDmFormatUpdater *pFormatUpdater = FindFormatUpdater(pSourceFormatName);
    if (!pFormatUpdater)
        return true;    // Used to return false, if no format updater exists, simply means a format updater hasn't been required yet

    return pFormatUpdater->Update(ppRoot, nSourceFormatVersion);
}

//-----------------------------------------------------------------------------
// file id reference methods
//-----------------------------------------------------------------------------

int CDataModel::NumFileIds() {
    return m_openFiles.GetHandleCount();
}

DmFileId_t CDataModel::GetFileId(int i) {
    Assert(i >= 0 && i < (int) m_openFiles.GetHandleCount());
    if (i < 0 || i >= (int) m_openFiles.GetHandleCount())
        return DMFILEID_INVALID;

    return (DmFileId_t) m_openFiles.GetHandleFromIndex(i);
}

void CleanupFileName(char *pOutFileName, size_t nOutLen, const char *pInFileName) {
    char fixedFileName[MAX_PATH]{0};
    V_FixupPathName(fixedFileName, sizeof(fixedFileName), pInFileName);

    if (!V_IsAbsolutePath(fixedFileName)) {
        char currentDir[MAX_PATH]{0};
        g_pFullFileSystem->GetCurrentDirectory(currentDir, sizeof(currentDir));
        V_MakeAbsolutePath(pOutFileName, nOutLen, fixedFileName, currentDir);
    } else {
        V_strncpy(pOutFileName, fixedFileName, nOutLen);
    }
}

DmFileId_t CDataModel::FindOrCreateFileId(const char *pFileName) {
    Assert(pFileName && *pFileName);
    if (!pFileName || !*pFileName)
        return DMFILEID_INVALID;

    char fixedFileName[MAX_PATH]{0};
    CleanupFileName(fixedFileName, sizeof(fixedFileName), pFileName);

    DmFileId_t fileid = GetFileId(fixedFileName);
    if (fileid != DMFILEID_INVALID) {
//		Assert( IsFileLoaded( fileid ) );
        MarkFileLoaded(fileid); // this is sort of a hack, but I'm planning a rewrite phase on all this anyways - joe
        return fileid;
    }

    fileid = (DmFileId_t) m_openFiles.AddHandle();
    m_openFiles.SetHandle(fileid, new FileElementSet_t(GetSymbol(fixedFileName)));
    return fileid;
}

void CDataModel::RemoveFileId(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    if (!fes)
        return;

    if (fes->m_bLoaded) {
        UnloadFile(fileid, true);
    }
    delete fes;

    m_openFiles.RemoveHandle(fileid);
}

DmFileId_t CDataModel::GetFileId(const char *pFileName) {
    char fixedFileName[MAX_PATH]{0};
    CleanupFileName(fixedFileName, sizeof(fixedFileName), pFileName);

    CUtlSymbolLarge filenameSym = m_SymbolTable.Find(fixedFileName);
    if (filenameSym == UTL_INVAL_SYMBOL_LARGE)
        return DMFILEID_INVALID;

    int nFiles = m_openFiles.GetHandleCount();
    for (int i = 0; i < nFiles; ++i) {
        DmFileId_t fileid = (DmFileId_t) m_openFiles.GetHandleFromIndex(i);
        FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
        Assert(fes || !m_openFiles.IsHandleValid(fileid));
        if (fes && fes->m_filename == filenameSym)
            return fileid;
    }

    return DMFILEID_INVALID;
}

const char *CDataModel::GetFileName(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    return fes ? fes->m_filename.String() : nullptr;
}

void CDataModel::SetFileName(DmFileId_t fileid, const char *pFileName) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return;

    char pFixedFileName[MAX_PATH];
    CleanupFileName(pFixedFileName, sizeof(pFixedFileName), pFileName);

    fes->m_filename = GetSymbol(pFixedFileName);
}

const char *CDataModel::GetFileFormat(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    return fes ? fes->m_format.String() : nullptr;
}

void CDataModel::SetFileFormat(DmFileId_t fileid, const char *pFormat) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return;

    fes->m_format = GetSymbol(pFormat);
}

DmElementHandle_t CDataModel::GetFileRoot(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    return fes ? (DmElementHandle_t) fes->m_hRoot : DMELEMENT_HANDLE_INVALID;
}

void CDataModel::SetFileRoot(DmFileId_t fileid, DmElementHandle_t hRoot) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return;

    if (fes->m_hRoot == hRoot)
        return;

    fes->m_hRoot = hRoot;
}

int64_t CDataModel::GetFileModificationUTCTime(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    return fes ? fes->m_fileModificationTime : 0;
}

void CDataModel::SetFileModificationUTCTime(DmFileId_t fileid, int64_t fileModificationTime) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    if (!fes)
        return;
    fes->m_fileModificationTime = fileModificationTime;
}

int64_t CDataModel::GetCurrentUTCTime() {
    return (int64_t)time(nullptr);
}

void CDataModel::UTCTimeToString(char *pString, int maxChars, int64_t fileTime) {
    return g_pFullFileSystem->FileTimeToString(pString, maxChars, fileTime);
}

bool CDataModel::IsFileLoaded(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes || fileid == DMFILEID_INVALID);
    return fes ? fes->m_bLoaded : false;
}

void CDataModel::UnloadFile(DmFileId_t fileid, bool bDeleteElements) {
    m_bDeleteOrphanedElements = true; // next time we delete unreferenced elements, delete orphaned subtrees as well

    int nHandles = (int) m_Handles.GetHandleCount();
    for (int i = 0; i < nHandles; ++i) {
        DmElementHandle_t hElement = (DmElementHandle_t) m_Handles.GetHandleFromIndex(i);
        if (hElement == DMELEMENT_HANDLE_INVALID)
            continue;

        CDmElement *pElement = GetElement(hElement);
        if (!pElement || pElement->GetFileId() != fileid)
            continue;

        DeleteElement(hElement, bDeleteElements ? HR_ALWAYS : HR_IF_NOT_REFERENCED);
    }

    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    if (fes) {
        fes->m_bLoaded = false;
    }
}

void CDataModel::UnloadFile(DmFileId_t fileid) {
    UnloadFile(fileid, false);
}

void CDataModel::MarkFileLoaded(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return;

    fes->m_bLoaded = true;
}

int CDataModel::NumElementsInFile(DmFileId_t fileid) {
    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return 0;

    return fes->m_nElements;
}

//-----------------------------------------------------------------------------
// file id reference methods not in IDataModel
//-----------------------------------------------------------------------------
void CDataModel::RemoveElementFromFile(DmElementHandle_t hElement, DmFileId_t fileid) {
    if (fileid == DMFILEID_INVALID)
        return;

    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return;

    --fes->m_nElements;
}

void CDataModel::AddElementToFile(DmElementHandle_t hElement, DmFileId_t fileid) {
    if (fileid == DMFILEID_INVALID)
        return;

    FileElementSet_t *fes = m_openFiles.GetHandle(fileid);
    Assert(fes);
    if (!fes)
        return;

    ++fes->m_nElements;
}

// search id->handle table (both loaded and unloaded) for id, and if not found, create a new handle, map it to the id and return it
DmElementHandle_t CDataModel::FindOrCreateElementHandle(const DmObjectId_t &id) {
    UtlHashHandle_t h = m_elementIds.Find(id);
    if (h != m_elementIds.InvalidHandle())
        return m_elementIds[h];

    h = m_unloadedIdElementMap.Find(ElementIdHandlePair_t(id)); // TODO - consider optimizing find to take just an id
    if (h != m_unloadedIdElementMap.InvalidHandle())
        return m_unloadedIdElementMap[h].m_ref.m_hElement;

    DmElementHandle_t hElement = AcquireElementHandle();
    m_unloadedIdElementMap.Insert(ElementIdHandlePair_t(id, DmElementReference_t(hElement)));
    MarkHandleInvalid(hElement);
    return hElement;
}

// changes an element's id and associated mappings - generally during unserialization
DmElementHandle_t
CDataModel::ChangeElementId(DmElementHandle_t hElement, const DmObjectId_t &oldId, const DmObjectId_t &newId) {
    UtlHashHandle_t oldHash = m_elementIds.Find(oldId);
    Assert(oldHash != m_elementIds.InvalidHandle());
    if (oldHash == m_elementIds.InvalidHandle())
        return hElement;

    Assert(m_elementIds[oldHash] == hElement);

    // can't change an element's id once it has attributes or handles linked to it
    CDmElement *pElement = GetElement(hElement);
    Assert(pElement);
    if (!pElement)
        return DMELEMENT_HANDLE_INVALID;

    Assert(!pElement->GetReference()->IsWeaklyReferenced());

    UtlHashHandle_t newHash = m_elementIds.Find(newId);
    if (newHash != m_elementIds.InvalidHandle())
        return DMELEMENT_HANDLE_INVALID; // can't change an element's id to the id of an existing element

    // remove old element entry
    m_elementIds.Remove(oldHash);

    // change the element id
    pElement->SetId(newId);

    newHash = m_unloadedIdElementMap.Find(ElementIdHandlePair_t(newId));
    if (newHash == m_unloadedIdElementMap.InvalidHandle()) {
        // the newId has never been seen before - keep the element handle the same and rehash into the id->handle map
        m_elementIds.Insert(hElement);
        return hElement;
    }

    // else, the newId is being referenced by some other element
    // change element to use newId and the associated handle from an element reference

    DmElementReference_t &newRef = m_unloadedIdElementMap[newHash].m_ref;
    DmElementHandle_t newHandle = newRef.m_hElement;
    Assert(newHandle != hElement); // no two ids should have the same handle
    Assert(!m_Handles.IsHandleValid(newHandle)); // unloaded elements shouldn't have valid handles

    m_Handles.SetHandle(newHandle, GetElement(hElement));
    pElement->ChangeHandle(newHandle);
    pElement->SetReference(newRef);
    ReleaseElementHandle(hElement);

    // move new element entry from the unloaded map to the loaded map
    m_elementIds.Insert(newHandle);
    m_unloadedIdElementMap.Remove(newHash);

    return newHandle;
}

DmElementReference_t *CDataModel::FindElementReference(DmElementHandle_t hElement, DmObjectId_t **ppId /* = NULL */ ) {
    if (ppId) {
        *ppId = nullptr;
    }

    CDmElement *pElement = GetElement(hElement);
    if (pElement)
        return pElement->GetReference();

    for (UtlHashHandle_t h = m_unloadedIdElementMap.GetFirstHandle();
         h != m_unloadedIdElementMap.InvalidHandle(); h = m_unloadedIdElementMap.GetNextHandle(h)) {
        DmElementReference_t &ref = m_unloadedIdElementMap[h].m_ref;
        if (ref.m_hElement == hElement) {
            if (ppId) {
                *ppId = &m_unloadedIdElementMap[h].m_id;
            }
            return &ref;
        }
    }

    return nullptr;
}

void CDataModel::DontAutoDelete(DmElementHandle_t hElement) {
    // this artificially adds a strong reference to the element, so it won't ever get unref'ed to 0
    // the only ways for this element to go away are explicit deletion, or file unload
    OnElementReferenceAdded(hElement, HT_STRONG);
}

void CDataModel::OnElementReferenceAdded(DmElementHandle_t hElement, CDmAttribute *pAttribute) {
    Assert(pAttribute);
    if (!pAttribute)
        return;

    if (hElement == DMELEMENT_HANDLE_INVALID)
        return;

    DmElementReference_t *pRef = FindElementReference(hElement);
    if (!pRef)
        return;

// 	Msg( "OnElementReferenceAdded: {} 0x{:x} '{}' referenced by {} 0x{:x} '{}'\n",
// 		GetElementType( hElement ).String(), hElement, GetElementName( hElement ),
// 		pAttribute->GetOwner()->GetTypeString(), pAttribute->GetOwner()->GetHandle(), pAttribute->GetName() );

    if (pRef->m_bHasEverBeenReferenced && !pRef->IsStronglyReferenced()) {
        CDmElement *pElement = GetElement(hElement);
        if (!pElement || pElement->GetFileId() != DMFILEID_INVALID) {
            m_bDeleteOrphanedElements = true;
        }
    }
    pRef->m_bHasEverBeenReferenced = true;

    pRef->AddAttribute(pAttribute);
}

void CDataModel::OnElementReferenceAdded(DmElementHandle_t hElement, HandleType_t handleType) {
    if (hElement == DMELEMENT_HANDLE_INVALID)
        return;

    DmElementReference_t *pRef = FindElementReference(hElement);
    if (!pRef)
        return;

// 	if ( handleType != HT_WEAK )
// 	{
// 		int nCount = handleType == HT_STRONG ? pRef->m_nStrongHandleCount : pRef->m_nUndoHandleCount;
// 		Msg( "OnElementReferenceAdded: {} 0x{:x} \"{}\" referenced by {} handle\n",
// 			GetElementType( hElement ).String(), hElement, GetElementName( hElement ),
// 			handleType == HT_STRONG ? "strong" : "undo" );
// 	}

    if (handleType != HT_WEAK) {
        bool bUpdate = handleType == HT_STRONG ? pRef->m_bHasEverBeenReferenced && !pRef->IsStronglyReferenced()
                                               : !pRef->IsReferencedByUndo();
        if (bUpdate) {
            CDmElement *pElement = GetElement(hElement);
            if (!pElement || pElement->GetFileId() != DMFILEID_INVALID) {
                m_bDeleteOrphanedElements = true;
            }
        }
        pRef->m_bHasEverBeenReferenced = true;
    }

    switch (handleType) {
        case HT_WEAK:
            ++pRef->m_nWeakHandleCount;
            break;
        case HT_STRONG:
            ++pRef->m_nStrongHandleCount;
            break;
        case HT_UNDO:
            ++pRef->m_nUndoHandleCount;
            break;
    }
}

void CDataModel::OnElementReferenceRemoved(DmElementHandle_t hElement, CDmAttribute *pAttribute) {

    Assert(pAttribute);
    if (!pAttribute)
        return;

    if (hElement == DMELEMENT_HANDLE_INVALID)
        return;

    DmElementReference_t *pRef = FindElementReference(hElement);
    if (!pRef)
        return;

    pRef->RemoveAttribute(pAttribute);

// 	Msg( "OnElementReferenceRemoved: {} 0x{:x} '{}' referenced by {} 0x{:x} '{}'\n",
// 		GetElementType( hElement ).String(), hElement, GetElementName( hElement ),
// 		pAttribute->GetOwner()->GetTypeString(), pAttribute->GetOwner()->GetHandle(), pAttribute->GetName() );

    if (!pRef->IsStronglyReferenced()) {
        CDmElement *pElement = GetElement(hElement);
        if (!pElement || pElement->GetFileId() != DMFILEID_INVALID) {
            m_bDeleteOrphanedElements = true;
        }
    }
}

void CDataModel::OnElementReferenceRemoved(DmElementHandle_t hElement, HandleType_t handleType) {

    if (hElement == DMELEMENT_HANDLE_INVALID)
        return;

    DmElementReference_t *pRef = FindElementReference(hElement);
    if (!pRef)
        return;

    switch (handleType) {
        case HT_WEAK:
            --pRef->m_nWeakHandleCount;
            break;
        case HT_STRONG:
            --pRef->m_nStrongHandleCount;
            break;
        case HT_UNDO:
            --pRef->m_nUndoHandleCount;
            break;
    }

// 	if ( handleType != HT_WEAK )
// 	{
// 		int nCount = handleType == HT_STRONG ? pRef->m_nStrongHandleCount : pRef->m_nUndoHandleCount;
// 		Msg( "OnElementReferenceRemoved: {} 0x{:x} \"{}\" referenced by {} handle\n",
// 			GetElementType( hElement ).String(), hElement, GetElementName( hElement ),
// 			handleType == HT_STRONG ? "strong" : "undo" );
// 	}

    if (handleType != HT_WEAK) {
        bool bUpdate = handleType == HT_STRONG ? !pRef->IsStronglyReferenced() : !pRef->IsReferencedByUndo();
        if (bUpdate) {
            CDmElement *pElement = GetElement(hElement);
            if (!pElement || pElement->GetFileId() != DMFILEID_INVALID) {
                m_bDeleteOrphanedElements = true;
            }
        }
    }
}

void CDataModel::RemoveUnreferencedElements() {
    if (m_bDeleteOrphanedElements) {
        FindAndDeleteOrphanedElements();
        m_bDeleteOrphanedElements = false;
    }
}

void UpdateReferenceToElementsNotInUndoWithParityBitSet(CDmAttribute *pAttr, CDmElement *pChild, int nParityBit,
                                                        bool bDetach) {
    if (!pAttr || !pChild)
        return;

    if (pChild->IsOnlyInUndo() || !pChild->GetParity(nParityBit))
        return;

    DmElementReference_t *pRef = pChild->GetReference();

    if (bDetach) {
// 		Msg( "removing " );
        pRef->RemoveAttribute(pAttr);
    } else {
        Assert(!pChild->GetReference()->FindAttribute(pAttr));
// 		Msg( "adding   " );
        pRef->AddAttribute(pAttr);
    }

// 	CDmElement *pOwner = pAttr->GetOwner();
// 	Msg( "reference to {} 0x{:x} '{}' from {} 0x{:x} '{}' {}\n",
// 		pChild->GetTypeString(), pChild->GetHandle(), pChild->GetName(),
// 		pOwner->GetTypeString(), pOwner->GetHandle(), pOwner->GetName(),
// 		pAttr->GetName() );
}

void UpdateReferencesToElementsNotInUndoWithParityBitSet(CDmElement *pElement, int nParityBit, bool bDetach) {
    for (CDmAttribute *pAttr = pElement->FirstAttribute(); pAttr; pAttr = pAttr->NextAttribute()) {
        DmAttributeType_t type = pAttr->GetType();
        if (type == AT_ELEMENT) {
            UpdateReferenceToElementsNotInUndoWithParityBitSet(pAttr, pAttr->GetValueElement<CDmElement>(), nParityBit,
                                                               bDetach);
        } else if (type == AT_ELEMENT_ARRAY) {
            const CDmrElementArrayConst<> elementArray(pAttr);
            int nChildren = elementArray.Count();
            for (int i = 0; i < nChildren; ++i) {
                UpdateReferenceToElementsNotInUndoWithParityBitSet(pAttr, elementArray[i], nParityBit, bDetach);
            }
        }
    }
}


void CDataModel::FindAndDeleteOrphanedElements() {
    // mark & sweep algorithm for elements

    enum {
        BIT_STRONG = 0,
        BIT_UNDO = 1,
    };

    // clear accessible flag from all elements
    int nHandles = (int) m_Handles.GetHandleCount();
    for (int i = 0; i < nHandles; ++i) {
        DmElementHandle_t hElement = (DmElementHandle_t) m_Handles.GetHandleFromIndex(i);
        CDmElement *pElement = m_Handles.GetHandle(hElement,
                                                   false); // walk through invalidated handles as well (such as deleted elements)
        if (!pElement)
            continue;

        DmFileId_t fileid = pElement->GetFileId();
        if (fileid == DMFILEID_INVALID)
            continue;

        pElement->SetParity(false, BIT_STRONG);
        pElement->SetParity(false, BIT_UNDO);
    }

    // mark elements accessible from undo system
    for (int i = 0; i < nHandles; ++i) {
        DmElementHandle_t hElement = (DmElementHandle_t) m_Handles.GetHandleFromIndex(i);
        CDmElement *pElement = m_Handles.GetHandle(hElement,
                                                   false); // walk through invalidated handles as well (such as deleted elements)
        if (!pElement)
            continue;

        DmFileId_t fileid = pElement->GetFileId();
        if (fileid == DMFILEID_INVALID)
            continue;

        if (pElement->GetReference()->m_nUndoHandleCount == 0)
            continue;

        pElement->SetParity(true, TD_ALL, BIT_UNDO);
    }

    // mark elements accessible from file roots
    int nFiles = NumFileIds();
    for (int i = 0; i < nFiles; ++i) {
        DmFileId_t fileid = GetFileId(i);
        if (fileid == DMFILEID_INVALID)
            continue;

        DmElementHandle_t hRoot = GetFileRoot(fileid);
        CDmElement *pRoot = GetElement(hRoot);
        if (!pRoot)
            continue;

        pRoot->SetParity(true, TD_ALL, BIT_STRONG);
    }

    // mark elements accessible from counted handles
    for (DmElementHandle_t hElement = FirstAllocatedElement();
         hElement != DMELEMENT_HANDLE_INVALID; hElement = NextAllocatedElement(hElement)) {
        CDmElement *pElement = GetElement(hElement);
        if (!pElement)
            continue;

        DmFileId_t fileid = pElement->GetFileId();
        if (fileid == DMFILEID_INVALID)
            continue;

        // an element is deleted when its strong handle count goes to zero, but if it never leaves zero, it shouldn't be deleted
        DmElementReference_t *pRef = pElement->GetReference();
        if (pRef->m_bHasEverBeenReferenced && pRef->m_nStrongHandleCount == 0)
            continue;

        pElement->SetParity(true, TD_ALL, BIT_STRONG);
    }


    // BIT_STRONG,  IsOnlyInUndo() => being moved out of undo
    // BIT_STRONG, !IsOnlyInUndo() => remaining out of undo
    // BIT_UNDO,    IsOnlyInUndo() => remaining in undo
    // BIT_UNDO,   !IsOnlyInUndo() => being moved into undo

    // update element references
    for (DmElementHandle_t hElement = FirstAllocatedElement();
         hElement != DMELEMENT_HANDLE_INVALID; hElement = NextAllocatedElement(hElement)) {
        CDmElement *pElement = GetElement(hElement);
        if (!pElement)
            continue;

        DmFileId_t fileid = pElement->GetFileId();
        if (fileid == DMFILEID_INVALID)
            continue;

        if (pElement->GetParity(BIT_STRONG)) {
            if (pElement->IsOnlyInUndo()) {
                // this element is leaving the undo system - add references to elements that aren't in undo (and weren't before)
                UpdateReferencesToElementsNotInUndoWithParityBitSet(pElement, BIT_STRONG, false);
            }
        } else if (pElement->GetParity(BIT_UNDO)) {
            if (!pElement->IsOnlyInUndo()) {
                // this element is entering the undo system - remove references to elements that aren't in undo (and weren't before)
                UpdateReferencesToElementsNotInUndoWithParityBitSet(pElement, BIT_STRONG, true);
            }
        }
    }

    // delete elements that aren't accessible
    for (DmElementHandle_t hElement = FirstAllocatedElement();
         hElement != DMELEMENT_HANDLE_INVALID; hElement = NextAllocatedElement(hElement)) {
        CDmElement *pElement = GetElement(hElement);
        if (!pElement)
            continue;

        DmFileId_t fileid = pElement->GetFileId();
        if (fileid == DMFILEID_INVALID)
            continue;

// 		Msg( "{} 0x{:08x} '{}': {} {} - {}\n",
// 			pElement->GetTypeString(), hElement, pElement->GetName(),
// 			pElement->GetParity( BIT_STRONG ) ? "strong" : "!strong",
// 			pElement->GetParity( BIT_UNDO ) ? "undo" : "!undo",
// 			pElement->IsOnlyInUndo() ? "only in undo" : "not only in undo" );

        if (pElement->GetParity(BIT_STRONG)) {
            if (pElement->IsOnlyInUndo()) {
// 				Msg( "adopting  {} 0x{:08x} '{}'\n", pElement->GetTypeString(), hElement, pElement->GetName() );
                pElement->SetOnlyInUndo(false);
                pElement->OnAdoptedFromUndo();
            }
        } else if (pElement->GetParity(BIT_UNDO)) {
            if (!pElement->IsOnlyInUndo()) {
// 				Msg( "orphaning {} 0x{:08x} '{}'\n", pElement->GetTypeString(), hElement, pElement->GetName() );
                pElement->SetOnlyInUndo(true);
                pElement->OnOrphanedToUndo();
            }
        } else {
//			Msg( "deleting {} 0x{:08x} '{}'\n", pElement->GetTypeString(), hElement, pElement->GetName() );
            DeleteElement(hElement);
        }
    }
}


DmElementHandle_t CDataModel::FindElement(const DmObjectId_t &id) {
    UtlHashHandle_t h = m_elementIds.Find(id);
    if (h == m_elementIds.InvalidHandle())
        return DMELEMENT_HANDLE_INVALID;

    return m_elementIds[h];
}

void CDataModel::GetExistingElements(CElementIdHash &hash) const {
    DMX_PROFILE_SCOPE(GetExistingElements_Copy);
    hash = m_elementIds;
}


DmAttributeReferenceIterator_t CDataModel::FirstAttributeReferencingElement(DmElementHandle_t hElement) {
    DmElementReference_t *pRef = FindElementReference(hElement);
    if (!pRef || pRef->m_attributes.m_hAttribute == DMATTRIBUTE_HANDLE_INVALID)
        return DMATTRIBUTE_REFERENCE_ITERATOR_INVALID;

    return (DmAttributeReferenceIterator_t) (uint64_t) &pRef->m_attributes;
}

DmAttributeReferenceIterator_t CDataModel::NextAttributeReferencingElement(DmAttributeReferenceIterator_t hAttrIter) {
    DmAttributeList_t *pList = (DmAttributeList_t *) hAttrIter;
    if (!pList)
        return DMATTRIBUTE_REFERENCE_ITERATOR_INVALID;

    return (DmAttributeReferenceIterator_t) (intptr_t) pList->m_pNext;
}

CDmAttribute *CDataModel::GetAttribute(DmAttributeReferenceIterator_t hAttrIter) {
    DmAttributeList_t *pList = (DmAttributeList_t *) hAttrIter;
    if (!pList)
        return nullptr;

    return GetAttribute(pList->m_hAttribute);
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : buf -
// Output : IDmElementInternal
//-----------------------------------------------------------------------------
CDmElement *CDataModel::Unserialize(CUtlBuffer &buf) {
    return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : *element -
//			buf -
//-----------------------------------------------------------------------------
void CDataModel::Serialize(CDmElement *element, CUtlBuffer &buf) {
}


// NOTE: The name of this extern is defined by the macro IMPLEMENT_ELEMENT_FACTORY
extern DmElementTypeInfo g_CDmElement_TypeInfo;

void CDataModel::InstallElementTypes(const DmElementTypeInfo *pFirst) {
    if (!m_UnknownElementTypeName.empty())
        return; // already installed

    for (const DmElementTypeInfo *pType = pFirst; pType; pType = pType->m_pNext) {
        auto [it, inserted] = m_ElementTypes.try_emplace(pType->m_TypeName, pType);
        if (!inserted) {
            it->second = pType;
            Warning("Factory for element type '{}' already exists\n", pType->m_TypeName);
        }
        // Can't be done at static-init time: needs the datamodel's symbol table
        pType->m_pSetTypeSymbol(GetSymbol(pType->m_TypeName.data()));
    }

    // From now on (typed mode) elements of unregistered types are plain CDmElements of type "DmElement"
    m_UnknownElementTypeName = g_CDmElement_TypeInfo.m_TypeName;
}

const DmElementTypeInfo *CDataModel::FindElementType(std::string_view elementType) const {
    const auto it = m_ElementTypes.find(elementType);
    return it != m_ElementTypes.end() ? it->second : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Creates a scene object
//-----------------------------------------------------------------------------
DmElementHandle_t CDataModel::CreateElement(CUtlSymbolLarge typeSymbol, const char *pElementName, DmFileId_t fileid,
                                            const DmObjectId_t *pObjectID) {
    return CreateElement(typeSymbol.String(), pElementName, fileid, pObjectID);
}

DmElementHandle_t CDataModel::CreateElement(const char *pElementType, const char *pElementName, DmFileId_t fileid,
                                            const DmObjectId_t *pObjectID) {
    DMX_PROFILE_SCOPE(CDataModel_CreateElement1);

    Assert(!pObjectID || m_elementIds.Find(*pObjectID) == m_elementIds.InvalidHandle());

    UtlHashHandle_t h = pObjectID ? m_unloadedIdElementMap.Find(ElementIdHandlePair_t(*pObjectID))
                                  : m_unloadedIdElementMap.InvalidHandle();
    if (h != m_unloadedIdElementMap.InvalidHandle()) {
        CDmElement *pElement = CreateElement(m_unloadedIdElementMap[h].m_ref, pElementType, pElementName, fileid,
                                             pObjectID);
        if (pElement) {
            m_unloadedIdElementMap.Remove(h);
            return pElement->GetHandle();
        }
    } else {
        DmElementHandle_t hElement = AcquireElementHandle();
        CDmElement *pElement = CreateElement(DmElementReference_t(hElement), pElementType, pElementName, fileid,
                                             pObjectID);
        if (pElement)
            return pElement->GetHandle();

        ReleaseElementHandle(hElement);
    }

    return DMELEMENT_HANDLE_INVALID;
}

//-----------------------------------------------------------------------------
// CreateElement references the attribute list passed in via ref, so don't edit or purge ref's attribute list afterwards
// this is kosher because the ref either is created on the fly and has no attributes, or is being removed from m_unloadedIdElementMap
//-----------------------------------------------------------------------------
CDmElement *
CDataModel::CreateElement(const DmElementReference_t &ref, const char *pElementType, const char *pElementName,
                          DmFileId_t fileid, const DmObjectId_t *pObjectID) {
    DMX_PROFILE_SCOPE(CDataModel_CreateElement2);

//	Msg( "Creating {} 0x{:x} '{}' in file \"{}\" - {:d} elements loaded\n", pElementType, ref.m_hElement, pElementName ? pElementName : "", GetFileName( fileid ), m_elementIds.Count() );


    DmPhase_t phase = g_pDmElementFramework->GetPhase();
    if (phase != PH_EDIT) {
        Assert(0);
        return nullptr;
    }

    // Create a new id if we weren't given one to use
    DmObjectId_t newId;
    if (!pObjectID) {
        CreateUniqueId(&newId);
        pObjectID = &newId;
    }

    if (!pElementName) {
        pElementName = UNNAMED_ELEMENT_NAME;
    }

    CDmElement *pElement = nullptr;
    if (m_bOnlyCreateUntypedElements) {
        // Untyped mode (dmxconvert, old-version conversion) keeps every element's own type name
        pElement = CDmElement::CreateInstance(ref.m_hElement, pElementType, *pObjectID, pElementName, fileid);
    } else if (const DmElementTypeInfo *pType = pElementType ? FindElementType(pElementType) : nullptr) {
        m_bUnableToCreateOnlyUntypedElements = true;
        // Abstract types have no create function and can't be instanced
        if (pType->m_pCreate) {
            pElement = pType->m_pCreate(ref.m_hElement, pType->m_TypeName.data(), *pObjectID, pElementName, fileid);
        }
    } else {
        const char *pTypeName = !m_UnknownElementTypeName.empty() ? m_UnknownElementTypeName.data() : pElementType;
        pElement = CDmElement::CreateInstance(ref.m_hElement, pTypeName, *pObjectID, pElementName, fileid);
    }

    if (pElement) {
        DMX_PROFILE_SCOPE(CreateElement_PerformConstruction);

        ++m_nElementsAllocatedSoFar;
        m_nMaxNumberOfElements = MAX(m_nMaxNumberOfElements, GetAllocatedElementCount());

        pElement->SetReference(ref);
        m_Handles.SetHandle(ref.m_hElement, pElement);
        m_elementIds.Insert(ref.m_hElement);

        pElement->PerformConstruction();
        pElement->EnableOnChangedCallbacks();

        NotifyState(NOTIFY_CHANGE_TOPOLOGICAL);

#ifdef _ELEMENT_HISTOGRAM_
                                                                                                                                CUtlSymbolLarge typeSym = GetSymbol( pElementType );
		++g_typeHistogram[ typeSym ];
#endif _ELEMENT_HISTOGRAM_
    }

    return pElement;
}


//-----------------------------------------------------------------------------
// Purpose: Destroys a scene object
//-----------------------------------------------------------------------------
void CDataModel::DestroyElement(DmElementHandle_t hElement) {
    DmPhase_t phase = g_pDmElementFramework->GetPhase();
    if (phase != PH_EDIT && phase !=
                            PH_EDIT_APPLY) // need to allow edit_apply to delete elements, so that cascading deletes can occur in one phase
    {
        Assert(0);
        return;
    }

    if (hElement == DMELEMENT_HANDLE_INVALID)
        return;

    CDmElement *pElement = m_Handles.GetHandle(hElement);
    if (pElement == nullptr)
        return;


    DeleteElement(hElement);
}

void CDataModel::DeleteElement(DmElementHandle_t hElement, DmHandleReleasePolicy hrp /* = HR_ALWAYS */ ) {
    DmPhase_t phase = g_pDmElementFramework->GetPhase();
    if (phase != PH_EDIT && phase != PH_EDIT_APPLY) {
        Assert(0);
        return;
    }

    if (hElement == DMELEMENT_HANDLE_INVALID)
        return;

    CDmElement *pElement = m_Handles.GetHandle(hElement);
    if (pElement == nullptr)
        return;

    UtlHashHandle_t h = m_elementIds.Find(pElement->GetId());
    Assert(h != m_elementIds.InvalidHandle());
    if (h != m_elementIds.InvalidHandle()) {
        m_elementIds.Remove(h);
    }

    DmElementReference_t *pRef = pElement->GetReference();
    bool bReleaseHandle = hrp == HR_ALWAYS || (hrp == HR_IF_NOT_REFERENCED && !pRef->IsWeaklyReferenced());
    if (!bReleaseHandle) {
        m_unloadedIdElementMap.Insert(ElementIdHandlePair_t(GetElementId(hElement), *pRef));
    }

    pElement->PerformDestruction();

    // NOTE: Attribute destruction has to happen before the containing object is destroyed
    // because the inline optimization will crash otherwise, and after PerformDestruction
    // or else PerformDestruction will crash
    pElement->Purge();

    // Virtual destructor: runs the element class's destructor and its pooled operator delete
    delete pElement;
    if (bReleaseHandle) {
        ReleaseElementHandle(hElement);
    } else {
        MarkHandleInvalid(hElement);
    }
}


//-----------------------------------------------------------------------------
// handle-related methods
//-----------------------------------------------------------------------------
DmElementHandle_t CDataModel::AcquireElementHandle() {
    NotifyState(NOTIFY_CHANGE_TOPOLOGICAL);
    return (DmElementHandle_t) m_Handles.AddHandle();
}

void CDataModel::ReleaseElementHandle(DmElementHandle_t hElement) {
    m_Handles.RemoveHandle(hElement);
    NotifyState(NOTIFY_CHANGE_TOPOLOGICAL);
}

void CDataModel::MarkHandleInvalid(DmElementHandle_t hElement) {
    m_Handles.MarkHandleInvalid(hElement);
    NotifyState(NOTIFY_CHANGE_TOPOLOGICAL);
}

void CDataModel::MarkHandleValid(DmElementHandle_t hElement) {
    m_Handles.MarkHandleValid(hElement);
    NotifyState(NOTIFY_CHANGE_TOPOLOGICAL);
}


CDmElement *CDataModel::GetElement(DmElementHandle_t hElement) const {
    return (hElement != DMELEMENT_HANDLE_INVALID) ? m_Handles.GetHandle(hElement) : nullptr;
}

CUtlSymbolLarge CDataModel::GetElementType(DmElementHandle_t hElement) const {
    CDmElement *pElement = (hElement != DMELEMENT_HANDLE_INVALID) ? m_Handles.GetHandle(hElement) : nullptr;
    if (pElement == nullptr)
        return UTL_INVAL_SYMBOL_LARGE;
    return pElement->GetType();
}

const char *CDataModel::GetElementName(DmElementHandle_t hElement) const {
    CDmElement *pElement = (hElement != DMELEMENT_HANDLE_INVALID) ? m_Handles.GetHandle(hElement) : nullptr;
    if (pElement == nullptr)
        return "";
    return pElement->GetName();
}

const DmObjectId_t &CDataModel::GetElementId(DmElementHandle_t hElement) const {
    CDmElement *pElement = (hElement != DMELEMENT_HANDLE_INVALID) ? m_Handles.GetHandle(hElement) : nullptr;
    if (pElement == nullptr) {
        static DmObjectId_t s_id;
        InvalidateUniqueId(&s_id);
        return s_id;
    }
    return pElement->GetId();
}


//-----------------------------------------------------------------------------
// Attribute types
//-----------------------------------------------------------------------------
const char *CDataModel::GetAttributeNameForType(DmAttributeType_t attType) const {
    return AttributeTypeName(attType);
}

DmAttributeType_t CDataModel::GetAttributeTypeForName(const char *name) const {
    return AttributeType(name);
}


//-----------------------------------------------------------------------------
//
// Methods related to notification callbacks
//
//-----------------------------------------------------------------------------
bool CDataModel::InstallNotificationCallback(IDmNotify *pNotify) {
    return m_NotifyMgr.InstallNotificationCallback(pNotify);
}

void CDataModel::RemoveNotificationCallback(IDmNotify *pNotify) {
    m_NotifyMgr.RemoveNotificationCallback(pNotify);
}

bool CDataModel::IsSuppressingNotify() const {
    return GetNotifyMgr()->IsSuppressingNotify();
}

void CDataModel::SetSuppressingNotify(bool bSuppress) {
    GetNotifyMgr()->SetSuppressingNotify(bSuppress);
}

void CDataModel::PushNotificationScope(const char *pReason, int nNotifySource, int nNotifyFlags) {
    GetNotifyMgr()->PushNotificationScope(pReason, nNotifySource, nNotifyFlags);
}

void CDataModel::PopNotificationScope(bool bAbort) {
    GetNotifyMgr()->PopNotificationScope(bAbort);
}


//-----------------------------------------------------------------------------
//
// Methods related to attribute handles
//
//-----------------------------------------------------------------------------
DmAttributeHandle_t CDataModel::AcquireAttributeHandle(CDmAttribute *pAttribute) {
    DmAttributeHandle_t hAttribute = (DmAttributeHandle_t) m_AttributeHandles.AddHandle();
    m_AttributeHandles.SetHandle(hAttribute, pAttribute);
    return hAttribute;
}

void CDataModel::ReleaseAttributeHandle(DmAttributeHandle_t hAttribute) {
    if (hAttribute != DMATTRIBUTE_HANDLE_INVALID) {
        m_AttributeHandles.RemoveHandle(hAttribute);
    }
}

CDmAttribute *CDataModel::GetAttribute(DmAttributeHandle_t h) {
    return m_AttributeHandles.GetHandle(h);
}

bool CDataModel::IsAttributeHandleValid(DmAttributeHandle_t h) const {
    return m_AttributeHandles.IsHandleValid(h);
}


// Commits symbols in symbol table
void CDataModel::CommitSymbols() {
    m_SymbolTable.Commit();
}

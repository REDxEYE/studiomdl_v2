//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#pragma once
#include "tier1_v2/function_timing.hpp"


#include "datamodel/dmattribute.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmehandle.h"
#include "tier1/uniqueid.h"
#include "tier1_v2/string_utils.hpp"
#include <map>
#include <string_view>
#include "tier1/utlhandletable.h"
#include "tier1/utlhash.h"
#include "notifymanager.h"


//-----------------------------------------------------------------------------
// forward declarations
//-----------------------------------------------------------------------------
class IDmElementFramework;
class CDmElement;
struct DmElementTypeInfo;

enum DmHandleReleasePolicy {
    HR_ALWAYS,
    HR_NEVER,
    HR_IF_NOT_REFERENCED,
};


//-----------------------------------------------------------------------------
// memory categories
//-----------------------------------------------------------------------------
enum {
    MEMORY_CATEGORY_OUTER,
    MEMORY_CATEGORY_ELEMENT_INTERNAL,
    MEMORY_CATEGORY_DATAMODEL,
    MEMORY_CATEGORY_REFERENCES,
    MEMORY_CATEGORY_ATTRIBUTE_TREE,
    MEMORY_CATEGORY_ATTRIBUTE_OVERHEAD,
    MEMORY_CATEGORY_ATTRIBUTE_DATA,
    MEMORY_CATEGORY_ATTRIBUTE_COUNT,

    MEMORY_CATEGORY_COUNT,
};


//-----------------------------------------------------------------------------
// hash map of id->element, with the id storage optimized out
//-----------------------------------------------------------------------------
class CElementIdHash : public CUtlHash<DmElementHandle_t> {
public:
    CElementIdHash(int nBucketCount = 0, int nGrowCount = 0, int nInitCount = 0)
        : CUtlHash<DmElementHandle_t>(nBucketCount, nGrowCount, nInitCount, CompareFunc, KeyFunc) {
    }

protected:
    typedef CUtlHash<DmElementHandle_t> BaseClass;

    static bool CompareFunc(DmElementHandle_t const &a, DmElementHandle_t const &b) { return a == b; }

    static bool IdCompareFunc(DmElementHandle_t const &hElement, DmObjectId_t const &id) {
        CDmElement* pElement = g_pDataModel->GetElement(hElement);
        Assert(pElement);
        if (!pElement)
            return false;

        return IsUniqueIdEqual(id, pElement->GetId());
    }

    static unsigned int KeyFunc(DmElementHandle_t const &hElement) {
        CDmElement* pElement = g_pDataModel->GetElement(hElement);
        Assert(pElement);
        if (!pElement)
            return 0;

        return *(unsigned int*) &pElement->GetId();
    }

    static unsigned int IdKeyFunc(DmObjectId_t const &src) {
        return *(unsigned int*) &src;
    }

protected:
    bool DoFind(DmObjectId_t const &src, unsigned int* pBucket, int* pIndex) {
        // generate the data "key"
        unsigned int key = IdKeyFunc(src);

        // hash the "key" - get the correct hash table "bucket"
        unsigned int ndxBucket;
        if (m_bPowerOfTwo) {
            *pBucket = ndxBucket = (key & m_ModMask);
        }
        else {
            int bucketCount = m_Buckets.size();
            *pBucket = ndxBucket = key % bucketCount;
        }

        int ndxKeyData;
        CUtlVector<DmElementHandle_t> &bucket = m_Buckets[ndxBucket];
        int keyDataCount = bucket.size();
        for (ndxKeyData = 0; ndxKeyData < keyDataCount; ndxKeyData++) {
            if (IdCompareFunc(bucket.at(ndxKeyData), src))
                break;
        }

        if (ndxKeyData == keyDataCount)
            return false;

        *pIndex = ndxKeyData;
        return true;
    }

public:
    UtlHashHandle_t Find(DmElementHandle_t const &src) { return BaseClass::Find(src); }

    UtlHashHandle_t Find(DmObjectId_t const &src) {
        vstd::FunctionTimingRecorder _("DMX");
        unsigned int ndxBucket;
        int ndxKeyData;

        if (DoFind(src, &ndxBucket, &ndxKeyData))
            return BuildHandle(ndxBucket, ndxKeyData);

        return InvalidHandle();
    }
};

//-----------------------------------------------------------------------------
// struct to hold the set of elements in any given file
//-----------------------------------------------------------------------------

struct FileElementSet_t {
    FileElementSet_t(CUtlSymbolLarge filename = UTL_INVAL_SYMBOL_LARGE,
                     CUtlSymbolLarge format = UTL_INVAL_SYMBOL_LARGE) : m_filename(filename), m_format(format),
                                                                        m_hRoot(DMELEMENT_HANDLE_INVALID),
                                                                        m_bLoaded(true),
                                                                        m_nElements(0),
                                                                        m_fileModificationTime(0) {
    }

    FileElementSet_t(const FileElementSet_t &that) : m_filename(that.m_filename), m_format(that.m_format),
                                                     m_hRoot(DMELEMENT_HANDLE_INVALID), m_bLoaded(that.m_bLoaded),
                                                     m_nElements(that.m_nElements) {
        // the only time this should be copy constructed is when passing in an empty set to the parent array
        // otherwise it could get prohibitively expensive time and memory wise
        Assert(that.m_nElements == 0);
    }

    CUtlSymbolLarge m_filename;
    CUtlSymbolLarge m_format;
    CDmeCountedHandle m_hRoot;
    bool m_bLoaded;
    int m_nElements;
    int64_t m_fileModificationTime;
};


//-----------------------------------------------------------------------------
// Purpose: Versionable factor for element types
//-----------------------------------------------------------------------------
class CDataModel : public IDataModel {
public:
    CDataModel();

    virtual ~CDataModel();

    // External interface
public:
    bool Init() override;

    void Shutdown() override;

    // Registers the linked element classes (list from DmElementTypeInfo::s_pFirst) and sets their type symbols
    void InstallElementTypes(const DmElementTypeInfo* pFirst);

    // Methods of IDataModel
    DmElementHandle_t CreateElement(std::string_view typeName, std::string_view elementName, DmFileId_t fileid,
                                    const DmObjectId_t* pObjectID = nullptr) override;

    void DestroyElement(DmElementHandle_t hElement) override;

    CDmElement* GetElement(DmElementHandle_t hElement) const override;

    CUtlSymbolLarge GetElementType(DmElementHandle_t hElement) const override;

    const char* GetElementName(DmElementHandle_t hElement) const override;

    const DmObjectId_t& GetElementId(DmElementHandle_t hElement) const override;

    const char* GetAttributeNameForType(DmAttributeType_t attType) const override;

    DmAttributeType_t GetAttributeTypeForName(const char* name) const override;

    void AddSerializer(IDmSerializer* pSerializer) override;

    void AddLegacyUpdater(IDmLegacyUpdater* pUpdater) override;

    void AddFormatUpdater(IDmFormatUpdater* pUpdater) override;

    const char* GetFormatExtension(const char* pFormatName) override;

    const char* GetFormatDescription(const char* pFormatName) override;

    int GetFormatCount() const override;

    const char* GetFormatName(int i) const override;

    const char* GetDefaultEncoding(const char* pFormatName) override;

    int GetEncodingCount() const override;

    const char* GetEncodingName(int i) const override;

    bool IsEncodingBinary(const char* pEncodingName) const override;

    bool DoesEncodingStoreVersionInFile(const char* pEncodingName) const override;

    void SetSerializationDelimiter(CUtlCharConversion* pConv) override;

    void SetSerializationArrayDelimiter(const char* pDelimiter) override;

    bool IsUnserializing() override;

    bool Serialize(CUtlBuffer &outBuf, const char* pEncodingName, const char* pFormatName,
                   DmElementHandle_t hRoot) override;

    bool Unserialize(CUtlBuffer &buf, const char* pEncodingName, const char* pSourceFormatName, const char* pFormatHint,
                     const char* pFileName, DmConflictResolution_t idConflictResolution,
                     DmElementHandle_t &hRoot) override;

    bool UpdateUnserializedElements(const char* pSourceFormatName, int nSourceFormatVersion,
                                    DmFileId_t fileid, DmConflictResolution_t idConflictResolution,
                                    CDmElement** ppRoot) override;

    IDmSerializer* FindSerializer(const char* pEncodingName) const override;

    IDmLegacyUpdater* FindLegacyUpdater(const char* pLegacyFormatName) const override;

    IDmFormatUpdater* FindFormatUpdater(const char* pFormatName) const override;

    bool SaveToFile(char const* pFileName, char const* pPathID, const char* pEncodingName, const char* pFormatName,
                    CDmElement* pRoot) override;

    DmFileId_t RestoreFromFile(char const* pFileName, char const* pPathID, const char* pFormatHint, CDmElement** ppRoot,
                               DmConflictResolution_t idConflictResolution = CR_DELETE_NEW,
                               DmxHeader_t* pHeaderOut = nullptr) override;

    bool IsDMXFormat(CUtlBuffer &buf) const override;

    void SetKeyValuesElementCallback(IElementForKeyValueCallback* pCallbackInterface) override;

    std::optional<std::string_view> GetKeyValuesElementName(std::string_view keyName, int iNestingLevel) override;

    CUtlSymbolLarge GetSymbol(std::string_view pString) override;

    int GetElementsAllocatedSoFar() override;

    int GetMaxNumberOfElements() override;

    int GetAllocatedAttributeCount() override;

    int GetAllocatedElementCount() override;

    DmElementHandle_t FirstAllocatedElement() override;

    DmElementHandle_t NextAllocatedElement(DmElementHandle_t hElement) override;

    int EstimateMemoryUsage(DmElementHandle_t hElement, TraversalDepth_t depth = TD_DEEP) override;

    CDmAttribute* GetAttribute(DmAttributeHandle_t h) override;

    bool IsAttributeHandleValid(DmAttributeHandle_t h) const override;

    void OnlyCreateUntypedElements(bool bEnable) override;

    int NumFileIds() override;

    DmFileId_t GetFileId(int i) override;

    DmFileId_t FindOrCreateFileId(const char* pFilename) override;

    void RemoveFileId(DmFileId_t fileid) override;

    DmFileId_t GetFileId(const char* pFilename) override;

    const char* GetFileName(DmFileId_t fileid) override;

    void SetFileName(DmFileId_t fileid, const char* pFileName) override;

    const char* GetFileFormat(DmFileId_t fileid) override;

    void SetFileFormat(DmFileId_t fileid, const char* pFormat) override;

    DmElementHandle_t GetFileRoot(DmFileId_t fileid) override;

    void SetFileRoot(DmFileId_t fileid, DmElementHandle_t hRoot) override;

    int64_t GetFileModificationUTCTime(DmFileId_t fileid) override;

    int64_t GetCurrentUTCTime() override;

    void UTCTimeToString(char* pString, int maxChars, int64_t fileTime) override;

    bool IsFileLoaded(DmFileId_t fileid) override;

    void MarkFileLoaded(DmFileId_t fileid) override;

    void UnloadFile(DmFileId_t fileid) override;

    int NumElementsInFile(DmFileId_t fileid) override;

    void DontAutoDelete(DmElementHandle_t hElement) override;

    void MarkHandleInvalid(DmElementHandle_t hElement) override;

    void MarkHandleValid(DmElementHandle_t hElement) override;

    DmElementHandle_t FindElement(const DmObjectId_t &id) override;

    void GetExistingElements(CElementIdHash &hash) const override;

    DmAttributeReferenceIterator_t FirstAttributeReferencingElement(DmElementHandle_t hElement) override;

    DmAttributeReferenceIterator_t NextAttributeReferencingElement(DmAttributeReferenceIterator_t hAttrIter) override;

    CDmAttribute* GetAttribute(DmAttributeReferenceIterator_t hAttrIter) override;

    bool InstallNotificationCallback(IDmNotify* pNotify) override;

    void RemoveNotificationCallback(IDmNotify* pNotify) override;

    bool IsSuppressingNotify() const override;

    void SetSuppressingNotify(bool bSuppress) override;

    void PushNotificationScope(const char* pReason, int nNotifySource, int nNotifyFlags) override;

    void PopNotificationScope(bool bAbort) override;

    void DisplayMemoryStats(DmElementHandle_t hElement = DMELEMENT_HANDLE_INVALID) override;

    // Dump the symbol table to the console
    void DumpSymbolTable() override;

public:
    // Commits symbols in symbol table
    void CommitSymbols();

    // Internal public methods
    int GetCurrentFormatVersion(const char* pFormatName);

    // CreateElement references the attribute list passed in via ref, so don't edit or purge ref's attribute list afterwards
    CDmElement* CreateElement(const DmElementReference_t &ref, std::string_view elementType, std::string_view elementName,
                              DmFileId_t fileid, const DmObjectId_t* pObjectID);

    void DeleteElement(DmElementHandle_t hElement, DmHandleReleasePolicy hrp = HR_ALWAYS);

    // element handle related methods
    DmElementHandle_t AcquireElementHandle();

    void ReleaseElementHandle(DmElementHandle_t hElement);

    // Handles to attributes
    DmAttributeHandle_t AcquireAttributeHandle(CDmAttribute* pAttribute);

    void ReleaseAttributeHandle(DmAttributeHandle_t hAttribute);

    // remove orphaned element subtrees
    void FindAndDeleteOrphanedElements();

    // search id->handle table (both loaded and unloaded) for id, and if not found, create a new handle, map it to the id and return it
    DmElementHandle_t FindOrCreateElementHandle(const DmObjectId_t &id);

    // changes an element's id and associated mappings - generally during unserialization
    DmElementHandle_t ChangeElementId(DmElementHandle_t hElement, const DmObjectId_t &oldId, const DmObjectId_t &newId);

    DmElementReference_t* FindElementReference(DmElementHandle_t hElement, DmObjectId_t** ppId = nullptr);

    void RemoveUnreferencedElements();

    void RemoveElementFromFile(DmElementHandle_t hElement, DmFileId_t fileid);

    void AddElementToFile(DmElementHandle_t hElement, DmFileId_t fileid);

    void NotifyState(int nNotifyFlags);

    int EstimateMemoryOverhead() const;

    bool IsCreatingUntypedElements() const { return m_bOnlyCreateUntypedElements; }

private:
    struct MailingList_t {
        CUtlVector<DmElementHandle_t> m_Elements;
    };

    struct ElementIdHandlePair_t {
        DmObjectId_t m_id;
        DmElementReference_t m_ref;

        ElementIdHandlePair_t() {
        }

        explicit ElementIdHandlePair_t(const DmObjectId_t &id) : m_ref() {
            CopyUniqueId(id, &m_id);
        }

        ElementIdHandlePair_t(const DmObjectId_t &id, const DmElementReference_t &ref) : m_ref(ref) {
            CopyUniqueId(id, &m_id);
        }

        ElementIdHandlePair_t(const ElementIdHandlePair_t &that) : m_ref(that.m_ref) {
            CopyUniqueId(that.m_id, &m_id);
        }

        ElementIdHandlePair_t& operator=(const ElementIdHandlePair_t &that) {
            CopyUniqueId(that.m_id, &m_id);
            m_ref = that.m_ref;
            return *this;
        }

        static unsigned int HashKey(const ElementIdHandlePair_t &that) {
            return *(unsigned int*) &that.m_id.m_Value;
        }

        static bool Compare(const ElementIdHandlePair_t &a, const ElementIdHandlePair_t &b) {
            return IsUniqueIdEqual(a.m_id, b.m_id);
        }
    };

    // Internals used by the rest of the datamodel library (attributes, handles, serializers)
public:
    CDmElement* Unserialize(CUtlBuffer &buf);

    // Registered type for the name (case-insensitive), nullptr if none
    const DmElementTypeInfo* FindElementType(std::string_view elementType) const;

    void Serialize(CDmElement* element, CUtlBuffer &buf);

    // Read the header, return the version (or false if it's not a DMX file)
    bool ReadDMXHeader(CUtlBuffer &inBuf, DmxHeader_t* pHeader) const;

    const char* GetEncodingFromLegacyFormat(const char* pLegacyFormatName) const;

    bool IsValidNonDMXFormat(const char* pFormatName) const;

    bool IsLegacyFormat(const char* pFormatName) const;

    // Returns the current notify manager
    CNotifyManager* GetNotifyMgr();

    const CNotifyManager* GetNotifyMgr() const;

    void UnloadFile(DmFileId_t fileid, bool bDeleteElements);

    void SetFileModificationUTCTime(DmFileId_t fileid, int64_t fileModificationTime);

    void OnElementReferenceAdded(DmElementHandle_t hElement, CDmAttribute* pAttribute);

    void OnElementReferenceRemoved(DmElementHandle_t hElement, CDmAttribute* pAttribute);

    void OnElementReferenceAdded(DmElementHandle_t hElement, HandleType_t handleType);

    void OnElementReferenceRemoved(DmElementHandle_t hElement, HandleType_t handleType);

    void BuildHistogramForHandles(std::map<CUtlSymbolLarge, struct DmMemoryInfo_t> &typeHistogram,
                                  CUtlVector<DmElementHandle_t> &handles);

private:
    CUtlVector<IDmSerializer*> m_Serializers;
    CUtlVector<IDmLegacyUpdater*> m_LegacyUpdaters;
    CUtlVector<IDmFormatUpdater*> m_FormatUpdaters;

    // Keyed by the registered type names (string literals of the IMPLEMENT_* macros), compared case-insensitively
    std::map<std::string_view, const DmElementTypeInfo*, vstd::iless> m_ElementTypes;
    // Type given to elements whose type isn't registered (typed mode only, set by InstallElementTypes); empty keeps
    // their own type name. Views a string literal, so data() is NUL-terminated.
    std::string_view m_UnknownElementTypeName;
    CUtlSymbolTableLarge m_SymbolTable;
    CUtlHandleTable<CDmElement, 21> m_Handles;
    CUtlHandleTable<CDmAttribute, 21> m_AttributeHandles;
    CNotifyManager m_NotifyMgr;

    bool m_bIsUnserializing: 1;
    bool m_bOnlyCreateUntypedElements: 1;
    bool m_bUnableToCreateOnlyUntypedElements: 1;
    bool m_bDeleteOrphanedElements: 1;

    CUtlHandleTable<FileElementSet_t, 20> m_openFiles;

    CElementIdHash m_elementIds;
    CUtlHash<ElementIdHandlePair_t> m_unloadedIdElementMap;

    IElementForKeyValueCallback* m_pKeyvaluesCallbackInterface;

    int m_nElementsAllocatedSoFar;
    int m_nMaxNumberOfElements;
};

//-----------------------------------------------------------------------------
// Singleton
//-----------------------------------------------------------------------------
extern CDataModel* g_pDataModelImp;


//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
inline CNotifyManager* CDataModel::GetNotifyMgr() {
    return &m_NotifyMgr;
}

inline const CNotifyManager* CDataModel::GetNotifyMgr() const {
    return &m_NotifyMgr;
}

inline void CDataModel::NotifyState(int nNotifyFlags) {
    GetNotifyMgr()->NotifyState(nNotifyFlags);
}


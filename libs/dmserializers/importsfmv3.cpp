//====== Copyright � 1996-2006, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "dmserializers.h"
#include "dmebaseimporter.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattributevar.h"
#include "tier1/utlbuffer.h"


//-----------------------------------------------------------------------------
// Format converter
//-----------------------------------------------------------------------------
class CImportSFMV3 : public CSFMBaseImporter {
    typedef CSFMBaseImporter BaseClass;

public:
    CImportSFMV3(char const* formatName, char const* nextFormatName);

private:
    bool DoFixup(CDmElement* pSourceRoot) override;


    void FixupElement(CDmElement* pElement);

    // Fixes up all elements
    void BuildList(CDmElement* pElement, CUtlRBTree<CDmElement*, int> &list);
};


//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
static CImportSFMV3 s_ImportSFMV3("sfm_v3", "sfm_v4");

void InstallSFMV3Importer(IDataModel* pFactory) {
    pFactory->AddLegacyUpdater(&s_ImportSFMV3);
}


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CImportSFMV3::CImportSFMV3(char const* formatName, char const* nextFormatName) : BaseClass(formatName, nextFormatName) {
}


struct LogToCurveInfoTypeMap_t {
    const std::string_view pLogType;
    const std::string_view pLogLayerType;
    const std::string_view pCurveInfoType;
};

std::array g_typeMap =
{
    LogToCurveInfoTypeMap_t{"DmeIntLog", "DmeIntLogLayer", "DmeIntCurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeFloatLog", "DmeFloatLogLayer", "DmeFloatCurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeBoolLog", "DmeBoolLogLayer", "DmeBoolCurveInfo"},
    // string,
    // void,
    // objectid,
    LogToCurveInfoTypeMap_t{"DmeColorLog", "DmeColorLogLayer", "DmeColorCurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeVector2Log", "DmeVector2LogLayer", "DmeVector2CurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeVector3Log", "DmeVector3LogLayer", "DmeVector3CurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeVector4Log", "DmeVector4LogLayer", "DmeVector4CurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeQAngleLog", "DmeQAngleLogLayer", "DmeQAngleCurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeQuaternionLog", "DmeQuaternionLogLayer", "DmeQuaternionCurveInfo"},
    LogToCurveInfoTypeMap_t{"DmeVMatrixLog", "DmeVMatrixLogLayer", "DmeVMatrixCurveInfo"},
};

static std::optional<std::string_view> GetCurveInfoTypeFromLogType(const std::string_view pLogType) {
    for (const auto &type: g_typeMap) {
        if (vstd::iequals(pLogType, type.pLogType))
            return type.pCurveInfoType;
    }
    return std::nullopt;
}

static bool IsLogLayerType(const std::string_view pLogLayerType) {
    return std::ranges::any_of(g_typeMap, [&pLogLayerType](const auto & type) {
        return vstd::iequals(pLogLayerType, type.pLogLayerType);
    });
}

void MoveAttribute(CDmElement* pFromElement, const char* pFromAttrName, CDmElement* pToElement = nullptr,
                   const char* pToAttrName = nullptr, DmAttributeType_t toType = AT_UNKNOWN) {
    if (!pToAttrName) {
        pToAttrName = pFromAttrName;
    }

    if (pToElement) {
        CDmAttribute* pFromAttr = pFromElement->GetAttribute(pFromAttrName);
        const void* pValue = pFromAttr->GetValueUntyped();
        DmAttributeType_t fromType = pFromAttr->GetType();
        if (toType == AT_UNKNOWN) {
            toType = fromType;
        }

        CDmAttribute* pToAttr = pToElement->AddAttribute(pToAttrName, toType);
        if (!pToAttr) {
            Warning("*** Problem in converter encountered!\n");
            Warning("*** Unable to find or add attribute \"{}\" to element \"{}\"!\n", pToAttrName,
                    pToElement->GetName());
        }
        else if (fromType != toType) {
            Warning("*** Problem in file encountered!\n");
            Warning("*** Element \"{}\" has attribute \"{}\" with an unexpected type!\n", pFromElement->GetName(),
                    pFromAttrName);
        }
        else {
            pToAttr->SetValue(toType, pValue);
        }
    }

    pFromElement->RemoveAttribute(pFromAttrName);
}

// Fixes up all elements
//-----------------------------------------------------------------------------
void CImportSFMV3::FixupElement(CDmElement* pElement) {
    if (!pElement)
        return;

    const auto pType = pElement->GetTypeString();

    // log layer
    if (IsLogLayerType(pType)) {
        pElement->RemoveAttribute("ownerlog");
        return;
    }

    // log
    const auto pCurveInfoType = GetCurveInfoTypeFromLogType(pType);
    if (!pCurveInfoType)
        return;

    CDmElement* pCurveInfo = nullptr;
    CDmAttribute* pUseCurveTypeAttr = pElement->GetAttribute("usecurvetypes");
    if (pUseCurveTypeAttr && pUseCurveTypeAttr->GetValue<bool>()) {
        DmElementHandle_t hElement = g_pDataModel->CreateElement("curve info", pCurveInfoType.value(), pElement->GetFileId());
        pCurveInfo = g_pDataModel->GetElement(hElement);
    }
    pElement->RemoveAttribute("usecurvetypes");

    MoveAttribute(pElement, "defaultcurvetype", pCurveInfo, "defaultCurveType", AT_INT);
    MoveAttribute(pElement, "defaultedgezerovalue", pCurveInfo, "defaultEdgeZeroValue");
    MoveAttribute(pElement, "useedgeinfo", pCurveInfo, "useEdgeInfo", AT_BOOL);
    MoveAttribute(pElement, "rightedgetime", pCurveInfo, "rightEdgeTime", AT_INT);
    MoveAttribute(pElement, "left_edge_active", pCurveInfo, "leftEdgeActive", AT_BOOL);
    MoveAttribute(pElement, "right_edge_active", pCurveInfo, "rightEdgeActive", AT_BOOL);
    MoveAttribute(pElement, "left_edge_curvetype", pCurveInfo, "leftEdgeCurveType", AT_INT);
    MoveAttribute(pElement, "right_edge_curvetype", pCurveInfo, "rightEdgeCurveType", AT_INT);
    MoveAttribute(pElement, "left_edge_value", pCurveInfo, "leftEdgeValue");
    MoveAttribute(pElement, "right_edge_value", pCurveInfo, "rightEdgeValue");
}

// Fixes up all elements
//-----------------------------------------------------------------------------
void CImportSFMV3::BuildList(CDmElement* pElement, CUtlRBTree<CDmElement*, int> &list) {
    if (!pElement)
        return;

    if (list.Find(pElement) != list.InvalidIndex())
        return;

    list.Insert(pElement);

    // Descend to bottom of tree, then do fixup coming back up the tree
    for (CDmAttribute* pAttribute = pElement->FirstAttribute(); pAttribute; pAttribute = pAttribute->NextAttribute()) {
        if (pAttribute->GetType() == AT_ELEMENT) {
            CDmElement* pElement = pAttribute->GetValueElement<CDmElement>();
            BuildList(pElement, list);
            continue;
        }

        if (pAttribute->GetType() == AT_ELEMENT_ARRAY) {
            CDmrElementArray<> array(pAttribute);
            int nCount = array.Count();
            for (int i = 0; i < nCount; ++i) {
                CDmElement* pChild = array[i];
                BuildList(pChild, list);
            }
            continue;
        }
    }
}

bool CImportSFMV3::DoFixup(CDmElement* pSourceRoot) {
    CUtlRBTree<CDmElement*, int> fixlist(0, 0, DefLessFunc(CDmElement *));
    BuildList(pSourceRoot, fixlist);
    for (int i = fixlist.FirstInorder(); i != fixlist.InvalidIndex(); i = fixlist.NextInorder(i)) {
        // Search and replace in the entire tree!
        FixupElement(fixlist[i]);
    }
    return true;
}

//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "dmserializers.h"
#include "dmebaseimporter.h"
#include "datamodel/idatamodel.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattributevar.h"

//-----------------------------------------------------------------------------
// Format converter
//-----------------------------------------------------------------------------
class CImportSFMV1 : public CSFMBaseImporter {
    typedef CSFMBaseImporter BaseClass;

public:
    CImportSFMV1(char const* formatName, char const* nextFormatName);

private:
    bool DoFixup(CDmElement* pElementInternal) override;

    // Fixes up a single time attribute - converting from float seconds to int tenths-of-a-millisecond
    void ConvertTimeAttribute(CDmElement* pElementInternal, const char* pOldName, const char* pNewName);

    // Fixes up a single timeframe
    void FixupTimeframe(CDmElement* pElementInternal);

    // Fixes up a single log - converting from int milliseconds to int tenths-of-a-millisecond
    void FixupLog(CDmElement* pElementInternal);

    CUtlRBTree<CDmElement*, int> m_fixedElements;
};


//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
static CImportSFMV1 s_ImportDmxV1("sfm_v1", "sfm_v2");

void InstallSFMV1Importer(IDataModel* pFactory) {
    pFactory->AddLegacyUpdater(&s_ImportDmxV1);
}


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CImportSFMV1::CImportSFMV1(char const* formatName, char const* nextFormatName) : BaseClass(formatName, nextFormatName) {
    m_fixedElements.SetLessFunc(DefLessFunc(CDmElement *));
}


//-----------------------------------------------------------------------------
// Fixes up all elements
//-----------------------------------------------------------------------------
bool CImportSFMV1::DoFixup(CDmElement* pElementInternal) {
    if (!pElementInternal)
        return true;

    if (m_fixedElements.Find(pElementInternal) != m_fixedElements.InvalidIndex())
        return true;

    m_fixedElements.Insert(pElementInternal);

    const auto pType = pElementInternal->GetTypeString();
    if (pType == "DmeTimeFrame") {
        FixupTimeframe(pElementInternal);
    }
    else if (pType == "DmeLog" ||
             pType == "DmeIntLog" ||
             pType == "DmeFloatLog" ||
             pType == "DmeBoolLog" ||
             pType == "DmeColorLog" ||
             pType == "DmeVector2Log" ||
             pType == "DmeVector3Log" ||
             pType == "DmeVector4Log" ||
             pType == "DmeQAngleLog" ||
             pType == "DmeQuaternionLog" ||
             pType == "DmeVMatrixLog") {
        FixupLog(pElementInternal);
    }


    for (CDmAttribute* pAttribute = pElementInternal->FirstAttribute(); pAttribute;
         pAttribute = pAttribute->NextAttribute()) {
        if (pAttribute->GetType() == AT_ELEMENT) {
            CDmElement* pElement = pAttribute->GetValueElement<CDmElement>();
            DoFixup(pElement);
            continue;
        }

        if (pAttribute->GetType() == AT_ELEMENT_ARRAY) {
            CDmrElementArray<> array(pAttribute);
            int nCount = array.Count();
            for (int i = 0; i < nCount; ++i) {
                CDmElement* pChild = array[i];
                DoFixup(pChild);
            }
            continue;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
// Fixes up a single time attribute - converting from float seconds to int tenths-of-a-millisecond
//-----------------------------------------------------------------------------
void CImportSFMV1::ConvertTimeAttribute(CDmElement* pElementInternal, const char* pOldName, const char* pNewName) {
    float time = 0.0f;
    CDmAttribute* pOldAttr = pElementInternal->GetAttribute(pOldName);
    if (!pOldAttr) {
        Warning("*** Problem in file encountered!\n");
        Warning("*** TimeFrame \"{}\" is missing attribute \"{}\"!\n", pElementInternal->GetName(), pOldName);
        Warning("*** Setting new attribute \"{}\" to 0\n", pNewName);
    }
    else if (pOldAttr->GetType() != AT_FLOAT) {
        Warning("*** Problem in file encountered!\n");
        Warning("*** TimeFrame \"{}\" has attribute \"{}\" with an unexpected type (expected float)!\n",
                pElementInternal->GetName(), pOldName);
    }
    else {
        time = pOldAttr->GetValue<float>();
        pElementInternal->RemoveAttribute(pOldName);
    }

    // Only plain CDmElements are converted here, so it's safe to add attributes
    CDmAttribute* pNewAttr = pElementInternal->AddAttribute(pNewName, AT_INT);
    if (!pNewAttr) {
        Assert(0);
        Warning("*** Converter error - element \"{}\" already has a non-int attribute \"{}\"!\n",
                pElementInternal->GetName(), pNewName);
        return;
    }

    pNewAttr->SetValue<int>(floor(time * 10000 + 0.5f));
}

//-----------------------------------------------------------------------------
// Fixes up a single timeframe
//-----------------------------------------------------------------------------
void CImportSFMV1::FixupTimeframe(CDmElement* pElementInternal) {
    ConvertTimeAttribute(pElementInternal, "start", "startTime");
    ConvertTimeAttribute(pElementInternal, "duration", "durationTime");
    ConvertTimeAttribute(pElementInternal, "offset", "offsetTime");
}

//-----------------------------------------------------------------------------
// Fixes up a single log - converting from int milliseconds to int tenths-of-a-millisecond
//-----------------------------------------------------------------------------
void CImportSFMV1::FixupLog(CDmElement* pElementInternal) {
    CDmAttribute* pAttr = pElementInternal->GetAttribute("times");
    if (!pAttr) {
        Warning("*** Problem in file encountered!\n");
        Warning("*** Log \"{}\" is missing attribute \"{}\"!\n", pElementInternal->GetName(), "times");
        return;
    }

    if (pAttr->GetType() != AT_INT_ARRAY) {
        Warning("*** Problem in file encountered!\n");
        Warning("*** Log \"{}\" has attribute \"{}\" with an unexpected type (expected int array)!\n",
                pElementInternal->GetName(), "times");
        return;
    }

    CDmrArray<int> array(pAttr);
    int c = array.size();
    for (int i = 0; i < c; ++i) {
        // convert all log times from int milliseconds to int tenths-of-a-millisecond
        array.Set(i, 10 * array[i]);
    }
}

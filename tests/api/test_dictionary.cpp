#include "harness.hpp"
#include "../../libs/datamodel/dmelementdictionary.h"
#include "datamodel/dmelement.h"

SMDL_TEST(dictionary_copy_id_remap) {
    const auto first = g_pDataModel->CreateElement("TestDictionary", "first", DMFILEID_INVALID);
    const auto second = g_pDataModel->CreateElement("TestDictionary", "second", DMFILEID_INVALID);
    const auto owner = g_pDataModel->CreateElement("TestDictionary", "owner", DMFILEID_INVALID);
    Require(first != DMELEMENT_HANDLE_INVALID && second != DMELEMENT_HANDLE_INVALID &&
            owner != DMELEMENT_HANDLE_INVALID, "create dictionary elements");

    CDmElementDictionary dictionary;
    DmObjectId_t firstId = g_pDataModel->GetElementId(first);
    DmObjectId_t secondId = firstId;
    secondId.m_Value[15] ^= 1; // Only the last UUID byte differs.

    dictionary.SetElementId(dictionary.InsertElement(g_pDataModel->GetElement(first)), firstId, CR_FORCE_COPY);
    dictionary.SetElementId(dictionary.InsertElement(g_pDataModel->GetElement(second)), secondId, CR_FORCE_COPY);

    auto *parent = g_pDataModel->GetElement(owner);
    dictionary.AddAttribute(parent->AddAttribute("firstRef", AT_ELEMENT), firstId);
    dictionary.AddAttribute(parent->AddAttribute("secondRef", AT_ELEMENT), secondId);
    dictionary.HookUpElementReferences();
    Require(parent->FindAttribute("firstRef")->GetValue<DmElementHandle_t>(DMELEMENT_HANDLE_INVALID) == first &&
            parent->FindAttribute("secondRef")->GetValue<DmElementHandle_t>(DMELEMENT_HANDLE_INVALID) == second,
            "copy ID remaps must compare complete UUIDs");

    dictionary.Clear();
    g_pDataModel->DestroyElement(owner);
    g_pDataModel->DestroyElement(first);
    g_pDataModel->DestroyElement(second);
}

SMDL_TEST(dictionary_serialization_roots) {
    const auto root = g_pDataModel->CreateElement("TestDictionary", "root", DMFILEID_INVALID);
    const auto child = g_pDataModel->CreateElement("TestDictionary", "child", DMFILEID_INVALID);
    Require(root != DMELEMENT_HANDLE_INVALID && child != DMELEMENT_HANDLE_INVALID, "create serialization elements");

    auto *parent = g_pDataModel->GetElement(root);
    auto *descendant = g_pDataModel->GetElement(child);
    parent->AddAttribute("first", AT_ELEMENT)->SetValue(child);
    parent->AddAttribute("again", AT_ELEMENT)->SetValue(child);

    CDmElementSerializationDictionary dictionary;
    dictionary.BuildElementList(parent, false);
    const auto first = dictionary.FirstRootElement();
    const auto second = dictionary.NextRootElement(first);
    Require(dictionary.RootElementCount() == 2 && dictionary.GetRootElement(first) == parent &&
            dictionary.GetRootElement(second) == descendant &&
            dictionary.NextRootElement(second) == ELEMENT_DICT_HANDLE_INVALID,
            "serialize root first, then a multiply referenced child");

    dictionary.Clear();
    g_pDataModel->DestroyElement(root);
    g_pDataModel->DestroyElement(child);
}

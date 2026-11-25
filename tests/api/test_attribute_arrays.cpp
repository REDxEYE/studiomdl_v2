#include "harness.hpp"
#include "datamodel/dmattributevar.h"
#include "tier1/utlbuffer.h"

#include <string_view>
#include <vector>

SMDL_TEST(attribute_arrays_round_trip_dmx) {
    if (!g_pDataModel->FindSerializer("binary"))
        Require(g_pDataModel->Init(), "initialize DMX serializers");

    const auto rootHandle = g_pDataModel->CreateElement("DmElement", "array root", DMFILEID_INVALID);
    const auto childHandle = g_pDataModel->CreateElement("DmElement", "array child", DMFILEID_INVALID);
    auto *root = g_pDataModel->GetElement(rootHandle);
    Require(root && g_pDataModel->GetElement(childHandle), "create array graph");

    CDmrArray<int> numbers(root, "numbers", true);
    std::vector<int> values{2, 4, 8};
    numbers.SwapArray(values);
    numbers.InsertBefore(1, 3);
    numbers.Remove(2);
    CUtlBuffer malformed;
    malformed.PutInt(200);
    Require(!root->FindAttribute("numbers")->Unserialize(malformed) &&
            numbers.Get() == std::vector{2, 3, 8},
            "a truncated array must not replace existing values");

    CDmrStringArray labels(root->AddAttribute("labels", AT_STRING_ARRAY));
    labels.AddToTail("first value");
    labels.AddToTail("");
    const unsigned char bytes[]{0, 42, 255};
    const CUtlBinaryBlock block(bytes, sizeof(bytes));
    CDmrArray<CUtlBinaryBlock>(root, "payloads", true).AddToTail(block);
    CDmrArray<bool> flags(root, "flags", true);
    flags.AddToTail(true);
    flags.AddToTail(false);
    CDmrElementArray<> children(root->AddAttribute("children", AT_ELEMENT_ARRAY));
    children.AddToTail(childHandle);

    for (const bool text : {false, true}) {
        const char *encoding = text ? "keyvalues2" : "binary";
        const char *fileName = text ? "test_arrays_text.dmx" : "test_arrays_binary.dmx";
        CUtlBuffer buffer(0, 0, text ? CUtlBuffer::TEXT_BUFFER : 0);
        Require(g_pDataModel->Serialize(buffer, encoding, "dmx", rootHandle), "encode DMX arrays");
        DmElementHandle_t copyHandle = DMELEMENT_HANDLE_INVALID;
        Require(g_pDataModel->Unserialize(buffer, encoding, "dmx", nullptr, fileName, CR_FORCE_COPY, copyHandle),
                "decode DMX arrays");
        auto *copy = g_pDataModel->GetElement(copyHandle);
        Require(copy && copy != root, "decode a separate DMX graph");
        const CDmrArrayConst<int> copyNumbers(copy, "numbers");
        const CDmrStringArrayConst copyLabels(copy, "labels");
        const CDmrArrayConst<CUtlBinaryBlock> copyPayloads(copy, "payloads");
        const CDmrArrayConst<bool> copyFlags(copy, "flags");
        const CDmrElementArrayConst<> copyChildren(copy, "children");
        Require(copyNumbers.Get() == std::vector{2, 3, 8}, "preserve edited integer array order");
        Require(copyLabels.size() == 2 && copyLabels[0] && copyLabels[1] &&
                std::string_view(copyLabels[0]) == "first value" && std::string_view(copyLabels[1]).empty(),
                "preserve string array values, including empty strings");
        Require(copyPayloads.size() == 1 && copyPayloads[0] == block, "preserve binary block array bytes");
        Require(copyFlags.size() == 2 && copyFlags.data()[0] && !copyFlags.data()[1],
                "preserve contiguous bool array values");
        Require(copyChildren.size() == 1 && copyChildren[0] && copyChildren[0]->GetHandle() != childHandle &&
                copyChildren[0]->GetName() == "array child", "remap array references to decoded elements");
        std::vector<ElementPathItem_t> path;
        Require(copyChildren[0]->FindReferer(copyHandle, path, TD_ALL) && path.size() == 1 && path[0].nIndex == 0,
                "decoded array references must be tracked by the datamodel");
        g_pDataModel->RemoveFileId(copy->GetFileId());
    }

    g_pDataModel->DestroyElement(rootHandle);
    g_pDataModel->DestroyElement(childHandle);
}

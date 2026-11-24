#include "harness.hpp"
#include "datamodel/dmattributevar.h"
#include "datamodel/dmelement.h"

#include <vector>

SMDL_TEST(element_traversal_and_group_copy) {
    const auto rootHandle = g_pDataModel->CreateElement("TestTraversal", "root", DMFILEID_INVALID);
    const auto firstHandle = g_pDataModel->CreateElement("TestTraversal", "first", DMFILEID_INVALID);
    const auto secondHandle = g_pDataModel->CreateElement("TestTraversal", "second", DMFILEID_INVALID);
    auto *root = g_pDataModel->GetElement(rootHandle);
    auto *first = g_pDataModel->GetElement(firstHandle);
    auto *second = g_pDataModel->GetElement(secondHandle);
    Require(root && first && second, "create traversal graph");

    auto *children = root->AddAttribute("children", AT_ELEMENT_ARRAY);
    CDmrElementArray<> rootChildren(children);
    rootChildren.AddToTail(firstHandle);
    rootChildren.AddToTail(secondHandle);
    CDmrElementArray<>(first->AddAttribute("children", AT_ELEMENT_ARRAY)).AddToTail(secondHandle);

    CElementTreeTraversal traversal(root, "children");
    Require(traversal.Next() == root && traversal.CurrentDepth() == 0 &&
            traversal.Next() == first && traversal.CurrentDepth() == 1 &&
            traversal.Next(true) == second && traversal.CurrentDepth() == 1 &&
            traversal.Next() == nullptr && !traversal.IsValid(),
            "skipping a subtree must resume at the next sibling");

    std::vector<ElementPathItem_t> path;
    Require(first->FindReferer(rootHandle, path, TD_ALL) && path.size() == 1 &&
            path.front().hElement == rootHandle && path.front().hAttribute == children->GetHandle() &&
            path.front().nIndex == 0,
            "reference paths must record owner, attribute, and array index");
    std::vector<CDmElement *> ancestors;
    FindAncestorsOfElement(second, ancestors, true);
    Require(ancestors.size() == 2 && ancestors[0] != ancestors[1],
            "a shared ancestor must only be added once");

    const std::vector source{root, first};
    std::vector copies{second};
    CopyElements(source, copies, TD_NONE);
    Require(copies.size() == 3 && copies[0] == second && copies[1] && copies[2] &&
            copies[1] != root && copies[2] != first &&
            CDmrElementArrayConst<>(copies[1], "children")[0] == copies[2] && rootChildren[0] == first,
            "group copy must remap references in appended copies, not the originals");

    g_pDataModel->DestroyElement(copies[1]->GetHandle());
    g_pDataModel->DestroyElement(copies[2]->GetHandle());
    g_pDataModel->DestroyElement(rootHandle);
    g_pDataModel->DestroyElement(firstHandle);
    g_pDataModel->DestroyElement(secondHandle);
}

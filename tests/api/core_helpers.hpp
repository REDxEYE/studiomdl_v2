// Helpers shared by the Core part tests (test_core.cpp, test_core_bones.cpp): building a model from the SMD fixtures
// and comparing it with the QC compile of the equivalent fixture.
#pragma once

#include "harness.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace coretest {

inline constexpr const char *kCrate = "src/crate.smd";

// Adds a mesh of one SMD to a group (and the SMD's skeleton to the model unless `addSkeleton` is false)
inline smdl::Mesh AddMesh(smdl::Model &model, smdl::BodyGroup &group, const std::string &meshName, const char *smdPath,
                          VertexMode mode, bool addSkeleton = true) {
    const SmdFile smd = ReadSmd(FixturePath(smdPath));
    if (addSkeleton)
        AddSkeleton(model, smd);
    smdl::Mesh mesh = group.createMesh(meshName);
    FillGeometry(model, mesh.geometry(), smd, mode);
    return mesh;
}

// $body: a group with one choice
inline smdl::Mesh AddBody(smdl::Model &model, const std::string &groupName, const std::string &meshName,
                          const char *smdPath, VertexMode mode) {
    smdl::BodyGroup group = model.addBodyGroup(groupName);
    return AddMesh(model, group, meshName, smdPath, mode);
}

// A QC compile records the path of its QC in the .mdl (qc_path); a model built in code has none unless told
inline void RequireSameAsQc(smdl::Model &model, const char *qc, std::vector<std::string> arguments = {}) {
    model.setQcPath(QcPathEntry(FixturePath(qc)));
    RequireSameOutputs(CompileQcReference(FixturePath(qc), std::move(arguments)), ResultFiles(model.compile()));
}

// The globals every fixture QC has
inline void BasicGlobals(smdl::Model &model, const char *surfaceProp) {
    model.addMaterialPath("models/api");
    model.setSurfaceProp(surfaceProp);
}

// A model without sequences passes the compile when it includes another model
inline void NoSequences(smdl::Model &model) { model.addIncludeModel("api/none.mdl"); }

inline smdl::Error CaptureError(const std::function<void()> &call) {
    try {
        call();
    } catch (const smdl::Error &error) {
        return error;
    }
    Fail("expected an smdl::Error");
}

inline bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

} // namespace coretest

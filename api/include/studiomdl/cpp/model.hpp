// studiomdl C++ API: the model. Every part adds its methods through a base class defined in its own header; the
// methods are implemented in the library (api/src). The C++ API is used through the static library (studiomdl::static);
// the shared library exports only the C API, which wraps this one.
#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "common.hpp"
#include "result.hpp"
#include "globals.hpp"
#include "skeleton.hpp"
#include "geometry.hpp"
#include "body.hpp"
#include "flex.hpp"
#include "rig.hpp"
#include "animation.hpp"
#include "sequence.hpp"
#include "lod.hpp"
#include "collision.hpp"
#include "bone_dynamics.hpp"

namespace smdl {

class Model;

using LogFunction = std::function<void(Severity severity, std::string_view text)>;

namespace detail {
    // A non-owning Model over a handle, and the handle of an owning Model taken out of it (the C API wraps the C++ API
    // with them). Not for applications.
    Model BorrowModel(smdl_model *model);
    smdl_model *ReleaseModel(Model &&model);
}

class Model : public detail::ModelGlobalsApi,
              public detail::ModelSkeletonApi,
              public detail::ModelBodyApi,
              public detail::ModelFlexApi,
              public detail::ModelRigApi,
              public detail::ModelAnimationApi,
              public detail::ModelSequenceApi,
              public detail::ModelLodApi,
              public detail::ModelCollisionApi,
              public detail::ModelBoneDynamicsApi {
public:
    // name: path under models/ without extension ("props/crate")
    explicit Model(std::string_view name);

    Model(Model &&) noexcept = default;
    Model &operator=(Model &&) noexcept = default;

    smdl_model *handle() const { return m_Model.get(); }

    std::string_view name() const;
    void setName(std::string_view name);

    // Throws smdl::Error (status SMDL_ERROR_COMPILE, message and object of the failure) when the compile fails.
    // gameDir empty: in-memory compile without a game directory.
    Result compile(std::string_view gameDir = {}, const LogFunction &log = {}) const;

private:
    friend Model detail::BorrowModel(smdl_model *model);
    friend smdl_model *detail::ReleaseModel(Model &&model);
    struct Borrowed {};

    static void forwardLog(void *user, smdl_severity severity, const char *text) {
        (*static_cast<const LogFunction *>(user))(static_cast<Severity>(severity), text);
    }

    struct Deleter {
        bool owned = true;
        void operator()(smdl_model *model) const; // api/src/cpp_api.cpp
    };
    Model(smdl_model *model, Borrowed) : m_Model(model, Deleter{false}) {}

    std::unique_ptr<smdl_model, Deleter> m_Model;
};

} // namespace smdl

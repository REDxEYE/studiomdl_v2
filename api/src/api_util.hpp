// Helpers for implementing the API: error reporting, exception guard, array reading, object creation.
#pragma once

#include <cmath>
#include <cstring>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "model.hpp"
#include "studiomdl/cpp/common.hpp"

namespace smdl_api {

// Reports a failed call in the status style: records the message as the model's last error and returns status. Code
// written this way propagates statuses (`return Fail(...)`, `if (auto s = Check(...)) return s;`); a C++ API method runs
// such a body through RunBody, which turns a non-OK status into smdl::Error(status, last error).
//     return Fail(model, SMDL_ERROR_OUT_OF_RANGE, "bone {} out of range ({} bones)", bone, n);
template<class... Args>
smdl_status Fail(const smdl_model *model, smdl_status status, std::format_string<Args...> fmt, Args &&... args) {
    if (model)
        model->lastError = std::format(fmt, std::forward<Args>(args)...);
    return status;
}

// Records the message as the model's last error without throwing (for a C entry point that returns a result object
// together with a failure status).
template<class... Args>
void RecordError(const smdl_model *model, std::format_string<Args...> fmt, Args &&... args) {
    if (model)
        model->lastError = std::format(fmt, std::forward<Args>(args)...);
}

inline smdl_status Ok(const smdl_model *model) {
    if (model)
        model->lastError.clear();
    return SMDL_OK;
}

// The C boundary: runs body (returning smdl_status) and turns exceptions into status codes. smdl::Error carries its own
// status (Fail, the C++ API); anything else is SMDL_ERROR_INTERNAL / out of memory. No exception may cross into C.
template<class F>
smdl_status Guard(const smdl_model *model, F &&body) noexcept {
    try {
        return body();
    }
    catch (const smdl::Error &e) {
        if (model)
            model->lastError = e.what();
        return e.status();
    }
    catch (const std::bad_alloc &) {
        if (model)
            model->lastError = "out of memory";
        return SMDL_ERROR_INTERNAL;
    }
    catch (const std::exception &e) {
        if (model)
            model->lastError = std::string("internal error: ") + e.what();
        return SMDL_ERROR_INTERNAL;
    }
    catch (...) {
        if (model)
            model->lastError = "internal error";
        return SMDL_ERROR_INTERNAL;
    }
}

// A C entry point that forwards to the C++ API: Guard around a void call. On success the last error is cleared.
//     return CallCpp(g->model, [&] { CppGeometry(g).setPositions(xyz, count, stride); });
template<class F>
smdl_status CallCpp(const smdl_model *model, F &&call) noexcept {
    return Guard(model, [&] {
        call();
        return Ok(model);
    });
}

// The body of a C++ API method, written in the status style (`return Fail(...)` / `return Ok(...)` / helper statuses):
// runs it and throws smdl::Error for a non-OK status, with the message the body recorded.
template<class F>
void RunBody(const smdl_model *model, F &&body) {
    if (model)
        model->lastError.clear();
    const smdl_status status = body();
    if (status != SMDL_OK) {
        const std::string message = model && !model->lastError.empty() ? model->lastError : smdl_status_string(status);
        throw smdl::Error(status, message);
    }
}

// Reads `count` elements of `components` floats from a strided array (stride 0 = packed). Rejects null data with a
// non-zero count and non-finite values. Appends to out.
inline bool ReadFloats(const float *data, size_t count, size_t strideBytes, size_t components, std::vector<float> &out,
                       const smdl_model *model, std::string_view what, smdl_status &status) {
    if (count && !data) {
        status = Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: data is null", what);
        return false;
    }
    const size_t stride = strideBytes ? strideBytes : components * sizeof(float);
    if (stride < components * sizeof(float)) {
        status = Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: stride {} is smaller than an element ({} bytes)", what,
                      strideBytes, components * sizeof(float));
        return false;
    }
    out.reserve(out.size() + count * components);
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    for (size_t i = 0; i < count; ++i) {
        for (size_t c = 0; c < components; ++c) {
            float value;
            std::memcpy(&value, bytes + i * stride + c * sizeof(float), sizeof(float));
            if (!std::isfinite(value)) {
                status = Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: element {} is not finite", what, i);
                return false;
            }
            out.push_back(value);
        }
    }
    return true;
}

// Same for uint32_t elements (ids, indices)
inline bool ReadUints(const uint32_t *data, size_t count, size_t strideBytes, size_t components,
                      std::vector<uint32_t> &out, const smdl_model *model, std::string_view what, smdl_status &status) {
    if (count && !data) {
        status = Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: data is null", what);
        return false;
    }
    const size_t stride = strideBytes ? strideBytes : components * sizeof(uint32_t);
    if (stride < components * sizeof(uint32_t)) {
        status = Fail(model, SMDL_ERROR_INVALID_ARGUMENT, "{}: stride {} is smaller than an element ({} bytes)", what,
                      strideBytes, components * sizeof(uint32_t));
        return false;
    }
    out.reserve(out.size() + count * components);
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    for (size_t i = 0; i < count; ++i) {
        for (size_t c = 0; c < components; ++c) {
            uint32_t value;
            std::memcpy(&value, bytes + i * stride + c * sizeof(uint32_t), sizeof(uint32_t));
            out.push_back(value);
        }
    }
    return true;
}

// Null or empty C string check for names
inline bool ValidName(const char *name) { return name && *name; }

smdl_geometry *NewGeometry(smdl_model &model, std::string owner);
smdl_mesh *NewMesh(smdl_model &model, std::string name, smdl_bodygroup *group);
smdl_bodygroup *NewBodyGroup(smdl_model &model, std::string name);

} // namespace smdl_api

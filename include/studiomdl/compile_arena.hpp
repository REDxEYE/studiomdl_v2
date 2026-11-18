//
// Storage that belongs to one compile but is handed around by raw pointer (sources, models, animations, the per-frame
// and per-vertex arrays their structs point to, strings the tables keep): the legacy data structures of studiomdl
// store pointers and never say who frees them. StudioMdlState owns a CompileArena; everything allocated from it is
// destroyed (objects in reverse order of creation) and freed with the state, whether the compile finished or was
// abandoned by an exception. Nothing allocated here is ever freed on its own: pointers into the arena stay valid
// until the state is gone, and replacing a pointer in a struct (a rebuilt vertex array) just leaves the old block in
// the arena until then.
//
// Use it for objects that outlive the function that made them. Scope-local allocations are std::vector /
// std::string / std::unique_ptr, not the arena.
//

#pragma once

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>

class CompileArena {
public:
    CompileArena() = default;
    CompileArena(const CompileArena &) = delete;
    CompileArena &operator=(const CompileArena &) = delete;

    ~CompileArena() {
        // reverse order: later objects may refer to earlier ones
        for (auto it = m_Objects.rbegin(); it != m_Objects.rend(); ++it)
            it->destroy(it->address);
    }

    // A zero-filled T that is then value-initialized, like c_allocate<T>(): the legacy structs rely on zeroed members.
    template<typename T>
    T *Create() {
        m_Objects.reserve(m_Objects.size() + 1); // so that registering below can't fail after the allocation
        void *raw = std::calloc(1, sizeof(T));
        if (!raw)
            throw std::bad_alloc();
        T *object;
        try {
            object = ::new(raw) T();
        }
        catch (...) {
            std::free(raw);
            throw;
        }
        m_Objects.push_back({raw, &DestroyObject<T>});
        return object;
    }

    // Takes over an object made with new: destroyed with `delete` (in reverse creation order, with the others) when
    // the arena goes. For objects built outside the arena that may still be dropped on an error path first (a
    // std::unique_ptr until they are known to stay).
    template<typename T>
    T *Adopt(std::unique_ptr<T> object) {
        m_Objects.reserve(m_Objects.size() + 1);
        T *address = object.get();
        m_Objects.push_back({address, &DeleteObject<T>});
        object.release();
        return address;
    }

    // An array of count zero-filled T, like calloc(count, sizeof(T)). The block is only freed: no constructor or
    // destructor runs (the legacy structs these arrays hold, s_vertexinfo_t, s_face_t, s_bone_t..., are only ever
    // used as zero-filled memory, as they were with calloc).
    template<typename T>
    T *CreateArray(const size_t count) {
        m_Blocks.reserve(m_Blocks.size() + 1);
        void *raw = std::calloc(count ? count : 1, sizeof(T));
        if (!raw)
            throw std::bad_alloc();
        m_Blocks.emplace_back(raw);
        return static_cast<T *>(raw);
    }

    // A copy of the text with a terminating NUL, like strdup(); lives as long as the arena.
    char *CopyString(const std::string_view text) {
        char *copy = CreateArray<char>(text.size() + 1);
        std::memcpy(copy, text.data(), text.size());
        return copy;
    }

private:
    struct FreeDeleter {
        void operator()(void *address) const { std::free(address); }
    };
    struct Object {
        void *address;
        void (*destroy)(void *);
    };

    template<typename T>
    static void DestroyObject(void *address) {
        static_cast<T *>(address)->~T();
        std::free(address);
    }

    template<typename T>
    static void DeleteObject(void *address) {
        delete static_cast<T *>(address);
    }

    std::vector<Object> m_Objects;
    std::vector<std::unique_ptr<void, FreeDeleter>> m_Blocks;
};

//
// Drop-in replacement for "char name[N]" buffers.
//

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <format>
#include <string_view>

namespace vstd {
    // A NUL-terminated string in a char[N] buffer (N includes the terminator).
    // sizeof(FixedString<N>) == N and the length is not cached, so it stays valid when old C APIs
    // write through data() / the char* conversion; code using sizeof(name) keeps working unchanged.
    // Assignment and append truncate to capacity() and always keep the buffer terminated.
    // Deliberately NOT trivially copyable: passing it through "..." (printf without .c_str()) is a compile error,
    // and memset/memcpy/qsort on structs containing it are flagged (-Wclass-memaccess / -Wnontrivial-memaccess).
    template<size_t N>
    class FixedString {
        static_assert(N > 0, "FixedString needs room for the terminator");

    public:
        FixedString() noexcept = default;
        // Copies the whole buffer, so a copy is byte-identical to the source (same as copying a char[N])
        FixedString(const FixedString &other) noexcept { std::memcpy(m_data, other.m_data, N); }
        FixedString &operator=(const FixedString &other) noexcept {
            if (this != &other) std::memcpy(m_data, other.m_data, N);
            return *this;
        }

        FixedString(const char *text) noexcept { assign(text); }
        FixedString(std::string_view text) noexcept { assign(text); }

        template<size_t M>
        FixedString(const FixedString<M> &other) noexcept { assign(other.view()); }

        // printf-style constructor ("%s", not "{}"); truncates to capacity()
        [[gnu::format(printf, 1, 2)]] static FixedString from_format(const char *fmt, ...) noexcept {
            va_list args;
            va_start(args, fmt);
            FixedString tmp;
            tmp.vformat(fmt, args);
            va_end(args);
            return tmp;
        }

        FixedString &operator=(const char *text) noexcept {
            assign(text);
            return *this;
        }

        FixedString &operator=(std::string_view text) noexcept {
            assign(text);
            return *this;
        }

        template<size_t M>
        FixedString &operator=(const FixedString<M> &other) noexcept {
            assign(other.view());
            return *this;
        }

        FixedString &operator+=(std::string_view text) noexcept {
            append(text);
            return *this;
        }

        FixedString &operator+=(char c) noexcept {
            append(std::string_view(&c, 1));
            return *this;
        }

        // Returns false if text was truncated
        bool assign(const char *text) noexcept { return assign(text ? std::string_view(text) : std::string_view()); }

        bool assign(std::string_view text) noexcept {
            const size_t count = text.size() < capacity() ? text.size() : capacity();
            std::memmove(m_data, text.data(), count);
            m_data[count] = '\0';
            return count == text.size();
        }

        // Returns false if text was truncated
        bool append(std::string_view text) noexcept {
            const size_t length = size();
            const size_t room = capacity() - length;
            const size_t count = text.size() < room ? text.size() : room;
            std::memmove(m_data + length, text.data(), count);
            m_data[length + count] = '\0';
            return count == text.size();
        }

        // printf into the buffer; returns false if the result was truncated
        [[gnu::format(printf, 2, 3)]] bool format(const char *fmt, ...) noexcept {
            va_list args;
            va_start(args, fmt);
            const bool fits = vformat(fmt, args);
            va_end(args);
            return fits;
        }

        [[gnu::format(printf, 2, 0)]] bool vformat(const char *fmt, va_list args) noexcept {
            const int written = std::vsnprintf(m_data, N, fmt, args);
            return written >= 0 && static_cast<size_t>(written) < N;
        }

        [[gnu::format(printf, 2, 3)]] bool append_format(const char *fmt, ...) noexcept {
            const size_t length = size();
            va_list args;
            va_start(args, fmt);
            const int written = std::vsnprintf(m_data + length, N - length, fmt, args);
            va_end(args);
            return written >= 0 && static_cast<size_t>(written) < N - length;
        }

        void clear() noexcept { m_data[0] = '\0'; }

        size_t size() const noexcept { return std::strlen(m_data); }
        size_t length() const noexcept { return size(); }
        bool empty() const noexcept { return m_data[0] == '\0'; }
        static constexpr size_t capacity() noexcept { return N - 1; } // characters, without terminator
        static constexpr size_t buffer_size() noexcept { return N; } // bytes, for (char *buf, int size) APIs

        char *data() noexcept { return m_data; }
        const char *data() const noexcept { return m_data; }
        const char *c_str() const noexcept { return m_data; }
        std::string_view view() const noexcept { return std::string_view(m_data); }

        // Implicit so old "char name[N]" call sites (Q_strncpy(name, ...), Q_stricmp(name, ...), name[i]) compile unchanged
        operator char *() noexcept { return m_data; }
        operator const char *() const noexcept { return m_data; }
        operator std::string_view() const noexcept { return view(); }

        bool operator==(const std::string_view other) const noexcept { return view() == other; }
        bool operator==(const char *other) const noexcept { return view() == std::string_view(other ? other : ""); }

        template<size_t M>
        bool operator==(const FixedString<M> &other) const noexcept { return view() == other.view(); }

    private:
        char m_data[N] = {};
    };

    static_assert(sizeof(FixedString<1>) == 1, "sizeof(FixedString<N>!=N)");
    static_assert(sizeof(FixedString<64>) == 64, "sizeof(FixedString<N>!=N)");
}

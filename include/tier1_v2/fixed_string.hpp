//
// Drop-in replacement for "char name[N]" buffers.
//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace vstd {
    constexpr char to_lower_ascii(char c) noexcept;

    constexpr bool iequals(std::string_view a, std::string_view b) noexcept;

    // A NUL-terminated string in a char[N] buffer (N includes the terminator).
    // sizeof(FixedString<N>) == N and the length is not cached, so it stays valid when old C APIs
    // write through data() / the char* conversion; code using sizeof(name) keeps working unchanged.
    // Assignment and append truncate to capacity() and always keep the buffer terminated.
    // Deliberately NOT trivially copyable: passing it through "..." (printf without .c_str()) is a compile error,
    // and memset/memcpy/qsort on structs containing it are flagged (-Wclass-memaccess / -Wnontrivial-memaccess).
    // It formats with std::format ("{}", name) through the std::formatter specialization below.
    template<size_t N>
    class FixedString {
        static_assert(N > 0, "FixedString needs room for the terminator");

    public:
        FixedString() noexcept = default;

        // Copies the whole buffer, so a copy is byte-identical to the source (same as copying a char[N])
        FixedString(const FixedString &other) noexcept { std::memcpy(m_data, other.m_data, N); }

        FixedString& operator=(const FixedString &other) noexcept {
            if (this != &other) std::memcpy(m_data, other.m_data, N);
            return *this;
        }

        FixedString(const char* text) noexcept { assign(text); }
        FixedString(const std::string_view text) noexcept { assign(text); }

        template<size_t M>
        FixedString(const FixedString<M> &other) noexcept { assign(other.view()); }

        // std::format-style constructor ("{}", not "%s"); truncates to capacity()
        template<typename... Args>
        static FixedString from_format(std::format_string<Args...> fmt, Args &&... args) {
            FixedString tmp;
            tmp.write_format(0, fmt, std::forward<Args>(args)...);
            return tmp;
        }

        FixedString& operator=(const char* text) noexcept {
            assign(text);
            return *this;
        }

        FixedString& operator=(std::string_view text) noexcept {
            assign(text);
            return *this;
        }

        template<size_t M>
        FixedString& operator=(const FixedString<M> &other) noexcept {
            assign(other.view());
            return *this;
        }

        FixedString& operator+=(std::string_view text) noexcept {
            append(text);
            return *this;
        }

        FixedString& operator+=(char c) noexcept {
            append(std::string_view(&c, 1));
            return *this;
        }

        // Returns false if text was truncated
        bool assign(const char* text) noexcept { return assign(text ? std::string_view(text) : std::string_view()); }

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

        // std::format into the buffer; returns false if the result was truncated
        template<typename... Args>
        bool format(std::format_string<Args...> fmt, Args &&... args) {
            return write_format(0, fmt, std::forward<Args>(args)...);
        }

        template<typename... Args>
        bool append_format(std::format_string<Args...> fmt, Args &&... args) {
            return write_format(size(), fmt, std::forward<Args>(args)...);
        }

        void clear() noexcept { m_data[0] = '\0'; }

        size_t size() const noexcept { return std::strlen(m_data); }
        size_t length() const noexcept { return size(); }
        bool empty() const noexcept { return m_data[0] == '\0'; }
        static constexpr size_t capacity() noexcept { return N - 1; } // characters, without terminator
        static constexpr size_t buffer_size() noexcept { return N; } // bytes, for (char *buf, int size) APIs

        char* data() noexcept { return m_data; }
        const char* data() const noexcept { return m_data; }
        const char* c_str() const noexcept { return m_data; }
        std::string_view view() const noexcept { return std::string_view(m_data); }
        std::string str() const noexcept { return std::string(m_data); }

        // Implicit so old "char name[N]" call sites (Q_strncpy(name, ...), Q_stricmp(name, ...), name[i]) compile unchanged
        operator char*() noexcept { return m_data; }
        operator const char*() const noexcept { return m_data; }
        explicit operator std::string_view() const noexcept { return view(); }
        explicit operator const std::string_view() const noexcept { return view(); }
        // operator std::string() const noexcept { return std::string(m_data); }

        bool operator==(const std::string_view other) const noexcept { return view() == other; }
        bool operator==(const char* other) const noexcept { return view() == std::string_view(other ? other : ""); }

        template<size_t M>
        bool operator==(const FixedString<M> &other) const noexcept { return view() == other.view(); }

        bool iequals(const char* other) const noexcept { return vstd::iequals(view(), std::string_view(other)); }

        bool iequals(const std::string_view other) const noexcept { return vstd::iequals(view(), other); }

        template<size_t M>
        bool iequals(const FixedString<M> &other) const noexcept { return vstd::iequals(view(), other.view()); }

    private:
        // Formats at offset (<= size()), truncating to capacity(); returns false if truncated
        template<typename... Args>
        bool write_format(size_t offset, std::format_string<Args...> fmt, Args &&... args) {
            const size_t room = capacity() - offset;
            const auto result = std::format_to_n(m_data + offset, static_cast<std::ptrdiff_t>(room), fmt,
                                                 std::forward<Args>(args)...);
            const size_t needed = static_cast<size_t>(result.size);
            m_data[offset + std::min(needed, room)] = '\0';
            return needed <= room;
        }

        char m_data[N] = {}; // NOLINT(*-avoid-c-arrays)
    };

    static_assert(sizeof(FixedString<1>) == 1, "sizeof(FixedString<N>!=N)");
    static_assert(sizeof(FixedString<64>) == 64, "sizeof(FixedString<N>!=N)");
}

template<size_t N>
struct std::formatter<vstd::FixedString<N>, char> : std::formatter<std::string_view> {
    template<typename FormatContext>
    auto format(const vstd::FixedString<N> &text, FormatContext &ctx) const {
        return std::formatter<std::string_view>::format(text.view(), ctx);
    }
};

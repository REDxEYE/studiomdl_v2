//
// Case-insensitive helpers (Q_stricmp, Q_strncasecmp, Q_strlower) for std::string_view and anything convertible to it
// (std::string, vstd::FixedString).
//
// ASCII only: bytes >= 0x80 compare and stay as they are, so UTF-8 text (paths) is never altered or mismatched.
//

#pragma once

#include <cstddef>
#include <string_view>

namespace vstd {
    constexpr char to_lower_ascii(const char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

    // Q_stricmp(a, b) == 0
    constexpr bool iequals(const std::string_view a, const std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (to_lower_ascii(a[i]) != to_lower_ascii(b[i])) {
                return false;
            }
        }
        return true;
    }

    // Q_strncasecmp(prefix, text, strlen(prefix)) == 0
    constexpr bool istarts_with(const std::string_view text, const std::string_view prefix) noexcept {
        return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
    }

    // Position of the first case-insensitive match (Q_stristr), npos if there is none
    constexpr size_t ifind(const std::string_view text, const std::string_view pattern) noexcept {
        if (pattern.size() > text.size()) {
            return std::string_view::npos;
        }
        for (size_t i = 0; i + pattern.size() <= text.size(); ++i) {
            if (iequals(text.substr(i, pattern.size()), pattern)) {
                return i;
            }
        }
        return std::string_view::npos;
    }

    // Q_strlower for std::string and vstd::FixedString
    template<typename S>
    void make_lower_ascii(S &text) noexcept {
        char *data = text.data();
        const size_t length = std::string_view(text).size();
        for (size_t i = 0; i < length; ++i) {
            data[i] = to_lower_ascii(data[i]);
        }
    }
}

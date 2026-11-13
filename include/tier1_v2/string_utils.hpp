//
// Case-insensitive helpers (Q_stricmp, Q_strncasecmp, Q_strlower) for std::string_view and anything convertible to it
// (std::string, vstd::FixedString).
//
// ASCII only: bytes >= 0x80 compare and stay as they are, so UTF-8 text (paths) is never altered or mismatched.
//

#pragma once

#include <cstddef>
#include <iostream>
#include <string_view>
#include "studiomdl/studiomdl_log.hpp"

namespace vstd {
    constexpr char to_lower_ascii(const char c) noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

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

    // The text of anything the case-insensitive helpers accept: const char *, std::string, std::string_view, FixedString,
    // and Valve strings that convert to const char * (CUtlString, CDmaString)
    constexpr std::string_view as_view(const std::string_view text) noexcept {
        return text;
    }

    constexpr std::string_view as_view(const char *text) noexcept {
        return text;
    }

    template<size_t N>
    constexpr std::string_view as_view(const FixedString<N> &text) noexcept {
        return text.view();
    }

    // Q_stricmp(a, b) == 0 for any mix of the string types above
    template<typename A, typename B>
    constexpr bool iequals(const A &a, const B &b) noexcept {
        return iequals(as_view(a), as_view(b));
    }


    // Q_strncasecmp(prefix, text, strlen(prefix)) == 0
    constexpr bool istarts_with(const std::string_view text, const std::string_view prefix) noexcept {
        return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
    }

    template<typename A, typename B>
    constexpr bool istarts_with(const A &text, const B &prefix) noexcept {
        return istarts_with(as_view(text), as_view(prefix));
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

    template<typename A, typename B>
    constexpr size_t ifind(const A &text, const B &pattern) noexcept {
        return ifind(as_view(text), as_view(pattern));
    }

    // Q_strlower for std::string and vstd::FixedString
    template<typename S>
    void make_lower_ascii(S &text) noexcept {
        char* data = text.data();
        const size_t length = as_view(text).size();
        for (size_t i = 0; i < length; ++i) {
            data[i] = to_lower_ascii(data[i]);
        }
    }

    template<std::integral T>
    T str_to_int(std::string_view view, uint32_t radix = 10) {
        T val = 0;
        auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), val, radix);

        if (ec == std::errc()) {
            return val;
        }
        if (ec == std::errc::result_out_of_range) {
            MdlError("Number fits outside type bounds.\n");
        }
        MdlError("Failed to parse integer from string");
    }

    template<std::floating_point T>
    T str_to_float(std::string_view view) {
        T val = 0;
        auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), val);

        if (ec == std::errc()) {
            return val;
        }
        if (ec == std::errc::result_out_of_range) {
            MdlError("Number fits outside type bounds.\n");
        }
        MdlError("Failed to parse floating point value from string");
    }

    inline std::string substr_right(const std::string& str, size_t size) {
        return str.substr(str.size() - std::min(size, str.size()));
    }

    inline std::string_view substr_right(const std::string_view str, size_t size) {
        return str.substr(str.size() - std::min(size, str.size()));
    }
}
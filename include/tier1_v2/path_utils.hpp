//
// Path string helpers: modern replacements for Q_StripFilename, Q_StripExtension, Q_FileBase, ... that work on
// std::string, std::string_view and vstd::FixedString.
//
// Every char path in the tools is UTF-8. Both '/' and '\' are separators; bytes >= 0x80 are never touched,
// so UTF-8 names (e.g. Cyrillic folders) pass through unchanged.
// Store paths in std::string / std::filesystem::path. vstd::FixedString is accepted for ASCII names only: it truncates
// at a byte capacity, which can cut a UTF-8 sequence in half.
//
// A path reaches the OS only through to_fs(): constructing std::filesystem::path from a char string directly,
// or calling path::string(), uses the ANSI code page on Windows and turns non-ASCII names into '?'.
//

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#include "tier1_v2/fixed_string.hpp"

namespace vstd::paths {
    constexpr bool is_separator(const char c) noexcept { return c == '/' || c == '\\'; }

#ifdef _WIN32
    constexpr char native_separator = '\\';
#else
    constexpr char native_separator = '/';
#endif

    // "C:...", "/..." or "\..."
    constexpr bool is_absolute(const std::string_view path) noexcept {
        return (!path.empty() && is_separator(path[0])) || (path.size() >= 2 && path[1] == ':');
    }

    // Position of the last separator, npos if there is none
    constexpr size_t last_separator(const std::string_view path) noexcept { return path.find_last_of("/\\"); }

    // "dir/name.ext" -> "name.ext"
    constexpr std::string_view file_name(const std::string_view path) noexcept {
        const size_t slash = last_separator(path);
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    // Position of the extension dot in the file name, npos if the name has no extension
    constexpr size_t extension_dot(const std::string_view path) noexcept {
        const size_t dot = path.find_last_of('.');
        const size_t slash = last_separator(path);
        if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
            return std::string_view::npos;
        }
        return dot;
    }

    // "dir/name.ext" -> "ext" (without the dot); empty if there is none
    constexpr std::string_view extension(const std::string_view path) noexcept {
        const size_t dot = extension_dot(path);
        return dot == std::string_view::npos ? std::string_view() : path.substr(dot + 1);
    }

    // "dir/name.ext" -> "dir/name" (Q_StripExtension)
    constexpr std::string_view without_extension(const std::string_view path) noexcept {
        return path.substr(0, extension_dot(path));
    }

    // "dir/name.ext" -> "name" (Q_FileBase)
    constexpr std::string_view file_base(const std::string_view path) noexcept {
        return without_extension(file_name(path));
    }

    // "dir/sub/name.ext" -> "dir/sub/" with the separator; empty if there is none (Q_ExtractFilePath)
    constexpr std::string_view directory(const std::string_view path) noexcept {
        const size_t slash = last_separator(path);
        return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash + 1);
    }

    // "dir/sub/name.ext" -> "dir/sub" without the separator; empty if there is none (Q_StripFilename)
    constexpr std::string_view parent(const std::string_view path) noexcept {
        const size_t slash = last_separator(path);
        return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
    }

    // "a" + "b" -> "a/b"; no separator is added when a is empty or already ends in one
    inline std::string join(const std::string_view a, const std::string_view b, const char separator = '/') {
        std::string result;
        result.reserve(a.size() + b.size() + 1);
        result.append(a);
        if (!result.empty() && !is_separator(result.back())) {
            result.push_back(separator);
        }
        result.append(b);
        return result;
    }

    // ---- In-place edits of std::string and vstd::FixedString ----

    namespace detail {
        inline void truncate(std::string &text, const size_t length) { text.resize(length); }

        template<size_t N>
        void truncate(FixedString<N> &text, const size_t length) noexcept { text.data()[length] = '\0'; }

        // FixedString converts to std::string_view only explicitly
        inline std::string_view as_view(const std::string &text) noexcept { return text; }

        template<size_t N>
        std::string_view as_view(const FixedString<N> &text) noexcept { return text.view(); }
    }

    // Q_StripExtension
    template<typename S>
    void strip_extension(S &path) {
        const size_t dot = extension_dot(detail::as_view(path));
        if (dot != std::string_view::npos) {
            detail::truncate(path, dot);
        }
    }

    // Q_StripFilename: keeps the directory without the trailing separator (empty if there is no directory)
    template<typename S>
    void strip_filename(S &path) {
        const size_t slash = last_separator(detail::as_view(path));
        detail::truncate(path, slash == std::string_view::npos ? 0 : slash);
    }

    // Replaces the extension (ext with or without the dot)
    template<typename S>
    void set_extension(S &path, std::string_view ext) {
        strip_extension(path);
        if (!ext.empty() && ext.front() != '.') {
            path += '.';
        }
        path += ext;
    }

    // Q_DefaultExtension: adds ext only if the file name has none
    template<typename S>
    void default_extension(S &path, const std::string_view ext) {
        if (extension_dot(detail::as_view(path)) == std::string_view::npos) {
            set_extension(path, ext);
        }
    }

    // Q_AppendSlash: adds a separator unless path is empty or already ends in one
    template<typename S>
    void append_slash(S &path, const char separator = native_separator) {
        const std::string_view view = detail::as_view(path);
        if (!view.empty() && !is_separator(view.back())) {
            path += separator;
        }
    }

    // Q_FixSlashes
    template<typename S>
    void fix_slashes(S &path, const char separator = native_separator) {
        char* text = path.data();
        const size_t length = detail::as_view(path).size();
        for (size_t i = 0; i < length; ++i) {
            if (is_separator(text[i])) {
                text[i] = separator;
            }
        }
    }

    template<typename S>
    void strip_trailing_slash(S &path) {
        char* text = path.data();
        const size_t length = detail::as_view(path).size();
        if (length > 0 && is_separator(text[length - 1])) {
            text[length - 1] = '\0';
        }
    }

    std::string to_utf8(const std::filesystem::path &path);

    inline std::string make_absolute_path(const std::string_view path, const std::string_view startingDir="") {
        if (is_absolute(path)) {
            // pPath is not relative.. just copy it.
            return std::string(path);
        }
        std::string out;
        // Make sure the starting directory is absolute..
        if (!startingDir.empty() && is_absolute(startingDir)) {
            out = startingDir;
        }
        else {
            std::error_code ec;
            const std::string dir = to_utf8(std::filesystem::current_path(ec));
            if (ec)
                throw std::runtime_error("make_absolute_path: can't get the current directory.");

            if (!startingDir.empty()) {
                append_slash(out);
                out += startingDir;
            }
        }

        // Concatenate the paths.
        append_slash(out);
        out += path;
        fix_slashes(out);

        std::filesystem::path tmp = out;
        tmp = tmp.lexically_normal();
        out = to_utf8(tmp);
        return out;
    }

    // ---- std::filesystem::path ----

    // UTF-8 -> path; '\' becomes '/', which std::filesystem understands everywhere
    inline std::filesystem::path to_fs(const std::string_view utf8) {
        std::u8string text(utf8.size(), u8'\0');
        for (size_t i = 0; i < utf8.size(); ++i) {
            text[i] = utf8[i] == '\\' ? u8'/' : static_cast<char8_t>(utf8[i]);
        }
        return {text};
    }

    // path -> UTF-8 with native separators (like path::string(), without the code page conversion)
    inline std::string to_utf8(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    }

    // fopen for a UTF-8 path (plain fopen takes the ANSI code page on Windows)
    inline std::FILE* open_file(const std::string_view utf8, const char* mode) {
#ifdef _WIN32
        const std::wstring wideMode(mode, mode + std::strlen(mode));
        return _wfopen(to_fs(utf8).c_str(), wideMode.c_str());
#else
        return std::fopen(to_fs(utf8).c_str(), mode);
#endif
    }
}

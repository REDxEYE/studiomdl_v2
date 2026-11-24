#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <string_view>

#include "tier1_v2/charconv_float.hpp"

// Reads one record without copying its tokens or looking beyond the input view.
class LineParser {
public:
    explicit LineParser(std::string_view line) : m_remaining(line) {}

    std::string_view Word() {
        Trim();
        const auto word = m_remaining.substr(0, m_remaining.find_first_of(whitespace));
        m_remaining.remove_prefix(word.size());
        return word;
    }

    std::string_view Quoted() {
        if (!Consume("\""))
            return {};
        const auto end = m_remaining.find('"');
        if (end == std::string_view::npos)
            return {};
        const auto value = m_remaining.substr(0, end);
        m_remaining.remove_prefix(end + 1);
        return value;
    }

    bool Consume(std::string_view prefix) {
        Trim();
        if (!m_remaining.starts_with(prefix))
            return false;
        m_remaining.remove_prefix(prefix.size());
        return true;
    }

    bool Empty() {
        Trim();
        return m_remaining.empty();
    }

    // Accept a complete finite number, leaving any explicit terminator unread.
    template<class T>
    bool Number(T &value, char terminator = '\0') {
        if (Empty())
            return false;
        const char *first = m_remaining.data();
        const char *last = first + m_remaining.size();
        if (*first == '+') {
            ++first;
            if (first == last || *first == '-')
                return false;
        }

        T parsed;
        const auto result = [&] {
            if constexpr (std::floating_point<T>)
                return vstd::from_chars_float(first, last, parsed);
            else
                return std::from_chars(first, last, parsed);
        }();
        if (result.ec != std::errc{} ||
            (result.ptr != last && (!terminator || *result.ptr != terminator) &&
             whitespace.find(*result.ptr) == std::string_view::npos))
            return false;
        if constexpr (std::floating_point<T>) {
            if (!std::isfinite(parsed))
                return false;
        }

        value = parsed;
        m_remaining.remove_prefix(result.ptr - m_remaining.data());
        return true;
    }

    template<class... T>
    bool Read(T &... values) {
        return (Number(values) && ...);
    }

private:
    static constexpr std::string_view whitespace = " \t\r\n\f\v";

    void Trim() {
        const auto first = m_remaining.find_first_not_of(whitespace);
        m_remaining.remove_prefix(first == std::string_view::npos ? m_remaining.size() : first);
    }

    std::string_view m_remaining;
};

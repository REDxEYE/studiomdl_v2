//
// std::from_chars for floating point types, also where the standard library doesn't provide it: Apple's libc++ marks
// it unavailable before macOS 26, so older deployment targets parse with strtod_l in the "C" locale instead, with
// from_chars' rules (no leading whitespace or '+', no hex prefix in the general format, value unchanged on errors).
//

#pragma once

#include <charconv>
#include <concepts>
#include <system_error>

#if defined(__APPLE__)
#include <Availability.h>
#endif

#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED < 260000
#define VSTD_FROM_CHARS_FLOAT_FALLBACK 1
#include <cerrno>
#include <cstdlib>
#include <string>
#include <xlocale.h>
#endif

namespace vstd {

template<std::floating_point T>
std::from_chars_result from_chars_float(const char *first, const char *last, T &value,
                                        std::chars_format format = std::chars_format::general) {
#ifndef VSTD_FROM_CHARS_FLOAT_FALLBACK
    return std::from_chars(first, last, value, format);
#else
    (void)format; // callers use the general format only
    const char *digits = first != last && *first == '-' ? first + 1 : first;
    if (digits == last || *digits == '+' || *digits == ' ' || (*digits >= '\t' && *digits <= '\r'))
        return {first, std::errc::invalid_argument};
    // strtod reads "0x1p3" as hex; from_chars' general format stops after the "0"
    if (last - digits >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        value = *first == '-' ? T(-0.0) : T(0);
        return {digits + 1, std::errc{}};
    }
    const std::string text(first, last); // strtod needs a terminated string
    char *end = nullptr;
    errno = 0;
    T parsed;
    if constexpr (std::same_as<T, float>)
        parsed = strtof_l(text.c_str(), &end, LC_C_LOCALE);
    else if constexpr (std::same_as<T, double>)
        parsed = strtod_l(text.c_str(), &end, LC_C_LOCALE);
    else
        parsed = strtold_l(text.c_str(), &end, LC_C_LOCALE);
    if (end == text.c_str())
        return {first, std::errc::invalid_argument};
    const char *ptr = first + (end - text.c_str());
    if (errno == ERANGE)
        return {ptr, std::errc::result_out_of_range};
    value = parsed;
    return {ptr, std::errc{}};
#endif
}

} // namespace vstd

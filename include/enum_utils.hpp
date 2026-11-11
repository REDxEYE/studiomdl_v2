//
// Created by red_eye on 11/5/26.
//

#pragma once
#include <type_traits>
#include <utility>

// Bit operators for a flag `enum class`: |, & and ~ give the enum type back, |= and &= modify the left operand.
// A scoped enum doesn't convert to bool, so tests are written as HasFlag(flags, Flag) (any bit of the mask set).
#define ENUM_OPERATORS(enum_name) \
    constexpr enum_name operator|(enum_name lhs, enum_name rhs) { \
        return static_cast<enum_name>(std::to_underlying(lhs) | std::to_underlying(rhs)); } \
    constexpr enum_name operator&(enum_name lhs, enum_name rhs) { \
        return static_cast<enum_name>(std::to_underlying(lhs) & std::to_underlying(rhs)); } \
    constexpr enum_name operator~(enum_name value) { return static_cast<enum_name>(~std::to_underlying(value)); } \
    constexpr enum_name &operator|=(enum_name &lhs, enum_name rhs) { return lhs = lhs | rhs; } \
    constexpr enum_name &operator&=(enum_name &lhs, enum_name rhs) { return lhs = lhs & rhs; }

template<typename E> requires std::is_enum_v<E>
constexpr bool HasFlag(E value, E mask) { return std::to_underlying(value & mask) != 0; }

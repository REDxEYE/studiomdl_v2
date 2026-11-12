//
// Created by red_eye on 11/10/26.
//

#pragma once
#include <concepts>
#include <numbers>
#include <utility>


namespace vstd {
    template<std::floating_point T>
    constexpr T deg2rad(T degrees) noexcept {
        constexpr T multiplier = std::numbers::pi_v<T> / T(180);
        return degrees * multiplier;
    }

    template<std::signed_integral I>
    I as_signed_checked(auto value) {
        if (std::in_range<I>(value)) {
            return static_cast<I>(value);
        }
        MdlError("Value is out of range");
    }
}

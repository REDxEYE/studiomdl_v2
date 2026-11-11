//
// Created by red_eye on 11/10/26.
//

#pragma once
#include <concepts>
#include <numbers>


namespace vstd {
    template<std::floating_point T>
    constexpr T deg2rad(T degrees) noexcept {
        constexpr T multiplier = std::numbers::pi_v<T> / T(180);
        return degrees * multiplier;
    }
}

//
// Created by red_eye on 11/10/26.
//

#pragma once
#include <numbers>


namespace vstd {
    template<std::floating_point T>
    constexpr T deg2rad(T degrees) noexcept {
        constexpr T multiplier = 180.0 / std::numbers::pi;
        return degrees * multiplier;
    }
}

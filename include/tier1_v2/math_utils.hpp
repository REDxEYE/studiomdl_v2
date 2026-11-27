//
// Created by red_eye on 11/10/26.
//

#pragma once
#include <random>
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
        throw std::out_of_range("Value is out of range");
    }

    template<std::floating_point F>
    F rand_float(F mn, F mx) {
        std::random_device rd;
        std::uniform_real_distribution<F> dist(mn, mx);
        return dist(rd);
    }
}

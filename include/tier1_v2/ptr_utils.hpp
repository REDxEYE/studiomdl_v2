//
// Created by red_eye on 11/13/26.
//

#pragma once
#include <type_traits>
#include <cstdint>

namespace vstd {
    template<typename T, typename Base, typename Offset>
    requires std::is_integral_v<Offset>
    auto index_array_at(Base* base, Offset offset, const uint32_t index) {
        using ReturnT = std::conditional_t<std::is_const_v<Base>, const T, T>;

        auto offset_ptr = reinterpret_cast<uintptr_t>(base) + offset;
        auto* array = reinterpret_cast<ReturnT*>(offset_ptr);
        return &array[index];
    }

    template<typename T, typename Base, typename Offset>
    requires std::is_integral_v<Offset>
    auto rel_ptr(Base* base, Offset offset) {
        using ReturnT = std::conditional_t<std::is_const_v<Base>, const T, T>;

        auto offset_ptr = reinterpret_cast<uintptr_t>(base) + offset;
        auto* item = reinterpret_cast<ReturnT*>(offset_ptr);
        return item;
    }


}

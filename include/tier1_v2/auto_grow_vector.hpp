//
// Created by red_eye on 11/13/26.
//

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace vstd {
    // std::vector whose non-const operator[] grows the vector (value-initialized elements) to cover the index,
    // for code that fills arrays by index in any order (the behavior of Valve's CUtlVectorAuto).
    // size() is one past the highest index written, or what resize()/push_back() made it.
    template<typename T>
    class AutoGrowVector {
    public:
        using value_type = T;
        using size_type = size_t;
        using reference = T &;
        using const_reference = const T &;
        using iterator = typename std::vector<T>::iterator;
        using const_iterator = typename std::vector<T>::const_iterator;

        AutoGrowVector() = default;

        // Grows to index + 1 elements when needed
        T &operator[](const size_t index) {
            if (index >= m_items.size()) {
                m_items.resize(index + 1);
            }
            return m_items[index];
        }

        // No growing on a const vector: the index must be below size()
        const T &operator[](const size_t index) const { return m_items[index]; }

        T &at(const size_t index) { return m_items.at(index); }
        const T &at(const size_t index) const { return m_items.at(index); }

        T *data() noexcept { return m_items.data(); }
        const T *data() const noexcept { return m_items.data(); }

        iterator begin() noexcept { return m_items.begin(); }
        iterator end() noexcept { return m_items.end(); }
        const_iterator begin() const noexcept { return m_items.begin(); }
        const_iterator end() const noexcept { return m_items.end(); }

        template<typename... Args>
        T &emplace_back(Args &&... args) { return m_items.emplace_back(std::forward<Args>(args)...); }

        void push_back(const T &value) { m_items.push_back(value); }
        void push_back(T &&value) { m_items.push_back(std::move(value)); }

        void resize(const size_t count) { m_items.resize(count); }
        void reserve(const size_t count) { m_items.reserve(count); }
        void clear() noexcept { m_items.clear(); }

        size_t size() const noexcept { return m_items.size(); }
        bool empty() const noexcept { return m_items.empty(); }

    private:
        std::vector<T> m_items;
    };
}

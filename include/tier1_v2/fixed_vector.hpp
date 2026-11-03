//
// Created by red_eye on 11/2/26.
//

#pragma once

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vstd {
    // std::vector-like container with a fixed in-place capacity of N elements.
    // Only the first size() elements are constructed; capacity never changes, and exceeding it throws.
    // Unused slots are always zero bytes (also after an element is removed), so stale data never shows up.
    template<typename T, size_t N>
    class FixedVector {
    public:
        using value_type = T;
        using size_type = size_t;
        using reference = T &;
        using const_reference = const T &;
        using iterator = T *;
        using const_iterator = const T *;

        FixedVector() noexcept { zero_slots(0, N); }

        FixedVector(std::initializer_list<T> init) {
            zero_slots(0, N);
            for (const T &value: init) push_back(value);
        }

        FixedVector(const FixedVector &other) {
            zero_slots(0, N);
            std::uninitialized_copy(other.begin(), other.end(), m_items);
            m_count = other.m_count;
        }

        FixedVector(FixedVector &&other) noexcept(std::is_nothrow_move_constructible_v<T>) {
            zero_slots(0, N);
            std::uninitialized_move(other.begin(), other.end(), m_items);
            m_count = other.m_count;
            other.clear();
        }

        FixedVector &operator=(const FixedVector &other) {
            if (this != &other) {
                clear();
                std::uninitialized_copy(other.begin(), other.end(), m_items);
                m_count = other.m_count;
            }
            return *this;
        }

        FixedVector &operator=(FixedVector &&other) noexcept(std::is_nothrow_move_constructible_v<T>) {
            if (this != &other) {
                clear();
                std::uninitialized_move(other.begin(), other.end(), m_items);
                m_count = other.m_count;
                other.clear();
            }
            return *this;
        }

        ~FixedVector() {
            clear();
        }

        // Bounds-checked element access (operator[] checks too)
        T &operator[](const size_t index) { return at(index); }
        const T &operator[](const size_t index) const { return at(index); }

        T &at(const size_t index) {
            if (index >= m_count) throw std::out_of_range("FixedVector: index out of range");
            return m_items[index];
        }

        const T &at(const size_t index) const {
            if (index >= m_count) throw std::out_of_range("FixedVector: index out of range");
            return m_items[index];
        }

        T &front() { return at(0); }
        const T &front() const { return at(0); }
        T &back() { return at(m_count - 1); }
        const T &back() const { return at(m_count - 1); }

        T *data() noexcept { return m_items; }
        const T *data() const noexcept { return m_items; }

        iterator begin() noexcept { return m_items; }
        iterator end() noexcept { return m_items + m_count; }
        const_iterator begin() const noexcept { return m_items; }
        const_iterator end() const noexcept { return m_items + m_count; }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        // Constructs a new element at the end and returns it
        template<typename... Args>
        T &emplace_back(Args &&... args) {
            if (m_count >= N) throw std::length_error("FixedVector: capacity exceeded");
            T *item = ::new(static_cast<void *>(m_items + m_count)) T(std::forward<Args>(args)...);
            ++m_count;
            return *item;
        }

        template<typename... Args>
        T &emplace(Args &&... args) { return emplace_back(std::forward<Args>(args)...); }

        void push_back(const T &value) { emplace_back(value); }
        void push_back(T &&value) { emplace_back(std::move(value)); }

        void pop_back() {
            if (m_count == 0) throw std::out_of_range("FixedVector: pop_back on empty vector");
            std::destroy_at(m_items + --m_count);
            zero_slots(m_count, m_count + 1);
        }

        // Removes the element at index, keeping the order of the rest
        void remove(const size_t index) {
            if (index >= m_count) throw std::out_of_range("FixedVector: index out of range");
            std::move(m_items + index + 1, m_items + m_count, m_items + index);
            std::destroy_at(m_items + --m_count);
            zero_slots(m_count, m_count + 1);
        }

        // Value-initializes new elements when growing, destroys the tail when shrinking
        void resize(const size_t count) {
            if (count > N) throw std::length_error("FixedVector: capacity exceeded");
            while (m_count > count) pop_back();
            while (m_count < count) emplace_back();
        }

        void clear() noexcept {
            std::destroy(m_items, m_items + m_count);
            zero_slots(0, m_count);
            m_count = 0;
        }

        size_t size() const noexcept { return m_count; }
        static constexpr size_t capacity() noexcept { return N; }
        bool empty() const noexcept { return m_count == 0; }
        bool full() const noexcept { return m_count == N; }

        ssize_t index(T* item) const noexcept {
            return item >= m_items && item < m_items + N ? item - m_items : -1;
        }

    private:
        // Clears the bytes of slots [first, last), which must not hold live elements
        void zero_slots(const size_t first, const size_t last) noexcept {
            std::memset(static_cast<void *>(m_items + first), 0, (last - first) * sizeof(T));
        }

        // Anonymous union: typed T[N] for debuggers and pointer arithmetic, but no element is constructed
        // or destroyed by the union itself, so only the first m_count elements are ever live; the rest are zero bytes.
        union {
            T m_items[N];
        };
        size_t m_count = 0;
    };
}

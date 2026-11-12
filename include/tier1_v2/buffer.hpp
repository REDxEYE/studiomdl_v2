// Created by RED on 11.03.2026.
#pragma once

#include <concepts>
#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace vstd {
    class Buffer;

    template<typename T>
    class BufferPtr;

    template<typename T>
    class BufferView {
    public:
        using value_type = std::remove_cv_t<T>;
        using pointer = T *;
        using const_pointer = const value_type *;
        using reference = T &;
        using const_reference = const value_type &;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using size_type = std::size_t;

        static constexpr size_type npos = std::numeric_limits<size_type>::max();

        constexpr BufferView() noexcept = default;

        constexpr BufferView(const pointer data, const size_type count) noexcept
            : data_(data), size_(count) {
        }

        [[nodiscard]] constexpr pointer data() const noexcept { return data_; }
        [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
        [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
        [[nodiscard]] constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }

        [[nodiscard]] constexpr iterator begin() const noexcept { return data_; }
        [[nodiscard]] constexpr iterator end() const noexcept { return data_ + size_; }
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_; }
        [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_ + size_; }

        [[nodiscard]] constexpr reference operator[](size_type index) const noexcept {
            return data_[index];
        }

        [[nodiscard]] constexpr operator bool() const noexcept {
            return data_ != nullptr && size_ != 0;
        }

        [[nodiscard]] constexpr reference front() const noexcept { return data_[0]; }
        [[nodiscard]] constexpr reference back() const noexcept { return data_[size_ - 1]; }

        [[nodiscard]] reference at(size_type index) const {
            if (index >= size_) {
                throw std::out_of_range("BufferView::at index is out of range");
            }
            return data_[index];
        }

        template<typename Tp>
            requires std::is_same_v<value_type, uint8_t>
        [[nodiscard]] const Tp *reinterpret_at(const size_type offset) const {
            if (offset + sizeof(T) > size()) {
                throw std::out_of_range("Buffer::reinterpret_at index+sizeof is out of range");
            }
            return reinterpret_cast<const Tp *>(this->data() + offset);
        }

        [[nodiscard]] std::span<const value_type> as_span() const {
            return {data_, size_};
        }

        std::span<T> as_span() {
            return {data_, size_};
        }


        [[nodiscard]] constexpr BufferView subview(size_type offset, const size_type count = npos) const {
            if (offset > size_) {
                return {};
            }
            const size_type remaining = size_ - offset;
            const size_type actual_count = (count == npos) ? remaining : std::min(count, remaining);
            return BufferView(data_ + offset, actual_count);
        }

        template<typename U>
        [[nodiscard]] BufferView<U> view_as(const size_type offset = 0,
                                            const size_type element_count = npos,
                                            const bool permit_unaligned = false) {
            static_assert(std::is_trivially_copyable_v<std::remove_cv_t<U> >, "U must be trivially copyable");
            static_assert(!std::is_const_v<T>, "Cannot create writable typed view from const BufferView");
            static_assert(std::is_trivially_copyable_v<std::remove_cv_t<U> >, "U must be trivially copyable");

            const size_type byte_count = validate_typed_view(offset, element_count, sizeof(U), alignof(U),
                                                             permit_unaligned);
            auto *base = reinterpret_cast<uint8_t *>(data_);
            return BufferView<U>(reinterpret_cast<U *>(base + offset), byte_count / sizeof(U));
        }

        template<typename U>
        [[nodiscard]] BufferView<U> writable_view_as(const size_type offset = 0,
                                                     const size_type element_count = npos,
                                                     const bool permit_unaligned = false) {
            static_assert(!std::is_const_v<U>, "writable_view_as<U>() requires non-const U");
            return view_as<U>(offset, element_count, permit_unaligned);
        }

        template<typename U>
        [[nodiscard]] BufferView<const U> view_as(const size_type offset = 0,
                                                  const size_type element_count = npos,
                                                  const bool permit_unaligned = false) const {
            static_assert(std::is_trivially_copyable_v<std::remove_cv_t<U> >, "U must be trivially copyable");

            const size_type byte_count = validate_typed_view(offset, element_count, sizeof(U), alignof(U),
                                                             permit_unaligned);
            auto *base = reinterpret_cast<const uint8_t *>(data_);
            return BufferView<const U>(reinterpret_cast<const U *>(base + offset), byte_count / sizeof(U));
        }

        template<typename U>
        [[nodiscard]] BufferView<const U> readonly_view_as(const size_type offset = 0,
                                                           const size_type element_count = npos,
                                                           const bool permit_unaligned = false) const {
            return view_as<U>(offset, element_count, permit_unaligned);
        }

    private:
        static size_type checked_mul(const size_type lhs, const size_type rhs) {
            if (lhs == 0 || rhs == 0) {
                return 0;
            }

            if (lhs > std::numeric_limits<size_type>::max() / rhs) {
                throw std::overflow_error("BufferView typed view request overflow");
            }
            return lhs * rhs;
        }

        [[nodiscard]] size_type validate_typed_view(const size_type byte_offset, const size_type element_count,
                                                    const size_type element_size, const size_type alignment,
                                                    const bool permit_unaligned) const {
            const size_type total_bytes = checked_mul(size_, sizeof(T));
            if (byte_offset > total_bytes) {
                throw std::out_of_range("BufferView typed view offset is out of range");
            }

            const auto raw_address = reinterpret_cast<std::uintptr_t>(
                reinterpret_cast<const uint8_t *>(data_) + byte_offset);
            if (!permit_unaligned && (raw_address % alignment) != 0) {
                throw std::invalid_argument("BufferView typed view is not properly aligned");
            }

            const size_type available_bytes = total_bytes - byte_offset;
            if (element_count == npos) {
                if ((available_bytes % element_size) != 0) {
                    throw std::invalid_argument("BufferView typed view does not have full elements in tail");
                }
                return available_bytes;
            }

            const size_type requested_bytes = checked_mul(element_count, element_size);
            if (requested_bytes > available_bytes) {
                throw std::out_of_range("BufferView typed view range is out of bounds");
            }
            return requested_bytes;
        }

        pointer data_ = nullptr;
        size_type size_ = 0;
    };

    using ByteBufferView = BufferView<uint8_t>;
    using ConstByteBufferView = BufferView<const uint8_t>;
    using MutableByteBufferView = ByteBufferView;

    class Buffer {
    public:
        using value_type = uint8_t;
        using pointer = value_type *;
        using const_pointer = const value_type *;
        using reference = value_type &;
        using const_reference = const value_type &;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using size_type = std::size_t;

        static constexpr size_type npos = std::numeric_limits<size_type>::max();

        Buffer() : backend_(std::make_unique<VectorBackend>()) {
        }

        explicit Buffer(size_type size)
            : backend_(std::make_unique<VectorBackend>(size)) {
        }

        explicit Buffer(std::vector<uint8_t> data)
            : backend_(std::make_unique<VectorBackend>(std::move(data))) {
        }

        static Buffer of_fixed_size(size_type size) {
            return Buffer(std::make_unique<UniqueFixedArrayBackend>(size));
        }

        static Buffer from_unique(std::unique_ptr<uint8_t[]> data, size_type size, size_type capacity = npos) {
            return Buffer(std::make_unique<UniqueArrayBackend>(std::move(data), size, capacity));
        }

        static Buffer wrap(uint8_t *data, size_type size, size_type capacity = npos) {
            return Buffer(std::make_unique<ExternalMutableBackend>(data, size, capacity));
        }

        static Buffer wrap(const uint8_t *data, size_type size) {
            return Buffer(std::make_unique<ExternalConstBackend>(data, size));
        }

        static Buffer wrap(const std::span<const uint8_t> data) {
            return Buffer(std::make_unique<ExternalConstBackend>(data.data(), data.size()));
        }

        static Buffer wrap_borrowed(std::vector<uint8_t> *data) {
            return Buffer(std::make_unique<BorrowedVectorBackend>(data));
        }

        template<typename T>
        static Buffer copy_of(BufferView<T> view) {
            const auto ro_view = view.template readonly_view_as<uint8_t>();
            std::vector<uint8_t> data(ro_view.begin(), ro_view.end());
            return Buffer(std::make_unique<VectorBackend>(std::move(data)));
        }

        Buffer(const Buffer &other) = delete;

        Buffer(Buffer &&other) noexcept
            : backend_(std::move(other.backend_)) {
        }

        Buffer &operator=(const Buffer &other) = delete;

        Buffer &operator=(Buffer &&other) noexcept {
            if (this == &other)
                return *this;
            backend_ = std::move(other.backend_);
            return *this;
        }

        [[nodiscard]] bool is_owning() const noexcept { return backend_->is_owning(); }
        [[nodiscard]] bool is_mutable() const noexcept { return backend_->is_mutable(); }
        [[nodiscard]] bool is_resizable() const noexcept { return backend_->is_resizable(); }
        [[nodiscard]] bool is_expandable() const noexcept { return backend_->is_expandable(); }

        [[nodiscard]] pointer data() { return backend_->data_mut(); }
        [[nodiscard]] const_pointer data() const noexcept { return backend_->data(); }

        [[nodiscard]] size_type size() const noexcept { return backend_->size(); }
        [[nodiscard]] size_type capacity() const noexcept { return backend_->capacity(); }
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        [[nodiscard]] iterator begin() { return data(); }
        [[nodiscard]] iterator end() { return data() + size(); }
        [[nodiscard]] const_iterator begin() const noexcept { return data(); }
        [[nodiscard]] const_iterator end() const noexcept { return data() + size(); }
        [[nodiscard]] const_iterator cbegin() const noexcept { return data(); }
        [[nodiscard]] const_iterator cend() const noexcept { return data() + size(); }

        [[nodiscard]] reference operator[](const size_type index) { return data()[index]; }
        [[nodiscard]] const_reference operator[](const size_type index) const noexcept { return data()[index]; }

        [[nodiscard]] reference front() { return (*this)[0]; }
        [[nodiscard]] const_reference front() const noexcept { return (*this)[0]; }
        [[nodiscard]] reference back() { return (*this)[size() - 1]; }
        [[nodiscard]] const_reference back() const noexcept { return (*this)[size() - 1]; }

        [[nodiscard]] reference at(const size_type index) {
            if (index >= size()) {
                throw std::out_of_range("Buffer::at index is out of range");
            }
            return (*this)[index];
        }

        [[nodiscard]] const_reference at(const size_type index) const {
            if (index >= size()) {
                throw std::out_of_range("Buffer::at index is out of range");
            }
            return (*this)[index];
        }

        template<typename T>
        [[nodiscard]] const T *reinterpret_at(const size_type offset) const {
            if (offset + sizeof(T) > size()) {
                throw std::out_of_range("Buffer::reinterpret_at index+sizeof is out of range");
            }
            return reinterpret_cast<const T *>(this->data() + offset);
        }


        template<typename T>
        [[nodiscard]] T *reinterpret_at(const size_type offset) {
            if (offset + sizeof(T) > size()) {
                throw std::out_of_range("Buffer::reinterpret_at index+sizeof is out of range");
            }
            return reinterpret_cast<T *>(this->data() + offset);
        }

        // Appends count zero-initialized T and returns a reallocation-safe pointer to the first one.
        // count == 0 returns the current end: a valid position for offsets, not dereferenceable.
        template<typename T>
        [[nodiscard]] BufferPtr<T> allocate(size_type count = 1);

        template<typename T>
        [[nodiscard]] BufferPtr<T> ptr_at(size_type offset);

        // position one past the last byte (where the next allocate starts)
        [[nodiscard]] BufferPtr<uint8_t> end_ptr();

        // pads with zeros to a multiple of alignment; no padding when already aligned (ALIGN4 semantics)
        void align(const uint32_t alignment) {
            if (const auto remainder = size() % alignment) {
                resize(size() + (alignment - remainder));
            }
        }

        [[nodiscard]] std::span<const uint8_t> as_span() const {
            return {data(), size()};
        }

        std::span<uint8_t> as_span() {
            return {data(), size()};
        }

        void clear() { backend_->clear(); }

        void resize(const size_type new_size) { backend_->resize(new_size); }

        void reserve(const size_type new_capacity) { backend_->reserve(new_capacity); }

        void push_back(const uint8_t value) { backend_->push_back(value); }

        void append(const uint8_t *bytes, const size_type count) {
            if (count == 0) {
                return;
            }
            if (bytes == nullptr) {
                throw std::invalid_argument("Buffer::append bytes cannot be null when count is non-zero");
            }
            backend_->append(bytes, count);
        }

        void append(const ConstByteBufferView bytes) { append(bytes.data(), bytes.size()); }

        void append(const std::initializer_list<uint8_t> values) {
            append(values.begin(), values.size());
        }

        [[nodiscard]] std::vector<uint8_t> to_vector() const {
            return {begin(), end()};
        }

        template<typename T>
        [[nodiscard]] BufferView<T> view_as(const size_type offset = 0, const size_type element_count = npos) {
            static_assert(std::is_trivially_copyable_v<std::remove_cv_t<T> >, "T must be trivially copyable");

            const size_type byte_count = validate_typed_view(offset, element_count, sizeof(T));
            return BufferView<T>(reinterpret_cast<T *>(data() + offset), byte_count / sizeof(T));
        }

        template<typename T>
        [[nodiscard]] BufferView<T> writable_view_as(const size_type offset = 0,
                                                     const size_type element_count = npos) {
            static_assert(!std::is_const_v<T>, "writable_view_as<T>() requires non-const T");
            return view_as<T>(offset, element_count);
        }

        template<typename T>
        [[nodiscard]] BufferView<const T> view_as(const size_type offset = 0,
                                                  const size_type element_count = npos) const {
            static_assert(std::is_trivially_copyable_v<std::remove_cv_t<T> >, "T must be trivially copyable");

            const size_type byte_count = validate_typed_view(offset, element_count, sizeof(T));
            return BufferView<const T>(reinterpret_cast<const T *>(data() + offset), byte_count / sizeof(T));
        }

        template<typename T>
        [[nodiscard]] BufferView<const T> readonly_view_as(const size_type offset = 0,
                                                           const size_type element_count = npos) const {
            return view_as<T>(offset, element_count);
        }

        [[nodiscard]] ByteBufferView view(const size_type offset = 0, const size_type count = npos) {
            if (offset > size()) {
                throw std::out_of_range("Buffer::view offset is out of range");
            }

            const size_type remaining = size() - offset;
            const size_type actual_count = (count == npos) ? remaining : std::min(count, remaining);
            return {data() + offset, actual_count};
        }

        [[nodiscard]] MutableByteBufferView writable_view(const size_type offset = 0, const size_type count = npos) {
            return view(offset, count);
        }

        [[nodiscard]] ConstByteBufferView view(const size_type offset = 0, const size_type count = npos) const {
            if (offset > size()) {
                throw std::out_of_range("Buffer::view offset is out of range");
            }

            const size_type remaining = size() - offset;
            const size_type actual_count = (count == npos) ? remaining : std::min(count, remaining);
            return {data() + offset, actual_count};
        }

        [[nodiscard]] ConstByteBufferView
        readonly_view(const size_type offset = 0, const size_type count = npos) const {
            return view(offset, count);
        }

    private:
        class Backend {
        public:
            virtual ~Backend() = default;

            [[nodiscard]] virtual bool is_owning() const noexcept = 0;

            [[nodiscard]] virtual bool is_mutable() const noexcept = 0;

            [[nodiscard]] virtual bool is_resizable() const noexcept = 0;

            [[nodiscard]] virtual bool is_expandable() const noexcept = 0;

            [[nodiscard]] virtual uint8_t *data_mut() = 0;

            [[nodiscard]] virtual const uint8_t *data() const = 0;

            [[nodiscard]] virtual size_type size() const = 0;

            [[nodiscard]] virtual size_type capacity() const = 0;

            virtual void clear() = 0;

            virtual void resize(size_type new_size) = 0;

            virtual void reserve(size_type new_capacity) = 0;

            virtual void push_back(uint8_t value) = 0;

            virtual void append(const uint8_t *bytes, size_type count) = 0;
        };

        class VectorBackend final : public Backend {
        public:
            VectorBackend() = default;

            explicit VectorBackend(const size_type size) : data_(size) {
            }

            explicit VectorBackend(std::vector<uint8_t> data) : data_(std::move(data)) {
            }

            [[nodiscard]] bool is_owning() const noexcept override { return true; }
            [[nodiscard]] bool is_mutable() const noexcept override { return true; }
            [[nodiscard]] bool is_resizable() const noexcept override { return true; }
            [[nodiscard]] bool is_expandable() const noexcept override { return true; }

            [[nodiscard]] uint8_t *data_mut() override { return data_.data(); }
            [[nodiscard]] const uint8_t *data() const noexcept override { return data_.data(); }
            [[nodiscard]] size_type size() const noexcept override { return data_.size(); }
            [[nodiscard]] size_type capacity() const noexcept override { return data_.capacity(); }

            void clear() override { data_.clear(); }
            void resize(const size_type new_size) override { data_.resize(new_size); }
            void reserve(const size_type new_capacity) override { data_.reserve(new_capacity); }
            void push_back(const uint8_t value) override { data_.push_back(value); }

            void append(const uint8_t *bytes, const size_type count) override {
                data_.insert(data_.end(), bytes, bytes + count);
            }

        private:
            std::vector<uint8_t> data_;
        };

        class BorrowedVectorBackend final : public Backend {
        public:
            explicit BorrowedVectorBackend(std::vector<uint8_t> *data) : data_(data) {
            }

            [[nodiscard]] bool is_owning() const noexcept override { return true; }

            [[nodiscard]] bool is_mutable() const noexcept override { return true; }

            [[nodiscard]] bool is_resizable() const noexcept override { return false; };

            [[nodiscard]] bool is_expandable() const noexcept override { return true; };

            [[nodiscard]] uint8_t *data_mut() override { return data_->data(); }

            [[nodiscard]] const uint8_t *data() const override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                return data_->data();
            }

            [[nodiscard]] size_type size() const override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                return data_->size();
            }

            [[nodiscard]] size_type capacity() const override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                return data_->capacity();
            }

            void clear() override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                data_->clear();
            }

            void resize(const size_type new_size) override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                data_->resize(new_size);
            }

            void reserve(const size_type new_capacity) override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                data_->reserve(new_capacity);
            }

            void push_back(const uint8_t value) override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                data_->push_back(value);
            }

            void append(const uint8_t *bytes, const size_type count) override {
                if (data_ == nullptr) {
                    throw std::runtime_error("BorrowedVectorBackend: data_ is null");
                }
                data_->insert(data_->end(), bytes, bytes + count);
            }

        private:
            std::vector<uint8_t> *data_;
        };

        class UniqueArrayBackend final : public Backend {
        public:
            UniqueArrayBackend(std::unique_ptr<uint8_t[]> data, const size_type size,
                               const size_type capacity) : data_(std::move(data)), size_(size) {
                capacity_ = (capacity == npos) ? size : capacity;
                if (capacity_ < size_) {
                    capacity_ = size_;
                }
                if (!data_ && capacity_ > 0) {
                    data_ = std::make_unique<uint8_t[]>(capacity_);
                }
            }

            [[nodiscard]] bool is_owning() const noexcept override { return true; }
            [[nodiscard]] bool is_mutable() const noexcept override { return true; }
            [[nodiscard]] bool is_resizable() const noexcept override { return true; }
            [[nodiscard]] bool is_expandable() const noexcept override { return true; }

            [[nodiscard]] uint8_t *data_mut() override { return data_.get(); }
            [[nodiscard]] const uint8_t *data() const noexcept override { return data_.get(); }
            [[nodiscard]] size_type size() const noexcept override { return size_; }
            [[nodiscard]] size_type capacity() const noexcept override { return capacity_; }

            void clear() override { size_ = 0; }

            void resize(const size_type new_size) override {
                if (new_size > capacity_) {
                    reserve(growth_capacity(new_size));
                }
                size_ = new_size;
            }

            void reserve(const size_type new_capacity) override {
                if (new_capacity <= capacity_) {
                    return;
                }

                auto replacement = std::make_unique<uint8_t[]>(new_capacity);
                if (size_ > 0) {
                    std::copy_n(data_.get(), size_, replacement.get());
                }
                data_ = std::move(replacement);
                capacity_ = new_capacity;
            }

            void push_back(const uint8_t value) override {
                const size_type index = size_;
                resize(size_ + 1);
                data_[index] = value;
            }

            void append(const uint8_t *bytes, const size_type count) override {
                if (count == 0) {
                    return;
                }

                const size_type old_size = size_;
                resize(size_ + count);
                std::copy_n(bytes, count, data_.get() + old_size);
            }

        private:
            static size_type growth_capacity(const size_type requested) {
                size_type grown = requested > 8 ? requested : 8;
                while (grown < requested) {
                    grown = grown + grown / 2 + 1;
                }
                return grown;
            }

            std::unique_ptr<uint8_t[]> data_;
            size_type size_ = 0;
            size_type capacity_ = 0;
        };

        class UniqueFixedArrayBackend final : public Backend {
        public:
            UniqueFixedArrayBackend(const size_type size) : size_(size) {
                data_ = std::make_unique<uint8_t[]>(size);
            }

            [[nodiscard]] bool is_owning() const noexcept override { return true; }
            [[nodiscard]] bool is_mutable() const noexcept override { return true; }
            [[nodiscard]] bool is_resizable() const noexcept override { return false; }
            [[nodiscard]] bool is_expandable() const noexcept override { return false; }

            [[nodiscard]] uint8_t *data_mut() override { return data_.get(); }
            [[nodiscard]] const uint8_t *data() const noexcept override { return data_.get(); }
            [[nodiscard]] size_type size() const noexcept override { return size_; }
            [[nodiscard]] size_type capacity() const noexcept override { return size_; }

            void clear() override { size_ = 0; }

            void resize(const size_type new_size) override {
                throw std::runtime_error("resize not implemented");
            }

            void reserve(const size_type new_capacity) override {
                throw std::runtime_error("reserve not implemented");
            }

            void push_back(const uint8_t value) override {
                const size_type index = size_;
                resize(size_ + 1);
                data_[index] = value;
            }

            void append(const uint8_t *bytes, const size_type count) override {
                if (count == 0) {
                    return;
                }

                const size_type old_size = size_;
                resize(size_ + count);
                std::copy_n(bytes, count, data_.get() + old_size);
            }

        private:
            std::unique_ptr<uint8_t[]> data_;
            size_type size_ = 0;
        };

        class ExternalMutableBackend final : public Backend {
        public:
            ExternalMutableBackend(uint8_t *data, const size_type size, const size_type capacity)
                : data_(data), size_(size), capacity_((capacity == npos) ? size : capacity) {
                if (capacity_ < size_) {
                    throw std::invalid_argument("External mutable buffer capacity is smaller than size");
                }
            }

            [[nodiscard]] bool is_owning() const noexcept override { return false; }
            [[nodiscard]] bool is_mutable() const noexcept override { return true; }
            [[nodiscard]] bool is_resizable() const noexcept override { return true; }
            [[nodiscard]] bool is_expandable() const noexcept override { return false; }

            [[nodiscard]] uint8_t *data_mut() override { return data_; }
            [[nodiscard]] const uint8_t *data() const noexcept override { return data_; }
            [[nodiscard]] size_type size() const noexcept override { return size_; }
            [[nodiscard]] size_type capacity() const noexcept override { return capacity_; }

            void clear() override { size_ = 0; }

            void resize(const size_type new_size) override {
                if (new_size > capacity_) {
                    throw std::length_error("External mutable buffer cannot grow past fixed capacity");
                }
                size_ = new_size;
            }

            void reserve(const size_type new_capacity) override {
                if (new_capacity > capacity_) {
                    throw std::length_error("External mutable buffer cannot reserve past fixed capacity");
                }
            }

            void push_back(const uint8_t value) override {
                resize(size_ + 1);
                data_[size_ - 1] = value;
            }

            void append(const uint8_t *bytes, const size_type count) override {
                const size_type old_size = size_;
                resize(size_ + count);
                std::copy_n(bytes, count, data_ + old_size);
            }

        private:
            uint8_t *data_ = nullptr;
            size_type size_ = 0;
            size_type capacity_ = 0;
        };

        class ExternalConstBackend final : public Backend {
        public:
            ExternalConstBackend(const uint8_t *data, const size_type size) : data_(data), size_(size) {
            }

            [[nodiscard]] bool is_owning() const noexcept override { return false; }
            [[nodiscard]] bool is_mutable() const noexcept override { return false; }
            [[nodiscard]] bool is_resizable() const noexcept override { return false; }
            [[nodiscard]] bool is_expandable() const noexcept override { return false; }

            [[nodiscard]] uint8_t *data_mut() override {
                throw std::runtime_error("Buffer is read-only");
            }

            [[nodiscard]] const uint8_t *data() const noexcept override { return data_; }
            [[nodiscard]] size_type size() const noexcept override { return size_; }
            [[nodiscard]] size_type capacity() const noexcept override { return size_; }

            void clear() override { throw std::runtime_error("Buffer is read-only"); }
            void resize(size_type) override { throw std::runtime_error("Buffer is read-only"); }
            void reserve(size_type) override { throw std::runtime_error("Buffer is read-only"); }
            void push_back(uint8_t) override { throw std::runtime_error("Buffer is read-only"); }
            void append(const uint8_t *, size_type) override { throw std::runtime_error("Buffer is read-only"); }

        private:
            const uint8_t *data_ = nullptr;
            size_type size_ = 0;
        };

        explicit Buffer(std::unique_ptr<Backend> backend) : backend_(std::move(backend)) {
        }

        static size_type checked_mul(const size_type lhs, const size_type rhs) {
            if (lhs == 0 || rhs == 0) {
                return 0;
            }

            if (lhs > std::numeric_limits<size_type>::max() / rhs) {
                throw std::overflow_error("Buffer typed view request overflow");
            }
            return lhs * rhs;
        }

        [[nodiscard]] size_type validate_typed_view(const size_type byte_offset, const size_type element_count,
                                                    const size_type element_size) const {
            if (byte_offset > size()) {
                throw std::out_of_range("Buffer typed view offset is out of range");
            }

            const size_type available_bytes = size() - byte_offset;
            if (element_count == npos) {
                if ((available_bytes % element_size) != 0) {
                    throw std::invalid_argument("Buffer typed view does not have full elements in tail");
                }
                return available_bytes;
            }

            const size_type requested_bytes = checked_mul(element_count, element_size);
            if (requested_bytes > available_bytes) {
                throw std::out_of_range("Buffer typed view range is out of bounds");
            }
            return requested_bytes;
        }

        std::unique_ptr<Backend> backend_;
    };

    //-----------------------------------------------------------------------------
    // Pointer into a Buffer that survives reallocation: it stores the buffer and a byte offset and resolves the
    // address on every access. Raw pointers from get() are valid only until the buffer grows again.
    // A default-constructed BufferPtr is null; one from allocate(0) is a valid position but not dereferenceable.
    //-----------------------------------------------------------------------------
    template<typename T>
    class BufferPtr {
    public:
        using size_type = Buffer::size_type;

        BufferPtr() = default;

        BufferPtr(Buffer &buffer, const size_type offset) noexcept : buffer_(&buffer), offset_(offset) {
        }

        [[nodiscard]] T *get() const {
            return buffer_ ? reinterpret_cast<T *>(buffer_->data() + offset_) : nullptr;
        }

        T *operator->() const { return get(); }
        T &operator*() const { return *get(); }
        T &operator[](const std::ptrdiff_t index) const { return get()[index]; }

        explicit operator bool() const noexcept { return buffer_ != nullptr; }

        [[nodiscard]] Buffer *buffer() const noexcept { return buffer_; }
        // byte offset from the start of the buffer
        [[nodiscard]] size_type offset() const noexcept { return offset_; }

        // byte distance from base to this, both in the same buffer: the relative "xxxindex" fields of the file formats
        template<std::integral I, typename U>
        [[nodiscard]] I offset_from(const BufferPtr<U> &base) const {
            if (buffer_ != base.buffer()) {
                throw std::invalid_argument("BufferPtr::offset_from pointers are in different buffers");
            }
            const auto delta = static_cast<std::ptrdiff_t>(offset_) - static_cast<std::ptrdiff_t>(base.offset());
            if (delta < static_cast<std::ptrdiff_t>(std::numeric_limits<I>::min()) ||
                delta > static_cast<std::ptrdiff_t>(std::numeric_limits<I>::max())) {
                throw std::out_of_range("BufferPtr::offset_from distance does not fit the integer type");
            }
            return static_cast<I>(delta);
        }

        // offset of this from the start of the buffer, range checked for I
        template<std::integral I>
        [[nodiscard]] I offset_as() const {
            if (offset_ > static_cast<size_type>(std::numeric_limits<I>::max())) {
                throw std::out_of_range("BufferPtr::offset_as offset does not fit the integer type");
            }
            return static_cast<I>(offset_);
        }

        // pointer to a field of the pointed-to object: ptr.field(&mstudiobone_t::sznameindex)
        template<typename M, typename C = T>
        [[nodiscard]] BufferPtr<M> field(M C::*member) const {
            const auto *field_address = reinterpret_cast<const uint8_t *>(&(get()->*member));
            return {*buffer_, offset_ + static_cast<size_type>(field_address - reinterpret_cast<const uint8_t *>(get()))};
        }

        template<typename U>
        [[nodiscard]] BufferPtr<U> cast() const { return buffer_ ? BufferPtr<U>(*buffer_, offset_) : BufferPtr<U>(); }

        BufferPtr operator+(const std::ptrdiff_t count) const {
            return {*buffer_, static_cast<size_type>(static_cast<std::ptrdiff_t>(offset_) + count * static_cast<std::ptrdiff_t>(sizeof(T)))};
        }

        BufferPtr operator-(const std::ptrdiff_t count) const { return *this + -count; }
        BufferPtr &operator+=(const std::ptrdiff_t count) { return *this = *this + count; }
        BufferPtr &operator++() { return *this += 1; }

        BufferPtr operator++(int) {
            BufferPtr previous = *this;
            *this += 1;
            return previous;
        }

        bool operator==(const BufferPtr &other) const noexcept = default;

    private:
        Buffer *buffer_ = nullptr;
        size_type offset_ = 0;
    };

    template<typename T>
    BufferPtr<T> Buffer::allocate(const size_type count) {
        // the offset, not the address: resize may move the data
        const size_type offset = size();
        resize(offset + checked_mul(count, sizeof(T)));
        return BufferPtr<T>(*this, offset);
    }

    template<typename T>
    BufferPtr<T> Buffer::ptr_at(const size_type offset) {
        if (offset > size()) {
            throw std::out_of_range("Buffer::ptr_at offset is out of range");
        }
        return BufferPtr<T>(*this, offset);
    }

    inline BufferPtr<uint8_t> Buffer::end_ptr() { return BufferPtr<uint8_t>(*this, size()); }
}

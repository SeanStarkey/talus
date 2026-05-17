#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace talus::detail {

template<typename T, std::size_t BlockSize = 256>
class PoolAllocator {
    static_assert(BlockSize > 0);

public:
    using value_type = T;

    PoolAllocator() = default;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    PoolAllocator(PoolAllocator&&) noexcept = default;

    PoolAllocator& operator=(PoolAllocator&& other) noexcept(std::is_nothrow_destructible_v<T>) {
        if (this == &other) {
            return *this;
        }

        clear();
        blocks_ = std::move(other.blocks_);
        free_list_ = std::move(other.free_list_);
        current_block_ = other.current_block_;
        next_slot_ = other.next_slot_;
        size_ = other.size_;

        other.free_list_.clear();
        other.current_block_ = 0;
        other.next_slot_ = 0;
        other.size_ = 0;
        return *this;
    }

    ~PoolAllocator() {
        clear();
    }

    template<typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        Slot slot = acquire_slot();
        T* object = slot.object;

        try {
            std::construct_at(object, std::forward<Args>(args)...);
        } catch (...) {
            release_unconstructed(slot);
            throw;
        }

        set_live(object, true);
        ++size_;
        return object;
    }

    void destroy(T* object) {
        if (object == nullptr) {
            return;
        }

        Block& block = block_for(object);
        const std::size_t index = block.index_of(object);
        assert(block.live[index]);

        std::destroy_at(object);
        block.live[index] = false;
        free_list_.push_back(object);
        --size_;
    }

    void reset() noexcept(std::is_nothrow_destructible_v<T>) {
        destroy_live_objects();
        current_block_ = 0;
        next_slot_ = 0;
        free_list_.clear();
        size_ = 0;
    }

    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        destroy_live_objects();
        blocks_.clear();
        current_block_ = 0;
        next_slot_ = 0;
        free_list_.clear();
        size_ = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] std::size_t block_count() const noexcept {
        return blocks_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return blocks_.size() * BlockSize;
    }

private:
    struct Slot {
        T* object = nullptr;
        bool from_free_list = false;
    };

    struct Block {
        Block()
            : data(static_cast<T*>(::operator new(sizeof(T) * BlockSize, std::align_val_t{alignof(T)}))),
              live(std::make_unique<bool[]>(BlockSize)) {}

        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;

        Block(Block&& other) noexcept
            : data(other.data),
              live(std::move(other.live)),
              used(other.used) {
            other.data = nullptr;
            other.used = 0;
        }

        Block& operator=(Block&& other) noexcept {
            if (this == &other) {
                return *this;
            }

            ::operator delete(data, std::align_val_t{alignof(T)});
            data = other.data;
            live = std::move(other.live);
            used = other.used;
            other.data = nullptr;
            other.used = 0;
            return *this;
        }

        ~Block() {
            ::operator delete(data, std::align_val_t{alignof(T)});
        }

        [[nodiscard]] bool contains(const T* object) const noexcept {
            auto* bytes = reinterpret_cast<const std::byte*>(object);
            auto* begin = reinterpret_cast<const std::byte*>(data);
            auto* end = begin + sizeof(T) * BlockSize;
            return bytes >= begin && bytes < end;
        }

        [[nodiscard]] std::size_t index_of(const T* object) const noexcept {
            auto* bytes = reinterpret_cast<const std::byte*>(object);
            auto* begin = reinterpret_cast<const std::byte*>(data);
            return static_cast<std::size_t>(bytes - begin) / sizeof(T);
        }

        T* data = nullptr;
        std::unique_ptr<bool[]> live{};
        std::size_t used = 0;
    };

    [[nodiscard]] Slot acquire_slot() {
        if (!free_list_.empty()) {
            T* object = free_list_.back();
            free_list_.pop_back();
            return {object, true};
        }

        if (blocks_.empty()) {
            blocks_.emplace_back();
            current_block_ = 0;
            next_slot_ = 0;
        } else if (next_slot_ == BlockSize) {
            ++current_block_;
            if (current_block_ == blocks_.size()) {
                blocks_.emplace_back();
            }
            next_slot_ = 0;
        }

        Block& block = blocks_[current_block_];
        block.used = next_slot_ + 1;
        return {block.data + next_slot_++, false};
    }

    void release_unconstructed(Slot slot) {
        if (slot.from_free_list) {
            free_list_.push_back(slot.object);
            return;
        }

        assert(next_slot_ > 0);
        Block& block = blocks_[current_block_];
        assert(slot.object == block.data + next_slot_ - 1);
        --next_slot_;
        block.used = next_slot_;
    }

    [[nodiscard]] Block& block_for(const T* object) noexcept {
        for (Block& block : blocks_) {
            if (block.contains(object)) {
                return block;
            }
        }

        assert(false);
        return blocks_.front();
    }

    [[nodiscard]] const Block& block_for(const T* object) const noexcept {
        for (const Block& block : blocks_) {
            if (block.contains(object)) {
                return block;
            }
        }

        assert(false);
        return blocks_.front();
    }

    [[nodiscard]] bool is_live(const T* object) const noexcept {
        const Block& block = block_for(object);
        return block.live[block.index_of(object)];
    }

    void set_live(T* object, bool live) noexcept {
        Block& block = block_for(object);
        block.live[block.index_of(object)] = live;
    }

    void destroy_live_objects() noexcept(std::is_nothrow_destructible_v<T>) {
        for (Block& block : blocks_) {
            for (std::size_t i = 0; i < block.used; ++i) {
                if (block.live[i]) {
                    std::destroy_at(block.data + i);
                    block.live[i] = false;
                }
            }
            block.used = 0;
        }
    }

    std::vector<Block> blocks_{};
    std::vector<T*> free_list_{};
    std::size_t current_block_ = 0;
    std::size_t next_slot_ = 0;
    std::size_t size_ = 0;
};

} // namespace talus::detail

#pragma once

/// @file detail/pool_alloc.hpp
/// @brief Contiguous block pool allocator for tree nodes and other fixed types.
///
/// `PoolAllocator` constructs objects in reusable blocks, tracks live slots, and
/// preserves alignment for cache-sensitive structures such as `RTreeNode`. It is
/// an internal utility for fast allocation and deterministic cleanup in the
/// header-only indexes.

#include <algorithm>
#include <cassert>
#include "assert.hpp"
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace talus::detail {

/// @brief Fixed-size block allocator for address-stable objects.
///
/// Objects are constructed on demand in contiguous blocks and destroyed
/// explicitly. Freed slots are reused before new block capacity is consumed.
template<typename T, std::size_t BlockSize = 256>
class PoolAllocator {
    static_assert(BlockSize > 0);

public:
    /// Object type managed by this allocator.
    using value_type = T;

    /// @brief Constructs an empty pool.
    PoolAllocator() = default;

    /// Pools own object storage and cannot be copied.
    PoolAllocator(const PoolAllocator&) = delete;

    /// Pools own object storage and cannot be copied.
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// @brief Moves pool storage and live objects without relocating objects.
    PoolAllocator(PoolAllocator&&) noexcept = default;

    /// @brief Clears this pool and takes ownership of another pool's blocks.
    PoolAllocator& operator=(PoolAllocator&& other) noexcept(std::is_nothrow_destructible_v<T>) {
        if (this == &other) {
            return *this;
        }

        clear();
        blocks_ = std::move(other.blocks_);
        block_map_ = std::move(other.block_map_);
        free_list_ = std::move(other.free_list_);
        current_block_ = other.current_block_;
        next_slot_ = other.next_slot_;
        size_ = other.size_;

        other.block_map_.clear();
        other.free_list_.clear();
        other.current_block_ = 0;
        other.next_slot_ = 0;
        other.size_ = 0;
        return *this;
    }

    /// @brief Destroys all live objects and releases all blocks.
    ~PoolAllocator() {
        clear();
    }

    /// @brief Allocates enough raw storage for at least `object_count` objects.
    ///
    /// No objects are constructed, live objects remain valid, and the pool never
    /// shrinks. Later calls to `create()` consume the reserved slots before
    /// allocating additional blocks.
    void reserve(std::size_t object_count) {
        const std::size_t required_blocks =
            (object_count + BlockSize - 1) / BlockSize;
        if (required_blocks <= blocks_.size()) {
            return;
        }

        blocks_.reserve(required_blocks);
        block_map_.reserve(required_blocks);
        while (blocks_.size() < required_blocks) {
            add_block();
        }
    }

    /// @brief Constructs an object in pool storage and returns its address.
    ///
    /// If construction throws, the acquired slot is returned to the allocator.
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

        blocks_[slot.block_index].live[slot.slot_index] = true;
        ++size_;
        return object;
    }

    /// @brief Destroys a live object and makes its slot reusable.
    ///
    /// Passing null is a no-op. Non-null pointers must have been returned by
    /// this allocator and still be live.
    void destroy(T* object) {
        if (object == nullptr) {
            return;
        }

        auto [bi, si] = locate(object);
        Block& block = blocks_[bi];

        if (!block.live[si]) {
            TALUS_ASSERT(false && "destroy: double-destroy or unowned pointer");
        }

        std::destroy_at(object);
        block.live[si] = false;
        free_list_.push_back({object, bi, si, true});
        --size_;
    }

    /// @brief Destroys live objects while retaining allocated blocks for reuse.
    void reset() noexcept(std::is_nothrow_destructible_v<T>) {
        destroy_live_objects();
        current_block_ = 0;
        next_slot_ = 0;
        free_list_.clear();
        size_ = 0;
    }

    /// @brief Destroys live objects and releases all allocated blocks.
    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        destroy_live_objects();
        blocks_.clear();
        block_map_.clear();
        current_block_ = 0;
        next_slot_ = 0;
        free_list_.clear();
        size_ = 0;
    }

    /// @brief Returns the number of currently live objects.
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    /// @brief Returns true when no objects are live.
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    /// @brief Returns the number of allocated blocks.
    [[nodiscard]] std::size_t block_count() const noexcept {
        return blocks_.size();
    }

    /// @brief Returns total object slots across allocated blocks.
    [[nodiscard]] std::size_t capacity() const noexcept {
        return blocks_.size() * BlockSize;
    }

private:
    struct Slot {
        T* object = nullptr;
        std::size_t block_index = 0;
        std::size_t slot_index = 0;
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

    struct IndexEntry {
        const std::byte* base = nullptr;
        std::size_t block_index = 0;
    };

    void add_block() {
        blocks_.emplace_back();
        const std::byte* base = reinterpret_cast<const std::byte*>(blocks_.back().data);
        auto pos = std::lower_bound(block_map_.begin(), block_map_.end(), base,
            [](const IndexEntry& e, const std::byte* ptr) { return e.base < ptr; });
        block_map_.insert(pos, {base, blocks_.size() - 1});
    }

    [[nodiscard]] std::pair<std::size_t, std::size_t> locate(const T* object) const noexcept {
        const std::byte* bytes = reinterpret_cast<const std::byte*>(object);
        auto it = std::upper_bound(block_map_.begin(), block_map_.end(), bytes,
            [](const std::byte* ptr, const IndexEntry& e) { return ptr < e.base; });
        if (it != block_map_.begin()) {
            --it;
            const Block& block = blocks_[it->block_index];
            if (block.contains(object)) {
                return {it->block_index, block.index_of(object)};
            }
        }
        TALUS_ASSERT(false && "locate: pointer not owned by this pool");
    }

    [[nodiscard]] Slot acquire_slot() {
        if (!free_list_.empty()) {
            Slot slot = free_list_.back();
            free_list_.pop_back();
            return slot;
        }

        if (blocks_.empty()) {
            add_block();
            current_block_ = 0;
            next_slot_ = 0;
        } else if (next_slot_ == BlockSize) {
            ++current_block_;
            if (current_block_ == blocks_.size()) {
                add_block();
            }
            next_slot_ = 0;
        }

        const std::size_t si = next_slot_;
        Block& block = blocks_[current_block_];
        block.used = si + 1;
        ++next_slot_;
        return {block.data + si, current_block_, si, false};
    }

    void release_unconstructed(Slot slot) {
        if (slot.from_free_list) {
            free_list_.push_back(slot);
            return;
        }

        TALUS_ASSERT(next_slot_ > 0);
        TALUS_ASSERT(slot.object == blocks_[slot.block_index].data + next_slot_ - 1);
        --next_slot_;
        blocks_[slot.block_index].used = next_slot_;
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
    std::vector<IndexEntry> block_map_{};
    std::vector<Slot> free_list_{};
    std::size_t current_block_ = 0;
    std::size_t next_slot_ = 0;
    std::size_t size_ = 0;
};

} // namespace talus::detail

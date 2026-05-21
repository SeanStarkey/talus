#pragma once

/// @file detail/node.hpp
/// @brief Cache-aligned R-tree node storage primitives.
///
/// Defines the leaf and internal entry storage used by the R-tree. `RTreeNode`
/// owns value lifetimes explicitly so leaves can store arbitrary user-provided
/// types, including non-default-constructible, move-only, and immovable classes.
/// This header intentionally contains storage mechanics only; insertion,
/// splitting, search, and deletion algorithms live in later detail headers.

#include <cassert>
#include "assert.hpp"
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "../geometry.hpp"

namespace talus::detail {

template<typename T, typename Scalar = double, std::size_t MaxChildren = 9>
class RTreeNode;

/// @brief Leaf-node entry pairing stored bounds with a user value.
template<typename T, typename Scalar>
struct RTreeValueEntry {
    /// Bounds used to route and query this value.
    BoundingBox<Scalar> bounds{};

    /// User object stored in the leaf entry.
    T value;

    /// @brief Constructs the value in place with arbitrary constructor args.
    template<typename... Args>
    constexpr RTreeValueEntry(BoundingBox<Scalar> entry_bounds, Args&&... args)
        : bounds(entry_bounds),
          value(std::forward<Args>(args)...) {}
};

/// @brief Internal-node entry pairing stored bounds with a child node pointer.
template<typename T, typename Scalar = double, std::size_t MaxChildren = 9>
struct RTreeChildEntry {
    /// Node type referenced by this child entry.
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    /// Bounds covering all entries in `child`.
    BoundingBox<Scalar> bounds{};

    /// Child node owned by the tree pool.
    node_type* child = nullptr;

    /// @brief Creates an internal entry for `entry_child`.
    constexpr RTreeChildEntry(BoundingBox<Scalar> entry_bounds, node_type* entry_child) noexcept
        : bounds(entry_bounds),
          child(entry_child) {}
};

/// @brief Cache-aligned R-tree node storing either value entries or child entries.
///
/// The node owns the lifetimes of constructed entries in raw storage. It has one
/// overflow slot beyond `MaxChildren` so insertion can temporarily exceed normal
/// capacity before split logic runs.
template<typename T, typename Scalar, std::size_t MaxChildren>
class alignas(64) RTreeNode {
    static_assert(MaxChildren >= 4, "RTreeNode requires MaxChildren >= 4");

public:
    /// User value type stored by leaf entries.
    using value_type = T;

    /// Coordinate scalar used by entry bounds.
    using scalar_type = Scalar;

    /// This node specialization.
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    /// Leaf entry type stored by this node.
    using value_entry_type = RTreeValueEntry<T, Scalar>;

    /// Internal entry type stored by this node.
    using child_entry_type = RTreeChildEntry<T, Scalar, MaxChildren>;

    /// Normal maximum number of entries before overflow.
    static constexpr std::size_t max_children = MaxChildren;

    /// Minimum number of entries expected after split or reinsertion.
    static constexpr std::size_t min_children = MaxChildren / 2;

    /// Physical entry capacity, including one overflow slot.
    static constexpr std::size_t entry_capacity = MaxChildren + 1;

    /// @brief Constructs an empty leaf node by default, or internal node when false.
    explicit constexpr RTreeNode(bool leaf = true) noexcept
        : is_leaf_(leaf) {}

    /// Nodes are address-stable pool objects and cannot be copied.
    RTreeNode(const RTreeNode&) = delete;

    /// Nodes are address-stable pool objects and cannot be copied.
    RTreeNode& operator=(const RTreeNode&) = delete;

    /// Nodes are address-stable pool objects and cannot be moved.
    RTreeNode(RTreeNode&&) = delete;

    /// Nodes are address-stable pool objects and cannot be moved.
    RTreeNode& operator=(RTreeNode&&) = delete;

    /// @brief Destroys all live entries held by the node.
    ~RTreeNode() {
        clear();
    }

    /// @brief Returns true when the node stores value entries.
    [[nodiscard]] constexpr bool is_leaf() const noexcept {
        return is_leaf_;
    }

    /// @brief Returns true when the node stores child entries.
    [[nodiscard]] constexpr bool is_internal() const noexcept {
        return !is_leaf_;
    }

    /// @brief Returns the number of currently constructed entries.
    [[nodiscard]] constexpr std::size_t count() const noexcept {
        return count_;
    }

    /// @brief Returns true when the node has no entries.
    [[nodiscard]] constexpr bool empty() const noexcept {
        return count_ == 0;
    }

    /// @brief Returns true when the node has reached normal capacity.
    [[nodiscard]] constexpr bool full() const noexcept {
        return count_ >= max_children;
    }

    /// @brief Returns true when the overflow slot is occupied.
    [[nodiscard]] constexpr bool has_overflow() const noexcept {
        return count_ > max_children;
    }

    /// @brief Returns true when another entry can be appended, including overflow.
    [[nodiscard]] constexpr bool can_append_entry() const noexcept {
        return count_ < entry_capacity;
    }

    /// @brief Returns true when the node is below the configured minimum fill.
    [[nodiscard]] constexpr bool underfull() const noexcept {
        return count_ < min_children;
    }

    /// @brief Returns the union of all entry bounds, or a default box when empty.
    [[nodiscard]] constexpr BoundingBox<Scalar> bounds() const noexcept {
        return bounds_;
    }

    /// @brief Returns the parent node, or null for roots and detached nodes.
    [[nodiscard]] constexpr node_type* parent() noexcept {
        return parent_;
    }

    /// @brief Returns the parent node, or null for roots and detached nodes.
    [[nodiscard]] constexpr const node_type* parent() const noexcept {
        return parent_;
    }

    /// @brief Sets the parent pointer maintained by higher-level tree algorithms.
    constexpr void set_parent(node_type* parent) noexcept {
        parent_ = parent;
    }

    /// @brief Appends a leaf value by forwarding it into storage.
    template<typename U>
    value_entry_type& append_value(BoundingBox<Scalar> entry_bounds, U&& value) {
        return emplace_value(entry_bounds, std::forward<U>(value));
    }

    /// @brief Constructs and appends a leaf value in place.
    ///
    /// Requires this node to be a leaf and to have available entry capacity.
    template<typename... Args>
    value_entry_type& emplace_value(BoundingBox<Scalar> entry_bounds, Args&&... args) {
        TALUS_ASSERT(is_leaf_);
        TALUS_ASSERT(can_append_entry());
        TALUS_ASSERT(entry_bounds.is_valid());

        value_entry_type* entry = value_entry(count_);
        std::construct_at(entry, entry_bounds, std::forward<Args>(args)...);
        append_bounds(entry_bounds);
        ++count_;
        return *entry;
    }

    /// @brief Appends an internal child and updates that child node's parent.
    ///
    /// Requires this node to be internal and `child` to be non-null.
    child_entry_type& append_child(BoundingBox<Scalar> entry_bounds, node_type* child) {
        TALUS_ASSERT(!is_leaf_);
        TALUS_ASSERT(child != nullptr);
        TALUS_ASSERT(can_append_entry());
        TALUS_ASSERT(entry_bounds.is_valid());

        child_entry_type* entry = child_entry(count_);
        std::construct_at(entry, entry_bounds, child);
        child->set_parent(this);
        append_bounds(entry_bounds);
        ++count_;
        return *entry;
    }

    /// @brief Returns the mutable leaf entry at `index`.
    [[nodiscard]] value_entry_type& value_at(std::size_t index) noexcept {
        TALUS_ASSERT(is_leaf_);
        TALUS_ASSERT(index < count_);
        return *value_entry(index);
    }

    /// @brief Returns the immutable leaf entry at `index`.
    [[nodiscard]] const value_entry_type& value_at(std::size_t index) const noexcept {
        TALUS_ASSERT(is_leaf_);
        TALUS_ASSERT(index < count_);
        return *value_entry(index);
    }

    /// @brief Returns the mutable child entry at `index`.
    [[nodiscard]] child_entry_type& child_at(std::size_t index) noexcept {
        TALUS_ASSERT(!is_leaf_);
        TALUS_ASSERT(index < count_);
        return *child_entry(index);
    }

    /// @brief Returns the immutable child entry at `index`.
    [[nodiscard]] const child_entry_type& child_at(std::size_t index) const noexcept {
        TALUS_ASSERT(!is_leaf_);
        TALUS_ASSERT(index < count_);
        return *child_entry(index);
    }

    /// @brief Returns the bounds stored for any entry at `index`.
    [[nodiscard]] BoundingBox<Scalar> entry_bounds_at(std::size_t index) const noexcept {
        TALUS_ASSERT(index < count_);
        return is_leaf_ ? value_entry(index)->bounds : child_entry(index)->bounds;
    }

    /// @brief Returns a mutable span over live leaf entries.
    ///
    /// Writing to an entry's `bounds` field through this span bypasses
    /// `recompute_bounds()`. Call `recompute_bounds()` or `update_bounds()`
    /// after any direct modification to keep `bounds()` consistent.
    [[nodiscard]] std::span<value_entry_type> values() noexcept {
        TALUS_ASSERT(is_leaf_);
        if (count_ == 0) return {};
        return {value_entry(0), count_};
    }

    /// @brief Returns an immutable span over live leaf entries.
    [[nodiscard]] std::span<const value_entry_type> values() const noexcept {
        TALUS_ASSERT(is_leaf_);
        if (count_ == 0) return {};
        return {value_entry(0), count_};
    }

    /// @brief Returns a mutable span over live child entries.
    ///
    /// Writing to an entry's `bounds` field through this span bypasses
    /// `recompute_bounds()`. Call `recompute_bounds()` or `update_bounds()`
    /// after any direct modification to keep `bounds()` consistent.
    [[nodiscard]] std::span<child_entry_type> children() noexcept {
        TALUS_ASSERT(!is_leaf_);
        if (count_ == 0) return {};
        return {child_entry(0), count_};
    }

    /// @brief Returns an immutable span over live child entries.
    [[nodiscard]] std::span<const child_entry_type> children() const noexcept {
        TALUS_ASSERT(!is_leaf_);
        if (count_ == 0) return {};
        return {child_entry(0), count_};
    }

    /// @brief Removes the entry at `index` by moving the last entry into its slot.
    ///
    /// Entry order is not preserved. Bounds are recomputed after removal.
    void remove_at(std::size_t index) {
        TALUS_ASSERT(index < count_);

        const std::size_t last = count_ - 1;

        if (is_leaf_) {
            std::destroy_at(value_entry(index));
            if (index != last) {
                std::construct_at(value_entry(index), std::move(*value_entry(last)));
                std::destroy_at(value_entry(last));
            }
        } else {
            child_entry(index)->child->set_parent(nullptr);
            std::destroy_at(child_entry(index));
            if (index != last) {
                std::construct_at(child_entry(index), std::move(*child_entry(last)));
                std::destroy_at(child_entry(last));
            }
        }

        --count_;
        recompute_bounds();
    }

    /// @brief Replaces one entry's stored bounds and refreshes node bounds.
    void update_bounds(std::size_t index, BoundingBox<Scalar> entry_bounds) noexcept {
        TALUS_ASSERT(index < count_);
        TALUS_ASSERT(entry_bounds.is_valid());

        if (is_leaf_) {
            value_entry(index)->bounds = entry_bounds;
        } else {
            child_entry(index)->bounds = entry_bounds;
        }

        recompute_bounds();
    }

    /// @brief Rebuilds node bounds from the current entries.
    void recompute_bounds() noexcept {
        if (count_ == 0) {
            bounds_ = {};
            return;
        }

        BoundingBox<Scalar> combined = entry_bounds_at(0);
        for (std::size_t i = 1; i < count_; ++i) {
            combined = combined.expand(entry_bounds_at(i));
        }
        bounds_ = combined;
    }

    /// @brief Destroys all entries and resets count and bounds.
    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        if (is_leaf_) {
            for (std::size_t i = 0; i < count_; ++i) {
                std::destroy_at(value_entry(i));
            }
        } else {
            for (std::size_t i = 0; i < count_; ++i) {
                child_entry(i)->child->set_parent(nullptr);
                std::destroy_at(child_entry(i));
            }
        }

        count_ = 0;
        bounds_ = {};
    }

    /// @brief Clears entries and converts the node to leaf mode.
    void reset_as_leaf() noexcept(std::is_nothrow_destructible_v<T>) {
        clear();
        is_leaf_ = true;
        parent_ = nullptr;
    }

    /// @brief Clears entries and converts the node to internal mode.
    void reset_as_internal() noexcept(std::is_nothrow_destructible_v<T>) {
        clear();
        is_leaf_ = false;
        parent_ = nullptr;
    }

private:
    union EntryStorage {
        constexpr EntryStorage() noexcept {}
        ~EntryStorage() {}

        alignas(value_entry_type) std::byte values[sizeof(value_entry_type) * entry_capacity];
        alignas(child_entry_type) std::byte children[sizeof(child_entry_type) * entry_capacity];
    };

    [[nodiscard]] value_entry_type* value_entry(std::size_t index) noexcept {
        return std::launder(reinterpret_cast<value_entry_type*>(
            storage_.values + sizeof(value_entry_type) * index));
    }

    [[nodiscard]] const value_entry_type* value_entry(std::size_t index) const noexcept {
        return std::launder(reinterpret_cast<const value_entry_type*>(
            storage_.values + sizeof(value_entry_type) * index));
    }

    [[nodiscard]] child_entry_type* child_entry(std::size_t index) noexcept {
        return std::launder(reinterpret_cast<child_entry_type*>(
            storage_.children + sizeof(child_entry_type) * index));
    }

    [[nodiscard]] const child_entry_type* child_entry(std::size_t index) const noexcept {
        return std::launder(reinterpret_cast<const child_entry_type*>(
            storage_.children + sizeof(child_entry_type) * index));
    }

    constexpr void append_bounds(BoundingBox<Scalar> entry_bounds) noexcept {
        bounds_ = count_ == 0 ? entry_bounds : bounds_.expand(entry_bounds);
    }

    bool is_leaf_ = true;
    std::size_t count_ = 0;
    BoundingBox<Scalar> bounds_{};
    node_type* parent_ = nullptr;
    EntryStorage storage_{};
};

static_assert(alignof(RTreeNode<int>) == 64);

} // namespace talus::detail

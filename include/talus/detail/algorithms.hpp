#pragma once

/// @file detail/algorithms.hpp
/// @brief Core R-tree algorithms operating on RTreeNode storage.
///
/// This header contains the tree logic layered above `node.hpp`. The public
/// `SpatialIndex` wrapper owns roots and pools; these helpers operate on nodes
/// that have already been allocated by that wrapper or by tests.

#include <cstddef>
#include <limits>
#include <utility>

#include "assert.hpp"
#include "node.hpp"
#include "../geometry.hpp"

namespace talus::detail {

/// @brief Result returned from low-level insertion.
template<typename Node>
struct RTreeInsertResult {
    /// Leaf that received the inserted entry.
    Node* leaf = nullptr;

    /// Node that now exceeds normal capacity and must be split, or null.
    Node* overflow = nullptr;

    /// True when the entry was appended.
    bool inserted = false;

    /// @brief Returns true when split handling is required.
    [[nodiscard]] constexpr bool needs_split() const noexcept {
        return overflow != nullptr;
    }
};

/// @brief Returns the index of `child` in `parent`.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] std::size_t find_child_index(
    const RTreeNode<T, Scalar, MaxChildren>& parent,
    const RTreeNode<T, Scalar, MaxChildren>* child) noexcept {
    TALUS_ASSERT(parent.is_internal());
    TALUS_ASSERT(child != nullptr);

    for (std::size_t i = 0; i < parent.count(); ++i) {
        if (parent.child_at(i).child == child) {
            return i;
        }
    }

    TALUS_ASSERT(false && "child is not attached to parent");
    return parent.count();
}

/// @brief Refreshes stored child bounds from `node` up to the root.
template<typename T, typename Scalar, std::size_t MaxChildren>
void refresh_ancestor_bounds(RTreeNode<T, Scalar, MaxChildren>& node) noexcept {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    node_type* child = &node;
    node_type* parent = child->parent();

    while (parent != nullptr) {
        const std::size_t index = find_child_index(*parent, child);
        parent->update_bounds(index, child->bounds());
        child = parent;
        parent = child->parent();
    }
}

/// @brief Chooses the leaf that should receive an entry with `entry_bounds`.
///
/// Descends by minimum area enlargement. Ties are broken by smaller current
/// area, then by fewer entries to keep behavior deterministic.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] RTreeNode<T, Scalar, MaxChildren>* choose_leaf(
    RTreeNode<T, Scalar, MaxChildren>& root,
    BoundingBox<Scalar> entry_bounds) noexcept {
    TALUS_ASSERT(entry_bounds.is_valid());

    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    node_type* node = &root;

    while (node->is_internal()) {
        TALUS_ASSERT(!node->empty());

        std::size_t best = 0;
        Scalar best_enlargement = std::numeric_limits<Scalar>::infinity();
        Scalar best_area = std::numeric_limits<Scalar>::infinity();
        std::size_t best_count = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < node->count(); ++i) {
            const auto& child = node->child_at(i);
            const Scalar enlargement = child.bounds.enlarged_area(entry_bounds);
            const Scalar area = child.bounds.area();
            const std::size_t count = child.child->count();

            if (enlargement < best_enlargement
                || (enlargement == best_enlargement && area < best_area)
                || (enlargement == best_enlargement && area == best_area && count < best_count)) {
                best = i;
                best_enlargement = enlargement;
                best_area = area;
                best_count = count;
            }
        }

        node = node->child_at(best).child;
        TALUS_ASSERT(node != nullptr);
    }

    return node;
}

/// @brief Inserts a value into the chosen leaf without performing splits.
///
/// The target leaf may use its overflow slot. When this happens, the result's
/// `overflow` member points to that leaf so later split logic can continue.
template<typename T, typename Scalar, std::size_t MaxChildren, typename U>
RTreeInsertResult<RTreeNode<T, Scalar, MaxChildren>> insert(
    RTreeNode<T, Scalar, MaxChildren>& root,
    BoundingBox<Scalar> entry_bounds,
    U&& value) {
    TALUS_ASSERT(entry_bounds.is_valid());

    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    node_type* leaf = choose_leaf(root, entry_bounds);
    TALUS_ASSERT(leaf != nullptr);
    TALUS_ASSERT(leaf->is_leaf());

    if (!leaf->can_append_entry()) {
        TALUS_ASSERT(false && "insert requires split handling before appending past overflow");
        return {leaf, nullptr, false};
    }

    leaf->append_value(entry_bounds, std::forward<U>(value));
    refresh_ancestor_bounds(*leaf);

    return {leaf, leaf->has_overflow() ? leaf : nullptr, true};
}

} // namespace talus::detail

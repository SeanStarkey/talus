#pragma once

/// @file detail/algorithms.hpp
/// @brief Core R-tree algorithms operating on RTreeNode storage.
///
/// This header contains the tree logic layered above `node.hpp`. The public
/// `SpatialIndex` wrapper owns roots and pools; these helpers operate on nodes
/// that have already been allocated by that wrapper or by tests.

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

#include "assert.hpp"
#include "node.hpp"
#include "../geometry.hpp"

namespace talus::detail {

/// @brief Returns the area shared by two bounding boxes.
template<typename Scalar>
[[nodiscard]] constexpr Scalar overlap_area(
    BoundingBox<Scalar> lhs,
    BoundingBox<Scalar> rhs) noexcept {
    const Scalar min_x = std::max(lhs.min.x, rhs.min.x);
    const Scalar min_y = std::max(lhs.min.y, rhs.min.y);
    const Scalar max_x = std::min(lhs.max.x, rhs.max.x);
    const Scalar max_y = std::min(lhs.max.y, rhs.max.y);

    const Scalar width = max_x - min_x;
    const Scalar height = max_y - min_y;
    return (width > Scalar{0} && height > Scalar{0}) ? width * height : Scalar{0};
}

/// @brief Computes how much a child expansion would increase sibling overlap.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] Scalar overlap_enlargement(
    const RTreeNode<T, Scalar, MaxChildren>& parent,
    std::size_t child_index,
    BoundingBox<Scalar> expanded_bounds) noexcept {
    TALUS_ASSERT(parent.is_internal());
    TALUS_ASSERT(child_index < parent.count());

    const BoundingBox<Scalar> current_bounds = parent.child_at(child_index).bounds;
    Scalar current_overlap = Scalar{0};
    Scalar expanded_overlap = Scalar{0};

    for (std::size_t i = 0; i < parent.count(); ++i) {
        if (i == child_index) {
            continue;
        }

        const BoundingBox<Scalar> sibling_bounds = parent.child_at(i).bounds;
        current_overlap += overlap_area(current_bounds, sibling_bounds);
        expanded_overlap += overlap_area(expanded_bounds, sibling_bounds);
    }

    return expanded_overlap - current_overlap;
}

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
/// Descends with R*-tree ChooseSubtree semantics. When the children are leaves,
/// minimum overlap enlargement is preferred first; otherwise minimum area
/// enlargement is used. Ties are broken by smaller current area, then by fewer
/// entries to keep behavior deterministic.
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
        Scalar best_overlap_enlargement = std::numeric_limits<Scalar>::infinity();
        Scalar best_enlargement = std::numeric_limits<Scalar>::infinity();
        Scalar best_area = std::numeric_limits<Scalar>::infinity();
        std::size_t best_count = std::numeric_limits<std::size_t>::max();
        const auto& first_child = node->child_at(0);
        TALUS_ASSERT(first_child.child != nullptr);
        const bool choose_by_overlap = first_child.child->is_leaf();

        for (std::size_t i = 0; i < node->count(); ++i) {
            const auto& child = node->child_at(i);
            TALUS_ASSERT(child.child != nullptr);
            TALUS_ASSERT(child.child->is_leaf() == choose_by_overlap);

            const BoundingBox<Scalar> expanded_bounds = child.bounds.expand(entry_bounds);
            const Scalar overlap_growth = choose_by_overlap
                ? overlap_enlargement(*node, i, expanded_bounds)
                : Scalar{0};
            const Scalar enlargement = child.bounds.enlarged_area(entry_bounds);
            const Scalar area = child.bounds.area();
            const std::size_t count = child.child->count();

            if ((choose_by_overlap && overlap_growth < best_overlap_enlargement)
                || (choose_by_overlap && overlap_growth == best_overlap_enlargement
                    && enlargement < best_enlargement)
                || (choose_by_overlap && overlap_growth == best_overlap_enlargement
                    && enlargement == best_enlargement && area < best_area)
                || (choose_by_overlap && overlap_growth == best_overlap_enlargement
                    && enlargement == best_enlargement && area == best_area && count < best_count)
                || (!choose_by_overlap && enlargement < best_enlargement)
                || (!choose_by_overlap && enlargement == best_enlargement && area < best_area)
                || (!choose_by_overlap && enlargement == best_enlargement
                    && area == best_area && count < best_count)) {
                best = i;
                best_overlap_enlargement = overlap_growth;
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

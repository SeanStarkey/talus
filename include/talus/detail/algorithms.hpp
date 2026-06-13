#pragma once

/// @file detail/algorithms.hpp
/// @brief Core R-tree algorithms operating on RTreeNode storage.
///
/// This header contains the tree logic layered above `node.hpp`. The public
/// `SpatialIndex` wrapper owns roots and pools; these helpers operate on nodes
/// that have already been allocated by that wrapper or by tests.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "assert.hpp"
#include "node.hpp"
#include "pool_alloc.hpp"
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

/// @brief Result returned after split propagation adjusts the tree.
template<typename Node>
struct RTreeAdjustResult {
    /// Stable root object after adjustment.
    Node* root = nullptr;

    /// Number of node splits performed while walking back to the root.
    std::size_t split_count = 0;

    /// True when a root split created a new internal root level.
    bool grew_height = false;

    /// True when all required bound refresh and split propagation completed.
    bool adjusted = false;
};

/// @brief Result returned from insertion with split propagation enabled.
template<typename Node>
struct RTreeAdjustedInsertResult {
    /// Stable root object after insertion.
    Node* root = nullptr;

    /// True when the entry was appended.
    bool inserted = false;

    /// Number of node splits performed while adjusting the tree.
    std::size_t split_count = 0;

    /// True when a root split created a new internal root level.
    bool grew_height = false;
};

/// @brief Result returned after deleting one matching value.
template<typename Node>
struct RTreeDeleteResult {
    /// Stable root object after condensing and reinsertion.
    Node* root = nullptr;

    /// True when a matching leaf entry was found and removed.
    bool erased = false;

    /// Number of underfull non-root nodes detached while condensing the tree.
    std::size_t condensed_nodes = 0;

    /// Number of remaining entries reinserted from detached subtrees.
    std::size_t reinserted_entries = 0;
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
    TALUS_UNREACHABLE();
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
/// Descends with R*-tree ChooseSubtree semantics. Children are ranked
/// lexicographically by: overlap enlargement (leaf children only), area
/// enlargement, margin enlargement, current area, current margin, then fewer
/// entries. The margin terms break the ties that area leaves on point and
/// axis-aligned data — where boxes have zero area and the area terms give no
/// signal — and the final term keeps the choice deterministic.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] RTreeNode<T, Scalar, MaxChildren>* choose_leaf(
    RTreeNode<T, Scalar, MaxChildren>& root,
    BoundingBox<Scalar> entry_bounds) noexcept {
    TALUS_ASSERT(entry_bounds.is_valid());

    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    node_type* node = &root;

    while (node->is_internal()) {
        TALUS_ASSERT(!node->empty());

        const auto& first_child = node->child_at(0);
        TALUS_ASSERT(first_child.child != nullptr);
        // R*-tree ChooseSubtree minimizes overlap enlargement only when the
        // children are leaves; at higher levels overlap is held at zero so the
        // ranking starts from area enlargement.
        const bool choose_by_overlap = first_child.child->is_leaf();

        // Lexicographic ranking key (smallest wins): overlap enlargement, area
        // enlargement, margin enlargement, current area, current margin, entry
        // count. The margin terms discriminate degenerate (zero-area) boxes.
        using Key = std::tuple<Scalar, Scalar, Scalar, Scalar, Scalar, std::size_t>;
        std::size_t best = 0;
        Key best_key{};

        for (std::size_t i = 0; i < node->count(); ++i) {
            const auto& child = node->child_at(i);
            TALUS_ASSERT(child.child != nullptr);
            TALUS_ASSERT(child.child->is_leaf() == choose_by_overlap);

            const BoundingBox<Scalar> expanded = child.bounds.expand(entry_bounds);
            const Scalar overlap_growth = choose_by_overlap
                ? overlap_enlargement(*node, i, expanded)
                : Scalar{0};
            const Key key{
                overlap_growth,
                expanded.area() - child.bounds.area(),
                expanded.margin() - child.bounds.margin(),
                child.bounds.area(),
                child.bounds.margin(),
                child.child->count(),
            };

            if (i == 0 || key < best_key) {
                best = i;
                best_key = key;
            }
        }

        node = node->child_at(best).child;
        TALUS_ASSERT(node != nullptr);
    }

    return node;
}


/// @brief Result returned after splitting an overflowing node.
template<typename Node>
struct RTreeSplitResult {
    /// Original node after redistribution.
    Node* left = nullptr;

    /// Caller-provided sibling node after redistribution.
    Node* right = nullptr;

    /// True when redistribution completed.
    bool split = false;
};

namespace split_detail {

template<typename Scalar, std::size_t Capacity>
struct SplitChoice {
    std::array<std::size_t, Capacity> order{};
    std::size_t left_count = 0;
};

template<typename Scalar, std::size_t Capacity>
void sort_order(
    std::array<std::size_t, Capacity>& order,
    const std::array<BoundingBox<Scalar>, Capacity>& bounds,
    std::size_t count,
    std::size_t axis,
    bool use_max) {
    std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count),
        [&](std::size_t lhs, std::size_t rhs) {
            const auto& left = bounds[lhs];
            const auto& right = bounds[rhs];
            const Scalar left_primary = axis == 0
                ? (use_max ? left.max.x : left.min.x)
                : (use_max ? left.max.y : left.min.y);
            const Scalar right_primary = axis == 0
                ? (use_max ? right.max.x : right.min.x)
                : (use_max ? right.max.y : right.min.y);
            if (left_primary != right_primary) {
                return left_primary < right_primary;
            }

            const Scalar left_secondary = axis == 0
                ? (use_max ? left.max.y : left.min.y)
                : (use_max ? left.max.x : left.min.x);
            const Scalar right_secondary = axis == 0
                ? (use_max ? right.max.y : right.min.y)
                : (use_max ? right.max.x : right.min.x);
            if (left_secondary != right_secondary) {
                return left_secondary < right_secondary;
            }

            return lhs < rhs;
        });
}

template<typename Scalar, std::size_t Capacity>
[[nodiscard]] BoundingBox<Scalar> ordered_bounds(
    const std::array<std::size_t, Capacity>& order,
    const std::array<BoundingBox<Scalar>, Capacity>& bounds,
    std::size_t first,
    std::size_t count) noexcept {
    TALUS_ASSERT(count > 0);

    BoundingBox<Scalar> result = bounds[order[first]];
    for (std::size_t i = 1; i < count; ++i) {
        result = result.expand(bounds[order[first + i]]);
    }
    return result;
}

template<typename Scalar, std::size_t Capacity>
[[nodiscard]] Scalar margin_sum(
    const std::array<std::size_t, Capacity>& order,
    const std::array<BoundingBox<Scalar>, Capacity>& bounds,
    std::size_t count,
    std::size_t min_children) noexcept {
    Scalar sum = Scalar{0};
    for (std::size_t left_count = min_children; left_count <= count - min_children; ++left_count) {
        const BoundingBox<Scalar> left = ordered_bounds(order, bounds, 0, left_count);
        const BoundingBox<Scalar> right = ordered_bounds(order, bounds, left_count, count - left_count);
        sum += left.margin() + right.margin();
    }
    return sum;
}

template<typename Scalar, std::size_t Capacity>
[[nodiscard]] SplitChoice<Scalar, Capacity> choose_split(
    const std::array<BoundingBox<Scalar>, Capacity>& bounds,
    std::size_t count,
    std::size_t min_children) {
    SplitChoice<Scalar, Capacity> best{};
    std::array<std::size_t, Capacity> base_order{};
    for (std::size_t i = 0; i < count; ++i) {
        base_order[i] = i;
    }

    std::size_t best_axis = 0;
    Scalar best_margin = std::numeric_limits<Scalar>::infinity();
    for (std::size_t axis = 0; axis < 2; ++axis) {
        Scalar axis_margin = Scalar{0};
        for (bool use_max : {false, true}) {
            auto order = base_order;
            sort_order(order, bounds, count, axis, use_max);
            axis_margin += margin_sum(order, bounds, count, min_children);
        }

        if (axis_margin < best_margin) {
            best_axis = axis;
            best_margin = axis_margin;
        }
    }

    Scalar best_overlap = std::numeric_limits<Scalar>::infinity();
    Scalar best_area = std::numeric_limits<Scalar>::infinity();
    Scalar best_total_margin = std::numeric_limits<Scalar>::infinity();
    for (bool use_max : {false, true}) {
        auto order = base_order;
        sort_order(order, bounds, count, best_axis, use_max);

        for (std::size_t left_count = min_children; left_count <= count - min_children; ++left_count) {
            const BoundingBox<Scalar> left = ordered_bounds(order, bounds, 0, left_count);
            const BoundingBox<Scalar> right = ordered_bounds(order, bounds, left_count, count - left_count);
            const Scalar overlap = overlap_area(left, right);
            const Scalar area = left.area() + right.area();
            // Margin breaks the ties overlap and area leave on point/axis-aligned
            // groups, where both collapse to zero.
            const Scalar total_margin = left.margin() + right.margin();

            if (overlap < best_overlap
                || (overlap == best_overlap && area < best_area)
                || (overlap == best_overlap && area == best_area && total_margin < best_total_margin)
                || (overlap == best_overlap && area == best_area && total_margin == best_total_margin
                    && left_count > best.left_count)) {
                best.order = order;
                best.left_count = left_count;
                best_overlap = overlap;
                best_area = area;
                best_total_margin = total_margin;
            }
        }
    }

    return best;
}

template<typename T, typename Scalar, std::size_t MaxChildren>
void split_leaf_node(
    RTreeNode<T, Scalar, MaxChildren>& node,
    RTreeNode<T, Scalar, MaxChildren>& sibling) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    using entry_type = typename node_type::value_entry_type;
    constexpr std::size_t capacity = node_type::entry_capacity;

    static_assert(node_type::can_relocate_value_entries,
        "split_node requires move-constructible value entries for leaf nodes");

    const std::size_t count = node.count();
    std::array<BoundingBox<Scalar>, capacity> bounds{};
    std::vector<entry_type> entries;
    entries.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        bounds[i] = node.value_at(i).bounds;
        entries.emplace_back(std::move(node.value_at(i)));
    }

    const auto choice = choose_split(bounds, count, node_type::min_children);
    node_type* parent = node.parent();
    node.clear();
    sibling.reset_as_leaf();
    sibling.set_parent(parent);

    for (std::size_t i = 0; i < choice.left_count; ++i) {
        node.append_value_entry(std::move(entries[choice.order[i]]));
    }
    for (std::size_t i = choice.left_count; i < count; ++i) {
        sibling.append_value_entry(std::move(entries[choice.order[i]]));
    }
}

template<typename T, typename Scalar, std::size_t MaxChildren>
void split_internal_node(
    RTreeNode<T, Scalar, MaxChildren>& node,
    RTreeNode<T, Scalar, MaxChildren>& sibling) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    using entry_type = typename node_type::child_entry_type;
    constexpr std::size_t capacity = node_type::entry_capacity;

    const std::size_t count = node.count();
    std::array<BoundingBox<Scalar>, capacity> bounds{};
    std::vector<entry_type> entries;
    entries.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        bounds[i] = node.child_at(i).bounds;
        entries.emplace_back(node.child_at(i));
    }

    const auto choice = choose_split(bounds, count, node_type::min_children);
    node_type* parent = node.parent();
    node.clear();
    sibling.reset_as_internal();
    sibling.set_parent(parent);

    for (std::size_t i = 0; i < choice.left_count; ++i) {
        entry_type& entry = entries[choice.order[i]];
        node.append_child(entry.bounds, entry.child);
    }
    for (std::size_t i = choice.left_count; i < count; ++i) {
        entry_type& entry = entries[choice.order[i]];
        sibling.append_child(entry.bounds, entry.child);
    }
}

} // namespace split_detail

/// @brief Splits an overflowing node into the node and a caller-provided sibling.
///
/// The sibling is reset to the same leaf/internal mode as `node`. Parent split
/// propagation is handled by AdjustTree; this helper only redistributes entries,
/// recomputes node bounds, and updates child parent pointers for internal nodes.
template<typename T, typename Scalar, std::size_t MaxChildren>
RTreeSplitResult<RTreeNode<T, Scalar, MaxChildren>> split_node(
    RTreeNode<T, Scalar, MaxChildren>& node,
    RTreeNode<T, Scalar, MaxChildren>& sibling) {
    TALUS_ASSERT(node.has_overflow());
    TALUS_ASSERT(&node != &sibling);

    if (node.is_leaf()) {
        split_detail::split_leaf_node(node, sibling);
    } else {
        split_detail::split_internal_node(node, sibling);
    }

    TALUS_ASSERT(!node.has_overflow());
    TALUS_ASSERT(!sibling.has_overflow());
    TALUS_ASSERT(!node.underfull());
    TALUS_ASSERT(!sibling.underfull());

    return {&node, &sibling, true};
}

namespace adjust_detail {

template<typename T, typename Scalar, std::size_t MaxChildren>
void move_entries(
    RTreeNode<T, Scalar, MaxChildren>& source,
    RTreeNode<T, Scalar, MaxChildren>& destination) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    TALUS_ASSERT(source.count() <= node_type::max_children);
    TALUS_ASSERT(&source != &destination);

    if (source.is_leaf()) {
        using entry_type = typename node_type::value_entry_type;
        static_assert(node_type::can_relocate_value_entries,
            "root split propagation requires move-constructible value entries");

        std::vector<entry_type> entries;
        entries.reserve(source.count());
        for (std::size_t i = 0; i < source.count(); ++i) {
            entries.emplace_back(std::move(source.value_at(i)));
        }

        source.clear();
        destination.reset_as_leaf();
        for (auto& entry : entries) {
            destination.append_value_entry(std::move(entry));
        }
    } else {
        using entry_type = typename node_type::child_entry_type;

        std::vector<entry_type> entries;
        entries.reserve(source.count());
        for (std::size_t i = 0; i < source.count(); ++i) {
            entries.emplace_back(source.child_at(i));
        }

        source.clear();
        destination.reset_as_internal();
        for (auto& entry : entries) {
            destination.append_child(entry.bounds, entry.child);
        }
    }
}

template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize>
void grow_root(
    RTreeNode<T, Scalar, MaxChildren>& root,
    RTreeNode<T, Scalar, MaxChildren>& right,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    node_type* left = pool.create(root.is_leaf());
    move_entries(root, *left);

    root.reset_as_internal();
    root.append_child(left->bounds(), left);
    root.append_child(right.bounds(), &right);
    root.set_parent(nullptr);
}

} // namespace adjust_detail

/// @brief Propagates an overflowing node split upward and refreshes ancestor bounds.
///
/// `pool` supplies any split siblings and the promoted left child required when
/// the stable root object itself must become a new internal root.
template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize>
RTreeAdjustResult<RTreeNode<T, Scalar, MaxChildren>> adjust_tree(
    RTreeNode<T, Scalar, MaxChildren>& root,
    RTreeNode<T, Scalar, MaxChildren>* overflow,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    if (overflow == nullptr) {
        root.recompute_bounds();
        return {&root, 0, false, true};
    }

    TALUS_ASSERT(overflow->has_overflow());

    RTreeAdjustResult<node_type> result{&root, 0, false, false};
    node_type* node = overflow;

    while (node != nullptr && node->has_overflow()) {
        node_type* sibling = pool.create(node->is_leaf());
        split_node(*node, *sibling);
        ++result.split_count;

        node_type* parent = node->parent();
        if (parent == nullptr) {
            TALUS_ASSERT(node == &root);
            adjust_detail::grow_root(root, *sibling, pool);
            result.grew_height = true;
            result.adjusted = true;
            return result;
        }

        const std::size_t node_index = find_child_index(*parent, node);
        parent->update_bounds(node_index, node->bounds());
        parent->append_child(sibling->bounds(), sibling);
        node = parent;
    }

    if (node != nullptr) {
        refresh_ancestor_bounds(*node);
    }

    result.adjusted = true;
    return result;
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

/// @brief Inserts a value and propagates any required splits to the root.
template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize, typename U>
RTreeAdjustedInsertResult<RTreeNode<T, Scalar, MaxChildren>> insert_with_split(
    RTreeNode<T, Scalar, MaxChildren>& root,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool,
    BoundingBox<Scalar> entry_bounds,
    U&& value) {
    auto insert_result = insert(root, entry_bounds, std::forward<U>(value));
    if (!insert_result.inserted) {
        return {&root, false, 0, false};
    }

    if (!insert_result.needs_split()) {
        return {&root, true, 0, false};
    }

    auto adjust_result = adjust_tree(root, insert_result.overflow, pool);
    return {
        adjust_result.root,
        true,
        adjust_result.split_count,
        adjust_result.grew_height
    };
}

namespace delete_detail {

template<typename Node>
struct LocatedEntry {
    Node* leaf = nullptr;
    std::size_t index = 0;
};

template<typename T, typename Scalar, std::size_t MaxChildren, typename Predicate>
[[nodiscard]] LocatedEntry<RTreeNode<T, Scalar, MaxChildren>> find_leaf_entry(
    RTreeNode<T, Scalar, MaxChildren>& node,
    BoundingBox<Scalar> entry_bounds,
    Predicate& predicate) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    if (node.empty() || !node.bounds().intersects(entry_bounds)) {
        return {};
    }

    if (node.is_leaf()) {
        for (std::size_t i = 0; i < node.count(); ++i) {
            auto& entry = node.value_at(i);
            if (entry.bounds == entry_bounds && predicate(entry.value())) {
                return {&node, i};
            }
        }
        return {};
    }

    for (auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        if (!entry.bounds.intersects(entry_bounds)) {
            continue;
        }

        LocatedEntry<node_type> found = find_leaf_entry(*entry.child, entry_bounds, predicate);
        if (found.leaf != nullptr) {
            return found;
        }
    }

    return {};
}

template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize>
void collect_subtree_entries(
    RTreeNode<T, Scalar, MaxChildren>& node,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool,
    std::vector<typename RTreeNode<T, Scalar, MaxChildren>::value_entry_type>& entries) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    if (node.is_leaf()) {
        entries.reserve(entries.size() + node.count());
        for (std::size_t i = 0; i < node.count(); ++i) {
            entries.emplace_back(std::move(node.value_at(i)));
        }
        node.clear();
        pool.destroy(&node);
        return;
    }

    std::vector<node_type*> children;
    children.reserve(node.count());
    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        children.push_back(entry.child);
    }

    node.clear();
    for (node_type* child : children) {
        collect_subtree_entries(*child, pool, entries);
    }
    pool.destroy(&node);
}

template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize>
[[nodiscard]] bool collapse_root_if_needed(
    RTreeNode<T, Scalar, MaxChildren>& root,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    bool collapsed = false;
    while (root.is_internal() && root.count() <= 1) {
        if (root.count() == 0) {
            root.reset_as_leaf();
            return true;
        }

        node_type* only_child = root.child_at(0).child;
        TALUS_ASSERT(only_child != nullptr);
        adjust_detail::move_entries(*only_child, root);
        pool.destroy(only_child);
        root.set_parent(nullptr);
        collapsed = true;
    }

    return collapsed;
}

template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize>
std::size_t reinsert_entries(
    RTreeNode<T, Scalar, MaxChildren>& root,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool,
    std::vector<typename RTreeNode<T, Scalar, MaxChildren>::value_entry_type>& entries) {
    std::size_t reinserted = 0;
    for (auto& entry : entries) {
        auto result = insert_with_split(root, pool, entry.bounds, std::move(entry.value()));
        TALUS_ASSERT(result.inserted);
        (void)result;
        ++reinserted;
    }
    return reinserted;
}

/// @brief Drops overflow-slot entries left behind by an aborted split.
///
/// When `insert_with_split` fails to allocate a split sibling, exactly one node
/// on the insertion path may be left holding an entry in its overflow slot.
/// Removing that entry (and, for internal nodes, unreachably detaching its
/// subtree) restores the capacity invariant so the tree stays valid for later
/// operations. Entry loss here is covered by erase's basic exception guarantee.
template<typename T, typename Scalar, std::size_t MaxChildren>
void drop_overflow_entries(RTreeNode<T, Scalar, MaxChildren>& node) {
    if (node.has_overflow()) {
        node.remove_at(node.count() - 1);
    }

    if (node.is_internal()) {
        for (const auto& entry : node.children()) {
            TALUS_ASSERT(entry.child != nullptr);
            drop_overflow_entries(*entry.child);
        }
    }
}

} // namespace delete_detail

/// @brief Returns the number of value entries reachable from `node`.
///
/// Never allocates or throws, so it is safe to call from exception-recovery
/// paths that resynchronize cached size counters after a failed mutation.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] std::size_t count_values(const RTreeNode<T, Scalar, MaxChildren>& node) noexcept {
    if (node.is_leaf()) {
        return node.count();
    }

    std::size_t total = 0;
    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        total += count_values(*entry.child);
    }
    return total;
}

/// @brief Removes one leaf entry matching `entry_bounds` and `predicate`.
///
/// After removal, underfull non-root nodes are detached, their remaining leaf
/// entries are reinserted, and a root with a single child is collapsed. This is
/// the classic R-tree CondenseTree deletion flow.
///
/// Exception safety: basic guarantee, provided `T` is nothrow-move-
/// constructible. If an allocation fails while condensing or reinserting, the
/// tree is left structurally valid (any aborted-split overflow is repaired and
/// root bounds are recomputed) but entries detached for reinsertion may be
/// lost; callers caching an entry count must resynchronize via `count_values`.
template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize, typename Predicate>
RTreeDeleteResult<RTreeNode<T, Scalar, MaxChildren>> erase(
    RTreeNode<T, Scalar, MaxChildren>& root,
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool,
    BoundingBox<Scalar> entry_bounds,
    Predicate&& predicate) {
    TALUS_ASSERT(entry_bounds.is_valid());

    using node_type = RTreeNode<T, Scalar, MaxChildren>;
    using entry_type = typename node_type::value_entry_type;

    static_assert(node_type::can_relocate_value_entries,
        "erase requires move-constructible value entries");

    auto located = delete_detail::find_leaf_entry(root, entry_bounds, predicate);
    if (located.leaf == nullptr) {
        return {&root, false, 0, 0};
    }

    node_type* node = located.leaf;
    node->remove_at(located.index);

    std::vector<entry_type> orphaned_entries;
    std::size_t condensed_nodes = 0;
    std::size_t reinserted = 0;

    try {
        while (node != &root) {
            node_type* parent = node->parent();
            TALUS_ASSERT(parent != nullptr);

            if (node->underfull()) {
                const std::size_t node_index = find_child_index(*parent, node);
                parent->remove_at(node_index);
                delete_detail::collect_subtree_entries(*node, pool, orphaned_entries);
                ++condensed_nodes;
                node = parent;
                continue;
            }

            const std::size_t node_index = find_child_index(*parent, node);
            parent->update_bounds(node_index, node->bounds());
            node = parent;
        }

        root.recompute_bounds();
        [[maybe_unused]] const bool collapsed_before_reinsert =
            delete_detail::collapse_root_if_needed(root, pool);
        reinserted = delete_detail::reinsert_entries(root, pool, orphaned_entries);
        [[maybe_unused]] const bool collapsed_after_reinsert =
            delete_detail::collapse_root_if_needed(root, pool);
        root.recompute_bounds();
    } catch (...) {
        delete_detail::drop_overflow_entries(root);
        root.recompute_bounds();
        throw;
    }

    return {&root, true, condensed_nodes, reinserted};
}

namespace visit_detail {

/// Invokes `visitor` on a matched value and normalizes the result: a
/// void-returning visitor always continues the traversal, while a visitor
/// returning something convertible to bool requests a stop by returning false.
template<typename Visitor, typename T>
bool visit_value(Visitor& visitor, const T& value) {
    if constexpr (std::is_void_v<std::invoke_result_t<Visitor&, const T&>>) {
        visitor(value);
        return true;
    } else {
        return static_cast<bool>(visitor(value));
    }
}

} // namespace visit_detail

namespace search_detail {

// Returns false when the visitor requested an early stop, true otherwise.
// `matches` counts every visited value, including the one that stopped.
template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
bool search_impl(
    const RTreeNode<T, Scalar, MaxChildren>& node,
    BoundingBox<Scalar> query_bounds,
    Visitor& visitor,
    std::size_t& matches) {
    if (node.empty() || !node.bounds().intersects(query_bounds)) {
        return true;
    }

    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            if (entry.bounds.intersects(query_bounds)) {
                ++matches;
                if (!visit_detail::visit_value(visitor, entry.value())) {
                    return false;
                }
            }
        }
        return true;
    }

    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        if (entry.bounds.intersects(query_bounds)
            && !search_impl(*entry.child, query_bounds, visitor, matches)) {
            return false;
        }
    }
    return true;
}

} // namespace search_detail

/// @brief Visits each value whose stored bounds intersect `query_bounds`.
///
/// Returns the number of visited leaf entries. Traversal prunes any subtree
/// whose stored bounds do not intersect the query rectangle. Boundary-touching
/// boxes are considered matches, matching `BoundingBox::intersects`.
///
/// The visitor either returns void (every match is visited) or a type
/// convertible to bool — returning false stops the traversal early, and the
/// value that requested the stop is included in the returned count.
template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
std::size_t search(
    const RTreeNode<T, Scalar, MaxChildren>& root,
    BoundingBox<Scalar> query_bounds,
    Visitor&& visitor) {
    TALUS_ASSERT(query_bounds.is_valid());

    std::size_t matches = 0;
    search_detail::search_impl(root, query_bounds, visitor, matches);
    return matches;
}

namespace radius_search_detail {

// Returns false when the visitor requested an early stop, true otherwise.
// `matches` counts every visited value, including the one that stopped.
template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
bool radius_search_impl(
    const RTreeNode<T, Scalar, MaxChildren>& node,
    Point<Scalar> query,
    Scalar radius_sq,
    Visitor& visitor,
    std::size_t& matches) {
    if (node.empty() || node.bounds().min_sq_distance(query) > radius_sq) {
        return true;
    }

    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            if (entry.bounds.min_sq_distance(query) <= radius_sq) {
                ++matches;
                if (!visit_detail::visit_value(visitor, entry.value())) {
                    return false;
                }
            }
        }
        return true;
    }

    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        if (entry.bounds.min_sq_distance(query) <= radius_sq
            && !radius_search_impl(*entry.child, query, radius_sq, visitor, matches)) {
            return false;
        }
    }
    return true;
}

} // namespace radius_search_detail

/// @brief Visits each value whose stored bounds are within `radius` of `query`.
///
/// Distance is measured from the query point to each stored bounding box using
/// squared Euclidean distance. Values whose bounds contain the query point have
/// distance zero, and values exactly on the radius boundary are included.
///
/// The visitor either returns void (every match is visited) or a type
/// convertible to bool — returning false stops the traversal early, and the
/// value that requested the stop is included in the returned count.
template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
std::size_t radius_search(
    const RTreeNode<T, Scalar, MaxChildren>& root,
    Point<Scalar> query,
    Scalar radius,
    Visitor&& visitor) {
    TALUS_ASSERT(radius >= Scalar{0});

    const Scalar radius_sq = radius * radius;
    std::size_t matches = 0;
    radius_search_detail::radius_search_impl(root, query, radius_sq, visitor, matches);
    return matches;
}

namespace nearest_detail {

template<typename T, typename Scalar, std::size_t MaxChildren>
struct NearestState {
    const T* value = nullptr;
    Scalar sq_distance = std::numeric_limits<Scalar>::infinity();
};

template<typename T, typename Scalar, std::size_t MaxChildren>
void nearest_impl(
    const RTreeNode<T, Scalar, MaxChildren>& node,
    Point<Scalar> query,
    NearestState<T, Scalar, MaxChildren>& best) {
    if (node.empty() || node.bounds().min_sq_distance(query) > best.sq_distance) {
        return;
    }

    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            const Scalar sq_distance = entry.bounds.min_sq_distance(query);
            if (sq_distance < best.sq_distance) {
                best.value = &entry.value();
                best.sq_distance = sq_distance;
            }
        }
        return;
    }

    struct Candidate {
        const RTreeNode<T, Scalar, MaxChildren>* child = nullptr;
        Scalar sq_distance = Scalar{};
    };

    std::vector<Candidate> candidates;
    candidates.reserve(node.count());
    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        const Scalar sq_distance = entry.bounds.min_sq_distance(query);
        if (sq_distance <= best.sq_distance) {
            candidates.push_back({entry.child, sq_distance});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.sq_distance < rhs.sq_distance;
        });

    for (const Candidate& candidate : candidates) {
        if (candidate.sq_distance > best.sq_distance) {
            break;
        }
        nearest_impl(*candidate.child, query, best);
    }
}

template<typename T, typename Scalar>
struct KNearestCandidate {
    const T* value = nullptr;
    Scalar sq_distance = Scalar{};
};

/// Max-heap order on squared distance, so the heap front is the current
/// worst (farthest) of the k best candidates and is the one evicted first.
template<typename T, typename Scalar>
[[nodiscard]] constexpr bool farther_first(
    const KNearestCandidate<T, Scalar>& lhs,
    const KNearestCandidate<T, Scalar>& rhs) noexcept {
    return lhs.sq_distance < rhs.sq_distance;
}

template<typename T, typename Scalar>
[[nodiscard]] Scalar prune_distance(
    const std::vector<KNearestCandidate<T, Scalar>>& best,
    std::size_t k) noexcept {
    return best.size() == k
        ? best.front().sq_distance
        : std::numeric_limits<Scalar>::infinity();
}

template<typename T, typename Scalar, std::size_t MaxChildren>
void k_nearest_impl(
    const RTreeNode<T, Scalar, MaxChildren>& node,
    Point<Scalar> query,
    std::size_t k,
    std::vector<KNearestCandidate<T, Scalar>>& best) {
    if (node.empty() || node.bounds().min_sq_distance(query) > prune_distance(best, k)) {
        return;
    }

    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            const Scalar sq_distance = entry.bounds.min_sq_distance(query);
            if (best.size() < k) {
                best.push_back({&entry.value(), sq_distance});
                std::push_heap(best.begin(), best.end(), farther_first<T, Scalar>);
            } else if (sq_distance < best.front().sq_distance) {
                std::pop_heap(best.begin(), best.end(), farther_first<T, Scalar>);
                best.back() = {&entry.value(), sq_distance};
                std::push_heap(best.begin(), best.end(), farther_first<T, Scalar>);
            }
        }
        return;
    }

    struct Candidate {
        const RTreeNode<T, Scalar, MaxChildren>* child = nullptr;
        Scalar sq_distance = Scalar{};
    };

    std::vector<Candidate> candidates;
    candidates.reserve(node.count());
    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        const Scalar sq_distance = entry.bounds.min_sq_distance(query);
        if (sq_distance <= prune_distance(best, k)) {
            candidates.push_back({entry.child, sq_distance});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.sq_distance < rhs.sq_distance;
        });

    for (const Candidate& candidate : candidates) {
        // Recomputed each pass: recursion may have filled the heap or
        // tightened its worst distance since the candidate list was built.
        if (candidate.sq_distance > prune_distance(best, k)) {
            break;
        }
        k_nearest_impl(*candidate.child, query, k, best);
    }
}

} // namespace nearest_detail

/// @brief Returns the stored value nearest to `query`, or null when the tree is empty.
///
/// Distance is measured from the query point to each stored entry's bounding box
/// using squared Euclidean distance. A point inside a stored box therefore has
/// distance zero. Traversal visits child boxes in increasing minimum-distance
/// order and prunes subtrees that cannot improve the current best result.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] const T* nearest_neighbor(
    const RTreeNode<T, Scalar, MaxChildren>& root,
    Point<Scalar> query) {
    nearest_detail::NearestState<T, Scalar, MaxChildren> best;
    nearest_detail::nearest_impl(root, query, best);
    return best.value;
}

/// @brief Returns pointers to the `k` stored values nearest to `query`.
///
/// Distance is measured from the query point to each stored entry's bounding
/// box using squared Euclidean distance, so a point inside a stored box has
/// distance zero. Results are sorted by ascending distance; the order of
/// equidistant values is unspecified, as is which equidistant values are kept
/// when more than `k` entries tie at the k-th distance. Fewer than `k`
/// pointers are returned when the tree holds fewer than `k` entries.
/// Traversal visits child boxes in increasing minimum-distance order and
/// prunes subtrees that cannot beat the current k-th best distance.
template<typename T, typename Scalar, std::size_t MaxChildren>
[[nodiscard]] std::vector<const T*> k_nearest_neighbors(
    const RTreeNode<T, Scalar, MaxChildren>& root,
    Point<Scalar> query,
    std::size_t k) {
    std::vector<const T*> result;
    if (k == 0) {
        return result;
    }

    // Not reserved to `k` up front: the heap only ever holds
    // min(k, entry count) candidates, and `k` may be huge (e.g. SIZE_MAX
    // to request "all values by distance").
    std::vector<nearest_detail::KNearestCandidate<T, Scalar>> best;
    nearest_detail::k_nearest_impl(root, query, k, best);

    // `best` is a max-heap on squared distance; sort_heap leaves it ascending.
    std::sort_heap(best.begin(), best.end(), nearest_detail::farther_first<T, Scalar>);

    result.reserve(best.size());
    for (const auto& candidate : best) {
        result.push_back(candidate.value);
    }
    return result;
}

namespace bulk_load_detail {

/// @brief Returns `value / divisor` rounded up.
[[nodiscard]] constexpr std::size_t ceil_div(std::size_t value, std::size_t divisor) noexcept {
    return value / divisor + (value % divisor == 0 ? 0 : 1);
}

/// @brief Sorts an index segment by entry-bounds center along one axis.
///
/// Ties fall back to the other axis's center, then to the index itself, so the
/// resulting order is deterministic even for duplicate geometry.
template<typename Scalar, typename BoundsAt>
void sort_segment_by_center(
    std::vector<std::size_t>& order,
    std::size_t first,
    std::size_t count,
    BoundsAt& bounds_at,
    bool by_x) {
    const auto begin = order.begin() + static_cast<std::ptrdiff_t>(first);
    std::sort(begin, begin + static_cast<std::ptrdiff_t>(count),
        [&](std::size_t lhs, std::size_t rhs) {
            const Point<Scalar> left = bounds_at(lhs).center();
            const Point<Scalar> right = bounds_at(rhs).center();
            const Scalar left_primary = by_x ? left.x : left.y;
            const Scalar right_primary = by_x ? right.x : right.y;
            if (left_primary != right_primary) {
                return left_primary < right_primary;
            }

            const Scalar left_secondary = by_x ? left.y : left.x;
            const Scalar right_secondary = by_x ? right.y : right.x;
            if (left_secondary != right_secondary) {
                return left_secondary < right_secondary;
            }

            return lhs < rhs;
        });
}

/// @brief Produces the STR packing order for one tree level.
///
/// Sorts all entries by x-center, partitions them into `ceil(sqrt(group_count))`
/// vertical slices of `slice_count * capacity` entries, and sorts each slice by
/// y-center. Consecutive runs of `capacity` indices in the returned order form
/// one node's entries.
template<typename Scalar, typename BoundsAt>
[[nodiscard]] std::vector<std::size_t> tile_order(
    std::size_t count,
    BoundsAt&& bounds_at,
    std::size_t capacity) {
    TALUS_ASSERT(count > 0);
    TALUS_ASSERT(capacity > 0);

    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    sort_segment_by_center<Scalar>(order, 0, count, bounds_at, true);

    const std::size_t group_count = ceil_div(count, capacity);
    std::size_t slice_count =
        static_cast<std::size_t>(std::sqrt(static_cast<double>(group_count)));
    while (slice_count * slice_count < group_count) {
        ++slice_count;
    }

    const std::size_t slice_size = slice_count * capacity;
    for (std::size_t first = 0; first < count; first += slice_size) {
        sort_segment_by_center<Scalar>(
            order, first, std::min(slice_size, count - first), bounds_at, false);
    }

    return order;
}

/// @brief Returns per-node entry counts for packing one tree level.
///
/// Every group takes `capacity` entries except possibly the last. When the
/// final remainder would leave a node below `min_fill` (and the level has more
/// than one node), the deficit is borrowed from the preceding group so every
/// non-root node satisfies the minimum-fill invariant. `capacity >= 2 *
/// min_fill` guarantees the donor group stays at or above `min_fill`.
[[nodiscard]] inline std::vector<std::size_t> group_sizes(
    std::size_t count,
    std::size_t capacity,
    std::size_t min_fill) {
    TALUS_ASSERT(count > 0);
    TALUS_ASSERT(capacity >= 2 * min_fill);

    std::vector<std::size_t> sizes(count / capacity, capacity);
    const std::size_t remainder = count % capacity;
    if (remainder > 0) {
        sizes.push_back(remainder);
    }

    if (sizes.size() > 1 && sizes.back() < min_fill) {
        const std::size_t deficit = min_fill - sizes.back();
        sizes[sizes.size() - 2] -= deficit;
        sizes.back() += deficit;
    }

    return sizes;
}

} // namespace bulk_load_detail

/// @brief Builds a packed R-tree from leaf entries with Sort-Tile-Recursive loading.
///
/// Packs the entries into leaves slice-by-slice (sorted by x-center, then
/// y-center within each vertical slice), then repeats the same tiling over each
/// finished level's node bounds until a single root remains. Every non-root
/// node holds between `min_children` and `MaxChildren` entries and all leaves
/// sit at the same depth. Returns the new root, whose parent pointer is null.
///
/// All required nodes are reserved in `pool` up front, so for entries with
/// nothrow-move values the build itself cannot fail once reservation succeeds.
/// If an exception does escape (reservation failure, or a throwing value
/// move), already-built nodes are left allocated in `pool`; callers owning the
/// pool should reset it.
template<typename T, typename Scalar, std::size_t MaxChildren, std::size_t BlockSize>
[[nodiscard]] RTreeNode<T, Scalar, MaxChildren>* str_bulk_load(
    PoolAllocator<RTreeNode<T, Scalar, MaxChildren>, BlockSize>& pool,
    std::vector<typename RTreeNode<T, Scalar, MaxChildren>::value_entry_type> entries) {
    using node_type = RTreeNode<T, Scalar, MaxChildren>;

    static_assert(node_type::can_relocate_value_entries,
        "str_bulk_load requires move-constructible value entries");
    TALUS_ASSERT(!entries.empty());

    // Reserve every node the build will create so the packing loops below
    // cannot run out of pool capacity partway through.
    std::size_t total_nodes = 0;
    for (std::size_t level_count = entries.size();;) {
        level_count = bulk_load_detail::ceil_div(level_count, MaxChildren);
        total_nodes += level_count;
        if (level_count == 1) {
            break;
        }
    }
    pool.reserve(pool.size() + total_nodes);

    const auto leaf_order = bulk_load_detail::tile_order<Scalar>(
        entries.size(),
        [&](std::size_t i) { return entries[i].bounds; },
        MaxChildren);
    const auto leaf_sizes = bulk_load_detail::group_sizes(
        entries.size(), MaxChildren, node_type::min_children);

    std::vector<node_type*> level;
    level.reserve(leaf_sizes.size());
    std::size_t cursor = 0;
    for (const std::size_t size : leaf_sizes) {
        node_type* leaf = pool.create(true);
        for (std::size_t i = 0; i < size; ++i) {
            leaf->append_value_entry(std::move(entries[leaf_order[cursor + i]]));
        }
        level.push_back(leaf);
        cursor += size;
    }

    while (level.size() > 1) {
        const auto order = bulk_load_detail::tile_order<Scalar>(
            level.size(),
            [&](std::size_t i) { return level[i]->bounds(); },
            MaxChildren);
        const auto sizes = bulk_load_detail::group_sizes(
            level.size(), MaxChildren, node_type::min_children);

        std::vector<node_type*> parents;
        parents.reserve(sizes.size());
        cursor = 0;
        for (const std::size_t size : sizes) {
            node_type* parent = pool.create(false);
            for (std::size_t i = 0; i < size; ++i) {
                node_type* child = level[order[cursor + i]];
                parent->append_child(child->bounds(), child);
            }
            parents.push_back(parent);
            cursor += size;
        }
        level = std::move(parents);
    }

    TALUS_ASSERT(level.front()->parent() == nullptr);
    return level.front();
}

} // namespace talus::detail

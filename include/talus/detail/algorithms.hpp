#pragma once

/// @file detail/algorithms.hpp
/// @brief Core R-tree algorithms operating on RTreeNode storage.
///
/// This header contains the tree logic layered above `node.hpp`. The public
/// `SpatialIndex` wrapper owns roots and pools; these helpers operate on nodes
/// that have already been allocated by that wrapper or by tests.

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <tuple>
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

namespace search_detail {

template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
std::size_t search_impl(
    const RTreeNode<T, Scalar, MaxChildren>& node,
    BoundingBox<Scalar> query_bounds,
    Visitor& visitor) {
    if (node.empty() || !node.bounds().intersects(query_bounds)) {
        return 0;
    }

    std::size_t matches = 0;
    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            if (entry.bounds.intersects(query_bounds)) {
                visitor(entry.value());
                ++matches;
            }
        }
        return matches;
    }

    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        if (entry.bounds.intersects(query_bounds)) {
            matches += search_impl(*entry.child, query_bounds, visitor);
        }
    }
    return matches;
}

} // namespace search_detail

/// @brief Visits each value whose stored bounds intersect `query_bounds`.
///
/// Returns the number of matching leaf entries. Traversal prunes any subtree
/// whose stored bounds do not intersect the query rectangle. Boundary-touching
/// boxes are considered matches, matching `BoundingBox::intersects`.
template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
std::size_t search(
    const RTreeNode<T, Scalar, MaxChildren>& root,
    BoundingBox<Scalar> query_bounds,
    Visitor&& visitor) {
    TALUS_ASSERT(query_bounds.is_valid());

    return search_detail::search_impl(root, query_bounds, visitor);
}

namespace radius_search_detail {

template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
std::size_t radius_search_impl(
    const RTreeNode<T, Scalar, MaxChildren>& node,
    Point<Scalar> query,
    Scalar radius_sq,
    Visitor& visitor) {
    if (node.empty() || node.bounds().min_sq_distance(query) > radius_sq) {
        return 0;
    }

    std::size_t matches = 0;
    if (node.is_leaf()) {
        for (const auto& entry : node.values()) {
            if (entry.bounds.min_sq_distance(query) <= radius_sq) {
                visitor(entry.value());
                ++matches;
            }
        }
        return matches;
    }

    for (const auto& entry : node.children()) {
        TALUS_ASSERT(entry.child != nullptr);
        if (entry.bounds.min_sq_distance(query) <= radius_sq) {
            matches += radius_search_impl(*entry.child, query, radius_sq, visitor);
        }
    }
    return matches;
}

} // namespace radius_search_detail

/// @brief Visits each value whose stored bounds are within `radius` of `query`.
///
/// Distance is measured from the query point to each stored bounding box using
/// squared Euclidean distance. Values whose bounds contain the query point have
/// distance zero, and values exactly on the radius boundary are included.
template<typename T, typename Scalar, std::size_t MaxChildren, typename Visitor>
std::size_t radius_search(
    const RTreeNode<T, Scalar, MaxChildren>& root,
    Point<Scalar> query,
    Scalar radius,
    Visitor&& visitor) {
    TALUS_ASSERT(radius >= Scalar{0});

    const Scalar radius_sq = radius * radius;
    return radius_search_detail::radius_search_impl(root, query, radius_sq, visitor);
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

} // namespace talus::detail

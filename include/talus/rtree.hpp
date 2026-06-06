#pragma once

/// @file rtree.hpp
/// @brief Public R*-tree spatial index wrapper.
///
/// `SpatialIndex` owns the R-tree root and node pool, exposing the narrow
/// Phase 5 public API over the lower-level algorithms in `detail/`.

#include <cstddef>
#include <concepts>
#include <type_traits>
#include <utility>
#include <vector>

#include "concepts.hpp"
#include "detail/algorithms.hpp"
#include "detail/assert.hpp"
#include "detail/node.hpp"
#include "detail/pool_alloc.hpp"
#include "geometry.hpp"

namespace talus {

/// @brief R*-tree backed spatial index for automatically indexable values.
///
/// Values are stored by value. Bounds are extracted at insertion time using
/// `bounding_box_of<Scalar>()`, so structs with `.x/.y`, `.lat/.lon`, or
/// `.bounds()` are accepted without adapter code.
template<typename T, typename Scalar = double, std::size_t MaxChildren = 9>
    requires Indexable<T, Scalar>
class SpatialIndex {
public:
    /// User value type stored by the index.
    using value_type = T;

    /// Coordinate scalar used by the index.
    using scalar_type = Scalar;

    /// Bounding box type accepted by rectangular queries.
    using bounds_type = BoundingBox<Scalar>;

    /// Maximum node fanout before a split is required.
    static constexpr std::size_t max_children = MaxChildren;

    /// @brief Constructs an empty index.
    SpatialIndex() = default;

    /// Indexes own address-stable tree nodes and cannot be copied.
    SpatialIndex(const SpatialIndex&) = delete;

    /// Indexes own address-stable tree nodes and cannot be copied.
    SpatialIndex& operator=(const SpatialIndex&) = delete;

    /// Indexes own address-stable tree nodes and cannot be moved.
    SpatialIndex(SpatialIndex&&) = delete;

    /// Indexes own address-stable tree nodes and cannot be moved.
    SpatialIndex& operator=(SpatialIndex&&) = delete;

    /// @brief Inserts a copy of `value` into the index.
    void insert(const T& value)
        requires std::copy_constructible<T> {
        insert_impl(value, value);
    }

    /// @brief Inserts `value` into the index by move.
    void insert(T&& value) {
        const bounds_type bounds = bounding_box_of<Scalar>(value);
        insert_with_bounds(bounds, std::move(value));
    }

    /// @brief Returns the number of stored values.
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    /// @brief Returns true when the index contains no values.
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    /// @brief Removes every value while retaining already allocated node blocks.
    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        root_.reset_as_leaf();
        pool_.reset();
        size_ = 0;
    }

    /// @brief Returns copies of all values whose bounds intersect `query_bounds`.
    ///
    /// Boundary-touching boxes are included, matching `BoundingBox::intersects`.
    [[nodiscard]] std::vector<T> search(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        TALUS_ASSERT(query_bounds.is_valid());

        std::vector<T> matches;
        detail::search(root_, query_bounds, [&](const T& value) {
            matches.push_back(value);
        });
        return matches;
    }

    /// @brief Alias for rectangular search, matching the documented query name.
    [[nodiscard]] std::vector<T> within(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        return search(query_bounds);
    }

private:
    using node_type = detail::RTreeNode<T, Scalar, MaxChildren>;
    using pool_type = detail::PoolAllocator<node_type>;

    template<typename U>
    void insert_impl(const T& bounds_source, U&& value) {
        const bounds_type bounds = bounding_box_of<Scalar>(bounds_source);
        insert_with_bounds(bounds, std::forward<U>(value));
    }

    template<typename U>
    void insert_with_bounds(bounds_type bounds, U&& value) {
        TALUS_ASSERT(bounds.is_valid());

        const auto result = detail::insert_with_split(
            root_,
            pool_,
            bounds,
            std::forward<U>(value));
        TALUS_ASSERT(result.inserted);
        if (result.inserted) {
            ++size_;
        }
    }

    pool_type pool_{};
    node_type root_{};
    std::size_t size_ = 0;
};

} // namespace talus

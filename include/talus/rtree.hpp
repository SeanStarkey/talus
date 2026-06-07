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

    /// Copying would require deep-copying the whole tree (and a copyable `T`);
    /// the index is move-only.
    SpatialIndex(const SpatialIndex&) = delete;

    /// Copying would require deep-copying the whole tree (and a copyable `T`);
    /// the index is move-only.
    SpatialIndex& operator=(const SpatialIndex&) = delete;

    /// @brief Move-constructs by taking the other index's pool and root.
    ///
    /// Node storage is owned by the pool and stays address-stable across the
    /// move, so every parent/child pointer (the root included) remains valid.
    /// The moved-from index is left empty and reusable.
    SpatialIndex(SpatialIndex&& other) noexcept
        : pool_(std::move(other.pool_)),
          root_(other.root_),
          size_(other.size_) {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    /// @brief Move-assigns, releasing this index's nodes and taking `other`'s.
    SpatialIndex& operator=(SpatialIndex&& other) noexcept(std::is_nothrow_destructible_v<T>) {
        if (this != &other) {
            pool_ = std::move(other.pool_);
            root_ = other.root_;
            size_ = other.size_;
            other.root_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

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
        pool_.reset();   // destroys all nodes (the root included), keeps blocks
        root_ = nullptr; // re-created lazily on the next insert
        size_ = 0;
    }

    /// @brief Returns copies of all values whose bounds intersect `query_bounds`.
    ///
    /// Boundary-touching boxes are included, matching `BoundingBox::intersects`.
    [[nodiscard]] std::vector<T> search(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        TALUS_ASSERT(query_bounds.is_valid());

        std::vector<T> matches;
        if (root_ != nullptr) {
            detail::search(*root_, query_bounds, [&](const T& value) {
                matches.push_back(value);
            });
        }
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

        if (root_ == nullptr) {
            root_ = pool_.create();  // empty leaf root, allocated on first insert
        }

        const auto result = detail::insert_with_split(
            *root_,
            pool_,
            bounds,
            std::forward<U>(value));
        TALUS_ASSERT(result.inserted);
        root_ = result.root;  // stable today, but follow it in case the root changes
        if (result.inserted) {
            ++size_;
        }
    }

    pool_type pool_{};
    node_type* root_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace talus

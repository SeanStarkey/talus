#pragma once

/// @file rtree.hpp
/// @brief Public R*-tree spatial index wrapper.
///
/// `SpatialIndex` owns the R-tree root and node pool, exposing the narrow
/// Phase 5 public API over the lower-level algorithms in `detail/`.

#include <cstddef>
#include <concepts>
#include <cmath>
#include <optional>
#include <ranges>
#include <stdexcept>
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

/// @brief Thrown by `SpatialIndex` when given geometry with invalid bounds.
///
/// Bounds are "invalid" when a coordinate is NaN or infinite, or `min > max` on
/// an axis — that is, when `BoundingBox::is_valid()` is false. Derives from
/// `std::invalid_argument`, so it can also be caught as `std::invalid_argument`
/// or `std::exception`.
class invalid_geometry : public std::invalid_argument {
public:
    invalid_geometry()
        : std::invalid_argument(
              "talus: invalid geometry — coordinates must be finite with min <= max") {}
};

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
    ///
    /// @throws invalid_geometry if the value's extracted bounds are invalid
    /// (a NaN/infinite coordinate, or min > max). The index is left unchanged.
    void insert(const T& value)
        requires std::copy_constructible<T> {
        insert_impl(value, value);
    }

    /// @brief Inserts `value` into the index by move.
    ///
    /// @throws invalid_geometry if the value's extracted bounds are invalid. The
    /// index is unchanged and `value` is not moved from.
    void insert(T&& value) {
        const bounds_type bounds = bounding_box_of<Scalar>(value);
        insert_with_bounds(bounds, std::move(value));
    }

    /// @brief Bulk-loads copies of `values` into an empty index using STR packing.
    ///
    /// Builds the tree bottom-up with the Sort-Tile-Recursive algorithm, which
    /// is much faster than repeated insertion for large datasets and produces a
    /// well-packed tree. The index must be empty; callers replacing existing
    /// contents should call `clear()` first. Loading an empty range is a no-op.
    ///
    /// Every value's extracted bounds are validated before the tree is touched,
    /// so an `invalid_geometry` throw leaves the index unchanged. If building
    /// the tree itself fails (allocation failure, or a throwing copy/move of
    /// `T`), the index is reset to a valid empty state before the exception
    /// propagates.
    ///
    /// @throws std::logic_error if the index is not empty.
    /// @throws invalid_geometry if any value's extracted bounds are invalid
    /// (a NaN/infinite coordinate, or min > max).
    template<std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    void bulk_load(Range&& values) {
        if (!empty()) {
            throw std::logic_error("talus: bulk_load requires an empty index");
        }

        std::vector<typename node_type::value_entry_type> entries;
        if constexpr (std::ranges::sized_range<Range>) {
            entries.reserve(std::ranges::size(values));
        }
        for (auto&& value : values) {
            const bounds_type bounds = bounding_box_of<Scalar>(value);
            if (!bounds.is_valid()) {
                throw invalid_geometry{};
            }
            entries.emplace_back(bounds, std::forward<decltype(value)>(value));
        }

        build_from_entries(std::move(entries));
    }

    /// @brief Bulk-loads by moving values out of `values` using STR packing.
    ///
    /// Same contract as the range overload, but stored values are
    /// move-constructed from the vector's elements, so move-only types are
    /// supported.
    void bulk_load(std::vector<T>&& values) {
        if (!empty()) {
            throw std::logic_error("talus: bulk_load requires an empty index");
        }

        std::vector<typename node_type::value_entry_type> entries;
        entries.reserve(values.size());
        for (T& value : values) {
            const bounds_type bounds = bounding_box_of<Scalar>(value);
            if (!bounds.is_valid()) {
                throw invalid_geometry{};
            }
            entries.emplace_back(bounds, std::move(value));
        }

        build_from_entries(std::move(entries));
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

    /// @brief Removes one stored value equal to `value`.
    ///
    /// Returns true when a matching value was found and erased. A stored entry
    /// matches only when it compares equal to `value` AND its stored bounds are
    /// exactly equal (not merely overlapping) to the bounds extracted from
    /// `value`. Because bounds are re-extracted deterministically from the
    /// value, erasing a previously inserted value always satisfies the bounds
    /// requirement despite the floating-point comparison. Underfull nodes are
    /// condensed internally and their remaining entries are reinserted.
    ///
    /// Exception safety: basic guarantee, provided `T` is nothrow-move-
    /// constructible (large values stored boxed always are). If an allocation
    /// fails mid-condense, entries detached for reinsertion may be lost, but
    /// the index stays valid for further use and `size()` is resynchronized to
    /// the surviving entries before the exception propagates. If `T`'s move
    /// constructor throws, no guarantee is provided.
    ///
    /// @throws invalid_geometry if the value's extracted bounds are invalid.
    bool erase(const T& value)
        requires std::equality_comparable<T> {
        const bounds_type bounds = bounding_box_of<Scalar>(value);
        if (!bounds.is_valid()) {
            throw invalid_geometry{};
        }

        if (root_ == nullptr) {
            return false;
        }

        try {
            const auto result = detail::erase(
                *root_,
                pool_,
                bounds,
                [&](const T& stored) {
                    return stored == value;
                });
            root_ = result.root;
            if (result.erased) {
                --size_;
            }
            return result.erased;
        } catch (...) {
            size_ = detail::count_values(*root_);
            throw;
        }
    }

    /// @brief Returns copies of all values whose bounds intersect `query_bounds`.
    ///
    /// Boundary-touching boxes are included, matching `BoundingBox::intersects`.
    ///
    /// @throws invalid_geometry if `query_bounds` is invalid (a NaN/infinite
    /// coordinate, or min > max).
    [[nodiscard]] std::vector<T> search(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        if (!query_bounds.is_valid()) {
            throw invalid_geometry{};
        }

        std::vector<T> matches;
        if (root_ != nullptr) {
            detail::search(*root_, query_bounds, [&](const T& value) {
                matches.push_back(value);
            });
        }
        return matches;
    }

    /// @brief Visits each value whose bounds intersect `query_bounds`.
    ///
    /// The visitor is invoked with `const T&` for every match in unspecified
    /// order, without copying values — so it works with move-only `T` and lets
    /// callers filter on custom predicates or aggregate matches in place. A
    /// visitor returning void is invoked for every match; a visitor returning
    /// a type convertible to `bool` stops the traversal early by returning
    /// false. Returns the number of values visited, including the one that
    /// requested an early stop.
    ///
    /// Boundary-touching boxes are included, matching `BoundingBox::intersects`.
    ///
    /// @throws invalid_geometry if `query_bounds` is invalid (a NaN/infinite
    /// coordinate, or min > max).
    template<QueryVisitor<T> Visitor>
    std::size_t search(bounds_type query_bounds, Visitor&& visitor) const {
        if (!query_bounds.is_valid()) {
            throw invalid_geometry{};
        }

        if (root_ == nullptr) {
            return 0;
        }
        return detail::search(*root_, query_bounds, visitor);
    }

    /// @brief Alias for rectangular search, matching the documented query name.
    [[nodiscard]] std::vector<T> within(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        return search(query_bounds);
    }

    /// @brief Alias for visitor-based rectangular search.
    template<QueryVisitor<T> Visitor>
    std::size_t within(bounds_type query_bounds, Visitor&& visitor) const {
        return search(query_bounds, std::forward<Visitor>(visitor));
    }

    /// @brief Returns copies of all values whose bounds are within `radius` of `query`.
    ///
    /// Distance is measured from the query point to each stored value's bounds;
    /// bounded geometries containing the query point have distance zero. Values
    /// exactly on the radius boundary are included.
    ///
    /// @throws invalid_geometry if the query coordinates or radius are NaN or
    /// infinite, or if `radius` is negative.
    [[nodiscard]] std::vector<T> radius_search(Point<Scalar> query, Scalar radius) const
        requires std::copy_constructible<T> {
        if (!std::isfinite(query.x) || !std::isfinite(query.y)
            || !std::isfinite(radius) || radius < Scalar{0}) {
            throw invalid_geometry{};
        }

        std::vector<T> matches;
        if (root_ != nullptr) {
            detail::radius_search(*root_, query, radius, [&](const T& value) {
                matches.push_back(value);
            });
        }
        return matches;
    }

    /// @brief Visits each value whose bounds are within `radius` of `query`.
    ///
    /// Same matching rules as the vector-returning overload, with the visitor
    /// semantics of the rectangular visitor `search`: matches are visited in
    /// unspecified order without copying, a bool-returning visitor stops the
    /// traversal by returning false, and the returned count includes every
    /// visited value.
    ///
    /// @throws invalid_geometry if the query coordinates or radius are NaN or
    /// infinite, or if `radius` is negative.
    template<QueryVisitor<T> Visitor>
    std::size_t radius_search(Point<Scalar> query, Scalar radius, Visitor&& visitor) const {
        if (!std::isfinite(query.x) || !std::isfinite(query.y)
            || !std::isfinite(radius) || radius < Scalar{0}) {
            throw invalid_geometry{};
        }

        if (root_ == nullptr) {
            return 0;
        }
        return detail::radius_search(*root_, query, radius, visitor);
    }

    /// @brief Returns the value whose bounds are nearest to `query`, if any.
    ///
    /// Distance is measured from the query point to each stored value's bounds;
    /// bounded geometries containing the query point have distance zero.
    ///
    /// @throws invalid_geometry if either query coordinate is NaN or infinite.
    [[nodiscard]] std::optional<T> nearest_neighbor(Point<Scalar> query) const
        requires std::copy_constructible<T> {
        if (!std::isfinite(query.x) || !std::isfinite(query.y)) {
            throw invalid_geometry{};
        }

        if (root_ == nullptr) {
            return std::nullopt;
        }

        const T* nearest = detail::nearest_neighbor(*root_, query);
        if (nearest == nullptr) {
            return std::nullopt;
        }
        return *nearest;
    }

    /// @brief Returns copies of the `k` values whose bounds are nearest to `query`.
    ///
    /// Distance is measured from the query point to each stored value's bounds;
    /// bounded geometries containing the query point have distance zero. Results
    /// are sorted by ascending distance. The order of equidistant values is
    /// unspecified, as is which equidistant values are kept when more than `k`
    /// entries tie at the k-th distance. Fewer than `k` values are returned when
    /// the index holds fewer than `k`; `k == 0` returns an empty vector.
    ///
    /// @throws invalid_geometry if either query coordinate is NaN or infinite.
    [[nodiscard]] std::vector<T> nearest_neighbors(Point<Scalar> query, std::size_t k) const
        requires std::copy_constructible<T> {
        if (!std::isfinite(query.x) || !std::isfinite(query.y)) {
            throw invalid_geometry{};
        }

        std::vector<T> matches;
        if (root_ == nullptr || k == 0) {
            return matches;
        }

        const std::vector<const T*> nearest = detail::k_nearest_neighbors(*root_, query, k);
        matches.reserve(nearest.size());
        for (const T* value : nearest) {
            matches.push_back(*value);
        }
        return matches;
    }

private:
    using node_type = detail::RTreeNode<T, Scalar, MaxChildren>;
    using pool_type = detail::PoolAllocator<node_type>;

    /// Hands validated leaf entries to the STR builder. The caller has already
    /// verified the index is empty, so on failure resetting back to the empty
    /// state (releasing any partially built nodes) preserves what the caller saw.
    void build_from_entries(std::vector<typename node_type::value_entry_type> entries) {
        if (entries.empty()) {
            return;
        }

        const std::size_t count = entries.size();
        pool_.reset();   // drop any stale empty root left behind by erase
        root_ = nullptr;

        try {
            root_ = detail::str_bulk_load(pool_, std::move(entries));
            size_ = count;
        } catch (...) {
            pool_.reset();
            root_ = nullptr;
            size_ = 0;
            throw;
        }
    }

    template<typename U>
    void insert_impl(const T& bounds_source, U&& value) {
        const bounds_type bounds = bounding_box_of<Scalar>(bounds_source);
        insert_with_bounds(bounds, std::forward<U>(value));
    }

    template<typename U>
    void insert_with_bounds(bounds_type bounds, U&& value) {
        if (!bounds.is_valid()) {
            throw invalid_geometry{};
        }

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

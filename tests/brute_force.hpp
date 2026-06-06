#pragma once

/// @file brute_force.hpp
/// @brief Linear-scan oracle for public spatial index tests.
///
/// The oracle intentionally mirrors the public rectangle-search behavior so
/// R-tree tests can compare results against a simple implementation.

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include <talus/concepts.hpp>
#include <talus/geometry.hpp>

namespace talus::test {

template<typename T, typename Scalar = double>
    requires Indexable<T, Scalar>
class BruteForceIndex {
public:
    using value_type = T;
    using scalar_type = Scalar;
    using bounds_type = BoundingBox<Scalar>;

    void insert(const T& value)
        requires std::copy_constructible<T> {
        values_.push_back(value);
    }

    void insert(T&& value) {
        values_.push_back(std::move(value));
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return values_.empty();
    }

    void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        values_.clear();
    }

    [[nodiscard]] std::vector<T> search(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        std::vector<T> matches;
        for (const T& value : values_) {
            if (bounding_box_of<Scalar>(value).intersects(query_bounds)) {
                matches.push_back(value);
            }
        }
        return matches;
    }

    [[nodiscard]] std::vector<T> within(bounds_type query_bounds) const
        requires std::copy_constructible<T> {
        return search(query_bounds);
    }

private:
    std::vector<T> values_{};
};

} // namespace talus::test

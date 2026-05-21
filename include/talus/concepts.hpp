#pragma once

/// @file concepts.hpp
/// @brief Type-detection concepts and coordinate extraction helpers.
///
/// This header defines the zero-boilerplate rules Talus uses to recognize
/// points, bounded geometries, and custom coordinate extractors. New spatial
/// index code should call `bounding_box_of()` instead of inspecting user types
/// directly.

#include <concepts>
#include "geometry.hpp"

namespace talus {

/// @brief Matches structs with `.x` and `.y` fields convertible to `double`.
template<typename T>
concept HasXY = requires(const T& t) {
    { t.x } -> std::convertible_to<double>;
    { t.y } -> std::convertible_to<double>;
};

/// @brief Matches structs with `.lat` and `.lon` fields convertible to `double`.
template<typename T>
concept HasLatLon = requires(const T& t) {
    { t.lat } -> std::convertible_to<double>;
    { t.lon } -> std::convertible_to<double>;
};

/// @brief Matches types exposing `.bounds()` convertible to `BoundingBox<Scalar>`.
template<typename T, typename Scalar = double>
concept HasBounds = requires(const T& t) {
    { t.bounds() } -> std::convertible_to<BoundingBox<Scalar>>;
};

/// @brief Matches point-like user types recognized without adapters.
template<typename T>
concept Pointlike = HasXY<T> || HasLatLon<T>;

/// @brief Matches types that can be indexed directly by Talus.
template<typename T, typename Scalar = double>
concept Indexable = Pointlike<T> || HasBounds<T, Scalar>;

/// @brief Matches callables that extract a bounding box from otherwise opaque types.
///
/// Use this when a type cannot satisfy `HasXY`, `HasLatLon`, or `HasBounds`
/// directly.
template<typename Extractor, typename T, typename Scalar = double>
concept CoordExtractor = requires(Extractor e, const T& t) {
    { e(t) } -> std::convertible_to<BoundingBox<Scalar>>;
};

// ── Coordinate extraction helpers ─────────────────────────────────────────────
// These are used internally so the tree never needs to special-case HasXY vs HasLatLon.

/// @brief Extracts a point-sized bounding box from `.x/.y` or `.lat/.lon` fields.
///
/// Geographic-style values use `lon` as x and `lat` as y.
template<typename Scalar = double, Pointlike T>
    requires (!HasBounds<T, Scalar>)
[[nodiscard]] constexpr BoundingBox<Scalar> bounding_box_of(const T& v) noexcept {
    if constexpr (HasXY<T>) {
        Scalar x = static_cast<Scalar>(v.x);
        Scalar y = static_cast<Scalar>(v.y);
        return {{x, y}, {x, y}};
    } else {
        // HasLatLon — treat lon as x, lat as y (standard geographic convention)
        Scalar x = static_cast<Scalar>(v.lon);
        Scalar y = static_cast<Scalar>(v.lat);
        return {{x, y}, {x, y}};
    }
}

/// @brief Extracts a bounding box by calling `.bounds()`.
///
/// This overload takes precedence when a type also has point-like fields.
template<typename Scalar = double, typename T>
    requires HasBounds<T, Scalar>
[[nodiscard]] constexpr BoundingBox<Scalar> bounding_box_of(const T& v) {
    return static_cast<BoundingBox<Scalar>>(v.bounds());
}

} // namespace talus

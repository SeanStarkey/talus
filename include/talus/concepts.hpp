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
///
/// Use this when code must work directly with `BoundingBox<Scalar>` values.
/// For coordinate extraction, prefer `bounding_box_of()` which handles
/// scalar mismatches automatically via `HasBoundsAny`.
template<typename T, typename Scalar = double>
concept HasBounds = requires(const T& t) {
    { t.bounds() } -> std::convertible_to<BoundingBox<Scalar>>;
};

/// @brief Matches types exposing `.bounds()` returning any BoundingBox specialisation.
///
/// Unlike `HasBounds<T, Scalar>`, this concept is scalar-agnostic: it matches
/// whenever `.bounds()` returns a `BoundingBox` regardless of its coordinate type.
/// `bounding_box_of()` uses this concept so that a type whose `.bounds()` returns
/// `BoundingBox<double>` can still be extracted with `bounding_box_of<float>()` —
/// the coordinate values are cast to the requested scalar.
template<typename T>
concept HasBoundsAny = requires(const T& t) {
    t.bounds().min.x;
    t.bounds().min.y;
    t.bounds().max.x;
    t.bounds().max.y;
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
/// Geographic-style values use `lon` as x and `lat` as y. This overload is
/// skipped whenever `.bounds()` is present — `HasBoundsAny` wins regardless of
/// whether the bounds scalar matches the requested Scalar.
template<typename Scalar = double, Pointlike T>
    requires (!HasBoundsAny<T>)
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

/// @brief Extracts a bounding box by calling `.bounds()`, casting to the requested Scalar.
///
/// This overload takes precedence over point-field extraction whenever `.bounds()` is
/// present, even when the bounds return type uses a different scalar. Coordinates are
/// cast explicitly so that e.g. `bounding_box_of<float>()` works on a type whose
/// `.bounds()` returns `BoundingBox<double>`.
template<typename Scalar = double, typename T>
    requires HasBoundsAny<T>
[[nodiscard]] constexpr BoundingBox<Scalar> bounding_box_of(const T& v) {
    auto b = v.bounds();
    return {
        {static_cast<Scalar>(b.min.x), static_cast<Scalar>(b.min.y)},
        {static_cast<Scalar>(b.max.x), static_cast<Scalar>(b.max.y)}
    };
}

} // namespace talus

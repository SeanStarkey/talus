#pragma once

#include <concepts>
#include "geometry.hpp"

namespace talus {

// Structs with .x and .y fields (the most common point representation).
template<typename T>
concept HasXY = requires(T t) {
    { t.x } -> std::convertible_to<double>;
    { t.y } -> std::convertible_to<double>;
};

// Structs with .lat and .lon fields (geographic-style naming).
template<typename T>
concept HasLatLon = requires(T t) {
    { t.lat } -> std::convertible_to<double>;
    { t.lon } -> std::convertible_to<double>;
};

// Anything that exposes a .bounds() returning a BoundingBox (e.g., Segment).
template<typename T>
concept HasBounds = requires(T t) {
    { t.bounds() } -> std::convertible_to<BoundingBox<double>>;
};

// A type that looks like a point — matched by field names, no boilerplate needed.
template<typename T>
concept Pointlike = HasXY<T> || HasLatLon<T>;

// A type that can be placed into the spatial index: either a point or a bounded geometry.
template<typename T>
concept Indexable = Pointlike<T> || HasBounds<T>;

// Escape hatch: a callable that extracts a BoundingBox<double> from T.
// Use when T doesn't satisfy any of the concepts above.
template<typename Extractor, typename T>
concept CoordExtractor = requires(Extractor e, const T& t) {
    { e(t) } -> std::convertible_to<BoundingBox<double>>;
};

// ── Coordinate extraction helpers ─────────────────────────────────────────────
// These are used internally so the tree never needs to special-case HasXY vs HasLatLon.

template<Pointlike T>
[[nodiscard]] constexpr BoundingBox<double> bounding_box_of(const T& v) noexcept {
    if constexpr (HasXY<T>) {
        double x = static_cast<double>(v.x);
        double y = static_cast<double>(v.y);
        return {{x, y}, {x, y}};
    } else {
        // HasLatLon — treat lon as x, lat as y (standard geographic convention)
        double x = static_cast<double>(v.lon);
        double y = static_cast<double>(v.lat);
        return {{x, y}, {x, y}};
    }
}

template<HasBounds T>
[[nodiscard]] constexpr BoundingBox<double> bounding_box_of(const T& v) {
    return static_cast<BoundingBox<double>>(v.bounds());
}

} // namespace talus

#pragma once

/// @file geometry.hpp
/// @brief Core constexpr-friendly geometry primitives for Talus indexes.
///
/// Provides `Point`, `BoundingBox`, `Segment`, and distance helpers used by the
/// R-tree, k-d tree, brute-force oracle, and user-facing geometry APIs. The
/// bounding-box operations here are the shared source of truth for containment,
/// intersection, expansion, and distance calculations.

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace talus {

// ── Point ─────────────────────────────────────────────────────────────────────

/// @brief Two-dimensional point using the selected scalar precision.
template<typename Scalar = double>
struct Point {
    /// Horizontal coordinate.
    Scalar x{};

    /// Vertical coordinate.
    Scalar y{};

    /// @brief Compares points by coordinate value.
    constexpr bool operator==(const Point&) const noexcept = default;
};

// ── BoundingBox ───────────────────────────────────────────────────────────────

/// @brief Axis-aligned bounding box described by minimum and maximum corners.
template<typename Scalar = double>
struct BoundingBox {
    static_assert(std::is_floating_point_v<Scalar>,
        "BoundingBox<Scalar>: Scalar must be a floating-point type; "
        "integer scalars cause signed overflow UB in area(), center(), and distance operations");
    /// Lower-left corner of the box.
    Point<Scalar> min{};

    /// Upper-right corner of the box.
    Point<Scalar> max{};

    /// @brief Returns true when `p` lies inside or on the boundary.
    [[nodiscard]] constexpr bool contains(Point<Scalar> p) const noexcept {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y;
    }

    /// @brief Returns true when two boxes overlap or touch at an edge.
    [[nodiscard]] constexpr bool intersects(BoundingBox other) const noexcept {
        return min.x <= other.max.x && max.x >= other.min.x
            && min.y <= other.max.y && max.y >= other.min.y;
    }

    /// @brief Returns the smallest box that encloses this box and `other`.
    [[nodiscard]] constexpr BoundingBox expand(BoundingBox other) const noexcept {
        return {
            {std::min(min.x, other.min.x), std::min(min.y, other.min.y)},
            {std::max(max.x, other.max.x), std::max(max.y, other.max.y)}
        };
    }

    /// @brief Returns the box area, or zero for empty or inverted boxes.
    [[nodiscard]] constexpr Scalar area() const noexcept {
        Scalar dx = max.x - min.x;
        Scalar dy = max.y - min.y;
        return (dx > Scalar{0} && dy > Scalar{0}) ? dx * dy : Scalar{0};
    }

    /// @brief Returns the area growth required to enclose `other`.
    [[nodiscard]] constexpr Scalar enlarged_area(BoundingBox other) const noexcept {
        return expand(other).area() - area();
    }

    /// @brief Returns the midpoint between the box corners.
    [[nodiscard]] constexpr Point<Scalar> center() const noexcept {
        return {(min.x + max.x) / Scalar{2}, (min.y + max.y) / Scalar{2}};
    }

    /// @brief Returns the minimum squared distance from `p` to this box.
    ///
    /// The result is zero when `p` is inside the box. Squared distance avoids a
    /// square root in nearest-neighbor comparisons.
    [[nodiscard]] constexpr Scalar min_sq_distance(Point<Scalar> p) const noexcept {
        auto clamp = [](Scalar v, Scalar lo, Scalar hi) constexpr noexcept {
            return v < lo ? lo : (v > hi ? hi : v);
        };
        Scalar cx = clamp(p.x, min.x, max.x);
        Scalar cy = clamp(p.y, min.y, max.y);
        Scalar dx = p.x - cx;
        Scalar dy = p.y - cy;
        return dx * dx + dy * dy;
    }

    /// @brief Compares boxes by corner value.
    constexpr bool operator==(const BoundingBox&) const noexcept = default;
};

// ── Segment ───────────────────────────────────────────────────────────────────

/// @brief Line segment represented by two endpoints.
template<typename Scalar = double>
struct Segment {
    /// First endpoint.
    Point<Scalar> start{};

    /// Second endpoint.
    Point<Scalar> end{};

    /// @brief Returns the smallest bounding box that encloses the segment.
    [[nodiscard]] constexpr BoundingBox<Scalar> bounds() const noexcept {
        return {
            {std::min(start.x, end.x), std::min(start.y, end.y)},
            {std::max(start.x, end.x), std::max(start.y, end.y)}
        };
    }

    /// @brief Compares segments by endpoint value.
    constexpr bool operator==(const Segment&) const noexcept = default;
};

// ── Free-function helpers ─────────────────────────────────────────────────────

/// @brief Returns squared Euclidean distance between two points.
template<typename Scalar>
[[nodiscard]] constexpr Scalar sq_distance(Point<Scalar> a, Point<Scalar> b) noexcept {
    static_assert(std::is_floating_point_v<Scalar>,
        "sq_distance: Scalar must be a floating-point type to avoid signed overflow UB");
    Scalar dx = a.x - b.x;
    Scalar dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/// @brief Returns Euclidean distance between two points.
template<typename Scalar>
[[nodiscard]] Scalar distance(Point<Scalar> a, Point<Scalar> b) {
    static_assert(std::is_floating_point_v<Scalar>,
        "distance: Scalar must be a floating-point type to avoid signed overflow UB");
    return std::sqrt(sq_distance(a, b));
}

} // namespace talus

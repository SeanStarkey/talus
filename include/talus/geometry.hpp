#pragma once

#include <algorithm>
#include <cmath>

namespace talus {

// ── Point ─────────────────────────────────────────────────────────────────────

template<typename Scalar = double>
struct Point {
    Scalar x{};
    Scalar y{};

    constexpr bool operator==(const Point&) const noexcept = default;
};

// ── BoundingBox ───────────────────────────────────────────────────────────────

template<typename Scalar = double>
struct BoundingBox {
    Point<Scalar> min{};
    Point<Scalar> max{};

    // True if p lies inside or on the boundary of this box.
    [[nodiscard]] constexpr bool contains(Point<Scalar> p) const noexcept {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y;
    }

    // True if the two boxes share any area (touching edges count).
    [[nodiscard]] constexpr bool intersects(BoundingBox other) const noexcept {
        return min.x <= other.max.x && max.x >= other.min.x
            && min.y <= other.max.y && max.y >= other.min.y;
    }

    // Smallest box that encloses both this box and other.
    [[nodiscard]] constexpr BoundingBox expand(BoundingBox other) const noexcept {
        return {
            {std::min(min.x, other.min.x), std::min(min.y, other.min.y)},
            {std::max(max.x, other.max.x), std::max(max.y, other.max.y)}
        };
    }

    [[nodiscard]] constexpr Scalar area() const noexcept {
        Scalar dx = max.x - min.x;
        Scalar dy = max.y - min.y;
        return (dx > Scalar{0} && dy > Scalar{0}) ? dx * dy : Scalar{0};
    }

    // How much the area would grow if we expanded to enclose other.
    [[nodiscard]] constexpr Scalar enlarged_area(BoundingBox other) const noexcept {
        return expand(other).area() - area();
    }

    [[nodiscard]] constexpr Point<Scalar> center() const noexcept {
        return {(min.x + max.x) / Scalar{2}, (min.y + max.y) / Scalar{2}};
    }

    // Minimum squared distance from point p to the nearest point on (or inside) this box.
    // Returns 0 when p is inside.
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

    constexpr bool operator==(const BoundingBox&) const noexcept = default;
};

// ── Segment ───────────────────────────────────────────────────────────────────

template<typename Scalar = double>
struct Segment {
    Point<Scalar> start{};
    Point<Scalar> end{};

    [[nodiscard]] constexpr BoundingBox<Scalar> bounds() const noexcept {
        return {
            {std::min(start.x, end.x), std::min(start.y, end.y)},
            {std::max(start.x, end.x), std::max(start.y, end.y)}
        };
    }

    constexpr bool operator==(const Segment&) const noexcept = default;
};

// ── Free-function helpers ─────────────────────────────────────────────────────

template<typename Scalar>
[[nodiscard]] constexpr Scalar sq_distance(Point<Scalar> a, Point<Scalar> b) noexcept {
    Scalar dx = a.x - b.x;
    Scalar dy = a.y - b.y;
    return dx * dx + dy * dy;
}

template<typename Scalar>
[[nodiscard]] Scalar distance(Point<Scalar> a, Point<Scalar> b) {
    return std::sqrt(sq_distance(a, b));
}

} // namespace talus

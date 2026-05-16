#include <talus/talus.hpp>

#include <cassert>
#include <cmath>

namespace {

struct XYPoint {
    float x;
    int y;
};

struct LatLonPoint {
    double lat;
    double lon;
};

struct BoundedObject {
    constexpr talus::BoundingBox<double> bounds() const noexcept {
        return {{-1.0, -2.0}, {3.0, 4.0}};
    }
};

struct BoundedFloatObject {
    constexpr talus::BoundingBox<float> bounds() const noexcept {
        return {{-1.0F, -2.0F}, {3.0F, 4.0F}};
    }
};

struct PointWithBounds {
    double x;
    double y;

    constexpr talus::BoundingBox<double> bounds() const noexcept {
        return {{-5.0, -6.0}, {7.0, 8.0}};
    }
};

struct CustomExtractor {
    constexpr talus::BoundingBox<double> operator()(const XYPoint& point) const noexcept {
        return {{point.x, static_cast<double>(point.y)}, {point.x, static_cast<double>(point.y)}};
    }
};

struct FloatExtractor {
    constexpr talus::BoundingBox<float> operator()(const XYPoint& point) const noexcept {
        return {{point.x, static_cast<float>(point.y)}, {point.x, static_cast<float>(point.y)}};
    }
};

static_assert(talus::HasXY<XYPoint>);
static_assert(!talus::HasLatLon<XYPoint>);
static_assert(talus::HasLatLon<LatLonPoint>);
static_assert(!talus::HasXY<LatLonPoint>);
static_assert(talus::HasBounds<BoundedObject>);
static_assert(talus::HasBounds<BoundedFloatObject, float>);
static_assert(talus::HasBounds<PointWithBounds>);
static_assert(talus::Pointlike<XYPoint>);
static_assert(talus::Pointlike<LatLonPoint>);
static_assert(talus::Pointlike<PointWithBounds>);
static_assert(talus::Indexable<XYPoint>);
static_assert(talus::Indexable<LatLonPoint>);
static_assert(talus::Indexable<BoundedObject>);
static_assert(talus::Indexable<BoundedFloatObject, float>);
static_assert(talus::CoordExtractor<CustomExtractor, XYPoint>);
static_assert(talus::CoordExtractor<FloatExtractor, XYPoint, float>);

void test_geometry() {
    using talus::BoundingBox;
    using talus::Point;
    using talus::Segment;

    // ── Point equality ────────────────────────────────────────────────────────
    constexpr Point<double> origin{0.0, 0.0};
    constexpr Point<double> same_origin{0.0, 0.0};
    static_assert(origin == same_origin);
    constexpr Point<double> other{1.0, 0.0};
    static_assert(!(origin == other));
    static_assert(origin != other);

    // ── Bounding-box containment ──────────────────────────────────────────────
    // box = [0,4] x [0,3]
    constexpr BoundingBox<double> box{{0.0, 0.0}, {4.0, 3.0}};
    // corners
    static_assert(box.contains({0.0, 0.0}));
    static_assert(box.contains({4.0, 3.0}));
    static_assert(box.contains({0.0, 3.0}));
    static_assert(box.contains({4.0, 0.0}));
    // interior
    static_assert(box.contains({2.0, 1.5}));
    // edge midpoints (non-corner)
    static_assert(box.contains({2.0, 0.0}));  // bottom edge
    static_assert(box.contains({2.0, 3.0}));  // top edge
    static_assert(box.contains({0.0, 1.5}));  // left edge
    static_assert(box.contains({4.0, 1.5}));  // right edge
    // outside each side
    static_assert(!box.contains({4.1, 1.5})); // right
    static_assert(!box.contains({-0.1, 1.5}));// left
    static_assert(!box.contains({2.0, -0.1}));// below
    static_assert(!box.contains({2.0, 3.1})); // above

    // ── Intersections (touching edges count) ─────────────────────────────────
    // touching right edge
    constexpr BoundingBox<double> touching{{4.0, 1.0}, {5.0, 2.0}};
    static_assert(box.intersects(touching));
    // touching top-right corner
    constexpr BoundingBox<double> corner_touch{{4.0, 3.0}, {6.0, 5.0}};
    static_assert(box.intersects(corner_touch));
    // partially overlapping
    constexpr BoundingBox<double> overlapping{{2.0, 1.0}, {6.0, 4.0}};
    static_assert(box.intersects(overlapping));
    // fully contained inside box
    constexpr BoundingBox<double> inner{{1.0, 1.0}, {3.0, 2.0}};
    static_assert(box.intersects(inner));
    // box itself
    static_assert(box.intersects(box));
    // disjoint (gap of 0.1)
    constexpr BoundingBox<double> disjoint{{4.1, 1.0}, {5.0, 2.0}};
    static_assert(!box.intersects(disjoint));
    // fully separate
    constexpr BoundingBox<double> separate{{10.0, 10.0}, {20.0, 20.0}};
    static_assert(!box.intersects(separate));

    // ── Expansion ────────────────────────────────────────────────────────────
    constexpr BoundingBox<double> expanded = box.expand({{-1.0, 1.0}, {2.0, 5.0}});
    static_assert(expanded == BoundingBox<double>{{-1.0, 0.0}, {4.0, 5.0}});
    // expand with identical box → unchanged
    static_assert(box.expand(box) == box);
    // expand with fully contained box → unchanged
    static_assert(box.expand(inner) == box);
    // expand with negative-coord box
    constexpr BoundingBox<double> neg_box{{-3.0, -3.0}, {-1.0, -1.0}};
    static_assert(box.expand(neg_box) == BoundingBox<double>{{-3.0, -3.0}, {4.0, 3.0}});

    // ── Area ─────────────────────────────────────────────────────────────────
    static_assert(box.area() == 12.0);
    constexpr BoundingBox<double> unit{{0.0, 0.0}, {1.0, 1.0}};
    static_assert(unit.area() == 1.0);
    // degenerate: line (zero height)
    constexpr BoundingBox<double> h_line{{0.0, 0.0}, {4.0, 0.0}};
    static_assert(h_line.area() == 0.0);
    // degenerate: point
    constexpr BoundingBox<double> pt_box{{2.0, 2.0}, {2.0, 2.0}};
    static_assert(pt_box.area() == 0.0);

    // ── Enlarged area ─────────────────────────────────────────────────────────
    static_assert(box.enlarged_area({{-1.0, 1.0}, {2.0, 5.0}}) == 13.0);
    // other already contained → no growth
    static_assert(box.enlarged_area(inner) == 0.0);
    // other identical → no growth
    static_assert(box.enlarged_area(box) == 0.0);

    // ── Center ───────────────────────────────────────────────────────────────
    static_assert(box.center() == Point<double>{2.0, 1.5});
    static_assert(unit.center() == Point<double>{0.5, 0.5});
    constexpr BoundingBox<double> sym_neg{{-2.0, -4.0}, {2.0, 4.0}};
    static_assert(sym_neg.center() == Point<double>{0.0, 0.0});

    // ── Minimum squared distance ──────────────────────────────────────────────
    // inside → 0
    static_assert(box.min_sq_distance({2.0, 1.0}) == 0.0);
    // on each edge → 0
    static_assert(box.min_sq_distance({0.0, 1.5}) == 0.0);  // left edge
    static_assert(box.min_sq_distance({4.0, 1.5}) == 0.0);  // right edge
    static_assert(box.min_sq_distance({2.0, 0.0}) == 0.0);  // bottom edge
    static_assert(box.min_sq_distance({2.0, 3.0}) == 0.0);  // top edge
    // outside along one axis only
    static_assert(box.min_sq_distance({5.0, 1.5}) == 1.0);  // 1 right of box
    static_assert(box.min_sq_distance({2.0, 5.0}) == 4.0);  // 2 above box
    // outside corner
    static_assert(box.min_sq_distance({5.0, 5.0}) == 5.0);  // dx=1, dy=2 → 1+4=5

    // ── Segment bounds ────────────────────────────────────────────────────────
    // diagonal (start > end for both axes)
    constexpr Segment<double> diag{{4.0, -2.0}, {-1.0, 3.0}};
    static_assert(diag.bounds() == BoundingBox<double>{{-1.0, -2.0}, {4.0, 3.0}});
    // horizontal
    constexpr Segment<double> horiz{{1.0, 2.0}, {5.0, 2.0}};
    static_assert(horiz.bounds() == BoundingBox<double>{{1.0, 2.0}, {5.0, 2.0}});
    // vertical
    constexpr Segment<double> vert{{3.0, -1.0}, {3.0, 4.0}};
    static_assert(vert.bounds() == BoundingBox<double>{{3.0, -1.0}, {3.0, 4.0}});
    // reversed direction (start has larger coords)
    constexpr Segment<double> rev{{5.0, 3.0}, {1.0, 1.0}};
    static_assert(rev.bounds() == BoundingBox<double>{{1.0, 1.0}, {5.0, 3.0}});

    // ── sq_distance / distance ────────────────────────────────────────────────
    static_assert(talus::sq_distance(Point<double>{0.0, 0.0}, Point<double>{3.0, 4.0}) == 25.0);
    assert(std::abs(talus::distance(Point<double>{0.0, 0.0}, Point<double>{3.0, 4.0}) - 5.0) < 1e-12);
}

void test_concepts() {
    // ── .x/.y extraction ─────────────────────────────────────────────────────
    // default scalar (double)
    constexpr auto xy_box = talus::bounding_box_of(XYPoint{1.5F, 2});
    static_assert(xy_box == talus::BoundingBox<double>{{1.5, 2.0}, {1.5, 2.0}});
    // result is a degenerate (point) box
    static_assert(xy_box.min == xy_box.max);
    // scalar-aware: float scalar
    constexpr auto xy_float_box = talus::bounding_box_of<float>(XYPoint{1.5F, 2});
    static_assert(xy_float_box == talus::BoundingBox<float>{{1.5F, 2.0F}, {1.5F, 2.0F}});
    // negative coordinates
    constexpr auto xy_neg_box = talus::bounding_box_of(XYPoint{-3.0F, -7});
    static_assert(xy_neg_box == talus::BoundingBox<double>{{-3.0, -7.0}, {-3.0, -7.0}});
    // zero coordinates
    constexpr auto xy_zero = talus::bounding_box_of(XYPoint{0.0F, 0});
    static_assert(xy_zero == talus::BoundingBox<double>{{0.0, 0.0}, {0.0, 0.0}});

    // ── .lat/.lon extraction (lon → x, lat → y) ───────────────────────────────
    constexpr auto lat_lon_box = talus::bounding_box_of(LatLonPoint{40.0, -105.0});
    static_assert(lat_lon_box == talus::BoundingBox<double>{{-105.0, 40.0}, {-105.0, 40.0}});
    // convention: lon maps to x, lat maps to y
    static_assert(lat_lon_box.min.x == -105.0);
    static_assert(lat_lon_box.min.y == 40.0);
    // scalar-aware: float scalar
    constexpr auto lat_lon_float_box = talus::bounding_box_of<float>(LatLonPoint{40.0, -105.0});
    static_assert(lat_lon_float_box == talus::BoundingBox<float>{{-105.0F, 40.0F}, {-105.0F, 40.0F}});
    // result is also a degenerate box
    static_assert(lat_lon_box.min == lat_lon_box.max);

    // ── .bounds() extraction ──────────────────────────────────────────────────
    constexpr auto bounded_box = talus::bounding_box_of(BoundedObject{});
    static_assert(bounded_box == talus::BoundingBox<double>{{-1.0, -2.0}, {3.0, 4.0}});
    // scalar-aware: float HasBounds type
    constexpr auto bounded_float_box = talus::bounding_box_of<float>(BoundedFloatObject{});
    static_assert(bounded_float_box == talus::BoundingBox<float>{{-1.0F, -2.0F}, {3.0F, 4.0F}});
    // result is a proper (non-degenerate) box
    static_assert(bounded_box.min != bounded_box.max);

    // ── .bounds() precedence over .x/.y ──────────────────────────────────────
    // PointWithBounds satisfies both Pointlike and HasBounds; bounds() must win
    constexpr auto point_with_bounds_box = talus::bounding_box_of(PointWithBounds{1.0, 2.0});
    static_assert(point_with_bounds_box == talus::BoundingBox<double>{{-5.0, -6.0}, {7.0, 8.0}});
    // confirm the x/y values (1.0, 2.0) were NOT used
    static_assert(point_with_bounds_box.min.x == -5.0);
    static_assert(point_with_bounds_box.min.y == -6.0);
}

} // namespace

int main() {
    test_geometry();
    test_concepts();
}

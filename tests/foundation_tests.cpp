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

struct CustomExtractor {
    constexpr talus::BoundingBox<double> operator()(const XYPoint& point) const noexcept {
        return {{point.x, static_cast<double>(point.y)}, {point.x, static_cast<double>(point.y)}};
    }
};

static_assert(talus::HasXY<XYPoint>);
static_assert(!talus::HasLatLon<XYPoint>);
static_assert(talus::HasLatLon<LatLonPoint>);
static_assert(!talus::HasXY<LatLonPoint>);
static_assert(talus::HasBounds<BoundedObject>);
static_assert(talus::Pointlike<XYPoint>);
static_assert(talus::Pointlike<LatLonPoint>);
static_assert(talus::Indexable<XYPoint>);
static_assert(talus::Indexable<LatLonPoint>);
static_assert(talus::Indexable<BoundedObject>);
static_assert(talus::CoordExtractor<CustomExtractor, XYPoint>);

void test_geometry() {
    using talus::BoundingBox;
    using talus::Point;

    constexpr Point<double> origin{0.0, 0.0};
    constexpr Point<double> same_origin{0.0, 0.0};
    static_assert(origin == same_origin);

    constexpr BoundingBox<double> box{{0.0, 0.0}, {4.0, 3.0}};
    static_assert(box.contains({0.0, 0.0}));
    static_assert(box.contains({4.0, 3.0}));
    static_assert(box.contains({2.0, 1.5}));
    static_assert(!box.contains({4.1, 1.5}));

    constexpr BoundingBox<double> touching{{4.0, 1.0}, {5.0, 2.0}};
    static_assert(box.intersects(touching));

    constexpr BoundingBox<double> disjoint{{4.1, 1.0}, {5.0, 2.0}};
    static_assert(!box.intersects(disjoint));

    constexpr BoundingBox<double> expanded = box.expand({{-1.0, 1.0}, {2.0, 5.0}});
    static_assert(expanded == BoundingBox<double>{{-1.0, 0.0}, {4.0, 5.0}});
    static_assert(box.area() == 12.0);
    static_assert(box.enlarged_area({{-1.0, 1.0}, {2.0, 5.0}}) == 13.0);
    static_assert(box.center() == Point<double>{2.0, 1.5});
    static_assert(box.min_sq_distance({2.0, 1.0}) == 0.0);
    static_assert(box.min_sq_distance({5.0, 5.0}) == 5.0);

    constexpr talus::Segment<double> segment{{4.0, -2.0}, {-1.0, 3.0}};
    static_assert(segment.bounds() == BoundingBox<double>{{-1.0, -2.0}, {4.0, 3.0}});

    static_assert(talus::sq_distance(Point<double>{0.0, 0.0}, Point<double>{3.0, 4.0}) == 25.0);
    assert(std::abs(talus::distance(Point<double>{0.0, 0.0}, Point<double>{3.0, 4.0}) - 5.0) < 1e-12);
}

void test_concepts() {
    constexpr auto xy_box = talus::bounding_box_of(XYPoint{1.5F, 2});
    static_assert(xy_box == talus::BoundingBox<double>{{1.5, 2.0}, {1.5, 2.0}});

    constexpr auto lat_lon_box = talus::bounding_box_of(LatLonPoint{40.0, -105.0});
    static_assert(lat_lon_box == talus::BoundingBox<double>{{-105.0, 40.0}, {-105.0, 40.0}});

    constexpr auto bounded_box = talus::bounding_box_of(BoundedObject{});
    static_assert(bounded_box == talus::BoundingBox<double>{{-1.0, -2.0}, {3.0, 4.0}});
}

} // namespace

int main() {
    test_geometry();
    test_concepts();
}

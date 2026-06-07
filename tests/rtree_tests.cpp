#include <talus/rtree.hpp>

#include "brute_force.hpp"

#include <algorithm>
#include "test_check.hpp"
#include <array>
#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Box = talus::BoundingBox<double>;

struct PointRecord {
    double x = 0.0;
    double y = 0.0;
    int id = 0;
};

struct BoundedRecord {
    int id = 0;
    Box box{};

    [[nodiscard]] Box bounds() const noexcept {
        return box;
    }
};

struct NamedPoint {
    double x = 0.0;
    double y = 0.0;
    std::string name;
};

template<typename T>
[[nodiscard]] std::vector<int> sorted_ids(std::vector<T> values) {
    std::vector<int> ids;
    ids.reserve(values.size());
    for (const T& value : values) {
        ids.push_back(value.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

template<typename T>
void assert_same_ids(std::vector<T> actual, std::vector<T> expected) {
    TALUS_CHECK(sorted_ids(std::move(actual)) == sorted_ids(std::move(expected)));
}

// Test: test_spatial_index_starts_empty_and_tracks_size
// Verifies the public wrapper reports empty state, increments size after inserts,
// and clears back to an empty reusable tree.
void test_spatial_index_starts_empty_and_tracks_size() {
    talus::SpatialIndex<PointRecord, double, 4> index;

    TALUS_CHECK(index.empty());
    TALUS_CHECK(index.size() == 0);

    index.insert(PointRecord{1.0, 2.0, 1});
    index.insert(PointRecord{3.0, 4.0, 2});

    TALUS_CHECK(!index.empty());
    TALUS_CHECK(index.size() == 2);

    index.clear();

    TALUS_CHECK(index.empty());
    TALUS_CHECK(index.size() == 0);
    TALUS_CHECK(index.search(Box{{0.0, 0.0}, {10.0, 10.0}}).empty());

    index.insert(PointRecord{5.0, 6.0, 3});
    TALUS_CHECK(index.size() == 1);
    assert_same_ids(index.search(Box{{5.0, 6.0}, {5.0, 6.0}}), std::vector<PointRecord>{{5.0, 6.0, 3}});
}

// Test: test_spatial_index_search_matches_brute_force_fixture
// Verifies deterministic point searches match the brute-force oracle after
// enough inserts to force public wrapper split propagation.
void test_spatial_index_search_matches_brute_force_fixture() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;

    const std::vector<PointRecord> points{
        {-4.0, -4.0, 1},
        {-1.0, -1.0, 2},
        {0.0, 0.0, 3},
        {1.0, 1.0, 4},
        {2.0, 2.0, 5},
        {8.0, 8.0, 6},
        {9.0, 9.0, 7},
        {10.0, 10.0, 8},
        {11.0, 11.0, 9},
        {20.0, 20.0, 10}
    };

    for (const PointRecord& point : points) {
        index.insert(point);
        oracle.insert(point);
    }

    const std::vector<Box> queries{
        {{-2.0, -2.0}, {2.0, 2.0}},
        {{8.0, 8.0}, {10.0, 10.0}},
        {{100.0, 100.0}, {101.0, 101.0}},
        {{-4.0, -4.0}, {-4.0, -4.0}},
        {{0.0, -10.0}, {10.0, 10.0}}
    };

    for (Box query : queries) {
        assert_same_ids(index.search(query), oracle.search(query));
        assert_same_ids(index.within(query), oracle.within(query));
    }
}

// Test: test_spatial_index_search_matches_bounded_geometry_oracle
// Verifies `.bounds()` values are indexed and searched by rectangle intersection,
// including boundary-touching boxes.
void test_spatial_index_search_matches_bounded_geometry_oracle() {
    talus::SpatialIndex<BoundedRecord, double, 4> index;
    talus::test::BruteForceIndex<BoundedRecord, double> oracle;

    const std::vector<BoundedRecord> records{
        {1, {{0.0, 0.0}, {2.0, 2.0}}},
        {2, {{3.0, 3.0}, {5.0, 5.0}}},
        {3, {{5.0, 5.0}, {6.0, 6.0}}},
        {4, {{-10.0, -10.0}, {-8.0, -8.0}}},
        {5, {{1.0, 4.0}, {2.0, 6.0}}}
    };

    for (const BoundedRecord& record : records) {
        index.insert(record);
        oracle.insert(record);
    }

    const Box query{{2.0, 2.0}, {5.0, 5.0}};
    assert_same_ids(index.search(query), oracle.search(query));
}

// Test: test_spatial_index_accepts_move_inserted_values
// Verifies rvalue insertion stores the moved value and rectangle search returns
// the expected copied payload.
void test_spatial_index_accepts_move_inserted_values() {
    talus::SpatialIndex<NamedPoint, double, 4> index;

    NamedPoint point{1.0, 2.0, "alpha"};
    index.insert(std::move(point));

    const std::vector<NamedPoint> matches = index.search(Box{{1.0, 2.0}, {1.0, 2.0}});
    TALUS_CHECK(matches.size() == 1);
    TALUS_CHECK(matches.front().name == "alpha");
}

// Test: test_spatial_index_randomized_search_matches_brute_force
// Verifies randomized point fixtures and queries produce the same hit sets as
// the brute-force oracle across many split and subtree-selection paths.
void test_spatial_index_randomized_search_matches_brute_force() {
    talus::SpatialIndex<PointRecord, double, 6> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(1337);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);
    std::uniform_real_distribution<double> extent(0.0, 250.0);

    for (int id = 0; id < 500; ++id) {
        PointRecord point{coord(rng), coord(rng), id};
        index.insert(point);
        oracle.insert(point);
    }

    for (std::size_t i = 0; i < 200; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const double width = extent(rng);
        const double height = extent(rng);
        const Box query{{x, y}, {x + width, y + height}};
        assert_same_ids(index.search(query), oracle.search(query));
    }
}

// Test: test_spatial_index_grid_data_search_matches_brute_force
// Verifies correctness on grid-aligned (degenerate) data: integer coordinates in
// a tiny range yield many collinear points and zero-area bounding boxes, which
// exercises the margin-based ChooseSubtree and split tie-breakers heavily, and a
// small fanout forces many splits. Results must still match the brute-force
// oracle, guarding the heuristic against regressions on point/axis-aligned data.
void test_spatial_index_grid_data_search_matches_brute_force() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(2024);
    std::uniform_int_distribution<int> grid(0, 12);

    for (int id = 0; id < 800; ++id) {
        PointRecord point{static_cast<double>(grid(rng)), static_cast<double>(grid(rng)), id};
        index.insert(point);
        oracle.insert(point);
    }

    std::uniform_int_distribution<int> bound(-2, 14);
    for (std::size_t i = 0; i < 200; ++i) {
        const int x0 = bound(rng);
        const int x1 = bound(rng);
        const int y0 = bound(rng);
        const int y1 = bound(rng);
        const Box query{
            {static_cast<double>(std::min(x0, x1)), static_cast<double>(std::min(y0, y1))},
            {static_cast<double>(std::max(x0, x1)), static_cast<double>(std::max(y0, y1))}
        };
        assert_same_ids(index.search(query), oracle.search(query));
    }
}

// Test: test_spatial_index_is_movable
// Verifies SpatialIndex is move-constructible and move-assignable with the tree
// intact. The index is built large enough to force a multi-level internal tree,
// so node parent pointers must survive the move. After moving, searches still
// match the oracle, further inserts (which walk parent pointers up to the root)
// keep working, and the moved-from index is left empty and reusable.
void test_spatial_index_is_movable() {
    using Index = talus::SpatialIndex<PointRecord, double, 4>;

    Index a;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);
    for (int id = 0; id < 200; ++id) {
        const PointRecord p{coord(rng), coord(rng), id};
        a.insert(p);
        oracle.insert(p);
    }

    const Box query{{-50.0, -50.0}, {50.0, 50.0}};
    const std::size_t original_size = a.size();

    // Move-construct: b takes a's tree; a is left empty and reusable.
    Index b(std::move(a));
    TALUS_CHECK(b.size() == original_size);
    assert_same_ids(b.search(query), oracle.search(query));
    TALUS_CHECK(a.empty());
    a.insert(PointRecord{0.0, 0.0, -1});
    TALUS_CHECK(a.size() == 1);

    // Inserting into the moved index walks parent pointers up to the root; a
    // dangling parent or stale root would corrupt here (and trip ASan in CI).
    for (int id = 200; id < 280; ++id) {
        const PointRecord p{coord(rng), coord(rng), id};
        b.insert(p);
        oracle.insert(p);
    }
    assert_same_ids(b.search(query), oracle.search(query));

    // Move-assign over an index that already holds data.
    Index c;
    c.insert(PointRecord{1.0, 2.0, -2});
    c = std::move(b);
    assert_same_ids(c.search(query), oracle.search(query));
    TALUS_CHECK(b.empty());
}

// Test: test_spatial_index_throws_on_invalid_geometry
// Verifies the public boundary rejects invalid geometry: insert throws
// talus::invalid_geometry on NaN and infinite coordinates and leaves the index
// unchanged (strong guarantee); search throws on an inverted (min > max) query
// box; the exception is catchable as std::invalid_argument; and valid operations
// still work afterward.
void test_spatial_index_throws_on_invalid_geometry() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    index.insert(PointRecord{1.0, 2.0, 1});
    const std::size_t before = index.size();

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    bool threw = false;
    try {
        index.insert(PointRecord{nan, 0.0, 2});
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);
    TALUS_CHECK(index.size() == before);  // rejected insert left the index unchanged

    // Infinite coordinate, caught as the standard base type.
    threw = false;
    try {
        index.insert(PointRecord{0.0, inf, 3});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TALUS_CHECK(threw);
    TALUS_CHECK(index.size() == before);

    // Inverted query box (min > max) on search.
    threw = false;
    try {
        (void)index.search(Box{{5.0, 5.0}, {0.0, 0.0}});
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);

    // A valid query still returns the stored value.
    assert_same_ids(index.search(Box{{0.0, 0.0}, {2.0, 3.0}}),
                    std::vector<PointRecord>{{1.0, 2.0, 1}});
}

// Large value type whose values are stored out of line (boxed). Has `.x/.y` for
// indexing and `.id` for oracle comparison; the 256-byte blob makes it boxed.
struct BigRecord {
    double x = 0.0;
    double y = 0.0;
    int id = 0;
    std::array<char, 256> blob{};
};

// Test: test_spatial_index_handles_large_boxed_values
// Verifies the public index works end-to-end with a large (boxed) value type:
// inserts that trigger splits relocate boxed entries, searches match the
// brute-force oracle, and payloads survive intact across the boxed round trip.
void test_spatial_index_handles_large_boxed_values() {
    static_assert(talus::detail::rtree_boxes_value<BigRecord>);

    talus::SpatialIndex<BigRecord, double, 4> index;  // small fanout => many splits
    talus::test::BruteForceIndex<BigRecord, double> oracle;
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);

    for (int id = 0; id < 300; ++id) {
        BigRecord r;
        r.x = coord(rng);
        r.y = coord(rng);
        r.id = id;
        r.blob[0] = static_cast<char>(id & 0x7F);
        index.insert(r);
        oracle.insert(r);
    }

    const Box query{{-40.0, -40.0}, {40.0, 40.0}};
    assert_same_ids(index.search(query), oracle.search(query));

    // Payload integrity through the boxed round trip.
    for (const BigRecord& r : index.search(query)) {
        TALUS_CHECK(r.blob[0] == static_cast<char>(r.id & 0x7F));
    }
}

} // namespace

int main() {
    test_spatial_index_starts_empty_and_tracks_size();
    test_spatial_index_search_matches_brute_force_fixture();
    test_spatial_index_search_matches_bounded_geometry_oracle();
    test_spatial_index_accepts_move_inserted_values();
    test_spatial_index_randomized_search_matches_brute_force();
    test_spatial_index_grid_data_search_matches_brute_force();
    test_spatial_index_is_movable();
    test_spatial_index_throws_on_invalid_geometry();
    test_spatial_index_handles_large_boxed_values();
    return 0;
}

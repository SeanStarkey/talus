#include <talus/rtree.hpp>

#include "brute_force.hpp"

#include <algorithm>
#include "test_check.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
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

    constexpr bool operator==(const PointRecord&) const noexcept = default;
};

struct BoundedRecord {
    int id = 0;
    Box box{};

    [[nodiscard]] Box bounds() const noexcept {
        return box;
    }

    constexpr bool operator==(const BoundedRecord&) const noexcept = default;
};

struct NamedPoint {
    double x = 0.0;
    double y = 0.0;
    std::string name;

    bool operator==(const NamedPoint&) const = default;
};

// A record whose coordinates Talus cannot detect automatically: they live in
// a packed array, so indexing it requires a custom CoordExtractor.
struct PackedRecord {
    std::array<double, 2> position{};
    int id = 0;

    constexpr bool operator==(const PackedRecord&) const noexcept = default;
};

struct PackedRecordExtractor {
    [[nodiscard]] Box operator()(const PackedRecord& record) const noexcept {
        const talus::Point<double> point{record.position[0], record.position[1]};
        return {point, point};
    }
};

static_assert(!talus::Indexable<PackedRecord>);
static_assert(talus::CoordExtractor<PackedRecordExtractor, PackedRecord>);

// Extractor that resolves coordinates through an external table keyed by the
// stored value, exercising the stateful SpatialIndex(Extractor) constructor.
struct TableExtractor {
    const std::vector<talus::Point<double>>* table = nullptr;

    [[nodiscard]] Box operator()(const int& id) const {
        const talus::Point<double> point = (*table)[static_cast<std::size_t>(id)];
        return {point, point};
    }
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

// Compares id sequences without sorting, so result ordering must match too.
// Only valid for tie-free queries, where ascending-distance order is unique.
template<typename T>
void assert_same_ordered_ids(const std::vector<T>& actual, const std::vector<T>& expected) {
    TALUS_CHECK(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        TALUS_CHECK(actual[i].id == expected[i].id);
    }
}

template<typename T>
void assert_same_optional_id(const std::optional<T>& actual, const std::optional<T>& expected) {
    TALUS_CHECK(actual.has_value() == expected.has_value());
    if (actual && expected) {
        TALUS_CHECK(actual->id == expected->id);
    }
}

// Test: test_spatial_index_starts_empty_and_tracks_size
// Verifies the public wrapper reports empty state, increments size after inserts,
// and clears back to an empty reusable tree.
void test_spatial_index_starts_empty_and_tracks_size() {
    talus::SpatialIndex<PointRecord, double, 4> index;

    TALUS_CHECK(index.empty());
    TALUS_CHECK(index.size() == 0);
    TALUS_CHECK(!index.nearest_neighbor(talus::Point<double>{0.0, 0.0}).has_value());

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

// Test: test_spatial_index_erase_matches_brute_force_fixture
// Verifies public erase removes one exact value, reports misses without
// changing size, and keeps later searches aligned with the brute-force oracle
// after enough inserts and erases to trigger condense/reinsertion paths.
void test_spatial_index_erase_matches_brute_force_fixture() {
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

    for (PointRecord point : {points[0], points[3], points[8], points[5]}) {
        TALUS_CHECK(index.erase(point));
        TALUS_CHECK(oracle.erase(point));
        TALUS_CHECK(index.size() == oracle.size());
    }

    const PointRecord missing{123.0, 456.0, 99};
    TALUS_CHECK(!index.erase(missing));
    TALUS_CHECK(!oracle.erase(missing));
    TALUS_CHECK(index.size() == oracle.size());

    for (Box query : {
        Box{{-10.0, -10.0}, {3.0, 3.0}},
        Box{{7.0, 7.0}, {12.0, 12.0}},
        Box{{-100.0, -100.0}, {100.0, 100.0}}
    }) {
        assert_same_ids(index.search(query), oracle.search(query));
    }
}

// Test: test_spatial_index_erase_one_of_duplicate_values
// Verifies erase removes a single matching value per call, preserving duplicate
// equal records until each copy is explicitly erased.
void test_spatial_index_erase_one_of_duplicate_values() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    const PointRecord duplicate{1.0, 2.0, 7};

    index.insert(duplicate);
    index.insert(duplicate);
    index.insert(PointRecord{3.0, 4.0, 8});

    TALUS_CHECK(index.erase(duplicate));
    TALUS_CHECK(index.size() == 2);
    TALUS_CHECK(index.search(Box{{1.0, 2.0}, {1.0, 2.0}}).size() == 1);

    TALUS_CHECK(index.erase(duplicate));
    TALUS_CHECK(index.size() == 1);
    TALUS_CHECK(index.search(Box{{1.0, 2.0}, {1.0, 2.0}}).empty());
}

// Test: test_spatial_index_erase_to_empty_then_reuse
// Verifies that erasing every entry from a multi-level index leaves the root
// in a clean leaf state, so subsequent inserts and searches behave like a
// freshly constructed index.
void test_spatial_index_erase_to_empty_then_reuse() {
    talus::SpatialIndex<PointRecord, double, 4> index;

    std::vector<PointRecord> points;
    for (int i = 0; i < 24; ++i) {
        points.push_back({static_cast<double>(i), static_cast<double>(i * 2), i + 1});
    }

    for (const PointRecord& point : points) {
        index.insert(point);
    }

    for (const PointRecord& point : points) {
        TALUS_CHECK(index.erase(point));
    }
    TALUS_CHECK(index.empty());
    TALUS_CHECK(index.search(Box{{-100.0, -100.0}, {100.0, 100.0}}).empty());

    const PointRecord revived{5.0, 5.0, 99};
    index.insert(revived);
    TALUS_CHECK(index.size() == 1);

    const std::vector<PointRecord> found = index.search(Box{{5.0, 5.0}, {5.0, 5.0}});
    TALUS_CHECK(found.size() == 1);
    TALUS_CHECK(found.front() == revived);
}

// Test: test_spatial_index_randomized_erase_matches_brute_force
// Verifies randomized erases and subsequent rectangle queries stay identical to
// the brute-force oracle across many condense, root-collapse, and reinsert paths.
void test_spatial_index_randomized_erase_matches_brute_force() {
    talus::SpatialIndex<PointRecord, double, 6> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(707);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);
    std::uniform_real_distribution<double> extent(0.0, 250.0);
    std::vector<PointRecord> points;

    for (int id = 0; id < 500; ++id) {
        PointRecord point{coord(rng), coord(rng), id};
        points.push_back(point);
        index.insert(point);
        oracle.insert(point);
    }

    std::shuffle(points.begin(), points.end(), rng);
    for (std::size_t i = 0; i < 260; ++i) {
        TALUS_CHECK(index.erase(points[i]));
        TALUS_CHECK(oracle.erase(points[i]));
        TALUS_CHECK(index.size() == oracle.size());
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

// Test: test_spatial_index_radius_search_matches_brute_force_fixture
// Verifies deterministic point radius searches match the brute-force oracle
// after enough inserts to force public wrapper split propagation.
void test_spatial_index_radius_search_matches_brute_force_fixture() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;

    const std::vector<PointRecord> points{
        {-20.0, -20.0, 1},
        {-4.0, -2.0, 2},
        {0.0, 0.0, 3},
        {3.0, 4.0, 4},
        {10.0, 10.0, 5},
        {50.0, 0.0, 6},
        {80.0, 80.0, 7}
    };

    for (const PointRecord& point : points) {
        index.insert(point);
        oracle.insert(point);
    }

    assert_same_ids(
        index.radius_search(talus::Point<double>{0.0, 0.0}, 5.0),
        oracle.radius_search(talus::Point<double>{0.0, 0.0}, 5.0));
    assert_same_ids(
        index.radius_search(talus::Point<double>{50.0, 4.0}, 4.0),
        oracle.radius_search(talus::Point<double>{50.0, 4.0}, 4.0));
    assert_same_ids(
        index.radius_search(talus::Point<double>{100.0, 100.0}, 1.0),
        oracle.radius_search(talus::Point<double>{100.0, 100.0}, 1.0));
}

// Test: test_spatial_index_radius_search_matches_bounded_geometry_oracle
// Verifies radius search uses each value's stored bounding box, so records that
// contain the query point match with zero distance and boundary hits are kept.
void test_spatial_index_radius_search_matches_bounded_geometry_oracle() {
    talus::SpatialIndex<BoundedRecord, double, 4> index;
    talus::test::BruteForceIndex<BoundedRecord, double> oracle;

    const std::vector<BoundedRecord> records{
        {1, {{0.0, 0.0}, {2.0, 2.0}}},
        {2, {{10.0, 10.0}, {20.0, 20.0}}},
        {3, {{30.0, 30.0}, {31.0, 31.0}}},
        {4, {{-20.0, -20.0}, {-10.0, -10.0}}},
        {5, {{5.0, 40.0}, {8.0, 50.0}}}
    };

    for (const BoundedRecord& record : records) {
        index.insert(record);
        oracle.insert(record);
    }

    assert_same_ids(
        index.radius_search(talus::Point<double>{15.0, 12.0}, 0.0),
        oracle.radius_search(talus::Point<double>{15.0, 12.0}, 0.0));
    assert_same_ids(
        index.radius_search(talus::Point<double>{28.0, 29.0}, std::sqrt(5.0)),
        oracle.radius_search(talus::Point<double>{28.0, 29.0}, std::sqrt(5.0)));
}

// Test: test_spatial_index_randomized_radius_search_matches_brute_force
// Verifies radius queries agree with the brute-force oracle across randomized
// point fixtures and many split/subtree layouts.
void test_spatial_index_randomized_radius_search_matches_brute_force() {
    talus::SpatialIndex<PointRecord, double, 6> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(5150);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);
    std::uniform_real_distribution<double> radius(0.0, 250.0);

    for (int id = 0; id < 500; ++id) {
        PointRecord point{coord(rng), coord(rng), id};
        index.insert(point);
        oracle.insert(point);
    }

    for (std::size_t i = 0; i < 200; ++i) {
        const talus::Point<double> query{coord(rng), coord(rng)};
        const double r = radius(rng);
        assert_same_ids(index.radius_search(query, r), oracle.radius_search(query, r));
    }
}

// Test: test_spatial_index_nearest_neighbor_matches_brute_force_fixture
// Verifies deterministic nearest-neighbor queries match the linear-scan oracle
// after enough inserts to force split propagation and an internal root.
void test_spatial_index_nearest_neighbor_matches_brute_force_fixture() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;

    const std::vector<PointRecord> points{
        {-20.0, -20.0, 1},
        {-4.0, -2.0, 2},
        {0.0, 0.0, 3},
        {3.0, 4.0, 4},
        {10.0, 10.0, 5},
        {50.0, 0.0, 6},
        {80.0, 80.0, 7}
    };

    for (const PointRecord& point : points) {
        index.insert(point);
        oracle.insert(point);
    }

    const std::vector<talus::Point<double>> queries{
        {-19.0, -18.0},
        {2.0, 3.0},
        {12.0, 9.0},
        {55.0, -2.0},
        {100.0, 100.0}
    };

    for (talus::Point<double> query : queries) {
        assert_same_optional_id(index.nearest_neighbor(query), oracle.nearest_neighbor(query));
    }
}

// Test: test_spatial_index_nearest_neighbor_matches_bounded_geometry_oracle
// Verifies nearest-neighbor queries use each value's stored bounding box, so a
// query inside a bounded record returns that record with zero distance.
void test_spatial_index_nearest_neighbor_matches_bounded_geometry_oracle() {
    talus::SpatialIndex<BoundedRecord, double, 4> index;
    talus::test::BruteForceIndex<BoundedRecord, double> oracle;

    const std::vector<BoundedRecord> records{
        {1, {{0.0, 0.0}, {2.0, 2.0}}},
        {2, {{10.0, 10.0}, {20.0, 20.0}}},
        {3, {{30.0, 30.0}, {31.0, 31.0}}},
        {4, {{-20.0, -20.0}, {-10.0, -10.0}}},
        {5, {{5.0, 40.0}, {8.0, 50.0}}}
    };

    for (const BoundedRecord& record : records) {
        index.insert(record);
        oracle.insert(record);
    }

    assert_same_optional_id(
        index.nearest_neighbor(talus::Point<double>{15.0, 12.0}),
        oracle.nearest_neighbor(talus::Point<double>{15.0, 12.0}));
    assert_same_optional_id(
        index.nearest_neighbor(talus::Point<double>{28.0, 29.0}),
        oracle.nearest_neighbor(talus::Point<double>{28.0, 29.0}));
}

// Test: test_spatial_index_randomized_nearest_neighbor_matches_brute_force
// Verifies nearest-neighbor queries agree with the brute-force oracle across
// randomized unique point data and many split/subtree layouts.
void test_spatial_index_randomized_nearest_neighbor_matches_brute_force() {
    talus::SpatialIndex<PointRecord, double, 6> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(4242);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);

    for (int id = 0; id < 500; ++id) {
        // The small deterministic offset prevents exact ties from duplicate
        // generated coordinates while keeping the fixture effectively random.
        const PointRecord point{
            coord(rng) + static_cast<double>(id) * 1.0e-6,
            coord(rng) - static_cast<double>(id) * 1.0e-6,
            id
        };
        index.insert(point);
        oracle.insert(point);
    }

    for (std::size_t i = 0; i < 200; ++i) {
        const talus::Point<double> query{coord(rng), coord(rng)};
        assert_same_optional_id(index.nearest_neighbor(query), oracle.nearest_neighbor(query));
    }
}

// Test: test_spatial_index_nearest_neighbors_matches_brute_force_fixture
// Verifies deterministic k-nearest queries return the same values in the same
// ascending-distance order as the linear-scan oracle, across k values below,
// at, and above the index size, for both point and bounded-geometry records.
// Also verifies k == 0 and an empty index return empty results. All fixture
// queries are tie-free, so result order is fully determined and the comparison
// can be exact rather than set-based.
void test_spatial_index_nearest_neighbors_matches_brute_force_fixture() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;

    TALUS_CHECK(index.nearest_neighbors(talus::Point<double>{0.0, 0.0}, 3).empty());

    const std::vector<PointRecord> points{
        {-20.0, -20.0, 1},
        {-4.0, -2.0, 2},
        {0.0, 0.0, 3},
        {3.0, 4.0, 4},
        {10.0, 10.0, 5},
        {50.0, 0.0, 6},
        {80.0, 80.0, 7}
    };

    for (const PointRecord& point : points) {
        index.insert(point);
        oracle.insert(point);
    }

    const std::vector<talus::Point<double>> queries{
        {-19.0, -18.0},
        {2.0, 3.0},
        {12.0, 9.0},
        {55.0, -2.0},
        {100.0, 100.0}
    };

    for (talus::Point<double> query : queries) {
        TALUS_CHECK(index.nearest_neighbors(query, 0).empty());
        for (std::size_t k : {std::size_t{1}, std::size_t{3}, points.size(), points.size() + 5}) {
            assert_same_ordered_ids(index.nearest_neighbors(query, k),
                                    oracle.nearest_neighbors(query, k));
        }
    }

    // Bounded geometries rank by box distance: a query inside a stored box is
    // at distance zero and sorts first.
    talus::SpatialIndex<BoundedRecord, double, 4> box_index;
    talus::test::BruteForceIndex<BoundedRecord, double> box_oracle;

    const std::vector<BoundedRecord> records{
        {1, {{0.0, 0.0}, {2.0, 2.0}}},
        {2, {{10.0, 10.0}, {20.0, 20.0}}},
        {3, {{30.0, 30.0}, {31.0, 31.0}}},
        {4, {{-20.0, -20.0}, {-10.0, -10.0}}},
        {5, {{5.0, 40.0}, {8.0, 50.0}}}
    };

    for (const BoundedRecord& record : records) {
        box_index.insert(record);
        box_oracle.insert(record);
    }

    const talus::Point<double> inside{15.0, 12.0};
    assert_same_ordered_ids(box_index.nearest_neighbors(inside, 3),
                            box_oracle.nearest_neighbors(inside, 3));
    TALUS_CHECK(box_index.nearest_neighbors(inside, 1).front().id == 2);
}

// Test: test_spatial_index_randomized_nearest_neighbors_matches_brute_force
// Verifies k-nearest queries agree with the brute-force oracle — same values,
// same ascending-distance order — across randomized unique point data, many
// split/subtree layouts, and varying k. Coordinates are perturbed per id so
// distances are tie-free and exact ordered comparison is valid.
void test_spatial_index_randomized_nearest_neighbors_matches_brute_force() {
    talus::SpatialIndex<PointRecord, double, 6> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    std::mt19937 rng(24242);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);

    for (int id = 0; id < 500; ++id) {
        // The small deterministic offset prevents exact ties from duplicate
        // generated coordinates while keeping the fixture effectively random.
        const PointRecord point{
            coord(rng) + static_cast<double>(id) * 1.0e-6,
            coord(rng) - static_cast<double>(id) * 1.0e-6,
            id
        };
        index.insert(point);
        oracle.insert(point);
    }

    std::uniform_int_distribution<std::size_t> k_dist(1, 25);
    for (std::size_t i = 0; i < 200; ++i) {
        const talus::Point<double> query{coord(rng), coord(rng)};
        const std::size_t k = k_dist(rng);
        assert_same_ordered_ids(index.nearest_neighbors(query, k),
                                oracle.nearest_neighbors(query, k));
    }

    // k >= size returns every value, fully ordered by distance.
    const talus::Point<double> query{coord(rng), coord(rng)};
    assert_same_ordered_ids(index.nearest_neighbors(query, 10000),
                            oracle.nearest_neighbors(query, 10000));
}

// Test: test_spatial_index_nearest_neighbor_returns_a_minimum_on_ties
// Pins the documented equidistant-tie contract for the singular
// nearest_neighbor: when several values are equidistant from the query at the
// minimum distance, the call returns one of those minima (never a farther
// value). Which tied value wins is unspecified — it follows tree-traversal
// order, not insertion order — but it is stable for a fixed tree, so two calls
// agree. The randomized nearest tests deliberately offset coordinates to avoid
// ties, so this is the only place the tie path is exercised. A small fanout
// forces a split so the tie is resolved across multiple nodes.
void test_spatial_index_nearest_neighbor_returns_a_minimum_on_ties() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    // Four points equidistant (distance 1) from the origin, plus a far point.
    index.insert(PointRecord{1.0, 0.0, 1});
    index.insert(PointRecord{-1.0, 0.0, 2});
    index.insert(PointRecord{0.0, 1.0, 3});
    index.insert(PointRecord{0.0, -1.0, 4});
    index.insert(PointRecord{5.0, 5.0, 5});  // distance sqrt(50), never nearest

    const talus::Point<double> query{0.0, 0.0};
    const auto nearest = index.nearest_neighbor(query);
    TALUS_CHECK(nearest.has_value());

    // The winner is one of the four tied minima, not the far point ...
    TALUS_CHECK(nearest->id >= 1 && nearest->id <= 4);
    // ... and its distance is exactly the minimum (1).
    TALUS_CHECK(talus::sq_distance(query, talus::Point<double>{nearest->x, nearest->y}) == 1.0);

    // Unspecified which tied value wins, but deterministic for a fixed tree.
    TALUS_CHECK(index.nearest_neighbor(query)->id == nearest->id);
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

    // Non-finite nearest-neighbor query.
    threw = false;
    try {
        (void)index.nearest_neighbor(talus::Point<double>{0.0, nan});
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);

    // Non-finite k-nearest query.
    threw = false;
    try {
        (void)index.nearest_neighbors(talus::Point<double>{nan, 0.0}, 3);
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);

    // Invalid radius-search query and radius.
    threw = false;
    try {
        (void)index.radius_search(talus::Point<double>{0.0, nan}, 1.0);
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);

    threw = false;
    try {
        (void)index.radius_search(talus::Point<double>{0.0, 0.0}, -1.0);
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);

    // Visitor overloads validate before traversing: the visitor never runs.
    std::size_t visitor_calls = 0;
    threw = false;
    try {
        (void)index.search(Box{{5.0, 5.0}, {0.0, 0.0}},
            [&](const PointRecord&) { ++visitor_calls; });
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);

    threw = false;
    try {
        (void)index.radius_search(talus::Point<double>{0.0, nan}, 1.0,
            [&](const PointRecord&) { ++visitor_calls; });
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);
    TALUS_CHECK(visitor_calls == 0);

    // Invalid erased value.
    threw = false;
    try {
        (void)index.erase(PointRecord{nan, 0.0, 4});
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }
    TALUS_CHECK(threw);
    TALUS_CHECK(index.size() == before);
}

// Test: test_spatial_index_enforces_coordinate_domain
// Verifies the distance-safe coordinate domain is enforced at the public
// boundary so the squared-distance math can never overflow to +inf (the M1
// finding). Coordinates beyond coordinate_limit are rejected by every
// coordinate-taking entry point (insert, erase, bulk_load, nearest_neighbor,
// nearest_neighbors, radius_search) with talus::invalid_geometry, and rejected
// mutations leave the index unchanged. radius_search additionally rejects a
// radius so large that radius*radius overflows — rather than silently matching
// every entry — while a large but in-domain radius still matches all stored
// values. Rectangular search, which uses comparisons rather than distances,
// remains valid for query bounds of any finite magnitude.
void test_spatial_index_enforces_coordinate_domain() {
    const double limit = talus::coordinate_limit<double>();
    const double over = std::nextafter(limit, std::numeric_limits<double>::infinity());

    talus::SpatialIndex<PointRecord, double, 4> index;
    index.insert(PointRecord{1.0, 2.0, 1});
    index.insert(PointRecord{-3.0, 4.0, 2});
    const std::size_t before = index.size();

    auto throws_invalid = [](auto&& fn) {
        bool threw = false;
        try {
            fn();
        } catch (const talus::invalid_geometry&) {
            threw = true;
        }
        return threw;
    };

    // Every coordinate-taking entry point rejects an out-of-domain coordinate,
    // and rejected mutations leave the index unchanged.
    TALUS_CHECK(throws_invalid([&] { index.insert(PointRecord{over, 0.0, 3}); }));
    TALUS_CHECK(index.size() == before);
    TALUS_CHECK(throws_invalid([&] { (void)index.erase(PointRecord{over, 0.0, 3}); }));
    TALUS_CHECK(index.size() == before);
    TALUS_CHECK(throws_invalid([&] {
        (void)index.nearest_neighbor(talus::Point<double>{over, 0.0});
    }));
    TALUS_CHECK(throws_invalid([&] {
        (void)index.nearest_neighbors(talus::Point<double>{0.0, over}, 3);
    }));
    TALUS_CHECK(throws_invalid([&] {
        (void)index.radius_search(talus::Point<double>{over, 0.0}, 1.0);
    }));

    // bulk_load validates the whole range up front; an out-of-domain entry
    // leaves the (empty) target unchanged.
    talus::SpatialIndex<PointRecord, double, 4> bulk;
    TALUS_CHECK(throws_invalid([&] {
        bulk.bulk_load(std::vector<PointRecord>{{0.0, 0.0, 1}, {over, 0.0, 2}});
    }));
    TALUS_CHECK(bulk.empty());

    // A radius whose square overflows to +inf is rejected (it would otherwise
    // make every entry compare as "within").
    const double huge_radius = std::sqrt(std::numeric_limits<double>::max()) * 2.0;
    TALUS_CHECK(std::isfinite(huge_radius));               // the radius itself is finite
    TALUS_CHECK(!std::isfinite(huge_radius * huge_radius)); // but its square overflows
    TALUS_CHECK(throws_invalid([&] {
        (void)index.radius_search(talus::Point<double>{0.0, 0.0}, huge_radius);
    }));

    // A large but in-domain radius (square stays finite) is accepted and matches
    // every stored value.
    TALUS_CHECK(std::isfinite(limit * limit));  // precondition: radius*radius finite
    TALUS_CHECK(index.radius_search(talus::Point<double>{0.0, 0.0}, limit).size() == before);

    // Rectangular search uses only comparisons, so a query box of any finite
    // magnitude — even beyond the distance domain — is still valid.
    TALUS_CHECK(index.search(Box{{-over, -over}, {over, over}}).size() == before);
}

// Large value type whose values are stored out of line (boxed). Has `.x/.y` for
// indexing and `.id` for oracle comparison; the 256-byte blob makes it boxed.
struct BigRecord {
    double x = 0.0;
    double y = 0.0;
    int id = 0;
    std::array<char, 256> blob{};

    constexpr bool operator==(const BigRecord&) const noexcept = default;
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

// Test: test_spatial_index_bulk_load_matches_brute_force
// Verifies STR bulk load of random points produces an index whose rectangular
// search, radius search, and nearest neighbor results all match the
// brute-force oracle, and whose size/empty reporting reflects the load.
void test_spatial_index_bulk_load_matches_brute_force() {
    std::mt19937 rng(2026);
    std::uniform_real_distribution<double> coord(-500.0, 500.0);

    std::vector<PointRecord> points;
    points.reserve(700);
    for (int id = 0; id < 700; ++id) {
        points.push_back(PointRecord{coord(rng), coord(rng), id});
    }

    talus::SpatialIndex<PointRecord, double, 4> index;  // small fanout => deep tree
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    index.bulk_load(points);
    for (const PointRecord& point : points) {
        oracle.insert(point);
    }

    TALUS_CHECK(index.size() == 700);
    TALUS_CHECK(!index.empty());

    for (int i = 0; i < 60; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const double width = std::abs(coord(rng)) / 4.0;
        const double height = std::abs(coord(rng)) / 4.0;
        const Box query{{x, y}, {x + width, y + height}};
        assert_same_ids(index.search(query), oracle.search(query));

        const talus::Point<double> center{coord(rng), coord(rng)};
        assert_same_ids(index.radius_search(center, 75.0), oracle.radius_search(center, 75.0));
        assert_same_optional_id(index.nearest_neighbor(center), oracle.nearest_neighbor(center));
    }
}

// Test: test_spatial_index_bulk_load_bounded_geometry_matches_brute_force
// Verifies bulk load works for `.bounds()` geometries (extended boxes, not
// points) and search results match the brute-force oracle.
void test_spatial_index_bulk_load_bounded_geometry_matches_brute_force() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);
    std::uniform_real_distribution<double> extent(0.0, 10.0);

    std::vector<BoundedRecord> records;
    for (int id = 0; id < 250; ++id) {
        const double x = coord(rng);
        const double y = coord(rng);
        records.push_back(BoundedRecord{id, Box{{x, y}, {x + extent(rng), y + extent(rng)}}});
    }

    talus::SpatialIndex<BoundedRecord, double, 4> index;
    talus::test::BruteForceIndex<BoundedRecord, double> oracle;
    index.bulk_load(records);
    for (const BoundedRecord& record : records) {
        oracle.insert(record);
    }

    for (int i = 0; i < 40; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const Box query{{x, y}, {x + 30.0, y + 30.0}};
        assert_same_ids(index.search(query), oracle.search(query));
    }
}

// Test: test_spatial_index_bulk_load_empty_range_is_noop
// Verifies bulk loading an empty range leaves the index empty and usable.
void test_spatial_index_bulk_load_empty_range_is_noop() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    index.bulk_load(std::vector<PointRecord>{});

    TALUS_CHECK(index.empty());
    TALUS_CHECK(index.size() == 0);

    index.insert(PointRecord{1.0, 1.0, 1});
    TALUS_CHECK(index.size() == 1);
}

// Test: test_spatial_index_bulk_load_requires_empty_index
// Verifies bulk loading a non-empty index throws std::logic_error and leaves
// the existing contents untouched.
void test_spatial_index_bulk_load_requires_empty_index() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    index.insert(PointRecord{1.0, 1.0, 1});

    bool threw = false;
    try {
        index.bulk_load(std::vector<PointRecord>{{2.0, 2.0, 2}});
    } catch (const std::logic_error&) {
        threw = true;
    }

    TALUS_CHECK(threw);
    TALUS_CHECK(index.size() == 1);
    assert_same_ids(index.search(Box{{0.0, 0.0}, {3.0, 3.0}}),
        std::vector<PointRecord>{{1.0, 1.0, 1}});

    // After an explicit clear the same load succeeds.
    index.clear();
    index.bulk_load(std::vector<PointRecord>{{2.0, 2.0, 2}});
    TALUS_CHECK(index.size() == 1);
    assert_same_ids(index.search(Box{{0.0, 0.0}, {3.0, 3.0}}),
        std::vector<PointRecord>{{2.0, 2.0, 2}});
}

// Test: test_spatial_index_bulk_load_throws_on_invalid_geometry
// Verifies a NaN coordinate anywhere in the input rejects the whole load with
// invalid_geometry before any value is stored, leaving the index empty and
// fully usable afterwards.
void test_spatial_index_bulk_load_throws_on_invalid_geometry() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<PointRecord> points{
        {1.0, 1.0, 1},
        {nan, 2.0, 2},
        {3.0, 3.0, 3},
    };

    talus::SpatialIndex<PointRecord, double, 4> index;
    bool threw = false;
    try {
        index.bulk_load(points);
    } catch (const talus::invalid_geometry&) {
        threw = true;
    }

    TALUS_CHECK(threw);
    TALUS_CHECK(index.empty());

    index.insert(PointRecord{5.0, 5.0, 9});
    TALUS_CHECK(index.size() == 1);
}

// Test: test_spatial_index_bulk_load_moves_vector_values
// Verifies the rvalue-vector overload move-constructs stored values (payloads
// arrive intact) instead of requiring copies from the caller's container.
void test_spatial_index_bulk_load_moves_vector_values() {
    std::vector<NamedPoint> points{
        {1.0, 1.0, "alpha"},
        {2.0, 2.0, "beta"},
        {3.0, 3.0, "gamma"},
        {4.0, 4.0, "delta"},
        {5.0, 5.0, "epsilon"},
    };

    talus::SpatialIndex<NamedPoint, double, 4> index;
    index.bulk_load(std::move(points));

    TALUS_CHECK(index.size() == 5);
    const auto matches = index.search(Box{{2.0, 2.0}, {4.0, 4.0}});
    std::vector<std::string> names;
    for (const NamedPoint& point : matches) {
        names.push_back(point.name);
    }
    std::sort(names.begin(), names.end());
    TALUS_CHECK(names == (std::vector<std::string>{"beta", "delta", "gamma"}));
}

// Test: test_spatial_index_bulk_load_then_mutate_matches_brute_force
// Verifies a bulk-loaded tree composes with later incremental mutation:
// inserts and erases after the load keep matching the brute-force oracle, so
// the packed structure upholds every invariant the dynamic algorithms rely on.
void test_spatial_index_bulk_load_then_mutate_matches_brute_force() {
    std::mt19937 rng(31);
    std::uniform_real_distribution<double> coord(-50.0, 50.0);

    std::vector<PointRecord> points;
    for (int id = 0; id < 200; ++id) {
        points.push_back(PointRecord{coord(rng), coord(rng), id});
    }

    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    index.bulk_load(points);
    for (const PointRecord& point : points) {
        oracle.insert(point);
    }

    // Erase a deterministic-but-scattered third of the loaded values, then
    // insert fresh ones.
    for (std::size_t i = 0; i < points.size(); i += 3) {
        TALUS_CHECK(index.erase(points[i]) == oracle.erase(points[i]));
    }
    for (int id = 200; id < 260; ++id) {
        const PointRecord point{coord(rng), coord(rng), id};
        index.insert(point);
        oracle.insert(point);
    }

    TALUS_CHECK(index.size() == oracle.size());
    for (int i = 0; i < 30; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const Box query{{x, y}, {x + 20.0, y + 20.0}};
        assert_same_ids(index.search(query), oracle.search(query));
    }
}

// Test: test_spatial_index_visitor_search_matches_vector_search
// Verifies the visitor overload of `search` visits exactly the values the
// vector-returning overload collects (randomized against the brute-force
// oracle) and returns the number of visited matches.
void test_spatial_index_visitor_search_matches_vector_search() {
    std::mt19937 rng(97);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);

    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    for (int id = 0; id < 300; ++id) {
        const PointRecord point{coord(rng), coord(rng), id};
        index.insert(point);
        oracle.insert(point);
    }

    for (int i = 0; i < 40; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const Box query{{x, y}, {x + 25.0, y + 25.0}};

        std::vector<PointRecord> visited;
        const std::size_t count = index.search(query, [&](const PointRecord& value) {
            visited.push_back(value);
        });

        TALUS_CHECK(count == visited.size());
        assert_same_ids(visited, oracle.search(query));
    }
}

// Test: test_spatial_index_visitor_search_supports_predicates
// Verifies a visitor can apply a custom predicate during traversal (keep only
// even ids) and that the kept values equal the post-filtered oracle results,
// while the returned count still reflects every geometric match visited.
void test_spatial_index_visitor_search_supports_predicates() {
    std::mt19937 rng(101);
    std::uniform_real_distribution<double> coord(-50.0, 50.0);

    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    for (int id = 0; id < 200; ++id) {
        const PointRecord point{coord(rng), coord(rng), id};
        index.insert(point);
        oracle.insert(point);
    }

    const Box query{{-25.0, -25.0}, {25.0, 25.0}};

    std::vector<PointRecord> filtered;
    const std::size_t count = index.search(query, [&](const PointRecord& value) {
        if (value.id % 2 == 0) {
            filtered.push_back(value);
        }
    });

    std::vector<PointRecord> expected;
    for (const PointRecord& value : oracle.search(query)) {
        if (value.id % 2 == 0) {
            expected.push_back(value);
        }
    }

    TALUS_CHECK(count == oracle.search(query).size());
    assert_same_ids(filtered, expected);
}

// Test: test_spatial_index_visitor_search_stops_early
// Verifies a bool-returning visitor halts the traversal when it returns false:
// no further values are visited, the stopping value is included in the count,
// and a visitor that always returns true visits every match.
void test_spatial_index_visitor_search_stops_early() {
    talus::SpatialIndex<PointRecord, double, 4> index;
    for (int id = 0; id < 100; ++id) {
        index.insert(PointRecord{static_cast<double>(id), static_cast<double>(id), id});
    }

    const Box everything{{-1.0, -1.0}, {101.0, 101.0}};

    std::size_t visited = 0;
    const std::size_t count = index.search(everything, [&](const PointRecord&) {
        ++visited;
        return visited < 3;  // stop after the third visit
    });

    TALUS_CHECK(visited == 3);
    TALUS_CHECK(count == 3);

    std::size_t all = 0;
    const std::size_t full_count = index.search(everything, [&](const PointRecord&) {
        ++all;
        return true;
    });

    TALUS_CHECK(all == 100);
    TALUS_CHECK(full_count == 100);
}

// Test: test_spatial_index_visitor_radius_search_matches_vector
// Verifies the visitor overload of `radius_search` visits exactly the values
// the vector-returning overload collects (randomized against the brute-force
// oracle), and that a bool-returning visitor can stop the traversal early.
void test_spatial_index_visitor_radius_search_matches_vector() {
    std::mt19937 rng(103);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);
    std::uniform_real_distribution<double> radius_dist(0.0, 60.0);

    talus::SpatialIndex<PointRecord, double, 4> index;
    talus::test::BruteForceIndex<PointRecord, double> oracle;
    for (int id = 0; id < 300; ++id) {
        const PointRecord point{coord(rng), coord(rng), id};
        index.insert(point);
        oracle.insert(point);
    }

    for (int i = 0; i < 40; ++i) {
        const talus::Point<double> query{coord(rng), coord(rng)};
        const double radius = radius_dist(rng);

        std::vector<PointRecord> visited;
        const std::size_t count = index.radius_search(query, radius,
            [&](const PointRecord& value) {
                visited.push_back(value);
            });

        TALUS_CHECK(count == visited.size());
        assert_same_ids(visited, oracle.radius_search(query, radius));
    }

    // Early stop: visiting everything within a huge radius, halt immediately.
    std::size_t stopped_visits = 0;
    const std::size_t stopped_count = index.radius_search(
        talus::Point<double>{0.0, 0.0}, 1000.0, [&](const PointRecord&) {
            ++stopped_visits;
            return false;
        });
    TALUS_CHECK(stopped_visits == 1);
    TALUS_CHECK(stopped_count == 1);
}

// Test: test_spatial_index_visitor_search_supports_move_only_values
// Verifies visitor queries work for move-only value types, which the
// vector-returning overloads cannot serve because they require copyable T.
// Also exercises the visitor `within` alias on an empty region.
void test_spatial_index_visitor_search_supports_move_only_values() {
    struct MoveOnlyRecord {
        double x = 0.0;
        double y = 0.0;
        std::unique_ptr<int> payload;
    };

    talus::SpatialIndex<MoveOnlyRecord, double, 4> index;
    for (int id = 0; id < 20; ++id) {
        index.insert(MoveOnlyRecord{
            static_cast<double>(id), static_cast<double>(id),
            std::make_unique<int>(id)});
    }

    int payload_sum = 0;
    const std::size_t count = index.search(Box{{0.0, 0.0}, {4.0, 4.0}},
        [&](const MoveOnlyRecord& record) {
            payload_sum += *record.payload;
        });

    TALUS_CHECK(count == 5);
    TALUS_CHECK(payload_sum == 0 + 1 + 2 + 3 + 4);

    const std::size_t none = index.within(Box{{50.0, 50.0}, {60.0, 60.0}},
        [](const MoveOnlyRecord&) {});
    TALUS_CHECK(none == 0);
}

// Test: test_spatial_index_visitor_queries_on_empty_index
// Verifies visitor search and radius search on an empty index visit nothing
// and return zero, without touching a (nonexistent) root.
void test_spatial_index_visitor_queries_on_empty_index() {
    const talus::SpatialIndex<PointRecord, double, 4> index;

    std::size_t visits = 0;
    TALUS_CHECK(index.search(Box{{0.0, 0.0}, {10.0, 10.0}},
        [&](const PointRecord&) { ++visits; }) == 0);
    TALUS_CHECK(index.radius_search(talus::Point<double>{0.0, 0.0}, 5.0,
        [&](const PointRecord&) { ++visits; }) == 0);
    TALUS_CHECK(visits == 0);
}

} // namespace

// Test: test_spatial_index_custom_extractor_matches_brute_force
// Verifies an index over an opaque type (no .x/.y, .lat/.lon, or .bounds())
// using a custom CoordExtractor produces the same search, radius-search,
// nearest-neighbor, k-nearest, and erase results as the brute-force oracle
// equipped with the same extractor, and that bulk_load extracts bounds
// through the custom extractor as well.
void test_spatial_index_custom_extractor_matches_brute_force() {
    talus::SpatialIndex<PackedRecord, double, 4, PackedRecordExtractor> index;
    talus::test::BruteForceIndex<PackedRecord, double, PackedRecordExtractor> oracle;

    std::mt19937 rng(20260611U);
    std::uniform_real_distribution<double> coord(-50.0, 50.0);

    std::vector<PackedRecord> records;
    for (int id = 1; id <= 200; ++id) {
        const PackedRecord record{{coord(rng), coord(rng)}, id};
        records.push_back(record);
        index.insert(record);
        oracle.insert(record);
    }
    TALUS_CHECK(index.size() == oracle.size());

    for (int i = 0; i < 50; ++i) {
        const talus::Point<double> a{coord(rng), coord(rng)};
        const talus::Point<double> b{coord(rng), coord(rng)};
        const Box query{
            {std::min(a.x, b.x), std::min(a.y, b.y)},
            {std::max(a.x, b.x), std::max(a.y, b.y)}
        };
        assert_same_ids(index.search(query), oracle.search(query));
        assert_same_ids(index.radius_search(a, 10.0), oracle.radius_search(a, 10.0));
        assert_same_optional_id(index.nearest_neighbor(a), oracle.nearest_neighbor(a));
        assert_same_ids(index.nearest_neighbors(a, 5), oracle.nearest_neighbors(a, 5));
    }

    // Erase resolves bounds through the custom extractor too.
    for (std::size_t i = 0; i < records.size(); i += 2) {
        TALUS_CHECK(index.erase(records[i]));
        TALUS_CHECK(oracle.erase(records[i]));
    }
    const Box everything{{-100.0, -100.0}, {100.0, 100.0}};
    assert_same_ids(index.search(everything), oracle.search(everything));

    talus::SpatialIndex<PackedRecord, double, 4, PackedRecordExtractor> bulk;
    bulk.bulk_load(records);
    assert_same_ids(bulk.search(everything), records);
}

// Test: test_spatial_index_stateful_extractor_resolves_external_table
// Verifies the SpatialIndex(Extractor) constructor overload: stored values
// are plain ids whose coordinates live in an external table captured by the
// extractor, queries and erase resolve through that state, and the extractor
// state survives a move of the index.
void test_spatial_index_stateful_extractor_resolves_external_table() {
    const std::vector<talus::Point<double>> table{
        {0.0, 0.0}, {10.0, 0.0}, {0.0, 10.0}, {25.0, 25.0}
    };
    talus::SpatialIndex<int, double, 4, TableExtractor> index{TableExtractor{&table}};

    for (int id = 0; id < static_cast<int>(table.size()); ++id) {
        index.insert(id);
    }
    TALUS_CHECK(index.size() == table.size());

    const std::vector<int> near_origin = index.search(Box{{-1.0, -1.0}, {1.0, 1.0}});
    TALUS_CHECK(near_origin == std::vector<int>{0});

    const std::optional<int> nearest = index.nearest_neighbor({24.0, 24.0});
    TALUS_CHECK(nearest.has_value() && *nearest == 3);

    TALUS_CHECK(index.erase(2));
    TALUS_CHECK(index.search(Box{{-1.0, 9.0}, {1.0, 11.0}}).empty());

    // Moving the index must carry the extractor state along with the tree.
    talus::SpatialIndex<int, double, 4, TableExtractor> moved = std::move(index);
    const std::optional<int> still_nearest = moved.nearest_neighbor({9.0, 1.0});
    TALUS_CHECK(still_nearest.has_value() && *still_nearest == 1);
}

int main() {
    test_spatial_index_starts_empty_and_tracks_size();
    test_spatial_index_search_matches_brute_force_fixture();
    test_spatial_index_search_matches_bounded_geometry_oracle();
    test_spatial_index_accepts_move_inserted_values();
    test_spatial_index_randomized_search_matches_brute_force();
    test_spatial_index_erase_matches_brute_force_fixture();
    test_spatial_index_erase_one_of_duplicate_values();
    test_spatial_index_erase_to_empty_then_reuse();
    test_spatial_index_randomized_erase_matches_brute_force();
    test_spatial_index_grid_data_search_matches_brute_force();
    test_spatial_index_radius_search_matches_brute_force_fixture();
    test_spatial_index_radius_search_matches_bounded_geometry_oracle();
    test_spatial_index_randomized_radius_search_matches_brute_force();
    test_spatial_index_nearest_neighbor_matches_brute_force_fixture();
    test_spatial_index_nearest_neighbor_matches_bounded_geometry_oracle();
    test_spatial_index_randomized_nearest_neighbor_matches_brute_force();
    test_spatial_index_nearest_neighbors_matches_brute_force_fixture();
    test_spatial_index_randomized_nearest_neighbors_matches_brute_force();
    test_spatial_index_nearest_neighbor_returns_a_minimum_on_ties();
    test_spatial_index_is_movable();
    test_spatial_index_throws_on_invalid_geometry();
    test_spatial_index_enforces_coordinate_domain();
    test_spatial_index_handles_large_boxed_values();
    test_spatial_index_bulk_load_matches_brute_force();
    test_spatial_index_bulk_load_bounded_geometry_matches_brute_force();
    test_spatial_index_bulk_load_empty_range_is_noop();
    test_spatial_index_bulk_load_requires_empty_index();
    test_spatial_index_bulk_load_throws_on_invalid_geometry();
    test_spatial_index_bulk_load_moves_vector_values();
    test_spatial_index_bulk_load_then_mutate_matches_brute_force();
    test_spatial_index_visitor_search_matches_vector_search();
    test_spatial_index_visitor_search_supports_predicates();
    test_spatial_index_visitor_search_stops_early();
    test_spatial_index_visitor_radius_search_matches_vector();
    test_spatial_index_visitor_search_supports_move_only_values();
    test_spatial_index_visitor_queries_on_empty_index();
    test_spatial_index_custom_extractor_matches_brute_force();
    test_spatial_index_stateful_extractor_resolves_external_table();
    return 0;
}

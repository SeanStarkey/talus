#include <talus/rtree.hpp>

#include "brute_force.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <random>
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
    assert(sorted_ids(std::move(actual)) == sorted_ids(std::move(expected)));
}

// Test: test_spatial_index_starts_empty_and_tracks_size
// Verifies the public wrapper reports empty state, increments size after inserts,
// and clears back to an empty reusable tree.
void test_spatial_index_starts_empty_and_tracks_size() {
    talus::SpatialIndex<PointRecord, double, 4> index;

    assert(index.empty());
    assert(index.size() == 0);

    index.insert(PointRecord{1.0, 2.0, 1});
    index.insert(PointRecord{3.0, 4.0, 2});

    assert(!index.empty());
    assert(index.size() == 2);

    index.clear();

    assert(index.empty());
    assert(index.size() == 0);
    assert(index.search(Box{{0.0, 0.0}, {10.0, 10.0}}).empty());

    index.insert(PointRecord{5.0, 6.0, 3});
    assert(index.size() == 1);
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
    assert(matches.size() == 1);
    assert(matches.front().name == "alpha");
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

} // namespace

int main() {
    test_spatial_index_starts_empty_and_tracks_size();
    test_spatial_index_search_matches_brute_force_fixture();
    test_spatial_index_search_matches_bounded_geometry_oracle();
    test_spatial_index_accepts_move_inserted_values();
    test_spatial_index_randomized_search_matches_brute_force();
    return 0;
}

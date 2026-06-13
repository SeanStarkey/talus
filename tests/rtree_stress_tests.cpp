/// @file rtree_stress_tests.cpp
/// @brief Large randomized stress tests for the public SpatialIndex.
///
/// Every query result is diffed against the brute-force oracle, so this is a
/// correctness stress test, not a benchmark. The oracle cost is O(points) per
/// query, so total runtime scales with points x queries. The default scale
/// (100K points, 1K queries) keeps the suite fast enough for ctest, Debug, and
/// the Valgrind CI lane. The documented full-scale run (~1M points, ~10K
/// queries) is opt-in via environment variables:
///
///   TALUS_STRESS_POINTS=1000000 TALUS_STRESS_QUERIES=10000 ./build/tests/talus_rtree_stress_tests

#include <talus/rtree.hpp>

#include "brute_force.hpp"
#include "test_check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <vector>

namespace {

using Box = talus::BoundingBox<double>;

struct PointRecord {
    double x = 0.0;
    double y = 0.0;
    int id = 0;

    constexpr bool operator==(const PointRecord&) const noexcept = default;
};

constexpr std::size_t default_point_count = 100'000;
constexpr std::size_t default_query_count = 1'000;

// Reads a positive integer from the environment, falling back when the
// variable is unset, empty, zero, or not a clean base-10 number.
[[nodiscard]] std::size_t env_size(const char* name, std::size_t fallback) {
#ifdef _MSC_VER
    // std::getenv is standard C++; MSVC's C4996 deprecation nag is spurious here.
#  pragma warning(push)
#  pragma warning(disable : 4996)
#endif
    const char* text = std::getenv(name);
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
    if (text == nullptr || *text == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || parsed == 0) {
        return fallback;
    }
    return static_cast<std::size_t>(parsed);
}

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

// Test: test_spatial_index_stress_matches_brute_force
// The documented large stress test (PLAN section 6). Builds one index by
// repeated insert and a second by STR bulk load from the same random points,
// then diffs both against the brute-force oracle across a mixed query
// workload: rectangular search (~40% of the query budget), radius search
// (~30%), nearest neighbor (~20%), and k-nearest with randomized k (~10%,
// the smallest share because the oracle sorts all points per query). Finally
// it erases a random quarter of the points from the insert-built index and
// re-diffs rectangular searches, exercising condense/reinsert paths at scale.
// Coordinates are perturbed per id so distances are tie-free and the ordered
// nearest-neighbor comparisons are exact.
void test_spatial_index_stress_matches_brute_force(std::size_t point_count,
                                                   std::size_t query_count) {
    std::mt19937 rng(20260612U);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);
    std::uniform_real_distribution<double> extent(0.0, 100.0);
    std::uniform_real_distribution<double> radius(0.0, 100.0);

    std::vector<PointRecord> points;
    points.reserve(point_count);
    for (std::size_t i = 0; i < point_count; ++i) {
        // The small deterministic offset prevents exact ties from duplicate
        // generated coordinates while keeping the fixture effectively random.
        points.push_back(PointRecord{
            coord(rng) + static_cast<double>(i) * 1.0e-9,
            coord(rng) - static_cast<double>(i) * 1.0e-9,
            static_cast<int>(i)
        });
    }

    talus::SpatialIndex<PointRecord> inserted;
    talus::SpatialIndex<PointRecord> bulk_loaded;
    talus::test::BruteForceIndex<PointRecord> oracle;

    for (const PointRecord& point : points) {
        inserted.insert(point);
        oracle.insert(point);
    }
    bulk_loaded.bulk_load(points);

    TALUS_CHECK(inserted.size() == point_count);
    TALUS_CHECK(bulk_loaded.size() == point_count);
    TALUS_CHECK(oracle.size() == point_count);

    // Rectangular search: ~40% of the query budget against both trees.
    const std::size_t rect_queries = (query_count * 4) / 10;
    for (std::size_t i = 0; i < rect_queries; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const Box query{{x, y}, {x + extent(rng), y + extent(rng)}};
        const std::vector<PointRecord> expected = oracle.search(query);
        assert_same_ids(inserted.search(query), expected);
        assert_same_ids(bulk_loaded.search(query), expected);
    }

    // Radius search: ~30%.
    const std::size_t radius_queries = (query_count * 3) / 10;
    for (std::size_t i = 0; i < radius_queries; ++i) {
        const talus::Point<double> center{coord(rng), coord(rng)};
        const double r = radius(rng);
        const std::vector<PointRecord> expected = oracle.radius_search(center, r);
        assert_same_ids(inserted.radius_search(center, r), expected);
        assert_same_ids(bulk_loaded.radius_search(center, r), expected);
    }

    // Nearest neighbor: ~20%.
    const std::size_t nn_queries = (query_count * 2) / 10;
    for (std::size_t i = 0; i < nn_queries; ++i) {
        const talus::Point<double> query{coord(rng), coord(rng)};
        const std::optional<PointRecord> expected = oracle.nearest_neighbor(query);
        assert_same_optional_id(inserted.nearest_neighbor(query), expected);
        assert_same_optional_id(bulk_loaded.nearest_neighbor(query), expected);
    }

    // k-nearest: ~10%. The oracle sorts every point per query, so this is the
    // most expensive query type and gets the smallest share of the budget.
    const std::size_t knn_queries = query_count / 10;
    std::uniform_int_distribution<std::size_t> k_dist(1, 100);
    for (std::size_t i = 0; i < knn_queries; ++i) {
        const talus::Point<double> query{coord(rng), coord(rng)};
        const std::size_t k = k_dist(rng);
        const std::vector<PointRecord> expected = oracle.nearest_neighbors(query, k);
        assert_same_ordered_ids(inserted.nearest_neighbors(query, k), expected);
        assert_same_ordered_ids(bulk_loaded.nearest_neighbors(query, k), expected);
    }

    // Erase a random quarter of the points from the insert-built index, then
    // re-diff rectangular searches against the shrunken oracle.
    std::vector<std::size_t> order(points.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::shuffle(order.begin(), order.end(), rng);
    const std::size_t erase_count = point_count / 4;
    for (std::size_t i = 0; i < erase_count; ++i) {
        TALUS_CHECK(inserted.erase(points[order[i]]));
        TALUS_CHECK(oracle.erase(points[order[i]]));
    }
    TALUS_CHECK(inserted.size() == oracle.size());

    const std::size_t post_erase_queries = std::max<std::size_t>(query_count / 10, 1);
    for (std::size_t i = 0; i < post_erase_queries; ++i) {
        const double x = coord(rng);
        const double y = coord(rng);
        const Box query{{x, y}, {x + extent(rng), y + extent(rng)}};
        assert_same_ids(inserted.search(query), oracle.search(query));
    }
}

} // namespace

int main() {
    const std::size_t point_count = env_size("TALUS_STRESS_POINTS", default_point_count);
    const std::size_t query_count = env_size("TALUS_STRESS_QUERIES", default_query_count);
    std::printf("stress scale: %zu points, %zu queries\n", point_count, query_count);

    test_spatial_index_stress_matches_brute_force(point_count, query_count);
    return 0;
}

// rtree_benchmarks.cpp — microbenchmarks for the public SpatialIndex API.
//
// Dependency-free, like the test suite: a small steady_clock harness times
// insertion, STR bulk loading, rectangular search, and nearest-neighbor
// queries over uniformly distributed random points. Query benchmarks run
// against both an insert-built and a bulk-loaded tree so the effect of STR
// packing on query speed is visible directly.
//
// Usage: talus_benchmarks [size...]
//   size...  dataset sizes to benchmark (default: 1000 10000 100000)
//
// Each measurement repeats 5 times; the best and median repetitions are
// reported. Build with -DCMAKE_BUILD_TYPE=Release for meaningful numbers.

#include <talus/talus.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Scalar = double;
using Box = talus::BoundingBox<Scalar>;
using Pt = talus::Point<Scalar>;
using Clock = std::chrono::steady_clock;

// Generated points are uniform over [0, world_extent)^2; query windows are
// sized relative to this extent.
constexpr Scalar world_extent = 1000.0;

// Each rectangular query window covers 0.01% of the world area, so a query
// over n points matches ~n * 1e-4 of them on average.
constexpr Scalar query_window_side = world_extent / 100.0;

constexpr std::size_t queries_per_run = 1000;
constexpr int reps = 5;

struct BenchRecord {
    Scalar x = 0;
    Scalar y = 0;
    std::uint32_t id = 0;
};

using Index = talus::SpatialIndex<BenchRecord>;

// Accumulates benchmark results so the optimizer cannot discard the work.
volatile std::uint64_t g_sink = 0;

[[nodiscard]] std::vector<BenchRecord> make_records(std::size_t count) {
    std::mt19937_64 rng{20260611};
    std::uniform_real_distribution<Scalar> coord{0.0, world_extent};
    std::vector<BenchRecord> records;
    records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        records.push_back({coord(rng), coord(rng), static_cast<std::uint32_t>(i)});
    }
    return records;
}

[[nodiscard]] std::vector<Box> make_query_boxes(std::size_t count) {
    std::mt19937_64 rng{7177};
    std::uniform_real_distribution<Scalar> corner{0.0, world_extent - query_window_side};
    std::vector<Box> boxes;
    boxes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Pt min{corner(rng), corner(rng)};
        boxes.push_back({min, {min.x + query_window_side, min.y + query_window_side}});
    }
    return boxes;
}

[[nodiscard]] std::vector<Pt> make_query_points(std::size_t count) {
    std::mt19937_64 rng{40961};
    std::uniform_real_distribution<Scalar> coord{0.0, world_extent};
    std::vector<Pt> points;
    points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back({coord(rng), coord(rng)});
    }
    return points;
}

// Times one run of `body`, feeding its checksum to the volatile sink. The
// body does only the work being measured; setup and teardown stay outside.
template<typename Body>
[[nodiscard]] double timed(Body&& body) {
    const auto start = Clock::now();
    g_sink = g_sink + body();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Timing {
    double best_seconds = 0;
    double median_seconds = 0;
};

// Runs `sample` (which returns one timed duration in seconds) `reps` times
// and reports the best and median repetitions.
template<typename Sample>
[[nodiscard]] Timing measure(Sample&& sample) {
    std::vector<double> seconds(reps);
    for (double& s : seconds) {
        s = sample();
    }
    std::sort(seconds.begin(), seconds.end());
    return {seconds.front(), seconds[seconds.size() / 2]};
}

[[nodiscard]] std::string format_time(double seconds) {
    struct Unit {
        double scale;
        const char* suffix;
    };
    constexpr Unit units[] = {{1e-9, "ns"}, {1e-6, "us"}, {1e-3, "ms"}, {1.0, "s"}};

    const Unit* unit = &units[0];
    for (const Unit& candidate : units) {
        if (seconds >= candidate.scale) {
            unit = &candidate;
        }
    }

    std::ostringstream out;
    out << std::setprecision(3) << seconds / unit->scale << ' ' << unit->suffix;
    return out.str();
}

// One result row: per-op times divide by `ops` (values inserted/loaded, or
// queries executed); the total column is the whole median repetition.
void report(const std::string& name, std::size_t ops, Timing timing) {
    std::cout << "  " << std::left << std::setw(38) << name << std::right
              << std::setw(9) << ops
              << std::setw(13) << format_time(timing.best_seconds / static_cast<double>(ops))
              << std::setw(13) << format_time(timing.median_seconds / static_cast<double>(ops))
              << std::setw(13) << format_time(timing.median_seconds) << "\n";
}

void run_benchmarks(std::size_t size) {
    const std::vector<BenchRecord> records = make_records(size);
    const std::vector<Box> boxes = make_query_boxes(queries_per_run);
    const std::vector<Pt> points = make_query_points(queries_per_run);

    std::cout << "\nn = " << size << " uniform random points, "
              << queries_per_run << " queries per repetition\n";
    std::cout << "  " << std::left << std::setw(38) << "benchmark" << std::right
              << std::setw(9) << "ops"
              << std::setw(13) << "best/op"
              << std::setw(13) << "median/op"
              << std::setw(13) << "median total" << "\n";

    // insert: build the tree one value at a time. The index is constructed
    // and destroyed outside the timed region.
    report("insert", size, measure([&] {
        Index index;
        return timed([&] {
            for (const BenchRecord& record : records) {
                index.insert(record);
            }
            return static_cast<std::uint64_t>(index.size());
        });
    }));

    // bulk_load: STR-pack the same values into an empty index.
    report("bulk_load (STR)", size, measure([&] {
        Index index;
        return timed([&] {
            index.bulk_load(records);
            return static_cast<std::uint64_t>(index.size());
        });
    }));

    // Query benchmarks run against both tree shapes, built once per size.
    Index inserted;
    for (const BenchRecord& record : records) {
        inserted.insert(record);
    }
    Index bulk_loaded;
    bulk_loaded.bulk_load(records);

    // Rectangular search uses the visitor overload, so the measurement is
    // tree traversal and match visits without result-vector allocation.
    const auto bench_search = [&](const Index& index) {
        return measure([&] {
            return timed([&] {
                std::uint64_t matches = 0;
                for (const Box& query : boxes) {
                    matches += index.search(query, [](const BenchRecord&) {});
                }
                return matches;
            });
        });
    };
    report("search (insert-built)", queries_per_run, bench_search(inserted));
    report("search (bulk-loaded)", queries_per_run, bench_search(bulk_loaded));

    const auto bench_nn = [&](const Index& index) {
        return measure([&] {
            return timed([&] {
                std::uint64_t checksum = 0;
                for (const Pt& query : points) {
                    const std::optional<BenchRecord> nearest = index.nearest_neighbor(query);
                    if (nearest) {
                        checksum += nearest->id;
                    }
                }
                return checksum;
            });
        });
    };
    report("nearest_neighbor (insert-built)", queries_per_run, bench_nn(inserted));
    report("nearest_neighbor (bulk-loaded)", queries_per_run, bench_nn(bulk_loaded));

    const auto bench_knn = [&](const Index& index) {
        return measure([&] {
            return timed([&] {
                std::uint64_t checksum = 0;
                for (const Pt& query : points) {
                    for (const BenchRecord& record : index.nearest_neighbors(query, 10)) {
                        checksum += record.id;
                    }
                }
                return checksum;
            });
        });
    };
    report("nearest_neighbors k=10 (insert-built)", queries_per_run, bench_knn(inserted));
    report("nearest_neighbors k=10 (bulk-loaded)", queries_per_run, bench_knn(bulk_loaded));
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes;
    for (int i = 1; i < argc; ++i) {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || parsed == 0) {
            std::cerr << "usage: " << argv[0] << " [size...]\n"
                      << "  size...  positive dataset sizes (default: 1000 10000 100000)\n";
            return 1;
        }
        sizes.push_back(static_cast<std::size_t>(parsed));
    }
    if (sizes.empty()) {
        sizes = {1000, 10000, 100000};
    }

    std::cout << "Talus R*-tree microbenchmarks (MaxChildren = "
              << Index::max_children << ", " << reps << " repetitions)\n";
#ifndef NDEBUG
    std::cout << "warning: NDEBUG is not defined — this looks like a Debug build; "
                 "use -DCMAKE_BUILD_TYPE=Release for meaningful numbers\n";
#endif

    for (const std::size_t size : sizes) {
        run_benchmarks(size);
    }

    // Read the sink so its accumulation is observable behavior end to end.
    std::cout << "\nchecksum: " << g_sink << "\n";
    return 0;
}

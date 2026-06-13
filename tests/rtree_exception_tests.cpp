/// @file rtree_exception_tests.cpp
/// @brief Allocation-failure injection tests for `SpatialIndex::erase`.
///
/// This binary replaces the global allocation functions with a countdown
/// failure injector so it can force `std::bad_alloc` at every allocation
/// point inside `erase` and verify the documented basic exception guarantee.
/// Because the replacement operators are global to the executable, these
/// tests must stay in their own standalone test binary.

#include <talus/talus.hpp>

#include "test_check.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include <stdlib.h>

#ifdef _MSC_VER
// MSVC has no posix_memalign; aligned allocations use _aligned_malloc and
// must be released with _aligned_free.
#include <malloc.h>
#endif

namespace {

// Countdown failure injection shared by the replacement operators below.
// A negative countdown disarms injection; otherwise that many allocations
// succeed and every later armed allocation throws until disarm() is called.
long long g_allocations_until_failure = -1;
bool g_injection_fired = false;

void arm(long long allocations) noexcept {
    g_allocations_until_failure = allocations;
    g_injection_fired = false;
}

void disarm() noexcept {
    g_allocations_until_failure = -1;
}

[[nodiscard]] bool should_fail() noexcept {
    if (g_allocations_until_failure < 0) {
        return false;
    }
    if (g_allocations_until_failure == 0) {
        g_injection_fired = true;
        return true;
    }
    --g_allocations_until_failure;
    return false;
}

[[nodiscard]] void* checked_alloc(std::size_t size) {
    if (should_fail()) {
        throw std::bad_alloc{};
    }
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    return pointer;
}

[[nodiscard]] void* checked_aligned_alloc(std::size_t size, std::size_t alignment) {
    if (should_fail()) {
        throw std::bad_alloc{};
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    const std::size_t request = size == 0 ? alignment : size;
#ifdef _MSC_VER
    void* pointer = ::_aligned_malloc(request, alignment);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
#else
    void* pointer = nullptr;
    if (::posix_memalign(&pointer, alignment, request) != 0) {
        throw std::bad_alloc{};
    }
#endif
    return pointer;
}

// Release memory obtained from checked_aligned_alloc. On MSVC, _aligned_malloc
// memory must not be passed to std::free.
void aligned_free(void* pointer) noexcept {
#ifdef _MSC_VER
    ::_aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

} // namespace

void* operator new(std::size_t size) {
    return checked_alloc(size);
}

void* operator new[](std::size_t size) {
    return checked_alloc(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return checked_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return checked_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept {
    aligned_free(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept {
    aligned_free(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    aligned_free(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    aligned_free(pointer);
}

namespace {

using Box = talus::BoundingBox<double>;

struct PointRecord {
    double x = 0.0;
    double y = 0.0;
    int id = 0;

    constexpr bool operator==(const PointRecord&) const noexcept = default;
};

using Index = talus::SpatialIndex<PointRecord, double, 4>;

constexpr Box everything{{-1.0e9, -1.0e9}, {1.0e9, 1.0e9}};

[[nodiscard]] std::vector<PointRecord> make_fixture_points() {
    // The allocation-failure sweep below rebuilds and drains this whole fixture
    // once per induced failure point, and on a slow debug build (MSVC Debug)
    // checked iterators inflate the per-drain allocation count, so the cost is
    // many minutes at full scale. Shrink it hard there to 8 points — still
    // larger than MaxChildren (4), so a node split, condensing, and forced
    // reinsertion are all still exercised.
    constexpr int cluster_count = talus::test::scaled_workload(6, 2);
    constexpr int per_cluster = talus::test::scaled_workload(8, 4);
    std::vector<PointRecord> points;
    int id = 1;
    for (int cluster = 0; cluster < cluster_count; ++cluster) {
        for (int i = 0; i < per_cluster; ++i) {
            points.push_back({
                cluster * 100.0 + i,
                cluster * 100.0 + i * 2.0,
                id++
            });
        }
    }
    return points;
}

// Test: test_erase_allocation_failure_keeps_index_consistent
// Sweeps an allocation-failure countdown across every allocation point hit
// while draining an index through repeated erases, and verifies the documented
// basic guarantee on each induced std::bad_alloc: the in-flight target is
// gone (it is detached before the first allocation), size() exactly matches
// the entries still reachable by search, surviving entries are a duplicate-free
// subset of the values not yet erased, and the index keeps answering queries.
// The sweep ends at the first countdown large enough that a full drain
// completes without the injector firing, proving all failure points were hit.
void test_erase_allocation_failure_keeps_index_consistent() {
    const std::vector<PointRecord> points = make_fixture_points();
    bool any_failure_injected = false;
    bool sweep_completed = false;

    for (long long fail_after = 0; fail_after < 100000; ++fail_after) {
        Index index;
        for (const PointRecord& point : points) {
            index.insert(point);
        }

        std::vector<bool> erased(points.size(), false);
        bool threw = false;

        arm(fail_after);
        try {
            for (std::size_t i = 0; i < points.size(); ++i) {
                TALUS_CHECK(index.erase(points[i]));
                erased[i] = true;
            }
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        disarm();

        if (threw) {
            // The in-flight target is detached before erase's first possible
            // allocation, so it is gone even though the call threw.
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (!erased[i]) {
                    erased[i] = true;
                    break;
                }
            }
        }

        const std::vector<PointRecord> survivors = index.search(everything);
        TALUS_CHECK(index.size() == survivors.size());

        std::vector<bool> seen(points.size(), false);
        for (const PointRecord& record : survivors) {
            TALUS_CHECK(record.id >= 1);
            TALUS_CHECK(static_cast<std::size_t>(record.id) <= points.size());
            const std::size_t i = static_cast<std::size_t>(record.id) - 1;
            TALUS_CHECK(record == points[i]);
            TALUS_CHECK(!erased[i]);
            TALUS_CHECK(!seen[i]);
            seen[i] = true;
        }

        if (threw) {
            any_failure_injected = true;
        } else {
            TALUS_CHECK(!g_injection_fired);
            TALUS_CHECK(survivors.empty());
            TALUS_CHECK(index.empty());
            sweep_completed = true;
            break;
        }
    }

    TALUS_CHECK(any_failure_injected);
    TALUS_CHECK(sweep_completed);
}

// Test: test_erase_allocation_failure_index_remains_usable
// Verifies that after a recovered allocation failure the index still supports
// the full mutation API: inserts succeed, the reinserted value is findable,
// and a later un-injected erase completes normally.
void test_erase_allocation_failure_index_remains_usable() {
    const std::vector<PointRecord> points = make_fixture_points();

    Index index;
    for (const PointRecord& point : points) {
        index.insert(point);
    }

    // Fail the very first allocation inside erase to force a recovery path.
    bool threw = false;
    arm(0);
    try {
        for (const PointRecord& point : points) {
            TALUS_CHECK(index.erase(point));
        }
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    disarm();
    TALUS_CHECK(threw);

    const std::size_t recovered_size = index.size();
    TALUS_CHECK(index.search(everything).size() == recovered_size);

    const PointRecord extra{-500.0, -500.0, 999};
    index.insert(extra);
    TALUS_CHECK(index.size() == recovered_size + 1);

    const std::vector<PointRecord> found = index.search(Box{{-501.0, -501.0}, {-499.0, -499.0}});
    TALUS_CHECK(found.size() == 1);
    TALUS_CHECK(found.front() == extra);

    TALUS_CHECK(index.erase(extra));
    TALUS_CHECK(index.size() == recovered_size);
}

// Returns true when the replacement allocation operators are actually in
// effect. Tools like Valgrind redirect the global allocation symbols to their
// own interceptors, which silently disables countdown injection; probing at
// runtime lets the binary skip under such tools instead of failing its
// "injection actually fired" assertions.
[[nodiscard]] bool injection_available() {
    bool fired = false;
    arm(0);
    try {
        delete new int{0};
    } catch (const std::bad_alloc&) {
        fired = true;
    }
    disarm();
    return fired;
}

} // namespace

int main() {
    if (!injection_available()) {
        std::fprintf(stderr,
            "allocation-failure injection unavailable (global allocator symbols "
            "intercepted, e.g. by Valgrind); skipping\n");
        return 0;
    }

    test_erase_allocation_failure_keeps_index_consistent();
    test_erase_allocation_failure_index_remains_usable();
    return 0;
}

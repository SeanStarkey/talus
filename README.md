# Talus

**Solid footing for fast geospatial queries.**

A zero-dependency, header-only C++20 spatial index library. Drop it in, include one header, and start querying — no adapter traits, no build system integration, no Boost required.

[![CI](https://github.com/SeanStarkey/talus/actions/workflows/ci.yml/badge.svg)](https://github.com/SeanStarkey/talus/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

> **Status: early development (v0.1.0, in progress).** Working today: automatic
> type detection, `insert`, and rectangle (`within`) queries, validated against a
> brute-force oracle. Nearest-neighbor, radius search, delete, bulk loading, and the
> k-d tree are **planned, not yet implemented** — see the [Roadmap](#roadmap).
> [PLAN.md](PLAN.md) is the source of truth for what is and isn't done.

---

## Quick start

```cpp
#include <talus/talus.hpp>

struct Restaurant {
    double x, y;        // Talus detects these automatically — no boilerplate
    std::string name;
};

int main() {
    talus::SpatialIndex<Restaurant> index;

    index.insert({-104.82, 38.83, "Café on Tejon"});
    index.insert({-104.81, 38.84, "Pizza on Pikes"});
    index.insert({-104.79, 38.81, "Tacos on Tijeras"});

    // Rectangle (range) query — returns std::vector<Restaurant>
    auto results = index.within({{-104.85, 38.80}, {-104.78, 38.88}});

    // Nearest-neighbor and radius search are on the roadmap (not yet available).
}
```

---

## Why Talus?

| | Talus | Boost.Geometry | nanoflann |
|---|---|---|---|
| Zero dependencies | ✓ | ✗ | ✓ |
| Header-only | ✓ | ✓ | ✓ |
| No adapter boilerplate | ✓ | ✗ | ✗ |
| Range queries | ✓ | ✓ | ✗ |
| Nearest neighbor | ◐ | ✓ | ✓ |
| Radius search | ◐ | ✓ | partial |
| R*-tree | ✓ | ✓ | ✗ |
| k-d tree | ◐ | ✗ | ✓ |
| C++20 concepts API | ✓ | ✗ | ✗ |

**Talus column: ✓ available now · ◐ planned ([Roadmap](#roadmap)).** Competitor
columns describe their released features. The R\*-tree ships with insert and range
query today; forced reinsertion and delete are still on the roadmap.

---

## Installation

### FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(talus
    GIT_REPOSITORY https://github.com/SeanStarkey/talus
    GIT_TAG        main)  # no tagged release yet; v0.1.0 is the first planned tag
FetchContent_MakeAvailable(talus)

target_link_libraries(my_project PRIVATE talus::talus)
```

### Subdirectory

```cmake
add_subdirectory(talus)
target_link_libraries(my_project PRIVATE talus::talus)
```

### Manual

Copy `include/talus/` into your project. Add it to your include path. Done.

---

## Automatic type detection

Talus uses C++20 concepts to detect your type's coordinate fields with no boilerplate:

**Structs with `.x` / `.y` fields**
```cpp
struct Vertex { double x, y; int id; };
talus::SpatialIndex<Vertex> index;
```

**Structs with `.lat` / `.lon` fields**
```cpp
struct Waypoint { float lat, lon; std::string label; };
talus::SpatialIndex<Waypoint> index;
```

**Geometries with a `.bounds()` method** (segments, polygons, etc.)
```cpp
struct Building {
    talus::BoundingBox<> footprint;
    talus::BoundingBox<> bounds() const { return footprint; }
};
talus::SpatialIndex<Building> index;
```

**Exotic types — explicit extractor** *(planned, not yet available)*

A `CoordExtractor` concept exists, but passing a custom extractor to
`SpatialIndex` is still on the roadmap. Until then, give your type a `.bounds()`
method (above) to index types that lack `.x/.y` or `.lat/.lon` fields.

---

## API reference

### Available now

```cpp
talus::SpatialIndex<T, Scalar = double, MaxChildren = 9>
// Move-only: movable (noexcept) but not copyable.

// Insertion (one value at a time)
void insert(const T& value);   // requires copy-constructible T
void insert(T&& value);

// State
std::size_t size() const noexcept;
bool        empty() const noexcept;
void        clear() noexcept;

// Rectangle query — returns std::vector<T> (requires copy-constructible T)
std::vector<T> search(BoundingBox<Scalar> query) const;
std::vector<T> within(BoundingBox<Scalar> query) const;  // alias for search
```

### Planned (not yet implemented)

```cpp
// Bulk insertion via STR bulk load
template<std::ranges::input_range R> void insert(R&& range);

// Removal (delete + forced reinsertion)
bool remove(const T& value);

// Nearest-neighbor and radius queries
std::vector<T> nearest(Point<Scalar> query, std::size_t k = 1) const;
std::vector<T> within_radius(Point<Scalar> query, Scalar radius) const;

// Allocation-free visitor variants, for hot paths
template<std::invocable<const T&> Fn>
void for_each_within(BoundingBox<Scalar> query, Fn&& fn) const;

template<std::invocable<const T&> Fn>
void for_each_nearest(Point<Scalar> query, std::size_t k, Fn&& fn) const;
```

---

## Building and testing

Requires CMake 3.20+ and a C++20-capable compiler (GCC 12+, Clang 15+, MSVC 2022, Apple Clang 15+).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are built by default at the top level. They are plain executables (no
external test framework) whose checks always run — they use a local `TALUS_CHECK`
macro that is not stripped by `NDEBUG`, so the suite is meaningful in any build
configuration, including `Release`. Every index operation is validated against a
brute-force linear-scan oracle.

Talus's own targets build with `-Wall -Wextra -Wpedantic` by default (`/W4` on
MSVC); add `-DTALUS_WARNINGS_AS_ERRORS=ON` to make warnings fatal. To run the
suite under AddressSanitizer + UndefinedBehaviorSanitizer:

```bash
cmake -B build-asan -DTALUS_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

On Linux the suite can also run under Valgrind (Memcheck), which catches reads of
uninitialized memory that the sanitizers do not. It needs a plain, non-sanitized
build (Valgrind and ASan are incompatible):

```bash
cmake -B build-vg -DCMAKE_BUILD_TYPE=Debug
cmake --build build-vg
for t in build-vg/tests/talus_*; do valgrind --error-exitcode=1 --leak-check=full "$t"; done
```

CI (GitHub Actions) builds and tests with GCC and Clang under `-Werror`, runs the
ASan+UBSan suite, and runs the tests under Valgrind, on every push and pull request.

## Example driver

Talus includes a small command-line driver that exercises the currently exposed
public API. It loads seeded example geometries, can import additional JSON data,
and lets you list records, run bounding-box searches, clear the index, and view
the supported import format.

```bash
cmake -B build -DTALUS_BUILD_EXAMPLES=ON
cmake --build build --target talus_driver
./build/examples/talus_driver
```

To run it with the sample JSON import file:

```bash
./build/examples/talus_driver examples/sample_geometries.json
```

---

## Roadmap

- [x] Geometry primitives (`Point`, `BoundingBox`, `Segment`)
- [x] C++20 concept-based type detection
- [x] R*-tree core (insert, range query)
- [ ] Nearest neighbor and radius search
- [ ] Delete and reinsertion
- [ ] STR bulk loading
- [ ] k-d tree
- [ ] Benchmarks vs Boost.Geometry and nanoflann

---

## License

MIT — see [LICENSE](LICENSE).

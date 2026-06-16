# Talus

**Solid footing for fast geospatial queries.**

A zero-dependency, header-only C++20 spatial index library. Drop it in, include one header, and start querying — no adapter traits, no build system integration, no Boost required. Continuously tested on Linux, macOS, and Windows (GCC, Clang, and MSVC).

[![CI](https://github.com/SeanStarkey/talus/actions/workflows/ci.yml/badge.svg)](https://github.com/SeanStarkey/talus/actions/workflows/ci.yml)
[![Platforms: Linux | macOS | Windows](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-blue.svg)](https://github.com/SeanStarkey/talus/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

> **Status: early development (v0.1.0, in progress).** Working today: automatic
> type detection, custom coordinate extractors for types Talus cannot detect,
> `insert`, STR bulk loading with `bulk_load`, rectangle
> (`within`) queries, radius searches, nearest-neighbor and k-nearest queries,
> visitor-based queries with custom predicates and early termination, and
> deletion with `erase`, validated against a brute-force oracle, plus a
> dependency-free microbenchmark suite. The umbrella header also exposes
> `TALUS_VERSION_MAJOR/MINOR/PATCH`, `TALUS_VERSION`, and
> `TALUS_VERSION_STRING` for vendored-header and conditional-compilation use.
> The k-d tree
> is **planned, not yet implemented** — see the [Roadmap](#roadmap).
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

    // Single nearest-neighbor query — returns std::optional<Restaurant>
    auto nearest = index.nearest_neighbor({-104.80, 38.82});

    // k-nearest query — returns std::vector<Restaurant>, nearest first
    auto top_two = index.nearest_neighbors({-104.80, 38.82}, 2);

    // Radius query — returns std::vector<Restaurant>
    auto nearby = index.radius_search({-104.80, 38.82}, 0.03);

    // Visitor query — apply any predicate in place, no result vector built;
    // return false from a bool visitor to stop the traversal early
    std::size_t cafes = 0;
    index.search({{-104.85, 38.80}, {-104.78, 38.88}}, [&](const Restaurant& r) {
        if (r.name.starts_with("Café")) ++cafes;
    });

    // Delete one equal stored value — returns true when a value was removed
    bool erased = index.erase({-104.81, 38.84, "Pizza on Pikes"});
}
```

---

## Why Talus?

| | Talus | Boost.Geometry | nanoflann | RTree.h |
|---|---|---|---|---|
| Zero dependencies | ✓ | ✗ | ✓ | ✓ |
| Header-only | ✓ | ✓ | ✓ | ✓ |
| No adapter boilerplate | ✓ | ✗ | ✗ | ✗ |
| C++20 concepts API | ✓ | ✗ | ✗ | ✗ |
| Range queries | ✓ | ✓ | ✗ | ✓ |
| Nearest neighbor | ✓ | ✓ | ✓ | ✗ |
| k-nearest (k>1) | ✓ | ✓ | ✓ | ✗ |
| Radius search | ✓ | ✓ | ✓ | ✗ |
| Custom query predicates / visitor | ✓ | ✓ | ✗ | ✓ |
| R*-tree | ✓ | ✓ | ✗ | ✗ |
| Header version macro | ✓ | ✓ | ✓ | ✗ |
| Delete / removal | ✓ | ✓ | ✗ | ✓ |
| Bulk loading | ✓ | ✓ | ✓ | ✗ |
| k-d tree | ◐ | ✗ | ✓ | ✗ |
| N-dimensional (>2D) | ◐ | ✓ | ✓ | ✓ |
| Serialization | ◐ | ✓ | ✓ | ✓ |

**Talus column: ✓ available now · ◐ planned ([Roadmap](#roadmap)).** Competitor
columns describe their released features. Talus ships insert, STR bulk loading,
range query, radius search, nearest-neighbor and k-nearest queries, visitor
queries with custom predicates, deletion, a custom coordinate extractor escape
hatch for opaque types, header version macros, and documented thread-safety
guarantees today; N-dimensional support and serialization are longer-range
(post-1.0) items.
*RTree.h* is the widely-vendored single-header R-tree (Guttman-style, e.g.
`nushoin/RTree`): a plain R-tree with a callback-based rectangle search, removal,
and save/load, configured through raw template parameters and min/max arrays
rather than type detection.

Out of scope for this table: heavier standalone R-tree libraries
([libspatialindex](https://libspatialindex.org/)) and approximate
nearest-neighbor libraries for high-dimensional vector search (FLANN, Annoy,
hnswlib) solve a different problem than Talus's exact, low-dimensional queries.

---

## Installation

### FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(talus
    GIT_REPOSITORY https://github.com/SeanStarkey/talus
    GIT_TAG        main)  # use a release tag such as v0.1.0 once published
FetchContent_MakeAvailable(talus)

target_link_libraries(my_project PRIVATE talus::talus)
```

Talus's CI includes a downstream smoke test for this path.

### Installed package

```cmake
find_package(talus CONFIG REQUIRED)
target_link_libraries(my_project PRIVATE talus::talus)
```

The installed package exports the same `talus::talus` target and is checked in
CI by configuring a tiny downstream project against an installed prefix.

### Subdirectory

```cmake
add_subdirectory(talus)
target_link_libraries(my_project PRIVATE talus::talus)
```

### Manual

Copy `include/talus/` into your project. Add it to your include path. Done.

---

## Versioning and releases

Talus follows semantic versioning. The first stable API is planned for 1.0; until
then, 0.x releases may still include breaking public API changes while the
R*-tree surface hardens, and those changes are called out in
[CHANGELOG.md](CHANGELOG.md). Starting at 1.0, source-compatible updates stay
within the same major version, matching the installed CMake package config's
`SameMajorVersion` compatibility rule.

Every release has:

- a `CHANGELOG.md` entry with the release date and user-visible changes,
- synchronized `project(talus VERSION ...)` and `<talus/talus.hpp>` version
  macros,
- a passing CI run for the release commit,
- an annotated git tag named `vMAJOR.MINOR.PATCH`,
- a GitHub release whose notes come from the matching changelog entry.

Release checklist:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DTALUS_WARNINGS_AS_ERRORS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure

git tag -a vMAJOR.MINOR.PATCH -m "talus vMAJOR.MINOR.PATCH"
git push origin vMAJOR.MINOR.PATCH
```

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

**Exotic types — explicit extractor**

When a type matches none of the patterns above, supply a custom extractor —
any callable taking `const T&` and returning a `BoundingBox` — as the fourth
`SpatialIndex` template parameter:

```cpp
struct PackedStation {
    std::array<double, 2> position;  // no .x/.y, .lat/.lon, or .bounds()
    std::string name;
};

struct PackedStationExtractor {
    talus::BoundingBox<> operator()(const PackedStation& s) const {
        return {{s.position[0], s.position[1]}, {s.position[0], s.position[1]}};
    }
};

talus::SpatialIndex<PackedStation, double, 9, PackedStationExtractor> index;
```

Stateful extractors (e.g. one that looks coordinates up in an external table
keyed by the stored value) are passed to the constructor:
`SpatialIndex<Id, double, 9, TableExtractor> index{TableExtractor{&table}};`.
Extraction must be deterministic — `erase` re-extracts bounds from its argument
and requires them to match the stored bounds exactly.

---

## API reference

### Available now

```cpp
// Version macros from <talus/talus.hpp>
#define TALUS_VERSION_MAJOR 0
#define TALUS_VERSION_MINOR 1
#define TALUS_VERSION_PATCH 0
#define TALUS_VERSION 100  // major * 10000 + minor * 100 + patch
#define TALUS_VERSION_STRING "0.1.0"

talus::SpatialIndex<T, Scalar = double, MaxChildren = 9,
                    Extractor = DefaultExtractor<Scalar>>
// Move-only: movable but not copyable.

// Construction — the second overload carries a stateful custom extractor
SpatialIndex();
explicit SpatialIndex(Extractor extractor);

// Insertion (one value at a time)
void insert(const T& value);   // requires copy-constructible T
void insert(T&& value);

// Bulk loading (STR packing) — requires an empty index; call clear() first
// to replace existing contents
template<std::ranges::input_range R> void bulk_load(R&& values);  // copies
void bulk_load(std::vector<T>&& values);  // moves; supports move-only T

// State
std::size_t size() const noexcept;
bool        empty() const noexcept;
void        clear() noexcept;

// Removal — deletes one equal stored value, returns true on success
bool erase(const T& value);  // requires equality-comparable T

// Rectangle query — returns std::vector<T> (requires copy-constructible T)
std::vector<T> search(BoundingBox<Scalar> query) const;
std::vector<T> within(BoundingBox<Scalar> query) const;  // alias for search

// Radius query — returns std::vector<T> (requires copy-constructible T)
std::vector<T> radius_search(Point<Scalar> query, Scalar radius) const;

// Visitor queries — invoke the visitor with const T& for each match instead of
// returning a vector; work with move-only T. Return the number of values visited.
template<QueryVisitor<T> Visitor>
std::size_t search(BoundingBox<Scalar> query, Visitor&& visitor) const;
template<QueryVisitor<T> Visitor>
std::size_t within(BoundingBox<Scalar> query, Visitor&& visitor) const;  // alias
template<QueryVisitor<T> Visitor>
std::size_t radius_search(Point<Scalar> query, Scalar radius, Visitor&& visitor) const;

// Single nearest-neighbor query — returns std::optional<T> (requires copy-constructible T)
std::optional<T> nearest_neighbor(Point<Scalar> query) const;

// k-nearest query — returns std::vector<T> sorted nearest-first
// (requires copy-constructible T)
std::vector<T> nearest_neighbors(Point<Scalar> query, std::size_t k) const;
```

`insert`, `bulk_load`, `erase`, `search`, `within`, `radius_search`,
`nearest_neighbor`, and `nearest_neighbors` validate geometry at the boundary and throw
`talus::invalid_geometry` (a `std::invalid_argument`) when a coordinate is NaN
or infinite, a radius is negative, or a box has `min > max`. A rejected
`insert` leaves the index unchanged, and a rejected `erase` leaves the index
unchanged.

Stored values and distance-query points must also lie within
`talus::coordinate_limit<Scalar>()` (≈3.3e153 for `double`), and `radius` must
be small enough that `radius*radius` stays finite. This keeps the squared
Euclidean distances behind nearest-neighbor and radius queries from overflowing
to `+inf` (which would otherwise collapse the distance ordering and make
`radius_search` match the entire index); values or queries outside the domain
throw `invalid_geometry`. The limit is far beyond any real spatial dataset, and
rectangular `search`/`within` — which use comparisons, not distances — accept
any valid finite box.

`bulk_load` builds the tree bottom-up with the Sort-Tile-Recursive (STR)
algorithm — much faster than inserting values one at a time, and it produces a
better-packed tree. It requires an empty index and throws `std::logic_error`
otherwise. Every value's bounds are validated before the tree is touched, so an
`invalid_geometry` throw leaves the index unchanged; if building the tree
itself fails, the index is reset to a valid empty state.

`erase` removes one stored value equal to the argument. Bounds are extracted from
the argument first, so deletion requires both matching bounds and value equality.
If multiple equal values were inserted, each `erase` call removes one of them.

Thread safety: Talus does not perform internal locking. Multiple threads may
call `const` member functions on the same `SpatialIndex` concurrently, including
`size`, `empty`, `search`, `within`, `radius_search`, `nearest_neighbor`, and
`nearest_neighbors`, as long as no thread is mutating, moving, or destroying that
index at the same time. Any non-const operation (`insert`, `bulk_load`, `erase`,
`clear`, move assignment, or destruction) requires exclusive external
synchronization. User-provided value types and visitor callbacks must also avoid
their own data races.

Values are stored by value. Small values live inline in the tree nodes; values
larger than 128 bytes are automatically stored out of line, so large payloads
don't bloat the index's internal nodes. (For very large records, indexing a small
key and keeping the payload in a side table is still the most cache-friendly
pattern.)

`nearest_neighbors` returns the `k` stored values nearest to the query point,
sorted by ascending distance (measured to each value's bounding box, so a query
inside a stored box is at distance zero). When fewer than `k` values are stored
it returns them all; `k == 0` returns an empty vector. The order of equidistant
values — and which are kept when more than `k` tie at the k-th distance — is
unspecified.

`nearest_neighbor` (singular) returns the single closest value, or
`std::nullopt` when the index is empty. When two or more values are equidistant
at the minimum distance, which one is returned is unspecified.

The visitor overloads of `search`, `within`, and `radius_search` traverse the
tree without materializing a result vector: the visitor is invoked with
`const T&` for each match, in unspecified order, so it can filter on arbitrary
predicates, aggregate in place, or collect into its own container. A visitor
satisfies the `talus::QueryVisitor` concept by returning either `void` (every
match is visited) or a type convertible to `bool` — returning `false` stops the
traversal early, and the stopping value is included in the returned count.
Because no copies are made, visitor queries are the way to query indexes of
move-only types.

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

CI (GitHub Actions) builds and tests on every push and pull request across
**Linux** (GCC and Clang), **macOS** (Apple Clang), and **Windows** (MSVC) —
each in both Debug and Release with warnings treated as errors (`-Werror` /
`/WX`). Dedicated baseline lanes also prove the advertised minimum compiler
floor: GCC 12, Clang 15, Apple Clang 15, and MSVC 2022. CI additionally runs the
ASan+UBSan suite, the tests under Valgrind (Memcheck) on Linux, and downstream
consumption smoke tests through both `find_package(talus)` and `FetchContent`.

## Benchmarks

A dependency-free microbenchmark suite (no Google Benchmark — a small
`steady_clock` harness, like the framework-free tests) times insertion, STR
bulk loading, rectangular search, and nearest-neighbor / k-nearest queries
over uniformly distributed random points. Query benchmarks run against both an
insert-built and a bulk-loaded tree, so the effect of STR packing on query
speed is visible directly. Build in Release — numbers from unoptimized builds
are not meaningful:

```bash
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DTALUS_BUILD_BENCHMARKS=ON
cmake --build build-bench
./build-bench/benchmarks/talus_benchmarks            # default sizes: 1000 10000 100000
./build-bench/benchmarks/talus_benchmarks 1000000    # custom dataset sizes
```

Each measurement repeats 5 times and reports the best and median repetitions,
total and per operation. Comparative benchmarks against Boost.Geometry and
nanoflann are a separate roadmap item.

## Example driver

Talus includes a small command-line driver that exercises the currently exposed
public API. It loads seeded example geometries, can import additional JSON data,
and lets you list records, run bounding-box searches, run radius searches, find
the nearest geometry (or k nearest geometries) to a query point, run a
visitor-based filtered search (category predicate plus an early-stop match
cap), try the custom coordinate extractor demo (an index over a type whose
coordinates Talus cannot auto-detect), run a quick micro-benchmark over
synthetic random points (insert vs STR bulk load, plus query timings), erase a
geometry by id, clear the index, and view the supported import format.

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
- [x] R*-tree core (insert, range query, radius search, single nearest neighbor, delete)
- [x] k-nearest queries
- [x] Custom query predicates / visitor
- [x] Custom coordinate extractors (`CoordExtractor` escape hatch)
- [x] Delete and reinsertion
- [x] STR bulk loading
- [x] Microbenchmarks (insert, search, nearest neighbor, bulk load)
- [x] Header version macros
- [x] Release process and semantic versioning policy
- [x] Thread-safety guarantees
- [x] Install/consumption smoke tests
- [ ] k-d tree
- [ ] Benchmarks vs Boost.Geometry and nanoflann

### Longer-range / exploratory

- [ ] Index serialization (save/load) — builds on STR bulk loading; trivially
  copyable values first, with a user-provided hook for richer payloads
- [ ] N-dimensional points and boxes — a foundational change to the geometry
  primitives and type detection; likely a 2.0 effort

---

## License

MIT — see [LICENSE](LICENSE).

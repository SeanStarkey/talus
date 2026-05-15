# Talus

**Solid footing for fast geospatial queries.**

A zero-dependency, header-only C++20 spatial index library. Drop it in, include one header, and start querying — no adapter traits, no build system integration, no Boost required.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

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

    // Range query
    auto results = index.within({{-104.85, 38.80}, {-104.78, 38.88}});

    // Nearest neighbor
    auto nearest = index.nearest({-104.82, 38.83}, /*k=*/2);
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
| Nearest neighbor | ✓ | ✓ | ✓ |
| Radius search | ✓ | ✓ | partial |
| R*-tree | ✓ | ✓ | ✗ |
| k-d tree | ✓ | ✗ | ✓ |
| C++20 concepts API | ✓ | ✗ | ✗ |

---

## Installation

### FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(talus
    GIT_REPOSITORY https://github.com/SeanStarkey/talus
    GIT_TAG        v0.1.0)
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

**Exotic types — explicit extractor**
```cpp
struct Tile { float coords[4]; };

talus::SpatialIndex<Tile, double, 9, decltype(extractor)> index{
    [](const Tile& t) {
        return talus::BoundingBox{
            talus::Point{(double)t.coords[0], (double)t.coords[1]},
            talus::Point{(double)t.coords[2], (double)t.coords[3]}};
    }
};
```

---

## API reference

```cpp
talus::SpatialIndex<T, Scalar = double, MaxChildren = 9>

// Insertion
void insert(const T& value);
void insert(T&& value);
template<std::ranges::input_range R> void insert(R&& range);  // bulk — uses STR load

// Removal
bool remove(const T& value);

// State
std::size_t size() const noexcept;
bool        empty() const noexcept;
void        clear() noexcept;

// Queries — return std::vector<T>
std::vector<T> within(BoundingBox<Scalar> query) const;
std::vector<T> nearest(Point<Scalar> query, std::size_t k = 1) const;
std::vector<T> within_radius(Point<Scalar> query, Scalar radius) const;

// Visitor variants — no allocation, for hot paths
template<std::invocable<const T&> Fn>
void for_each_within(BoundingBox<Scalar> query, Fn&& fn) const;

template<std::invocable<const T&> Fn>
void for_each_nearest(Point<Scalar> query, std::size_t k, Fn&& fn) const;
```

---

## Building and testing

Requires CMake 3.20+ and a C++20-capable compiler (GCC 12+, Clang 15+, MSVC 2022, Apple Clang 15+).

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests fetch [Catch2](https://github.com/catchorg/Catch2) automatically. Every operation is validated against a brute-force linear-scan oracle.

---

## Roadmap

- [x] Geometry primitives (`Point`, `BoundingBox`, `Segment`)
- [x] C++20 concept-based type detection
- [ ] R*-tree core (insert, range query)
- [ ] Nearest neighbor and radius search
- [ ] Delete and reinsertion
- [ ] STR bulk loading
- [ ] k-d tree
- [ ] Benchmarks vs Boost.Geometry and nanoflann

---

## License

MIT — see [LICENSE](LICENSE).

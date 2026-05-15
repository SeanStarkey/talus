# Talus Development Plan

## Current State

Talus is currently a clean Phase 1 skeleton. The implemented project surface is:

- `include/talus/geometry.hpp`
- `include/talus/concepts.hpp`
- `CMakeLists.txt`

The R-tree implementation has not started yet. The following directories exist but are empty:

- `include/talus/detail/`
- `tests/`
- `examples/`
- `benchmarks/`

A lightweight GCC 13.3 header syntax check passed for the current geometry and concepts headers. CMake could not be validated in the current environment because `cmake` was not installed.

## Immediate Issues

1. Top-level CMake likely fails in a normal environment because `TALUS_BUILD_TESTS` defaults to `ON`, but `tests/` has no `CMakeLists.txt`.
2. `examples/` and `benchmarks/` have the same issue if `TALUS_BUILD_EXAMPLES` or `TALUS_BUILD_BENCHMARKS` are enabled.
3. `concepts.hpp` is currently hard-wired to `BoundingBox<double>` for `HasBounds`, `CoordExtractor`, and `bounding_box_of()`. This should be reconciled with the planned `SpatialIndex<T, Scalar, MaxChildren>` API.
4. Types satisfying both `Pointlike` and `HasBounds` may make `bounding_box_of()` overload resolution ambiguous.
5. The single-include public entry point `include/talus/talus.hpp` does not exist yet.

## Recommended Work Plan

### 1. Stabilize Build and Test Scaffolding

- Add `tests/CMakeLists.txt`, or guard `add_subdirectory(tests)` on the file existing.
- Add placeholder `examples/CMakeLists.txt` and `benchmarks/CMakeLists.txt`, or guard those subdirectories too.
- Add `include/talus/talus.hpp` that includes all public headers.
- Add basic smoke tests for `geometry.hpp` and `concepts.hpp`.

### 2. Harden Foundation APIs

- Decide whether the index internals are always `double` or whether `Scalar` is supported end-to-end.
- If `Scalar` is real, make coordinate extraction scalar-aware.
- Resolve `Pointlike && HasBounds` precedence explicitly.
- Add tests for:
  - point equality
  - bounding-box containment
  - edge-touching intersections
  - expansion
  - area
  - enlarged area
  - center
  - minimum squared distance
  - segment bounds
  - `.x/.y`, `.lat/.lon`, and `.bounds()` extraction

### 3. Implement R-tree Storage Primitives

- Add `include/talus/detail/pool_alloc.hpp`.
- Add `include/talus/detail/node.hpp`.
- Test allocation, reset/clear behavior, node alignment, node capacity, and leaf/internal invariants.
- Keep this layer independent from the higher-level tree algorithms where practical.

### 4. Implement the Minimal Public R-tree API

- Add `include/talus/rtree.hpp`.
- Introduce `SpatialIndex<T, Scalar, MaxChildren>`.
- Start with:
  - `insert`
  - `size`
  - `empty`
  - `clear`
  - rectangular `search`

Do not start nearest neighbor, delete, or bulk loading until insert/search correctness is solid.

### 5. Add the Brute-force Oracle

- Add `tests/brute_force.hpp`.
- Compare R-tree search results against brute force for deterministic fixtures.
- Add randomized search tests after deterministic tests are stable.
- Scale randomized tests gradually before attempting the documented large stress tests.

### 6. Implement Full R*-tree Algorithms Incrementally

Implement and test in this order:

1. ChooseLeaf
2. Insert
3. SplitNode
4. AdjustTree
5. Search
6. NearestNeighbor
7. Radius search
8. Delete
9. STR bulk load

Each step should have focused tests before moving to the next one.

### 7. Add Examples and Benchmarks

After correctness is established:

- Add examples for `.x/.y` point structs.
- Add examples for `.lat/.lon` structs.
- Add examples for bounded geometries.
- Add examples for custom coordinate extractors.
- Add benchmarks for insertion, rectangular search, nearest neighbor, and bulk loading.

## Next Concrete Task

Fix the build scaffolding and add foundation tests, then adjust `concepts.hpp` for the intended scalar model before implementing `pool_alloc.hpp`.

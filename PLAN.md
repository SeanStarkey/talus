# Talus Development Plan

## Current State

Talus is currently a clean Phase 1 skeleton. The implemented project surface is:

- `include/talus/geometry.hpp`
- `include/talus/concepts.hpp`
- `include/talus/talus.hpp`
- `CMakeLists.txt`

The R-tree implementation has not started yet. The following directory exists but is empty:

- `include/talus/detail/`

The following scaffolding directories currently contain placeholder CMake files:

- `examples/`
- `benchmarks/`

The test suite currently includes dependency-free foundation smoke tests for `geometry.hpp`, `concepts.hpp`, and the `talus.hpp` umbrella include.

CMake configure and build pass with default options, tests disabled, and examples/benchmarks enabled. `ctest` passes with the current foundation smoke test target.

## Immediate Issues

1. `concepts.hpp` is currently hard-wired to `BoundingBox<double>` for `HasBounds`, `CoordExtractor`, and `bounding_box_of()`. This should be reconciled with the planned `SpatialIndex<T, Scalar, MaxChildren>` API.
2. Types satisfying both `Pointlike` and `HasBounds` may make `bounding_box_of()` overload resolution ambiguous.

## Recommended Work Plan

### 1. Stabilize Build and Test Scaffolding

- Completed: add placeholder `tests/CMakeLists.txt`, `examples/CMakeLists.txt`, and `benchmarks/CMakeLists.txt`.
- Completed: add `include/talus/talus.hpp` that includes all current public headers.
- Completed: add basic smoke tests for `geometry.hpp`, `concepts.hpp`, and the umbrella include.

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

Adjust `concepts.hpp` for the intended scalar model before implementing `pool_alloc.hpp`.

# Talus Development Plan

## Current State

Talus has completed its Phase 1 foundation and is now in Phase 2 R*-tree core
work. The implemented project surface is:

- `include/talus/geometry.hpp`
- `include/talus/concepts.hpp`
- `include/talus/talus.hpp`
- `include/talus/detail/pool_alloc.hpp`
- `include/talus/detail/node.hpp`
- `include/talus/detail/algorithms.hpp`
- `CMakeLists.txt`

The R-tree implementation has started with storage primitives and the first
algorithm slice. Node layout, overlap-aware ChooseLeaf, and non-splitting Insert
are in place; SplitNode and higher-level tree adjustment are next.

The following scaffolding directories currently contain placeholder CMake files:

- `examples/`
- `benchmarks/`

The concept layer now supports scalar-aware bounding-box extraction and gives `.bounds()` precedence over point fields when a type satisfies both.

All current library headers now include file-level comment headers that describe
their purpose and ownership boundaries.

Public structures, concepts, helpers, and storage methods now include
Doxygen-style comments for generated API documentation.

The test suite currently includes dependency-free foundation smoke tests for `geometry.hpp`, `concepts.hpp`, and the `talus.hpp` umbrella include, plus focused pool allocator and R-tree node storage tests.

CMake configure, build, and `ctest` pass with the current foundation, pool allocator, and node storage test targets.

## Recommended Work Plan

### 1. Stabilize Build and Test Scaffolding

- Completed: add placeholder `tests/CMakeLists.txt`, `examples/CMakeLists.txt`, and `benchmarks/CMakeLists.txt`.
- Completed: add `include/talus/talus.hpp` that includes all current public headers.
- Completed: add basic smoke tests for `geometry.hpp`, `concepts.hpp`, and the umbrella include.

### 2. Harden Foundation APIs

- Completed: use `Scalar` for index internals and coordinate extraction.
- Completed: make `HasBounds`, `Indexable`, `CoordExtractor`, and `bounding_box_of()` scalar-aware, including cross-scalar bounded objects.
- Completed: resolve `Pointlike && HasBounds` precedence by preferring `.bounds()`.
- Completed: add tests for point equality, bounding-box containment, edge-touching intersections, expansion, area, enlarged area, center, minimum squared distance, segment bounds, `.x/.y`/`.lat/.lon`/`.bounds()` extraction, scalar-aware extraction, and `.bounds()` precedence over point fields.

### 3. Implement R-tree Storage Primitives

- Completed: add `include/talus/detail/pool_alloc.hpp`.
- Completed: add `include/talus/detail/node.hpp`.
- Completed: test pool allocation, reset/clear behavior, slot reuse, alignment, and capacity.
- Completed: test node alignment, node capacity, leaf/internal invariants, parent links, and non-trivial/move-only value storage.
- Completed: add and test pool reserve/preallocation for large builds.
- Completed: harden pool reserve block-count calculation against size overflow.
- Completed: harden pool destroy against interior pointers into valid blocks.
- Keep this layer independent from the higher-level tree algorithms where practical.
- Preserve a serialization-friendly and large-dataset-friendly design: keep persistent formats pointer-free, keep pool block size tunable, and avoid public APIs that expose node addresses as durable IDs.

### 4. Implement the R*-tree Algorithms

`include/talus/detail/algorithms.hpp` is the R-tree implementation. It contains
the tree logic that operates on `RTreeNode` objects from `node.hpp`. Implement
and test in this order:

1. Completed: refine ChooseLeaf to R*-tree ChooseSubtree semantics
   - Completed: basic minimum-area-enlargement descent
   - Completed: when choosing among leaf children, minimize overlap enlargement first, then area enlargement and area
2. Completed: Insert without split handling
3. SplitNode
4. AdjustTree
5. Search

The R*-tree overlap-aware ChooseLeaf behavior is covered by focused tests. Do
not start NearestNeighbor, Delete, or STR bulk load until Insert and Search
correctness is solid. Each step should have focused tests before moving to the
next one.

### 5. Add the Brute-force Oracle

- Add `tests/brute_force.hpp`.
- Compare R-tree search results against brute force for deterministic fixtures.
- Add randomized search tests after deterministic tests are stable.
- Scale randomized tests gradually before attempting the documented large stress tests.

### 6. Expose the Public R-tree API

`include/talus/rtree.hpp` is a thin wrapper around the algorithms. It owns the
root node and pool, and exposes the user-facing `SpatialIndex<T, Scalar,
MaxChildren>` API. Implement only after the algorithms in step 4 are correct.

Start with:
  - `insert`
  - `size`
  - `empty`
  - `clear`
  - rectangular `search`

### 7. Implement Remaining R*-tree Algorithms

After Insert and Search are solid, add to `algorithms.hpp`:

1. NearestNeighbor
2. Radius search
3. Delete
4. STR bulk load

### 8. Add Examples and Benchmarks

After correctness is established:

- Add examples for `.x/.y` point structs.
- Add examples for `.lat/.lon` structs.
- Add examples for bounded geometries.
- Add examples for custom coordinate extractors.
- Add benchmarks for insertion, rectangular search, nearest neighbor, and bulk loading.

## Next Concrete Task

Continue `include/talus/detail/algorithms.hpp` with SplitNode. Add focused
tests for splitting leaf nodes first, then internal nodes, and keep Insert/
AdjustTree split propagation scoped until those split invariants are solid.
`rtree.hpp` is the public wrapper that comes after internal insertion and search
are correct.

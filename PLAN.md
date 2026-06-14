# Talus Development Plan

## Current State

Talus has completed its Phase 1 foundation and is now in Phase 2 R*-tree core
work. The implemented project surface is:

- `include/talus/geometry.hpp`
- `include/talus/concepts.hpp`
- `include/talus/talus.hpp`
- `include/talus/rtree.hpp`
- `include/talus/detail/pool_alloc.hpp`
- `include/talus/detail/node.hpp`
- `include/talus/detail/algorithms.hpp`
- `CMakeLists.txt`

The top-level CMake project now provides a `clean-cmake` target that removes
the configured build tree's generated CMake/build-system files while refusing
to operate on in-source builds.

The R-tree implementation has storage primitives, the first algorithm slice, and
the minimal public wrapper. Node layout, overlap-aware ChooseLeaf, non-splitting
Insert, the low-level SplitNode primitive, AdjustTree split propagation, Search,
NearestNeighbor, k-nearest queries, RadiusSearch, Delete, STR bulk load, and public `SpatialIndex`
insert/search/nearest-neighbor/k-nearest/radius-search/erase/bulk-load are in place. ChooseSubtree and the
split-index selection use a margin (half-perimeter) tie-breaker so point and
axis-aligned data — where bounding boxes have zero area — are still ranked
spatially instead of collapsing to the entry-count fallback.

`benchmarks/` contains `talus_benchmarks` (`benchmarks/rtree_benchmarks.cpp`),
a dependency-free microbenchmark suite (built with `-DTALUS_BUILD_BENCHMARKS=ON`)
covering insertion, STR bulk loading, rectangular search, and
nearest-neighbor/k-nearest queries, with query benchmarks run against both
insert-built and bulk-loaded trees.

`examples/` now includes `talus_driver`, a menu-driven command-line
program that exercises the current public `SpatialIndex` API with seeded data,
rectangular search, nearest-neighbor and k-nearest queries, radius search,
erase-by-id deletion, clearing, size/empty reporting,
STR bulk-load rebuilds of the loaded records,
a custom coordinate extractor demo over a packed-array type, and
JSON import for mixed point, lat/lon, box, and segment records. The example datasets include Colorado
14er and ranked Colorado 13er summit coordinates.

The concept layer now supports scalar-aware bounding-box extraction and gives `.bounds()` precedence over point fields when a type satisfies both. `SpatialIndex` accepts a custom `CoordExtractor` as its fourth template parameter for types the concepts cannot detect; the default extractor preserves the zero-boilerplate detection path.

All current library headers now include file-level comment headers that describe
their purpose and ownership boundaries.

Public structures, concepts, helpers, and storage methods now include
Doxygen-style comments for generated API documentation.

The test suite currently includes dependency-free foundation smoke tests for `geometry.hpp`, `concepts.hpp`, and the `talus.hpp` umbrella include, focused pool allocator and R-tree node storage tests, R-tree algorithm tests, public `SpatialIndex` oracle tests, allocation-failure exception tests, and an environment-scalable large randomized stress test (`tests/rtree_stress_tests.cpp`). Test invariants are checked with a local `TALUS_CHECK` macro (`tests/test_check.hpp`) that always runs and is not stripped by `NDEBUG`, so the suite stays meaningful in `Release` as well as `Debug`.

CMake configure, build, and `ctest` pass with the current foundation, pool allocator, node storage, algorithm, and public R-tree test targets.

## Versioning and Release Sequencing

The R*-tree is feature-complete, so 1.0 ships the R*-tree alone rather than
waiting on the k-d tree. Version numbers follow semver as promised by the
package config (`talusConfig.cmake` advertises `SameMajorVersion`
compatibility): a major bump signals a breaking change to existing consumers,
not merely a large new feature.

- **1.0** — R*-tree, hardened (section 10). The supported, stable API.
- **1.1** — k-d tree (section 9). Purely additive: a new `KdTree` class and
  new headers, no change to the R*-tree / `SpatialIndex` API, so it is a minor
  bump. Shipping the R*-tree first also lets its API settle before the k-d
  tree mirrors that surface for interchangeability.
- **1.x** — serialization (save/load) and lossless `erase` (section 11). Also
  additive.
- **2.0** — N-dimensional points and boxes (section 11). The genuine major
  bump: a foundational rewrite of `geometry.hpp` and `concepts.hpp` that
  reshapes the public geometry API.

This holds only while the k-d tree slots in without forcing changes to shared
concepts or existing `SpatialIndex` signatures (the current plan reuses the
concept layer, so it should). If wiring it in cleanly reopens those APIs, the
minor-vs-major call for the k-d tree should be revisited.

## Recommended Work Plan

### 1. Stabilize Build and Test Scaffolding

- Completed: add placeholder `tests/CMakeLists.txt`, `examples/CMakeLists.txt`, and `benchmarks/CMakeLists.txt`.
- Completed: add `include/talus/talus.hpp` that includes all current public headers.
- Completed: add basic smoke tests for `geometry.hpp`, `concepts.hpp`, and the umbrella include.
- Completed: make test checks non-vacuous under `NDEBUG`/Release via a local always-on `TALUS_CHECK` macro (`tests/test_check.hpp`) in place of `assert`.
- Completed: build Talus's own targets with `-Wall -Wextra -Wpedantic` (internal `talus_dev_options` target), add opt-in `TALUS_WARNINGS_AS_ERRORS` and `TALUS_ENABLE_SANITIZERS` (ASan+UBSan), and a GitHub Actions CI lane (gcc/clang `-Werror`, a sanitizer job, and a Valgrind/Memcheck job for uninitialized-read coverage).

### 2. Harden Foundation APIs

- Completed: use `Scalar` for index internals and coordinate extraction.
- Completed: make `HasBounds`, `Indexable`, `CoordExtractor`, and `bounding_box_of()` scalar-aware, including cross-scalar bounded objects.
- Completed: resolve `Pointlike && HasBounds` precedence by preferring `.bounds()`.
- Completed: add tests for point equality, bounding-box containment, edge-touching intersections, expansion, area, enlarged area, center, minimum squared distance, segment bounds, `.x/.y`/`.lat/.lon`/`.bounds()` extraction, scalar-aware extraction, and `.bounds()` precedence over point fields.

### 3. Implement R-tree Storage Primitives

- Completed: add `include/talus/detail/pool_alloc.hpp`.
- Completed: add `include/talus/detail/node.hpp`.
- Completed: test pool allocation, reset/clear behavior, slot reuse, alignment, and capacity.
- Completed: test node alignment, node capacity, leaf/internal invariants, parent links, non-trivial/move-only value storage, and immovable value emplacement boundaries.
- Completed: add and test pool reserve/preallocation for large builds.
- Completed: harden pool reserve block-count calculation against size overflow.
- Completed: harden pool destroy against interior pointers into valid blocks.
- Completed: adaptive leaf value storage — values larger than `rtree_inline_value_max_size` (128 bytes) are boxed behind a `unique_ptr`; small values stay inline.
- Future (benchmark-gated): expose the inline/boxed threshold (`rtree_inline_value_max_size`, currently fixed at 128 bytes) as a tuning parameter. Prefer a defaulted template parameter threaded `SpatialIndex` → `RTreeNode` → `RTreeValueEntry` so the auto default and existing code stay unchanged; a compile-time macro override is a lighter alternative. Hold until the section-8 benchmarks can show the threshold affects real workloads — don't add the knob before there is evidence to tune against.
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
3. Completed: SplitNode
4. Completed: AdjustTree
5. Completed: Search
6. Completed: NearestNeighbor

The R*-tree overlap-aware ChooseLeaf behavior is covered by focused tests. Do
not start Delete or STR bulk load until Insert, Search, and NearestNeighbor
correctness is solid. Each step should have focused tests before moving to the
next one.

### 5. Expose the Public R-tree API

`include/talus/rtree.hpp` is a thin wrapper around the algorithms. It owns the
root node and pool, and exposes the user-facing `SpatialIndex<T, Scalar,
MaxChildren>` API. Implement only after the algorithms in step 4 are correct.

Completed:
  - `insert`, `size`, `empty`, `clear`
  - rectangular `search` and the `within` alias
  - `radius_search` and `nearest_neighbor`
  - `erase` (with CondenseTree), including allocation-before-mutation setup and
    basic-guarantee exception safety verified by `tests/rtree_exception_tests.cpp`
  - move construction / move assignment (copying stays deleted)
  - input validation via `talus::invalid_geometry`

### 6. Add the Brute-force Oracle

- Completed: add `tests/brute_force.hpp`.
- Completed: compare public `SpatialIndex` rectangular search results against brute force
  for deterministic fixtures.
- Completed: add randomized public API search tests after deterministic tests are stable.
- Completed: large stress test (`tests/rtree_stress_tests.cpp`) diffing insert-built and bulk-loaded indexes against the oracle across mixed search/radius/NN/k-NN/erase workloads; scale via `TALUS_STRESS_POINTS`/`TALUS_STRESS_QUERIES` (ctest default 100K/1K; full 1M/10K verified in Release, ~2 min; Valgrind CI lane runs it scaled down).

### 7. Implement Remaining R*-tree Algorithms

After Insert, Search, NearestNeighbor, and RadiusSearch are solid, add to `algorithms.hpp`:

1. Completed: Radius search
2. Completed: Delete
3. Completed: STR bulk load (`detail::str_bulk_load`, exposed as
   `SpatialIndex::bulk_load`)
4. Completed: k-nearest (k>1) queries (`detail::k_nearest_neighbors`, exposed
   as `SpatialIndex::nearest_neighbors(query, k)`)
5. Completed: custom query predicates / visitor traversal (the `QueryVisitor`
   concept plus visitor overloads of `search`/`within`/`radius_search`)

### 8. Add Examples and Benchmarks

After correctness is established:

- Completed: add a public-API R*-tree driver for `.x/.y` point structs,
  `.lat/.lon` point data, and bounded geometries.
- Completed: add example JSON import data for point, lat/lon, box, and segment
  geometries.
- Completed: add Colorado 14ers lat/lon sample JSON data for the driver.
- Completed: custom coordinate extractors — `CoordExtractor` wired into
  `SpatialIndex` as a fourth template parameter, with oracle tests and a
  driver menu demo.
- Completed: benchmarks for insertion, rectangular search, nearest neighbor
  (single and k=10), and bulk loading — dependency-free `talus_benchmarks`
  target in `benchmarks/rtree_benchmarks.cpp`, plus a quick-benchmark driver
  menu option.

### 9. Implement the k-d Tree

The second target structure, now scheduled for **1.1** (post-1.0; see
Versioning and Release Sequencing). A k-d tree indexes points only, so it
complements the R*-tree (which also handles boxes) and should be the faster
choice for static point datasets and nearest-neighbor-heavy workloads.

- Decide the structural design first: static (bulk-built, median-split, array
  packed — simplest and fastest to query) vs dynamic (insert/erase). Suggested
  scope for 1.0: build-from-range plus queries; defer dynamic mutation unless
  it falls out naturally.
- `include/talus/detail/kdtree_*.hpp` — node layout and build/query
  algorithms, mirroring the `detail/` split used by the R-tree.
- Public wrapper (e.g. `KdTree<T, Scalar, Extractor>` in
  `include/talus/kdtree.hpp`, added to the `talus.hpp` umbrella): build from a
  range, `size`/`empty`/`clear`, rectangular `search`/`within`,
  `radius_search`, `nearest_neighbor`, `nearest_neighbors(k)`, visitor
  overloads — matching the `SpatialIndex` API surface where it makes sense so
  the two are interchangeable for point data.
- Reuse the existing concept layer: accept `Pointlike` types and custom
  `CoordExtractor`s via `bounding_box_of()`; reject or document
  bounded-geometry types (a k-d tree stores points, not boxes).
- Tests: diff every query against `BruteForceIndex` (deterministic fixtures,
  randomized fixtures, degenerate/grid data), plus a scaled stress run like
  `tests/rtree_stress_tests.cpp`.
- Benchmarks: add k-d tree build and query benchmarks to
  `benchmarks/rtree_benchmarks.cpp` (or a sibling file) so the R*-tree and
  k-d tree can be compared on identical point workloads.
- Examples/README: driver menu options exercising the k-d tree, README status
  note, "Why Talus" row, API reference, and roadmap updates.

### 10. 1.0 Release Readiness

**This is the current focus** — with the R*-tree feature-complete, the next
work is hardening to the 1.0 tag rather than starting the k-d tree (section 9,
deferred to 1.1). Hardening and process work that should gate the 1.0 tag. The packaging
fundamentals already exist (install/export rules, `talusConfig.cmake` with
`SameMajorVersion` compatibility, LICENSE, `project(... VERSION ...)`); these
items close the remaining gaps. The cross-platform and Release CI lanes are now
in place; the remaining gaps are version macros, release process, thread-safety
docs, and an install/consumption smoke test.

- **Release-mode CI lane.** Completed: gcc/clang build both Debug and Release in `.github/workflows/ci.yml`.
- **MSVC/Windows + macOS CI.** Completed: `windows/MSVC` and `macos/AppleClang` lanes (Debug + Release, warnings-as-errors) in `.github/workflows/ci.yml`, all green.
- **Version macros in the headers.** `TALUS_VERSION_MAJOR/MINOR/PATCH` (and a
  combined value) in `talus.hpp`, kept in sync with the CMake project version,
  so consumers who vendor the headers or need conditional compilation can see
  the version.
- **Release process.** CHANGELOG.md, annotated git tags per release, and a
  stated semver policy — the package config already promises
  `SameMajorVersion` compatibility, so the policy should be written down.
- **Thread-safety documentation.** State the guarantee explicitly in the
  README and class-level Doxygen comments (expected: concurrent const queries
  are safe; any mutation requires external synchronization), and audit the
  code for anything that would silently violate it (e.g. mutable caches).
- **Install/consumption smoke test in CI.** A tiny downstream project that
  consumes Talus via `find_package(talus)` against an installed tree (and via
  FetchContent) and compiles a minimal program, so the install rules cannot
  bit-rot unnoticed.

Considered and rejected for 1.0: single-header amalgamation (the umbrella
header suffices), a generated Doxygen site (comments exist; a site can come
later), and whole-index iteration APIs.

### 11. Longer-range / exploratory (post-1.0)

These are larger, lower-priority efforts surfaced in the README "Why Talus"
comparison. They are intentionally deferred past the v0.1.x line.

- **Index serialization (save/load).** Pairs naturally with STR bulk load (load
  = read entries, bulk-build). The node layout already cooperates: entries are
  `bounds + value` / `bounds + child*` with only pool-internal pointers, and
  PLAN already commits to a pointer-free persistent format. The open design
  question is the user value `T`: start with a trivially-copyable-only API
  (`static_assert(std::is_trivially_copyable_v<T>)`), then add a user-provided
  serialize/deserialize hook (same escape-hatch pattern as `CoordExtractor`) for
  richer payloads. Additive — a new `save()`/`load()` on `SpatialIndex` plus
  tests; no rewrite.
- **Lossless `erase` under allocation failure.** Today `erase` gives the basic
  guarantee: on `bad_alloc` mid-condense, detached entries may be dropped
  (size stays accurate). Upgrading to "no entry loss" means pre-reserving
  worst-case pool capacity before mutating — orphan count is bounded by
  `condensed_nodes × MaxChildren` entries and each reinsert splits at most
  `tree_height` nodes — which needs a `reserve(n)` API on `PoolAllocator`
  plus the reservation math, and a nothrow-move constraint on `T` so the only
  throw source is allocation. Boost.Geometry's rtree documents the same
  basic-guarantee caveat, so this is hardening, not a competitive gap.
- **N-dimensional points and boxes.** A foundational change, not a bolt-on:
  `geometry.hpp` (`Point`, every `BoundingBox` method) is hand-written over
  `.x`/`.y`, `concepts.hpp` field-detection probes `.x`/`.y`, and the R*-tree
  split-axis loop assumes 2 axes. Generalizing means `std::array<Scalar, N>`
  coordinates and looping every per-axis operation over N. The harder part is
  that zero-boilerplate auto-detection does not generalize past 2-3 named fields
  — dimensions >2 would lean on the `CoordExtractor` adapter rather than field
  detection. Likely a 2.0 effort with its own design pass (compile-time `N`
  template parameter, 2D auto-detection kept as a fast path). Prefer
  `std::array<Scalar, N>` over `std::tuple`: coordinates are homogeneous and the
  per-axis loops want runtime `coords[axis]` indexing, which a tuple would force
  into `std::get<I>` template recursion for no benefit. Two consequences to plan
  for: (1) `area()` becomes a volume (product over N axes), which underflows
  toward zero for thin boxes in high N and degrades the area-based split
  heuristics — this is the curse-of-dimensionality reason production R*-trees cap
  around N ≈ 10-20; `margin()` (a sum over axes) generalizes cleanly and is less
  affected. (2) `BoundingBox` grows linearly with N, so the `alignas(64)`
  "one node = one cache line" invariant in `node.hpp` only holds at low N — a
  node stores `MaxChildren` boxes, so node size scales with `MaxChildren × N`
  and spills multiple lines as N grows.

## Next Concrete Task

Sections 1–8 are complete, including the large stress tests (section 6). The
R*-tree is feature-complete, so the next focus is 1.0 release readiness
(section 10) rather than the k-d tree (section 9), which is deferred to 1.1
per the Versioning and Release Sequencing plan. The cross-platform CI lanes
(Windows/MSVC, macOS) and the Release-mode lane are now done and green. Next
release-readiness step: version macros in `talus.hpp`
(`TALUS_VERSION_MAJOR/MINOR/PATCH`) kept in sync with the CMake project version.

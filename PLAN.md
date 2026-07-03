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

- **1.0** — R*-tree, hardened (section 9). The supported, stable API.
- **1.1** — k-d tree (section 10). Purely additive: a new `KdTree` class and
  new headers, no change to the R*-tree / `SpatialIndex` API, so it is a minor
  bump. Shipping the R*-tree first also lets its API settle before the k-d
  tree mirrors that surface for interchangeability.
- **1.x** — serialization (save/load), lazy query ranges, PMR memory-resource
  support, and lossless `erase` (section 11). All additive. The verification
  hardening in section 12 has no API surface at all, so it can land in any
  release, including before 1.1.
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

### 9. 1.0 Release Readiness

Complete. With the R*-tree feature-complete, this section captures the hardening
and process work that should gate the 1.0 tag before starting the k-d tree
(section 10, deferred to 1.1). The packaging
fundamentals already exist (install/export rules, `talusConfig.cmake` with
`SameMajorVersion` compatibility, LICENSE, `project(... VERSION ...)`), and the
remaining release-readiness gaps are now closed.

- Completed: add a Release-mode CI lane — gcc/clang build both Debug and Release in `.github/workflows/ci.yml`.
- Completed: add Windows/MSVC and macOS CI lanes — `windows/MSVC` and `macos/AppleClang` (Debug + Release, warnings-as-errors) in `.github/workflows/ci.yml`, all green.
- Completed: add public header version macros — `TALUS_VERSION_MAJOR/MINOR/PATCH`, `TALUS_VERSION`, and `TALUS_VERSION_STRING` in `include/talus/talus.hpp`, with a CMake sync guard.
- Completed: pin minimum compiler versions in CI — `linux/gcc-12`, `linux/clang-15`, `macos/AppleClang 15`, and `windows/MSVC 2022` baseline lanes in `.github/workflows/ci.yml`.
- Completed: document the release process and semver policy — `CHANGELOG.md` plus README release checklist, annotated tag convention, and `SameMajorVersion` compatibility policy.
- Completed: document thread-safety guarantees — concurrent const queries are safe; mutation requires external synchronization.
- Completed: add install/consumption smoke tests — `tests/consumer_smoke` covers installed `find_package(talus)` and `FetchContent` in CI.
- Completed: enforce a distance-safe coordinate domain — `coordinate_limit`/`within_coordinate_limit` (`geometry.hpp`) validated at the `SpatialIndex` boundary so extreme finite coordinates and an overflowing `radius` throw `invalid_geometry` instead of overflowing the squared-distance math (`tests/foundation_tests.cpp`, `tests/rtree_tests.cpp`).

Considered and rejected for 1.0: single-header amalgamation (the umbrella
header suffices), a generated Doxygen site (comments exist; a site can come
later), and whole-index iteration APIs.

### 10. Implement the k-d Tree

The second target structure, now scheduled for **1.1** (post-1.0; see
Versioning and Release Sequencing). A k-d tree indexes points only, so it
complements the R*-tree (which also handles boxes) and should be the faster
choice for static point datasets and nearest-neighbor-heavy workloads.

- Start the post-1.0 comparative benchmark track: add an optional
  `TALUS_BUILD_COMPARATIVE_BENCHMARKS` target for R-tree-to-R-tree comparisons
  against Boost.Geometry's rtree and, if setup stays lightweight, `RTree.h`.
  Keep third-party dependencies out of the default build: the option defaults
  OFF, all competitor dependency discovery/downloads are guarded by that option,
  and no competitor headers or targets affect `talus`, tests, examples,
  install, or ordinary benchmarks. Report dataset shape, seed, compiler, build
  type, and machine notes, and compare insert, bulk load, rectangle search,
  radius search, nearest/k-nearest, and erase on identical point and box
  workloads.
- Decide the structural design first: static (bulk-built, median-split, array
  packed — simplest and fastest to query) vs dynamic (insert/erase). Suggested
  scope for 1.1: build-from-range plus queries; defer dynamic mutation unless
  it falls out naturally.
- Make the k-d tree the data-oriented counterpoint to the R*-tree, and say so
  in the README: static implicit-layout, array-packed nodes (contiguous
  storage, no per-node allocation, no parent/child pointers), iterative
  stack-based queries rather than recursion, and construction from any
  `std::ranges::input_range`. The R*-tree demonstrates the pointer-based,
  cache-line-conscious school; the k-d tree should demonstrate the
  contiguous-layout school, with the comparative benchmarks above backing the
  contrast with numbers.
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
- Comparative benchmarks: once Talus has a k-d tree, add nanoflann comparisons
  under the optional comparative-benchmark target. Compare static point build,
  radius search, nearest neighbor, and k-nearest workloads against the Talus
  k-d tree first, and include the R*-tree only as a secondary "general index on
  point data" baseline so the results are not framed as a direct replacement
  for a point-specialized k-d tree.
- Examples/README: driver menu options exercising the k-d tree, README status
  note, "Why Talus" row, API reference, and roadmap updates.

### 11. Longer-range / exploratory (post-1.0)

Additive post-1.0 work, intentionally deferred until after the 1.0 release.
Serialization, lossless `erase`, and N-dimensional geometry were surfaced in
the README "Why Talus" comparison; lazy query ranges and PMR support are
self-contained API additions. Suggested order within this section:
serialization first (it pairs naturally with the STR bulk loader already in
place), then lazy query ranges and PMR support, then lossless `erase`, with
N-dimensional geometry last as the 2.0 rewrite.

- **Index serialization (save/load).** Pairs naturally with STR bulk load (load
  = read entries, bulk-build). The node layout already cooperates: entries are
  `bounds + value` / `bounds + child*` with only pool-internal pointers, and
  PLAN already commits to a pointer-free persistent format. The open design
  question is the user value `T`: start with a trivially-copyable-only API
  (`static_assert(std::is_trivially_copyable_v<T>)`), then add a user-provided
  serialize/deserialize hook (same escape-hatch pattern as `CoordExtractor`) for
  richer payloads. Additive — a new `save()`/`load()` on `SpatialIndex` plus
  tests; no rewrite.
- **Lazy query ranges.** A third query form alongside the vector-returning and
  visitor overloads: `index.query(box)` (and a radius variant) returning a
  lazy `std::ranges::forward_range` — a custom iterator holding an explicit
  traversal stack, `std::default_sentinel_t` as the end marker, and
  `std::ranges::view_interface` for the range shell — so callers can compose
  with `std::views` pipelines and stop early with no allocation or copying.
  The design work is the iterator contract: `std::forward_iterator`
  conformance, const-correctness, and documented invalidation (any mutation
  invalidates outstanding query ranges, consistent with the existing
  thread-safety note). Distinct from the whole-index iteration rejected in
  section 9 — this iterates query results, not the index. Additive (1.x).
- **PMR memory-resource support.** `PoolAllocator` blocks currently come
  straight from `::operator new`; thread an upstream
  `std::pmr::memory_resource*` through block allocation (defaulting to
  `std::pmr::get_default_resource()`), exposed as an optional `SpatialIndex`
  constructor argument, so users can back an entire index with an arena or
  observe allocations in tests. `<memory_resource>` is standard-library-only,
  so the zero-dependency constraint holds. Additive (1.x).
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

### 12. Verification Hardening (sequencing-independent)

Cross-cutting robustness work with no public API surface, so it can land in
any release, before or interleaved with the k-d tree.

- **ThreadSanitizer CI lane plus a concurrent-query test.** The thread-safety
  contract (concurrent `const` queries are safe) is documented in `rtree.hpp`
  but never verified. Add a test that runs `search`, `radius_search`,
  `nearest_neighbor`, and `nearest_neighbors` from several `std::jthread`s
  against one shared index, and run it under `-fsanitize=thread` in a new CI
  job beside the ASan/UBSan and Valgrind lanes. TSan is incompatible with
  ASan, so it needs its own build configuration, as Valgrind does.
- **Coverage-guided fuzz harness.** A libFuzzer target behind an opt-in
  `TALUS_BUILD_FUZZERS` CMake option (clang-only, defaulting OFF like the
  other opt-in lanes) that decodes fuzz input into a bounded op sequence —
  insert / erase / search / radius_search / NN / k-NN / bulk_load / clear —
  executed against both `SpatialIndex` and the `BruteForceIndex` oracle,
  diffing results and checking tree invariants after every op. Complements
  the fixed-seed randomized stress tests with coverage-guided input
  generation, and composes with the existing sanitizer builds.
- **Compile-time tests for the constexpr geometry surface.** `geometry.hpp`
  advertises constexpr-friendliness but nothing enforces it. Add a block of
  `static_assert` tests (containment, intersection, expand, area, margin,
  enlarged area, center, min squared distance) so a constexpr regression
  fails to compile. Make `BoundingBox::is_valid()` constexpr while there:
  `std::isfinite` is not constexpr until C++23, but a self-comparison NaN
  check plus `numeric_limits` infinity comparisons is a portable C++20
  equivalent. Behavior-neutral.

## Next Concrete Task

Sections 1–9 are complete, including the large stress tests (section 6) and 1.0
release-readiness gates (section 9). The R*-tree is feature-complete, so the
next focus is the k-d tree (section 10), scheduled for 1.1 per the Versioning
and Release Sequencing plan. The verification-hardening items in section 12
(TSan lane, fuzz harness, constexpr `static_assert` tests) have no API surface
and can be picked up at any point alongside that work.

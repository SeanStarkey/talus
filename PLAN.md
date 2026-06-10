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

The following scaffolding directories currently contain placeholder CMake files:

- `benchmarks/`

`examples/` now includes `talus_driver`, a menu-driven command-line
program that exercises the current public `SpatialIndex` API with seeded data,
rectangular search, nearest-neighbor and k-nearest queries, radius search,
erase-by-id deletion, clearing, size/empty reporting,
STR bulk-load rebuilds of the loaded records, and
JSON import for mixed point, lat/lon, box, and segment records. The example datasets include Colorado
14er and ranked Colorado 13er summit coordinates.

The concept layer now supports scalar-aware bounding-box extraction and gives `.bounds()` precedence over point fields when a type satisfies both.

All current library headers now include file-level comment headers that describe
their purpose and ownership boundaries.

Public structures, concepts, helpers, and storage methods now include
Doxygen-style comments for generated API documentation.

The test suite currently includes dependency-free foundation smoke tests for `geometry.hpp`, `concepts.hpp`, and the `talus.hpp` umbrella include, focused pool allocator and R-tree node storage tests, R-tree algorithm tests, and public `SpatialIndex` oracle tests. Test invariants are checked with a local `TALUS_CHECK` macro (`tests/test_check.hpp`) that always runs and is not stripped by `NDEBUG`, so the suite stays meaningful in `Release` as well as `Debug`.

CMake configure, build, and `ctest` pass with the current foundation, pool allocator, node storage, algorithm, and public R-tree test targets.

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
- Completed: adaptive leaf value storage — values larger than `rtree_inline_value_max_size` (128 bytes) are boxed behind a `unique_ptr` so large payloads do not inflate node storage (or, via the leaf/child union, internal nodes); small values stay inline. Access via the entry `value()` accessor.
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
  - `insert`
  - `size`
  - `empty`
  - `clear`
  - rectangular `search`
  - `within` alias for the documented rectangular query spelling
  - `radius_search` for point-to-bounds radius queries
  - `nearest_neighbor` for point-to-bounds nearest queries, returning
    `std::optional<T>`
  - `erase` for deleting one equality-comparable stored value, with internal
    CondenseTree handling for underfull nodes
  - `erase` exception safety — basic guarantee for nothrow-move `T`: on
    allocation failure mid-condense the tree is repaired to a valid state
    (aborted-split overflow entries are dropped), `size()` is resynchronized
    via `detail::count_values`, and the index stays fully usable; entries
    detached for reinsertion may be lost. Verified by allocation-failure
    injection sweeps in `tests/rtree_exception_tests.cpp` (countdown-failing
    global `operator new` in a dedicated test binary; self-skips under tools
    like Valgrind that intercept the global allocator symbols)
  - move construction / move assignment — the root is pool-allocated (created
    lazily on first insert) so node storage is address-stable across a move;
    copying stays deleted
  - input validation — `insert`/`search`/`within`/`nearest_neighbor` throw
    `talus::invalid_geometry` (a `std::invalid_argument`) on NaN/infinite
    coordinates or `min > max`, rather than asserting/terminating; a rejected
    insert leaves the index unchanged

Keep this phase intentionally narrow. Do not expand the public wrapper to
delete, radius search, or bulk load until the minimal insert/search/nearest
API is covered by oracle tests.

### 6. Add the Brute-force Oracle

- Completed: add `tests/brute_force.hpp`.
- Completed: compare public `SpatialIndex` rectangular search results against brute force
  for deterministic fixtures.
- Completed: add randomized public API search tests after deterministic tests are stable.
- Scale randomized tests gradually before attempting the documented large stress tests.

### 7. Implement Remaining R*-tree Algorithms

After Insert, Search, NearestNeighbor, and RadiusSearch are solid, add to `algorithms.hpp`:

1. Completed: Radius search
2. Completed: Delete
3. Completed: STR bulk load — `detail::str_bulk_load` packs leaf entries with
   Sort-Tile-Recursive tiling (x-center sort, vertical slices, y-center sort
   per slice) and builds upper levels the same way until a single root remains.
   All needed nodes are pool-reserved up front. A final-group min-fill
   redistribution keeps every non-root node at or above `min_children`, so the
   packed tree upholds the invariants the dynamic insert/erase algorithms rely
   on. Exposed publicly as `SpatialIndex::bulk_load` (a range overload that
   copies, plus a `std::vector<T>&&` overload that moves and supports move-only
   values). The public method requires an empty index (`std::logic_error`
   otherwise), validates every extracted bounds before touching the tree
   (`invalid_geometry` leaves the index unchanged), and resets to a valid empty
   state if the build itself fails. Covered by detail-level structural
   invariant tests (uniform leaf depth, fill bounds, parent links, bounds
   unions) and public brute-force-oracle tests, including post-load
   insert/erase interoperation.
4. Completed: k-nearest (k>1) queries — `detail::k_nearest_neighbors` reuses
   the single-nearest traversal shape (children visited in increasing
   minimum-distance order) with a size-k max-heap of the best candidates;
   subtrees farther than the current k-th best distance are pruned. Exposed
   publicly as `SpatialIndex::nearest_neighbors(query, k)`, which validates
   the query point (`invalid_geometry` on non-finite coordinates) and returns
   `std::vector<T>` sorted by ascending box distance (`k == 0`, or an empty
   index, returns empty; `k > size()` returns everything). Tie order at the
   k-th distance is unspecified. Covered by detail-level tests and tie-free
   fixture plus randomized brute-force-oracle tests that compare exact
   ascending-distance ordering.
5. Completed: Custom query predicates / visitor traversal — the public
   `QueryVisitor<Visitor, T>` concept (`concepts.hpp`) accepts callables taking
   `const T&` and returning either void (visit every match) or a type
   convertible to bool (return false to stop the traversal early; the stopping
   value is included in the returned count). Exposed as visitor overloads
   `SpatialIndex::search(bounds, visitor)`, `within(bounds, visitor)`, and
   `radius_search(query, radius, visitor)`, each returning the number of values
   visited. Matches are visited by const reference in unspecified order without
   copying, so visitor queries work with move-only `T` and let callers apply
   custom predicates or aggregate in place. The detail traversals
   (`detail::search` / `detail::radius_search`) propagate the early-stop signal
   through `visit_detail::visit_value`, which normalizes void- and
   bool-returning visitors. Covered by brute-force-oracle tests (randomized
   rectangle and radius queries), predicate-filtering, early-stop, move-only,
   empty-index, and validation-throw tests.

### 8. Add Examples and Benchmarks

After correctness is established:

- Completed: add a public-API R*-tree driver for `.x/.y` point structs,
  `.lat/.lon` point data, and bounded geometries.
- Completed: add example JSON import data for point, lat/lon, box, and segment
  geometries.
- Completed: add Colorado 14ers lat/lon sample JSON data for the driver.
- Add examples for custom coordinate extractors.
- Add benchmarks for insertion, rectangular search, nearest neighbor, and bulk loading.

### 9. Longer-range / exploratory (post-1.0)

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

Section 7 is complete. Continue section 8: add examples for custom coordinate
extractors, which first requires wiring the `CoordExtractor` concept (defined
in `concepts.hpp` but not yet used) into `SpatialIndex`.

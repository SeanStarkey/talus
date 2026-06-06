# AGENTS.md

## What this project is

Talus is a zero-dependency, header-only C++20 spatial index library. The target structures are an R*-tree (in progress) and a k-d tree (planned). The entire public API lives under `include/talus/`. There are no `.cpp` files — everything is headers.

## Build commands

```bash
# Configure (tests ON by default at top level)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test binary
./build/tests/<test_binary_name>

# With benchmarks or examples
cmake -B build -DTALUS_BUILD_BENCHMARKS=ON -DTALUS_BUILD_EXAMPLES=ON

# Warnings-as-errors (CI uses this) and sanitizers (ASan + UBSan)
cmake -B build -DTALUS_WARNINGS_AS_ERRORS=ON
cmake -B build-asan -DTALUS_ENABLE_SANITIZERS=ON
```

Talus's own targets (tests/examples/benchmarks) build with `-Wall -Wextra -Wpedantic` by default via the internal `talus_dev_options` interface target; these flags never reach the public `talus` interface. `.github/workflows/ci.yml` builds and tests with GCC and Clang under `-Werror` plus a sanitizer job. Keep the tree warning-clean under `-Werror`.

No external dependencies are needed to build the library itself. Tests are plain executables with no external framework — they check invariants with a local `TALUS_CHECK` macro (`tests/test_check.hpp`) that always runs, so it is not stripped by `NDEBUG` and the suite stays meaningful in `Release` builds — and `benchmarks/` is an empty placeholder, so nothing is fetched today. If a test framework (e.g. Catch2) or Google Benchmark is introduced later, it will come in via CMake FetchContent and only for `tests/` / `benchmarks/`.

## Development status

**PLAN.md is the authoritative, up-to-date status.** Summary:

**Phase 1 — Foundation** (complete): `geometry.hpp`, `concepts.hpp`, `CMakeLists.txt`

**Phase 2 — R*-tree Core** (in progress), in this file order:
1. `include/talus/detail/pool_alloc.hpp` — pool allocator for tree nodes *(done)*
2. `include/talus/detail/node.hpp` — `RTreeNode` (cache-line aligned, `alignas(64)`, union of children/values) *(done)*
3. `include/talus/detail/algorithms.hpp` — ChooseLeaf → Insert → SplitNode → AdjustTree → Search *(done)*; NearestNeighbor → Delete (with forced reinsertion) → STR bulk load *(remaining)*
4. `include/talus/rtree.hpp` — `SpatialIndex<T, Scalar, MaxChildren>` wrapping the R*-tree: `insert` / `size` / `empty` / `clear` / `search` / `within` *(done)*
5. `tests/` — all tests must pass against the `BruteForceIndex` oracle in `tests/brute_force.hpp` *(passing for the above)*

**Phases 3–5**: nearest neighbor, radius search, visitor pattern, delete, STR bulk load, k-d tree, benchmarks, examples.

## Architecture

### Type system (`concepts.hpp`)
Three primary concepts drive zero-boilerplate type detection:
- `HasXY` — struct has `.x`, `.y` fields → used as point coordinates
- `HasLatLon` — struct has `.lat`, `.lon` fields → used as point coordinates
- `HasBounds` — struct has `.bounds()` returning `BoundingBox` → used as a bounded geometry (`HasBoundsAny` is the scalar-agnostic variant used for extraction)
- `Indexable = Pointlike || HasBoundsAny` — what `SpatialIndex` accepts
- `CoordExtractor` — escape hatch concept for types that match none of the above (defined in `concepts.hpp`, but not yet wired into `SpatialIndex`)

`bounding_box_of()` in `concepts.hpp` handles the automatic dispatch — it checks which concept the type satisfies and extracts coordinates accordingly. All new code that needs coordinates should go through this function.

### Geometry types (`geometry.hpp`)
All types are `constexpr`-friendly templates on `Scalar` (default `double`). `BoundingBox` carries the core geometric logic (`contains`, `intersects`, `expand`, `area`, `enlarged_area`, `min_sq_distance`). The R*-tree split algorithm depends heavily on `enlarged_area` and `intersects` being correct.

### R*-tree node layout
Nodes are `alignas(64)` structs (one cache line). The union between `children` (internal) and `values` (leaf) is disambiguated by the `is_leaf` flag. `MaxChildren` is a compile-time parameter (default 9); `MinChildren = MaxChildren / 2`. The pool allocator keeps nodes contiguous in memory.

### Testing oracle (`tests/brute_force.hpp`)
`BruteForceIndex<T>` is a linear-scan reference implementation. Every R*-tree (and, later, k-d tree) operation must produce results identical to `BruteForceIndex` for the same inputs. Current randomized tests diff a few hundred points/queries against brute force; scaling up to the planned large stress tests (target: ~1M random points with ~10K random queries) is future work (see PLAN.md).

## Key constraints

- **Zero dependencies** for the library target. Only `tests/` and `benchmarks/` may pull in external libraries via FetchContent.
- **C++20 required** — concepts and `std::span` are used throughout the library; `std::ranges` is reserved for planned APIs (e.g. bulk insert).
- **Header-only** — no `.cpp` files in `include/`. Template implementations go in the same `.hpp` or in `detail/` headers included at the bottom of the public header.
- The single-include entry point is `include/talus/talus.hpp` — it `#include`s the public headers (`concepts.hpp`, `geometry.hpp`, `rtree.hpp`).

## Git And Collaboration

- Do not revert unrelated changes.
- Read nearby code before editing, and follow existing patterns.
- Summarize changed behavior and verification steps when finishing work.
- Update PLAN.md with any changes.
- If tests cannot be run because none exist, say so and describe the manual check used instead.
- All new test functions should include a short header block that names the test and describes the behavior or invariant being verified in enough detail for future maintainers to understand the intent.
- When Codex makes code changes and is asked to create a git commit, include `Co-authored-by: Codex <codex@openai.com>` in the commit message.
- Do not add the Codex co-author trailer for commits that only contain user-authored work or by other AIs.

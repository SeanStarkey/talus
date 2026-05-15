# AGENTS.md

## What this project is

Talus is a zero-dependency, header-only C++20 spatial index library (R*-tree and k-d tree). The entire public API lives under `include/talus/`. There are no `.cpp` files — everything is headers.

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
```

No external dependencies are needed to build the library itself. Test and benchmark dependencies (Catch2, Google Benchmark) are fetched via CMake FetchContent when those options are enabled.

## Development status

**Phase 1 — Foundation** (complete): `geometry.hpp`, `concepts.hpp`, `CMakeLists.txt`

**Phase 2 — R*-tree Core** (next): implement in this order:
1. `include/talus/detail/pool_alloc.hpp` — pool allocator for tree nodes
2. `include/talus/detail/node.hpp` — `RTreeNode` (cache-line aligned, `alignas(64)`, union of children/values)
3. `include/talus/detail/algorithms.hpp` — ChooseLeaf → Insert → SplitNode → AdjustTree → Search → NearestNeighbor → Delete → STR bulk load
4. `include/talus/rtree.hpp` — `SpatialIndex<T, Scalar, MaxChildren>` wrapping the R*-tree
5. `tests/` — all tests must pass against the `BruteForceIndex` oracle in `tests/brute_force.hpp`

**Phases 3–5**: nearest neighbor, radius search, visitor pattern, delete, STR bulk load, k-d tree, benchmarks, examples.

## Architecture

### Type system (`concepts.hpp`)
Three primary concepts drive zero-boilerplate type detection:
- `HasXY` — struct has `.x`, `.y` fields → used as point coordinates
- `HasLatLon` — struct has `.lat`, `.lon` fields → used as point coordinates
- `HasBounds` — struct has `.bounds()` returning `BoundingBox` → used as a bounded geometry
- `Indexable = Pointlike || HasBounds` — what `SpatialIndex` accepts
- `CoordExtractor` — escape hatch for types that match none of the above

`bounding_box_of()` in `concepts.hpp` handles the automatic dispatch — it checks which concept the type satisfies and extracts coordinates accordingly. All new code that needs coordinates should go through this function.

### Geometry types (`geometry.hpp`)
All types are `constexpr`-friendly templates on `Scalar` (default `double`). `BoundingBox` carries the core geometric logic (`contains`, `intersects`, `expand`, `area`, `enlarged_area`, `min_sq_distance`). The R*-tree split algorithm depends heavily on `enlarged_area` and `intersects` being correct.

### R*-tree node layout
Nodes are `alignas(64)` structs (one cache line). The union between `children` (internal) and `values` (leaf) is disambiguated by the `is_leaf` flag. `MaxChildren` is a compile-time parameter (default 9); `MinChildren = MaxChildren / 2`. The pool allocator keeps nodes contiguous in memory.

### Testing oracle (`tests/brute_force.hpp`)
`BruteForceIndex<T>` is a linear-scan reference implementation. Every R*-tree and k-d tree operation must produce results identical to `BruteForceIndex` for the same inputs. Stress tests run 1M random points with 10K random queries and diff against brute force.

## Key constraints

- **Zero dependencies** for the library target. Only `tests/` and `benchmarks/` may pull in external libraries via FetchContent.
- **C++20 required** — concepts, ranges, and `std::variant` are used freely.
- **Header-only** — no `.cpp` files in `include/`. Template implementations go in the same `.hpp` or in `detail/` headers included at the bottom of the public header.
- The single-include entry point will be `include/talus/talus.hpp` (not yet created) — it will `#include` all public headers.

## Git And Collaboration

- Do not revert unrelated changes.
- Read nearby code before editing, and follow existing patterns.
- Summarize changed behavior and verification steps when finishing work.
- Update PLAN.md with any changes.
- If tests cannot be run because none exist, say so and describe the manual check used instead.
- When Codex makes code changes and is asked to create a git commit, include `Co-authored-by: Codex <codex@openai.com>` in the commit message.
- Do not add the Codex co-author trailer for commits that only contain user-authored work or by other AIs.

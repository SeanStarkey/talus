# Contributing to Talus

Thanks for taking the time to improve Talus. This guide explains how to set up
the project, where to look before starting work, and what maintainers expect in
issues and pull requests.

Talus is a zero-dependency, header-only C++20 spatial index library. The public
API lives under `include/talus/`, with the single-include entry point at
`include/talus/talus.hpp`. The current stable surface is the R*-tree-backed
`talus::SpatialIndex`; the k-d tree is planned as a future additive feature.

`PLAN.md` is the source of truth for project status and roadmap details.

## Ways to contribute

Useful contributions include:

- bug reports with small reproductions,
- documentation improvements,
- examples that exercise real user workflows,
- tests that cover edge cases or regressions,
- performance benchmarks for existing query paths,
- R*-tree fixes and improvements that preserve the public API,
- future k-d tree work that follows the planned API direction in `PLAN.md`.

Before starting larger work, open an issue or discussion so the design can be
checked against the roadmap.

## Reporting bugs

Please include enough detail for someone else to reproduce the problem:

- Talus version or commit,
- operating system,
- compiler and compiler version,
- CMake version,
- build flags or CMake options,
- a minimal code example,
- expected behavior,
- actual behavior,
- whether the result differs from a simple brute-force scan, if applicable.

For crashes or sanitizer findings, include the full failing command and the
relevant diagnostic output.

## Development setup

Talus has no library dependencies. Tests, examples, and benchmarks are built
through CMake.

Configure, build, and run the default test suite:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a single test executable:

```bash
./build/tests/<test_binary_name>
```

Build with warnings as errors:

```bash
cmake -B build-werror -DTALUS_WARNINGS_AS_ERRORS=ON
cmake --build build-werror
ctest --test-dir build-werror --output-on-failure
```

Build with sanitizers:

```bash
cmake -B build-asan -DTALUS_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Build examples or benchmarks:

```bash
cmake -B build -DTALUS_BUILD_EXAMPLES=ON -DTALUS_BUILD_BENCHMARKS=ON
cmake --build build
```

## Coding guidelines

- Keep the library header-only. Do not add `.cpp` files under `include/`.
- Keep the public API under `include/talus/`; implementation details belong in
  `include/talus/detail/`.
- Preserve the zero-dependency library target. External packages, if ever
  needed, must be limited to tests, examples, or benchmarks.
- Use C++20 facilities consistently with the existing codebase.
- Prefer existing helpers and patterns over new abstractions.
- Use `bounding_box_of()` for coordinate extraction instead of duplicating
  point or bounds detection.
- Access stored R-tree values through their `value()` accessor.
- Keep public-facing changes synchronized across code, tests, docs, examples,
  and `PLAN.md`.
- Do not commit generated build directories or local tool artifacts.

Talus's own targets build with strict warnings in CI. Keep new code clean under
`-Wall -Wextra -Wpedantic` and warnings-as-errors configurations.

## Testing expectations

New behavior should include focused tests. Query behavior should be compared
against `tests/brute_force.hpp` whenever practical.

All test checks should use the local `TALUS_CHECK` macro from
`tests/test_check.hpp`, not `assert`, so tests remain meaningful in Release
builds.

New test functions should start with a short comment block that names the test
and describes the behavior or invariant being verified.

For public API changes, consider whether the following need coverage:

- deterministic fixtures,
- randomized tests against the brute-force oracle,
- invalid geometry or exception-safety paths,
- move-only or large value storage,
- Release-mode behavior,
- installed-package or FetchContent smoke tests.

## Documentation and examples

For a user-visible feature or behavior change, update the relevant docs in the
same pull request:

- `README.md` status, API, comparison table, or roadmap sections,
- `PLAN.md` current-state and roadmap entries,
- `CHANGELOG.md` when the change is release-facing,
- `examples/talus_driver.cpp` when the feature should be exercised
  interactively.

Keep completed `PLAN.md` items terse. Implementation details belong in code,
tests, comments, or the pull request description.

## Pull request checklist

Before opening a pull request, please check:

- The change is focused and does not include unrelated cleanup.
- The relevant tests pass locally.
- New behavior has tests.
- Public behavior changes are documented.
- `README.md`, `PLAN.md`, examples, and changelog entries are updated where
  appropriate.
- CI-facing changes have been tested with warnings as errors when practical.
- No build artifacts or generated files are included.

Useful final local checks:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

For release-facing or compiler-warning work, also run a Release warnings-as-
errors build:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DTALUS_WARNINGS_AS_ERRORS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

## Review process

Maintainers may ask for changes to keep the public API small, preserve the
header-only dependency-free design, or improve test coverage. CI must pass
before a pull request can be merged.

If a requested change affects the public surface, keep the related code, tests,
documentation, examples, and roadmap notes together in the same update.

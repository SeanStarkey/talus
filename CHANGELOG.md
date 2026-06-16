# Changelog

All notable Talus changes are recorded here.

Talus follows semantic versioning. Before 1.0, the public API may still change
while the R*-tree surface hardens; breaking changes are called out explicitly.
Starting at 1.0, compatible releases stay within the same major version, matching
the installed CMake package config's `SameMajorVersion` policy.

## [0.1.0] - Unreleased

First planned public release.

### Added

- Header-only C++20 R*-tree spatial index exposed as `talus::SpatialIndex`.
- Automatic indexing support for `.x`/`.y` points, `.lat`/`.lon` points, and
  geometries with `bounds()`.
- Custom coordinate extractor support for opaque user types.
- Rectangle, radius, single nearest-neighbor, k-nearest, visitor, delete, and
  STR bulk-load operations.
- Public version macros in `<talus/talus.hpp>`:
  `TALUS_VERSION_MAJOR`, `TALUS_VERSION_MINOR`, `TALUS_VERSION_PATCH`,
  `TALUS_VERSION`, and `TALUS_VERSION_STRING`.
- CMake install/export rules for `find_package(talus)` consumers and
  dependency-free tests, examples, and microbenchmarks.
- Cross-platform CI coverage for Linux, macOS, and Windows, with Debug and
  Release lanes, warning-clean baseline compiler lanes, sanitizer coverage, and
  Valgrind coverage on Linux.
- `coordinate_limit<Scalar>()` and `within_coordinate_limit()` helpers in
  `<talus/geometry.hpp>` defining the distance-safe coordinate domain.

### Fixed

- Distance queries no longer silently corrupt results at extreme coordinate
  magnitudes. Squared Euclidean distances (`dx*dx + dy*dy`) and squared radii
  could overflow to `+inf` for finite-but-enormous inputs, collapsing
  nearest-neighbor ordering and making `radius_search` match the entire index.
  `SpatialIndex` now rejects stored values and distance-query points outside
  `coordinate_limit<Scalar>()`, and a `radius` whose square overflows, with
  `talus::invalid_geometry`. The limit (~3.3e153 for `double`) is far beyond any
  real spatial data; rectangular `search`/`within`, which use only comparisons,
  are unaffected.

#pragma once

/// @file test_check.hpp
/// @brief Always-on assertion macro for Talus tests.
///
/// `TALUS_CHECK` behaves like `assert`, but is **never** compiled out — unlike
/// `<cassert>`'s `assert`, which becomes a no-op under `NDEBUG`. Using it keeps
/// test binaries meaningful in every build configuration (Debug, Release,
/// RelWithDebInfo), so a Release `ctest` run verifies behavior instead of passing
/// vacuously. Tests stay dependency-free: this is a single local header.

#include <cstdio>
#include <cstdlib>

namespace talus::test {

/// @brief Reports a failed check to stderr and aborts with a non-zero exit.
[[noreturn]] inline void check_failed(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "%s:%d: TALUS_CHECK failed: %s\n", file, line, expr);
    std::abort();
}

} // namespace talus::test

/// @brief Verifies `cond` at runtime in any build configuration.
///
/// Evaluates `cond` exactly once and, on failure, prints the file, line, and
/// expression, then aborts. Defined as a single expression (like `assert`) so it
/// is usable wherever a statement expression is, including after `if` without
/// braces.
#define TALUS_CHECK(cond) \
    ((cond) ? (void)0 : ::talus::test::check_failed(__FILE__, __LINE__, #cond))

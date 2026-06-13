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

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace talus::test {

/// @brief Routes CRT assertion/abort diagnostics to stderr instead of a dialog.
///
/// In an MSVC Debug build a failed `assert()` or `abort()` pops an interactive
/// "Debug Assertion Failed" dialog by default. On a headless CI agent nothing
/// dismisses it, so the process blocks forever — fatal for death tests that
/// intentionally abort a (sub)process and verify the non-zero exit. This sends
/// the reports to stderr and makes `abort()` terminate immediately. No-op off
/// MSVC. Safe to call more than once.
inline void disable_crt_report_dialogs() noexcept {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#  ifdef _DEBUG
    // The _CrtSetReport* routines exist only in Debug builds; in Release CRT
    // they are no-op macros that would leave `report` unreferenced (C4189). The
    // assertion dialog only appears in Debug anyway, so this is Debug-only.
    for (int report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
#  endif
#endif
}

/// @brief True on build configs where allocation/iterator-heavy loops are
/// pathologically slow, so large correctness sweeps should run at reduced scale.
///
/// Currently just MSVC Debug, where checked iterators (`_ITERATOR_DEBUG_LEVEL=2`)
/// slow tight loops by ~50-100x. Everywhere else this is `false` and tests run
/// at full scale. This is the single switch behind every "scale down on slow
/// debug" decision in the suite — adjust the per-test sizes at the call sites of
/// `scaled_workload`, not by sprinkling new `#if` guards.
#if defined(_MSC_VER) && defined(_DEBUG)
inline constexpr bool slow_debug_build = true;
#else
inline constexpr bool slow_debug_build = false;
#endif

/// @brief Returns `reduced` on a slow debug build, otherwise `full`.
///
/// Lets an expensive sweep shrink its input on slow configs without changing
/// what it verifies. Keep `reduced` large enough to still cover the behavior
/// under test (e.g. above `MaxChildren` for R-tree fixtures).
template<typename T>
[[nodiscard]] constexpr T scaled_workload(T full, T reduced) noexcept {
    return slow_debug_build ? reduced : full;
}

/// @brief Reports a failed check to stderr and aborts with a non-zero exit.
[[noreturn]] inline void check_failed(const char* file, int line, const char* expr) {
    // Ensure the abort() below exits instead of hanging on an MSVC Debug dialog.
    disable_crt_report_dialogs();
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

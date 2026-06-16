#pragma once

#include <cassert>
#include <exception>

/// @file detail/assert.hpp
/// @brief Hardened assertion macro for Talus internal precondition checks.
///
/// Behavior by build configuration:
///   - Debug (NDEBUG not defined): expands to assert(), preserving file/line diagnostics.
///   - Release (default): calls std::terminate() on failure — no UB, no silent corruption.
///   - Release + TALUS_DISABLE_HARDENED_CHECKS: strips all checks for maximum throughput.
///
/// Define TALUS_DISABLE_HARDENED_CHECKS before including any Talus header to opt out of
/// release-mode checks.
#ifndef NDEBUG
#  define TALUS_ASSERT(cond) assert(cond)
#  define TALUS_ASSERT_FAIL(message) assert(false && message)
#elif defined(TALUS_DISABLE_HARDENED_CHECKS)
#  define TALUS_ASSERT(cond) ((void)0)
#  define TALUS_ASSERT_FAIL(message) ((void)0)
#else
#  define TALUS_ASSERT(cond) \
     do { if (!(cond)) [[unlikely]] { std::terminate(); } } while (false)
#  define TALUS_ASSERT_FAIL(message) std::terminate()
#endif

/// @brief Marks a code path that must never be reached at runtime.
///
/// In debug builds the path fires an assert (for diagnostics) and then
/// terminates. In release builds with hardened checks disabled it expands to a
/// compiler hint that enables dead-code elimination without UB. Otherwise
/// std::terminate() is called.

// Compiler hint that the point is unreachable, used for dead-code elimination
// on the hardened-checks-disabled path.
#if defined(__GNUC__) || defined(__clang__)
#  define TALUS_UNREACHABLE_HINT() __builtin_unreachable()
#elif defined(_MSC_VER)
#  define TALUS_UNREACHABLE_HINT() __assume(false)
#else
#  define TALUS_UNREACHABLE_HINT() ((void)0)
#endif

#ifndef NDEBUG
namespace talus::detail {
/// In debug, an unreachable path fires the assert and then terminates. The
/// function is [[noreturn]] so MSVC trusts that callers ending in
/// TALUS_UNREACHABLE() do not fall through (avoiding C4715, "not all control
/// paths return a value"). A bare assert does not convey that, while appending
/// a no-return hint instead trips C4702 ("unreachable code") because MSVC's
/// debug assert is itself no-return — so the attribute plus a locally
/// suppressed terminate is the combination that satisfies every compiler.
[[noreturn]] inline void unreachable() {
    assert(false && "unreachable");
#ifdef _MSC_VER
#  pragma warning(suppress : 4702)
#endif
    std::terminate();
}
} // namespace talus::detail
#  define TALUS_UNREACHABLE() ::talus::detail::unreachable()
#elif defined(TALUS_DISABLE_HARDENED_CHECKS)
#  define TALUS_UNREACHABLE() TALUS_UNREACHABLE_HINT()
#else
#  define TALUS_UNREACHABLE() std::terminate()
#endif

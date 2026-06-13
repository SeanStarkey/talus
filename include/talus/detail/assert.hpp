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
#elif defined(TALUS_DISABLE_HARDENED_CHECKS)
#  define TALUS_ASSERT(cond) ((void)0)
#else
#  define TALUS_ASSERT(cond) \
     do { if (!(cond)) [[unlikely]] { std::terminate(); } } while (false)
#endif

/// @brief Marks a code path that must never be reached at runtime.
///
/// In debug builds the path is left reachable so sanitizers and debuggers can
/// catch it. In release builds with hardened checks disabled, the compiler hint
/// allows dead-code elimination without UB. Otherwise std::terminate() is called.
// Compiler hint that the point is unreachable. Beyond enabling dead-code
// elimination, it tells the compiler control does not fall through, which
// suppresses MSVC C4715 ("not all control paths return a value") at the end of
// functions whose last statement is TALUS_UNREACHABLE().
#if defined(__GNUC__) || defined(__clang__)
#  define TALUS_UNREACHABLE_HINT() __builtin_unreachable()
#elif defined(_MSC_VER)
#  define TALUS_UNREACHABLE_HINT() __assume(false)
#else
#  define TALUS_UNREACHABLE_HINT() ((void)0)
#endif

#ifndef NDEBUG
// Keep the path reachable for sanitizers/debuggers (assert fires first), but
// still emit the no-return hint so MSVC does not warn about falling through.
#  define TALUS_UNREACHABLE()                       \
     do {                                           \
         assert(false && "unreachable");            \
         TALUS_UNREACHABLE_HINT();                  \
     } while (false)
#elif defined(TALUS_DISABLE_HARDENED_CHECKS)
#  define TALUS_UNREACHABLE() TALUS_UNREACHABLE_HINT()
#else
#  define TALUS_UNREACHABLE() std::terminate()
#endif

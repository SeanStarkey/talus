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
#ifndef NDEBUG
#  define TALUS_UNREACHABLE() assert(false && "unreachable")
#elif defined(TALUS_DISABLE_HARDENED_CHECKS)
#  if defined(__GNUC__) || defined(__clang__)
#    define TALUS_UNREACHABLE() __builtin_unreachable()
#  else
#    define TALUS_UNREACHABLE() ((void)0)
#  endif
#else
#  define TALUS_UNREACHABLE() std::terminate()
#endif

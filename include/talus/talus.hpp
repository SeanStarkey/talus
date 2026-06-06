#pragma once

/// @file talus.hpp
/// @brief Single-include entry point for the Talus public API.
///
/// Include this header when using Talus as a header-only library. It re-exports
/// the public geometry, type-detection facilities, and spatial index APIs.

#include "concepts.hpp"
#include "geometry.hpp"
#include "rtree.hpp"

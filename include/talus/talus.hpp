#pragma once

/// @file talus.hpp
/// @brief Single-include entry point for the Talus public API.
///
/// Include this header when using Talus as a header-only library. It re-exports
/// the public geometry, type-detection facilities, and spatial index APIs.

/// @def TALUS_VERSION_MAJOR
/// @brief Talus major version component.
#define TALUS_VERSION_MAJOR 0

/// @def TALUS_VERSION_MINOR
/// @brief Talus minor version component.
#define TALUS_VERSION_MINOR 1

/// @def TALUS_VERSION_PATCH
/// @brief Talus patch version component.
#define TALUS_VERSION_PATCH 0

/// @def TALUS_VERSION
/// @brief Combined integer version: major * 10000 + minor * 100 + patch.
#define TALUS_VERSION 100

/// @def TALUS_VERSION_STRING
/// @brief Talus version as a string literal.
#define TALUS_VERSION_STRING "0.1.0"

#include "concepts.hpp"
#include "geometry.hpp"
#include "rtree.hpp"

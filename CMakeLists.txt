cmake_minimum_required(VERSION 3.20)
project(talus VERSION 0.1.0 LANGUAGES CXX)

# ── Options ──────────────────────────────────────────────────────────────────

# When consumed as a subdirectory or via FetchContent, tests/benchmarks/examples
# default to OFF so we don't impose build-time costs on the consumer.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(_talus_top_level ON)
else()
    set(_talus_top_level OFF)
endif()

option(TALUS_BUILD_TESTS      "Build Talus tests"      ${_talus_top_level})
option(TALUS_BUILD_BENCHMARKS "Build Talus benchmarks" OFF)
option(TALUS_BUILD_EXAMPLES   "Build Talus examples"   OFF)

# ── Library target (header-only interface) ───────────────────────────────────

add_library(talus INTERFACE)
add_library(talus::talus ALIAS talus)

target_include_directories(talus INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(talus INTERFACE cxx_std_20)

# ── Sub-directories ───────────────────────────────────────────────────────────

if(TALUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TALUS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(TALUS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS talus EXPORT talusTargets)
install(EXPORT talusTargets
    FILE      talusTargets.cmake
    NAMESPACE talus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/talus
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/talusConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/talusConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/talus
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/talusConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/talusConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/talusConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/talus
)

cmake_minimum_required(VERSION 3.20)
project(talus VERSION 1.0.0 LANGUAGES CXX)

# Keep the public vendorable header version macros synchronized with the CMake
# package version used by installed `talusConfigVersion.cmake`.
set(_talus_version_header "${CMAKE_CURRENT_SOURCE_DIR}/include/talus/talus.hpp")
file(READ "${_talus_version_header}" _talus_version_header_contents)

function(_talus_read_version_macro macro_name out_var)
    string(REGEX MATCH "#[ \t]*define[ \t]+${macro_name}[ \t]+([0-9]+)"
        _talus_macro_match "${_talus_version_header_contents}"
    )
    if(NOT _talus_macro_match)
        message(FATAL_ERROR "Could not find ${macro_name} in ${_talus_version_header}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_talus_read_version_string out_var)
    string(REGEX MATCH "#[ \t]*define[ \t]+TALUS_VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
        _talus_string_match "${_talus_version_header_contents}"
    )
    if(NOT _talus_string_match)
        message(FATAL_ERROR "Could not find TALUS_VERSION_STRING in ${_talus_version_header}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

_talus_read_version_macro(TALUS_VERSION_MAJOR _talus_header_version_major)
_talus_read_version_macro(TALUS_VERSION_MINOR _talus_header_version_minor)
_talus_read_version_macro(TALUS_VERSION_PATCH _talus_header_version_patch)
_talus_read_version_string(_talus_header_version_string)

if(NOT _talus_header_version_major EQUAL PROJECT_VERSION_MAJOR OR
   NOT _talus_header_version_minor EQUAL PROJECT_VERSION_MINOR OR
   NOT _talus_header_version_patch EQUAL PROJECT_VERSION_PATCH OR
   NOT _talus_header_version_string STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "Talus header version macros "
        "${_talus_header_version_major}.${_talus_header_version_minor}.${_talus_header_version_patch} "
        "(${_talus_header_version_string}) "
        "do not match CMake project version ${PROJECT_VERSION}"
    )
endif()

_talus_read_version_macro(TALUS_VERSION _talus_header_version)
math(EXPR _talus_expected_combined_version
    "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}"
)
if(NOT _talus_header_version EQUAL _talus_expected_combined_version)
    message(FATAL_ERROR
        "TALUS_VERSION=${_talus_header_version} does not match expected combined "
        "version ${_talus_expected_combined_version}"
    )
endif()

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

# Warnings and sanitizers apply only to Talus's own targets (tests, examples,
# benchmarks) — never to the public `talus` interface — so consumers are
# unaffected by how we choose to build the project itself.
option(TALUS_ENABLE_WARNINGS    "Build Talus's own targets with -Wall -Wextra -Wpedantic (/W4 on MSVC)" ${_talus_top_level})
option(TALUS_WARNINGS_AS_ERRORS "Treat warnings as errors for Talus's own targets"                      OFF)
option(TALUS_ENABLE_SANITIZERS  "Build Talus's own targets with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# ── Generated file cleanup ───────────────────────────────────────────────────

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clean-cmake.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/clean-cmake.cmake"
    @ONLY
)

add_custom_target(clean-cmake
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/clean-cmake.cmake"
    COMMENT "Removing generated CMake files from ${CMAKE_CURRENT_BINARY_DIR}"
)

# ── Library target (header-only interface) ───────────────────────────────────

add_library(talus INTERFACE)
add_library(talus::talus ALIAS talus)

target_include_directories(talus INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(talus INTERFACE cxx_std_20)

# ── Internal build options (warnings + sanitizers) ───────────────────────────

# An INTERFACE target carrying compiler flags for Talus's *own* targets. It is
# linked PRIVATELY by tests/examples/benchmarks and is never part of the
# installed/exported `talus` interface, so downstream consumers never see it.
add_library(talus_dev_options INTERFACE)

if(TALUS_ENABLE_WARNINGS)
    target_compile_options(talus_dev_options INTERFACE
        # /wd4324: 'structure was padded due to alignment specifier'. Talus's
        # node and pool types are deliberately cache-line aligned, so this MSVC
        # warning is categorical noise for our own targets. Public consumers are
        # unaffected (this is a dev-only target); the public node.hpp suppresses
        # it locally for downstream /W4 builds.
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/wd4324>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
    )
    if(TALUS_WARNINGS_AS_ERRORS)
        target_compile_options(talus_dev_options INTERFACE
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
        )
    endif()
endif()

if(TALUS_ENABLE_SANITIZERS)
    if(MSVC)
        message(WARNING "TALUS_ENABLE_SANITIZERS: only AddressSanitizer is available with MSVC; UBSan is skipped.")
        target_compile_options(talus_dev_options INTERFACE /fsanitize=address)
    else()
        target_compile_options(talus_dev_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(talus_dev_options INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()

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

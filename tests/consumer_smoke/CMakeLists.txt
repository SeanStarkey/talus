cmake_minimum_required(VERSION 3.20)
project(talus_consumer_smoke LANGUAGES CXX)

option(TALUS_CONSUMER_USE_FETCHCONTENT "Consume Talus through FetchContent instead of find_package" OFF)
set(TALUS_FETCHCONTENT_SOURCE_DIR "" CACHE PATH "Local Talus source tree for the FetchContent smoke test")

if(TALUS_CONSUMER_USE_FETCHCONTENT)
    if(NOT EXISTS "${TALUS_FETCHCONTENT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "TALUS_FETCHCONTENT_SOURCE_DIR must point at a Talus source tree")
    endif()

    include(FetchContent)
    FetchContent_Declare(talus
        SOURCE_DIR "${TALUS_FETCHCONTENT_SOURCE_DIR}"
    )
    FetchContent_MakeAvailable(talus)
else()
    find_package(talus CONFIG REQUIRED)
endif()

add_executable(talus_consumer_smoke main.cpp)
target_link_libraries(talus_consumer_smoke PRIVATE talus::talus)
target_compile_features(talus_consumer_smoke PRIVATE cxx_std_20)

enable_testing()
add_test(NAME talus_consumer_smoke COMMAND talus_consumer_smoke)

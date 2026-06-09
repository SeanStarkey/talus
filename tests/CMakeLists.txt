# Each test is a standalone executable validated via ctest. They link the
# internal `talus_dev_options` target so warnings (and sanitizers, when enabled)
# apply to the test code and the library headers it pulls in.
set(_talus_tests
    foundation_tests
    pool_alloc_tests
    node_tests
    algorithms_tests
    rtree_tests
    rtree_exception_tests
)

foreach(_test IN LISTS _talus_tests)
    add_executable(talus_${_test} ${_test}.cpp)
    target_link_libraries(talus_${_test} PRIVATE talus::talus talus_dev_options)
    add_test(NAME talus_${_test} COMMAND talus_${_test})
endforeach()

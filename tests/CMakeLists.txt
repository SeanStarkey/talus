add_executable(talus_foundation_tests
    foundation_tests.cpp
)

target_link_libraries(talus_foundation_tests PRIVATE talus::talus)

add_test(NAME talus_foundation_tests COMMAND talus_foundation_tests)

add_executable(talus_pool_alloc_tests
    pool_alloc_tests.cpp
)

target_link_libraries(talus_pool_alloc_tests PRIVATE talus::talus)

add_test(NAME talus_pool_alloc_tests COMMAND talus_pool_alloc_tests)

add_executable(talus_foundation_tests
    foundation_tests.cpp
)

target_link_libraries(talus_foundation_tests PRIVATE talus::talus)

add_test(NAME talus_foundation_tests COMMAND talus_foundation_tests)

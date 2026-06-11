# Benchmark targets link PRIVATE against talus::talus and talus_dev_options so
# they inherit the project warning/sanitizer flags.
#
# Build with -DTALUS_BUILD_BENCHMARKS=ON and -DCMAKE_BUILD_TYPE=Release;
# benchmark numbers from unoptimized builds are not meaningful.

add_executable(talus_benchmarks rtree_benchmarks.cpp)
target_link_libraries(talus_benchmarks PRIVATE talus::talus talus_dev_options)

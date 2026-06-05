add_executable(talus_rstar_driver rstar_driver.cpp)
target_link_libraries(talus_rstar_driver PRIVATE talus::talus)

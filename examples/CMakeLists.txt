add_executable(talus_driver talus_driver.cpp)
target_link_libraries(talus_driver PRIVATE talus::talus)

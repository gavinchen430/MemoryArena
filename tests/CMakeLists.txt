include_directories($(CMAKE_CURRENT_SOURCE_DIR)/../include/arena)

add_executable(test test.cpp)
target_link_libraries(test PRIVATE arena)

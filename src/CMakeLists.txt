include_directories($(CMAKE_CURRENT_SOURCE_DIR)/../include/arena)
set(ARENA_SRCS
    ArenaAllocator.cpp
)

add_library(arena STATIC ${ARENA_SRCS})

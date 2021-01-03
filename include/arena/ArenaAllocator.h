#pragma once

#include "Allocator.h"

class ArenaAllocator : public BasicAllocator<ArenaAllocator> {
    void* malloc(size_t nbytes);
    void* calloc(size_t nbytes);
    void free(void* p);
    friend class BasicAllocator<ArenaAllocator>;
};

#pragma once

#include <cstddef>

template<typename AllocatorImpl>
class BasicAllocator {
public:
    void* malloc(size_t nbytes) {
        return get()->malloc(nbytes);
    }
    void* calloc(size_t nbytes) {
        return get()->calloc(nbytes);
    }
    void free(void* p) {
        return get()->free(p);
    }
private:
    AllocatorImpl* get() {
        return static_cast<AllocatorImpl*>(this);
    }
};

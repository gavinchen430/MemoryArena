#include <stdio.h>
#include "ArenaAllocator.h"

using Allocator = BasicAllocator<ArenaAllocator>;

void test1() {
    Allocator a;
    void* p = a.malloc(3);
    void* p1 = a.malloc(3);
    void* p2 = a.malloc(8);
    for (int i = 0; i < 510; ++i) {
        p2 = a.malloc(8);
    }

    printf("%p, %p, %p, dis:%lu\n", p, p1, p2, ((char*)p2 - (char*)p));
}

void test2() {
    Allocator a;
    void* p = a.malloc(3);
    void* p1 = a.malloc(9);

    void* p2 = a.malloc(9);
    //void* p2 = a.malloc(17);

    printf("%p, %p, dis:%lu\n", p, p1, ((char*)p1 - (char*)p));
    //printf("%p, %p, %p, dis:%d\n", p, p1, p2, ((char*)p2 - (char*)p));
}

void test3() {
    Allocator a;
    void* p = a.malloc(3);
    void* p1 = a.malloc(9);

    void* p2 = a.malloc(9);
    //void* p2 = a.malloc(17);

    printf("%p, %p, %p, dis:%lu\n", p, p1, p2, ((char*)p2 - (char*)p));
    a.free(p1);
    a.free(p2);
    void* p3 = a.malloc(9);
    void* p4 = a.malloc(9);
    printf("%p, %p, %p, %p, dis:%lu\n", p, p2, p3,p4, ((char*)p3 - (char*)p));
}

int main() {
    //test1();
    //TODO:
    test3();
    return 0;
}

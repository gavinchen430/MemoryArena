#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "ArenaAllocator.h"
#include "ArenaConfig.h"

#include <stdio.h>

#define UNLIKELY(value) __builtin_expect((value), 0)
#define LIKELY(value) __builtin_expect((value), 1)

#define PTA(x)  ((poolp )((uint8_t *)&(usedpools[2*(x)]) - 2*sizeof(block *)))
#define PT(x)   PTA(x), PTA(x)

#define ALIGNMENT 8 /* must be 2^N */
#define ALIGNMENT_SHIFT 3
#define NB_SMALL_SIZE_CLASSES (SMALL_REQUEST_THRESHOLD / ALIGNMENT)

#define POOL_OVERHEAD ((sizeof(struct pool_header) + (ALIGNMENT -1)) / ALIGNMENT * ALIGNMENT)
#define DUMMY_SIZE_IDX 0xffff /* size class of newly cached pools */

#define _Py_ALIGN_DOWN(p, a) ((void *)((uintptr_t)(p) & ~(uintptr_t)((a) - 1)))
#define POOL_ADDR(P) ((poolp)(_Py_ALIGN_DOWN((P), POOL_SIZE) + 16))

#define INDEX2SIZE(I) (((uint)(I) + 1) << ALIGNMENT_SHIFT)

typedef uint8_t block;

struct pool_header {
    union {block* _padding;
        uint count;} ref;
    block* freeblock;
    struct pool_header* nextpool;
    struct pool_header* prevpool;
    uint arenaindex;
    uint szidx;
    uint nextoffset;
    uint maxnextoffset;
};

struct arena_object {
    uintptr_t address;
    block* pool_address;
    uint nfreepools;
    uint ntotalpools;
    struct pool_header* freepools;
    struct arena_object* nextarena;
    struct arena_object* prevarena;
};

typedef struct pool_header* poolp;

static poolp usedpools[2 * ((NB_SMALL_SIZE_CLASSES + 7) / 8) * 8] = {
    PT(0), PT(1), PT(2), PT(3), PT(4), PT(5), PT(6), PT(7)
#if NB_SMALL_SIZE_CLASSES > 8
    , PT(8), PT(9), PT(10), PT(11), PT(12), PT(13), PT(14), PT(15)
#if NB_SMALL_SIZE_CLASSES > 16
    , PT(16), PT(17), PT(18), PT(19), PT(20), PT(21), PT(22), PT(23)
#if NB_SMALL_SIZE_CLASSES > 24
    , PT(24), PT(25), PT(26), PT(27), PT(28), PT(29), PT(30), PT(31)
#if NB_SMALL_SIZE_CLASSES > 32
    , PT(32), PT(33), PT(34), PT(35), PT(36), PT(37), PT(38), PT(39)
#if NB_SMALL_SIZE_CLASSES > 40
    , PT(40), PT(41), PT(42), PT(43), PT(44), PT(45), PT(46), PT(47)
#if NB_SMALL_SIZE_CLASSES > 48
    , PT(48), PT(49), PT(50), PT(51), PT(52), PT(53), PT(54), PT(55)
#if NB_SMALL_SIZE_CLASSES > 56
    , PT(56), PT(57), PT(58), PT(59), PT(60), PT(61), PT(62), PT(63)
#if NB_SMALL_SIZE_CLASSES > 64
#error "NB_SMALL_SIZE_CLASSES should be less than 64"
#endif /* NB_SMALL_SIZE_CLASSES > 64 */
#endif /* NB_SMALL_SIZE_CLASSES > 56 */
#endif /* NB_SMALL_SIZE_CLASSES > 48 */
#endif /* NB_SMALL_SIZE_CLASSES > 40 */
#endif /* NB_SMALL_SIZE_CLASSES > 32 */
#endif /* NB_SMALL_SIZE_CLASSES > 24 */
#endif /* NB_SMALL_SIZE_CLASSES > 16 */
#endif /* NB_SMALL_SIZE_CLASSES >  8 */
};

static struct arena_object* arenas = NULL;
static uint maxarenas = 0;
static struct arena_object* unused_arena_objects = NULL;
static struct arena_object* usable_arenas = NULL;
/* nfp2lasta[nfp] is the last arena in usable_arenas with nfp free pools */
static struct arena_object* nfp2lasta[MAX_POOLS_IN_ARENA + 1] = { NULL };
static size_t narenas_currently_allocated = 0;


static struct arena_object* new_arena() {
    struct arena_object* arenaobj;
    void* address;
    if (unused_arena_objects == NULL) {
        uint i;
        uint numarenas;
        size_t nbytes;
        numarenas = maxarenas ? maxarenas << 1 : 16;
        if (numarenas <= maxarenas) {
            return NULL;
        }

        nbytes = numarenas * sizeof(*arenas);
        //TODO: ::realloc
        arenaobj = (struct arena_object*) ::malloc(nbytes);
        if (arenaobj == NULL) return NULL;
        arenas = arenaobj;
        assert(usable_arenas == NULL);
        assert(unused_arena_objects == NULL);
        for (i = maxarenas; i < numarenas; ++i) {
            arenas[i].address = 0;
            arenas[i].nextarena = i < numarenas - 1 ? &arenas[i+1] : NULL;
        }
        unused_arena_objects = &arenas[maxarenas];
        maxarenas = numarenas;
    }
    assert(unused_arena_objects != NULL);
    arenaobj = unused_arena_objects;
    unused_arena_objects = arenaobj->nextarena;
    assert(arenaobj->address == 0);
    address = ::malloc(ARENA_SIZE);
    //::memset(address, 0, ARENA_SIZE);
    printf("new arena address=%p\n", address);
    if (address == NULL) {
        arenaobj->nextarena = unused_arena_objects;
        unused_arena_objects = arenaobj;
        return NULL;
    }
    arenaobj->address = (uintptr_t)address;
    ++narenas_currently_allocated;
    arenaobj->freepools = NULL;
    arenaobj->pool_address = (block*)arenaobj->address;
    arenaobj->nfreepools = MAX_POOLS_IN_ARENA;
    return arenaobj;
}

static void* allocate_from_new_pool(uint size) {
    if (UNLIKELY(usable_arenas == NULL)) {
        usable_arenas = new_arena();
        printf("usable_arenas == null:%d\n", usable_arenas == NULL);
        if (usable_arenas == NULL) {
            return NULL;
        }
        usable_arenas->nextarena = usable_arenas->prevarena = NULL;
        assert(nfp2lasta[usable_arenas->nfreepools] == NULL);
        nfp2lasta[usable_arenas->nfreepools] = usable_arenas;
    }
    assert(usable_arenas->address != 0);
    assert(usable_arenas->nfreepools > 0);
    if (nfp2lasta[usable_arenas->nfreepools] == usable_arenas) {
        nfp2lasta[usable_arenas->nfreepools] = NULL;
    }
    if (usable_arenas->nfreepools > 1) {
        assert(nfp2lasta[usable_arenas->nfreepools - 1] == NULL);
        nfp2lasta[usable_arenas->nfreepools - 1] = usable_arenas;
    }

    poolp pool = usable_arenas->freepools;
    printf("pool == null:%d\n", pool == NULL);
    if (LIKELY(pool != NULL)) {
        usable_arenas->freepools = pool->nextpool;
    } else {
        /* Carve off a new pool */
        assert(usable_arenas->nfreepools > 0);
        assert(usable_arenas->freepools == NULL);
        pool = (poolp)usable_arenas->pool_address;
        printf("Carve off a new pool:%p\n", pool);
        assert((block*)pool <= (block*)usable_arenas->address + ARENA_SIZE - POOL_SIZE);
        pool->arenaindex = (uint)(usable_arenas - arenas);

        assert(&arenas[pool->arenaindex] == usable_arenas);
        pool->szidx = DUMMY_SIZE_IDX;
        usable_arenas->pool_address += POOL_SIZE;
        --usable_arenas->nfreepools;
        if (usable_arenas->nfreepools == 0) {
            assert(usable_arenas->nextarena == NULL
                    || usable_arenas->nextarena->prevarena == usable_arenas);
            usable_arenas = usable_arenas->nextarena;
            if (usable_arenas != NULL) {
                usable_arenas->prevarena = NULL;
                assert(usable_arenas->address != 0);
            }
        }
    }

    block* bp;
    poolp next = usedpools[size + size];
    printf("usedpools :%p\n", next);
    pool->nextpool = next;
    pool->prevpool = next;
    next->nextpool = pool;
    next->prevpool = pool;
    pool->ref.count = 1;
    if (pool->szidx == size) {
        bp = pool->freeblock;
        assert(bp != NULL);
        pool->freeblock = *(block**)bp;
        return bp;
    }
    pool->szidx = size;
    size = INDEX2SIZE(size);
    bp = (block*)pool + POOL_OVERHEAD;
    pool->nextoffset = POOL_OVERHEAD + (size << 1);
    printf("POOL_OVERHEAD:%d, pool->nextoffset=%d, size=%d, pool=%p\n", POOL_OVERHEAD, pool->nextoffset, size, pool);
    pool->maxnextoffset = POOL_SIZE - size;
    pool->freeblock = bp + size;
    *(block**)(pool->freeblock) = NULL;
    printf("allocate_from_new_pool freeblock:%p, %p\n", (block*)pool->freeblock, *(block**)(pool->freeblock));
    return bp;
}

static void malloc_pool_extend(poolp pool, uint size) {
    //TODO: why UNLIKELY
    if (UNLIKELY(pool->nextoffset <= pool->maxnextoffset)) {
        printf("malloc_pool_extend unlikely, pool=%p, nextoffset=%d, maxnextoffset=%d\n", pool, pool->nextoffset, pool->maxnextoffset);
        pool->freeblock = (block*)pool + pool->nextoffset;
        pool->nextoffset += INDEX2SIZE(size);
        printf("pool->freeblock:%p, %p, nextoffsetPtr:%p\n", pool->freeblock, (block**)(pool->freeblock), &(pool->nextoffset));
        *(block**)(pool->freeblock) = NULL;
        printf("malloc_pool_extend2 unlikely, pool=%p, nextoffset=%d, maxnextoffset=%d\n", pool, pool->nextoffset, pool->maxnextoffset);
        return;
    }
    printf("**************\n");
    poolp next;
    next = pool->nextpool;
    printf("pool:%p, next:%p, next->prevpool:%p\n", pool, next, next->prevpool);
    pool = pool->prevpool;
    printf("pool:%p\n", pool);
    next->prevpool = pool;
    pool->nextpool = next;
}

void* ArenaAllocator::malloc(size_t nbytes) {
    if (UNLIKELY(nbytes == 0)) {
        return NULL;
    }
    if (UNLIKELY(nbytes > SMALL_REQUEST_THRESHOLD)) {
        return NULL;
    }
    uint size = (uint)(nbytes-1)>>ALIGNMENT_SHIFT;
    poolp pool = usedpools[size + size];
    printf("malloc size:%d, usedpools:%p\n", size, pool);
    block* bp = NULL;
    printf("pool != pool->nextpool %d\n", pool != pool->nextpool);
    if (pool != pool->nextpool) {
        /*
         * There is a used pool for this size class.
         * Pick up the head block of its free list.
         */
        ++pool->ref.count;
        bp = pool->freeblock;
        assert(bp != NULL);
        if (UNLIKELY((pool->freeblock = *(block **)bp) == NULL)) {
            // Reached the end of the free list, try to extend it.
            printf("malloc_pool_extend -----------\n");
            malloc_pool_extend(pool, size);
        }
    } else {
        bp = (block*)allocate_from_new_pool(size);
    }
    printf("ref count:%d\n", usedpools[size+size]->ref.count);
    return (void*)bp;
}

void* ArenaAllocator::calloc(size_t nbytes) {
    return ::malloc(nbytes);
}

static bool address_in_range(void* p, poolp pool) {
    uint arenaindex = *((volatile uint*)&pool->arenaindex);
    bool r = arenaindex < maxarenas && 
        (uintptr_t)p - arenas[arenaindex].address < ARENA_SIZE
        && arenas[arenaindex].address != 0;
    printf("address_in_range arenaindex:%u, maxarenas:%u, r:%d\n",
            pool->arenaindex, maxarenas, r);
    return r;
}

static void
insert_to_freepool(poolp pool) {
    poolp next = pool->nextpool;
    poolp prev = pool->prevpool;
    next->prevpool = prev;
    prev->nextpool = next;

    struct arena_object* ao = &arenas[pool->arenaindex];
    pool->nextpool = ao->freepools;
    ao->freepools = pool;
    uint nf = ao->nfreepools;

    struct arena_object* lastnf = nfp2lasta[nf];
    assert((nf == 0 && lastnf == NULL) ||
            (nf > 0 &&
             lastnf != NULL &&
             lastnf->nfreepools == nf &&
             (lastnf->nextarena == NULL ||
              nf < lastnf->nextarena->nfreepools)));
    if (lastnf == ao) {
        struct arena_object* p = ao->prevarena;
        nfp2lasta[nf] = (p != NULL && p->nfreepools == nf) ? p : NULL;
    }
    ao->nfreepools = ++nf;
    if (nf == ao->ntotalpools && ao->nextarena != NULL) {
        assert(ao->prevarena == NULL ||
                ao->prevarena->address != 0);
        assert(ao->nextarena == NULL ||
                ao->nextarena->address != 0);
        if (ao->prevarena == NULL) {
            usable_arenas = ao->nextarena;
            assert(usable_arenas == NULL ||
                    usable_arenas->address != 0);
        } else {
            assert(ao->prevarena->nextarena == ao);
            ao->prevarena->nextarena = ao->nextarena;
        }
        if (ao->nextarena != NULL) {
            assert(ao->nextarena->prevarena == ao);
            ao->nextarena->prevarena = ao->prevarena;
        }
        ao->nextarena = unused_arena_objects;
        unused_arena_objects = ao;

        printf("------------\n");
        ::free((void*)ao->address);
        ao->address = 0;
        --narenas_currently_allocated;
        return;
    }
    if (nf == 1) {
        ao->nextarena = usable_arenas;
        ao->prevarena = NULL;
        if (usable_arenas) {
            usable_arenas->prevarena = ao;
        }
        usable_arenas = ao;
        assert(usable_arenas->address != 0);
        if (nfp2lasta[1] == NULL) {
            nfp2lasta[1] = ao;
        }
        return;
    }
    if (nfp2lasta[nf] == NULL) {
        nfp2lasta[nf] = ao;
    }
    if (ao == lastnf) {
        /* Case 4.  Nothing to do. */
        return;
    }
    assert(ao->nextarena != NULL);

    if (ao->prevarena != NULL) {
        assert(ao->prevarena->nextarena == ao);
        ao->prevarena->nextarena = ao->nextarena;
    } else {
        assert(usable_arenas == ao);
        usable_arenas = ao->nextarena;
    }
    ao->nextarena->prevarena = ao->prevarena;

    ao->prevarena = lastnf;
    ao->nextarena = lastnf->nextarena;
    if (ao->nextarena != NULL) {
        ao->nextarena->prevarena = ao;
    }

    lastnf->nextarena = ao;
    assert(ao->nextarena == NULL || nf <= ao->nextarena->nfreepools);
    assert(ao->prevarena == NULL || nf > ao->prevarena->nfreepools);
    assert(ao->nextarena == NULL || ao->nextarena->prevarena == ao);
    assert((usable_arenas == ao && ao->prevarena == NULL)
            || ao->prevarena->nextarena == ao);

}

static int _free(void* p) {
    assert(p != NULL);
    poolp pool = POOL_ADDR(p);
    printf("_free pool addr:%p, sizeof header:%d\n", pool, sizeof(pool_header));
    if (UNLIKELY(!address_in_range(p, pool))) {
        printf("_free return 0\n");
        return 0;
    }
    assert(pool->ref.count > 0);
    block* lastfree = pool->freeblock;
    *(block**)p = lastfree;
    pool->freeblock = (block*)p;
    pool->ref.count--;
    printf("_free -------pool lastFree==null:%p\n", lastfree);
    if (UNLIKELY(lastfree == NULL)) {
        insert_to_freepool(pool);
        return 1;
    }
    if (LIKELY(pool->ref.count != 0)) {
        return 1;
    }
    insert_to_freepool(pool);
    return 1;
}

void ArenaAllocator::free(void* p) {
    if (p == NULL) return;
    if (UNLIKELY(!_free(p))) {
        assert(false);
        /* pymalloc didn't allocate this address */
        ::free(p);
        //raw_allocated_blocks--;
    }
}

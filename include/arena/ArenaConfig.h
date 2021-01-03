#pragma once

#define SYSTEM_PAGE_SIZE (4 << 10) /*4K*/

#ifndef MEMORY_LIMIT
#define MEMORY_LIMIT (64 << 20) /*64M*/
#endif

#ifndef ARENA_SIZE
#define ARENA_SIZE (256 << 10) /*256K*/
#endif

#define MAX_ARENA (MEMORY_LIMIT / ARENA_SIZE)

#define POOL_SIZE SYSTEM_PAGE_SIZE
#define MAX_POOLS_IN_ARENA (ARENA_SIZE / POOL_SIZE)

#define SMALL_REQUEST_THRESHOLD 512

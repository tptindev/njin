#pragma once
// The allocator under njin: every C++ new/delete and every allocation raylib
// makes (RL_MALLOC and its siblings, redirected here by CMakeLists.txt) goes
// through it, so the engine knows how many bytes the game holds and can stop
// it at a limit. It lives inside the raylib target, so every program that
// links raylib has it. Private to the engine: the game reaches it through
// njin_mem.h.
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *njin_rl_malloc(size_t n);
void *njin_rl_calloc(size_t count, size_t size);
void *njin_rl_realloc(void *p, size_t n);
void njin_rl_free(void *p);

enum {
  NJIN_MEM_GROUPS = 2,   // 0: C++ new/delete, 1: raylib
  NJIN_MEM_BUCKETS = 16, // bucket b holds sizes up to 16 << b; the last one is open
};

typedef struct njin_mem_snapshot {
  size_t used;                           // live bytes, all groups
  size_t peak;                           // highest `used` so far
  size_t soft, hard;                     // limits in bytes, 0 is none
  size_t group_used[NJIN_MEM_GROUPS];    // live bytes per group
  unsigned long long allocs, frees;      // calls so far
  unsigned long long refused;            // allocations the hard limit turned down
  size_t bucket_count[NJIN_MEM_BUCKETS]; // live blocks per size class
  size_t bucket_bytes[NJIN_MEM_BUCKETS]; // live bytes per size class
} njin_mem_snapshot;

void njin_mem_get(njin_mem_snapshot *out);
// 0 turns a limit off. Safe from any thread.
void njin_mem_set_limits(size_t soft, size_t hard);
// 1 once if an allocation went past the soft limit since the last call.
int njin_mem_take_pressure(void);
void njin_mem_reset_peak(void);

#ifdef __cplusplus
}
#endif

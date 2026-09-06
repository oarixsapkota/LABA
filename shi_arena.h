/**
 * SHI_ARENA (Region Based Arena Allocator)
 *Original: https://github.com/oarixsapkota/SHI/blob/main/shi_arena.h
 *
 * IMPLEMENTATION:
 * Define SHI_ARENA_IMPLEMENTATION in exactly one C/C++ file before including
 * this header to instantiate the implementation block.
 *
 * #define SHI_ARENA_IMPLEMENTATION
 * #include "shi_arena.h"
 *
 * @note The arena is not thread-safe unless external synchronization is
 *       provided.
 *
 * @brief A single memory region belonging to an arena.
 *
 * An arena consists of one or more regions linked together as a singly
 * linked list. Each region owns a contiguous block of memory represented
 * by the flexible array member `data`.
 *
 * The `offset` and `cap` fields are measured in units of `uintptr_t`,
 * not bytes:
 *
 *     1 word = sizeof(uintptr_t) bytes
 *
 * `offset` represents the number of words already consumed in the region.
 * `cap` represents the total number of words available in the region.
 *
 * The usable memory range is:
 *
 *     data[0] ... data[cap - 1]
 *
 * and the next allocation begins at:
 *
 *     data[offset]
 *
 * @note Regions are owned by their parent arena and should not normally
 *       be created, modified, or freed independently by the caller.
 *
 *
 * @brief Memory arena consisting of a chain of reusable regions.
 *
 * The arena maintains three region pointers:
 *
 *     begin
 *       |
 *       v
 *     region -> region -> region -> NULL
 *                              ^
 *                              |
 *                             end
 *
 * `current` points to the region currently being used for allocations.
 *
 * When the current region does not have enough remaining space, the
 * allocator advances through existing regions. If none of the existing
 * regions can satisfy the allocation, a new region is allocated and
 * appended to the end of the chain.
 *
 * The arena does not individually free allocations. Memory remains owned
 * by the arena until the arena itself is destroyed with `free_arena()`.
 *
 * Calling `arena_reset()` makes all existing regions reusable without
 * returning their memory to the operating system.
 */

#ifndef SHI_ARENA_H
#define SHI_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct __arena_region__ __arena_region__;

struct __arena_region__ {
  __arena_region__ *next; /// Next region in the arena's region chain.
  size_t offset;          /// Number of uintptr_t words currently used in this region.
  size_t cap;             /// Total capacity of this region in uintptr_t words.
  uintptr_t data[];       /// Flexible array containing the region's storage.
};

typedef struct {
  size_t region_size;        /// Default region capacity in uintptr_t words.
  __arena_region__ *begin;   /// First region in the arena's region chain.
  __arena_region__ *current; /// Region currently receiving allocations.
  __arena_region__ *end;     /// Last region in the arena's region chain.
} __arena__;

/**
 * @brief Public alias for __arena__.
 */
#define Arena __arena__

/**
 * @brief Initializes a new arena region.
 *
 * Allocates a single region capable of storing at least `cap`
 * uintptr_t words.
 *
 * The returned region starts empty:
 *
 *     region->offset == 0
 *
 * and has no successor:
 *
 *     region->next == NULL
 *
 * @param cap
 *     Number of uintptr_t words to reserve for the region payload.
 *
 * @return
 *     A pointer to the newly allocated region on success.
 *     NULL if the requested allocation would overflow or if memory
 *     allocation fails.
 *
 * @note `cap` is measured in uintptr_t words, not bytes.
 *
 * @note The returned region is not automatically attached to an arena.
 *       Use `push_new_arena_region()` when a region needs to become
 *       part of an arena's region chain.
 */
__arena_region__ *init_arena_region(size_t cap);

/**
 * @brief Initializes a new memory arena.
 *
 * Creates an empty arena with no allocated regions.
 *
 * The supplied `region_size` determines the default size of regions
 * allocated by the arena.
 *
 * If `region_size` is zero, the implementation uses
 * `ARENA_REGION_DEFAULT_CAPACITY` as the default region capacity.
 *
 * The supplied size is specified in bytes and is internally rounded
 * up to the nearest number of uintptr_t words.
 *
 * @param region_size
 *     Default region size in bytes.
 *     A value of zero selects the implementation's default capacity.
 *
 * @return
 *     A pointer to a newly initialized arena on success.
 *     NULL if the arena cannot be allocated or the requested size
 *     cannot be represented safely.
 *
 * @note No memory regions are allocated by this function itself.
 *       Regions are allocated lazily when `arena_alloc()` is first called.
 *
 * @note The returned arena must eventually be released with
 *       `free_arena()`.
 */
__arena__ *init_arena(size_t region_size);

/**
 * @brief Appends a new region to an arena.
 *
 * Allocates a new region and appends it to the end of the arena's
 * region chain.
 *
 * The new region's capacity is the larger of:
 *
 *     arena->region_size
 *     min_cap
 *
 * This allows large individual allocations to receive a region large
 * enough to hold the requested allocation instead of repeatedly
 * allocating smaller regions.
 *
 * After successful insertion:
 *
 *     arena->end     == new region
 *     arena->current == new region
 *
 * If the arena has no regions, the new region becomes `begin`,
 * `current`, and `end`.
 *
 * @param arena
 *     Arena to which the new region will be appended.
 *
 * @param min_cap
 *     Minimum capacity of the new region in uintptr_t words.
 *
 * @return
 *     Non-zero on success.
 *     Zero if `arena` is NULL or the new region cannot be allocated.
 *
 * @note The caller normally does not need to call this function directly.
 *       `arena_alloc()` automatically creates regions when required.
 */
int push_new_arena_region(__arena__ *arena, size_t min_cap);

/**
 * @brief Allocates memory from an arena.
 *
 * Reserves at least `size` bytes from the arena and returns a pointer to
 * the beginning of the allocated memory.
 *
 * The requested byte count is rounded up to the nearest multiple of
 * `sizeof(uintptr_t)` so that subsequent allocations remain correctly
 * aligned for the arena's storage unit.
 *
 * Allocation proceeds as follows:
 *
 * 1. If the arena has no regions, a new region is created.
 *
 * 2. The current region is checked for sufficient remaining capacity.
 *
 * 3. If necessary, the allocator advances through already allocated
 *    regions looking for one with enough free space.
 *
 * 4. If no existing region can satisfy the allocation, a new region
 *    is appended with enough capacity for the request.
 *
 * 5. The requested space is reserved and the region's offset is advanced.
 *
 * The arena does not support individual deallocation. The returned
 * memory remains valid until the arena is reset or freed.
 *
 * @param arena
 *     Arena from which to allocate memory.
 *
 * @param size
 *     Number of bytes to allocate.
 *
 * @return
 *     Pointer to the allocated memory on success.
 *     NULL if `arena` is NULL, `size` is zero, the requested size
 *     cannot be represented safely, or memory allocation fails.
 *
 * @warning Memory returned by this function becomes invalid for reuse
 *          after `arena_reset()` and invalid after `free_arena()`.
 *
 * @note The returned memory is aligned according to the arena's
 *       uintptr_t-based storage.
 *
 * @note Individual allocations cannot be freed.
 *
 */
void *arena_alloc(__arena__ *arena, size_t size);

/**
 * @brief Copies data into newly allocated arena memory.
 *
 * Allocates `size` bytes from the arena and copies the contents of
 * `data` into the newly allocated memory.
 *
 * This is equivalent to:
 *
 *     void *dest = arena_alloc(arena, size);
 *     memcpy(dest, data, size);
 *
 * except that the operation handles allocation failure internally.
 *
 * @param arena
 *     Arena into which the copy will be allocated.
 *
 * @param data
 *     Pointer to the source data.
 *
 * @param size
 *     Number of bytes to copy.
 *
 * @return
 *     Pointer to the copied data on success.
 *     NULL if `arena` is NULL, `data` is NULL, `size` is zero,
 *     or the allocation fails.
 *
 * @warning The source memory must contain at least `size` readable bytes.
 *
 * @warning The returned memory is owned by the arena and becomes
 *          reusable after `arena_reset()` and invalid after
 *          `free_arena()`.
 *
 * @note This function does not take ownership of `data`.
 *       The caller remains responsible for the source memory.
 */
void *arena_memdup(__arena__ *arena, const void *data, size_t size);

/**
 * @brief Resets an arena for reuse.
 *
 * Sets the allocation offset of every region in the arena back to zero.
 * No region is freed.
 *
 * After a reset, the complete region chain becomes available for
 * subsequent allocations:
 *
 *     begin
 *       |
 *       v
 *     region -> region -> region -> region
 *                                  ^
 *                                  |
 *                                 end
 *                                  ^
 *                                  |
 *                               current
 *
 * All regions retain their previously allocated capacity. Therefore,
 * resetting an arena does not return memory to the operating system.
 *
 * This behavior is intentional and is useful when an arena is repeatedly
 * used for temporary allocations, such as:
 *
 * - parsers
 * - compilers
 * - AST construction
 * - frame/scratch allocations
 * - request-local temporary data
 * - short-lived object graphs
 *
 * For example:
 *
 *     Arena *arena = init_arena(4096);
 *
 *     for (...) {
 *         build_temporary_data(arena);
 *         arena_reset(arena);
 *     }
 *
 * The arena can reuse memory obtained during previous iterations instead
 * of repeatedly requesting memory from the system allocator.
 *
 * @param arena
 *     Arena to reset. NULL is safely ignored.
 *
 * @warning All allocations previously returned by the arena should be
 *          considered invalid for continued use after this operation.
 *          The memory itself is not necessarily cleared; it is simply
 *          marked as available for reuse.
 *
 * @note Resetting an arena does not reduce its memory footprint.
 *       If minimizing retained memory is more important than allocation
 *       performance, use `free_arena()` or provide a separate trimming
 *       operation.
 */
void arena_reset(__arena__ *arena);

/**
 * @brief Frees an entire arena and all of its regions.
 *
 * Walks through the arena's region chain and releases every allocated
 * region, then releases the arena itself.
 *
 * After this function returns, the arena pointer and every pointer
 * previously returned by `arena_alloc()` or `arena_memdup()` for that
 * arena must no longer be used.
 *
 * The destruction order is:
 *
 *     region 1
 *         ↓
 *     region 2
 *         ↓
 *     region 3
 *         ↓
 *       arena
 *
 * Passing NULL is safe and has no effect.
 *
 * @param arena
 *     Arena to destroy.
 *
 * @warning This function invalidates all memory previously allocated
 *          from the arena.
 *
 * @note After calling this function, do not attempt to call
 *       `arena_alloc()`, `arena_reset()`, or any other arena operation
 *       using the same pointer.
 */
void free_arena(__arena__ *arena);

#endif // SHI_ARENA_H

// =================
// TESTING & EXAMPLE
// =================
#ifdef SHI_TEST

#define SHI_ARENA_IMPLEMENTATION

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * SHI_TEST
 *
 * These tests intentionally exercise both normal behavior and
 * boundary/error cases.
 *
 */

#define SHI_TEST_CHECK(expr)                                                                                                   \
  do {                                                                                                                         \
    if (!(expr)) {                                                                                                             \
      fprintf(stderr, "shi_arena.h FAILED: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                          \
      return 1;                                                                                                                \
    }                                                                                                                          \
  } while (0)

typedef struct {
  char value;
  size_t col;
} Obj;

/* --------------------------------------------------------------- */
/* Initialization                                                  */
/* --------------------------------------------------------------- */

static int test_init(void) {
  Arena *arena = init_arena(64);

  SHI_TEST_CHECK(arena != NULL);
  SHI_TEST_CHECK(arena->begin == NULL);
  SHI_TEST_CHECK(arena->current == NULL);
  SHI_TEST_CHECK(arena->end == NULL);
  SHI_TEST_CHECK(arena->region_size >= 1);

  free_arena(arena);

  /* NULL destruction must be safe. */
  free_arena(NULL);

  return 0;
}

static int test_zero_operations(void) {
  Arena *arena = init_arena(64);

  SHI_TEST_CHECK(arena != NULL);

  /* Zero-byte allocation is explicitly rejected. */
  SHI_TEST_CHECK(arena_alloc(arena, 0) == NULL);

  /* Zero-byte duplication is rejected. */
  SHI_TEST_CHECK(arena_memdup(arena, "x", 0) == NULL);

  /* NULL source is rejected. */
  SHI_TEST_CHECK(arena_memdup(arena, NULL, 1) == NULL);

  /* NULL arena operations must be harmless. */
  SHI_TEST_CHECK(arena_alloc(NULL, 1) == NULL);
  SHI_TEST_CHECK(arena_memdup(NULL, "x", 1) == NULL);

  arena_reset(NULL);
  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Basic allocation                                                */
/* --------------------------------------------------------------- */

static int test_basic_allocation(void) {
  Arena *arena = init_arena(64);

  SHI_TEST_CHECK(arena != NULL);

  int *a = arena_alloc(arena, sizeof(*a));
  int *b = arena_alloc(arena, sizeof(*b));
  char *c = arena_alloc(arena, 32);

  SHI_TEST_CHECK(a != NULL);
  SHI_TEST_CHECK(b != NULL);
  SHI_TEST_CHECK(c != NULL);

  *a = 123;
  *b = 456;

  SHI_TEST_CHECK(*a == 123);
  SHI_TEST_CHECK(*b == 456);

  SHI_TEST_CHECK(arena->begin != NULL);
  SHI_TEST_CHECK(arena->current != NULL);
  SHI_TEST_CHECK(arena->end != NULL);

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Alignment                                                       */
/* --------------------------------------------------------------- */

static int test_alignment(void) {
  Arena *arena = init_arena(64);

  SHI_TEST_CHECK(arena != NULL);

  for (size_t i = 1; i <= 128; ++i) {
    void *ptr = arena_alloc(arena, i);

    SHI_TEST_CHECK(ptr != NULL);
    SHI_TEST_CHECK(((uintptr_t)ptr % sizeof(uintptr_t)) == 0);
  }

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Exact region boundaries                                         */
/* --------------------------------------------------------------- */

static int test_exact_capacity(void) {
  /*
   * Region size is specified in bytes and internally converted
   * to uintptr_t words.
   */
  Arena *arena = init_arena(sizeof(uintptr_t) * 4);

  SHI_TEST_CHECK(arena != NULL);

  void *a = arena_alloc(arena, sizeof(uintptr_t));
  void *b = arena_alloc(arena, sizeof(uintptr_t));
  void *c = arena_alloc(arena, sizeof(uintptr_t));
  void *d = arena_alloc(arena, sizeof(uintptr_t));

  SHI_TEST_CHECK(a != NULL);
  SHI_TEST_CHECK(b != NULL);
  SHI_TEST_CHECK(c != NULL);
  SHI_TEST_CHECK(d != NULL);

  SHI_TEST_CHECK(arena->begin != NULL);
  SHI_TEST_CHECK(arena->begin->offset == arena->begin->cap);

  /*
   * One more allocation must force another region.
   */
  void *e = arena_alloc(arena, sizeof(uintptr_t));

  SHI_TEST_CHECK(e != NULL);
  SHI_TEST_CHECK(arena->begin->next != NULL);
  SHI_TEST_CHECK(arena->current != arena->begin);

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Allocation requiring a new region                               */
/* --------------------------------------------------------------- */

static int test_region_growth(void) {
  Arena *arena = init_arena(sizeof(uintptr_t) * 2);

  SHI_TEST_CHECK(arena != NULL);

  void *a = arena_alloc(arena, sizeof(uintptr_t));
  void *b = arena_alloc(arena, sizeof(uintptr_t));
  void *c = arena_alloc(arena, sizeof(uintptr_t));

  SHI_TEST_CHECK(a != NULL);
  SHI_TEST_CHECK(b != NULL);
  SHI_TEST_CHECK(c != NULL);

  SHI_TEST_CHECK(arena->begin != NULL);
  SHI_TEST_CHECK(arena->end != NULL);
  SHI_TEST_CHECK(arena->begin != arena->end);
  SHI_TEST_CHECK(arena->begin->next == arena->end);
  SHI_TEST_CHECK(arena->current == arena->end);

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Oversized allocation                                            */
/* --------------------------------------------------------------- */

static int test_large_allocation(void) {
  Arena *arena = init_arena(16);

  SHI_TEST_CHECK(arena != NULL);

  /*
   * This is larger than the default region and therefore requires
   * a dedicated larger region.
   */
  const size_t large_size = 4096;

  unsigned char *ptr = arena_alloc(arena, large_size);

  SHI_TEST_CHECK(ptr != NULL);
  SHI_TEST_CHECK(arena->begin != NULL);
  SHI_TEST_CHECK(arena->current != NULL);
  SHI_TEST_CHECK(arena->current->cap >= (large_size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t));

  memset(ptr, 0xA5, large_size);

  for (size_t i = 0; i < large_size; ++i) {
    SHI_TEST_CHECK(ptr[i] == 0xA5);
  }

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* memdup                                                          */
/* --------------------------------------------------------------- */

static int test_memdup(void) {
  Arena *arena = init_arena(32);

  SHI_TEST_CHECK(arena != NULL);

  const char source[] = "hello arena";

  char *copy = arena_memdup(arena, source, sizeof(source));

  SHI_TEST_CHECK(copy != NULL);
  SHI_TEST_CHECK(memcmp(copy, source, sizeof(source)) == 0);

  /*
   * Verify that the destination is actually independent storage.
   */
  SHI_TEST_CHECK(copy != source);

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Reset and reuse                                                 */
/* --------------------------------------------------------------- */

static int test_reset(void) {
  Arena *arena = init_arena(32);

  SHI_TEST_CHECK(arena != NULL);

  void *first = arena_alloc(arena, 16);
  void *second = arena_alloc(arena, 16);
  void *third = arena_alloc(arena, 16);

  SHI_TEST_CHECK(first != NULL);
  SHI_TEST_CHECK(second != NULL);
  SHI_TEST_CHECK(third != NULL);

  __arena_region__ *begin = arena->begin;
  __arena_region__ *end = arena->end;

  SHI_TEST_CHECK(begin != NULL);
  SHI_TEST_CHECK(end != NULL);

  arena_reset(arena);

  SHI_TEST_CHECK(arena->begin == begin);
  SHI_TEST_CHECK(arena->end == end);
  SHI_TEST_CHECK(arena->current == arena->begin);

  /*
   * Every existing region must now be empty.
   */
  for (__arena_region__ *r = arena->begin; r != NULL; r = r->next) {
    SHI_TEST_CHECK(r->offset == 0);
  }

  /*
   * Allocation after reset must work.
   */
  void *after_reset = arena_alloc(arena, 16);

  SHI_TEST_CHECK(after_reset != NULL);

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Region reuse after reset                                        */
/* --------------------------------------------------------------- */

static int test_region_reuse(void) {
  Arena *arena = init_arena(sizeof(uintptr_t) * 2);

  SHI_TEST_CHECK(arena != NULL);

  /*
   * Force multiple regions.
   */
  for (size_t i = 0; i < 16; ++i) {
    SHI_TEST_CHECK(arena_alloc(arena, sizeof(uintptr_t)) != NULL);
  }

  __arena_region__ *begin = arena->begin;
  __arena_region__ *end = arena->end;

  SHI_TEST_CHECK(begin != NULL);
  SHI_TEST_CHECK(end != NULL);

  arena_reset(arena);

  /*
   * Reset must retain the region chain.
   */
  SHI_TEST_CHECK(arena->begin == begin);
  SHI_TEST_CHECK(arena->end == end);

  size_t region_count = 0;

  for (__arena_region__ *r = arena->begin; r != NULL; r = r->next) {
    SHI_TEST_CHECK(r->offset == 0);
    ++region_count;
  }

  SHI_TEST_CHECK(region_count >= 2);

  /*
   * Reallocate using the retained regions.
   */
  for (size_t i = 0; i < 16; ++i) {
    SHI_TEST_CHECK(arena_alloc(arena, sizeof(uintptr_t)) != NULL);
  }

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* push_new_arena_region                                           */
/* --------------------------------------------------------------- */

static int test_push_region(void) {
  Arena *arena = init_arena(8);

  SHI_TEST_CHECK(arena != NULL);

  SHI_TEST_CHECK(push_new_arena_region(arena, 4) != 0);

  SHI_TEST_CHECK(arena->begin != NULL);
  SHI_TEST_CHECK(arena->current == arena->begin);
  SHI_TEST_CHECK(arena->end == arena->begin);
  SHI_TEST_CHECK(arena->begin->cap >= 4);
  SHI_TEST_CHECK(arena->begin->offset == 0);
  SHI_TEST_CHECK(arena->begin->next == NULL);

  /*
   * A larger min_cap must produce a sufficiently large region.
   */
  SHI_TEST_CHECK(push_new_arena_region(arena, 128) != 0);

  SHI_TEST_CHECK(arena->end != arena->begin);
  SHI_TEST_CHECK(arena->end->cap >= 128);
  SHI_TEST_CHECK(arena->current == arena->end);

  /* NULL arena must fail safely. */
  SHI_TEST_CHECK(push_new_arena_region(NULL, 1) == 0);

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Object allocation                                               */
/* --------------------------------------------------------------- */

static int test_objects(void) {
  Arena *arena = init_arena(64);

  SHI_TEST_CHECK(arena != NULL);

  const char *text = "This is some dummy text";
  Obj *objects[64];
  size_t count = 0;

  for (size_t i = 0; text[i] != '\0'; ++i) {
    if (text[i] == ' ') {
      continue;
    }

    Obj *obj = arena_alloc(arena, sizeof(*obj));

    SHI_TEST_CHECK(obj != NULL);

    obj->value = text[i];
    obj->col = i;

    objects[count++] = obj;
  }

  SHI_TEST_CHECK(count > 0);
  SHI_TEST_CHECK(count < 64);

  for (size_t i = 0; i < count; ++i) {
    SHI_TEST_CHECK(objects[i] != NULL);
    SHI_TEST_CHECK(objects[i]->value != '\0');
  }

  free_arena(arena);

  return 0;
}

/* --------------------------------------------------------------- */
/* Main                                                            */
/* --------------------------------------------------------------- */
int main(void) {
  int (*tests[])(void) = {
      test_init,           test_zero_operations, test_basic_allocation, test_alignment,
      test_exact_capacity, test_region_growth,   test_large_allocation, test_memdup,
      test_reset,          test_region_reuse,    test_push_region,      test_objects,
  };

  const size_t test_count = sizeof(tests) / sizeof(tests[0]);

  for (size_t i = 0; i < test_count; ++i) {
    if (tests[i]() != 0) {
      fprintf(stderr, "shi_arena.h: test %zu/%zu failed\n", i + 1, test_count);
      return 1;
    }
  }

  printf("shi_arena.h: all %zu tests passed\n", test_count);

  return 0;
}

#endif // SHI_TEST

// ===============
// IMPLEMENTATION
// ===============
#ifdef SHI_ARENA_IMPLEMENTATION

#ifndef ARENA_REGION_DEFAULT_CAPACITY
#define ARENA_REGION_DEFAULT_CAPACITY (1024)
#endif

static int bytes_to_words(size_t bytes, size_t *out_words) {
  if (bytes > SIZE_MAX - (sizeof(uintptr_t) - 1)) {
    return 0;
  }
  *out_words = (bytes + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
  return 1;
}

__arena_region__ *init_arena_region(size_t capacity) {
  if (capacity > (SIZE_MAX - sizeof(__arena_region__)) / sizeof(uintptr_t)) {
    return NULL;
  }

  size_t size_bytes = sizeof(__arena_region__) + sizeof(uintptr_t) * capacity;
  __arena_region__ *region = (__arena_region__ *)malloc(size_bytes);
  if (!region) {
    return NULL;
  }

  region->next = NULL;
  region->offset = 0;
  region->cap = capacity;

  return region;
}

__arena__ *init_arena(size_t region_size_bytes) {
  __arena__ *arena = (__arena__ *)malloc(sizeof(__arena__));
  if (!arena) {
    return NULL;
  }

  size_t capacity_words = 0;
  if (region_size_bytes > 0) {
    if (!bytes_to_words(region_size_bytes, &capacity_words)) {
      free(arena);
      return NULL;
    }
  } else {
    capacity_words = ARENA_REGION_DEFAULT_CAPACITY;
  }

  *arena = (__arena__){
      .region_size = capacity_words,
      .begin = NULL,
      .current = NULL,
      .end = NULL,
  };

  return arena;
}

int push_new_arena_region(__arena__ *arena, size_t min_cap) {
  if (!arena) {
    return 0;
  }

  size_t cap = arena->region_size;
  if (cap < min_cap) {
    cap = min_cap;
  }

  __arena_region__ *region = init_arena_region(cap);
  if (!region) {
    return 0;
  }

  if (!arena->end) {
    arena->begin = region;
    arena->current = region;
    arena->end = region;
  } else {
    arena->end->next = region;
    arena->end = region;
    arena->current = region;
  }

  return 1;
}

void *arena_alloc(__arena__ *arena, size_t size_bytes) {
  if (!arena || size_bytes == 0) {
    return NULL;
  }

  size_t size_words = 0;
  if (!bytes_to_words(size_bytes, &size_words)) {
    return NULL;
  }

  if (!arena->current) {
    if (!push_new_arena_region(arena, size_words)) {
      return NULL;
    }
  }

  while (arena->current->offset > arena->current->cap || size_words > arena->current->cap - arena->current->offset) {

    if (arena->current->offset > arena->current->cap) {
      return NULL;
    }

    if (arena->current->next == NULL) {
      if (!push_new_arena_region(arena, size_words)) {
        return NULL;
      }
      break;
    }

    arena->current = arena->current->next;
  }

  if (arena->current->cap - arena->current->offset < size_words) {
    if (!push_new_arena_region(arena, size_words)) {
      return NULL;
    }
  }

  void *dest = &arena->current->data[arena->current->offset];
  arena->current->offset += size_words;
  return dest;
}

void *arena_memdup(__arena__ *arena, const void *data, size_t size) {
  if (!arena || !data || size == 0) {
    return NULL;
  }

  void *dest = arena_alloc(arena, size);
  if (!dest) {
    return NULL;
  }

  memcpy(dest, data, size);
  return dest;
}

void arena_reset(__arena__ *arena) {
  if (!arena) {
    return;
  }

  for (__arena_region__ *region = arena->begin; region != NULL; region = region->next) {
    region->offset = 0;
  }

  arena->current = arena->begin;
}

void free_arena(__arena__ *arena) {
  if (!arena) {
    return;
  }

  __arena_region__ *region = arena->begin;
  while (region) {
    __arena_region__ *next = region->next;
    free(region);
    region = next;
  }

  free(arena);
}

#endif // SHI_ARENA_IMPLEMENTATION

/*
 *
 * TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION:
 * License : https://raw.githubusercontent.com/parixitsapkota/SHI/refs/heads/main/LICENSE
 *
 */

/*
 * Segregated fits malloc implementation.
 * First fit placement with immediate coalescing.
 * Minimum block size is 16 bytes.
 */
#include "mm.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

typedef uint32_t INTERNAL_SIZE_T;
#define INTERNAL_SIZE_T_SIZE (sizeof(INTERNAL_SIZE_T))
#define HEADER_SIZE INTERNAL_SIZE_T_SIZE
#define FOOTER_SIZE INTERNAL_SIZE_T_SIZE

/* Read and write a INTERNAL_SIZE_T value at address p */
#define GET(p) (*(INTERNAL_SIZE_T *)(p))
#define PUT(p, val) (*(INTERNAL_SIZE_T *)(p) = (val))

#define SIZE_PTR(p) ((INTERNAL_SIZE_T *)(((char *)(p)) - HEADER_SIZE))

static char *heap_head = NULL;
struct free_list {
  void *head;
  void *tail;
};

#define PROLOGUE_SIZE (32 * sizeof(struct free_list))

typedef uint32_t INTERNAL_PTR_T;
#define POINTER_SIZE (sizeof(INTERNAL_PTR_T))

static inline INTERNAL_PTR_T to_internal_ptr(void *p) {  //
  return p == NULL ? 0 : (char *)p - heap_head;
}
static inline void *to_void_ptr(INTERNAL_PTR_T p) {  //
  return p == 0 ? NULL : heap_head + p;
}
/* Read a INTERNAL_PTR at address p */
static inline INTERNAL_PTR_T GET_PTR(void *p) {  //
  return *(INTERNAL_PTR_T *)p;
}
/* Write a INTERNAL_PTR at address p */
static inline void PUT_PTR(void *p, INTERNAL_PTR_T val) {  //
  *(INTERNAL_PTR_T *)p = val;
}

/* Pack a size, allocated bit, and allocated bit of previous block into a word */
#define PACK(size, alloc, prev_alloc) ((size) | (alloc) | (prev_alloc) << 1)

#define MIN_BLOCK_SIZE (HEADER_SIZE + 2 * POINTER_SIZE + FOOTER_SIZE)

/* Read and write the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define SET_SIZE(p, size) (PUT(p, (GET(p) & 0x7) | (size)))
#define GET_ALLOC(p) (GET(p) & 0x1)
#define SET_ALLOC(p, val) (PUT(p, (GET(p) & ~0x1) | (val)))
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)
#define SET_PREV_ALLOC(p, val) (PUT(p, (GET(p) & ~0x2) | (val) << 1))

#define GET_PAYLOAD(p) ((char *)(p) + HEADER_SIZE)

#define GET_NEXT_BLOCK(p) ((char *)(p) + GET_SIZE(p))

#define PREV_FOOTER_PTR(p) ((char *)(p)-FOOTER_SIZE)
#define GET_PREV_FOOTER(p) (GET(PREV_FOOTER_PTR(p)))
#define GET_PREV_BLOCK(p, prev_footer) ((char *)(p) - ((prev_footer) & ~0x7))

#define FOOTER_PTR(p) ((char *)(p) + GET_SIZE(p) - FOOTER_SIZE)
#define SET_FOOTER(p) (PUT(FOOTER_PTR(p), GET(p)))

/* Forward block and backward block in the list of free blocks */
static inline void *FWD_BLOCK_PTR(void *p) {  //
  return (char *)p + HEADER_SIZE;
}
static inline void *GET_FWD_BLOCK(void *p) {  //
  return to_void_ptr(GET_PTR(FWD_BLOCK_PTR(p)));
}
static inline void SET_FWD_BLOCK(void *p, void *fwd) {  //
  PUT_PTR(FWD_BLOCK_PTR(p), to_internal_ptr(fwd));
}
static inline void *BCK_BLOCK_PTR(void *p) {  //
  return (char *)p + HEADER_SIZE + POINTER_SIZE;
}
static inline void *GET_BCK_BLOCK(void *p) {  //
  return to_void_ptr(GET_PTR(BCK_BLOCK_PTR(p)));
}
static inline void SET_BCK_BLOCK(void *p, void *bck) {  //
  PUT_PTR(BCK_BLOCK_PTR(p), to_internal_ptr(bck));
}

static inline uint32_t __log2(const uint32_t x) {
  uint32_t y;
  asm("bsr %1, %0" : "=r"(y) : "r"(x));
  return y;
}
static inline struct free_list *get_list(uint32_t idx) {  //
  return ((struct free_list *)heap_head) + idx;
}
static inline struct free_list *get_list_of(void *p) {  //
  return get_list(__log2(GET_SIZE(p)));
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {
  int padding = ALIGN(HEADER_SIZE) - HEADER_SIZE;
  heap_head = mem_sbrk(PROLOGUE_SIZE + padding + HEADER_SIZE);
  struct free_list *l = (struct free_list *)heap_head;
  for (int i = 0; i < 32; i++) {
    l->head = NULL;
    l->tail = NULL;
    l++;
  }
  PUT(heap_head + PROLOGUE_SIZE + padding, PACK(0, 1, 1));
  return 0;
}

static inline void remove_from_free_list(void *p) {
  struct free_list *l = NULL;
  void *backword = GET_BCK_BLOCK(p);
  void *forward = GET_FWD_BLOCK(p);
  if (backword != NULL) {
    SET_FWD_BLOCK(backword, forward);
  } else {
    l = get_list_of(p);
    l->head = forward;
  }
  if (forward != NULL) {
    SET_BCK_BLOCK(forward, backword);
  } else {
    if (l == NULL) {
      l = get_list_of(p);
    }
    l->tail = backword;
  }
}

static inline INTERNAL_SIZE_T max(INTERNAL_SIZE_T a, INTERNAL_SIZE_T b) { return a > b ? a : b; }
static inline void check_tail_block(void *p) {
  assert(GET_SIZE(p) == 0);
  assert(GET_ALLOC(p) == 1);
}
/* Insert a block to the head of the free list */
static inline void insert_to_free_list(void *p) {
  struct free_list *l = get_list_of(p);
  if (l->head != NULL) {
    SET_BCK_BLOCK(l->head, p);
  }
  SET_FWD_BLOCK(p, l->head);
  SET_BCK_BLOCK(p, NULL);
  l->head = p;
}
/*
 * malloc - Allocate a block.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size) {
  // printf("malloc %d\n", size);
  if (size == 0) return NULL;
  INTERNAL_SIZE_T need_size = max(ALIGN(HEADER_SIZE + size), MIN_BLOCK_SIZE);
  void *p;
  for (int i = __log2(need_size); i < 32; i++) {
    p = get_list(i)->head;
    while (p != NULL) {
      if (GET_SIZE(p) >= need_size) {
        goto outer;
      }
      p = GET_FWD_BLOCK(p);
    }
  }
outer:
  if (p != NULL) {
    remove_from_free_list(p);
    size_t remain = GET_SIZE(p) - need_size;
    if (remain >= MIN_BLOCK_SIZE) {
      // split block
      SET_SIZE(p, need_size);
      void *new_block = GET_NEXT_BLOCK(p);
      SET_SIZE(new_block, remain);
      SET_ALLOC(new_block, 0);
      SET_PREV_ALLOC(new_block, 1);
      insert_to_free_list(new_block);
      SET_FOOTER(new_block);
    } else {
      SET_PREV_ALLOC(GET_NEXT_BLOCK(p), 1);
    }

    SET_ALLOC(p, 1);
    return GET_PAYLOAD(p);
  } else {
    // printf("size: %d, need_size %d\n", size, newsize);
    // fflush(stdout);
    char *p = mem_sbrk(need_size);
    if ((long)p < 0)
      return NULL;
    else {
      p -= HEADER_SIZE;
      // check_tail_block(p);
      SET_SIZE(p, need_size);
      SET_ALLOC(p, 1);
      PUT(p + need_size, PACK(0, 1, 1));
      return GET_PAYLOAD(p);
    }
  }
}

/*
 * free
 */
void free(void *ptr) {
  // printf("free %p\n", ptr);
  if (ptr == NULL) return;
  INTERNAL_SIZE_T *p = SIZE_PTR(ptr);
  // if (GET_ALLOC(p) == 0) return;
  char *next_block = GET_NEXT_BLOCK(p);
  INTERNAL_SIZE_T next_block_alloc = GET_ALLOC(next_block);
  INTERNAL_SIZE_T prev_block_alloc = GET_PREV_ALLOC(p);
  if (prev_block_alloc) {
    if (next_block_alloc) {
      // both prev and next block are allocated, no coalescing
      // mark the block as free, update footer, and insert to the free list
      SET_ALLOC(p, 0);
      SET_PREV_ALLOC(next_block, 0);
      SET_FOOTER(p);
      insert_to_free_list(p);
    } else {
      // prev block is allocated, next block is free
      // coalesce with next block
      SET_ALLOC(p, 0);
      remove_from_free_list(next_block);
      SET_SIZE(p, GET_SIZE(p) + GET_SIZE(next_block));
      SET_FOOTER(p);
      insert_to_free_list(p);
    }
  } else {
    // prev block is free, get prev block
    size_t prev_footer = GET_PREV_FOOTER(p);
    void *prev_block = GET_PREV_BLOCK(p, prev_footer);
    if (next_block_alloc) {
      // prev block is free, next block is allocated
      // coalesce with prev block
      remove_from_free_list(prev_block);
      SET_PREV_ALLOC(next_block, 0);
      SET_SIZE(prev_block, GET_SIZE(prev_block) + GET_SIZE(p));
      SET_FOOTER(prev_block);
      insert_to_free_list(prev_block);
    } else {
      // both prev and next block are free,
      // remove next block from free list,
      // merge (p + next_block) to prev block
      remove_from_free_list(prev_block);
      remove_from_free_list(next_block);
      SET_SIZE(prev_block, GET_SIZE(prev_block) + GET_SIZE(p) + GET_SIZE(next_block));
      SET_FOOTER(prev_block);
      insert_to_free_list(prev_block);
    }
  }
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size) {
  size_t oldsize;
  void *newptr;

  /* If size == 0 then this is just free, and we return NULL. */
  if (size == 0) {
    free(oldptr);
    return 0;
  }

  /* If oldptr is NULL, then this is just malloc. */
  if (oldptr == NULL) {
    return malloc(size);
  }

  newptr = malloc(size);

  /* If realloc() fails the original block is left untouched  */
  if (!newptr) {
    return 0;
  }

  /* Copy the old data. */
  oldsize = *SIZE_PTR(oldptr);
  if (size < oldsize) oldsize = size;
  memcpy(newptr, oldptr, oldsize);

  /* Free the old block. */
  free(oldptr);

  return newptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc(size_t nmemb, size_t size) {
  size_t bytes = nmemb * size;
  void *newptr;

  newptr = malloc(bytes);
  memset(newptr, 0, bytes);

  return newptr;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
  void *mem_brk = mem_sbrk(0);
  if (verbose > 1) {
    printf("mm_checkheap - mem_brk: %p\n", mem_brk);
  }

  for (int i = 0; i < 32; i++) {
    struct free_list *l = get_list(i);
    if (verbose > 2) {
      printf("free list head: %p\n", l->head);
      printf("free list tail: %p\n", l->tail);
    }
    // iterate through the free list
    char *p = l->head;
    while (p != NULL) {
      if (verbose > 2) {
        printf("free block: %p, size: %u\n", p, GET_SIZE(p));
      }
      if (GET_ALLOC(p) != 0) {
        fprintf(stderr, "free block %p is not free\n", p);
      }
      if (GET_FWD_BLOCK(p) != NULL && GET_BCK_BLOCK(GET_FWD_BLOCK(p)) != p) {
        fprintf(stderr, "free block %p forward pointer is not consistent\n", p);
      }
      if (GET_BCK_BLOCK(p) != NULL && GET_FWD_BLOCK(GET_BCK_BLOCK(p)) != p) {
        fprintf(stderr, "free block %p backward pointer is not consistent\n", p);
      }
      p = GET_FWD_BLOCK(p);
    }
  }

  // check all blocks by address order
  char *p = heap_head + PROLOGUE_SIZE + ALIGN(HEADER_SIZE) - HEADER_SIZE;
  size_t p_size;
  size_t p_alloc;
  size_t prev_alloc = 1, p_prev_alloc;
  size_t p_footer;
  while (1) {
    p_size = GET_SIZE(p);
    p_alloc = GET_ALLOC(p);
    p_prev_alloc = GET_PREV_ALLOC(p);
    if (verbose > 2 || p > (char *)mem_brk - 0x80) {
      printf("block: %p, size: %lu, alloc: %lu, prev_alloc: %lu\n", p, p_size, p_alloc, p_prev_alloc);
    }
    if (prev_alloc != p_prev_alloc) {
      fprintf(stderr, "prev_alloc: %lu, p_prev_alloc: %lu\n", prev_alloc, p_prev_alloc);
      // exit(1);
    }
    if (p_alloc == 0) {
      p_footer = GET(FOOTER_PTR(p));
      if (p_footer != GET(p)) {
        fprintf(stderr, "footer not consistent, block: %p, size: %lu, alloc: %lu, prev_alloc: %lu, footer: %lu\n", p,
                p_size, p_alloc, p_prev_alloc, p_footer);
        exit(1);
      }
    }
    if (p_size == 0) break;
    p = GET_NEXT_BLOCK(p);
    prev_alloc = p_alloc;
  }
}

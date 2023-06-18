#include "buddy.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PAGE_NUM (1 << ((MAX_RANK)-1))

struct tree_node {
  uint8_t *p;
  int is_allocated;
  struct tree_node *next;
  struct tree_node *prev;
} node[MAX_PAGE_NUM * 2];

struct node_list {
  struct tree_node *head;
  size_t size;
};
struct node_list free_list[MAX_RANK];
// When a range of pages is allocated, only the first page is recorded
// in the rank_of_page array, and the other pages are marked as -1.
// This is used in the query_ranks() function, and also helps to check
// whether a page address is valid (i.e. whether it is previously
// returned by alloc_pages() and not yet freed by return_pages()).
int rank_of_page[MAX_PAGE_NUM];

uint8_t *mem_start;
int pg_num, max_rank, max_page_num;

int init_page(void *p, int pgcount) {
  mem_start = p;
  pg_num = pgcount;
  // printf("mem_start %p, pg_num %d\n", mem_start, pg_num);
  max_rank = 0;
  while (1 << max_rank <= pg_num && max_rank < MAX_RANK) {
    max_rank++;
  }
  max_page_num = 1 << (max_rank - 1);  // size = 2^(rank-1)
  for (int i = 0; i < max_rank; i++) {
    free_list[i].head = NULL;
    free_list[i].size = 0;
    int start = 1 << (max_rank - 1 - i), end = start << 1;
    int addr_incr = 1 << (i + 12);  // page size = 4KB
    uint8_t *p = mem_start;
    for (int j = start; j < end; j++) {
      // printf("rank %d, start %d, end %d, j %d\n", i, start, end, j);
      node[j].p = p;
      node[j].is_allocated = 0;
      node[j].next = NULL;
      node[j].prev = NULL;
      p += addr_incr;
    }
  }
  // assert(node[1].p == mem_start);
  // assert(node[1].is_allocated == 0);
  // assert(node[1].next == NULL);
  // assert(node[1].prev == NULL);
  free_list[max_rank - 1].head = &node[1];
  free_list[max_rank - 1].size = 1;
  return OK;
}

void *alloc_pages(int rank) {
  int i = rank;
  struct tree_node *n = free_list[i - 1].head;
  while (n == NULL) {
    i++;
    // printf("alloc pages, i %d, rank %d\n", i, rank);
    if (i > max_rank) {
      return (void *)-ENOSPC;
    }
    n = free_list[i - 1].head;
  }

  // remove from free list
  // assert(n->prev == NULL);
  if (n->next != NULL) n->next->prev = NULL;
  free_list[i - 1].head = n->next;
  free_list[i - 1].size--;
  n->next = NULL;

  // i > rank, split a larger block
  while (i > rank) {
    n->is_allocated = 1;
    i--;
    // in this case, free_list[i - 1] must be empty
    // assert(free_list[i - 1].head == NULL);
    int node_idx = n - node;
    struct tree_node *rc = &node[(node_idx << 1) | 1];  // right child
    // assert(rc->is_allocated == 0);
    // assert(rc->next == NULL);
    // assert(rc->prev == NULL);
    free_list[i - 1].head = rc;
    free_list[i - 1].size = 1;
    n = &node[node_idx << 1];  // left child
  }
  // printf("node idx %ld\n", n - node);
  n->is_allocated = 1;
  rank_of_page[((uint8_t *)n->p - mem_start) >> 12] = rank;
  // printf("alloc_pages: rank %d, p %p\n", rank, n->p);
  return n->p;
}

int return_pages(void *p) {
  int page_idx = ((uint8_t *)p - mem_start) >> 12;
  if (page_idx < 0 || page_idx >= max_page_num) {
    return -EINVAL;
  }
  int rank = rank_of_page[page_idx];
  if (rank == -1) {
    return -EINVAL;
  }
  rank_of_page[page_idx] = -1;
  int node_idx = (max_page_num + page_idx) >> (rank - 1);
  // recursively merge with buddy
  while (rank < max_rank) {
    node[node_idx].is_allocated = 0;
    int buddy_idx = node_idx ^ 1;
    struct tree_node *buddy = &node[buddy_idx];
    // check if buddy is free
    if (buddy->is_allocated != 0) break;
    // remove buddy from free list
    if (buddy->prev != NULL) {
      buddy->prev->next = buddy->next;
    } else {
      free_list[rank - 1].head = buddy->next;
    }
    if (buddy->next != NULL) {
      buddy->next->prev = buddy->prev;
    }
    free_list[rank - 1].size--;
    // jump to parent
    node_idx = node_idx >> 1;
    rank++;
  }
  // add to free list
  struct tree_node **head = &free_list[rank - 1].head;
  node[node_idx].next = *head;
  if (*head != NULL) (*head)->prev = &node[node_idx];
  (*head) = &node[node_idx];
  free_list[rank - 1].size++;
  return OK;
}

static inline int lowbit(int x) { return x & (-x); }

int query_ranks(void *p) {
  int page_idx = ((uint8_t *)p - mem_start) >> 12;
  if (page_idx < 0 || page_idx >= max_page_num) {
    return -EINVAL;
  }
  int rank = rank_of_page[page_idx];
  if (rank == -1) {
    // unallocated page, find the largest rank
    rank = 1;
    int low_bit = lowbit((1 << (max_rank - 1)) | page_idx);
    // printf("page_idx %d, lowbit %d\n", page_idx, low_bit);
    for (int i = page_idx + 1; i < page_idx + low_bit; i += lowbit(i)) {
      // printf("i %d, rank_of_page[i] %d\n", i, rank_of_page[i]);
      if (rank_of_page[i] != -1) break;
      rank++;
    }
  }
  // printf("query_ranks: p %p, rank %d\n", p, rank);
  return rank;
}

int query_page_counts(int rank) {
  // printf("query_page_counts: rank %d, size %d\n", rank, free_list[rank - 1].size);
  return free_list[rank - 1].size;
}

/* YOUR CODE HERE */
#include "coroutine.h"

#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pthread.h"

struct node {
  struct node* nxt;
  struct node* pre;
  struct coroutine_t* c;
};

struct coroutine_list {
  struct node* head;
};

void add_node(struct coroutine_list* l, struct node* n) {
  n->pre = NULL;
  n->nxt = l->head;
  if (l->head != NULL) {
    l->head->pre = n;
  }
  l->head = n;
}

void add(struct coroutine_list* l, struct coroutine_t* c) {
  struct node* tmp = malloc(sizeof(struct node));
  tmp->c = c;
  add_node(l, tmp);
}

void add_to_array(struct coroutine_t** arr, size_t* size, struct coroutine_t* c) {
  arr[*size] = c;
  ++*size;
}

// Add all elements of l2 to l1.
void add_all(struct coroutine_list* l1, struct coroutine_list* l2) {
  struct node* n = l2->head;
  while (n != NULL) {
    add_node(l1, n);
    n = n->nxt;
  }
}

// Add all elements of l2 to l1.
void add_all_to_array(struct coroutine_t** l1, size_t* size, struct coroutine_list* l2) {
  struct node* n = l2->head;
  while (n != NULL) {
    add_to_array(l1, size, n->c);
    n = n->nxt;
  }
}

struct coroutine_t* pop(struct coroutine_list* l) {
  if (l->head == NULL) return NULL;
  struct node* head = l->head;
  l->head = head->nxt;
  struct coroutine_t* c = head->c;
  free(head);
  l->head->pre = NULL;
  return c;
}

// Remove this node from linked list, but do NOT free this node
void remove_without_free(struct coroutine_list* l, struct node* n) {
  if (n->pre != NULL) n->pre->nxt = n->nxt;
  if (n->nxt != NULL) n->nxt->pre = n->pre;
  if (l->head == n) l->head = n->nxt;
}

void remove_from_list(struct coroutine_list* l, struct node* n) {
  remove_without_free(l, n);
  free(n);
}

void remove_from_array(struct coroutine_t** arr, size_t* size, struct coroutine_t* c) {
  int i = 0;
  while (arr[i] != c && i < *size) {
    i++;
  }
  while (i + 1 < *size) {
    arr[i] = arr[i + 1];
    i++;
  }
  *size = *size - 1;
}

// Remove all elements of l.
void remove_all(struct coroutine_list* l) {
  struct node *n = l->head, *tmp;
  while (n != NULL) {
    tmp = n;
    n = n->nxt;
    remove_from_list(l, tmp);
  }
}

#define STACK_SIZE 65536
struct coroutine_t {
  int cid;
  int (*func)(void);
  int status;
  int retval;
  jmp_buf env;
  uint8_t stack[STACK_SIZE];
  struct coroutine_list waiting_cors;
  struct coroutine_t* parent;
};

// Use a thread local variable to store coroutine manager for each thread.
__thread struct co_maganer_t {
  int is_initialized;
  struct coroutine_t __cors[MAXN + 10];
  struct coroutine_t* cors;
  struct coroutine_t* main_co;
  struct coroutine_t* cur_coroutine;
  struct coroutine_t* avail_cors[MAXN + 10];
  size_t avail_cor_num;
  struct node cor_nodes[MAXN + 10];
  int cor_num, unfinished_cor_num;
} co_manager;

void init_co_manager() {
  co_manager.__cors[0] = (struct coroutine_t){.cid = -1, .func = NULL, .status = RUNNING};
  co_manager.cur_coroutine = co_manager.main_co = co_manager.__cors;
  co_manager.cors = co_manager.__cors + 1;
  co_manager.cor_num = 0;
  co_manager.unfinished_cor_num = 0;
  co_manager.avail_cors[0] = co_manager.main_co;
  co_manager.avail_cor_num = 1;
  srand(time(NULL));
}

void select_and_switch();

void continue_coroutine(struct coroutine_t* c) { longjmp(c->env, 1); }

void coroutine_finish(int retval) {
#ifdef DEBUG
  fprintf(stderr, "co #%d finished with retval %d\n", co_manager.cur_coroutine->cid, retval);
#endif
  co_manager.cur_coroutine->retval = retval;
  co_manager.cur_coroutine->status = FINISHED;
  co_manager.unfinished_cor_num--;
  add_all_to_array(co_manager.avail_cors, &co_manager.avail_cor_num, &co_manager.cur_coroutine->waiting_cors);
  remove_from_array(co_manager.avail_cors, &co_manager.avail_cor_num, co_manager.cur_coroutine);
#ifdef DEBUG
  fprintf(stderr, "co #%d parent: co #%d\n", co_manager.cur_coroutine->cid, co_manager.cur_coroutine->parent->cid);
#endif
  if (co_manager.cur_coroutine->parent->status != FINISHED) {
    continue_coroutine(co_manager.cur_coroutine->parent);
  } else {
    select_and_switch();
  }
}

void run_coroutine(struct coroutine_t* c) {
  struct coroutine_t* old_co = co_manager.cur_coroutine;
#ifdef DEBUG
  fprintf(stderr, "run co #%d, old co #%d\n", c->cid, old_co->cid);
#endif
  c->status = RUNNING;
  co_manager.cur_coroutine = c;

  if (setjmp(old_co->env) == 0) {
    asm(
#if __x86_64__
        "movq %0, %%rsp; call *%1; movl %%eax, %%edi; call *%2" ::"c"((uintptr_t)c->stack + STACK_SIZE),
        "d"((uintptr_t)c->func), "b"((uintptr_t)coroutine_finish)
#else
        "movl %0, %%esp; call *%1; movl %%eax, %%edi; call *%2" ::"c"((uintptr_t)c->stack + STACK_SIZE),
        "d"((uintptr_t)c->func), "a"((uintptr_t)coroutine_finish)
#endif
    );
//     asm(
// #if __x86_64__
//         "movq %0, %%rsp" ::"c"((uintptr_t)c->stack + STACK_SIZE)
// #else
// #endif
//     );
//     coroutine_finish(c->func());
  }
  co_manager.cur_coroutine = old_co;
}

int co_start_nonblock(int (*routine)(void)) {
  struct coroutine_t* c = &co_manager.cors[co_manager.cor_num];
  c->cid = co_manager.cor_num;
  c->func = routine;
  c->status = NEW;  // TODO
  c->waiting_cors.head = NULL;
  c->parent = co_manager.cur_coroutine;
  add_to_array(co_manager.avail_cors, &co_manager.avail_cor_num, c);
  co_manager.cor_num++;
  co_manager.unfinished_cor_num++;
  return c->cid;
}

int co_start(int (*routine)(void)) {
  if (co_manager.is_initialized == 0) {
    init_co_manager();
    co_manager.is_initialized = 1;
  }
  cid_t cid = co_start_nonblock(routine);
  run_coroutine(&co_manager.cors[cid]);
  return cid;
}

int co_getid() { return co_manager.cur_coroutine->cid; }

int is_parent_of(struct coroutine_t* c) {
  if (co_manager.cur_coroutine == c->parent) return 1;
  if (c->parent != NULL) return is_parent_of(c->parent);
  return 0;
}

int co_getret(int cid) {
  int retval;
  retval = co_manager.cors[cid].retval;
  return retval;
}

int co_status(int cid) {
  int status;
  if (!is_parent_of(&co_manager.cors[cid]))
    status = UNAUTHORIZED;
  else
    status = co_manager.cors[cid].status;
  return status;
}

void select_and_switch() {
#ifdef DEBUG
  fprintf(stderr, "enter select_and_switch\n");
#endif
  struct coroutine_t* c = co_manager.avail_cors[rand() % co_manager.avail_cor_num];
#ifdef DEBUG
  fprintf(stderr, "select coroutine #%d to switch\n", c->cid);
#endif
  if (c->status == NEW)
    run_coroutine(c);
  else
    continue_coroutine(c);
}

int co_yield () {
  struct coroutine_t* c = co_manager.cur_coroutine;
  int x = setjmp(c->env);
  if (x == 0) {
    select_and_switch();
  } else {
    co_manager.cur_coroutine = c;
  }
  return 0;
}

int co_waitall() {
  while (co_manager.unfinished_cor_num) {
    co_yield();
  }
}

int co_wait(int cid) {
  struct coroutine_t *cur, *c;
  int need_wait = 0;
  cur = co_manager.cur_coroutine;
  c = &co_manager.cors[cid];
  if (c->status != FINISHED) {
    add(&c->waiting_cors, cur);
    remove_from_array(co_manager.avail_cors, &co_manager.avail_cor_num, cur);
    need_wait = 1;
  }
  if (!need_wait) {
    return 0;
  }
  int x = setjmp(cur->env);
  if (x == 0) {
    select_and_switch();
  } else {
    co_manager.cur_coroutine = cur;
  }
}
// ? Loc here: header modification to adapt pthread_setaffinity_np
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <utmpx.h>
#include <assert.h>

void *thread1(void* dummy){
    assert(sched_getcpu() == 0);
    return NULL;
}

void *thread2(void* dummy){
    assert(sched_getcpu() == 1);
    return NULL;
}
int main(){
    pthread_t pid[2];
    int i;
    // ? LoC: Bind core here
    cpu_set_t cpu[2];
    pthread_attr_t attr[2];
    for (i = 0; i < 2; ++i) {
        CPU_ZERO(&cpu[i]);
        CPU_SET(i, &cpu[i]);
        pthread_attr_init(&attr[i]);
        pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpu[i]);
    }

    for(i = 0; i < 2; ++i){
        // 1 Loc code here: create thread and save in pid[2]
        pthread_create(&pid[i], &attr[i], i == 0 ? thread1 : thread2, NULL);
    }
    for(i = 0; i < 2; ++i){
        pthread_join(pid[i], NULL);
    }
    return 0;
}

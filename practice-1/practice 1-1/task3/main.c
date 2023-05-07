// ? Loc here: header modification to adapt pthread_barrier
#define _POSIX_C_SOURCE 200112L /* Or higher */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
// 2 Locs here: declare mutex and barrier
pthread_mutex_t mutex;
pthread_barrier_t barrier;
void *thread1(void* dummy){
    int i;
    // 2 Locs: mutex operation and barrier operation
    // please consider the order of mutex operation and barrier operation
    pthread_barrier_wait(&barrier);
    pthread_mutex_lock(&mutex);
    printf("This is thread 1!\n");
    for(i = 0; i < 20; ++i){
        printf("H");
        printf("e");
        printf("l");
        printf("l");
        printf("o");
        printf("W");
        printf("o");
        printf("r");
        printf("l");
        printf("d");
        printf("!");
    }
    // 1 Loc: mutex operation
    pthread_mutex_unlock(&mutex);
    return NULL;
}

void *thread2(void* dummy){
    int i;
    // 2 Locs: mutex operation and barrier operation
    // please consider the order of mutex operation and barrier operation
    pthread_barrier_wait(&barrier);
    pthread_mutex_lock(&mutex);
    printf("This is thread 2!\n");
    for(i = 0; i < 20; ++i){
        printf("A");
        printf("p");
        printf("p");
        printf("l");
        printf("e");
        printf("?");
    }
    // 1 Loc: mutex operation
    pthread_mutex_unlock(&mutex);
    return NULL;
}
int main(){
    pthread_t pid[2];
    int i;
    // 2 Locs: barrier initialization and mutex initialization
    // 2 Locs here: create 2 thread using thread1 and thread2 as function.
    // 1 Loc: barrier operation
    pthread_barrier_init(&barrier, NULL, 3);
    pthread_mutex_init(&mutex, NULL);
    pthread_create(&pid[0], NULL, thread1, NULL);
    pthread_create(&pid[1], NULL, thread2, NULL);
    pthread_barrier_wait(&barrier);
    for(i = 0; i < 2; ++i){
        // 1 Loc code here: join thread
        pthread_join(pid[i], NULL);
    }
    pthread_mutex_destroy(&mutex);
    pthread_barrier_destroy(&barrier);
    return 0;
}

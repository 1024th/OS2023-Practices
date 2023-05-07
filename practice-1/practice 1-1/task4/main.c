// ? Loc here: header modification to adapt pthread_cond_t
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define MAXTHREAD 10
// declare cond_variable: you may define MAXTHREAD variables
pthread_mutex_t mutex;
pthread_cond_t cond;
int thread_to_exec;
// ? Loc in thread1: you can do any modification here, but it should be less than 20 Locs
void *thread1(void* dummy){
    int i;
    pthread_mutex_lock(&mutex);
    while (*(int*) dummy != thread_to_exec) {
        pthread_cond_wait(&cond, &mutex);
    }
    printf("This is thread %d!\n", *((int*) dummy));
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
    thread_to_exec = *(int*) dummy + 1;
    pthread_mutex_unlock(&mutex);
    pthread_cond_broadcast(&cond);
    return NULL;
}

int main(){
    pthread_t pid[MAXTHREAD];
    int i;
    // ? Locs: initialize the cond_variables
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    thread_to_exec = 0;
    for(i = 0; i < MAXTHREAD; ++i){
        int* thr = (int*) malloc(sizeof(int)); 
        *thr = i;
        // 1 Loc here: create thread and pass thr as parameter
        pthread_create(&pid[i], NULL, thread1, thr);
    }
    for(i = 0; i < MAXTHREAD; ++i)
        // 1 Loc here: join thread
        pthread_join(pid[i], NULL);
    return 0;
}

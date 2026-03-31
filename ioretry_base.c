#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

#define THREADS 500          
#define FILE_SIZE (16*1024*1024) /* 16MB */

void *worker(void *arg) {
    long id = (long)arg;
    char path[64];
    
    /* * Create files in the current working directory to ensure they reside on 
     * a real filesystem (e.g., ext4/xfs), guaranteeing filemap_fault triggers.
     */
    sprintf(path, "./test_file_%ld.dat", id);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    ftruncate(fd, FILE_SIZE);
    
    struct timeval start, end;
    
    while (1) {
        gettimeofday(&start, NULL);
        
        char *f_map = mmap(NULL, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (f_map != MAP_FAILED) {
            /* * Pure page cache thrashing: 500 threads * 16MB = 8GB of file pages 
             * squeezed into a restricted memcg (e.g., 1GB). This forces aggressive 
             * LRU reclaim, reproducing the scenario where newly fetched pages 
             * are evicted before the faulting thread can map them.
             */
            for (int i = 0; i < FILE_SIZE; i += 4096) {
                volatile char c = f_map[i]; 
            }
            munmap(f_map, FILE_SIZE);
        }
        
        gettimeofday(&end, NULL);
        long diff = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        
        if (id == 0) {
            printf("Current Batch Latency: %ld ms\n", diff);
        }
    }
    return NULL;
}

int main() {
    printf(" Pure File Thrashing Started. PID: %d\n", getpid());
    pthread_t t[THREADS];
    for (long i = 0; i < THREADS; i++) pthread_create(&t[i], NULL, worker, (void *)i);
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
    return 0;
}

#define _GNU_SOURCE 
#include <errno.h> 
#include <fcntl.h> 
#include <pthread.h> 
#include <stdatomic.h> 
#include <stdint.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/mman.h> 
#include <unistd.h> 
#include <time.h> 

#define THREADS 500 
#define FILE_SIZE (16 * 1024 * 1024) /* 16MB */ 

/* Stop flag: workers exit the loop after completing their current round */ 
static _Atomic int g_stop = 0; 

/* Default runtime in seconds */
#define RUN_SECONDS 600 

struct worker_arg { 
        long id; 
        uint64_t *counts; /* Array to store local counts per thread */ 
}; 

void *worker(void *arg) 
{ 
        struct worker_arg *wa = (struct worker_arg *)arg; 
        long id = wa->id; 
        char path[64]; 
        uint64_t local_rounds = 0; 

        /* * Separate file per thread to ensure it resides on a real filesystem 
         * (e.g., ext4/xfs) and guarantees filemap_fault triggers.
         */ 
        snprintf(path, sizeof(path), "./test_file_%d_%ld.dat", getpid(), id); 
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666); 
        if (fd < 0) { 
                fprintf(stderr, "open %s: %s\n", path, strerror(errno)); 
                return NULL; 
        } 
        if (ftruncate(fd, FILE_SIZE) < 0) { 
                perror("ftruncate"); 
                close(fd); 
                return NULL; 
        } 

        while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) { 
                char *f_map = mmap(NULL, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0); 

                if (f_map != MAP_FAILED) { 
                        /* * Page cache thrashing: Multiple threads * 16MB mapped 
                         * within a restricted memcg forces aggressive LRU reclaim.
                         */ 
                        for (int i = 0; i < FILE_SIZE; i += 4096) { 
                                volatile unsigned char c = (unsigned char)f_map[i]; 
                                (void)c; 
                        } 
                        munmap(f_map, FILE_SIZE); 
                        local_rounds++; 
                } 
        } 

        wa->counts[id] = local_rounds; 
        close(fd); 
        
        /* Clean up test files to avoid leaving garbage behind */ 
        unlink(path); 
        return NULL; 
} 

int main(void) 
{ 
        printf("Pure File Thrashing Started. PID: %d\n", getpid()); 
        printf("Counting per-thread rounds, no per-thread latency. RUN_SECONDS=%d\n", 
               RUN_SECONDS); 

        pthread_t t[THREADS]; 
        uint64_t local_counts[THREADS]; 
        memset(local_counts, 0, sizeof(local_counts)); 

        struct worker_arg args[THREADS]; 

        for (long i = 0; i < THREADS; i++) { 
                args[i].id = i; 
                args[i].counts = local_counts; 
                if (pthread_create(&t[i], NULL, worker, &args[i]) != 0) { 
                        perror("pthread_create worker"); 
                        return 1; 
                } 
        } 

        sleep(RUN_SECONDS); 
        atomic_store_explicit(&g_stop, 1, memory_order_relaxed); 

        for (int i = 0; i < THREADS; i++) { 
                pthread_join(t[i], NULL); 
        } 

        uint64_t total = 0; 
        for (int i = 0; i < THREADS; i++) 
                total += local_counts[i]; 

        printf("========================================\n"); 
        printf("Total rounds     : %llu\n", (unsigned long long)total); 
        printf("Throughput       : %.2f rounds/sec\n", (double)total / RUN_SECONDS); 
        printf("========================================\n"); 

        return 0; 
}

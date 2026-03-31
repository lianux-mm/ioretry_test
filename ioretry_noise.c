#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sched.h> /* Required for cpu_set_t */

#define IO_THREADS    500
#define NOISE_THREADS 500
#define FILE_SIZE     (16 * 1024 * 1024)

struct sched_attr {
    uint32_t size; uint32_t sched_policy; uint64_t sched_flags;
    int32_t sched_nice; uint32_t sched_priority;
    uint64_t sched_runtime; uint64_t sched_deadline; uint64_t sched_period;
};

/* Total number of online CPU cores */
int num_cores;

/* Utility function to pin a thread to a specific CPU core */
void pin_thread_to_core(long thread_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(thread_id % num_cores, &cpuset); /* Distribute evenly across cores */
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

void *io_worker(void *arg) {
    long id = (long)arg;
    pin_thread_to_core(id); /* Pin I/O thread to a specific core */
    
    char path[64];
    struct timeval start, end;
    sprintf(path, "./test_file_%ld.dat", id);
    
    while (1) {
        gettimeofday(&start, NULL);
        int fd = open(path, O_RDWR, 0666);
        if (fd < 0) { usleep(100); continue; }
        
        char *f_map = mmap(NULL, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (f_map != MAP_FAILED) {
            for (int i = 0; i < FILE_SIZE; i += 4096) {
                volatile char c = f_map[i];
            }
            munmap(f_map, FILE_SIZE);
        }
        close(fd);
        
        gettimeofday(&end, NULL);
        if (id == 0) {
            long diff = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
            printf("Batch Latency: %ld ms\n", diff);
        }
    }
    return NULL;
}

void *noise_worker(void *arg) {
    long id = (long)arg;
    /* * Pin the noise thread to the EXACT same core as its corresponding I/O thread
     * to enforce strict same-core scheduler preemption.
     */
    pin_thread_to_core(id); 
    
    struct sched_attr attr = {0};
    attr.size = sizeof(attr);
    attr.sched_policy = 0; 
    attr.sched_nice = -10; 
    attr.sched_runtime = 10000; /* 10us EEVDF preemption */
    
    if (syscall(314, 0, &attr, 0) != 0 && id == 0) perror("sched_setattr failed");
    
    struct timespec req = { .tv_sec = 0, .tv_nsec = 50000 };
    while (1) {
        for (volatile int i = 0; i < 5000; i++) __asm__ volatile("":::"memory");
        nanosleep(&req, NULL);
    }
    return NULL;
}

int main(int argc, char **argv) {
    num_cores = sysconf(_SC_NPROCESSORS_ONLN); /* Retrieve online CPUs count (e.g., 256) */
    
    int enable_noise = (argc <= 1 || strcmp(argv[1], "no_noise") != 0);
    printf("[*] Mode: %s. Cores: %d. PID: %d\n", enable_noise ? "Thrashing + EEVDF + Pinning" : "Baseline", num_cores, getpid());
    
    char path[64];
    for (long i = 0; i < IO_THREADS; i++) {
        sprintf(path, "./test_file_%ld.dat", i);
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) { ftruncate(fd, FILE_SIZE); close(fd); }
    }
    
    pthread_t t_io[IO_THREADS], t_noise[NOISE_THREADS];
    for (long i = 0; i < IO_THREADS; i++) pthread_create(&t_io[i], NULL, io_worker, (void *)i);
    
    if (enable_noise) {
        for (long i = 0; i < NOISE_THREADS; i++) pthread_create(&t_noise[i], NULL, noise_worker, (void *)i);
    }
    
    for (int i = 0; i < IO_THREADS; i++) pthread_join(t_io[i], NULL);
    return 0;
}

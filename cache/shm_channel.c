#include "shm_channel.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "steque.h"

// Shared memory queue and synchronization
steque_t shm_queue;
pthread_mutex_t shm_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t shm_queue_cond = PTHREAD_COND_INITIALIZER;

// Create a pool of shared memory segments
void create_shm_pool(int nsegments, int segsize) {
    for (int i = 0; i < nsegments; i++) {
        char name[128];
        snprintf(name, sizeof(name), "/shm_%d_%d", getpid(), i);
        // Remove any previous shm with same name
        shm_unlink(name);
        int fd = shm_open(name,  O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
        if (fd < 0) {
            perror("shm_open");
        }

        if (ftruncate(fd, sizeof(shm_data_t) + segsize) < 0) {
            perror("ftruncate");
        }

        shm_data_t *shm = mmap(NULL, sizeof(shm_data_t) + segsize,
                               PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (shm == MAP_FAILED) {
            perror("mmap");
        }

        /*Initialize shared memory structure */
        strncpy(shm->name, name, sizeof(shm->name)-1);
        shm->name[sizeof(shm->name)-1] = '\0';
        shm->segsize = segsize;
        shm->file_size = 0;
        shm->status = 0;

        /* Add to segment pool queue */
        pthread_mutex_lock(&shm_queue_mutex);
        steque_enqueue(&shm_queue, shm);
        pthread_mutex_unlock(&shm_queue_mutex);
    }
}


shm_data_t* get_shm_segment(void) {
    shm_data_t *shm;
    pthread_mutex_lock(&shm_queue_mutex);
    while (steque_isempty(&shm_queue)) {
        pthread_cond_wait(&shm_queue_cond, &shm_queue_mutex);
    }
    shm = steque_pop(&shm_queue);
    pthread_mutex_unlock(&shm_queue_mutex);

    // Reset semaphores for reuse
    shm->file_size = 0;
    shm->status = 0;

    return shm;
}

// Return a segment to the pool
void return_segment_to_pool(shm_data_t *shm) {
    shm->file_size = 0;
    shm->status = 0;
    shm->bytes_written = 0;  
    pthread_mutex_lock(&shm_queue_mutex);
    steque_enqueue(&shm_queue, shm);
    pthread_mutex_unlock(&shm_queue_mutex);
    pthread_cond_broadcast(&shm_queue_cond);
}

// Cleanup all shared memory segments
void cleanup_shm_pool(void) {
    pthread_mutex_lock(&shm_queue_mutex);
    while (!steque_isempty(&shm_queue)) {
        shm_data_t *shm = (shm_data_t *)steque_pop(&shm_queue);

        char name_copy[128];
        strncpy(name_copy, shm->name, sizeof(name_copy)-1);
        name_copy[sizeof(name_copy)-1] = '\0';
        // Destroy semaphores
        sem_destroy(&shm->rsem);
        sem_destroy(&shm->wsem);
        // Unmap and unlink
        munmap(shm, sizeof(shm_data_t) + shm->segsize);
        if (shm_unlink(name_copy) < 0) {
            perror("shm_unlink");
        }
    }
    pthread_mutex_unlock(&shm_queue_mutex);
    steque_destroy(&shm_queue);
    // printf("[Proxy] Cleaned up shared memory pool\n");
}
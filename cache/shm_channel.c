#include "shm_channel.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <steque.h>

steque_t shm_queue;
pthread_mutex_t shm_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t shm_queue_cond = PTHREAD_COND_INITIALIZER;

void create_shm_pool(int nsegments, int segsize) {
    for (int i = 0; i < nsegments; i++) {
        char name[128];
        snprintf(name, sizeof(name), "/shm_%d_%d", getpid(), i);

        int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (fd < 0) { perror("shm_open"); exit(1); }

        if (ftruncate(fd, sizeof(shm_data_t) + segsize) < 0) {
            perror("ftruncate"); exit(1);
        }

        shm_data_t *shm = mmap(NULL, sizeof(shm_data_t) + segsize,
                               PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (shm == MAP_FAILED) { perror("mmap"); exit(1); }

        strncpy(shm->name, name, sizeof(shm->name)-1);
        shm->segsize = segsize;
        shm->status = 0;
        shm->size = 0;
        shm->bytes_written = 0;
        shm->file_path[0] = '\0';

        sem_init(&shm->rsem, 1, 1);  // proxy starts allowed to read
        sem_init(&shm->wsem, 1, 0);  // cache starts blocked

        pthread_mutex_lock(&shm_queue_mutex);
        steque_enqueue(&shm_queue, shm);
        pthread_mutex_unlock(&shm_queue_mutex);
    }
    printf("[Proxy] Created %d shared memory segments\n", nsegments);
}
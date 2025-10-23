#include "shm_channel.h"

shm_data_t *create_shm_segment(size_t segsize) {
    char name[64];
    snprintf(name, sizeof(name), "/shm_%d", getpid());

    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("shm_open"); exit(1); }

    if (ftruncate(shm_fd, segsize) < 0) { perror("ftruncate"); exit(1); }

    shm_data_t *shm = mmap(NULL, segsize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); exit(1); }

    sem_init(&shm->cache_written, 1, 0);
    sem_init(&shm->proxy_read, 1, 1); // proxy can start reading

    shm->capacity = segsize - sizeof(shm_data_t);
    shm->size = 0;

    return shm;
}


void destroy_shm_segment(shm_data_t *shm, size_t segsize) {
    sem_destroy(&shm->cache_written);
    sem_destroy(&shm->proxy_read);
    munmap(shm, segsize);

    char name[64];
    snprintf(name, sizeof(name), "/shm_%d", getpid());
    shm_unlink(name);
}
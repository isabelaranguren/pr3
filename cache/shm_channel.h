// In case you want to implement the shared memory IPC as a library
// You may use this file. It is optional. It does help with code reuse
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>      
#include <sys/mman.h>   
#include <sys/stat.h>   
#include <semaphore.h>

#define MAX_CHUNK 8192

typedef struct {
    sem_t cache_written;   // Signals when cache wrote a chunk
    sem_t proxy_read;      // Signals when proxy read the chunk
    size_t capacity;  // max usable space
    size_t size;      // actual data in this chunk
    char data[MAX_CHUNK];         
} shm_data_t;

shm_data_t *create_shm_segment(size_t segsize);

void destroy_shm_segment(shm_data_t *shm, size_t segsize);

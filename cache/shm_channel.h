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
    pthread_mutex_t lock;
    int in_use;

    sem_t cache_written;   // Signals when cache wrote a chunk
    sem_t proxy_read;      // Signals when proxy read the chunk
    
    int status;
    size_t chunk_size;      // actual data in this chunk
    size_t yes;
    size_t bytes_read;

    size_t capacity;
    char data[MAX_CHUNK];         
} shm_data_t;

// this is for themessage queue command channel 
typedef struct {
    char filepath[256];
    char shm_name[64];
    int num_segments;
    pid_t proxy_pid;  // optional, for tracking
} cache_request_t;

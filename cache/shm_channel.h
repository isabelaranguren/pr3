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
    char name[100]; //segment name on shm_open
    char file_path[1024]; // request file path
    sem_t rsem;  // Signals when proxy read the chunk
    sem_t wsem;  // Signals when cache wrote a chunk
    int segsize; // segment size specified by user
    int status;   // 0=OK, 1=NOT_FOUND, 2=ERROR
    size_t size;  // Total file size     
    size_t bytes_written;  
    char data[];  // being tansferred  
} shm_data_t;

#define MAX_SHM_NAME 100

void create_shm_pool(int nsegments, int segsize);
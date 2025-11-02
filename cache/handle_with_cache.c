#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include "steque.h"
#include "gfserver.h"
#include "shm_channel.h"
#include "cache-student.h"

ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void *arg) {
    shm_data_t *shm = get_shm_segment();
    cache_req_t request;
    mqd_t mq;
        
    printf("[Proxy] Thread %ld requesting file: %s\n", pthread_self(), path);
    printf("[Proxy] Thread %ld acquired segment: %s\n", pthread_self(), shm->name);
    
    // Prepare request
    strncpy(request.path, path, sizeof(request.path)-1);
    request.path[sizeof(request.path)-1] = '\0';
    strncpy(request.shm_name, shm->name, sizeof(request.shm_name)-1);
    request.shm_name[sizeof(request.shm_name)-1] = '\0';
    request.segsize = shm->segsize;
    
    // Send request to cache
    mq = mq_open(CACHE_COMMAND_QUEUE, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("[Proxy] mq_open");
        return_segment_to_pool(shm);
        return SERVER_FAILURE;
    }
    if (mq_send(mq, (char*)&request, sizeof(request), 0) == -1) {
        perror("[Proxy] mq_send");
        mq_close(mq);
        return_segment_to_pool(shm);
        return SERVER_FAILURE;
    }
    mq_close(mq);
    
    printf("[Proxy] Thread %ld sent request to cache: %s\n", pthread_self(), path);
    
    // Wait for cache to set status and file size
    sem_wait(&shm->wsem);
    
    if (shm->status == 404) {
        printf("[Proxy] Thread %ld: file not found: %s\n", pthread_self(), path);
        gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
        sem_post(&shm->rsem);
        return_segment_to_pool(shm);
        return SERVER_FAILURE;
    }
    if (shm->status != 200) {
        printf("[Proxy] Thread %ld: cache error: %d\n", pthread_self(), shm->status);
        gfs_sendheader(ctx, GF_ERROR, 0);
        sem_post(&shm->rsem);
        return_segment_to_pool(shm);
        return SERVER_FAILURE;
    }

    size_t file_size = shm->size;
    gfs_sendheader(ctx, GF_OK, file_size);
    printf("[Proxy] Thread %ld sent header: OK, size=%zu\n", pthread_self(), file_size);
    
    sem_post(&shm->rsem); // signal cache we're ready for first chunk
    
    size_t total_sent = 0;
    int chunk_count = 0;

    while (total_sent < file_size) {
        sem_wait(&shm->wsem);

        ssize_t chunk_bytes = shm->bytes_written;
        if (chunk_bytes == 0) { // EOF from cache
            sem_post(&shm->rsem);
            break;
        }

        // Adjust last chunk if it exceeds remaining bytes
        if (total_sent + chunk_bytes > file_size)
            chunk_bytes = file_size - total_sent;

        ssize_t sent = gfs_send(ctx, shm->data, chunk_bytes);
        if (sent < 0) {
            perror("[Proxy] gfs_send");
            printf("[Proxy] Thread %ld: client disconnected, draining cache\n", pthread_self());

            sem_post(&shm->rsem);
            // Drain remaining data
            while (1) {
                sem_wait(&shm->wsem);
                if (shm->bytes_written == 0) {
                    sem_post(&shm->rsem);
                    break;
                }
                sem_post(&shm->rsem);
            }
            break;
        }

        total_sent += sent;
        chunk_count++;
        printf("[Proxy] Thread %ld sent chunk %d (%ld bytes) from segment %s\n",
               pthread_self(), chunk_count, sent, shm->name);

        sem_post(&shm->rsem);
    }

    return_segment_to_pool(shm);
    printf("[Proxy] Thread %ld returned segment %s to pool\n", pthread_self(), shm->name);

    return total_sent;
}
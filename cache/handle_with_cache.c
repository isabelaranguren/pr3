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
    shm_data_t *shm;
    
    // Get shared memory segment from pool
    shm = get_shm_segment();
    
    /* 
    printf("[Proxy] Thread %ld acquired segment: %s\n", pthread_self(), shm->name);
    printf("[Proxy] Thread %ld requesting file: %s\n", pthread_self(), path)*/
    
    /*Prepare request*/ 
    cache_req_t request;
    strncpy(request.path, path, sizeof(request.path) - 1);
    request.path[sizeof(request.path) - 1] = '\0';
    strncpy(request.shm_name, shm->name, sizeof(request.shm_name) - 1);
    request.shm_name[sizeof(request.shm_name) - 1] = '\0';
    request.segsize = shm->segsize;
    
    // Initialize semaphores
    sem_init(&shm->wsem, 1, 0);
    sem_init(&shm->rsem, 1, 1);
    
    // Open message queue and send request
    mqd_t mq = mq_open(CACHE_COMMAND_QUEUE, O_WRONLY);
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
    
    // printf("[Proxy] Thread %ld waiting for cache\n", pthread_self());
    
    // Wait for status and file metadata
    sem_wait(&shm->wsem);
    
    // printf("[Proxy] Thread %ld received status: %d\n", pthread_self(), shm->status);
    
    // Check status
    if (shm->status == 404) {
        // printf("[Proxy] Thread %ld: file not found\n", pthread_self());
        gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
        sem_post(&shm->rsem);
        return_segment_to_pool(shm);
        return SERVER_FAILURE;
    }
    
    if (shm->status != 200) {
        // printf("[Proxy] Thread %ld: error status %d\n", pthread_self(), shm->status);
        gfs_sendheader(ctx, GF_ERROR, 0);
        sem_post(&shm->rsem);
        return_segment_to_pool(shm);
        return SERVER_FAILURE;
    }
    
    // Send OK header to client
    size_t file_size = shm->file_size;
    gfs_sendheader(ctx, GF_OK, file_size);
    // printf("[Proxy] Thread %ld sent header: OK, size=%zu\n", pthread_self(), file_size);
    
    sem_post(&shm->rsem);  // Signal cache to start sending data
    
    // SIMPLE LOOP - Transfer data chunks
    size_t bytes_transferred = 0;
    size_t bytes_sent = 0;
    
    while (bytes_transferred < file_size) {
        sem_wait(&shm->wsem);
        
        bytes_sent = gfs_send(ctx, shm->data, shm->bytes_written);
        if (bytes_sent <= 0) {
            perror("[Proxy] Error sending data to client");
            break;
        }
        
        bytes_transferred += bytes_sent;
        /* printf("[Proxy] Thread %ld: sent %zu bytes (total: %zu/%zu)\n",
            pthread_self(), bytes_sent, bytes_transferred, file_size); */
        
        sem_post(&shm->rsem);
    }
    
    /* printf("[Proxy] Thread %ld completed: %zu bytes\n", pthread_self(), bytes_transferred); */
    
    return_segment_to_pool(shm);
    return bytes_transferred;
}
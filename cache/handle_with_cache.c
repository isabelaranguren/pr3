#include "gfserver.h"
#include "cache-student.h"
#include "shm_channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <errno.h>
#include <unistd.h>

extern steque_t shm_queue;
extern pthread_mutex_t shm_queue_mutex;
extern pthread_cond_t shm_queue_cond;

// TODO: fIX THIS
ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void *arg) {
    shm_data_t *shm = NULL;
    cache_req_t request;
    mqd_t mq;
    ssize_t total_bytes_sent = 0;

    printf("[Proxy] Thread %ld requesting file: %s\n", pthread_self(), path);

    // --- Step 1: Acquire a shared memory segment ---
    pthread_mutex_lock(&shm_queue_mutex);
    while (steque_isempty(&shm_queue)) {
        printf("[Proxy] Thread %ld waiting for a free segment...\n", pthread_self());
        pthread_cond_wait(&shm_queue_cond, &shm_queue_mutex);
    }
    shm = steque_pop(&shm_queue);
    pthread_mutex_unlock(&shm_queue_mutex);

    if (!shm) {
        fprintf(stderr, "[Proxy] Thread %ld failed to acquire segment\n", pthread_self());
        return SERVER_FAILURE;
    }

    printf("[Proxy] Thread %ld acquired segment: %s\n", pthread_self(), shm->name);

    // --- Step 2: Prepare request for cache ---
    strncpy(request.path, path, sizeof(request.path) - 1);
    request.path[sizeof(request.path) - 1] = '\0';
    strncpy(request.shm_name, shm->name, sizeof(request.shm_name) - 1);
    request.shm_name[sizeof(request.shm_name) - 1] = '\0';
    request.segsize = shm->segsize;

    // --- Step 3: Send request to cache ---
    mq = mq_open(CACHE_COMMAND_QUEUE, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("[Proxy] mq_open");
        return SERVER_FAILURE;
    }

    if (mq_send(mq, (char*)&request, sizeof(request), 0) == -1) {
        perror("[Proxy] mq_send");
        mq_close(mq);
        return SERVER_FAILURE;
    }
    mq_close(mq);

    printf("[Proxy] Thread %ld sent request to cache: %s\n", pthread_self(), path);

    // --- Step 4: Receive chunks from cache ---
    int chunk_count = 0;

    while (1) {
        printf("[Proxy] Thread %ld waiting for wsem...\n", pthread_self());
        sem_wait(&shm->wsem);  // wait until cache wrote a chunk
        printf("[Proxy] Thread %ld woke up from wsem\n", pthread_self());

        if (shm->status == 404) {
            printf("[Proxy] Thread %ld: file not found: %s\n", pthread_self(), path);
            gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
            sem_post(&shm->rsem);  // allow worker to continue
            break;
        }

        if (shm->size == 0) {  // EOF signal
            printf("[Proxy] Thread %ld received EOF on segment %s\n", pthread_self(), shm->name);
            sem_post(&shm->rsem);  // allow worker to finish
            break;
        }

        // Send chunk to client
        ssize_t sent = gfs_send(ctx, shm->data, shm->size);
        if (sent < 0) {
            perror("[Proxy] gfs_send");
            sem_post(&shm->rsem);  // allow worker to continue
            break;
        }

        total_bytes_sent += sent;
        chunk_count++;
        printf("[Proxy] Thread %ld sent chunk %d (%ld bytes) from segment %s\n",
               pthread_self(), chunk_count, sent, shm->name);

        // Signal worker that proxy has read the chunk
        sem_post(&shm->rsem);
    }

    // --- Step 5: Reset segment and return to pool ---
    shm->size = 0;
    shm->status = 0;

    pthread_mutex_lock(&shm_queue_mutex);
    steque_enqueue(&shm_queue, shm);
    pthread_cond_broadcast(&shm_queue_cond);
    pthread_mutex_unlock(&shm_queue_mutex);

    printf("[Proxy] Thread %ld returned segment %s to pool\n", pthread_self(), shm->name);

    return total_bytes_sent;
}
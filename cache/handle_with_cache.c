#include "gfserver.h"
#include "cache-student.h"
#include "shm_channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <errno.h>
#include <unistd.h>

#define CACHE_COMMAND_QUEUE "/cache_command_q"
#define BUFSIZE 8192

typedef struct {
    char path[MAX_REQUEST_LEN];
    char shm_name[MAX_SHM_NAME];
    size_t segsize;
} cache_req_t;

// TODO: fIX THIS
ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void *arg) {
    shm_data_t *shm = NULL;
    cache_req_t request;
    mqd_t mq;
    ssize_t total_bytes_sent = 0;
    int finished = 0;

    // Get an available shared memory segment 
    pthread_mutex_lock(&shm_queue_mutex);
    while (steque_isempty(&shm_queue))
        pthread_cond_wait(&shm_queue_cond, &shm_queue_mutex);

    shm = steque_pop(&shm_queue);
    pthread_mutex_unlock(&shm_queue_mutex);

    if (!shm) {
        fprintf(stderr, "[Proxy] No shared memory segment available\n");
        return SERVER_FAILURE;
    }

    /* Step 2: Send request to cache via command channel (message queue) */
    strncpy(request.path, path, sizeof(request.path) - 1);
    strncpy(request.shm_name, shm->name, sizeof(request.shm_name) - 1);
    request.segsize = shm->segsize;

    mq = mq_open(CACHE_COMMAND_QUEUE, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("[Proxy] mq_open");
        return SERVER_FAILURE;
    }

    if (mq_send(mq, (char *)&request, sizeof(request), 0) == -1) {
        perror("[Proxy] mq_send");
        mq_close(mq);
        return SERVER_FAILURE;
    }
    mq_close(mq);

    /* Step 3: Wait for cache to start writing */
    gfs_sendheader(ctx, GF_OK, 0);  // you could send file size if cache gives it later

    /* Step 4: Read chunks from shared memory and stream to client */
    while (!finished) {
        // Wait for cache to write data
        sem_wait(&shm->wsem);

        if (shm->size == 0) {
            finished = 1;
            sem_post(&shm->rsem);
            break;
        }

        ssize_t sent = gfs_send(ctx, shm->data, shm->size);
        if (sent < 0) {
            perror("[Proxy] gfs_send");
            sem_post(&shm->rsem);
            break;
        }

        total_bytes_sent += sent;

        // Allow cache to write next chunk
        sem_post(&shm->rsem);
    }

    /* Step 5: Return segment to pool */
    pthread_mutex_lock(&shm_queue_mutex);
    steque_enqueue(&shm_queue, shm);
    pthread_cond_signal(&shm_queue_cond);
    pthread_mutex_unlock(&shm_queue_mutex);

    return total_bytes_sent;
}
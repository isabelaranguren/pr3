#include "gfserver.h"
#include "cache-student.h"
#include "shm_channel.h"

#define BUFSIZE (840)

extern shm_data_t *shm_segment;
extern size_t shm_segsize;  

ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg) {
    shm_data_t *shm = shm_segment;  // use global segment
    size_t file_len;
    size_t bytes_transferred = 0;
    ssize_t read_len, write_len;
    int fd;
    struct stat statbuf;

    // Open the requested file
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
        else return SERVER_FAILURE;
    }

    if (fstat(fd, &statbuf) < 0) {
        close(fd);
        return SERVER_FAILURE;
    }

    file_len = (size_t) statbuf.st_size;
    gfs_sendheader(ctx, GF_OK, file_len);

    char buffer[BUFSIZE];

    while (bytes_transferred < file_len) {
        // Read chunk from file
        read_len = read(fd, buffer, BUFSIZE);
        if (read_len <= 0) break;

        size_t offset = 0;
        while (offset < read_len) {
            // Wait for proxy to read previous data
            sem_wait(&shm->proxy_read);

            // Determine how much fits in shared memory
            size_t chunk_size = read_len - offset;
            if (chunk_size > shm_segsize) chunk_size = shm_segsize;

            // Copy to shared memory
            memcpy(shm->data, buffer + offset, chunk_size);
            shm->size = chunk_size;

            // Signal cache written
            sem_post(&shm->cache_written);

            offset += chunk_size;
        }

        // Now read back from shared memory into proxy and send to client
        sem_wait(&shm->cache_written); // Wait until cache wrote
        write_len = gfs_send(ctx, shm->data, shm->size);
        if (write_len != shm->size) {
            close(fd);
            return SERVER_FAILURE;
        }
        sem_post(&shm->proxy_read); // Allow next chunk to be written

        bytes_transferred += read_len;
    }

    close(fd);
    return bytes_transferred;
}
#include "proxy-student.h"
#include "gfserver.h"

#define MAX_REQUEST_N 512
#define BUFSIZE (6426)

typedef struct  {
    char *res;
    size_t size;
} memory;


/* Call back function adapted from lib curl docs
https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html */
static size_t write_callback(void *data, size_t size, size_t nmemb, void *clientp)
{
    size_t realsize = nmemb * size;
    memory *mem = (memory *)clientp;

    char *ptr = realloc(mem->res, mem->size + realsize + 1);
    if(!ptr) return 0;  // out of memory

    mem->res = ptr;
    memcpy(&(mem->res[mem->size]), data, realsize);
    mem->size += realsize;

    return realsize;
}

ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void *arg)
{
    CURL *curl = curl_easy_init();
    if (!curl) return SERVER_FAILURE;

    char url[BUFSIZE];
    snprintf(url, sizeof(url), "%s%s", (char *)arg, path);

    CURLcode ret;
    memory chunk = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);


    // fprintf(stderr, "URL: %s\n", url);

    ret = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (ret != CURLE_OK) {
        free(chunk.res);
        return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    }

    // Send header with actual size
    // fprintf(stderr, "downloaded size: %zu bytes\n", chunk.size);
    gfs_sendheader(ctx, GF_OK, chunk.size);

    // Stream data to client
    size_t total_sent = 0;
    while (total_sent < chunk.size) {
        ssize_t sent = gfs_send(ctx, chunk.res + total_sent, chunk.size - total_sent);
        if (sent <= 0) {
            // fprintf(stderr, "gfs_send failed after %zu bytes sent\n", total_sent);
            free(chunk.res);
            return SERVER_FAILURE;
        }
        total_sent += sent;
        // fprintf(stderr, "Sent %zu/%zu bytes...\n", total_sent, chunk.size);
    }

    free(chunk.res);
    return total_sent;
}

ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg) {
    return handle_with_curl(ctx, path, arg);
}
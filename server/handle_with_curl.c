#include "proxy-student.h"
#include "gfserver.h"



#define MAX_REQUEST_N 512
#define BUFSIZE (6426)

typedef struct  {
	gfcontext_t *ctx;
	size_t total_bytes;
} Stream;
 
static size_t write_cb( void *data, size_t size, size_t n, void *ptr)
{
	Stream *s = (Stream *)ptr;
	size_t bytes = size * n;
	size_t sent = gfs_send(s->ctx, data, bytes);

	if (sent != (ssize_t)bytes) {
        fprintf(stderr, "gfs_send failed\n");
        return 0;
    }

	s->total_bytes += sent;
	return bytes;
}


ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void* arg){
	//Your implementation here

	CURL *curl_handle;
	CURLcode ret;


	curl_handle = curl_easy_init();
	if (!curl_handle) return -1;


	char url[BUFSIZE];
	snprintf(url, sizeof(url), "%s%s", (char *)arg, path);	


	Stream s;
	s.ctx = ctx;
	s.total_bytes = 0;

	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &s);
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);

	
	ret = curl_easy_perform(curl_handle);

	if (ret != CURLE_OK) {
        return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    }

	curl_easy_cleanup(curl_handle);
	gfs_sendheader(ctx, GF_OK, s.total_bytes);

	return s.total_bytes;

}

/*
 * We provide a dummy version of handle_with_file that invokes handle_with_curl as a convenience for linking!
 */
ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg){
	return handle_with_curl(ctx, path, arg);
}	

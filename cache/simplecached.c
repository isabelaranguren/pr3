#include <stdio.h>
#include <unistd.h>
#include <printf.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include "cache-student.h"
#include "shm_channel.h"
#include "simplecache.h"
#include "gfserver.h"
#include <mqueue.h>

// CACHE_FAILURE
#if !defined(CACHE_FAILURE)
    #define CACHE_FAILURE (-1)
#endif 

#define MAX_CACHE_REQUEST_LEN 6112
#define MAX_SIMPLE_CACHE_QUEUE_SIZE 783

unsigned long int cache_delay;

static void _sig_handler(int signo){
	if (signo == SIGTERM || signo == SIGINT){
		mq_unlink(CACHE_COMMAND_QUEUE);		
		simplecache_destroy();
		exit(signo);
	}
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default is 8, Range is 1-100)\n"      \
"  -d [delay]          Delay in simplecache_get (Default is 0, Range is 0-2500000 (microseconds)\n "	\
"  -h                  Show this help message\n"

//OPTIONS
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
  {"delay", 			 required_argument,		 NULL, 			 'd'}, // delay.
  {NULL,                 0,                      NULL,             0}
};

mqd_t mqd;

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

void *cacheWorker(void *arg) {
    pthread_t tid = pthread_self();
    cache_req_t request;
    mqd_t mqd = *(mqd_t *)arg; 
    
    while (1) {
        // Receive request from message queue
        int n = mq_receive(mqd, (char*)&request, sizeof(request), NULL);
        if (n <= 0) {
            perror("[Cache] mq_receive");
            continue;
        }
        
        printf("[Cache TID:%lu] Request: %s, segment: %s\n",
               (unsigned long)tid, request.path, request.shm_name);
        
        // Open shared memory segment
        int shmfd = shm_open(request.shm_name, O_RDWR, 0);
        if (shmfd < 0) {
            perror("[Cache] shm_open");
            continue;
        }
        
        size_t shm_total_size = sizeof(shm_data_t) + request.segsize;
        
        if (ftruncate(shmfd, shm_total_size) == -1) {
            perror("[Cache] ftruncate");
            close(shmfd);
            continue;
        }
        
        shm_data_t *shm = mmap(NULL, shm_total_size, PROT_READ | PROT_WRITE,
                               MAP_SHARED, shmfd, 0);
        if (shm == MAP_FAILED) {
            perror("[Cache] mmap");
            close(shmfd);
            continue;
        }
        
        // Wait for proxy to initialize semaphores
        sem_wait(&shm->rsem);
        
        // Try to get file from cache
        int fd = simplecache_get(request.path);
        
        if (fd < 0) {
            // File not found
            printf("[Cache TID:%lu] File not found: %s\n", (unsigned long)tid, request.path);
            shm->status = 404;
            shm->file_size = 0;
            sem_post(&shm->wsem);
            munmap(shm, shm_total_size);
            close(shmfd);
            continue;
        }
        
        // Get file size
        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("[Cache] fstat");
            close(fd);
            munmap(shm, shm_total_size);
            close(shmfd);
            continue;
        }
        size_t file_size = st.st_size;
        
        printf("[Cache TID:%lu] Serving: %s (%zu bytes) in segment %s\n",
               (unsigned long)tid, request.path, file_size, request.shm_name);
        
        // Send status and file size to proxy
        shm->status = 200;
        shm->file_size = file_size;
        sem_post(&shm->wsem);  // Signal metadata ready
        
        // Transfer file in chunks
        char buffer[request.segsize];
        size_t bytes_read = 0;
        size_t offset = 0;
        
        while (bytes_read < file_size) {
            sem_wait(&shm->rsem);  // Wait for proxy to be ready
            
            size_t bytes_to_read = (file_size - bytes_read < request.segsize) 
                                   ? (file_size - bytes_read) 
                                   : request.segsize;
            
            ssize_t nbytes = pread(fd, buffer, bytes_to_read, offset);
            if (nbytes <= 0) {
                perror("[Cache] pread error");
                break;
            }
            
            // Copy to shared memory
            shm->bytes_written = nbytes;
            memcpy(shm->data, buffer, nbytes);
            
            bytes_read += nbytes;
            offset += nbytes;
            
            printf("[Cache TID:%lu] Chunk: %zd bytes (total: %zu/%zu)\n",
                   (unsigned long)tid, nbytes, bytes_read, file_size);
            
            sem_post(&shm->wsem);  // Signal chunk ready
        }
        
        printf("[Cache TID:%lu] Finished: %zu bytes\n", (unsigned long)tid, bytes_read);
        
        close(fd);
        munmap(shm, shm_total_size);
        close(shmfd);
    }
    
    return NULL;
}

int main(int argc, char **argv) {
	int nthreads = 6;
	char *cachedir = "locals.txt";
	char option_char;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "d:ic:hlt:x", gLongOptions, NULL)) != -1) {
		switch (option_char) {
			default:
				Usage();
				exit(1);
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;				
			case 'h': // help
				Usage();
				exit(0);
				break;    
            case 'c': //cache directory
				cachedir = optarg;
				break;
            case 'd':
				cache_delay = (unsigned long int) atoi(optarg);
				break;
			case 'i': // server side usage
			case 'o': // do not modify
			case 'a': // experimental
				break;
		}
	}

	if (cache_delay > 2500000) {
		fprintf(stderr, "Cache delay must be less than 2500000 (us)\n");
		exit(__LINE__);
	}

	if ((nthreads>100) || (nthreads < 1)) {
		fprintf(stderr, "Invalid number of threads must be in between 1-100\n");
		exit(__LINE__);
	}
	if (SIG_ERR == signal(SIGINT, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(CACHE_FAILURE);
	}
	if (SIG_ERR == signal(SIGTERM, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(CACHE_FAILURE);
	}
	/*Initialize cache*/
	simplecache_init(cachedir);

	// Cache should go here
     struct mq_attr attr = {0};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(cache_req_t);

    mq_unlink(CACHE_COMMAND_QUEUE); // remove old queue

    mqd = mq_open(CACHE_COMMAND_QUEUE, O_CREAT | O_RDWR, 0666, &attr);
    if (mqd == (mqd_t)-1) {
        perror("mq_open");
        exit(CACHE_FAILURE);
    }

    printf("[Main] Message queue created\n");

   pthread_t workers[nthreads];
    for (int i = 0; i < nthreads; i++) {
        if (pthread_create(&workers[i], NULL, cacheWorker, (void *)&mqd) != 0) {
            perror("pthread_create");
            exit(CACHE_FAILURE);
        }
        printf("[Main] Worker %d created\n", i);
    }

    // Block indefinitely
    for (int i = 0; i < nthreads; i++) {
        pthread_join(workers[i], NULL);
    }

	// Line never reached
	return -1;
}
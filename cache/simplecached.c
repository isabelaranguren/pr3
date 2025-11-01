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
		// This is where your IPC clean up should occur
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
    mqd_t mq = *((mqd_t *)arg);
    cache_req_t request;
    pthread_t tid = pthread_self();

    while (1) {
        if (mq_receive(mq, (char*)&request, sizeof(request), NULL) == -1) {
            perror("[Cache] mq_receive");
            continue;
        }

        printf("[Cache TID:%lu] Received request for file: %s, shm segment: %s\n",
               (unsigned long)tid, request.path, request.shm_name);

        // Open shared memory segment
        int shmfd = shm_open(request.shm_name, O_RDWR, 0);
        if (shmfd < 0) {
            perror("[Cache] shm_open");
            continue;
        }

        size_t shm_total_size = sizeof(shm_data_t) + request.segsize;
        shm_data_t *shm = mmap(NULL, shm_total_size, PROT_READ | PROT_WRITE,
                               MAP_SHARED, shmfd, 0);
        if (shm == MAP_FAILED) {
            perror("[Cache] mmap");
            close(shmfd);
            continue;
        }

        // Open file from cache
        int fd = simplecache_get(request.path);
        if (fd < 0) {
            printf("[Cache] File not found: %s\n", request.path);
            shm->status = 404;
            shm->size = 0;
            sem_post(&shm->wsem);
            munmap(shm, shm_total_size);
            close(shmfd);
            continue;
        }
        // Get file size
        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("[Cache] fstat");
            shm->status = 500;
            sem_post(&shm->wsem);
            munmap(shm, shm_total_size);
            close(shmfd);
            continue;
        }
                
        size_t file_size = st.st_size;
        printf("[Cache TID:%lu] Serving file: %s (%zu bytes)\n", 
               (unsigned long)tid, request.path, file_size);
        
        // Send status and file size first
        shm->status = 200;
        shm->size = file_size;  // Total file size
        shm->bytes_written = 0; // No data yet
        sem_post(&shm->wsem);   // Signal proxy to read status
        
        // Wait for proxy to be ready
        sem_wait(&shm->rsem);

        // Now send file data in chunks
        size_t bytes_sent = 0;
        while (bytes_sent < file_size) {
            size_t bytes_left = file_size - bytes_sent;
            size_t chunk_size = (bytes_left < request.segsize) ? bytes_left : request.segsize;

            ssize_t n = pread(fd, shm->data, chunk_size, bytes_sent);
            if (n <= 0) {
                perror("[Cache] pread");
                shm->bytes_written = 0;
                sem_post(&shm->wsem);
                break;
            }

            shm->bytes_written = n;
            bytes_sent += n;

            printf("[Cache TID:%lu] Wrote chunk: %zd bytes (total: %zu/%zu)\n",
                   (unsigned long)tid, n, bytes_sent, file_size);

            sem_post(&shm->wsem);  // signal proxy
            sem_wait(&shm->rsem);  // wait for proxy to read
        }

        // Signal EOF
        shm->bytes_written = 0;
        sem_post(&shm->wsem);
        printf("[Cache TID:%lu] Finished file: %s\n", (unsigned long)tid, request.path);

        munmap(shm, shm_total_size);
        close(fd);
        close(shmfd);
    }
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
        mqd_t *mq_copy = malloc(sizeof(mqd_t));
        *mq_copy = mqd;
        if (pthread_create(&workers[i], NULL, cacheWorker, mq_copy) != 0) {
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
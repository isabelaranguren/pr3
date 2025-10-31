/*
 You can use this however you want.
 */
 #ifndef __CACHE_STUDENT_H__844
 
 #define __CACHE_STUDENT_H__844

 #include "steque.h"


#define CACHE_COMMAND_QUEUE "/cache_command_q"
#define MAX_SHM_NAME 100
#define MAX_CACHE_REQUEST_LEN 6112

typedef struct {
    char path[MAX_CACHE_REQUEST_LEN];
    char shm_name[MAX_SHM_NAME];
    size_t segsize;
} cache_req_t;

 #endif // __CACHE_STUDENT_H__844
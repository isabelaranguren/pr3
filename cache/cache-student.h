/*
 You can use this however you want.
 */
 #ifndef __CACHE_STUDENT_H__844
 
 #define __CACHE_STUDENT_H__844

 #include "steque.h"
 #include <semaphore.h>
 #include <pthread.h>

typedef struct {
    char shm_name[64];  
    char path[256];      
    int request_id;     
} request_t;

 #endif // __CACHE_STUDENT_H__844
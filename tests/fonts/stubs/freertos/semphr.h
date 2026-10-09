#pragma once
#include "FreeRTOS.h"
typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t* SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t*s){pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(s,&a);pthread_mutexattr_destroy(&a);return s;}
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t s,uint32_t t){(void)t;return pthread_mutex_lock(s)==0;}
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t s){return pthread_mutex_unlock(s)==0;}

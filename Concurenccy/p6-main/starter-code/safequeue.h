#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <stdio.h>
#include <stdlib.h>



typedef struct {
    struct http_request *array;
    int capacity;
    int size;
} PQueue;

PQueue* create_queue(int capacity);
void add_work(PQueue* pq, struct http_request data);
struct http_request get_work(PQueue* pq);

#endif
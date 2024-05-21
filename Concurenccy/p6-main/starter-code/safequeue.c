#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "safequeue.h"
#include "proxyserver.h"

// Function to initialize a new PQueue
PQueue* create_queue(int capacity) {
    PQueue* pq = (PQueue*)malloc(sizeof(PQueue));
    pq->array = (struct http_request*)malloc(capacity * sizeof(struct http_request));
    pq->capacity = capacity;
    pq->size = 0;
    return pq;
}

// Function to swap two Datas
void swap(struct http_request* a, struct http_request* b) {
    struct http_request temp = *a;
    *a = *b;
    *b = temp;
}

// Function to compare Datas based on priority (larger number has higher priority)
int compare(struct http_request a, struct http_request b) {
    return a.priority > b.priority;
}

// Function to heapify the PQueue
void heapify(PQueue* pq, int index) {
    int largest = index;
    int left = 2 * index + 1;
    int right = 2 * index + 2;

    if (left < pq->size && compare(pq->array[left], pq->array[largest]))
        largest = left;

    if (right < pq->size && compare(pq->array[right], pq->array[largest]))
        largest = right;

    if (largest != index) {
        swap(&pq->array[index], &pq->array[largest]);
        heapify(pq, largest);
    }
}

// Function to insert a struct http_request into the PQueue
void add_work(PQueue* pq, struct http_request data) {
    if (pq->size == pq->capacity) {
        printf("Queue is full. Cannot enqueue.\n");
        return;
    }

    pq->array[pq->size] = data;
    int currentIndex = pq->size;

    while (currentIndex > 0 && compare(pq->array[currentIndex], pq->array[(currentIndex - 1) / 2])) {
        swap(&pq->array[currentIndex], &pq->array[(currentIndex - 1) / 2]);
        currentIndex = (currentIndex - 1) / 2;
    }

    pq->size++;
}

// Function to dequeue the struct http_request with the highest priority
struct http_request get_work(PQueue* pq) {
    if (pq->size == 0) {
        printf("Queue is empty. Cannot dequeue.\n");
        exit(1);
        // return NULL;
    }

    struct http_request root = pq->array[0];
    pq->array[0] = pq->array[pq->size - 1];
    pq->size--;

    heapify(pq, 0);

    return root;
}
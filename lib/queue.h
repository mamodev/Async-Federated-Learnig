#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#define QUEUE_OK 0
#define QUEUE_CLOSED -2
#define QUEUE_EMPTY -3

#define define_queue(T, N)        \
    typedef struct                \
    {                             \
        T *data;                  \
        uint8_t closed;           \
        int head;                 \
        int tail;                 \
        int size;                 \
        int capacity;             \
        pthread_mutex_t lock;     \
        pthread_cond_t wait_read; \
    } N##_queue_t;

#define declare_queue(T, N)                             \
    int queue_##N##_init(N##_queue_t *q, int capacity); \
    void queue_##N##_destroy(N##_queue_t *q);           \
    int queue_##N##_enqueue(N##_queue_t *q, T item);    \
    int queue_##N##_dequeue(N##_queue_t *q, T *item);   \
    void queue_##N##_close(N##_queue_t *q);             \
    int queue_##N##_tdequeue(N##_queue_t *q, T *item, struct timespec *ts);

#define define_queue_methods(T, N)                                         \
    int queue_##N##_init(N##_queue_t *q, int capacity)                     \
    {                                                                      \
        q->data = (T *)malloc(capacity * sizeof(T));                       \
        if (q->data == NULL)                                               \
        {                                                                  \
            return -1;                                                     \
        }                                                                  \
        q->head = 0;                                                       \
        q->tail = 0;                                                       \
        q->size = 0;                                                       \
        q->capacity = capacity;                                            \
        if (pthread_mutex_init(&q->lock, NULL) != 0)                       \
        {                                                                  \
            return -1;                                                     \
        }                                                                  \
        return 0;                                                          \
    }                                                                      \
    void queue_##N##_close(N##_queue_t *q)                                 \
    {                                                                      \
        pthread_mutex_lock(&q->lock);                                      \
        q->closed = 1;                                                     \
        pthread_cond_broadcast(&q->wait_read);                             \
        pthread_mutex_unlock(&q->lock);                                    \
    }                                                                      \
                                                                           \
    void queue_##N##_destroy(N##_queue_t *q)                               \
    {                                                                      \
        free(q->data);                                                     \
    }                                                                      \
    int queue_##N##_enqueue(N##_queue_t *q, T item)                        \
    {                                                                      \
        pthread_mutex_lock(&q->lock);                                      \
        assert(q->closed == 0);                                            \
        if (q->size == 0)                                                  \
        {                                                                  \
            pthread_cond_signal(&q->wait_read);                            \
        }                                                                  \
        if (q->size == q->capacity)                                        \
        {                                                                  \
            pthread_mutex_unlock(&q->lock);                                \
            return -1;                                                     \
        }                                                                  \
        q->data[q->tail] = item;                                           \
        q->tail = (q->tail + 1) % q->capacity;                             \
        q->size++;                                                         \
        pthread_mutex_unlock(&q->lock);                                    \
        return QUEUE_OK;                                                   \
    }                                                                      \
    int queue_##N##_dequeue(N##_queue_t *q, T *item)                       \
    {                                                                      \
        pthread_mutex_lock(&q->lock);                                      \
        while (q->size == 0)                                               \
        {                                                                  \
            pthread_cond_wait(&q->wait_read, &q->lock);                    \
            if (q->closed)                                                 \
            {                                                              \
                pthread_mutex_unlock(&q->lock);                            \
                return QUEUE_CLOSED;                                       \
            }                                                              \
        }                                                                  \
        *item = q->data[q->head];                                          \
        q->head = (q->head + 1) % q->capacity;                             \
        q->size--;                                                         \
        pthread_mutex_unlock(&q->lock);                                    \
        return QUEUE_OK;                                                   \
    }                                                                      \
    int queue_##N##_tdequeue(N##_queue_t *q, T *item, struct timespec *ts) \
    {                                                                      \
        pthread_mutex_lock(&q->lock);                                      \
        uint8_t timedout = 0;                                              \
        while (q->size == 0)                                               \
        {                                                                  \
            if (timedout)                                                  \
            {                                                              \
                pthread_mutex_unlock(&q->lock);                            \
                return QUEUE_EMPTY;                                        \
            }                                                              \
                                                                           \
            int ret = pthread_cond_timedwait(&q->wait_read, &q->lock, ts); \
            if (ret == ETIMEDOUT)                                          \
            {                                                              \
                timedout = 1;                                              \
            }                                                              \
            else if (ret != 0)                                             \
            {                                                              \
                pthread_mutex_unlock(&q->lock);                            \
                return -1;                                                 \
            }                                                              \
            if (q->closed)                                                 \
            {                                                              \
                pthread_mutex_unlock(&q->lock);                            \
                return QUEUE_CLOSED;                                       \
            }                                                              \
        }                                                                  \
        *item = q->data[q->head];                                          \
        q->head = (q->head + 1) % q->capacity;                             \
        q->size--;                                                         \
        pthread_mutex_unlock(&q->lock);                                    \
        return QUEUE_OK;                                                   \
    }

#define declare_queue_type(T, N) \
    define_queue(T, N)           \
        declare_queue(T, N)

#endif // QUEUE_H

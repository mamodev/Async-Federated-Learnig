#ifndef METRICS_H
#define METRICS_H

#include "queue.h"
#include <time.h>
#include <pthread.h>

define_queue(void *, metrics)

typedef struct {
    const char *print_format;
    size_t size;
} metric_t;

extern const metric_t MT_INT;
extern const metric_t MT_UINT;
extern const metric_t MT_LONG ;
extern const metric_t MT_ULONG;
extern const metric_t MT_LLONG;
extern const metric_t MT_ULLONG;  
extern const metric_t MT_FLOAT;

typedef struct {
    struct timespec flush_interval;
    metrics_queue_t *queue;
    const metric_t *metrics;
    pthread_t thread;
} metrics_logger_t;

int metrics_logger_init(metrics_logger_t *logger, const metric_t *metrics, int capacity, uint64_t flush_interval_ms);
void metrics_logger_destroy(metrics_logger_t *logger);
int metrics_logger_log(metrics_logger_t *logger, ...);


#endif // METRICS_H
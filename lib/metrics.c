#include "metrics.h"

const metric_t MT_INT = {"%d", sizeof(int)};
const metric_t MT_UINT = {"%u", sizeof(unsigned int)};
const metric_t MT_LONG = {"%ld", sizeof(long)};
const metric_t MT_ULONG = {"%lu", sizeof(unsigned long)};
const metric_t MT_LLONG = {"%lld", sizeof(long long)};
const metric_t MT_ULLONG = {"%llu", sizeof(unsigned long long)};
const metric_t MT_FLOAT = {"%f", sizeof(float)};

int metrics_logger_init(metrics_logger_t *logger, const metric_t *metrics, int capacity, uint64_t flush_interval_ms)
{
    if (queue_metrics_init(logger->queue, capacity) != 0)
    {
        return -1;
    }

    logger->metrics = metrics;
    logger->flush_interval.tv_sec = flush_interval_ms / 1000;
    logger->flush_interval.tv_nsec = (flush_interval_ms % 1000) * 1000000;

    return 0;
}
void metrics_logger_destroy(metrics_logger_t *logger);
int metrics_logger_log(metrics_logger_t *logger, ...);

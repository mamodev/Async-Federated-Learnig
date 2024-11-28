#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include <stdint.h>
#include <time.h>

#include "globals.h"

typedef struct
{
    uint8_t type; // 0: NO, 1: YES, 2: WAIT
    struct timespec ts;
} agg_config_t;

// This function must be defined by the user
int aggregate_models(model_upd_t **updates, size_t len);

// This function must be defined by the user
double get_weights_from_metadata(char *buff, size_t size);

// This function must be defined by the user
// Note that all updates are ensured as diffs from the latest global model
void should_aggregate_models(size_t buffered_updates, struct timespec *last_aggregation, agg_config_t *conf);

// thread for handling model updates
void model_queue_thread(void *_args);

#endif // AGGREGATOR_H
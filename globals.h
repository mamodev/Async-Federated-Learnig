#ifndef CONST_H
#define CONST_H

#include <stdint.h>
#include <errno.h>
#include <assert.h>
// include all type heade (uint8_t, uint16_t, uint32_t, uint64_t, size_t)
#include <stddef.h>
#include <unistd.h>
#include <pthread.h>

#include "socket_server.h"
#include "buffer.h"
#include "queue.h"
#include "debug.h"
#include "model.h"
#include "fs.h"

#define PORT 8080
#define SERVER_EVENT_LOOP_TIMEOUT 1000
#define MAX_MESSAGE_SIZE 1024 * 10
#define MAX_PENDING_MODEL_UPDATES 100

#define MODEL_FOLDER "./data/"
#define UPDATE_FOLDER "./data/updates/"

// Updates
typedef struct
{
    char file_name[255];
} model_upd_t;

declare_queue_type(model_upd_t *, model_upd);

extern model_file_info_t global_model_info;
extern model_upd_queue_t model_queue;

extern uint64_t current_global_model;
extern pthread_mutex_t global_model_lock;

static inline int open_model(uint64_t id)
{
    // path = MODEL_FOLDER + / + id
    char path[255];
    int n = sprintf(path, "%s/%ld", MODEL_FOLDER, id);
    assert(n > 0);
    path[n] = '\0';

    return open(path, O_RDONLY);
}

static inline int open_model_w(uint64_t id)
{
    // path = MODEL_FOLDER + / + id
    char path[255];
    int n = sprintf(path, "%s/%ld", MODEL_FOLDER, id);
    assert(n > 0);
    path[n] = '\0';

    return open(path, O_WRONLY | O_CREAT, 0644);
}

#define set_global_model_id(id)             \
    pthread_mutex_lock(&global_model_lock); \
    current_global_model = id;              \
    pthread_mutex_unlock(&global_model_lock);

#define global_model_id ({                    \
    uint64_t id;                              \
    pthread_mutex_lock(&global_model_lock);   \
    id = current_global_model;                \
    pthread_mutex_unlock(&global_model_lock); \
    id;                                       \
})

#ifdef DECLARE_GLOBALS
define_queue_methods(model_upd_t *, model_upd);
model_file_info_t global_model_info = {0};
model_upd_queue_t model_queue;
uint64_t current_global_model = 0;
pthread_mutex_t global_model_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// Session Types
#define AUTH_TOKEN_SIZE 255
typedef enum
{
    AUTHENTICATING = 0,
    IDLE = 1,
    WEIGHT_STREAM = 2,
} session_state_t;

typedef struct
{
    int fd; // this is safe to use untill the update is done
    uint64_t model_id;
    uint64_t written;
    uint64_t stream_size;
    uint8_t done;
} client_update_t;

typedef struct
{
    GENERIC_SESSION_FIELDS

    // PUT YOUR CUSTOM FIELDS HERE
    session_state_t state;
    char auth_token[AUTH_TOKEN_SIZE];
    client_update_t model_update; // this is in a valid state only if client state is WEIGHT_STREAM
} session_t;

#endif // CONST_H
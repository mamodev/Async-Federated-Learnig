#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int fd;
} event_loop_t;

typedef enum
{
    EVENT_READ = 1 << 0,
    EVENT_WRITE = 1 << 1,
} event_type_t;

#define EVENT_ERROR 1 << 0
#define EVENT_EOF 1 << 1

typedef struct
{
    event_type_t events;
    uint8_t flags;
    void *data;
} event_t;

int event_loop_init(event_loop_t *loop);
int event_loop_destroy(event_loop_t *loop);
int event_loop_add(event_loop_t *loop, int fd, event_type_t events, size_t data_size, void **data_ptr);
// WARNING: DATA POINTER IS NOT RECOVERED FROM PREVIOS event_loop_add CALL
int event_loop_modify(event_loop_t *loop, int fd, event_type_t events, void *data);
int event_loop_delete(event_loop_t *loop, int fd);
int event_loop_wait(event_loop_t *loop, event_t *events, int max_events, int timeout);

#endif // EVENT_LOOP_H
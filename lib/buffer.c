#include "buffer.h"

#include <stdlib.h>
#include <assert.h>

#include <pthread.h>

buffer_t *allocate_buffer(size_t capacity)
{
    buffer_t *buffer = (buffer_t *)malloc(sizeof(buffer_t) + capacity);
    if (buffer == NULL)
        return NULL;

    buffer->size = 0;
    buffer->type = 0;
    buffer->capacity = capacity;
    return buffer;
}

// Note that this function does not allocate memory for the buffer,
// it only converts the data to a buffer
// YOU MUST FREE THE DATA YOURSELF (OF THE UNDERLYING BUFFER)
buffer_t *convert_to_buffer(char *data, size_t size)
{
    assert(data != NULL);
    assert(size > 0);

    struct raw_buffer_t *buffer = (struct raw_buffer_t *)malloc(sizeof(struct raw_buffer_t));
    if (buffer == NULL)
        return NULL;

    buffer->size = size;
    buffer->capacity = size;
    buffer->type = 1;
    buffer->__data = data;

    return (buffer_t *)buffer;
}

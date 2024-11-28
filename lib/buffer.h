#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

typedef struct
{
    size_t size;
    size_t capacity;
    uint8_t type; // 0: inline data, 1: pointer to data
    char __data[];
} buffer_t;

struct raw_buffer_t
{
    size_t size;
    size_t capacity;
    uint8_t type; // 0: inline data, 1: pointer to data
    char *__data;
};

buffer_t *allocate_buffer(size_t capacity);

// Note that this function does not allocate memory for the buffer,
// it only converts the data to a buffer
// YOU MUST FREE THE DATA YOURSELF (OF THE UNDERLYING BUFFER)
buffer_t *convert_to_buffer(char *data, size_t size);

#define buffer_free(buffer) ({ \
    if (buffer->type == 1)     \
        free(buffer);          \
})

#define buffer_ptr(buffer) (buffer->type == 0 ? buffer->__data : ((struct raw_buffer_t *)buffer)->__data)
#define buffer_next(buffer) (buffer_ptr(buffer) + buffer->size)
#define buffer_remaining(buffer) buffer->capacity - buffer->size
#define buffer_full(buffer) buffer->size == buffer->capacity

#define buffer_cptr(buffer, cursor) (buffer_ptr(buffer) + cursor)

#define buffer_cnext(buffer, cursor, element_size) ({ \
    typeof(cursor) temp_cursor = cursor;              \
    cursor += element_size;                           \
    buffer_ptr(buffer) + temp_cursor;                 \
})

#define buffer_cremaining(buffer, cursor) buffer->size - cursor
#define buffer_crewind(buffer, size_t) ({ \
    cursor -= size_t;                     \
})

#define buffer_has_uint16(buffer, cursor) (buffer_cremaining(buffer, cursor) >= sizeof(uint16_t))
#define buffer_read_uint16(buffer, cursor) (*(uint16_t *)buffer_cnext(buffer, cursor, sizeof(uint16_t)))
#define buffer_read_net_uint16(buffer, cursor) ntohs(buffer_read_uint16(buffer, cursor))

#define buffer_has_uint32(buffer, cursor) (buffer_cremaining(buffer, cursor) >= sizeof(uint32_t))
#define buffer_read_uint32(buffer, cursor) (*(uint32_t *)buffer_cnext(buffer, cursor, sizeof(uint32_t)))
#define buffer_read_net_uint32(buffer, cursor) ntohl(buffer_read_uint32(buffer, cursor))

#define buffer_has_uint64(buffer, cursor) (buffer_cremaining(buffer, cursor) >= sizeof(uint64_t))
#define buffer_read_uint64(buffer, cursor) (*(uint64_t *)buffer_cnext(buffer, cursor, sizeof(uint64_t)))
#define buffer_read_net_uint64(buffer, cursor) be64toh(buffer_read_uint64(buffer, cursor))

#define buffer_has_uint8(buffer, cursor) (buffer_cremaining(buffer, cursor) >= sizeof(uint8_t))
#define buffer_read_uint8(buffer, cursor) (*(uint8_t *)buffer_cnext(buffer, cursor, sizeof(uint8_t)))

#define BUFF_NOT_ENOUGH_DATA (-1)
#define BUFF_DEST_SIZE_TOO_SMALL (-2)
#define BUFF_INVALID_STRING_SIZE (-3)
#define BUFF_INVALID_DEST (-4)
#define BUFF_SUCCESS (0)

#define BUFF_NOT_ENOUGH_DATA_STR "Not enough data in the buffer to read the next element"
#define BUFF_DEST_SIZE_TOO_SMALL_STR "Destination size is too small to store the next element"
#define BUFF_INVALID_STRING_SIZE_STR "Invalid string size"
#define BUFF_INVALID_DEST_STR "Invalid destination"

#define decode_buffer_error(error) ({             \
    char *error_str = NULL;                       \
    switch (error)                                \
    {                                             \
    case BUFF_NOT_ENOUGH_DATA:                    \
        error_str = BUFF_NOT_ENOUGH_DATA_STR;     \
        break;                                    \
    case BUFF_DEST_SIZE_TOO_SMALL:                \
        error_str = BUFF_DEST_SIZE_TOO_SMALL_STR; \
        break;                                    \
    case BUFF_INVALID_STRING_SIZE:                \
        error_str = BUFF_INVALID_STRING_SIZE_STR; \
        break;                                    \
    case BUFF_INVALID_DEST:                       \
        error_str = BUFF_INVALID_DEST_STR;        \
        break;                                    \
    }                                             \
    error_str;                                    \
})

#define MERGE_(a, b) a##b
#define LABEL_(a, b) MERGE_(a, b)
#define UNIQUE_NAME LABEL_(unique, __LINE__)

#define buffer_read_str(buffer, cursor, dest, size) ({                 \
    ssize_t str_len = 0;                                               \
    if (dest == NULL && size < 1)                                      \
    {                                                                  \
        str_len = BUFF_INVALID_DEST;                                   \
        goto UNIQUE_NAME;                                              \
    }                                                                  \
                                                                       \
    if (!buffer_has_uint32(buffer, cursor))                            \
    {                                                                  \
        str_len = BUFF_NOT_ENOUGH_DATA;                                \
        goto UNIQUE_NAME;                                              \
    }                                                                  \
                                                                       \
    str_len = (ssize_t)buffer_read_net_uint32(buffer, cursor);         \
    if (str_len >= size)                                               \
    {                                                                  \
        printf("str_len: %ld, size: %d\n", str_len, size);             \
        buffer_crewind(buffer, sizeof(uint32_t));                      \
        str_len = BUFF_DEST_SIZE_TOO_SMALL;                            \
        goto UNIQUE_NAME;                                              \
    }                                                                  \
                                                                       \
    if (buffer_cremaining(buffer, cursor) < str_len)                   \
    {                                                                  \
        buffer_crewind(buffer, sizeof(uint32_t));                      \
        str_len = BUFF_INVALID_STRING_SIZE;                            \
        goto UNIQUE_NAME;                                              \
    }                                                                  \
                                                                       \
    if (strlen != 0)                                                   \
        strncpy(dest, buffer_cnext(buffer, cursor, str_len), str_len); \
    dest[str_len] = '\0';                                              \
                                                                       \
UNIQUE_NAME:                                                           \
    str_len;                                                           \
})

#endif // BUFFER_H
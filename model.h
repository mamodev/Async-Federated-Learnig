#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <stddef.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#pragma pack(push, 1) // Set alignment to 1 byte
typedef struct
{
    uint8_t data_type;
    uint8_t dim;
    uint8_t name_len;
    uint8_t data[];
} mf_theader_t;
#pragma pack(pop) // Restore the previous alignment

// This is safe becouse string is not a valid data type
#define loop_theaders(buff, size)                 \
    for (mf_theader_t *th = (mf_theader_t *)buff; \
         (char *)th < (buff + size);              \
         th = (mf_theader_t *)((char *)th + sizeof(mf_theader_t) + th->name_len + MF_SIZE(th->data_type) * th->dim))

typedef struct
{
    uint64_t file_size;
    uint8_t flags;
    uint8_t version;
    uint64_t diffed_from_model_version; // valid only if flags & MF_FLAG_DIFF_FORMAT

    uint32_t tensor_header_size;
    uint32_t metadata_size;
    uint64_t data_size;

    size_t tensor_header_offset; // valid only if tensor_header_size > 0
    size_t metadata_offset;      // valid only if metadata_size > 0
    size_t data_offset;
} model_file_info_t;

#pragma pack(push, 1) // Set alignment to 1 byte
typedef struct
{
    uint8_t data_type;
    uint16_t name_len;
    char buff[]; // Flexible array member
} mf_metadata_t;
#pragma pack(pop) // Restore the previous alignment

#define mf_meta_compute_size(meta) ({                                                                                        \
    size_t size;                                                                                                             \
    if ((meta)->data_type == MF_TSTRING)                                                                                     \
    {                                                                                                                        \
        size = sizeof(mf_metadata_t) + (meta)->name_len + sizeof(uint32_t) + *(uint32_t *)((meta)->buff + (meta)->name_len); \
    }                                                                                                                        \
    else                                                                                                                     \
    {                                                                                                                        \
        size = sizeof(mf_metadata_t) + (meta)->name_len + MF_SIZE((meta)->data_type);                                        \
    }                                                                                                                        \
    size;                                                                                                                    \
})

#define loop_metadata(buff, size) \
    for (mf_metadata_t *meta = (mf_metadata_t *)buff; (char *)meta < (buff + size); meta = (mf_metadata_t *)((char *)meta + mf_meta_compute_size(meta)))

#define mfi_get_data_ptr(info, buff) ((buff) + (info).data_offset)
#define mf_add_flags(flags, buff) ((buff)[MF_FLAGS_OFF] |= (flags))
#define mf_remove_flags(flags, buff) ((buff)[MF_FLAGS_OFF] &= ~(flags))
#define mfi_set_diffed_from_version(info, version, buff) (*(uint64_t *)((buff) + MF_DIFFED_FROM_MODEL_VERSION_OFF) = (version))

#define MF_TFLOAT32 0
#define MF_TFLOAT64 1
#define MF_TFLOAT16 2
#define MF_TINT8 3
#define MF_TINT16 4
#define MF_TINT32 5
#define MF_TINT64 6
#define MF_TUINT8 7
#define MF_TUINT16 10
#define MF_TUINT32 11
#define MF_TUINT64 12
#define MF_TSTRING 13

static inline uint8_t MF_SIZE(uint8_t type)
{
    switch (type)
    {
    case MF_TFLOAT32:
        return sizeof(float);
    case MF_TFLOAT64:
        return sizeof(double);
    case MF_TFLOAT16:
        return sizeof(uint16_t);
    case MF_TINT8:
        return sizeof(int8_t);
    case MF_TINT16:
        return sizeof(int16_t);
    case MF_TINT32:
        return sizeof(int32_t);
    case MF_TINT64:
        return sizeof(int64_t);
    case MF_TUINT8:
        return sizeof(uint8_t);
    case MF_TUINT16:
        return sizeof(uint16_t);
    case MF_TUINT32:
        return sizeof(uint32_t);
    case MF_TUINT64:
        return sizeof(uint64_t);
    case MF_TSTRING:
    default:
        return 0;
    }
}

static inline char *MF_TYPE_NAME(uint8_t type)
{
    switch (type)
    {
    case MF_TFLOAT32:
        return "float32";
    case MF_TFLOAT64:
        return "float64";
    case MF_TFLOAT16:
        return "float16";
    case MF_TINT8:
        return "int8";
    case MF_TINT16:
        return "int16";
    case MF_TINT32:
        return "int32";
    case MF_TINT64:
        return "int64";
    case MF_TUINT8:
        return "uint8";
    case MF_TUINT16:
        return "uint16";
    case MF_TUINT32:
        return "uint32";
    case MF_TUINT64:
        return "uint64";
    case MF_TSTRING:
        return "string";
    default:
        return "unknown";
    }
}

#define MF_SFILE_SIZE sizeof(uint64_t)
#define MF_FLAGS_SIZE sizeof(uint8_t)
#define MF_VERSION_SIZE sizeof(uint8_t)
#define MF_METADATA_SIZE sizeof(uint32_t)
#define MF_TENSOR_HEADER_SIZE sizeof(uint32_t)
#define MF_DIFFED_FROM_MODEL_VERSION_SIZE sizeof(uint64_t)

// TENSOR_HEADER_SIZE is included also if not always inclided, for practical reasons
// It is reasonable to assume that the metadata + data is grather then uint32_t size
#define MIN_MF_SIZE (MF_SFILE_SIZE + MF_FLAGS_SIZE + MF_VERSION_SIZE + MF_METADATA_SIZE + MF_TENSOR_HEADER_SIZE + MF_DIFFED_FROM_MODEL_VERSION_SIZE)

#define ERR_MF_MALFORMED -1
#define ERR_MF_UNSUPPORTED_VERSION -2

#define MF_FLAG_COMPRESSED 0x01
#define MF_FLAG_DIFF_FORMAT 0x02
#define MF_FLAG_HEADER_LESS 0x04

#define mfi_is_compressed(info) ((info).flags & MF_FLAG_COMPRESSED)
#define mfi_is_diff_format(info) ((info).flags & MF_FLAG_DIFF_FORMAT)
#define mfi_is_header_less(info) ((info).flags & MF_FLAG_HEADER_LESS)

#define MF_SIZE_OFF 0
#define MF_FLAGS_OFF (MF_SIZE_OFF + MF_SFILE_SIZE)
#define MF_VERSION_OFF (MF_FLAGS_OFF + MF_FLAGS_SIZE)
#define MF_METADATA_SIZE_OFF (MF_VERSION_OFF + MF_VERSION_SIZE)
#define MF_TENSOR_HEADER_SIZE_OFF (MF_METADATA_SIZE_OFF + MF_METADATA_SIZE)
#define MF_DIFFED_FROM_MODEL_VERSION_OFF (MF_TENSOR_HEADER_SIZE_OFF + MF_TENSOR_HEADER_SIZE)

#define MF_VERSION (1)

#define mf_valid_header_size(size) ((size) >= MIN_MF_SIZE)

// ENSURE BUFFER SIZE IS AT LEAST MIN_MF_SIZE BEFORE CALLING THIS FUNCTION
static inline int extract_file_info(model_file_info_t *info, char *buff, size_t size)
{
    if (!mf_valid_header_size(size))
    {
        perror("Buffer too small");
        return ERR_MF_MALFORMED;
    }

    info->file_size = *(uint64_t *)(buff + MF_SIZE_OFF);
    info->flags = *(uint8_t *)(buff + MF_FLAGS_OFF);
    info->version = *(uint8_t *)(buff + MF_VERSION_OFF);
    info->metadata_size = *(uint32_t *)(buff + MF_METADATA_SIZE_OFF);
    info->tensor_header_size = *(uint32_t *)(buff + MF_TENSOR_HEADER_SIZE_OFF);
    info->diffed_from_model_version = *(uint64_t *)(buff + MF_DIFFED_FROM_MODEL_VERSION_OFF);

    info->metadata_offset = MF_DIFFED_FROM_MODEL_VERSION_OFF + MF_DIFFED_FROM_MODEL_VERSION_SIZE;
    info->tensor_header_offset = info->metadata_offset + info->metadata_size;
    info->data_offset = info->tensor_header_offset + info->tensor_header_size;
    info->data_size = info->file_size - info->data_offset;

    return 0;
}

static inline int load_model_info_from_file(int fd, model_file_info_t *info)
{
    char buff[MIN_MF_SIZE];
    ssize_t bytes_read = read(fd, buff, MIN_MF_SIZE);
    if (bytes_read != MIN_MF_SIZE)
    {
        perror("Failed to read file");
        return -1;
    }

    return extract_file_info(info, buff, MIN_MF_SIZE);
}

// this function dynamically allocates memory for the metadata buff, you must free it
static inline char *mfi_load_metadata_from_fd(int fd, model_file_info_t *info)
{
    char *buff = malloc(info->metadata_size);
    if (buff == NULL)
    {
        perror("Failed to allocate memory for metadata");
        return NULL;
    }

    if (lseek(fd, info->metadata_offset, SEEK_SET) == -1)
    {
        perror("Failed to seek to metadata offset");
        free(buff);
        return NULL;
    }

    ssize_t bytes_read = read(fd, buff, info->metadata_size);
    if (bytes_read != info->metadata_size)
    {
        perror("Failed to read metadata");
        free(buff);
        return NULL;
    }

    return buff;
}

static inline void print_model_info(model_file_info_t *info)
{
    printf("Model file size: %ld\n", info->file_size);
    printf("Model flags: %d\n", info->flags);
    printf("Model version: %d\n", info->version);
    printf("Model metadata size: %d\n", info->metadata_size);
    printf("Model tensor header size: %d\n", info->tensor_header_size);
    printf("Model diffed from version: %ld\n", info->diffed_from_model_version);
    printf("Model tensor header offset: %ld\n", info->tensor_header_offset);
    printf("Model metadata offset: %ld\n", info->metadata_offset);
    printf("Model data offset: %ld\n", info->data_offset);
    printf("Model data size: %ld\n", info->data_size);
}

#endif // MODEL_H
#define DECLARE_GLOBALS

#include "globals.h"
#include "aggregator.h"
#include "protocol.h"
#include "fs.h"
#include "socket_server.h"
#include "buffer.h"
#include "event_loop.h"

parallel_socket_server_t server;

static inline int n_metadata(mf_metadata_t *_metadata, size_t buff_size)
{
    size_t n = 0;
    char *next = (char *)_metadata;
    ssize_t remaining = buff_size;

    int i = 0;
    while (remaining > (ssize_t)sizeof(mf_metadata_t))
    {
        i++;
        fflush(stdout);
        uint8_t data_type = ((mf_metadata_t *)next)->data_type;
        uint16_t name_len = ((mf_metadata_t *)next)->name_len;
        uint8_t type_size = 0;

        if (data_type == MF_TSTRING)
        {
            uint32_t str_len = *(uint32_t *)(next + sizeof(mf_metadata_t) + name_len);
            type_size = sizeof(uint32_t) + str_len;
        }
        else
        {
            type_size = MF_SIZE(data_type);
        }

        if (type_size == 0)
        {
            perror("Invalid data type");
            return -1;
        }

        size_t struct_size = sizeof(mf_metadata_t) + type_size + name_len;
        remaining -= struct_size;
        next += struct_size;
        n++;
    }

    if (remaining != 0)
    {
        perror("malformed metadata");
        return -1;
    }

    return n;
}

static inline void *get_meta(char *buff, size_t size, const char *key, uint8_t type)
{

    size_t key_len = strlen(key);
    char *next = buff;
    ssize_t remaining = size;

    int i = 0;
    while (remaining > (ssize_t)sizeof(mf_metadata_t))
    {
        i++;
        uint8_t data_type = ((mf_metadata_t *)next)->data_type;
        uint16_t name_len = ((mf_metadata_t *)next)->name_len;
        uint8_t type_size = 0;

        if (data_type == MF_TSTRING)
        {
            uint32_t str_len = *(uint32_t *)(next + sizeof(mf_metadata_t) + name_len);
            type_size = sizeof(uint32_t) + str_len;
        }
        else
        {
            type_size = MF_SIZE(data_type);
        }

        if (type_size == 0)
        {
            perror("Invalid data type");
            return NULL;
        }

        if (name_len == key_len && data_type == type && strncmp(next + sizeof(mf_metadata_t), key, key_len) == 0)
        {
            return (void *)(next + sizeof(mf_metadata_t) + name_len);
        }

        size_t struct_size = sizeof(mf_metadata_t) + type_size + name_len;
        remaining -= struct_size;
        next += struct_size;
    }

    if (remaining != 0)
    {
        perror("malformed metadata");
        return NULL;
    }

    perror("key not found");
    return NULL;
}

int is_valid_metadata(mf_metadata_t *_metadata, size_t buff_size)
{
    if (buff_size < sizeof(mf_metadata_t))
    {
        return 0;
    }

    return 1;
}

double get_weights_from_metadata(char *buff, size_t size)
{
    uint32_t *dataset_size = (uint32_t *)get_meta(buff, size, "dataset_size", MF_TUINT32);

    if (dataset_size == NULL)
    {
        perror("Failed to get dataset size");
        return -1;
    }

    return (double)*dataset_size;
}

void should_aggregate_models(size_t buffered_updates, struct timespec *last_aggregation, agg_config_t *conf)
{
    if (buffered_updates >= 10)
    {
        conf->type = 1;
        return;
    }

    conf->type = 0;
}

int open_fds(size_t len, model_upd_t *updates[len], int fds[len])
{
    assert(len > 0);
    for (size_t i = 0; i < len; i++)
    {
        fds[i] = open(updates[i]->file_name, O_RDONLY);
        if (fds[i] == -1)
        {
            for (size_t j = 0; j < i; j++)
                close(fds[j]);

            perror("Failed to open model update file");
            return -1;
        }
    }

    return 0;
}

// This function seeks to the data part of the model file, and returns the size of the data
int seek_to_data(size_t n, int fd[n], model_file_info_t model_info[n])
{
    for (size_t i = 0; i < n; i++)
    {
        if (lseek(fd[i], model_info[i].data_offset, SEEK_SET) == -1)
        {
            perror("Failed to seek to data offset");
            goto fail;
        }
    }

    return 0;

fail:
    for (size_t i = 0; i < n; i++)
        close(fd[i]);

    return -1;
}

int load_model_info(size_t n, int fd[n], model_file_info_t model_info[n])
{
    char buff[MIN_MF_SIZE];
    for (size_t i = 0; i < n; i++)
    {
        if (read(fd[i], buff, MIN_MF_SIZE) != MIN_MF_SIZE)
        {
            perror("Failed to read model info");
            goto fail;
        }

        if (extract_file_info(&model_info[i], buff, MIN_MF_SIZE) < 0)
        {
            perror("Failed to extract model info");
            goto fail;
        }
    }

    return 0;

fail:
    for (size_t i = 0; i < n; i++)
        close(fd[i]);

    return -1;
}

int compute_weights(size_t n, int fd[n], model_file_info_t model_info[n], double w[n])
{
    // load metadata
    size_t max_metadata = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (model_info[i].metadata_size > max_metadata)
            max_metadata = model_info[i].metadata_size;
    }

    char metadata_buff[max_metadata];
    for (size_t i = 0; i < n; i++)
    {
        if (read(fd[i], metadata_buff, model_info[i].metadata_size) != model_info[i].metadata_size)
        {
            perror("Failed to read metadata");
            goto fail;
        }

        w[i] = get_weights_from_metadata(metadata_buff, model_info[i].metadata_size);
        if (w[i] < 0)
        {
            perror("Failed to get weights from metadata");
            goto fail;
        }
    }

    return 0;

fail:
    for (size_t i = 0; i < n; i++)
        close(fd[i]);

    return -1;
}

int clone_headers(int fd_source, int fd_dest)
{
    char buff[MIN_MF_SIZE];
    if (read(fd_source, buff, MIN_MF_SIZE) != MIN_MF_SIZE)
    {
        perror("Failed to read model info");
        return -1;
    }

    model_file_info_t model_info;
    if (extract_file_info(&model_info, buff, MIN_MF_SIZE) < 0)
    {
        perror("Failed to extract model info");
        return -1;
    }

    size_t header_size = model_info.file_size - model_info.data_size;

    if (lseek(fd_source, 0, SEEK_SET) == -1)
    {
        perror("Failed to seek to start of file");
        return -1;
    }

    if (lseek(fd_dest, 0, SEEK_SET) == -1)
    {
        perror("Failed to seek to start of file");
        return -1;
    }

    char header[header_size];
    if (read(fd_source, header, header_size) != header_size)
    {
        perror("Failed to read header");
        return -1;
    }

    if (write(fd_dest, header, header_size) != header_size)
    {
        perror("Failed to write header");
        return -1;
    }

    return 0;
}

#define COMPUTE_CHUNK_SIZE 64
int aggregate_models(model_upd_t **updates, size_t len)
{
    set_debug(1);

    int fds[len];
    int ret_code = 0;
    int out_fd = -1;
    model_file_info_t model_info[len];
    double weights[len];
    double avg[COMPUTE_CHUNK_SIZE];
    double data[COMPUTE_CHUNK_SIZE];

    if (open_fds(len, updates, fds) < 0)
    {
        perror("Failed to open model update files");
        return -1;
    }

    debug_print("Opened model update files\n");

    if (load_model_info(len, fds, model_info) < 0)
    {
        perror("Failed to load model info");
        return -1;
    }

    debug_print("Loaded model info\n");

    uint64_t last_global_model_id = model_info[0].diffed_from_model_version;
    uint64_t new_global_model_id = last_global_model_id + 1;
    out_fd = open_model_w(new_global_model_id);
    if (out_fd == -1)
    {
        perror("Failed to open output model file");
        ret_code = -1;
        goto close_all;
    }

    int old_fd = open_model(last_global_model_id);
    if (old_fd == -1)
    {
        perror("Failed to open old model file");
        ret_code = -1;
        goto close_all;
    }

    if (clone_headers(old_fd, out_fd) < 0)
    {
        perror("Failed to clone headers");
        close(old_fd);
        ret_code = -1;
        goto close_all;
    }

    close(old_fd);

    debug_print("Opened output model file\n");

    if (compute_weights(len, fds, model_info, weights) < 0)
    {
        perror("Failed to compute weights");
        return -1;
    }

    debug_print("Computed weights\n");

    if (seek_to_data(len, fds, model_info) < 0)
    {
        perror("Failed to seek to data");
        return -1;
    }

    debug_print("Seeked to data\n");

    // normalize weights
    double max_weight = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (weights[i] > max_weight)
            max_weight = weights[i];
    }

    for (size_t i = 0; i < len; i++)
    {
        weights[i] /= max_weight;
    }

    debug_print("Normalized weights\n");
    debug_print("Max weight: %f\n", max_weight);
    debug_print("Weights: \n");
    for (size_t i = 0; i < len; i++)
    {
        debug_print("\t%f\n", weights[i]);
    }

    size_t computed = 0;
    while (computed < model_info[0].data_size)
    {
        size_t remaining = model_info[0].data_size - computed;
        size_t chunk_size = remaining > COMPUTE_CHUNK_SIZE ? COMPUTE_CHUNK_SIZE : remaining;

        for (int f_idx = 0; f_idx < len; f_idx++)
        {
            if (read(fds[f_idx], data, chunk_size) != chunk_size)
            {
                perror("Failed to read data");
                ret_code = -1;
                goto close_all;
            }

            for (int i = 0; i < chunk_size; i++)
            {
                avg[i] += data[i] * weights[f_idx];
            }
        }

        debug_print("Computed chunk [%zu, %lu]\n", computed + chunk_size, model_info[0].data_size);
        computed += chunk_size;
        if (write(out_fd, avg, chunk_size) != chunk_size)
        {
            perror("Failed to write data");
            ret_code = -1;
            goto close_all;
        }
    }

    ret_code = new_global_model_id;

close_all:
    for (size_t i = 0; i < len; i++)
        close(fds[i]);

    if (out_fd != -1)
        close(out_fd);

    return ret_code;
}

volatile uint8_t stop = 0;
void handle_signal(int signal)
{
    write(STDOUT_FILENO, "Caught signal, exiting\n", 24);
    stop = 1;
    // server.stop_server = 1;
}

void client_cleanup(generic_session_t *session)
{
    session_t *s = (session_t *)session;
    if (s->state == WEIGHT_STREAM)
    {
        client_update_t *update = &s->model_update;
        if (update->fd != -1)
        {
            close(update->fd);
            update->fd = -1;
        }
    }
}

void on_next_iteration()
{
    set_debug(1);
    // debug_print("Pending updates: %ld, Done updates: %ld\n", pending_client_updates_counter, pending_client_updates_done_counter);
}

int main(int argc, char **argv)
{

    // usage ./main <n_threads>
    assert(argc == 2);

    int n_threads = atoi(argv[1]);
    assert(n_threads > 0);

    set_debug(1);
    signal(SIGINT, handle_signal);

    if (queue_model_upd_init(&model_queue, 100) < 0)
    {
        perror("Failed to initialize model queue");
        return -1;
    }

    int fd = open_model(0);
    if (fd == -1)
    {
        perror("Failed to open model file");
        return -1;
    }

    if (load_model_info_from_file(fd, &global_model_info) < 0)
    {
        perror("Failed to load model info from file");
        close(fd);
        return -1;
    }

    char *metadata = mfi_load_metadata_from_fd(fd, &global_model_info);
    if (metadata == NULL)
    {
        perror("Failed to load metadata from file");
        close(fd);
        return -1;
    }

    // loop_metadata(metadata, global_model_info.metadata_size)
    // {
    //     printf("Meta Data type: %s\n", MF_TYPE_NAME(meta->data_type));
    //     printf("Meta Name: %.*s\n", meta->name_len, meta->buff);
    //     if (meta->data_type == MF_TSTRING)
    //     {
    //         uint32_t str_len = *(uint32_t *)(meta->buff + meta->name_len);
    //         printf("Meta String: %.*s\n", str_len, meta->buff + meta->name_len + sizeof(uint32_t));
    //     }
    //     else
    //     {
    //         printf("Meta Value: %f\n", *(float *)(meta->buff + meta->name_len));
    //     }
    // }

    free(metadata);

    char *header_data = (char *)malloc(global_model_info.tensor_header_size);
    if (header_data == NULL)
    {
        perror("Failed to allocate memory for header data");
        close(fd);
        return -1;
    }

    // seek to the header data offset
    if (lseek(fd, global_model_info.tensor_header_offset, SEEK_SET) == -1)
    {
        perror("Failed to seek to header data offset");
        close(fd);
        return -1;
    }

    if (read(fd, header_data, global_model_info.tensor_header_size) != global_model_info.tensor_header_size)
    {
        perror("Failed to read header data");
        close(fd);
        return -1;
    }

    loop_theaders(header_data, global_model_info.tensor_header_size)
    {
        assert(th->data_type == 0);
    }

    // loop_theaders(header_data, global_model_info.tensor_header_size)
    // {
    //     printf("Data type: %s\n", MF_TYPE_NAME(th->data_type));
    //     printf("Dimension: %d\n", th->dim);
    //     // name is not null terminated
    //     printf("Name: %.*s\n", th->name_len, th->data);

    //     printf("Shape: [");
    //     for (int i = 0; i < th->dim; i++)
    //     {
    //         printf("%d", *(uint32_t *)(th->data + th->name_len + i * sizeof(uint32_t)));
    //         if (i < th->dim - 1)
    //             printf(", ");
    //     }

    //     printf("]\n");
    // }

    free(header_data);
    close(fd);

    if (mfi_is_compressed(global_model_info))
    {
        perror("Global model must not be compressed");
        return -1;
    }

    if (mfi_is_diff_format(global_model_info))
    {
        perror("Global model must not be in diff format");
        return -1;
    }

    if (mfi_is_header_less(global_model_info))
    {
        perror("Global model must not be header less");
        return -1;
    }

    if (recover_update_folder(UPDATE_FOLDER) < 0)
    {
        perror("Failed to create update folder");
        return -1;
    }

    socket_server_config_t config = {
        .debug = 1,
        .event_loop_timeout = 1000,
        .max_connections = 100,
        .max_events = 100,
        .max_message_size = 2048,
        .port = PORT,
        .session_size = sizeof(session_t),
        .metrics_file = "metrics.csv",
    };

    if (parallel_socket_server_init(&server, n_threads, config) < 0)
    {
        perror("Failed to initialize parallel socket server");
        return -1;
    }

    // if (socket_server_init(&server, config) < 0)
    // {
    //     perror("Failed to initialize server");
    //     return -1;
    // }

    // if (socket_server_listen(&server) < 0)
    // {
    //     perror("Failed to listen on server");
    //     return -1;
    // }

    // pthread_t queue_thread;
    // if (pthread_create(&queue_thread, NULL, (void *)model_queue_thread, (void *)NULL) < 0)
    // {
    //     perror("Failed to create queue thread");
    //     return -1;
    // }

    // if (socket_server_run(&server) < 0)
    // {
    //     perror("Failed to run server");
    //     return -1;
    // }

    if (parallel_socket_server_run(&server) < 0)
    {
        perror("Failed to run parallel socket server");
        return -1;
    }

    // wait for any signal
    while (!stop)
    {
        sleep(1);
    }

    parallel_socket_server_stop(&server);
    parallel_socket_server_destroy(&server);

    queue_model_upd_close(&model_queue);
    // pthread_join(queue_thread, NULL);
    printf("Server stopped\n");
    fflush(stdout);

    queue_model_upd_destroy(&model_queue);
    return 0;
}

#define DEBUG_PROTOCOL 1

#include "protocol.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

const char *__thread_model_prefix = "model_thread_";
uint64_t thread_model_counter = 0;
#define thread_model_name(model_id) ({                                            \
    char tmp[255];                                                                \
    int n = sprintf(tmp, UPDATE_FOLDER "%s%ld", __thread_model_prefix, model_id); \
    assert(n > 0);                                                                \
    tmp[n] = '\0';                                                                \
    tmp;                                                                          \
})

// 0x03, file_header, Stream: file_data
int handle_send_weight_packet(session_t *session, size_t cursor)
{
    set_debug(DEBUG_PROTOCOL);
    debug_print("Handling send weight packet\n");

    if (session->state != IDLE)
    {
        perror("Invalid state for send weight packet");
        return -1;
    }

    client_update_t *update = &session->model_update;
    assert(update->fd == -1);

    char *buff = buffer_cptr(session->buffer, cursor);
    size_t buff_size = buffer_cremaining(session->buffer, cursor);

    model_file_info_t model_info = {0};
    if (extract_file_info(&model_info, buff, buff_size) < 0)
    {
        perror("Failed to extract model file info");
        return -1;
    }

    uint64_t latest_model = global_model_id;
    if (!mfi_is_diff_format(model_info))
    {
        perror("Model is not in diff format");
        return -1;
    }

    if (model_info.diffed_from_model_version > latest_model)
    {
        debug_print("Model is diffed from a non-existent model\n");
        return -1;
    }

    debug_print("Model diffed from: %ld\n", model_info.diffed_from_model_version);

    size_t model_header_size = model_info.file_size - model_info.data_size;
    if (buff_size != model_header_size)
    {
        debug_print("Model header size: %ld, Buff size: %ld\n", model_header_size, buff_size);
        perror("Invalid model header size");
        return -1;
    }

    mf_metadata_t *metadata = (mf_metadata_t *)(buff + model_info.metadata_offset);
    if (!is_valid_metadata(metadata, model_info.metadata_size))
    {
        perror("Invalid metadata");
        return -1;
    }

    uint64_t model_id = thread_model_counter;
    const char *file_name = thread_model_name(model_id);

    // create a file, signal the os to create a new file of size global_model_size
    int fd = open(file_name, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        perror("Failed to create update file");
        return -1;
    }

    ssize_t written = write(fd, buff, buff_size);
    if (written != buff_size)
    {
        perror("Failed to write model header to file");
        close(fd);
        return -1;
    }

    // TODO: make this optimization
    //  if (ftruncate(fd, global_model_info->) == -1)
    //  {
    //      perror("Failed to set update file size");
    //      close(fd);
    //      return -1;
    //  }

    update->fd = fd;
    session->state = WEIGHT_STREAM;
    update->model_id = model_id;
    update->written = 0;
    update->done = 0;
    update->stream_size = model_info.file_size - buff_size;
    thread_model_counter++;
    return 0;
}

int handle_weight_stream(session_t *session, size_t cursor)
{
    set_debug(DEBUG_PROTOCOL);
    debug_print("Handling weight stream\n");

    assert(session->state == WEIGHT_STREAM);
    debug_print("Session state == WEIGHT_STREAM\n");

    client_update_t *update = &session->model_update;
    assert(update->fd != -1);
    assert(update->done == 0);
    assert(update->model_id != UINT64_MAX);
    assert(update->written < update->stream_size);

    size_t remaining = buffer_cremaining(session->buffer, cursor);
    if (remaining > update->stream_size - update->written)
    {
        debug_print("Remaining: %ld, Stream size: %ld, Written: %ld\n", remaining, update->stream_size, update->written);
        perror("Malformed weight stream packet");
        return -1;
    }

    while (buffer_cremaining(session->buffer, cursor) > 0)
    {
        ssize_t written = write(update->fd, buffer_cptr(session->buffer, cursor), buffer_cremaining(session->buffer, cursor));
        update->written += written;
        cursor += written;
    }

    assert(update->stream_size >= update->written);

    if (update->written == update->stream_size)
    {

        model_upd_t *model_upd = malloc(sizeof(model_upd_t));
        if (model_upd == NULL)
        {
            perror("Failed to allocate memory for model update");
            return -1;
        }

        int n = sprintf(model_upd->file_name, thread_model_name(update->model_id));
        assert(n > 0);

        if (queue_model_upd_enqueue(&model_queue, model_upd) < 0)
        {
            perror("Failed to enqueue model update");
            return -1;
        }

        close(update->fd);
        update->model_id = UINT64_MAX;
        update->done = 1;
        update->fd = -1;
        update->written = 0;
        update->stream_size = UINT64_MAX;
        session->state = IDLE;

        // TODO: Add model to queue

        uint8_t response = 0x01;
        client_clone_and_send((generic_session_t *)session, (void *)&response, sizeof(response));
    }

    debug_print("Written: %ld/%ld\n", update->written, update->stream_size);
    debug_print("Done handling weight stream correctly\n");
    return 0;
}

int handle_auth_packet(session_t *session, size_t cursor)
{
    set_debug(DEBUG_PROTOCOL);
    debug_print("Handling auth packet\n");

    buffer_t *buffer = session->buffer;
    ssize_t res = buffer_read_str(buffer, cursor, session->auth_token, AUTH_TOKEN_SIZE);
    if (res < 0)
    {
        debug_print("Failed to read auth token: %s\n", decode_buffer_error(res));
        return -1;
    }

    debug_print("Authenticating with token: %s\n", session->auth_token);
    session->state = IDLE;

    uint8_t response = 0x01;
    client_clone_and_send((generic_session_t *)session, (void *)&response, sizeof(response));

    // initialize sesssion
    client_update_t *update = &session->model_update;
    update->fd = -1;
    update->done = 0;
    update->model_id = UINT64_MAX;
    update->written = 0;

    return 0;
}

int handle_get_weight_packet(session_t *session, size_t cursor)
{
    set_debug(DEBUG_PROTOCOL);
    buffer_t *buffer = session->buffer;
    if (buffer_cremaining(buffer, cursor) < 2 * sizeof(uint64_t) + sizeof(uint8_t))
    {
        debug_print("Invalid packet size\n");
        return -1;
    }

    uint64_t model_id = buffer_read_net_uint64(buffer, cursor);
    if (model_id == UINT64_MAX)
    {
        model_id = global_model_id;
    }

    int file_fd = open_model(model_id);
    if (file_fd == -1)
    {
        debug_print("Failed to open model file id: %lu, (latest_version) %lu\n", model_id, global_model_id);
        return -1;
    }

    uint64_t local_model_id = buffer_read_net_uint64(buffer, cursor);
    if (local_model_id != UINT64_MAX && local_model_id >= model_id)
    {
        debug_print("Local model id is invalid\n");
        close(file_fd);
        return -1;
    }

    uint8_t flags = buffer_read_uint8(buffer, cursor);
    assert(flags == 0x00); // TODO: add support for compression and headerless models

    char *file;
    size_t file_size;
    if (load_file_fd(file_fd, &file, &file_size) < 0)
    {
        debug_print("Failed to load model file\n");
        close(file_fd);
        return -1;
    }

    close(file_fd);

    model_file_info_t file_info = {0};
    if (extract_file_info(&file_info, file, file_size) < 0)
    {
        debug_print("Failed to extract model info\n");
        free(file);
        return -1;
    }

    if (file_info.file_size != file_size)
    {
        debug_print("Model file size mismatch malformed local file\n");
        free(file);
        return -1;
    }

    if (local_model_id == UINT64_MAX)
    {
        debug_print("1) Sending model data:: len %ld\n", file_size);
        client_pass_ownership_and_send((generic_session_t *)session, file, file_size);
        return 0;
    }

    // diff model
    debug_print("Diffing model\n");
    char *local_file = NULL;
    size_t local_file_size = 0;

    int local_file_fd = open_model(local_model_id);
    if (local_file_fd == -1)
    {
        debug_print("Failed to open model reference\n");
        free(file);
        return -1;
    }

    if (load_file_fd(local_file_fd, &local_file, &local_file_size) < 0)
    {
        debug_print("Failed to load model reference\n");
        free(file);
        close(local_file_fd);
        return -1;
    }

    close(local_file_fd);

    model_file_info_t local_file_info = {0};
    if (extract_file_info(&local_file_info, local_file, local_file_size) < 0)
    {
        debug_print("Failed to load model info\n");
        free(file);
        free(local_file);
        return -1;
    }

    float *model_data = (float *)mfi_get_data_ptr(file_info, file);
    float *model_ref_data = (float *)mfi_get_data_ptr(local_file_info, local_file);

    // TODO add support for differenr tensor headers (types)
    assert(file_info.data_size % sizeof(float) == 0);
    assert(local_file_info.data_size % sizeof(float) == 0);
    assert(file_info.data_size == local_file_info.data_size);

    for (size_t i = 0; i < file_info.data_size / sizeof(float); i++)
    {
        model_data[i] -= model_ref_data[i];
    }

    // set file format to diff
    mf_add_flags(MF_FLAG_DIFF_FORMAT, file);
    mfi_set_diffed_from_version(file_info, local_model_id, file);

    debug_print("2) Sending model data:: len %ld\n", file_size);
    client_pass_ownership_and_send((generic_session_t *)session, file, file_size);
    free(local_file);
    return 0;
}

int handle_get_latest_model_packet(session_t *session, size_t cursor)
{
    set_debug(DEBUG_PROTOCOL);
    buffer_t *buffer = session->buffer;
    if (buffer_cremaining(buffer, cursor) != 0)
    {
        debug_print("Invalid packet size\n");
        return -1;
    }

    uint64_t model_id = global_model_id;
    model_id = htobe64(model_id);

    client_clone_and_send((generic_session_t *)session, (void *)&model_id, sizeof(model_id));
    return 0;
}

int handle_packet_event(generic_session_t *__session)
{
    set_debug(DEBUG_PROTOCOL);
    session_t *session = (session_t *)__session;

    size_t cursor = 0;
    buffer_t *buffer = session->buffer;

    // Ensure we have at least 2 bytes for the packet type
    assert(buffer_has_uint16(buffer, cursor));
    debug_print("Handling packet event, buffer_ptr: %p\n", buffer_ptr(buffer));
    uint16_t packet_type = buffer_read_net_uint16(buffer, cursor);
    debug_print("Packet type: %d\n", packet_type);

    int err_code = 0;
    switch (session->state)
    {
    case AUTHENTICATING:
        debug_print("CASE AUTHENTICATING\n");
        // WHEN NOT AUTHENTICATED ONLY AUTHENTICATION PACKETS ARE ALLOWED (0x01)
        if (packet_type != 0x01)
        {
            debug_print("Invalid packet type\n");
            err_code = -1;
            break;
        }

        err_code = handle_auth_packet(session, cursor);
        break;

    case IDLE:
        debug_print("CASE IDLE\n");
        switch (packet_type)
        {
        case GET_WEIGHT_PACKET:
            err_code = handle_get_weight_packet(session, cursor);
            break;

        case SEND_WEIGHT_PACKET:
            err_code = handle_send_weight_packet(session, cursor);
            break;

        case GET_LATEST_MODEL_PACKET:
            err_code = handle_get_latest_model_packet(session, cursor);
            break;

        default:
            err_code = -1;
            break;
        }
        break;

    case WEIGHT_STREAM:
        debug_print("CASE WEIGHT_STREAM\n");
        switch (packet_type)
        {
        case SEND_WEIGHT_PACKET:
            debug_print("CASE WEIGHT_STREAM: SEND_WEIGHT_PACKET\n");
            err_code = handle_weight_stream(session, cursor);
            break;

        default:
            err_code = -1;
            break;
        }
        break;
    default:
        debug_print("Invalid session state\n");
        err_code = -1;
    }

    debug_print("error code before buffer free %d, ptr: %p\n", err_code, buffer_ptr(buffer));
    session->buffer = NULL;
    // if (buffer != NULL)
    //     free(buffer);

    debug_print("Returning error code: %d\n", err_code);
    return err_code;
}
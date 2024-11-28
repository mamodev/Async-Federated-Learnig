#include "socket_server.h"

#include <signal.h> // sig_atomic_t
#include <stdint.h> // uint8_t
#include <stdlib.h> // malloc, free
#include <string.h> // memset

#include <assert.h>       // assert
#include <errno.h>        // errno
#include <stdio.h>        // perror
#include <sys/resource.h> //getrlimit

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "event_loop.h"
#include "buffer.h"
#include "debug.h"

// END OF LINTER FIX

int client_clone_and_send(generic_session_t *session, void *data, size_t size)
{
    set_debug(session->server->config.debug);

    assert(data != NULL);

    if (!session->write_event_enabled)
    {
        // try to send data immediately
        ssize_t bytes = send(session->fd, data, size, 0);
        if (bytes == size)
        {
            return 0;
        }

        if (bytes > 0 && bytes < size)
        {
            data += bytes;
            size -= bytes;
        }
    }

    debug_print("Cloning and sending data (%p, %zu)\n", data, size);
    void *data_clone = malloc(size);
    if (data_clone == NULL)
    {
        perror("Failed to allocate memory for data clone");
        return -1;
    }

    memcpy(data_clone, data, size);
    int res = __client_pass_ownership_and_send(session, data_clone, size);
    return res;
}

int client_pass_ownership_and_send(generic_session_t *session, void *data, size_t size)
{
    if (!session->write_event_enabled)
    {
        // try to send data immediately
        ssize_t bytes = send(session->fd, data, size, 0);
        if (bytes == size)
        {
            return 0;
        }

        if (bytes > 0 && bytes < size)
        {
            data += bytes;
            size -= bytes;
        }
    }

    return __client_pass_ownership_and_send(session, data, size);
}

int __client_pass_ownership_and_send(generic_session_t *session, void *data, size_t size)
{
    set_debug(session->server->config.debug);
    assert(session->last_request_time != NULL);

    debug_print("Passing ownership and sending data (%p, %zu)\n", data, size);

    struct buffer_list_node_t *node = malloc(sizeof(struct buffer_list_node_t));
    if (node == NULL)
    {
        perror("Failed to allocate memory for buffer list node");
        return -1;
    }

    node->data = data;
    node->size = size;
    node->cursor = 0;
    node->next = NULL;
    node->request_time = session->last_request_time;
    session->last_request_time = NULL;

    if (session->buffer_list == NULL)
        session->buffer_list = node;
    else
        session->buffer_list_end->next = node;

    session->buffer_list_end = node;

    if (session->write_event_enabled == 0)
    {
        size_t next_write_fd = session->server->write_fd_queue_size;

        write_fd_t *write_fd = &(session->server->write_fd_queue[next_write_fd]);
        write_fd->fd = session->fd;
        write_fd->session = session;

        session->server->write_fd_queue_size = next_write_fd + 1;
    }
}

int socket_server_init(socket_server_t *server, socket_server_config_t config)
{
    server->config = config;
    server->stop_server = 0;

    set_debug(config.debug);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("socket");
        return -1;
    }

    fcntl(server_socket, F_SETFL, O_NONBLOCK); // Set non-blocking mode
    if (server_socket < 0)
    {
        perror("fcntl");
        close(server_socket);
        return -1;
    }

    server->fd = server_socket;

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config.port);

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_socket);
        return -1;
    }

    // Optional: Set SO_REUSEPORT (if supported)
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt(SO_REUSEPORT) failed");
        close(server_socket);
        return -1;
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(server_socket);
        perror("bind");
        return -1;
    }

    if (event_loop_init(&server->loop) < 0)
    {
        close(server_socket);
        perror("event_loop_init");
        return -1;
    }

    server->write_fd_queue_size = 0;
    server->write_fd_queue = (write_fd_t *)malloc(sizeof(write_fd_t) * config.max_events);
    if (server->write_fd_queue == NULL)
    {
        close(server_socket);
        perror("Failed to allocate memory for write_fd_queue");
        return -1;
    }

    return 0;
}

void socket_server_destroy(socket_server_t *server)
{
    free(server->write_fd_queue);
    server->write_fd_queue = NULL;
    server->listening = 0;
    server->stop_server = 1;
    event_loop_destroy(&server->loop);
}

int socket_server_listen(socket_server_t *server)
{
    printf("Server isListening: %d %s\n", server->listening, server->config.metrics_file);

    if (server->listening)
    {
        perror("Server is already listening");
        return -1;
    }

    if (listen(server->fd, SOMAXCONN) < 0)
    {
        perror("listen");
        return -1;
    }

    server->listening = 1;
    return 0;
}

int socket_server_stop(socket_server_t *server)
{
    server->stop_server = 1;
    return 0;
}

int socket_server_run(socket_server_t *server)
{
    set_debug(server->config.debug);
    debug_print("Running server\n");

    FILE *metrics_fd = fopen(server->config.metrics_file, "w");
    if (metrics_fd == NULL)
    {
        perror("Failed to open metrics file");
        return -1;
    }

    char metrics_buffer[2048] = {0};
    if (setvbuf(metrics_fd, metrics_buffer, _IOFBF, 2048) != 0)
    {
        perror("Failed to setvbuf for metrics file");
        return -1;
    }

    if (fprintf(metrics_fd, "start_time_sec,start_time_nsec,end_time_sec,end_time_nsec\n") < 0)
    {
        perror("Failed to write header to metrics file");
        return -1;
    }

    fflush(metrics_fd);

    int server_socket = server->fd;
    event_loop_t *loop = &server->loop;
    size_t session_size = server->config.session_size;
    int max_events = server->config.max_events;
    int event_loop_timeout = server->config.event_loop_timeout;

    assert(session_size > 0);
    assert(max_events > 0);

    // ensure max_events is not greater than the maximum number of file descriptors and that is not causing a stack overflow
    assert(max_events <= FD_SETSIZE);
    struct rlimit rlim;
    getrlimit(RLIMIT_STACK, &rlim);
    assert(max_events * session_size < rlim.rlim_cur - 1024 * 1024); // 1MB stack space left

    event_t events[max_events];

    // if (socket_server_listen(server) == -1)
    // {
    //     perror("Failed to listen on server socket");
    //     return -1;
    // }

    generic_session_t server_session = {0};
    if (event_loop_add(loop, server_socket, EVENT_READ, 0, (void *)&server_session) == -1)
    {
        perror("event_loop_add");
        return -1;
    }

    uint8_t error = 0;
    uint32_t wait_counter = 0;
    while (!server->stop_server)
    {
        if (server->write_fd_queue_size != 0)
        {
            debug_print("Processing write_fd_queue (%zu)\n", server->write_fd_queue_size);
            for (size_t i = 0; i < server->write_fd_queue_size; i++)
            {
                write_fd_t sock = server->write_fd_queue[i];
                void *data = (void *)sock.session;
                event_loop_modify(loop, sock.fd, EVENT_WRITE | EVENT_READ, (void *)sock.session);
                // todo handle error
                sock.session->write_event_enabled = 1;
            }

            debug_print("Processed write_fd_queue\n");
            server->write_fd_queue_size = 0;
        }

        int n = event_loop_wait(loop, events, 1024, event_loop_timeout);
        if (n == -1)
        {
            if (errno == EINTR)
            {
                debug_print("Interrupted system call\n");
                continue;
            }

            perror("event_loop_wait");
            error = 1;
            break;
        }

        on_next_iteration();

        for (int i = 0; i < n; i++)
        {
            generic_session_t *session = (generic_session_t *)events[i].data;
            assert(session != NULL);
            if (session->fd == server_socket)
            {
                int client_socket = accept(server_socket, NULL, NULL);
                if (client_socket == -1)
                {
                    printf("Failed to accept client socket: %d\n", errno);
                    // EINVAL

                    perror("accept");
                    exit(1);
                    continue;
                }

                if (fcntl(client_socket, F_SETFL, O_NONBLOCK) == -1)
                {
                    printf("Failed to set client socket to non-blocking mode\n");
                    close(client_socket);
                    continue;
                }

                generic_session_t *c_session = NULL;
                if (event_loop_add(loop, client_socket, EVENT_READ, session_size, (void **)&c_session) < 0)
                {
                    printf("Failed to add client socket to event loop\n");
                    close(client_socket);
                    continue;
                }

                c_session->server = server;
                c_session->write_event_enabled = 0;
                c_session->buffer_list = NULL;
                c_session->buffer_list_end = NULL;
                c_session->last_request_time = NULL;

                debug_print("[%d, %d / %d] (fd %d) Accepted client from: %d\n", wait_counter, i + 1, n, client_socket, server_socket);
                continue;
            }

            if (events[i].events & EVENT_READ)
            {
                // debug_print("[%d,  %d / %d] (fd %d) Read event\n", wait_counter, i + 1, n, session->fd);
                uint8_t should_close = 0;

                generic_session_t *session = (generic_session_t *)events[i].data;
                if (session->last_request_time == NULL)
                {
                    struct timespec *now = malloc(sizeof(struct timespec));
                    if (now == NULL)
                    {
                        perror("Failed to allocate memory for timespec");
                        should_close = 1;
                    }
                    else
                    {
                        clock_gettime(CLOCK_MONOTONIC, now);
                        // todo handle error
                        session->last_request_time = now;
                    }
                }

                // todo handle error
                int res = __handle_write_event(&(server->config), &events[i]);
                if (res == -1)
                {
                    debug_print("Failed to handle write event\n");
                    should_close = 1;
                }
                else if (res == -2)
                {
                    // EOF
                    debug_print("EOF received\n");
                    should_close = 1;
                }

                if (should_close)
                {
                    free(session->last_request_time);
                    client_cleanup(session);
                    event_loop_delete(loop, session->fd);
                    close(session->fd);
                    free(events[i].data);
                }
            }

            if (events[i].events & EVENT_WRITE)
            {
                debug_print("[%d, %d / %d] (fd %d) Write event\n", wait_counter, i + 1, n, session->fd);
                generic_session_t *session = (generic_session_t *)events[i].data;

                debug_print("session->write_event_enabled: %d\n", session->write_event_enabled);

                int err = 0;
                while (session->buffer_list != NULL)
                {
                    struct buffer_list_node_t *node = session->buffer_list;
                    void *data = node->data + node->cursor;
                    size_t remaining = node->size - node->cursor;
                    ssize_t bytes = send(session->fd, data, remaining, 0);
                    if (bytes < 0)
                    {
                        if (errno == ECONNRESET || errno == EPIPE)
                        {
                            err = -2;
                            break;
                        }

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            err = 0;
                            break;
                        }

                        err = -3;
                        break;
                    }

                    if (bytes != remaining)
                    {
                        err = 0;
                        node->cursor += bytes;
                        break;
                    }

                    session->buffer_list = node->next;

                    // print to file (stdout) metrics (time, start_time, end_time);
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);

                    struct timespec *request_time = node->request_time;
                    fprintf(metrics_fd, "%ld,%ld,%ld,%ld\n", request_time->tv_sec, request_time->tv_nsec, now.tv_sec, now.tv_nsec);
                    // printf("Metrics: %ld,%ld,%ld,%ld\n", request_time->tv_sec, request_time->tv_nsec, now.tv_sec, now.tv_nsec);

                    free(node->request_time);
                    free(node->data);
                    free(node);
                }

                if (session->buffer_list == NULL)
                {
                    event_loop_modify(loop, session->fd, EVENT_READ, (void *)session);
                    // todo handle error
                    session->write_event_enabled = 0;
                    session->buffer_list_end = NULL;
                }
            }
        }

        fflush(metrics_fd);
        wait_counter++;
    }

    fflush(metrics_fd);
    fclose(metrics_fd);
    event_loop_destroy(&server->loop);
    close(server_socket);
    server->listening = 0;
    server->fd = -1;
    memset(&server->loop, 0, sizeof(event_loop_t));

    if (error)
        return -1;

    return 0;
}

buffer_t *cast_to_buffer(char *data, size_t raw_capacity, size_t target_capacity)
{
    assert(raw_capacity > sizeof(buffer_t));
    assert(raw_capacity - sizeof(buffer_t) >= target_capacity);

    buffer_t *buffer = (buffer_t *)data;
    buffer->size = 0;
    buffer->capacity = target_capacity;
    buffer->type = 0;

    return buffer;
}

__thread char gbuffer[2048] = {0};
int __handle_write_event(socket_server_config_t *config, event_t *event)
{

    set_debug(config->debug);
    debug_print("Handling write event, data PTR: %p\n", event->data);
    assert(event->data != NULL);

    generic_session_t *session = (generic_session_t *)event->data;
    ssize_t bytes = recv(session->fd, gbuffer, 2048, 0);
    if (bytes < 0)
    {
        if (errno == ECONNRESET || errno == EPIPE)
        {
            debug_print("Client disconnected\n");
            return -2;
        }

        debug_print("Failed to read data\n");
        return -1;
    }

    char ack = 1;
    send(session->fd, &ack, sizeof(ack), 0);
    return 0;

    // if (session->buffer == NULL)
    // {
    //     uint32_t message_size;
    //     ssize_t bytes = recv(session->fd, &message_size, sizeof(message_size), MSG_PEEK);
    //     if (bytes < 0)
    //     {
    //         if (errno == ECONNRESET || errno == EPIPE)
    //         {
    //             debug_print("Client disconnected\n");
    //             return -2;
    //         }

    //         debug_print("Failed to peek message size - %s - fd: (%d) \n", strerror(errno), session->fd);
    //         return -1;
    //     }

    //     if (bytes == 0)
    //     {
    //         return -2;
    //     }

    //     if ((size_t)bytes < sizeof(message_size))
    //     {
    //         debug_print("fd(%d) Not enough data to read message size: MSG_PEEK: %zd, sizeof(message_size): %zu\n", session->fd, bytes, sizeof(message_size));
    //         return 0;
    //     }

    //     if (recv(session->fd, &message_size, sizeof(message_size), 0) != sizeof(message_size))
    //     {
    //         debug_print("Failed to read message size - %s - fd: (%d) \n", strerror(errno), session->fd);
    //         return -1;
    //     }

    //     message_size = ntohl(message_size);
    //     debug_print("Received message size: %d\n", message_size);
    //     if (message_size > config->max_message_size)
    //     {
    //         debug_print("Message size exceeds max_message_size\n");
    //         return -1;
    //     }

    //     session->buffer = allocate_buffer(message_size - sizeof(uint32_t));
    //     // session->buffer = cast_to_buffer(gbuffer, 2048, message_size - sizeof(uint32_t));
    //     // session->buffer = convert_to_buffer(gbuffer, message_size - sizeof(uint32_t));

    //     if (session->buffer == NULL)
    //     {
    //         debug_print("Failed to allocate buffer\n");
    //         return -1;
    //     }

    //     debug_print("buffer size: %zu, capacity: %zu, ptr: %p\n", session->buffer->size, session->buffer->capacity, session->buffer);

    //     return 0;
    // }

    // buffer_t *buffer = session->buffer;

    // ssize_t bytes = recv(session->fd, buffer_next(buffer), buffer_remaining(buffer), 0);
    // if (bytes == 0)
    // {
    //     return -2;
    // }
    // if (bytes == -1)
    // {
    //     if (errno == ECONNRESET || errno == EPIPE)
    //     {
    //         debug_print("Client disconnected\n");
    //         return -2;
    //     }

    //     if (errno == EAGAIN || errno == EWOULDBLOCK)
    //     {
    //         debug_print("No more data to read, bytes: %zd, buff[%zd/%zd]\n", bytes, buffer->size, buffer->capacity);
    //         return 0;
    //     }

    //     debug_print("Failed to read data\n");
    //     return -1;
    // }

    // buffer->size += bytes;
    // debug_print("ptr: %p, data ptr: %p buffer size: %zu, capacity: %zu, buffer_full: %d, buffer_next: %p, buffer_remaining: %zu\n",
    //             buffer,
    //             buffer_ptr(buffer),
    //             buffer->size,
    //             buffer->capacity,
    //             buffer_full(buffer),
    //             buffer_next(buffer),
    //             buffer_remaining(buffer));

    // if (buffer_full(buffer))
    // {
    //     int err = handle_packet_event(session);
    //     return err;
    // }

    // return 0;
}

int parallel_socket_server_init(parallel_socket_server_t *server, int num_threads, socket_server_config_t config)
{
    parallel_socket_server_worker_t *servers = (parallel_socket_server_worker_t *)malloc(sizeof(parallel_socket_server_worker_t) * num_threads);
    if (servers == NULL)
    {
        perror("Failed to allocate memory for parallel socket server workers");
        return -1;
    }

    memset(servers, 0, sizeof(parallel_socket_server_worker_t) * num_threads);

    server->workers = servers;
    server->num_threads = num_threads;
    server->config = config;

    char metrics_file[256] = {0};
    for (int i = 0; i < num_threads; i++)
    {
        snprintf(metrics_file, 256, "%d_%s_", i, config.metrics_file);
        int len = strlen(metrics_file);

        config.metrics_file = (const char *)malloc(len + 1);
        if (config.metrics_file == NULL)
        {
            // todo free memory for previous metrics_file
            free(servers);
            perror("Failed to allocate memory for metrics file");
            return -1;
        }

        memcpy((void *)config.metrics_file, metrics_file, len);
        ((char *)config.metrics_file)[len] = '\0';

        if (socket_server_init(&(servers[i].server), config) < 0)
        {
            free(servers);
            perror("Failed to initialize socket server");
            return -1;
        }
    }

    return 0;
}

void *__ps_worker(void *arg)
{
    parallel_socket_server_worker_t *worker = (parallel_socket_server_worker_t *)arg;
    socket_server_t *server = &(worker->server);

    int res = socket_server_listen(server);
    if (res < 0)
    {
        perror("Failed to listen on server");
        exit(1);
    }

    res = socket_server_run(server);
    if (res < 0)
    {
        perror("Failed to run server");
        exit(1);
    }

    return NULL;
}

int parallel_socket_server_run(parallel_socket_server_t *server)
{
    printf("Running parallel socket server: %d\n", server->num_threads);

    for (int i = 0; i < server->num_threads; i++)
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, __ps_worker, (void *)&server->workers[i]) < 0)
        {
            perror("Failed to create thread");
            // todo add graceful cleanup
            return -1;
        }

        server->workers[i].thread = thread;
    }

    return 0;
}

int parallel_socket_server_stop(parallel_socket_server_t *server)
{
    for (int i = 0; i < server->num_threads; i++)
    {
        socket_server_stop(&server->workers[i].server);
    }

    for (int i = 0; i < server->num_threads; i++)
    {
        pthread_join(server->workers[i].thread, NULL);
    }

    return 0;
}

void parallel_socket_server_destroy(parallel_socket_server_t *server)
{
    for (int i = 0; i < server->num_threads; i++)
    {
        free((void *)server->workers[i].server.config.metrics_file);
        socket_server_destroy(&server->workers[i].server);
    }

    free(server->workers);
    server->workers = NULL;
    server->num_threads = 0;
    server->config.metrics_file = NULL;
}

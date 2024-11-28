#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

#include <pthread.h>

#include <signal.h> // sig_atomic_t
#include <stdint.h> // uint8_t
#include <stdlib.h> // malloc, free
#include <string.h> // memset

#include <assert.h>       // assert
#include <errno.h>        // errno
#include <sys/resource.h> //getrlimit

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "event_loop.h"
#include "buffer.h"
#include "debug.h"

#define MAX_PENDING_WRITES 2048

struct buffer_list_node_t
{
    void *data;
    size_t size;
    size_t cursor;
    struct timespec *request_time;
    struct buffer_list_node_t *next;
};

struct socket_server;

#define GENERIC_SESSION_FIELDS              \
    int fd;                                 \
    buffer_t *buffer;                       \
    struct timespec *last_request_time;     \
    struct socket_server *server;           \
    uint8_t write_event_enabled;            \
    struct buffer_list_node_t *buffer_list; \
    struct buffer_list_node_t *buffer_list_end;

typedef struct
{
    GENERIC_SESSION_FIELDS
} generic_session_t;

typedef struct
{
    int fd;
    generic_session_t *session;
} write_fd_t;

typedef struct
{
    uint16_t port;
    int max_connections;
    size_t max_message_size;
    int max_events;
    int event_loop_timeout;
    uint8_t debug;
    size_t session_size;
    const char *metrics_file;
} socket_server_config_t;

typedef struct socket_server
{
    int fd;
    socket_server_config_t config;
    event_loop_t loop;
    volatile sig_atomic_t stop_server;
    volatile sig_atomic_t listening;

    size_t write_fd_queue_size;
    write_fd_t *write_fd_queue;
} socket_server_t;

typedef struct
{
    socket_server_t server;
    pthread_t thread;
} parallel_socket_server_worker_t;

typedef struct
{
    int num_threads;
    socket_server_config_t config;
    parallel_socket_server_worker_t *workers;
} parallel_socket_server_t;

int parallel_socket_server_init(parallel_socket_server_t *server, int num_threads, socket_server_config_t config);
int parallel_socket_server_run(parallel_socket_server_t *server);
int parallel_socket_server_stop(parallel_socket_server_t *server);
void parallel_socket_server_destroy(parallel_socket_server_t *server);

int handle_packet_event(generic_session_t *session);
void client_cleanup(generic_session_t *session);
void on_next_iteration();

int client_clone_and_send(generic_session_t *session, void *data, size_t size);
int client_pass_ownership_and_send(generic_session_t *session, void *data, size_t size);

int __handle_write_event(socket_server_config_t *config, event_t *event);
int socket_server_init(socket_server_t *server, socket_server_config_t config);
int socket_server_run(socket_server_t *server);
int socket_server_stop(socket_server_t *server);
int socket_server_listen(socket_server_t *server);
void socket_server_destroy(socket_server_t *server);

#endif // SOCKET_SERVER_H
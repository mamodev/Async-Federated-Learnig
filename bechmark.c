// This program is used to bechmark the performance of the C server implementation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

// include atomic types
#include <stdatomic.h>

#include <time.h>

#define assert(cond, msg, ...)      \
    if (!(cond))                    \
    {                               \
        printf(msg, ##__VA_ARGS__); \
        fflush(stdout);             \
        exit(-1);                   \
    }

#define assert_break(fd, cond, msg, ...) \
    if (!(cond))                         \
    {                                    \
        if (fd > 0)                      \
        {                                \
            close(fd);                   \
        }                                \
        printf(msg, ##__VA_ARGS__);      \
        perror("");                      \
        fflush(stdout);                  \
        break;                           \
    }

int send_all(int fd, void *data, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t sent = send(fd, data + total, len - total, 0);
        if (sent < 0)
        {
            return -1;
        }
        total += sent;
    }
    return 0;
}

typedef struct
{
    int port;
    const char *host;
    volatile float req_per_sec;
    volatile uint64_t total_req;
    volatile sig_atomic_t done;
    volatile uint8_t running;
} worker_args_t;

void *worker(void *args)
{
    char res_buffer[2048];

    worker_args_t *wargs = (worker_args_t *)args;
    int port = wargs->port;
    const char *host = wargs->host;

    // AUTH packet
    const char *auth_token = "auth_token";
    int auth_token_len = strlen(auth_token);
    int auth_packet_size = 4 + 2 + 4 + auth_token_len;
    char auth_packet[auth_packet_size];

    *(uint32_t *)(&auth_packet[0]) = htonl(auth_packet_size);
    *(uint16_t *)(&auth_packet[4]) = htons(1);
    *(uint32_t *)(&auth_packet[6]) = htonl(auth_token_len);
    memcpy(&auth_packet[10], auth_token, auth_token_len);

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(host);

    struct timespec start_time = {0, 0};
    struct timespec current_time = {0, 0};
    int compute_metrics_every = 40;
    uint64_t latest_index = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    int i = 0;
    while (wargs->running)
    {
        i++;
        // connect to the server
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        assert_break(sockfd, sockfd > 0, "Failed to create socket\n");

        int enable = 1;
        int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        assert_break(sockfd, ret == 0, "Failed to set socket options\n");

        ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
        assert_break(sockfd, ret == 0, "Failed to set socket options\n");

        struct linger linger_option = {1, 0};
        ret = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &linger_option, sizeof(linger_option));
        assert_break(sockfd, ret == 0, "Failed to set socket options\n");

        ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        assert_break(sockfd, ret == 0, "Failed to connect to server\n");

        int ires = send_all(sockfd, auth_packet, auth_packet_size);
        assert_break(sockfd, ires == 0, "Failed to send auth packet\n");

        size_t sres = recv(sockfd, res_buffer, 1, 0);
        assert_break(sockfd, sres == 1, "Failed to receive response\n");
        assert_break(sockfd, res_buffer[0] == 0x01, "Invalid response\n");
        close(sockfd);

        if (i % compute_metrics_every == 0)
        {

            uint64_t req_diff = i - latest_index;

            clock_gettime(CLOCK_MONOTONIC, &current_time);
            float elapsed_time_sec = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            if (elapsed_time_sec < 1)
                continue;

            // printf("requests from last time: %lu, elapsed time: %.2f\n", req_diff, elapsed_time_sec);
            wargs->req_per_sec = req_diff / elapsed_time_sec;
            wargs->total_req = i;

            start_time = current_time;
            latest_index = i;
            // printf("Requests per second: %.2f, elapsed time: %.2f, total requests: %d\n", req_per_sec, elapsed_time_sec, i);
        }
    }

    wargs->done = 1;

    return NULL;
}

int main(int argc, char *argv[])
{
    int time = 1000;
    int port = 0;
    int threads = 0;

    assert(argc == 5, "Usage: %s <host> <port> <time> <threads>\n", argv[0]);

    const char *host = argv[1];
    port = atoi(argv[2]);
    time = atoi(argv[3]);
    threads = atoi(argv[4]);

    assert(host != NULL, "Invalid host\n");
    assert(strlen(host) > 0, "Invalid host\n");
    assert(threads > 0, "Invalid threads\n");
    assert(port > 0, "Invalid port\n");
    assert(time > 0, "Invalid time\n");

    worker_args_t wargs[threads];
    pthread_t tids[threads];

    for (int i = 0; i < threads; i++)
    {
        wargs[i].port = port;
        wargs[i].host = host;
        wargs[i].req_per_sec = 0;
        wargs[i].total_req = 0;
        wargs[i].done = 0;
        wargs[i].running = 1;

        pthread_create(&tids[i], NULL, worker, &wargs[i]);
    }

    uint64_t t = 0;
    while (t < time)
    {
        sleep(1);
        int done = 0;
        for (int i = 0; i < threads; i++)
        {
            done += wargs[i].done;
        }

        if (done == threads)
        {
            printf("%d threads done\n", threads);
            break;
        }

        float total_req_per_sec = 0;
        uint64_t total_req = 0;
        for (int i = 0; i < threads; i++)
        {
            total_req_per_sec += wargs[i].req_per_sec;
            total_req += wargs[i].total_req;
        }

        printf("[%lu] Total requests per second: %.2f, total requests: %lu\n", t, total_req_per_sec, total_req);
        fflush(stdout);
        t++;
    }

    for (int i = 0; i < threads; i++)
    {
        pthread_join(tids[i], NULL);
    }

    printf("Done\n");

    return 0;
}

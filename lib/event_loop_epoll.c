// This file implements the event loop using the epoll API.

#include <sys/epoll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <assert.h>
#include "event_loop.h"

int event_loop_init(event_loop_t *loop)
{
    int ret = 0;

    loop->fd = epoll_create1(0);

    if (loop->fd == -1)
    {
        ret = -1;
    }

    return ret;
}

int event_loop_destroy(event_loop_t *loop)
{
    close(loop->fd);
    loop->fd = -1;
    return 0;
}

int event_loop_add(event_loop_t *loop, int fd, event_type_t events, size_t data_size, void **data_ptr)
{
    int ret = 0;
    assert(data_ptr != NULL);

    struct epoll_event ev = {0};
    ev.events = 0;
    ev.data.ptr = NULL;
    void *dataPtr = NULL;

    if (data_size != 0)
    {
        dataPtr = malloc(data_size);
        if (dataPtr == NULL)
        {
            ret = -1;
            goto end;
        }

        memset(dataPtr, 0, data_size);
        *data_ptr = dataPtr;
    }
    else
    {
        dataPtr = data_ptr;
    }

    ev.data.ptr = dataPtr;
    *(int *)dataPtr = fd;

    if (events & EVENT_READ)
    {
        ev.events |= EPOLLIN;
    }
    if (events & EVENT_WRITE)
    {
        ev.events |= EPOLLOUT;
    }

    // printf("calling epoll_ctl(%d, EPOLL_CTL_ADD, %d, %p)\n", loop->fd, fd, &ev);
    if (epoll_ctl(loop->fd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        ret = -1;
        goto end;
    }

end:
    return ret;
}

int event_loop_modify(event_loop_t *loop, int fd, event_type_t events, void *data)
{
    int ret = 0;
    struct epoll_event ev = {0};
    ev.events = 0;
    ev.data.ptr = data;

    if (events & EVENT_READ)
    {
        ev.events |= EPOLLIN;
    }
    if (events & EVENT_WRITE)
    {
        ev.events |= EPOLLOUT;
    }

    if (epoll_ctl(loop->fd, EPOLL_CTL_MOD, fd, &ev) < 0)
    {
        ret = -1;
    }

    return ret;
}

int event_loop_delete(event_loop_t *loop, int fd)
{
    int err = epoll_ctl(loop->fd, EPOLL_CTL_DEL, fd, NULL);
    return err;
}

struct dummy_data
{
    int fd;
};

int event_loop_wait(event_loop_t *loop, event_t *events, int max_events, int timeout)
{
    int ret = 0;
    struct epoll_event epoll_events[max_events];

    // timeout is in milliseconds
    int n = epoll_wait(loop->fd, epoll_events, max_events, timeout);
    if (n == -1)
    {
        ret = -1;
        goto end;
    }

    // printf("Result of epoll_wait: %d\n", n);
    for (int i = 0; i < n; i++)
    {
        // printf("events[%d].data.fd = %d\n", i, epoll_events[i].data.fd);
        // printf("events[%d].data.ptr = %p\n", i, epoll_events[i].data.ptr);
        // printf("events[%d].events = %d\n", i, epoll_events[i].events);
        events[i].data = epoll_events[i].data.ptr;
        events[i].events = 0;
        events[i].flags = 0;

        if (epoll_events[i].events & EPOLLERR)
        {
            events[i].flags |= EVENT_ERROR;
        }

        if (epoll_events[i].events & EPOLLHUP)
        {
            events[i].flags |= EVENT_EOF;
        }

        if (epoll_events[i].events & EPOLLIN)
        {
            events[i].events |= EVENT_READ;
        }
        if (epoll_events[i].events & EPOLLOUT)
        {
            events[i].events |= EVENT_WRITE;
        }

        events[i].flags = 0;
    }

    ret = n;

end:
    return ret;
}
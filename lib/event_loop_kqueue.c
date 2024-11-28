#include "event_loop.h"
#include <sys/event.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int event_loop_init(event_loop_t *loop)
{
  loop->fd = kqueue();
  return loop->fd;
}

int event_loop_destroy(event_loop_t *loop)
{
  close(loop->fd);
  return 0;
}

int event_loop_add(event_loop_t *loop, int fd, event_type_t events, size_t data_size, void **data_ptr)
{
  struct kevent changes[2];
  int n = 0;

  void *dataPtr = NULL;
  if (data_size > 0)
  {
    dataPtr = malloc(data_size);
    if (dataPtr == NULL)
      return -1;

    memset(dataPtr, 0, data_size);
  }

  if (events & EVENT_READ)
  {
    EV_SET(&changes[n++], fd, EVFILT_READ, EV_ADD, 0, 0, dataPtr);
  }
  if (events & EVENT_WRITE)
  {
    EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD, 0, 0, dataPtr);
  }

  if (kevent(loop->fd, changes, n, NULL, 0, NULL) < 0)
  {
    free(dataPtr);
    return -1;
  }

  if (data_ptr != NULL)
    *data_ptr = dataPtr;

  return 0;
}

int event_loop_delete(event_loop_t *loop, int fd)
{
  int n = 0;
  struct kevent changes[2];

  EV_SET(&changes[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

  return kevent(loop->fd, changes, n, NULL, 0, NULL);
}

int event_loop_wait(event_loop_t *loop, event_t *events, int max_events, int timeout)
{
  struct kevent kevents[max_events];
  struct timespec ts = {0};
  struct timespec *pts = NULL;

  if (timeout >= 0)
  {
    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (timeout % 1000) * 1000000;
    pts = &ts;
  }

  int n = kevent(loop->fd, NULL, 0, kevents, max_events, pts);
  for (int i = 0; i < n; i++)
  {
    events[i].fd = (int)(intptr_t)kevents[i].ident;
    events[i].events = 0;
    events[i].data = (void **)kevents[i].udata;
    events[i].flags = 0;

    if (kevents[i].flags & EV_ERROR)
      events[i].flags |= EVENT_ERROR;

    if (kevents[i].flags & EV_EOF)
      events[i].flags |= EVENT_EOF;

    if (kevents[i].filter == EVFILT_READ)
      events[i].events |= EVENT_READ;
    if (kevents[i].filter == EVFILT_WRITE)
      events[i].events |= EVENT_WRITE;
  }
  return n;
}
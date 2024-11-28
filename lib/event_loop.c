#if defined(__linux__)
#include "event_loop_epoll.c"
#elif defined(__APPLE__)
#include "event_loop_kqueue.c"
#else
#error "Unsupported platform"
#endif
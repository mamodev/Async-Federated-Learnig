#ifndef DEBUG_H
#define DEBUG_H

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#define printwithtime(fmt, ...)                                     \
    {                                                               \
        struct timespec ts;                                         \
        assert(clock_gettime(CLOCK_REALTIME, &ts) == 0);            \
        fprintf(stdout, "[%09ld] " fmt, ts.tv_nsec, ##__VA_ARGS__); \
        fflush(stdout);                                             \
    }

// #define set_debug(active)
//   uint8_t debug __attribute__((__cleanup__(db_clean_up))) = active

#define set_debug(active)   \
    uint8_t debug = active; \
    (void)debug;

#ifdef DEBUG
#define debug_print(fmt, ...) __debug_print(fmt, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) \
    {                         \
    }
#endif

#define __debug_print(fmt, ...)                                                                        \
    {                                                                                                  \
        if (debug)                                                                                     \
        {                                                                                              \
            struct timespec ts;                                                                        \
            assert(clock_gettime(CLOCK_REALTIME, &ts) == 0);                                           \
            if (debug == 1)                                                                            \
            {                                                                                          \
                fprintf(stderr, "(%s:%d) %s\n", __FILE__, __LINE__, __func__);                         \
            }                                                                                          \
            fprintf(stderr, "\t (%s:%d) [%09ld] " fmt, __FILE__, __LINE__, ts.tv_nsec, ##__VA_ARGS__); \
            fflush(stderr);                                                                            \
            debug++;                                                                                   \
        }                                                                                              \
    }

#endif // DEBUG_H
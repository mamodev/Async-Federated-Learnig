#ifndef LOCK_H
#define LOCK_H

#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

typedef struct
{
  atomic_flag lock; // Atomic flag for spinlock
} spinlock_t;

static inline void spinlock_init(spinlock_t *s)
{
  atomic_flag_clear(&s->lock);
}

static inline void spinlock_lock(spinlock_t *s)
{
  while (atomic_flag_test_and_set(&s->lock))
  {
    // Spin-wait (busy-wait)
  }
}

static inline void spinlock_unlock(spinlock_t *s)
{
  atomic_flag_clear(&s->lock);
}

#endif // LOCK_H
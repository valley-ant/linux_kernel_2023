#pragma once

#if USE_PTHREADS

#include <pthread.h>

#define mutex_t pthread_mutex_t
#define mutex_init(m, t) pthread_mutex_init(m, t)
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define mutex_trylock(m) (!pthread_mutex_trylock(m))
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock

#else

#include <stdbool.h>
#include <stdint.h>
#include "atomic.h"
#include "futex.h"
#include "spinlock.h"

typedef int mutype_t;
#define gettid() syscall(SYS_gettid)

/* futex(2): use atomic compare-and-change.
 * memory model failed for strong?
 */
#define cmpxchg(obj, expect, desired) \
    compare_exchange_strong(obj, expect, desired, relaxed, relaxed)

#define MUTEX_SPINS 		128
#define FUTEX_IDLE_ID 		0

enum {
	PRIO_DEFAULT = 0,
	PRIO_INHERENT,
	PRIO_NONE
};

typedef struct {
    atomic int state;
	mutype_t type;
	pid_t owner;
} mutex_t;

enum {
    MUTEX_LOCKED = 1 << 0,
    MUTEX_SLEEPING = 1 << 1,
};

#define MUTEX_INITIALIZER 							\
    {                     							\
        .state = 0, .type = 0,        				\
    }

static inline void mutex_init(mutex_t *mutex, mutype_t type)
{
    atomic_init(&mutex->state, 0);
	mutex->type = type;
	mutex->owner = 0;	// no one take the mutex, default 0.
}

static bool mutex_trylock_pi(mutex_t *mutex)
{
	pid_t expect = FUTEX_IDLE_ID;
	pid_t tid;

	tid = gettid();

	//mutex->owner = gettid();
	
	/* 0: mutex is idle, exchange it and return true. */
	/* if state==expect, then set state to tid. (see futex(2) man page */
	if (cmpxchg(&mutex->state, &expect, tid))
		return true;

	/* lock is taken, return false.. */
    thread_fence(&mutex->state, acquire);
    return false;
}

static inline void mutex_lock_pi(mutex_t *mutex)
{

    for (int i = 0; i < MUTEX_SPINS; ++i) {
        if (mutex_trylock_pi(mutex))
            return;
        spin_hint();
    }

	/* can not take the lock... */
 	/* FUTEX_LOCK_PI to sleep */
	futex_lock_pi(&mutex->state,0 , NULL);

    thread_fence(&mutex->state, acquire);
}

static inline void mutex_unlock_pi(mutex_t *mutex)
{
	pid_t tid = gettid();
	
	/* If mutex is aquired, the tid will be the owner thread */
	if (cmpxchg(&mutex->state, &tid, FUTEX_IDLE_ID))
		return;

	futex_unlock_pi(&mutex->state);
}

static bool mutex_trylock_default(mutex_t *mutex)
{
    int state = load(&mutex->state, relaxed);
    if (state & MUTEX_LOCKED)
        return false;

    state = fetch_or(&mutex->state, MUTEX_LOCKED, relaxed);
    if (state & MUTEX_LOCKED)
        return false;

    thread_fence(&mutex->state, acquire);
    return true;
}

static inline void mutex_lock_default(mutex_t *mutex)
{
    for (int i = 0; i < MUTEX_SPINS; ++i) {
        if (mutex_trylock_default(mutex))
            return;
        spin_hint();
    }

    int state = exchange(&mutex->state, MUTEX_LOCKED | MUTEX_SLEEPING, relaxed);

    while (state & MUTEX_LOCKED) {
        futex_wait(&mutex->state, MUTEX_LOCKED | MUTEX_SLEEPING);
        state = exchange(&mutex->state, MUTEX_LOCKED | MUTEX_SLEEPING, relaxed);
    }

    thread_fence(&mutex->state, acquire);
}

static inline void mutex_unlock_default(mutex_t *mutex)
{
    int state = exchange(&mutex->state, 0, release);
    if (state & MUTEX_SLEEPING)
        futex_wake(&mutex->state, 1);
}

static bool mutex_trylock(mutex_t *mutex)
{
	return true;
}

static inline void mutex_lock(mutex_t *mutex)
{
#if 1
	switch (mutex->type) {
	case PRIO_DEFAULT:
		mutex_lock_default(mutex);
		break;
	case PRIO_INHERENT:
		mutex_lock_pi(mutex);
		break;

	default:
		break;
	}
#endif
}

static inline void mutex_unlock(mutex_t *mutex)
{
#if 1
    switch (mutex->type) {
	case PRIO_DEFAULT:
		mutex_unlock_default(mutex);
		break;
	case PRIO_INHERENT:
		mutex_unlock_pi(mutex);
		break;

	default:
		break;
	}
#endif
}


#endif

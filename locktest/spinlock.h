#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>

#include "def.h"

#define LOCK_ALGORITHM "SPINLOCK"
#define NEED_CONTEXT 0
#define SUPPORT_WAITING 0

typedef struct spinlock_mutex {
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    volatile int spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
} spinlock_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t spinlock_cond_t;
typedef void *spinlock_context_t; // Unused, take the less space as possible

spinlock_mutex_t *spinlock_mutex_create(const pthread_mutexattr_t *attr);
int spinlock_mutex_lock(spinlock_mutex_t *impl, spinlock_context_t *me);
int spinlock_mutex_trylock(spinlock_mutex_t *impl, spinlock_context_t *me);
void spinlock_mutex_unlock(spinlock_mutex_t *impl, spinlock_context_t *me);
int spinlock_mutex_destroy(spinlock_mutex_t *lock);
int spinlock_cond_init(spinlock_cond_t *cond, const pthread_condattr_t *attr);
int spinlock_cond_timedwait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                            spinlock_context_t *me, const struct timespec *ts);
int spinlock_cond_wait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                       spinlock_context_t *me);
int spinlock_cond_signal(spinlock_cond_t *cond);
int spinlock_cond_broadcast(spinlock_cond_t *cond);
int spinlock_cond_destroy(spinlock_cond_t *cond);
void spinlock_thread_start(void);
void spinlock_thread_exit(void);
void spinlock_application_init(void);
void spinlock_application_exit(void);

typedef spinlock_mutex_t lock_mutex_t;
typedef spinlock_context_t lock_context_t;
typedef spinlock_cond_t lock_cond_t;

#define lock_mutex_create spinlock_mutex_create
#define lock_mutex_lock spinlock_mutex_lock
#define lock_mutex_trylock spinlock_mutex_trylock
#define lock_mutex_unlock spinlock_mutex_unlock
#define lock_mutex_destroy spinlock_mutex_destroy
#define lock_cond_init spinlock_cond_init
#define lock_cond_timedwait spinlock_cond_timedwait
#define lock_cond_wait spinlock_cond_wait
#define lock_cond_signal spinlock_cond_signal
#define lock_cond_broadcast spinlock_cond_broadcast
#define lock_cond_destroy spinlock_cond_destroy
#define lock_thread_start spinlock_thread_start
#define lock_thread_exit spinlock_thread_exit
#define lock_application_init spinlock_application_init
#define lock_application_exit spinlock_application_exit
#define lock_init_context spinlock_init_context

#endif // __SPINLOCK_H__


extern __thread unsigned int cur_thread_id;

spinlock_mutex_t *spinlock_mutex_create(const pthread_mutexattr_t *attr) {
    spinlock_mutex_t *impl =
            (spinlock_mutex_t *)alloc_cache_align(sizeof(spinlock_mutex_t));
    impl->spin_lock = UNLOCKED;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, attr);
#endif

    return impl;
}

int spinlock_mutex_lock(spinlock_mutex_t *impl,
                        spinlock_context_t *UNUSED(me)) {
while (__sync_val_compare_and_swap(&impl->spin_lock, UNLOCKED, LOCKED) ==
LOCKED)
CPU_PAUSE();
#if COND_VAR
int ret = REAL(pthread_mutex_lock)(&impl->posix_lock);

    assert(ret == 0);
#endif
return 0;
}

int spinlock_mutex_trylock(spinlock_mutex_t *impl,
                           spinlock_context_t *UNUSED(me)) {
if (__sync_val_compare_and_swap(&impl->spin_lock, UNLOCKED, LOCKED) ==
UNLOCKED) {
#if COND_VAR
int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            CPU_PAUSE();

        assert(ret == 0);
#endif
return 0;
}

return EBUSY;
}

void __spinlock_mutex_unlock(spinlock_mutex_t *impl) {
    int old = __sync_val_compare_and_swap(&impl->spin_lock, LOCKED, UNLOCKED);
    assert(old == LOCKED);
}

void spinlock_mutex_unlock(spinlock_mutex_t *impl,
                           spinlock_context_t *UNUSED(me)) {
#if COND_VAR
int ret = REAL(pthread_mutex_unlock)(&impl->posix_lock);
    assert(ret == 0);
#endif
__spinlock_mutex_unlock(impl);
}

int spinlock_mutex_destroy(spinlock_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int spinlock_cond_init(spinlock_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_timedwait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                            spinlock_context_t *me, const struct timespec *ts) {
#if COND_VAR
    int res;

    __spinlock_mutex_unlock(lock);

    if (ts)
        res = REAL(pthread_cond_timedwait)(cond, &lock->posix_lock, ts);
    else
        res = REAL(pthread_cond_wait)(cond, &lock->posix_lock);

    if (res != 0 && res != ETIMEDOUT) {
        fprintf(stderr, "Error on cond_{timed,}wait %d\n", res);
        assert(0);
    }

    int ret = 0;
    if ((ret = REAL(pthread_mutex_unlock)(&lock->posix_lock)) != 0) {
        fprintf(stderr, "Error on mutex_unlock %d\n", ret == EPERM);
        assert(0);
    }

    spinlock_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_wait(spinlock_cond_t *cond, spinlock_mutex_t *lock,
                       spinlock_context_t *me) {
    return spinlock_cond_timedwait(cond, lock, me, 0);
}

int spinlock_cond_signal(spinlock_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_broadcast(spinlock_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int spinlock_cond_destroy(spinlock_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void spinlock_thread_start(void) {
}

void spinlock_thread_exit(void) {
}

void spinlock_application_init(void) {
}

void spinlock_application_exit(void) {
}

void spinlock_init_context(lock_mutex_t *UNUSED(impl),
        lock_context_t *UNUSED(context),
int UNUSED(number)) {
}

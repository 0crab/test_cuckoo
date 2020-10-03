
#ifndef __CLH_H__
#define __CLH_H__

#include "def.h"
#define LOCK_ALGORITHM "CLH"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1

// CLH variant with standard interface from M.L.Scott

typedef struct clh_node {
    volatile int spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
    char __pad[pad_to_cache_line(sizeof(int))];
} clh_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct clh_mutex {
    clh_node_t dummy __attribute__((aligned(L_CACHE_LINE_SIZE)));
    // clh_node_t is cache aligned
    clh_node_t *volatile head;
    clh_node_t *volatile tail;
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(clh_node_t *) + sizeof(clh_node_t *) +
                                 sizeof(pthread_mutex_t))];
#else
    char __pad[pad_to_cache_line(sizeof(clh_node_t *) + sizeof(clh_node_t *))];
#endif
} clh_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct clh_context {
    clh_node_t initial;
    clh_node_t *volatile current __attribute__((aligned(L_CACHE_LINE_SIZE)));
    char __pad[pad_to_cache_line(sizeof(clh_node_t) + sizeof(clh_node_t *))];
} clh_context_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t clh_cond_t;

clh_mutex_t *clh_mutex_create(const pthread_mutexattr_t *attr);
int clh_mutex_lock(clh_mutex_t *impl, clh_context_t *me);
int clh_mutex_trylock(clh_mutex_t *impl, clh_context_t *me);
void clh_mutex_unlock(clh_mutex_t *impl, clh_context_t *me);
int clh_mutex_destroy(clh_mutex_t *lock);
int clh_cond_init(clh_cond_t *cond, const pthread_condattr_t *attr);
int clh_cond_timedwait(clh_cond_t *cond, clh_mutex_t *lock, clh_context_t *me,
                       const struct timespec *ts);
int clh_cond_wait(clh_cond_t *cond, clh_mutex_t *lock, clh_context_t *me);
int clh_cond_signal(clh_cond_t *cond);
int clh_cond_broadcast(clh_cond_t *cond);
int clh_cond_destroy(clh_cond_t *cond);
void clh_thread_start(void);
void clh_thread_exit(void);
void clh_application_init(void);
void clh_application_exit(void);
void clh_init_context(clh_mutex_t *impl, clh_context_t *context, int number);

typedef clh_mutex_t lock_mutex_t;
typedef clh_context_t lock_context_t;
typedef clh_cond_t lock_cond_t;

#define lock_mutex_create clh_mutex_create
#define lock_mutex_lock clh_mutex_lock
#define lock_mutex_trylock clh_mutex_trylock
#define lock_mutex_unlock clh_mutex_unlock
#define lock_mutex_destroy clh_mutex_destroy
#define lock_cond_init clh_cond_init
#define lock_cond_timedwait clh_cond_timedwait
#define lock_cond_wait clh_cond_wait
#define lock_cond_signal clh_cond_signal
#define lock_cond_broadcast clh_cond_broadcast
#define lock_cond_destroy clh_cond_destroy
#define lock_thread_start clh_thread_start
#define lock_thread_exit clh_thread_exit
#define lock_application_init clh_application_init
#define lock_application_exit clh_application_exit
#define lock_init_context clh_init_context



#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>

extern __thread unsigned int cur_thread_id;

clh_mutex_t *clh_mutex_create(const pthread_mutexattr_t *attr) {
    clh_mutex_t *impl = (clh_mutex_t *)alloc_cache_align(sizeof(clh_mutex_t));
    // At the beginning, all threads need a first context.
    // This context is embedded inside the lock itself (dummy)
    impl->dummy.spin = UNLOCKED;
    impl->head       = NULL;
    impl->tail       = &impl->dummy;

#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    return impl;
}

void clh_init_context(lock_mutex_t *impl, lock_context_t *ctx, int number) {
    int i;

    // At the beginning, all threads use the node embedded in their own context
    for (i = 0; i < number; i++) {
        ctx[i].initial.spin = UNLOCKED;
        ctx[i].current      = &ctx[i].initial;
    }
}

static int __clh_mutex_lock(clh_mutex_t *impl, clh_context_t *me) {
    //std::cout<<cur_thread_id<<"lock-start"<<std::endl;
    clh_node_t *p = me->current;
    p->spin       = LOCKED;

    // The thread enqueues
    clh_node_t *pred = (clh_node_t*)xchg_64((void *)&impl->tail, (void *)p);
    // If the previous thread was locked, we wait on its context
    waiting_policy_sleep(&pred->spin);
    impl->head = p;
    COMPILER_BARRIER();
    // We take the context of the previous thread
    me->current = pred;
    //std::cout<<cur_thread_id<<"lock-finish"<<std::endl;
    return 0;
}

int clh_mutex_lock(clh_mutex_t *impl, clh_context_t *me) {
    int ret = __clh_mutex_lock(impl, me);
    assert(ret == 0);
#if COND_VAR
    if (ret == 0) {
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        assert(REAL(pthread_mutex_lock)(&impl->posix_lock) == 0);
    }
#endif
    return ret;
}

int clh_mutex_trylock(clh_mutex_t *impl, clh_context_t *me) {
    assert(0 && "Trylock not implemented for CLH.");

    return EBUSY;
}

static void __clh_mutex_unlock(clh_mutex_t *impl, clh_context_t *me) {
    COMPILER_BARRIER();
    waiting_policy_wake(&impl->head->spin);
}

void clh_mutex_unlock(clh_mutex_t *impl, clh_context_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    __clh_mutex_unlock(impl, me);
}

int clh_mutex_destroy(clh_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int clh_cond_init(clh_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int clh_cond_timedwait(clh_cond_t *cond, clh_mutex_t *lock, clh_context_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    int res;

    __clh_mutex_unlock(lock, me);
    DEBUG("[%d] Sleep cond=%p lock=%p posix_lock=%p\n", cur_thread_id, cond,
          lock, &(lock->posix_lock));
    DEBUG_PTHREAD("[%d] Cond posix = %p lock = %p\n", cur_thread_id, cond,
                  &lock->posix_lock);

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

    clh_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int clh_cond_wait(clh_cond_t *cond, clh_mutex_t *lock, clh_context_t *me) {
    return clh_cond_timedwait(cond, lock, me, 0);
}

int clh_cond_signal(clh_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int clh_cond_broadcast(clh_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int clh_cond_destroy(clh_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void clh_thread_start(void) {
}

void clh_thread_exit(void) {
}

void clh_application_init(void) {
}

void clh_application_exit(void) {
}


#endif // __CLH_H__



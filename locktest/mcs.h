#ifndef __MCS_H__
#define __MCS_H__

#include "def.h"
#define LOCK_ALGORITHM "MCS"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1

typedef struct mcs_node {
    struct mcs_node *volatile next;
    char __pad[pad_to_cache_line(sizeof(struct mcs_node *))];
    volatile int spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcs_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct mcs_mutex {
#if COND_VAR
    pthread_mutex_t posix_lock;
    char __pad[pad_to_cache_line(sizeof(pthread_mutex_t))];
#endif
    struct mcs_node *volatile tail __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcs_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t mcs_cond_t;
mcs_mutex_t *mcs_mutex_create(const pthread_mutexattr_t *attr);
int mcs_mutex_lock(mcs_mutex_t *impl, mcs_node_t *me);
int mcs_mutex_trylock(mcs_mutex_t *impl, mcs_node_t *me);
void mcs_mutex_unlock(mcs_mutex_t *impl, mcs_node_t *me);
int mcs_mutex_destroy(mcs_mutex_t *lock);
int mcs_cond_init(mcs_cond_t *cond, const pthread_condattr_t *attr);
int mcs_cond_timedwait(mcs_cond_t *cond, mcs_mutex_t *lock, mcs_node_t *me,
                       const struct timespec *ts);
int mcs_cond_wait(mcs_cond_t *cond, mcs_mutex_t *lock, mcs_node_t *me);
int mcs_cond_signal(mcs_cond_t *cond);
int mcs_cond_broadcast(mcs_cond_t *cond);
int mcs_cond_destroy(mcs_cond_t *cond);
void mcs_thread_start(void);
void mcs_thread_exit(void);
void mcs_application_init(void);
void mcs_application_exit(void);
void mcs_init_context(mcs_mutex_t *impl, mcs_node_t *context, int number);

typedef mcs_mutex_t lock_mutex_t;
typedef mcs_node_t lock_context_t;
typedef mcs_cond_t lock_cond_t;

#define lock_mutex_create mcs_mutex_create
#define lock_mutex_lock mcs_mutex_lock
#define lock_mutex_trylock mcs_mutex_trylock
#define lock_mutex_unlock mcs_mutex_unlock
#define lock_mutex_destroy mcs_mutex_destroy
#define lock_cond_init mcs_cond_init
#define lock_cond_timedwait mcs_cond_timedwait
#define lock_cond_wait mcs_cond_wait
#define lock_cond_signal mcs_cond_signal
#define lock_cond_broadcast mcs_cond_broadcast
#define lock_cond_destroy mcs_cond_destroy
#define lock_thread_start mcs_thread_start
#define lock_thread_exit mcs_thread_exit
#define lock_application_init mcs_application_init
#define lock_application_exit mcs_application_exit
#define lock_init_context mcs_init_context


#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>


extern __thread unsigned int cur_thread_id;

mcs_mutex_t *mcs_mutex_create(const pthread_mutexattr_t *attr) {
    mcs_mutex_t *impl = (mcs_mutex_t *)alloc_cache_align(sizeof(mcs_mutex_t));
    impl->tail        = 0;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif
    return impl;
}

static int __mcs_mutex_lock(mcs_mutex_t *impl, mcs_node_t *me) {
    mcs_node_t *tail;

    assert(me != NULL);

    me->next = LOCKED;
    me->spin = 0;

    // The atomic instruction is needed when two threads try to put themselves
    // at the tail of the list at the same time
    tail = static_cast<mcs_node_t *>(xchg_64((void *) &impl->tail, (void *) me));

    /* No one there? */
    if (!tail) {
        DEBUG("[%d] (1) Locking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
              impl->tail, me);
        return 0;
    }

    /* Someone there, need to link in */
    tail->next = me;
    COMPILER_BARRIER();

    waiting_policy_sleep(&me->spin);

    DEBUG("[%d] (2) Locking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
          impl->tail, me);
    return 0;
}

int mcs_mutex_lock(mcs_mutex_t *impl, mcs_node_t *me) {
    int ret = __mcs_mutex_lock(impl, me);
    assert(ret == 0);
#if COND_VAR
    if (ret == 0) {
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        assert(REAL(pthread_mutex_lock)(&impl->posix_lock) == 0);
    }
#endif
    DEBUG("[%d] Lock acquired posix=%p\n", cur_thread_id, &impl->posix_lock);
    return ret;
}

int mcs_mutex_trylock(mcs_mutex_t *impl, mcs_node_t *me) {
    mcs_node_t *tail;

    assert(me != NULL);

    me->next = 0;
    me->spin = LOCKED;

    // The trylock is a cmp&swap, where the thread enqueue itself to the end of
    // the list only if there are nobody at the tail
    tail = __sync_val_compare_and_swap(&impl->tail, 0, me);

    /* No one was there - can quickly return */
    if (!tail) {
        DEBUG("[%d] TryLocking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
              impl->tail, me);
#if COND_VAR
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            ;
        assert(ret == 0);
#endif
        return 0;
    }

    return EBUSY;
}

static void __mcs_mutex_unlock(mcs_mutex_t *impl, mcs_node_t *me) {
    DEBUG("[%d] Unlocking lock=%p tail=%p me=%p\n", cur_thread_id, impl,
          impl->tail, me);

    /* No successor yet? */
    if (!me->next) {
        // The atomic instruction is needed if a thread between the previous if
        // and now has enqueued itself at the tail
        if (__sync_val_compare_and_swap(&impl->tail, me, 0) == me)
            return;

        /* Wait for successor to appear */
        while (!me->next)
                CPU_PAUSE();
    }

    /* Unlock next one */
    waiting_policy_wake(&me->next->spin);
}

void mcs_mutex_unlock(mcs_mutex_t *impl, mcs_node_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    __mcs_mutex_unlock(impl, me);
}

int mcs_mutex_destroy(mcs_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int mcs_cond_init(mcs_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcs_cond_timedwait(mcs_cond_t *cond, mcs_mutex_t *lock, mcs_node_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    int res;

    __mcs_mutex_unlock(lock, me);
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

    mcs_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcs_cond_wait(mcs_cond_t *cond, mcs_mutex_t *lock, mcs_node_t *me) {
    return mcs_cond_timedwait(cond, lock, me, 0);
}

int mcs_cond_signal(mcs_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcs_cond_broadcast(mcs_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int mcs_cond_destroy(mcs_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void mcs_thread_start(void) {
}

void mcs_thread_exit(void) {
}

void mcs_application_init(void) {
}

void mcs_application_exit(void) {
}
void mcs_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}



#endif // __MCS_H__

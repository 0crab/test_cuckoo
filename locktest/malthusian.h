#ifndef __MALTHUSIAN_H__
#define __MALTHUSIAN_H__

#include "def.h"
#define LOCK_ALGORITHM "MALTHUSIAN"
#define NEED_CONTEXT 1
#define SUPPORT_WAITING 1

// This is the number of thread to let take the lock before taking the inactive
// list back to the active list
#define UNLOCK_COUNT_THRESHOLD 1024 //!\\ Must be a power of 2!

typedef struct malthusian_node {
    volatile int spin __attribute__((aligned(L_CACHE_LINE_SIZE)));
    char __pad[pad_to_cache_line(sizeof(int))];
    struct malthusian_node *volatile next;
    struct malthusian_node *volatile prev;
} malthusian_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct malthusian_mutex {
    struct malthusian_node *volatile tail
        __attribute__((aligned(L_CACHE_LINE_SIZE)));
    char __pad[pad_to_cache_line(sizeof(struct malthusian_node *))];
    struct malthusian_node *volatile passive_set_head;
    struct malthusian_node *volatile passive_set_tail;
#if COND_VAR
    pthread_mutex_t posix_lock;
#endif
} malthusian_mutex_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef pthread_cond_t malthusian_cond_t;
malthusian_mutex_t *malthusian_mutex_create(const pthread_mutexattr_t *attr);
int malthusian_mutex_lock(malthusian_mutex_t *impl, malthusian_node_t *me);
int malthusian_mutex_trylock(malthusian_mutex_t *impl, malthusian_node_t *me);
void malthusian_mutex_unlock(malthusian_mutex_t *impl, malthusian_node_t *me);
int malthusian_mutex_destroy(malthusian_mutex_t *lock);
int malthusian_cond_init(malthusian_cond_t *cond,
                         const pthread_condattr_t *attr);
int malthusian_cond_timedwait(malthusian_cond_t *cond, malthusian_mutex_t *lock,
                              malthusian_node_t *me, const struct timespec *ts);
int malthusian_cond_wait(malthusian_cond_t *cond, malthusian_mutex_t *lock,
                         malthusian_node_t *me);
int malthusian_cond_signal(malthusian_cond_t *cond);
int malthusian_cond_broadcast(malthusian_cond_t *cond);
int malthusian_cond_destroy(malthusian_cond_t *cond);
void malthusian_thread_start(void);
void malthusian_thread_exit(void);
void malthusian_application_init(void);
void malthusian_application_exit(void);
void malthusian_init_context(malthusian_mutex_t *impl,
                             malthusian_node_t *context, int number);

typedef malthusian_mutex_t lock_mutex_t;
typedef malthusian_node_t lock_context_t;
typedef malthusian_cond_t lock_cond_t;

#define lock_mutex_create malthusian_mutex_create
#define lock_mutex_lock malthusian_mutex_lock
#define lock_mutex_trylock malthusian_mutex_trylock
#define lock_mutex_unlock malthusian_mutex_unlock
#define lock_mutex_destroy malthusian_mutex_destroy
#define lock_cond_init malthusian_cond_init
#define lock_cond_timedwait malthusian_cond_timedwait
#define lock_cond_wait malthusian_cond_wait
#define lock_cond_signal malthusian_cond_signal
#define lock_cond_broadcast malthusian_cond_broadcast
#define lock_cond_destroy malthusian_cond_destroy
#define lock_thread_start malthusian_thread_start
#define lock_thread_exit malthusian_thread_exit
#define lock_application_init malthusian_application_init
#define lock_application_exit malthusian_application_exit
#define lock_init_context malthusian_init_context


#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <papi.h>


extern __thread unsigned int cur_thread_id;

// From D.Dice <https://blogs.oracle.com/dave/entry/a_simple_prng_idiom>
static inline uint32_t xor_random() {
    static __thread uint32_t rv = 0;

    if (rv == 0)
        rv = cur_thread_id + 1;

    uint32_t v = rv;
    v ^= v << 6;
    v ^= (uint32_t)(v) >> 21;
    v ^= v << 7;
    rv = v;

    return v & (UNLOCK_COUNT_THRESHOLD - 1);
}

malthusian_mutex_t *malthusian_mutex_create(const pthread_mutexattr_t *attr) {
    malthusian_mutex_t *impl =
            (malthusian_mutex_t *)alloc_cache_align(sizeof(malthusian_mutex_t));
    impl->tail             = 0;
    impl->passive_set_head = 0;
    impl->passive_set_tail = 0;
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, attr);
#endif

    return impl;
}

static int __malthusian_mutex_lock(malthusian_mutex_t *impl,
                                   malthusian_node_t *me) {
    malthusian_node_t *tail;

    assert(me != NULL);

    me->next = 0;
    me->spin = LOCKED;

    tail = static_cast<malthusian_node_t *>(xchg_64((void *) &impl->tail, (void *) me));

    /* No one there? */
    if (!tail) {
        return 0;
    }

    /* Someone there, need to link in */
    tail->next = me;
    COMPILER_BARRIER();

    /* Spin on my spin variable */
    waiting_policy_sleep(&me->spin);

    return 0;
}

int malthusian_mutex_lock(malthusian_mutex_t *impl, malthusian_node_t *me) {
    int ret = __malthusian_mutex_lock(impl, me);
    assert(ret == 0);
#if COND_VAR
    ret = REAL(pthread_mutex_lock)(&impl->posix_lock);
    assert(ret == 0);
#endif

    return 0;
}

int malthusian_mutex_trylock(malthusian_mutex_t *impl, malthusian_node_t *me) {
    malthusian_node_t *tail;

    me->next = 0;
    me->spin = LOCKED;

    /* Try to lock */
    tail = __sync_val_compare_and_swap(&impl->tail, 0, me);

    /* No one was there - can quickly return */
    if (!tail) {
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

// Helper functions to manage the passive set
static inline malthusian_node_t *
passive_set_pop_back(malthusian_mutex_t *impl) {
    malthusian_node_t *elem = impl->passive_set_tail;
    if (elem == 0)
        return NULL;

    impl->passive_set_tail = elem->prev;
    if (impl->passive_set_tail == 0)
        impl->passive_set_head = 0;
    else
        impl->passive_set_tail->next = 0;

    elem->prev = 0;
    elem->next = 0;

    return elem;
}

static inline malthusian_node_t *
passive_set_pop_front(malthusian_mutex_t *impl) {
    malthusian_node_t *elem = impl->passive_set_head;
    if (elem == 0)
        return NULL;

    impl->passive_set_head = elem->next;
    if (impl->passive_set_head == 0)
        impl->passive_set_tail = 0;
    else
        impl->passive_set_head->prev = 0;

    elem->prev = 0;
    elem->next = 0;

    return elem;
}

static inline void passive_set_push_front(malthusian_mutex_t *impl,
                                          malthusian_node_t *elem) {
    malthusian_node_t *prev_head = impl->passive_set_head;
    elem->next                   = prev_head;
    elem->prev                   = 0;

    impl->passive_set_head = elem;

    if (prev_head != 0) {
        prev_head->prev = elem;
    }

    if (impl->passive_set_tail == 0)
        impl->passive_set_tail = elem;
}

static void __malthusian_insert_at_head(malthusian_mutex_t *impl,
                                        malthusian_node_t *cur_head,
                                        malthusian_node_t *new_elem) {
    /**
     * Cur tail is either the current lock holder or a new thread enqueued in
     * the meantime (note that several new threads may be enqueued).
     * We insert new_elem just behind cur_head and (if any) in front of the
     * queue of new threads.
     **/
    malthusian_node_t *cur_tail =
            __sync_val_compare_and_swap(&impl->tail, cur_head, new_elem);
    if (cur_tail == cur_head) {
        cur_head->next = new_elem;
    } else {
        /**
         * One or several other threads managed to get inserted in the queue
         * before new_elem.
         * In this case, we must wait for the first thread to finish
         * its insertion and then insert new_elem in front of it.
         **/
        while (!cur_head->next)
                CPU_PAUSE();
        new_elem->next = cur_head->next;
        cur_head->next = new_elem;
        COMPILER_BARRIER();
    }
}

static void __malthusian_mutex_unlock(malthusian_mutex_t *impl,
                                      malthusian_node_t *me) {
    /**
     * "To ensure long-term fairness, the unlock operator periodically
     * selects the tail of the excess list T as the successor and then
     * grafts T into the main MCS chain immediately after the
     * lock-holder's element, passing ownership of the lock to T"
     **/
    if (xor_random() == 0) {
        DEBUG("[%d] Insert T as successor of me\n", cur_thread_id);
        malthusian_node_t *elem = passive_set_pop_back(impl);
        if (elem != 0) {
            __malthusian_insert_at_head(impl, me, elem);
            waiting_policy_wake(&me->next->spin);
            return;
        }
    }

    /* No successor yet? */
    if (!me->next) {
        /**
         * "Conversely, at unlock-time if the main queue is empty
         * except for the owner's node, we then extract a node
         * from the head of the passive list, insert it into the
         * queue at the tail and pass ownership to that thread."
         **/
        DEBUG("[%d - %p] Trying to extract from PS because no waiter\n",
              cur_thread_id, me);
        malthusian_node_t *extract_next = passive_set_pop_front(impl);
        if (extract_next == 0) {
            DEBUG("[%d - %p] No passive thread, old code\n", cur_thread_id, me);
            /* Try to atomically unlock */
            if (__sync_val_compare_and_swap(&impl->tail, me, 0) == me)
                return;

            /* Wait for successor to appear */
            DEBUG("[%d - %p] Wait for successor to appear\n", cur_thread_id,
                  me);
            while (!me->next)
                    CPU_PAUSE();

            waiting_policy_wake(&me->next->spin);
            return;
        } else {
            DEBUG("[%d - %p] Fetching thread from PS %p\n", cur_thread_id, me,
                  extract_next);
            __malthusian_insert_at_head(impl, me, extract_next);
            waiting_policy_wake(&me->next->spin);
            return;
        }
    }

    /**
     * "At unlock-time, if there exists any intermediate nodes in the
     * queue between the owner's node and the current tail, then we
     * have a surplus threads in the ACS and we can unlink and excise
     * one of those nodes and transfer it to the head of the passive
     * list where excess "cold" threads reside."
     * Note that we systematically choose the to unlink the thread
     * that is enqueued just behind the current lock holder. *
     **/
    if (me->next != impl->tail) {
        DEBUG("[%d - %p] Moving %p to PS\n", cur_thread_id, me, me->next);

        /**
         * It is possible that the successor of the node that we want to unlink
         *it not fully linked yet
         * (i.e., me->next->next is not set).
         * So we wait until the successor has finished its insertion.
         **/
        while (!me->next->next)
                CPU_PAUSE();

        malthusian_node_t *new_next = me->next->next;
        passive_set_push_front(impl, me->next);
        me->next = new_next;
        COMPILER_BARRIER();
    }

    /* Unlock next one */
    waiting_policy_wake(&me->next->spin);
}

void malthusian_mutex_unlock(malthusian_mutex_t *impl, malthusian_node_t *me) {
#if COND_VAR
    int ret = REAL(pthread_mutex_unlock)(&impl->posix_lock);
    assert(ret == 0);
#endif
    __malthusian_mutex_unlock(impl, me);
}

int malthusian_mutex_destroy(malthusian_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int malthusian_cond_init(malthusian_cond_t *cond,
                         const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int malthusian_cond_timedwait(malthusian_cond_t *cond, malthusian_mutex_t *lock,
                              malthusian_node_t *me,
                              const struct timespec *ts) {
#if COND_VAR
    int res;

    __malthusian_mutex_unlock(lock, me);
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

    malthusian_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int malthusian_cond_wait(malthusian_cond_t *cond, malthusian_mutex_t *lock,
                         malthusian_node_t *me) {
    return malthusian_cond_timedwait(cond, lock, me, 0);
}

int malthusian_cond_signal(malthusian_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int malthusian_cond_broadcast(malthusian_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int malthusian_cond_destroy(malthusian_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void malthusian_thread_start(void) {
}

void malthusian_thread_exit(void) {
}

void malthusian_application_init(void) {
}

void malthusian_application_exit(void) {
}

void malthusian_init_context(lock_mutex_t *UNUSED(impl),
                             lock_context_t *UNUSED(context),
                             int UNUSED(number)) {
}


#endif // __MALTHUSIAN_H__

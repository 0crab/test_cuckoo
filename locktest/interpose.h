#define PARTITIONED 1

#ifdef SPINLOCK
#include "spinlock.h"
#elif defined(CLHLOCK)
#include "clh.h"
#elif defined(MCSLOCK)
#include "mcs.h"
#elif defined(CNALOCK)
#include "cna.h"
#elif defined(MALTHUSIANLOCK)
#include "malthusian.h"
#elif defined(PARTITIONED)
#include "partitioned.h"
#endif


unsigned int last_thread_id;
__thread unsigned int cur_thread_id;


typedef struct {
    lock_mutex_t *lock_lock;
    char __pad[pad_to_cache_line(sizeof(lock_mutex_t *))];
#if NEED_CONTEXT
    lock_context_t lock_node[MAX_THREADS];
#endif
} lock_transparent_mutex_t;


static lock_transparent_mutex_t *
ht_lock_create(const pthread_mutexattr_t *attr) {
    lock_transparent_mutex_t *impl = (lock_transparent_mutex_t *)alloc_cache_align(sizeof *impl);
    impl->lock_lock                = lock_mutex_create(attr);
#if NEED_CONTEXT
    lock_init_context(impl->lock_lock, impl->lock_node, MAX_THREADS);
#endif
    return impl;
}


static lock_transparent_mutex_t *ht_lock_get(lock_transparent_mutex_t *impl) {
    if (impl == NULL) {
        impl = ht_lock_create(NULL);
    }
    return impl;
}


lock_transparent_mutex_t  *litl_mutex_init(
                       const pthread_mutexattr_t *attr) {

    return ht_lock_create(attr);
}

static inline lock_context_t *get_node(lock_transparent_mutex_t *impl) {
#if NEED_CONTEXT
    return &impl->lock_node[cur_thread_id];
#else
    return NULL;
#endif
}

int litl_mutex_lock(lock_transparent_mutex_t * impl_) {
    lock_transparent_mutex_t *impl = ht_lock_get(impl_);
    return lock_mutex_lock(impl->lock_lock, get_node(impl));
}

int litl_mutex_unlock(lock_transparent_mutex_t * impl_) {
    lock_transparent_mutex_t *impl = ht_lock_get(impl_);
    lock_mutex_unlock(impl->lock_lock, get_node(impl));
    return 0;

}

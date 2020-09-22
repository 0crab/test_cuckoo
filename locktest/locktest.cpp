#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <atomic>
#include <assert.h>
#include <thread>
#include <mutex>
#include "tracer.h"
#define TEST_NUM 1000000
#define THREAD_NUM 4

//#define MUTEXLOCK 1
#define PSPINLOCK 1
//#define LITLLOCK 1

#define CONFLICT true

using counter_type = int64_t;

unsigned long  *runtimelist;

#include "interpose.h"

class TABLE {
public:
    int global_count;

    TABLE() {
        all_locks_.emplace_back(112, spinlock());
    }

    void work() {
        locks_t &locks = get_current_locks();
#if CONFLICT
        locks[0].lock();
        global_count++;
        locks[0].unlock();
#else
        locks[cur_thread_id].lock();
        global_count++;
        locks[cur_thread_id].unlock();
#endif
    }

    class spinlock {
    public:
        spinlock() : elem_counter_(0), is_migrated_(true) {
#ifdef LITLLOCK
            impl = litl_mutex_init(NULL);
#elif defined(MUTEX)
            ;
#elif defined(PSPINLOCK)
            pthread_spin_init(&lock_,0);
#endif
        }

        spinlock(const spinlock &other) noexcept
                : elem_counter_(other.elem_counter()),
                  is_migrated_(other.is_migrated()) {
#ifdef LITLLOCK
            impl = litl_mutex_init(NULL);
#elif defined(MUTEX)
            ;
#elif defined(PSPINLOCK)
            pthread_spin_init(&lock_,0);
#endif
        }

        spinlock &operator=(const spinlock &other) noexcept {
            elem_counter() = other.elem_counter();
            is_migrated() = other.is_migrated();
            return *this;
        }

        void lock() noexcept {
#ifdef LITLLOCK
            litl_mutex_lock(impl);
#elif defined(MUTEX)
            mtx.lock();
#elif defined(PSPINLOCK)
            pthread_spin_lock(&lock_);
#endif

        }

        void unlock() noexcept {
#ifdef LITLLOCK
            litl_mutex_unlock(impl);
#elif defined(MUTEX)
            mtx.unlock();
#elif defined(PSPINLOCK)
            pthread_spin_unlock(&lock_);
#endif
        }

        bool try_lock() noexcept {
            return false;
        }

        counter_type &elem_counter() noexcept { return elem_counter_; }

        counter_type elem_counter() const noexcept { return elem_counter_; }

        bool &is_migrated() noexcept { return is_migrated_; }

        bool is_migrated() const noexcept { return is_migrated_; }

    private:
#ifdef LITLLOCK
        lock_transparent_mutex_t *impl;
#elif defined(MUTEX)
        std::mutex mtx;
#elif defined(PSPINLOCK)
        pthread_spinlock_t lock_;
#endif

        counter_type elem_counter_;
        bool is_migrated_;
    };

    using locks_t = std::vector<spinlock>;
    using all_locks_t = std::list<locks_t>;

    all_locks_t all_locks_;

    locks_t &get_current_locks() { return all_locks_.back(); }
};

TABLE table;

void run(int tid){
    cur_thread_id = tid;
    Tracer tracer;
    tracer.startTime();
    for(int i = 0; i < TEST_NUM; i++){
        table.work();
    }
    runtimelist[tid]+=tracer.getRunTime();
}

int main(){
    runtimelist=(unsigned long *)calloc(THREAD_NUM,sizeof(unsigned long));

    std::vector<std::thread> threads;
    for(int i = 0; i < THREAD_NUM; i++){
        threads.push_back(std::thread(run,i));
    }
    for(int i = 0; i < THREAD_NUM; i++){
        threads[i].join();
    }

    std::cout<<"global_count : "<<table.global_count<<std::endl;

    unsigned long runtime=0;
    for(int i = 0;i < THREAD_NUM; i++){
        runtime += runtimelist[i];
    }
    runtime /= (THREAD_NUM);
    printf("run runtime:%lu\n",runtime);

}
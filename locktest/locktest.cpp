#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <atomic>
#include <assert.h>
#include <thread>
#include <mutex>
#include <cstdlib>
#include "tracer.h"
#define TEST_NUM 1000000
#define THREAD_NUM 4

//#define MUTEX 1
//#define PSPINLOCK 1
#define LITLLOCK 1

//#define CONFLICT true

using counter_type = int64_t;

unsigned long  *runtimelist;

int conflict_rate;
bool *conflict_signal_list;

#include "interpose.h"

class TABLE {
public:
    int global_count_f;
    int global_count_nf;

    TABLE() {
        all_locks_.emplace_back(113, spinlock());
    }

    void work(int i) {
        locks_t &locks = get_current_locks();
        if(conflict_signal_list[i]){
            locks[112].lock();
            global_count_f++;
            locks[112].unlock();
        }else{
            locks[cur_thread_id].lock();
            global_count_nf++;
            locks[cur_thread_id].unlock();
        }
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
        std::mutex mtx  __attribute__((aligned(128)));
#elif defined(PSPINLOCK)
        pthread_spinlock_t lock_ __attribute__((aligned(128)));
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

typedef struct alignas(128) INST{
    std::atomic<uint64_t> lock_;
}Inst;

Inst  * insts;

void insts_init(){
    return;
}

void run(int tid){
    cur_thread_id = tid;
    Tracer tracer;
    tracer.startTime();
    Inst  * p = insts+tid;
    for(int i = 0; i < TEST_NUM; i++){
        //table.work(i);
        uint64_t d;
        do { d = p->lock_.load(); } while (!p->lock_.compare_exchange_strong(d, i));
    }
    runtimelist[tid]+=tracer.getRunTime();
}



int main(int argc,char **argv){

    if(argc == 2){
        conflict_rate = atol(argv[1]);
    }else{
        printf("./locktest <conflict_rate>");
        exit(0);
    }

    assert(conflict_rate<=100&&conflict_rate>=0);

    insts = static_cast<Inst *>(calloc(THREAD_NUM, sizeof(Inst)));
    insts_init();

    conflict_signal_list = static_cast<bool *>(calloc(TEST_NUM, sizeof(bool)));

    srand((unsigned)time(NULL));
    for(int i = 0 ; i < TEST_NUM; i++){
        conflict_signal_list[i] = rand() % 100 < conflict_rate;
    }

    runtimelist=(unsigned long *)calloc(THREAD_NUM,sizeof(unsigned long));

    std::vector<std::thread> threads;
    for(int i = 0; i < THREAD_NUM; i++){
        threads.push_back(std::thread(run,i));
    }
    for(int i = 0; i < THREAD_NUM; i++){
        threads[i].join();
    }

    std::cout<<"global_count_f : "<<table.global_count_f<<std::endl;
    std::cout<<"global_count_nf: "<<table.global_count_nf<<std::endl;

    unsigned long runtime=0;
    for(int i = 0;i < THREAD_NUM; i++){
        runtime += runtimelist[i];
    }
    runtime /= (THREAD_NUM);
    printf("run runtime:%lu\n",runtime);

}
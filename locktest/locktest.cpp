#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <atomic>
#include <assert.h>
#include <thread>

#define TEST_NUM 1000000
#define THREAD_NUM 4

using counter_type = int64_t;

#include "interpose.h"

class TABLE {
public:
    int global_count;

    TABLE() {
        all_locks_.emplace_back(1, spinlock());
    }

    void work() {
        locks_t &locks = get_current_locks();
        locks[0].lock();
        global_count++;
        locks[0].unlock();
    }

    class spinlock {
    public:
        spinlock() : elem_counter_(0), is_migrated_(true) {
            impl = litl_mutex_init(NULL);
        }

        spinlock(const spinlock &other) noexcept
                : elem_counter_(other.elem_counter()),
                  is_migrated_(other.is_migrated()) {
            impl = litl_mutex_init(NULL);
        }

        spinlock &operator=(const spinlock &other) noexcept {
            elem_counter() = other.elem_counter();
            is_migrated() = other.is_migrated();
            return *this;
        }

        void lock() noexcept {
            litl_mutex_lock(impl);
        }

        void unlock() noexcept {
            litl_mutex_unlock(impl);
        }

        bool try_lock() noexcept {
            return false;
        }

        counter_type &elem_counter() noexcept { return elem_counter_; }

        counter_type elem_counter() const noexcept { return elem_counter_; }

        bool &is_migrated() noexcept { return is_migrated_; }

        bool is_migrated() const noexcept { return is_migrated_; }

    private:
        lock_transparent_mutex_t *impl;
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
    for(int i = 0; i < TEST_NUM; i++){
        table.work();
    }
}

int main(){

    std::vector<std::thread> threads;
    for(int i = 0; i < THREAD_NUM; i++){
        threads.push_back(std::thread(run,i));
    }
    for(int i = 0; i < THREAD_NUM; i++){
        threads[i].join();
    }

    std::cout<<"global_count : "<<table.global_count<<std::endl;
}
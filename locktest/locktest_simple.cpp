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

    }

    void work(int i) {
        //locks_t &locks = get_current_locks();
       std::mutex mtx;
       for(int i = 0; i< TEST_NUM; i++){
        mtx.lock();
        global_count_nf++;
        mtx.unlock();
       }
    }

};

TABLE table;

void work1(int tid){
    std::mutex mtx;
    uint64_t dd;
    for(int i = 0; i< TEST_NUM; i++){
        mtx.lock();
        dd+=i;
        mtx.unlock();
    }
    printf("%d,%lu\n",tid,dd);
}


void run(int tid){
    cur_thread_id = tid;
    Tracer tracer;
    tracer.startTime();
    //table.work(0);
    work1(tid);
    runtimelist[tid]+=tracer.getRunTime();
}



int main(int argc,char **argv){

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
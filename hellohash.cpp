
#include <iostream>
#include <string>

#include "libcuckoo/cuckoohash_map.hh"

libcuckoo::cuckoohash_map<uint64_t , uint64_t> Table;

#define TEST_NUM 1000000
#define TEST_RANGE 1000000

#define INSERT_THREAD_NUM 4
#define READ_THREAD_NUM 0


void insert_thread(int tid){
    for(uint64_t i = 0; i <=  TEST_NUM; ++i){
        //uint64_t v = i % 2 + 1 ;
        uint64_t key = i % TEST_RANGE;
        //printf("%lu pre update %lu\n",pthread_self()%1000,i);
         Table.insert_or_assign(key,key );
         //printf("%lu after update %lu\n",pthread_self()%1000,i);
    }
    printf("write thread %d stop\n",tid);
}
void read_thread(int tid){
    uint64_t total =0;
    for(uint64_t i = 0; i < TEST_NUM; ++i){
        uint64_t key = i % TEST_RANGE;
        uint64_t v  ;
        //printf("--------------------------%lu pre find %lu\n",pthread_self()%1000,i);
        //v = Table.find(key);
        Table.find_KV(key,v);
        //printf("--------------------------%lu after find %lu\n",pthread_self()%1000,i);
        if(v != key){
            printf("error  thread %d, %lu read\n", tid, total);
            printf("key %d: value %d\n", key, v );
            exit(-1);
        }else{
            ++total ;
        }
    }
    printf("read thread %d read total %lu\n", tid, total);
}


int main() {
    //Table.insert_or_assign(key,1);

    for(int i = 0 ; i < TEST_RANGE ; i++){
        Table.insert_or_assign(i,i );
    }

    std::vector<std::thread> insert_threads;
    std::vector<std::thread> read_threads;

    for(int i = 0 ; i < INSERT_THREAD_NUM; i++){
        insert_threads.push_back(std::thread(insert_thread,i));
    }

    for(int i = 0 ; i < READ_THREAD_NUM; i++){
        read_threads.push_back(std::thread(read_thread,i));
    }

    for(int i = 0 ; i < INSERT_THREAD_NUM; i++){
        insert_threads[i].join();
    }

    for(int i = 0 ; i < READ_THREAD_NUM; i++){
        read_threads[i].join();
    }

    printf("run_cuckoo_count:%lu\n",Table.run_cuckoo_count);
    printf("run_cuckoo_loop_count:%lu\n",Table.run_cuckoo_loop_count);

    printf("hash power :%lu\n",Table.hashpower());

    unsigned long *p = Table.path_length_count;
    for(int i =0;i < Table.MAX_BFS_PATH_LEN;i++){
        printf("path len %d:%lu\n",i,p[i]);
    }
}

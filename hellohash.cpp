
#include <iostream>
#include <string>

#include "libcuckoo/cuckoohash_map.hh"

libcuckoo::cuckoohash_map<uint64_t , uint64_t> Table;

//uint64_t key = 666;

void insert_thread(int tid){
    for(uint64_t i = 0; i <=  1000; ++i){
        //uint64_t v = i % 2 + 1 ;
        uint64_t key = i % 10;
        Table.insert_or_assign(key,key );
    }
    printf("write thread %d stop\n",tid);
}
void read_thread(int tid){
    uint64_t total =0;
    for(uint64_t i = 0; i < 1000; ++i){
        uint64_t key = i % 10;
        uint64_t v  ;
        //Table.find_free(i % 10,v);
        Table.find(key,v);
        if(v != key){
            printf("error  thread %d, %lu read\n", tid, total);
            exit(-1);
        }else{
            ++total ;
        }
    }
    printf("read thread %d read total %lu\n", tid, total);
}


int main() {
    //Table.insert_or_assign(key,1);

    std::thread it(insert_thread,0);
    std::thread rt(read_thread,0);
    //std::thread it1(insert_thread,1);
    //std::thread rt1(read_thread,1);

    it.join();
    //it1.join();
    rt.join();
    //rt1.join();
}

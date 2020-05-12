#include <iostream>
#include "libcuckoo/cuckoohash_map.hh"
#include <sys/time.h>
#include <pthread.h>
#define DATA_NUM 10000000

#define THREAD_NUM 4

#define SET_OP 0

unsigned long runtimelist[THREAD_NUM];

libcuckoo::cuckoohash_map<unsigned long, unsigned long> Table;

uint64_t get_runtime(struct timeval start,struct timeval end){
    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

void *thread_set(int id){
    printf("starting set %d\n",id);
    unsigned long key,value;
    if(id==-1){
        for(int i=0;i<DATA_NUM;i++){
            Table.insert(key,value);
        }
    }else{
        struct timeval t1,t2;
        gettimeofday(&t1,NULL);
        for(int i=0;i<DATA_NUM;i++){
            key=i;
            value=i*10;
            Table.update(key,value);
        }
        gettimeofday(&t2,NULL);
        runtimelist[id]+=get_runtime(t1,t2);
    }

}

void *thread_get(int id){
    printf("starting get %d\n",id);
    int success_num=0;
    unsigned long key;
    struct timeval t1,t2;
    gettimeofday(&t1,NULL);
    for(int i=0;i<DATA_NUM;i++){
        key=(uint64_t)i;
        if(Table.find(key)!=NULL){
            success_num++;
        }
    }
    gettimeofday(&t2,NULL);
    runtimelist[id]+=get_runtime(t1,t2);
    printf("thread %d,total %d,find %d\n",id,DATA_NUM,success_num);
}

int main() {

    unsigned long tmpkey,tmpvalue;
    for (int i = 0; i < DATA_NUM; i++) {
        tmpkey=i;
        tmpvalue=i;
        Table.insert(tmpkey, tmpvalue);
    }

    pthread_t pid[THREAD_NUM];
    thread_set(-1);
    if(SET_OP){
        for(int i=0;i<THREAD_NUM;i++){
            if(pthread_create(&pid[i], NULL, reinterpret_cast<void *(*)(void *)>(thread_set),
                              reinterpret_cast<void *>(i)) != 0){
                printf("create pthread error\n");
                return 0;
            }
        }
    } else{

        for(int i=0;i<THREAD_NUM;i++){
            if(pthread_create(&pid[i], NULL, reinterpret_cast<void *(*)(void *)>(thread_get),
                              reinterpret_cast<void *>(i)) != 0){
                printf("create pthread error\n");
                return 0;
            }
        }
    }

    for(int i=0;i<THREAD_NUM;i++){
        pthread_join(pid[i],NULL);
    }

    uint64_t runtime=0;
    for(int i=0;i<THREAD_NUM;i++){
        runtime+=runtimelist[i];
    }
    printf("runtime:%ld\n", runtime/THREAD_NUM);

    return 0;
}

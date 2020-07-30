#include <iostream>
#include <libcuckoo/cuckoohash_map.hh>
#include <sys/time.h>
#include <pthread.h>
#include <chrono>
#include <string.h>
#include <sys/stat.h>
#include "generator.h"

using namespace std;


int thread_num;

int op;

#define THREAD_NUM thread_num

#define SET_OP 0

const char *existingFilePath = "./testfile.dat";

#define KEY_LEN 8
#define VALUE_LEN 8

#define SKEW 0.0
#define KEY_RANGE 260000
#define DATA_NUM KEY_RANGE
#define KV_NUM  KEY_RANGE
#define ROUND_SET 10

unsigned long * runtimelist;

libcuckoo::cuckoohash_map<string, string> Table;

typedef struct{
    string key;
    string value;
}KVOBJ;

vector<KVOBJ> database;

void con_database(){
    double skew = SKEW;
    uint64_t range = KEY_RANGE;
    uint64_t count = DATA_NUM;
    uint64_t *array =( uint64_t * ) calloc(count, sizeof(uint64_t));

    struct stat buffer;
    if (stat(existingFilePath, &buffer) == 0) {
        cout << "read generation" << endl;
        FILE *fp = fopen(existingFilePath, "rb+");
        fread(array, sizeof(uint64_t), count, fp);
        fclose(fp);
    }else{
        if (skew < zipf_distribution<uint64_t>::epsilon) {
            std::default_random_engine engine(
                    static_cast<uint64_t>(chrono::steady_clock::now().time_since_epoch().count()));
            std::uniform_int_distribution<size_t> dis(0, range + 0);
            for (size_t i = 0; i < count; i++) {
                array[i] = static_cast<uint64_t >(dis(engine));
            }
        } else {
            zipf_distribution<uint64_t> engine(range, skew);
            mt19937 mt;
            for (size_t i = 0; i < count; i++) {
                array[i] = engine(mt);
            }
        }
        FILE *fp = fopen(existingFilePath, "wb+");
        fwrite(array, sizeof(uint64_t), count, fp);
        fclose(fp);
        cout << "write generation" << endl;
    }

    char key_buf[KEY_LEN + 1];
    char value_buf[VALUE_LEN + 1];
    for(uint64_t i; i < DATA_NUM; i++){
        uint64_t n = array[i] / 26;
        uint8_t c = array[i] % 26;
        sprintf(key_buf, "%d", n);
        sprintf(value_buf, "%d", n);
        memset(key_buf + strlen(key_buf), 'a'+c, KEY_LEN - strlen(key_buf));
        memset(value_buf + strlen(value_buf), 'a'+c+1 , VALUE_LEN - strlen(value_buf));

        KVOBJ *kv = new KVOBJ ;
        kv->key = string(key_buf,KEY_LEN);
        kv->value = string(value_buf,VALUE_LEN);
        database.push_back(*kv);
    }

}

uint64_t get_runtime(struct timeval start,struct timeval end){
    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

void thread_set(int id){
    if(id == -1){
        printf("pre set\n");
        auto it = database.begin();
        while(it != database.end()){
            Table.insert_or_assign(it->key,it->value);
            it++;
        }
    }else{
        printf("starting set %d\n",id);
        struct timeval t1,t2;
        gettimeofday(&t1,NULL);
        auto it = database.begin();
        while(it != database.end()){
            Table.insert_or_assign(it->key,it->value);
            it++;
        }
        gettimeofday(&t2,NULL);
        runtimelist[id]+=get_runtime(t1,t2);
    }
}

void thread_get(int id){
    printf("starting get %d\n",id);
    struct timeval t1,t2;
    gettimeofday(&t1,NULL);
    auto it = database.begin();
    while(it != database.end()){
        try {
            Table.find(it->key);
        }catch(out_of_range e){
            cout<<e.what()<<endl;
            exit(-1);
        }
        it++;
    }
    gettimeofday(&t2,NULL);
    runtimelist[id]+=get_runtime(t1,t2);
   // printf("thread %d,total %d,find %d\n",id,DATA_NUM,success_num);
}



int main(int argc,char **argv) {
    if(argc == 3){
        thread_num = atol(argv[1]);
        op = atol(argv[2]);
    }else{
        printf("./test_cuckoo <thread> <op 1:update 0:get>");
    }

    runtimelist = new unsigned long[THREAD_NUM]();

    con_database();

    vector<thread> threads;

    thread_set(-1);

    for(int i = 0; i < thread_num; i++){
        if(op){
            threads.push_back(thread(thread_set,i));
        }else{
            threads.push_back(thread(thread_get,i));
        }
    }
    for(int i =0; i < thread_num; i++){
            threads[i].join();
    }
    uint64_t runtime=0;
    for(int i=0;i<THREAD_NUM;i++){
        runtime+=runtimelist[i];
    }
    printf("op:%d thread %d runtime:%ld\n\n", op,thread_num,runtime/THREAD_NUM);

    return 0;
}


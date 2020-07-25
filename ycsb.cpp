#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "libcuckoo/cuckoohash_map.hh"

#include "tracer1.h"

string load_path = "/home/czl/CLionProjects/test_cuckoo/load-a.dat";
string run_path = "/home/czl/CLionProjects/test_cuckoo/run-a.dat";


#define BATCH_SIZE 4096
#define SET_HEAD_SIZE 32
#define GET_HEAD_SIZE 24

using namespace ycsb;

libcuckoo::cuckoohash_map<string , string> Table;


typedef struct{
    string key;
    string value;
    bool r;
}ITEM;

vector<ITEM> database;

int THREAD_NUM=4;

unsigned long  *runtimelist;

std::vector<YCSB_request *> loads;

void con_database(int begin_index=0,int end_index=loads.size());

void con_package(char *buf,YCSB_request * req);

int get_package_size(YCSB_request * req);


void work_thread(int tid);

int main(int argc, char **argv){
    char * path;
    if(argc == 3){
        THREAD_NUM = std::atol(argv[1]);
        runtimelist=(unsigned long *)malloc(THREAD_NUM* sizeof(unsigned long));
        path=argv[2];
    }else{
        printf("please input filename\n");
        return 0;
    }

    {
        YCSBLoader loader( load_path.c_str());

        loads=loader.load();

        con_database();

        std::vector<std::thread> threads;

        for(int i=0;i<THREAD_NUM;i++){
            printf("creating thread %d\n",i);
            threads.push_back(std::thread(work_thread,i));
        }
        for(int i=0;i<THREAD_NUM;i++){
            threads[i].join();
        }

        unsigned long runtime=0;
        for(int i = 0;i < THREAD_NUM; i++){
            runtime += runtimelist[i];
        }
        runtime /= (THREAD_NUM);
        printf("\n____\n load runtime:%lu\n",runtime);
    }

    printf("***\n***\nfinish load start runing\n***\n***\n");

    {
        YCSBLoader loader(run_path.c_str());

        loads=loader.load();

        con_database();

        std::vector<std::thread> threads;

        for(int i=0;i<THREAD_NUM;i++){
            printf("creating thread %d\n",i);
            threads.push_back(std::thread(work_thread,i));
        }
        for(int i=0;i<THREAD_NUM;i++){
            threads[i].join();
        }

        unsigned long runtime=0;
        for(int i = 0;i < THREAD_NUM; i++){
            runtime += runtimelist[i];
        }
        runtime /= (THREAD_NUM);
        printf("\n____\n run runtime:%lu\n",runtime);
    }

    return 0;
}

void work_thread(int tid){
    int send_num = database.size()/THREAD_NUM;
    int start_index = tid*send_num;

    Tracer tracer;
    tracer.startTime();
    for(int i=0;i<send_num;i++){
        ITEM it = database[start_index + i];
        if(it.r){
            try {
                string res = Table.find(it.key);
            }catch (const std::out_of_range& e){
                std::cout<<e.what()<<endl;
                exit(-1);
            }
        }else{
            Table.insert_or_assign(it.key,it.value);
        }
    }
    runtimelist[tid]+=tracer.getRunTime();
    printf("thread %d stop\n",tid);
}

void con_database(int begin_index,int end_index){

    for(int i= begin_index;i<end_index;i++) {

        YCSB_request *req = loads[i];

        ITEM it;

        it.r = req->getOp() == lookup;
        it.key = string(req->getKey(),req->keyLength());
        it.value = string(req->getVal(),req->valLength());

        database.push_back(it);

    }

}




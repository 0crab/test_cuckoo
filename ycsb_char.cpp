#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "libcuckoo/cuckoohash_map.hh"

#include "tracer1.h"

#define LOCAL true
#define CHECK_FIND false

#if(LOCAL)
string load_path = "/home/czl/CLionProjects/test_cuckoo/load-a.dat";
string run_path = "/home/czl/CLionProjects/test_cuckoo/run-a.dat";
#else
string load_path = "/kolla/asterixdb/YCSB/load-a-200m-8B.dat";
string run_path = "/kolla/asterixdb/YCSB/run-a-200m-8B.dat";
#endif




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

int THREAD_NUM=4;

unsigned long  *runtimelist;

std::vector<YCSB_request *> loads;

std::vector<YCSB_request *> runs;

void work_thread(int tid,bool r);

int main(int argc, char **argv){
    char * path;
    if(argc == 2){
        THREAD_NUM = std::atol(argv[1]);
        runtimelist=(unsigned long *)malloc(THREAD_NUM* sizeof(unsigned long));
    }else{
        printf("please input thread_num\n");
        return 0;
    }

    cout<<"load file:"<<load_path<<endl;
    cout<<"run  file:"<<run_path<<endl;
    cout<<"CHECK FIND :"<<CHECK_FIND<<endl;
    {
        YCSBLoader loader( load_path.c_str());

        loads=loader.load();

        cout<<"finish load load file"<<endl;
        std::vector<std::thread> threads;

        for(int i=0;i<THREAD_NUM;i++){
            threads.push_back(std::thread(work_thread,i,true));
        }
        printf("finish create %d thread ==>",THREAD_NUM);
        for(int i=0;i<THREAD_NUM;i++){
            threads[i].join();
        }
        printf("run stop\n");

        unsigned long runtime=0;
        for(int i = 0;i < THREAD_NUM; i++){
            runtime += runtimelist[i];
        }
        runtime /= (THREAD_NUM);
        printf("load runtime:%lu\n",runtime);
    }

    {
        YCSBLoader loader1(run_path.c_str());

        runs=loader1.load();

        cout<<"finish load run file"<<endl;

        std::vector<std::thread> threads;

        for(int i=0;i<THREAD_NUM;i++){
            threads.push_back(std::thread(work_thread,i,false));
        }
        printf("finish create %d thread ==>",THREAD_NUM);
        for(int i=0;i<THREAD_NUM;i++){
            threads[i].join();
        }
        printf("run stop\n");

        unsigned long runtime=0;
        for(int i = 0;i < THREAD_NUM; i++){
            runtime += runtimelist[i];
        }
        runtime /= (THREAD_NUM);
        printf("run runtime:%lu\n",runtime);
    }

    printf("run_cuckoo_count:%lu\n",Table.run_cuckoo_count);
    printf("run_cuckoo_loop_count:%lu\n",Table.run_cuckoo_loop_count);

    unsigned long *p = Table.path_length_count;
    for(int i =0;i < Table.MAX_BFS_PATH_LEN;i++){
        printf("path len %d:%lu\n",i,p[i]);
    }

    return 0;
}

void work_thread(int tid,bool load){
    std::vector<YCSB_request *>  *work_load;
    if(load){
        work_load = &loads;
    }else{
        work_load = &runs;
    }

    int send_num = work_load->size()/THREAD_NUM;
    int start_index = tid*send_num;

    Tracer tracer;
    tracer.startTime();
    for(int i=0;i<send_num;i++){
        YCSB_request * it = work_load->at(start_index + i);
#if(!CHECK_FIND)
        if(!it->getOp()){
            try {
                string res = Table.find(it->getKey());
            }catch (const std::out_of_range& e){
                std::cout<<e.what()<<endl;
                exit(-1);
            }
        }else{
            Table.insert_or_assign(it->getKey(),it->getVal());
        }
#else
        if(!it->getOp()){
            try {
                string res = Table.find(it->getKey());
                if(res != it->getKey()){
                    cout<<"find err"<<endl;
                    cout<<it->getKey()<<":"<<res<<endl;
                    exit(-1);
                }
            }catch (const std::out_of_range& e){
                std::cout<<e.what()<<endl;
                exit(-1);
            }
        }else{
            Table.insert_or_assign(it->getKey(),it->getKey());
        }
#endif
    }
    runtimelist[tid]+=tracer.getRunTime();

}






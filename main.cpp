#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <sys/time.h>
#include <sys/stat.h>
#include <omp.h>
#include <atomic>
#include <random>
#include <time.h>
#include <mutex>
#include <algorithm>
#include <numeric>
//#include "pcm-memory.h"
//#define REMOVE_TEST
// #define CHECK_KEY
// #define SCAN_TEST
//#define VAR_TEST
// #define LATENCY
// #define PCMM
//#define SCAN_TEST
//#include "all_tree.h"
//#include "nyx_na.h"
// #define RECOVERY
//workload note: i: 50%scan 50%write, e: 100%update h: variable length
#define WARMUP

#include "timer.hpp"
#include "all_tree.hpp"
#ifdef PCMM
// #include "cpucounters.h"
// #include "utils.h"
#include "pcm-memory.h"
using namespace pcm;

#endif
char values_pool[] =
      "NvhE8N7yR26f4bbpMJnUKgHncH6QbsI10HyxlvYHKFiMk5nPNDbueF2xKLzteSd0NazU2APk"
      "JWXvBW2oUu8dkZnWMMu37G8TH2qm"
      "S0c8A9z41pxrC6ZU79OnfCZ06DsNXWY3U4dt1JTGQVvylBdZSlHWXC4PokCxsMdjv8xRptFM"
      "MQyHZRqMhNDnrsGKA12DEr7Zur0n"
      "tZpsyreMmPwuw7WMRnoN5wAYWtkqDwXyQlYb4RgtSc4xsonpTx2UhIUi15oJTx1CXAhmECac"
      "CQfntFnrSZt5qs1L64eeJ9Utus0N"
      "mKgEFV8qYDsNtJ21TkjCyCDhVIATkEugCw1BfNIB9AZDGiqXc0llp4rlJPl4bIG2QC4La3M1"
      "oh3yGlZTmdvN5pj1sIGkolpdoYVJ"
      "0NZM9KAo1d5sGFv9yGC7X0CTDOqyRu5c4NPktU70NbKqWNXa1kcaigIfeAuvJBs0Wso2osHz"
      "OjrbawgpfBPs1ePaWHgw7vbOcu9v"
      "Cqz1GnmdQw4mGSo4cc6tebQuKqLkQHuXa1MdRmzinBRoGQBQehqrDmmfNhcxfozcU7hOTjFA"
      "jryJ4HdSK57gOlrte5sZlvDW9rFd"
      "4OxG6WtFdZomRQPTNc4D9t7smqBR9EYDSjiAAqmIgZUiycHrlv6JQzEiexjqfGUbo8oJV6wi"
      "u7l3Jlfb94uByDxoexkMT5AjJzls"
      "er1dc9EfQz88q5Hv00g53Q3H6jcgicoY8YW5K4josd2e53ikesQi2kzqvTI9xxM5wtFexkFm"
      "8wFdMs6YmNpvNgTf37Hz204wX1Sf"
      "djFmCYEcP533LYcGB7CslEVMPYRZXHBT98XKtt8RqES7HBW65xSJRSj3qhIDUsgeu2Flo4Yq"
      "S68QoE69JzyBnwmmYw6uulVLVIAe"
      "iLl49oUhEiEjem8RrHPpEvrUoLDWwMdh14MfxwmEQbtGnUHEpRktUB6b7JTJN8OHBlLrvr71"
      "TkRK728ZgRv32rMZJ46O17qHTYc4"
      "AepNCGbpTII0J05OYiush6hiDo6H5pVHVUWy3nm7BBrBzEHVOCBMHNniw4CIzfavGLaUfgjl"
      "Bg0D4JBmYmkg0A4maCXsE9QTnGbA"
      "fQErGZkdMnRxXJ5EJ627e7zuFuVtazb0L65B3nU5R9tyUl2bTZiDcakK9evrTXoTkbkGjkCO"
      "iMSThGFScb6Lsgvl5wNCzlUZCxof"
      "jYQCLusRkXEm0CNVuifTnytctwLfKjwob4hJ0WxlQN9FV9Mm9zT61EQ8zEMrqr6hf7XMqhcQ"
      "R7DWAaf1fM4oNLIA7ZdKaspUaU6h"
      "oP2w3t3MktVaBp6MgS6Apbkb7EsihETHHqKFkKMCkYBbKfgsq7Jy49T1Wx2UJsD3XX03kVBb"
      "qRWmryYoMIqiCTCTqa0jIKzqQEnN";

#define OP_INSERT 1
#define OP_DELETE 2
#define OP_UPDATE 3
#define OP_READ 4
#define OP_SCAN 5
#define VAL_LEN 300

std::string find_path("workloads/");
//std::string find_path("/mnt/nvme/ycsb/");
std::string load_prefix("ycsb_load_workload");
std::string run_prefix("ycsb_run_workload");

std::vector<size_t> load_key;
std::vector<size_t> run_key;
std::vector<int> run_op;
std::vector<int> run_vlen;
size_t global_total = 0;

#ifdef VAR_TEST
std::vector<std::string> key_vec;
#endif
#ifdef LATENCY
std::vector<size_t> tail_latency_read;
std::vector<size_t> tail_latency_write;
#endif
Tree_api* my_tree;
int recover_arg = 1;
std::atomic<size_t> negative(0);

#ifdef LATENCY
    std::mutex lat_lock;
#endif

void load(int start, int end){

    for(size_t i=start; i<end; i++)
    {
#ifdef LONG_VAL
        char* value = &values_pool[i%1024];
        bool insert_result = my_tree->insert(load_key[i], 8 , value, val_len);
#else
        char* value = &values_pool[i%1024];
        bool insert_result = my_tree->insert(load_key[i], 8, value, 8);
#endif 
        
        if(insert_result != true)
        {
            std::cout<<"load fail!"<<std::endl;
            std::cout<<"fail pos is"<<i<<"key is"<<load_key[i]<<std::endl;
            abort();
        }
    }
}

void check(int start, int end){
    for(size_t i=start; i<end; i++)
    {
        //char* value = &values_pool[i%1024];
        //char* value = "1145141";
        //my_tree->update(run_key[i], 8, value, 8);
        //value = &values_pool[i%1000];
        //if(!my_tree->update(load_key[i], 8, value, 8))
// #ifdef LONG_VAL
//         if(!my_tree->find(load_key[i], val_len))
//         {
//             negative++;
//         }
#ifdef SCAN_TEST
        if(!my_tree->scan(load_key[i], 8, run_vlen[i]))
        {
            negative++;
        }
#else
        if(!my_tree->find(load_key[i], 8))
        {
            // negative++;
        }
#endif  
        // bool find_result = my_tree->find(load_key[i], 8);
        // if(!find_result)
        // {
        //    negative++;
        // }
    }
}

void run(int start, int end){
#ifdef LATENCY
        std::vector<size_t> local_write;
        std::vector<size_t> local_read;
        size_t local_total=0;
        Timer local_timer;
        size_t single_latency;
#endif
        char* value;
        for(size_t i=start; i<end; i++)
        {
            size_t nanosecond;
            switch(run_op[i]){
                case OP_INSERT:
                    value = &values_pool[0];
#ifdef LATENCY
                    local_timer.start();
#endif
                    my_tree->insert(run_key[i], 8, value, 8);
#ifdef LATENCY
                    local_write.push_back(local_timer.elapsed<std::chrono::nanoseconds>());
#endif
                    break;
                case OP_READ:
#ifdef LATENCY
                    local_timer.start();
#endif                   
                    if(!my_tree->find(run_key[i], 8))
                    {
                        // negative++;
                    }
#ifdef LATENCY
                    single_latency = local_timer.elapsed<std::chrono::nanoseconds>();
                    local_read.push_back(single_latency);
                    local_total += single_latency;
#endif
                  
                    //if(!my_tree->find(run_key[i], 8))
                    //{
                    //     negative++; 187,625,134    %19.1
                    //}
                    break;
                case OP_UPDATE:
                    value = "1145141";
                    //my_tree->update(run_key[i], 8, value, 8);
                    //value = &values_pool[i%1000];
                    //if(!my_tree->find(run_key[i], 8))
                    if(!my_tree->update(run_key[i], 8, value, 8))
                    {
                        negative++;
                    }
                    break;
                case OP_DELETE:
                    break;
                case OP_SCAN:
                    if(my_tree->scan(run_key[i], 8, run_vlen[i]) <= 0)
                    {
                        negative++;
                    }
                    break;
                default:
                    std::cout<<"unknown operation"<<std::endl;
                    break;
            }
        }
        //std::cout<<"negative is "<<negative<<std::endl;
#ifdef LATENCY
        lat_lock.lock();
        tail_latency_read.insert(tail_latency_read.end(), local_read.begin(), local_read.end());
        tail_latency_write.insert(tail_latency_write.end(), local_write.begin(), local_write.end());
        local_total = local_total / local_read.size();
        global_total += local_total;
        lat_lock.unlock();

#endif

}

void single_test(std::vector<size_t> &key_dist, size_t key_count){
    std::default_random_engine random(time(NULL));
    std::uniform_int_distribution<size_t> dist1(0, key_count-1);
    for(int i=key_count - 1; i>=0; i--)
    {
// Retry:
//         size_t new_key = dist1(random);
//         if(key_dist.size() > 1)
//         {
//             if(new_key > key_count - 1 || key_dist[i] < 0)
//             {
//                 std::cout<<"error!!!"<<std::endl;
//             }
//             if(std::find(key_dist.begin(), key_dist.end(), new_key) != key_dist.end())
//             {
//                 goto Retry;
//                 //std::cout<<"duplicate!!!"<<std::endl;
//             }
//         }

        key_dist.push_back(i);
       
    }
}  


void benchmark(std::string bench_name, int thread_num){
    std::vector<std::thread> thread_vec;
    std::string run_ycsb_name = find_path + run_prefix + bench_name;
    std::string load_ycsb_name = find_path + load_prefix + bench_name; 
    std::ifstream if1(load_ycsb_name);
    //read all key into memory than run benchmark
    std::string insert("INSERT");
    std::string remove("REMOVE");
    std::string read("READ");
    std::string update("UPDATE");
    std::string scan("SCAN");
    size_t key;
    std::string op;
    size_t count=0;
    size_t count_run=0;
    size_t value_len;
    while (if1.good())
    {
        /*if(count >= LOAD_NUM - 5)
        {
            std::string line_data;
            std::getline(if1, line_data);
            std::cout<<line_data<<std::endl;
        }*/
        
        if1 >> op >> key >> value_len;
        if(if1.fail())
            break;
        if (!op.size()) 
            continue;
        if (op.size() && op.compare(insert) != 0)
        {
            std::cout << "READING LOAD FILE FAIL!\n";
            std::cout << op <<std::endl;
            return;
        }
        load_key.push_back(key);
        count++;
    }
    if1.close();
    size_t op_array[6]={0,0,0,0,0,0};
    if1.open(run_ycsb_name);
    while(if1.good())
    {

        if1 >> op >> key;
        if(if1.fail())
            break;
        run_key.push_back(key);
        count_run++;
        if(op == insert)
        {
            if1>>value_len;
            run_op.push_back(OP_INSERT);
#ifndef SCAN_TEST
            run_vlen.push_back(8);
#endif
            op_array[OP_INSERT]++;
        }
        else if(op == update)
        {
            if1>>value_len;
            run_op.push_back(OP_UPDATE);
            op_array[OP_UPDATE]++;
        }
        else if(op == read)
        {
            run_op.push_back(OP_READ);
            op_array[OP_READ]++;
        }
        else if(op == remove)
        {
            run_op.push_back(OP_DELETE);
            op_array[OP_DELETE]++;
        }
        else if(op == scan)
        {
            if1>>value_len;
#ifndef SCAN_TEST
            run_op.push_back(OP_SCAN);
#endif
            run_vlen.push_back(value_len);
            op_array[OP_SCAN]++;
        }
    }
    //load execute
    if1.close();
    std::cout<<"finish read data, "<<count<<" key will be loaded, "<<count_run<<
        "key will be run" <<std::endl;
    printf("OP distribution: %ld insert, %ld remove, %ld update, %ld select, %ld scan \n", 
            op_array[OP_INSERT], op_array[OP_DELETE], op_array[OP_UPDATE], op_array[OP_READ], op_array[OP_SCAN]);

    // exit(0);
    Timer my_timer1;
    int start_pos = 0; 
    int step = count / thread_num;       
    if(count != 0)//skip load or not
    {
        
        my_timer1.start();
        std::thread thread_vec[thread_num];
        for(int i=0; i<thread_num; i++)
        {
            //std::cout<<"duplicate is "<<start_pos<<" "<<start_pos+step<<std::endl;
            if(i == thread_num - 1)
                thread_vec[i] = std::thread(load, start_pos, count);
            else
                thread_vec[i] = std::thread(load, start_pos, start_pos + step);
            start_pos += step;
        }

        for(int i=0; i<thread_num; i++)
        {
            thread_vec[i].join();
        }
        start_pos = 0;
        // my_tree->insert(0, 114514, nullptr, 8);
        // #pragma omp parallel num_threads(thread_num)
        // {
        //     //size_t load_start = omp_get_thread_num() * step;
        //     //size_t this_count = 0;
        //     #pragma omp for schedule(static)
        //     for(size_t i=0; i<count; i++)
        //     {
        //         char* value = &values_pool[i%1024];
        //         bool insert_result = my_tree->insert(load_key[i], 8, value, 8);
        //         if(insert_result != true)
        //         {
        //             std::cout<<"load fail!"<<std::endl;
        //             std::cout<<"fail pos is"<<i<<"key is"<<load_key[i]<<std::endl;
        //             abort();
        //         }
        //     }
        // }

        auto t = my_timer1.elapsed<std::chrono::milliseconds>();
        double throughput = (double) count / t;
        printf("Load throughput is %f Mop/s\n", (count / 1000000.0) / (t / 1000.0));
        //return;
    }
    //abort();
#ifdef CHECK_KEY

    size_t negative_single=0;
    size_t positive_single=0;
    //single_test(key_dist, count);
    // for(int i=0; i<count; i++)
    // {
    //     if(my_tree->find(load_key[i], 8) == false)
    //     {
    //         std::cout<<"fail pos is"<<i<<" key is "<<load_key[i]<<std::endl;
    //         //abort();
    //         negative_single++;
    //     }
    //     else
    //         positive_single++;
    // }
    // std::cout<<"success search "<<positive_single<<" key and fail "<<negative_single<<" key"<<std::endl;
    // std::cout<<"check load key!"<<std::endl;
    // return;
#ifdef REMOVE_TEST
    std::cout<<"test remove operation"<<std::endl;
#else
    std::cout<<"check all loaded key"<<std::endl;
#endif

//     #pragma omp parallel num_threads(thread_num)
//     {
//         #pragma omp for schedule(static)
//         for(int i=0; i<count; i++)
//         {
// #ifndef REMOVE_TEST
//             char* value = "1145141";
//             bool load_result = my_tree->update(load_key[key_dist[i]], 8, value, 8);
//             //bool load_result = my_tree->find(load_key[key_dist[i]], 8);
//             if(load_result == false)
//             {
//                 positive++;
//             }
// #else   
//             my_tree->remove(load_key[i], 8);  
// #endif
//         }
//         // if(negative != 0)

//     }
    std::thread check_vec[thread_num];
    // count = count / 200;
#ifdef SCAN_TEST
    std::default_random_engine random(time(NULL));
    std::uniform_int_distribution<int> dist1(1, 100);
    for(int i=0; i < count; i++)
    {
      run_vlen.push_back(dist1(random));
    }
#endif
    my_timer1.start();
    for(int i=0; i<thread_num; i++)
    {
        if(i == thread_num - 1)
            check_vec[i] = std::thread(check, start_pos, count);
        else
            check_vec[i] = std::thread(check, start_pos, start_pos + step);
        start_pos += step;
    }
    for(int i=0; i<thread_num; i++)
    {
        check_vec[i].join();
    }
    std::cout<<"success update "<<negative<<" key "<<std::endl;
    auto t2 = my_timer1.elapsed<std::chrono::milliseconds>();
    //std::cout<<"success "<<positive<<" read "<<std::endl;
    printf("throughput is %f Mop/s\n", (count / 1000000.0) / (t2 / 1000.0));
    std::cout<<"check done, all key can be found"<<std::endl;
    return;
#endif  
    //execute run
    count_run = count_run / recover_arg;
    size_t step2 = count_run / thread_num;
    

    
#ifdef PCMM
    PCM* m = PCM::getInstance();
    ServerUncoreMemoryMetrics metrics;
    metrics = m->PMMTrafficMetricsAvailable() ? Pmem : PartialWrites;
    size_t imc = 32;
    printf("metrics %d \n", metrics);
    ServerUncoreCounterState* before_state = new ServerUncoreCounterState[m->getNumSockets()];
    ServerUncoreCounterState* after_state = new ServerUncoreCounterState[m->getNumSockets()];
    // ServerUncoreCounterState *before_state;
    // ServerUncoreCounterState *end_state;
    uint64_t r_bytes;
    uint64_t w_bytes;
    uint64_t before_time = 0;
    uint64_t after_time = 0;
    for(int i=0; i<m->getNumSockets(); i++)
    {
        before_state[i] = m->getServerUncoreCounterState(i);
    }
    // before_state = m->getSystemCounterState();
    // before_time = m->getTickCount();
    // my_pcm.calculate_start();
#endif
#ifdef WARMUP
    printf("start warm up \n");
    std::thread warmup_thread[thread_num];
    int max_end = 200000000;
    for(int i=0; i<thread_num; i++)
    {
        warmup_thread[i] = std::thread([&](int start, int end){
            if(end > max_end)
              end = max_end;
            for(int i=start; i<end; i++)
            {
                if(run_op[i] == OP_READ)
                {
                    if(!my_tree->find(run_key[i], 8))
                    {
                        // negative++;
                    }
                  
                }
            }
        }, start_pos, start_pos + step2);
        start_pos += step2;
    }
    for(int i=0; i<thread_num; i++)
    {
        warmup_thread[i].join();
    }
    my_tree->insert(0, 114514, nullptr, 0);
    printf("warm up complete\n");
    start_pos = 0;
#endif
    

    my_timer1.start();
    std::thread run_thread[thread_num];
    for(int i=0; i<thread_num; i++)
    {
        if(i == thread_num - 1)
            run_thread[i] = std::thread(run, start_pos, count_run);
        else
            run_thread[i] = std::thread(run, start_pos, start_pos + step2);
        start_pos += step2;

    }
    for(int i=0; i<thread_num; i++)
    {
        run_thread[i].join();
    }

    
#ifdef PCMM
    //  after_state = m->getSystemCounterState();
    size_t bytes = 0;
    printf("thread number is %d\n", m->getNumSockets());
    for(int i=0; i<m->getNumSockets(); i++)
    {
        after_state[i] = m->getServerUncoreCounterState(i);
        for(int j=0; j<imc; j++)
        {
            bytes += getMCCounter(j, ServerUncorePMUs::EventPosition::PMM_WRITE, before_state[i], after_state[i]); 
        }
        
    }
    printf("bytes total is %d\n", bytes);
    delete[] before_state;
    delete[] after_state;
    //  after_time = m->getTickCount();
    //  r_bytes = getBytesReadFromPMM(before_state, after_state);
    //  w_bytes = getBytesWrittenToPMM(before_state, after_state);
    //  float r_gb = r_bytes / (1024.0 * 1024.0);
    //  float w_gb = w_bytes / (1024.0 * 1024.0);
    //  printf("Read total %f and Write total after %f second %d \n", r_gb, w_gb, after_time-before_time);
    // my_pcm.calculate_end();
    // my_pcm.output();
#endif

    auto t3 = my_timer1.elapsed<std::chrono::milliseconds>();
    printf("Run throughput is %f Mop/s\n", ((count_run - negative) / 1000000.0) / (t3 / 1000.0));
    std::cout<<"negative is "<<negative<<std::endl;
#ifdef LATENCY
    std::sort(tail_latency_read.begin(), tail_latency_read.end());
    std::cout<<"READ "<<std::endl;
    std::cout<<"P9 "<<tail_latency_read[tail_latency_read.size() * 0.9]<<std::endl;
    std::cout<<"P99 "<<tail_latency_read[tail_latency_read.size() * 0.99]<<std::endl;
    std::cout<<"P999 "<<tail_latency_read[tail_latency_read.size() * 0.999]<<std::endl;
    std::cout<<"P9999 "<<tail_latency_read[tail_latency_read.size() * 0.9999]<<std::endl;
    std::cout<<"P99999 "<<tail_latency_read[tail_latency_read.size() * 0.99999]<<std::endl;

    std::sort(tail_latency_write.begin(), tail_latency_write.end());
    std::cout<<"WRITE "<<std::endl;
    std::cout<<"P9 "<<tail_latency_write[tail_latency_write.size() * 0.9]<<std::endl;
    std::cout<<"P99 "<<tail_latency_write[tail_latency_write.size() * 0.99]<<std::endl;
    std::cout<<"P999 "<<tail_latency_write[tail_latency_write.size() * 0.999]<<std::endl;
    std::cout<<"P9999 "<<tail_latency_write[tail_latency_write.size() * 0.9999]<<std::endl;
    std::cout<<"P99999 "<<tail_latency_write[tail_latency_write.size() * 0.99999]<<std::endl;

    global_total = global_total / thread_num;
    std::cout<<"average latency is "<<global_total<<" " <<tail_latency_read.size()<<std::endl;
#endif


#ifdef RECOVERY
    abort();
#endif
    /*for(int i=0; i<count_run; i++)
    {
        if(my_tree->find(run_key[i], 8) == false)
        {
            std::cout<<"key" << i <<" not found!"<<std::endl;
            abort();
        }
    }*/
}


void benchmark_varkey(std::string bench_name, int thread_num){
#ifdef VAR_TEST
    std::string run_ycsb_name = find_path + run_prefix + bench_name;
    std::ifstream if1(run_ycsb_name);
    //read all key into memory than run benchmark
    std::string insert("INSERT");
    size_t key;
    std::string op;
    size_t count=0;
    size_t value_len;
   
    std::default_random_engine random(time(NULL));
    std::uniform_int_distribution<int> dist1(1, 1000);
    int error_count = 0;
    while (if1.good())
    {
        
        if1 >> op >> key >> value_len;
        if(if1.fail())
            break;
        if (!op.size()) 
            continue;
        if (op.size() && op.compare(insert) != 0)
        {
            std::cout << "READING LOAD FILE FAIL!\n";
            std::cout << op <<std::endl;
            return;
        }
        std::string str_key;
        str_key.reserve(value_len);
        if(value_len == 8)
        {
            error_count++;       
        }
        for(int i=0; i<value_len; i++)
        {
            char byte_key = values_pool[dist1(random)];
            str_key.push_back(byte_key);
        }
        uint8_t* char_key = (uint8_t*)&key;
        for(int i=0; i<8; i++)
        {
            str_key.push_back(char_key[i]);
        }
        key_vec.push_back(str_key);
        count++;
        // if(count >=500000)
        //     break;
    }
    if1.close();
    std::cout<<"finish read data, "<<count<<" key will be run error count "<<error_count<<std::endl;
    Timer my_timer1;
    my_timer1.start();
    std::thread load_vec[thread_num];
    int step = count / thread_num;
    int start_pos = 0;
    for(int i=0; i<thread_num; i++)
    {
        if(i != thread_num - 1)
        {
            load_vec[i] = std::thread([](int start, int end){
                for(int j=start; j<end; j++)
                {
                    char value[9] = "11111111";
                    size_t tree_key = (size_t) & key_vec[j][0];
                    my_tree->insert(tree_key, key_vec[j].size(), value, 8);
                }
            }, start_pos, start_pos + step);
        }
        else
        {
            load_vec[i] = std::thread([](int start, int end){
                for(int j=start; j<end; j++)
                {
                    char value[9] = "11111111";
                    size_t tree_key = (size_t) & key_vec[j][0];
                    my_tree->insert(tree_key, key_vec[j].size(), value, 8);
                }
            }, start_pos, count);
        }
        start_pos += step;
    }

    for(int i=0; i<thread_num; i++)
    {
        load_vec[i].join();
    }
    // #pragma omp parallel num_threads(thread_num)
    // {
    //     #pragma omp for schedule(static)
    //     for(size_t i=0; i<count; i++)
    //     {
    //         char value[9] = "11111111";
    //         size_t tree_key = (size_t) &key_vec[i][0];
    //         my_tree->insert(tree_key, key_vec[i].size(), value, 8);      
    //     }
    // }

    auto t = my_timer1.elapsed<std::chrono::milliseconds>();
    printf("insert throughput is %f Mop/s\n", (count / 1000000.0) / (t / 1000.0));
    start_pos = 0;
    my_timer1.start();
    std::atomic<size_t> positive(0);
    std::thread run_vec[thread_num];
    for(int i=0; i<thread_num; i++)
    {
        if(i != thread_num - 1)
        {
            run_vec[i] = std::thread([=](int start, int end){
                for(int j=start; j<end; j++)
                {
                    char value[9] = "11111111";
                    size_t tree_key = (size_t) & key_vec[j][0];
                    if(!my_tree->find(tree_key, key_vec[j].size()))
                    {
                        negative++;
                    }
                }
            }, start_pos, start_pos + step);
        }
        else
        {
            run_vec[i] = std::thread([=](int start, int end){
                for(int j=start; j<end; j++)
                {
                    char value[9] = "11111111";
                    size_t tree_key = (size_t) & key_vec[j][0];
                    if(!my_tree->find(tree_key, key_vec[j].size()))
                    {
                        negative++;
                    }
                }
            }, start_pos, count);
        }
        start_pos += step;

    }
    for(int i=0; i<thread_num; i++)
    {
        run_vec[i].join();
    }
    // #pragma omp parallel num_threads(thread_num)
    // {
    //     #pragma omp for schedule(static)
    //     for(size_t i = 0; i<count; i++)
    //     {
    //         char value[9] = "11111111";
    //         size_t tree_key = (size_t) &key_vec[i][0];
    //         //if(my_tree->find(tree_key, key_vec[i].size()))
    //         if(my_tree->scan(tree_key, key_vec[i].size(), 100))
    //             positive++;
    //     }
    // }
    t = my_timer1.elapsed<std::chrono::milliseconds>();
    std::cout<<"positive is "<<negative<<std::endl;
    printf("search throughput is %f Mop/s\n", ((count - negative) / 1000000.0) / (t / 1000.0));
#endif

}





int main(int argc, char* argv[]){
#ifdef RUN_SIMPLE
    int thread_num = std::stoi(argv[1]);
    char* workload_type = argv[2];
    run_thread = thread_num;
    my_tree = get_tree();
    // std::vector<std::thread> thread_vec;
    // for(int i=0; i<thread_num; i++)
    // {

    // }
    // for(int i=0; i<thread_num; i++)
    for(size_t i=0; i<10000; i++)
    {
        char* value =&values_pool[0];
        bool result = my_tree->insert(i, 8, value, 8);
        if(!result)
        {
            std::cout<<"abort at i "<<i<<std::endl;
            abort();     
        }
            
    }
    for(size_t i=0; i<10000; i++)
    {

        if(!my_tree->find(i, 8))
        {
            std::cout<<"abort at i "<<i<<std::endl;
            abort();
        }
    }
    return 0;
#else
    //Tree_api* test_tree;
    int thread_num = std::stoi(argv[1]);
    char* workload_type = argv[2];
    if(argc == 4)
    {
#ifdef LONG_VAL
        val_len = std::stoi(argv[3]);
#else
        recover_arg = std::stoi(argv[3]);
#endif
    }
    std::string load_ycsb_name = find_path + load_prefix + workload_type; 
    std::string run_ycsb_name = find_path + run_prefix + workload_type;
    std::cout<<load_ycsb_name<<" "<<run_ycsb_name<<std::endl;
#ifdef USE_DPTREE
    parallel_merge_worker_num = thread_num;
#endif
    run_thread = thread_num;
    Timer init_timer;
    init_timer.start();
    my_tree = get_tree();
    auto t = init_timer.elapsed<std::chrono::milliseconds>();
    printf("init done in %f sec \n", t / 1000.0);
#ifndef VAR_TEST
    benchmark(workload_type, thread_num);
#else
    benchmark_varkey(workload_type, thread_num);
#endif
    delete my_tree;
    return 0;
#endif
}
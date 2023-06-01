#include "nvm_mgr.h"
#include "Tree.h"
#include "threadinfo.h"
#include <cassert>
#include <iostream>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <algorithm>
#include <unordered_map>


namespace NVMMgr_ns {

// global nvm manager
NVMMgr *nvm_mgr = NULL;
std::mutex _mtx;

int create_file(const char *file_name, uint64_t file_size) {
    printf("[NVM MGR]File name: %s\tFile size: %ld\n", file_name, file_size);
    std::ofstream fout(file_name);
    if (fout) {
        fout.close();
        int result = truncate(file_name, file_size);
        if (result != 0) {
            printf("[NVM MGR]\ttruncate new file failed\n");
            exit(1);
        }
    } else {
        printf("[NVM MGR]\tcreate new file failed\n");
        exit(1);
    }

    return 0;
}

NVMMgr::NVMMgr() {
    // access 的返回结果， 0: 存在， 1: 不存在
    int initial = access(get_filename(), F_OK);
    first_created = false;

    if (initial) {
        int result = create_file(get_filename(), filesize);
        if (result != 0) {
            printf("[NVM MGR]\tcreate file failed when initalizing\n");
            exit(1);
        }
        first_created = true;
        printf("[NVM MGR]\tcreate file success.\n");
    }

    // open file
    fd = open(get_filename(), O_RDWR);
    if (fd < 0) {
        printf("[NVM MGR]\tfailed to open nvm file\n");
        exit(-1);
    }
    if (ftruncate(fd, filesize) < 0) {
        printf("[NVM MGR]\tfailed to truncate file\n");
        exit(-1);
    }
    printf("[NVM MGR]\topen file %s success.\n", get_filename());

    // mmap
    void *addr = mmap((void*)run_start_addr, filesize, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    run_thread_local_start = run_start_addr + PGSIZE;
    run_data_block_start = run_thread_local_start + PGSIZE * max_threads;

    /*if (addr != (void *)start_addr) {
        printf("[NVM MGR]\tmmap failed %p \n", addr);
        exit(0);
    }*/
    printf("[NVM MGR]\tmmap successfully\n");
    printf("start addr is %lx\n", run_start_addr);
    // initialize meta data
    meta_data = static_cast<Head *>(addr);
    if (initial) {
        // set status of head and set zero for bitmap
        // persist it
        memset((void *)meta_data, 0, PGSIZE);

        meta_data->status = magic_number;
        meta_data->threads = 0;
        meta_data->free_bit_offset = 0;
        meta_data->generation_version = 0;

        flush_data((void *)meta_data, PGSIZE);
        printf("[NVM MGR]\tinitialize nvm file's head\n");
    } else {
        meta_data->generation_version++;
        flush_data((void *)&meta_data->generation_version, sizeof(uint64_t));
        printf("nvm mgr restart, the free offset is %lld, generation version "
               "is %lld\n",
               meta_data->free_bit_offset, meta_data->generation_version);
    }
}

NVMMgr::~NVMMgr() {
    // normally exit
    printf("[NVM MGR]\tnormally exits, NVM reset..\n");
    //        Head *head = (Head *) start_addr;
    //        flush_data((void *) head, sizeof(Head));
    munmap((void *)run_start_addr, filesize);
    close(fd);
}

//    void NVMMgr::recover_done() {
//        Head *head = (Head *) start_addr;
//        head->threads = 0;
//        flush_data((void *) head, sizeof(Head));
//    }

// bool NVMMgr::reload_free_blocks() {
//    assert(free_page_list.empty());
//
//    while (true) {
//        if (free_bit_offset >= (filesize / PGSIZE) - (max_threads + 1)) {
//            return false;
//        }
//
//        uint8_t value = meta_data->bitmap[free_bit_offset];
//
//        // not free
//        if (value != 0) {
//            free_bit_offset++;
//            continue;
//        } else if (value == 0) { // free
//            for (int i = 0; i < 8; i++) {
//                if (free_bit_offset >=
//                    (filesize / PGSIZE) - (max_threads + 1)) {
//                    break;
//                }
//                if (meta_data->bitmap[free_bit_offset] != 0) {
//                    free_bit_offset++;
//                    continue;
//                }
//
//                free_page_list.push_back(free_bit_offset);
//                free_bit_offset++;
//            }
//        }
//        break;
//    }
////    std::cout << "[NVM MGR]\treload free blocks, now free_page_list size is
///" /              << free_page_list.size() << "\n";
//    return true;
//}

void *NVMMgr::alloc_thread_info() {
    // not thread safe
    size_t index = meta_data->threads++;
    flush_data((void *)&(meta_data->threads), sizeof(int));
    return (void *)(run_thread_local_start + index * PGSIZE);
}

void *NVMMgr::get_thread_info(int tid) {
    return (void *)(run_thread_local_start + tid * PGSIZE);
}

void *NVMMgr::alloc_block(int tid) {
    std::lock_guard<std::mutex> lock(_mtx);

    uint64_t id = meta_data->free_bit_offset;
    meta_data->free_bit_offset++;
    meta_data->bitmap[id] = tid;
    flush_data((void *)&(meta_data->bitmap[id]), sizeof(uint8_t));
    flush_data((void *)&(meta_data->free_bit_offset), sizeof(uint64_t));

    //void *addr = (void *)(run_block + id * PGSIZE);
    void *addr = (void*)(run_data_block_start + id * PGSIZE);
        /*printf("[NVM MGR]\talloc a new block %d, type is %lx\n", id, addr);
        std::cout<<"alloc a new block "<< meta_data->free_bit_offset<<"\n";
        std::cout<<"meta data addr "<< meta_data<<"\n";
        std::cout<<"mgr addr" <<this<<"\n";*/

    return addr;
}

// mutiple threads to recovery free list for
// threads using recovery_set
void NVMMgr::recovery_free_memory(PART_ns::Tree *art, int forward_thread) {
    int owner = 0;
    for (int i = 0; i < meta_data->free_bit_offset; i++) {
        meta_data->bitmap[i] = (owner++) % forward_thread;
    }
    std::cout << "finish set owner, all " << meta_data->free_bit_offset
              << " pages\n";

    const size_t power_two[10] = {8,   16,  32,   64,   128,
                                  256, 512, 1024, 2048, 4096};
    const int thread_num =1 ;
    std::thread *tid[thread_num];
    int per_thread_block = meta_data->free_bit_offset / thread_num;
    if (meta_data->free_bit_offset % thread_num != 0)
        per_thread_block++;

    std::cout << "every thread needs to recover " << per_thread_block
              << " pages\n";

    for (int i = 0; i < thread_num; i++) {
        tid[i] = new std::thread(
            [&](int id) {
                // [start, end]
                uint64_t start = id * per_thread_block;
                uint64_t end = (id + 1) * per_thread_block;
                //                std::cout << "start " << start
                //                          << " end " << end<<"\n";
                //uint64_t start_addr = data_block_start + start * PGSIZE;
                uint64_t start_addr = run_data_block_start + start * PGSIZE;
                
                uint64_t end_addr = std::min(
                    run_data_block_start + end * PGSIZE,
                    run_data_block_start + meta_data->free_bit_offset * PGSIZE);
                    //data_block_start + end * PGSIZE,
                    //data_block_start + meta_data->free_bit_offset * PGSIZE);
                //                std::set<std::pair<uint64_t, size_t>>
                //                    recovery_set; // used for memory recovery
                std::vector<std::pair<uint64_t, size_t>> recovery_set;

                art->rebuild(recovery_set, start_addr, end_addr, id);
#ifndef RECLAIM_MEMORY
                //std::cout<<"finish rebuild, start collect!"<<std::endl;
                std::sort(recovery_set.begin(), recovery_set.end());
                //std::cout << "start to reclaim\n";
                //int leaf_count = 0;
                //std::cout<<"leaf count !"<<recovery_set.size()<<std::endl;
                int j = start_addr / PGSIZE;
                int tid =
                    meta_data->bitmap[j]; // this block belong to which thread
                std::vector<thread_info*> the_vec;
                thread_info *the_ti = (thread_info *)get_thread_info(tid);
                the_ti->rebuild_info();
                the_vec.push_back(the_ti);
                thread_info * before = the_ti;
               for (int i = 0; i < recovery_set.size(); i++) {
                   uint64_t this_addr = recovery_set[i].first;
                    uint64_t this_size = recovery_set[i].second;
                    for (int id = 0; id < 10; id++) {
                        if (this_size <= power_two[id]) {
                           this_size = power_two[id];
                           break;
                       }
                   }
                    //std::cout<<"done "<<i<<std::endl;
                    int j = start_addr / PGSIZE;
                    int tid =
                        meta_data
                            ->bitmap[j]; // this block belong to which thread
                   
                    thread_info *the_ti = (thread_info *)get_thread_info(tid);
                    auto iter = std::find(the_vec.begin(), the_vec.end(), the_ti);
                    if(iter == the_vec.end())
                    {
                        //std::cout<<"new start!"<<std::endl;
                        the_ti->rebuild_info();
                        the_vec.push_back(the_ti);
                        before = the_ti;
                    }
                   the_ti->free_list->insert_into_freelist(
                       start_addr, this_addr - start_addr);
                   start_addr = this_addr + this_size;
               }


                    j = start_addr / PGSIZE;
                    tid =meta_data
                                ->bitmap[j]; // this block belong to which thread
                   
                    the_ti = (thread_info *)get_thread_info(tid);
                    auto iter = std::find(the_vec.begin(), the_vec.end(), the_ti);
                    if (iter == the_vec.end()) {
                        the_ti->rebuild_info();
                    the_ti->free_list->insert_into_freelist(
                        start_addr, end_addr - start_addr);
                }
#endif
            },
            i);
    }
    for (int i = 0; i < thread_num; i++) {
        tid[i]->join();
    }
}

/*
 * interface to call methods of nvm_mgr
 */
NVMMgr *get_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr == NULL) {
        printf("[NVM MGR]\tnvm manager is not initilized.\n");
        assert(0);
    }
    return nvm_mgr;
}

bool init_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr) {
        printf("[NVM MGR]\tnvm manager has already been initilized.\n");
        return false;
    }
    nvm_mgr = new NVMMgr();
    return true;
}

void close_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);
    std::cout << "[NVM MGR]\tclose nvm mgr\n";
    if (nvm_mgr != NULL) {
        delete nvm_mgr;
        nvm_mgr = NULL;
    }
}
} // namespace NVMMgr_ns

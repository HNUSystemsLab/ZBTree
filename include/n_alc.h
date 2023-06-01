#include <iostream>
#include <atomic>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include "pm_util.h"
#include <unordered_map>
#include <list>


#include "clht.h"
//#include "libpmem.h"
#ifndef _N_ALC_H
#define _N_ALC_H
//tree_alc doesn't need size-class and slab, all fixed length
//fast is the only goal
#define CHUNK_SIZE 1024*1024*64
#define CHUNK_SHIFT 26
#define MASK 0x3ffffff
#define NODE_BLK 1
#define LOG_BLK 2
#define BITMAP 3

struct Base_Meta{//used for DRAM
    void* start_address; //8 byte
    uint32_t block_id : 30;//also used for free_list to index, 8 byte
    uint32_t block_type : 2;//0 for tree block, 1 for log block
    uint32_t bitmap;//manage one block, temp use 8 bytes, 4bytes for sz_allocated 4bytes for sz_freed      
}__attribute__((aligned(64)));

struct List_node{
    void* address;
    Base_Meta* block_id;
}__attribute__((aligned(16)));

#ifdef RECOVERY

#define DUMMY_MAX_LEAF_KEY 30


struct Dummy_entry{
    uint64_t one_key;
    char* val;
};

struct Dummy_finger{
    uint64_t hash_byte:8;
    uint64_t control_bit:7;
    uint64_t control_bit2:1;
    uint64_t ptr_field:48;
};

union Dummy_adaptive{
    Dummy_finger sub_slot;
    uint64_t val;
};

struct Dummy_pleaf{
    uint32_t version_field;
    uint32_t bitmap;
    Dummy_pleaf* next;
    Dummy_entry slot_all[DUMMY_MAX_LEAF_KEY]; 
}__attribute__((aligned(256)));



struct Dummy_DLeaf2{
    Dummy_adaptive finger_print[DUMMY_MAX_LEAF_KEY];//8 * 30 = 240
    uint8_t temp_insert;//1
    uint8_t max_use;//1
    void* alt_ptr;//pointed to log or leaf//8
    Dummy_DLeaf2* next;//8
    size_t max_key;//8
    //size_t min_key;//8 5AP6JBP43T47
    //uint32_t version_field;
}__attribute__((aligned(8)));

struct Dummy_PLeaf2{
    size_t version_field;
    //size_t leaf_id;
    size_t max_key;
    //size_t min_key;
    Dummy_PLeaf2* next;
    Dummy_DLeaf2* dleaf_ptr;
}__attribute__((aligned(32)));

struct Dummy_dleaf{
    uint8_t hash_byte[DUMMY_MAX_LEAF_KEY];//finger print,1byte for one slot  31
    uint8_t max_use;//1
    uint8_t temp_insert;//1
    uint32_t version_field;//4
    Dummy_dleaf* next;//8
    Dummy_pleaf* pleaf; //8
    uint64_t min_key;
    //23 byte left
    uint8_t batch_update;
    //uint32_t bitmap=0;
    //31*16 space for data
    Dummy_entry buffer[DUMMY_MAX_LEAF_KEY];//31*16 
}__attribute__((aligned(64)));


#endif


class N_alc{
    private:
        std::list<List_node> release_list;
        uint32_t page_id[2];
        uint32_t decision_id[2];
        uint32_t alc_id;
        size_t pool_len;
    public:
        std::unordered_map<uint32_t, Base_Meta*> nfree_list;//used for tree index, thread local
        std::unordered_map<uint32_t, Base_Meta*> nlog_list;//used for variable pool
        void* start_address;
        void* end_address;
        N_alc(size_t fixed_size=0);//for fixed length k-v
        ~N_alc();
        //user
        void* n_allocate(uint32_t size, int log_alc=0);
        void n_free(void* ptr, size_t free_sz=0);
        void reorder_header(void* ptr);
        //inner
        bool test_exist(const char* file_name);
        void create_pool(const char* file_name, size_t pool_size);
        void get_more_chunk(int chunk_num, std::unordered_map<uint32_t, Base_Meta*> &temp_list, int pos);
        Base_Meta* get_free_block(std::unordered_map<uint32_t, Base_Meta*> &temp_list, int pos);
        void reuse_chunk(Base_Meta* free_header);
        void init();
        void recover_init(clht_t* fast_hash, size_t &anchor, int thread_num);
        void link_ptr(clht_t* fast_hash);
        void ignore_recover();
};



class D_alc{
    private:
#ifdef GLOBAL_ALC
        tbb::concurrent_unordered_map<uint32_t, Base_Meta*> dfree_list;//used for index
        tbb::concurrent_unordered_map<uint32_t, Base_Meta*> var_list;//used for variable key
#else
        std::unordered_map<uint32_t, Base_Meta*> dfree_list;
        std::unordered_map<uint32_t, Base_Meta*> var_list;
#endif
        size_t page_id[2];//each slot's size class
        size_t decision_id[2];
    public:
        D_alc();
        ~D_alc();
        void* huge_require(uint32_t size);
        void huge_release(void* ptr);
        uint64_t huge_malloc(uint32_t size, int variable_flag=0);
        void huge_free(void* ptr);
        //inner
        void* address_trans(uint64_t ptr, int list_flag=0);
        void get_push_back(std::unordered_map<uint32_t, Base_Meta*> &temp_map, int blk_num, int pos);
        Base_Meta* get_free_block(std::unordered_map<uint32_t, Base_Meta*> &temp_map, int pos);
        //void init_new_meta(size_t size, void* start_address);
        //int meta_find_slot(Base_Meta* this_meta);
        //301616.0038 ops/s
};
#endif  














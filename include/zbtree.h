#include <iostream>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <immintrin.h>
#include <fstream>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#include <jemalloc/jemalloc.h>
#include <omp.h>
#include <algorithm>
#include <thread>
//#include "search.h"
#include "tbb/spin_mutex.h"
#include "tbb/spin_rw_mutex.h"

#include "clht.h"
#include "libpmem.h"
#include "libpmemobj.h"  
#include "pm_util.h"
#include "n_alc.h"
#include <cmath>
//#include "struct.h"
typedef uint64_t Key_t;

#define MAX_LEAF_KEY 30
#define AVX_BORDER MAX_LEAF_KEY - 7
#define MAX_TREE_LEVEL 12 //31^12 big enough


#define SPLIT_POS (MAX_LEAF_KEY / 2 - 1) //15
#define LEFT_KEY (MAX_LEAF_KEY / 2 )//15
#define RIGHT_KEY (MAX_LEAF_KEY / 2)//15

#define LOG_INSERT '1'
#define LOG_UPDATE '2'
#define LOG_REMOVE '3'
#define LOG_SEGMENT '4'
// #define LONGVAL

struct Node_entry{
    Key_t one_key;
    Key_t val;
};


struct Inner_Node{
    uint64_t left_most_ptr; // 8 byte
    uint16_t max_size; //2 byte
    uint32_t version_field;
    uint64_t node_key[2][MAX_LEAF_KEY + 1];
    //std::atomic_flag leaf_lock2[MAX_LEAF_KEY + 1];
}; //inner node doesn't have to be 256 byte



// #pragma push
// #pragma pack(1)
struct finger_array{
    uint64_t hash_byte:8;
    uint64_t control_bit:6;
    uint64_t access_bit:2;
    uint64_t ptr_field:48;
};

union adaptive_slot{
    finger_array sub_slot;
    uint64_t val;
};
//#pragma pop


struct D_Leaf{
    adaptive_slot finger_print[MAX_LEAF_KEY];
    //finger_array finger_print[MAX_LEAF_KEY];//8 * 30 = 240
    uint8_t temp_insert;//1
    uint8_t max_use;//1
    void* alt_ptr;//pointed to log or leaf//8
    uint8_t compact_use = RIGHT_KEY; 
    uint8_t compact_full=false;
    D_Leaf* next;//8
    
    size_t max_key;//8
    //size_t min_key;//8
    //uint32_t version_field;//4
}__attribute__((aligned(8)));



#ifdef SNAP
struct P_Leaf{
    size_t version_field;
    P_Leaf* next;
    finger_array finger_print[MAX_LEAF_KEY];
}__attribute__((aligned(32)));
#else
//reduce the pleaf overhead for persistent
struct P_Leaf{
    size_t version_field;
    size_t max_key;
    //size_t min_key;
    P_Leaf* next;
    D_Leaf* dleaf_ptr;
}__attribute__((aligned(32)));
#endif
struct full_slot{
    uint8_t finger_print;
    uint64_t ptr_value;
    uint32_t offset;
};

struct Cursor{
    void* node;
    short node_pos;
    short node_max;
    uint32_t node_version;
}__attribute__((aligned(16)));

#pragma push
#pragma pack(1)
struct Log_Node{
    uint16_t log_len;//2
    uint8_t op_kind;//1
    uint8_t padding;//1
    uint32_t version_number;//4
    void* pptr;//point to pm leaf node 8
    uint64_t key;//8
    uint64_t val;//8
};
#pragma pop


struct VarKeyD{ //variable key structure store in DRAM
    uint32_t* key_len;
    char* key_ptr;
};

struct Log_buffer{
    uint32_t alt=0;
    void* pleaf=nullptr;
    Node_entry buffer[MAX_LEAF_KEY];
}__attribute__((aligned(256)));
//segment vector log
class DSeg{
    public:
        DSeg();
        ~DSeg();
        size_t temp_offset=0;
        size_t hit_count = 0;
        static const size_t log_end = CHUNK_SIZE - 64;
        static const size_t re_hold = log_end / 2;
        //Log_buffer* split_log = nullptr;
        uint16_t batch_count;
        uint16_t seg_count=0;
        Log_Node* log_vec=nullptr;
        size_t vec_start;
        void* append_one(size_t out_key, size_t out_val, void* out_pptr, char out_op, uint32_t version);
        void resize(size_t size);
        void recycle();
        void replay(std::vector<finger_array> &figner_vec, std::vector<Key_t> &key_vec);
};


//B+ tree implementation
class Tree{
    private:
        char* root;
        size_t temp_level=0;
        size_t temp_leaf=0;//total leaf count
        P_Leaf* leaf_start;
        D_Leaf* dstart;
    public:
        //N_alc tree_alloc;
        //E_log* elf_log;
        tbb::speculative_spin_rw_mutex function_lock;
        std::mutex start_mutex;
        //std::shared_mutex mut_;

        Tree(void* start_address=nullptr);
        ~Tree();
        void create_pmem();
        //basic
        size_t find(Key_t key, int val_sz=8);
        bool insert(Key_t key, void* val, int value_len=8);
        bool update(Key_t key, void* val, bool is_upsert=false);
        bool remove(Key_t key);
        bool simply_remove(Key_t key);
        int scan(Key_t start_key, int scan_count, Key_t* scan_array);


        uint64_t split_leaf(P_Leaf* split_node, D_Leaf* leaf_meta, adaptive_slot* dram_buffer, Node_entry* keys);
        void splitInnerNode(Cursor* path_array, Key_t split_key,uint64_t left_ptr, uint64_t right_ptr);
        void mergeInnerNode(Cursor* path_array);
        D_Leaf* beforeMerge(Cursor* path_array);
        uint64_t find_insert(Key_t key, Cursor* path_array);
        void recover();
        void bulkload(clht_t* elysia, size_t leafcount);

        //debug
        void liner_find(Key_t key);
        void liner_clear();
        //recovery
        void log_insert(Key_t key, finger_array val, Cursor* path);
        void quick_insert(Key_t key, void* val);


};


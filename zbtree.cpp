#include "zbtree.h"
// #define RECOVERY
// #define USE_PMDK
#define IGNORE
#ifdef BREAKDOWN_FIND
thread_local uint64_t breakdown_search=0;
thread_local uint64_t breakdown_check=0;
thread_local uint64_t breakdown_write=0;
thread_local uint64_t breakdown_split=0;
thread_local uint64_t breakdown_split2=0;
thread_local uint64_t breakdown_split2_write=0;
thread_local uint64_t breakdown_split2_split=0;
#endif
#define BINARY_HOLD 7
#define POOL_SIZE 64*1024*1024*1024ULL
#define TESTINLINE NOINLINE
#define THREAD_INIT 996996
// #define CXL
//#define SPACE
// #define BREAKDOWN
extern size_t pm_pool_size;
extern int run_thread;
extern int read_threshold;
extern int write_threshold;
std::atomic<size_t> countshit(0);
size_t count_dram = 0;
std::mutex thread_fence;
std::unordered_map<int, DSeg*> workers;
std::unordered_map<int, N_alc*> mmanagers;
thread_local int tid=114514;
#ifdef USE_PMDK
    PMEMobjpool* pop;
#else
    //thread_local N_alc tree_alloc(pm_pool_size);
#endif
thread_local Cursor root_path[MAX_TREE_LEVEL];
//thread_local DSeg sl;

static const uint64_t kFNVPrime64 = 1099511628211;
#ifdef _MSC_VER
    #define FORCEINLINE __forceinline
    #define NOINLINE __declspec(noinline)
    #define ALIGN(n) __declspec(align(n))
    FORCEINLINE uint32_t bsr(uint32_t x) {
        unsigned long res;
        _BitScanReverse(&res, x);
        return res;
    }
    FORCEINLINE uint32_t bsf(uint32_t x) {
        unsigned long res;
        _BitScanForward(&res, x);
        return res;
    }
#else
    #define FORCEINLINE __attribute__((always_inline)) inline
    #define NOINLINE __attribute__((noinline))
    #define ALIGN(n) __attribute__((aligned(n)))
#endif



#ifdef DALC
thread_local D_alc tree_dlc;
#endif
thread_local adaptive_slot finger_buffer[MAX_LEAF_KEY];
void* pmdk_alloc(size_t size, PMEMobjpool* ppop){
    PMEMoid paddr;
    pmemobj_alloc(ppop, &paddr, size, 0, nullptr, nullptr);
    void* return_ptr = pmemobj_direct(paddr);
    return return_ptr;
}

void pmdk_free(void* ptr){
    PMEMoid pmdk_ptr = pmemobj_oid(ptr);
    pmemobj_free(&pmdk_ptr);
}

static int ceiling(int key_num, int node_key){
    return (key_num + node_key - 1) / node_key;
}


void finger_differ(adaptive_slot* array, int* results, int count_result){
    int i=0;
    int temp[count_result];//1 for fake duplicate, 2 for real duplicate, 0 for uncheck
    memset(temp, 0, sizeof(int) * count_result);
    //divided conquer

    //both left
    for(; i<count_result-1 && results[i] < LEFT_KEY - 1; i++)
    {
        if(temp[i] != 0 )
            continue;
        for(int j= i + 1; j<count_result && results[j] < LEFT_KEY; j++)
        {
            if(array[results[j]].sub_slot.hash_byte == array[results[i]].sub_slot.hash_byte)
            {
                temp[j] = 1;
                temp[i] = 1;
            }
        }
    }
    //both right
    for(; i < count_result-1; i++)
    {
        for(int j = i +1; j<count_result; j++)
        {
            if(array[results[j]].sub_slot.hash_byte == array[results[i]].sub_slot.hash_byte)
            {
                temp[j] = temp[i] = 1;
            }
        }
    }

    for(int j=0; j<count_result; j++)
    {
        if(temp[j] != 1)
        {
            array[results[j]].sub_slot.access_bit = 0;
        }
    }
}

void qsort(int start, int end, Node_entry* temp, adaptive_slot* temp2){
    if(start >= end)
        return;
    Key_t sentinal = temp[start].one_key;
    void* sentinal_add;
    uint8_t hash_temp;
    int start_rem = start;
    int end_rem = end;
    while(start < end)
    {
            while(temp[end].one_key >= sentinal && start < end)
            {
                end--;
            } 
            std::swap(temp[end], temp[start]);
            std::swap(temp2[end], temp2[start]);
            while(temp[start].one_key <= sentinal && start < end)
            {
                start++;
            } 
            std::swap(temp[end], temp[start]);
            std::swap(temp2[end], temp2[start]);
    }
    qsort(start_rem, start, temp, temp2);
    qsort(start+1, end_rem, temp, temp2);
}


unsigned char hashfunc(uint64_t val)
{
  unsigned char hash = 123;
  int i;
  for (i = 0; i < sizeof(uint64_t); i++)
  {
    uint64_t octet = val & 0x00ff;
    val = val >> 8;

    hash = hash ^ octet;
    hash = hash * kFNVPrime64;
  }
  return hash;
}


DSeg::DSeg(){
    //split_log = (Log_buffer*) tree_alloc.n_allocate(sizeof(Log_buffer));
}

DSeg::~DSeg(){
    // std::cout<<"destruct"<<std::endl;
    // std::cout<<"log seg is "<<seg_count<<std::endl;
}

size_t timer_write=0;
size_t timer_split=0;
void DSeg::resize(size_t size){

#ifdef USE_PMDK
    void* log_start = pmdk_alloc(CHUNK_SIZE, pop);
#else
    if(tid == 114514)
    {   
        tid = gettid() % 56;
    }
#ifdef CXL
    void* log_start = malloc(size);
#else
    if(mmanagers[tid] == nullptr)
    {
        N_alc* new_manager = new N_alc(pm_pool_size);
        mmanagers[tid] = new_manager;
    }
    void* log_start = mmanagers[tid]->n_allocate(size, 1);
#endif
    //void* log_start = tree_alloc.n_allocate(size, 1);
#endif
    // if(log_vec!=nullptr)//append log end,link different segment
    // {
    //     log_vec[log_end].pptr = (Leaf_Node*)log_start;
    //     log_vec[log_end].op_kind = LOG_SEGMENT;
    //     persist(&log_vec[log_end]);
    // }
    
    log_vec = (Log_Node*)log_start;
    batch_count = 0;
    temp_offset = 0;
    seg_count++;
}

void* DSeg::append_one(size_t out_key, size_t out_val, void* out_pptr, char out_op, uint32_t version){
// #ifdef USE_PMDK
//     Log_Node* new_node = (Log_Node*)pmdk_alloc(sizeof(Log_Node), pop);
//     new_node->version_number = version;
//     new_node->pptr = out_pptr;
//     new_node->op_kind = out_op;
//     new_node->key = out_key;
//     new_node->val = out_val;
//     pmem_persist(new_node, sizeof(Log_Node));
//     return &new_node->key;
// #endif

    if(temp_offset + sizeof(Log_Node) + version >= log_end || log_vec == nullptr)
    {
        resize(CHUNK_SIZE - CACHELINE*2);
        vec_start = (size_t)log_vec;
        temp_offset = 0;
    }
#ifdef LONGVAL
    Log_Node* one_node = (Log_Node*)vec_start;
    one_node->version_number = version;
    one_node->pptr = out_pptr;
    one_node->op_kind = out_op;
    one_node->key = out_key;
    one_node->val = out_val;
    vec_start += sizeof(Log_Node);
    temp_offset += sizeof(Log_Node);
    //value
    char* pm_value = (char*)vec_start;
    memcpy(pm_value, (char*)out_val, version);
    vec_start += version;
    temp_offset += version;
    persist_range(one_node, sizeof(Log_Node) + version);
#else
    Log_Node* one_node = (Log_Node*)vec_start;
    //std::cout<<"one node is "<<one_node<<std::endl;
    //one_node->timestamp = out_timestamp;
    one_node->version_number = version;
    one_node->pptr = out_pptr;
    one_node->op_kind = out_op;
    one_node->key = out_key;
    one_node->val = out_val;
    vec_start += sizeof(Log_Node);
    temp_offset += sizeof(Log_Node);
    persist_range(one_node, sizeof(Log_Node));
    //persist(&log_vec[temp_offset]);
    // temp_offset++;
    // batch_count++;
    // if(batch_count == 8)
    // {
    //     persist_range(&log_vec[temp_offset-batch_count], 256);
    //     batch_count = 0;
    // }
    // if(log_end  == temp_offset)
    // {
    //     if(batch_count != 0)
    //     {
    //         persist_range(&log_vec[temp_offset - batch_count], sizeof(Log_Node) * batch_count);
    //         batch_count = 0;
    //     }
    //     resize(CHUNK_SIZE - CACHELINE * 2);
    //     temp_offset = 0;
    // }
#endif
    return &one_node->key;
}

//collect valid log and reuse the log space if possible
void DSeg::recycle(){
    //collect valied ptr field

    //update index ptr field

    //move to new block
}


void DSeg::replay(std::vector<finger_array> &finger_vec, std::vector<Key_t> &key_vec){
    int this_tid = gettid() % 56;
    for(auto iter=mmanagers[this_tid]->nlog_list.begin(); iter!=mmanagers[this_tid]->nlog_list.end(); iter++)
    {
        Base_Meta* one_page = iter->second;
        if(one_page->bitmap == 0)
        {
            continue;
        }
        size_t start_address = (size_t)iter->second->start_address;
        for(int i=0; i<log_end; i++)
        {
            Log_Node* kv_log = (Log_Node*)start_address;
            //D_Leaf* insert_leaf = (D_Leaf*)clht_get(elysia->ht, (size_t)kv_log->pptr);
            // if(insert_leaf->max_key >= kv_log->key && insert_leaf->min_key <= kv_log->key)//the right place to insert
            // {
            //     int insert_pos = __sync_fetch_and_add(&insert_leaf->max_use, 1);
            //     insert_leaf->finger_print[insert_pos].hash_byte = hashfunc(kv_log->key);
            //     insert_leaf->finger_print[insert_pos].ptr_field = (uint64_t)&kv_log->key;
            // }
            // else
            if(kv_log->version_number== 0)
                break;
            else
            {
                //insert into somewhere else
                //TODO add else place exactly
                finger_array new_finger;
                new_finger.hash_byte = hashfunc(kv_log->key);
                new_finger.ptr_field = (uint64_t)&kv_log->key;
                finger_vec.push_back(new_finger);
                key_vec.push_back(kv_log->key);
            }
            start_address += sizeof(Log_Node);
        }
    }
}






bool test_exist(const char* file_name){
    std::ifstream f1(file_name);
    if(f1.good())
        return true;
    return false;
}

void* inner_alloc(size_t sz){
    void* ptr;
    posix_memalign(&ptr, 64, sz);
    return ptr;
}


Tree::Tree(void* start_address){
#ifdef USE_PMDK
    if(test_exist("/mnt/pmem/zb_pmdk"))
    {
        std::cout<<"pmem file exist!"<<std::endl;
        exit(1);
    }
    
    pop = pmemobj_create("/mnt/pmem/zb_pmdk", "zbtree", POOL_SIZE, 0666);
#endif
#ifdef RECOVERY
    if(test_exist("/mnt/pmem/zbtree/nyx0"))
    {
        recover();
        return;
    }
#endif
    root = nullptr;
    std::cout<<"Inner node size is "<<sizeof(Inner_Node)<<std::endl;
    std::cout<<"DLeaf node size is "<<sizeof(D_Leaf)<<std::endl;
    std::cout<<"Log node size is "<<sizeof(Log_Node)<<std::endl;
    std::cout<<"hash fp "<<(int) fp_hash(0)<<std::endl;
    std::cout<<"Log buffer size is "<<sizeof(Log_buffer)<<std::endl;
    std::cout<<"PLeaf size is "<<sizeof(P_Leaf)<<std::endl;
    std::cout<<"finger array is "<<sizeof(finger_array)<<std::endl;
#ifdef DALC
            D_Leaf* new_meta = (D_Leaf*)tree_dlc.huge_malloc(sizeof(D_Leaf));
#else
            D_Leaf* new_meta = (D_Leaf*)malloc(sizeof(D_Leaf));
#ifdef SPACE
            count_dram += sizeof(D_Leaf);
#endif
#endif


#ifdef USE_PMDK
            //pmemobj_alloc(pop, &pmdk_ptr, sizeof(Leaf_Node), 0, nullptr, nullptr);
            P_Leaf* new_root = (P_Leaf*)pmdk_alloc(sizeof(P_Leaf), pop);
#else 
            //Leaf_Node* new_root = (Leaf_Node*)tree_alloc.n_allocate(sizeof(Leaf_Node));
            if(tid == 114514)
                tid = gettid() % run_thread;
#ifdef CXL
            P_Leaf* new_root = (P_Leaf*) malloc(sizeof(P_Leaf));
#else
            if(mmanagers[tid] == nullptr)
            {
                N_alc* new_alloc = new N_alc(pm_pool_size);
                mmanagers[tid] = new_alloc;
                
            }
            P_Leaf* new_root = (P_Leaf*)mmanagers[tid]->n_allocate(sizeof(P_Leaf));
#endif 
            //P_Leaf* new_root = (P_Leaf*)tree_alloc.n_allocate(sizeof(P_Leaf));
#endif
            new_root->next = nullptr;
            new_root->version_field = 0;
            //new_root->leaf_id = 114514;
            //insert into log
            //new_root->slot_all[0].one_key = key;
            //new_root->slot_all[0].val = (char*)val;
            //new_root->bitmap = 1;
            leaf_start = new_root;
            dstart = new_meta;
            new_meta->alt_ptr = new_root;
            new_meta->max_use = 0;
            new_meta->temp_insert = 0;
            //new_meta->min_key = ~0ULL;
            new_meta->max_key = 0;
            memset(new_meta->finger_print, 0, MAX_LEAF_KEY * 8);
            //new_meta->version_field = 0;
            //new_meta->key_max = key;
            root = (char*) new_meta;
#ifndef CXL
            // size_t* anchor = (size_t*)(mmanagers[tid]->start_address + 64);
            // anchor[0] = (size_t)new_root;
            // persist(anchor);
#endif
            persist_fence();
            //init log_buffer space
            std::cout<<"Panchor start is "<<leaf_start<<std::endl;
            std::cout<<"Mmanager id is "<<tid<<std::endl;
}

//find key from left most leaf, used for debug
void Tree::liner_clear(){
    // D_Leaf* start = dstart;
    // while(start!= nullptr)
    // {
    //     start->version_write = 0;
    //     start->version_read = 0;
    //     start = start->next;

    // }
}



void Tree::liner_find(Key_t key){
    D_Leaf* start = dstart;
    // Leaf_Node* start2 = (Leaf_Node*)start->alt_ptr;
    size_t count=0;
    size_t count2=0;
    size_t count3=0;
    size_t count4=0;
    while(start != nullptr)
    {

        // for(int i=0; i<start->max_use; i++)
        // {
        //   if(start->finger_print[i].sub_slot.control_bit > start->max_use && start->finger_print[i].sub_slot.access_bit == 0)
        //   {
        //       count++;
        //   }
        //   else
        //       count2++;
        //   if(start->finger_print[i].sub_slot.access_bit == 1)
        //       count3++;
        // }
        count2++;
        // if(start->version_write > write_threshold && start->version_read > read_threshold)
        //   count++;
        // if(start->version_write > write_threshold) 
        //   count3++;
        // if(start->version_read >  read_threshold)
        //   count4++;
        start = start->next;
    }
    std::cout<<"count is "<<count<<" "<<"count2 is "<< count2<<"  version write "<<count3 <<
     " version read "<<count4<<std::endl;
}



size_t Tree::find(Key_t key, int val_sz){
    tbb::speculative_spin_rw_mutex::scoped_lock lock_find;
    // if(tid == 114514)
    // {
    //     tid = gettid() % run_thread;
    //     if(workers[tid] == nullptr)
    //     {
    //         workers[tid] = new DSeg();
    //     }
    // }

Find_begin:
    int search_level = 0;
    if(temp_level == 0)//only have root
    {
        D_Leaf* root_meta = reinterpret_cast<D_Leaf*>(root);
        uint8_t fingerprint = hashfunc(key);
        for(int i=0; i<MAX_LEAF_KEY; i++)
        {
            if(root_meta->finger_print[i].sub_slot.hash_byte == fingerprint)
            {
                size_t* kv_ptr = (size_t*) root_meta->finger_print[i].sub_slot.ptr_field;
                if(kv_ptr[0] == key)
                  return kv_ptr[1];
            }
        }
        std::cout<<"key not existed"<<std::endl;
        return 0;
    }
    // lock_find.acquire(function_lock, false);
    Inner_Node* start_node = (Inner_Node*)root;

    //inner
#ifdef BREAKDOWN_FIND
    auto global_start = read_rdtsc();
#endif
    while(search_level <= temp_level-1)
    {

        prefetch0(start_node, sizeof(Inner_Node));
        {
#ifdef BREAKDOWN_FIND
            auto query_start = read_rdtsc();
#endif
            int start=0;
            int end = start_node->max_size - 1;
#ifdef BIN_SEARCH

            binary_search(key, start_node, start, end);
            if(end == -1)
            {
                start_node = reinterpret_cast<Inner_Node*>(start_node->node_key[1][start]);
                
            }
#endif
            for(int i=start; i<=end; i++)
            {
                if(start_node->node_key[0][i] >= key && i == 0)//left most case
                {
                    
#ifdef BREAKDOWN_FIND
                    auto query_end = read_rdtsc();
                    breakdown_search += query_end - query_start;
                    //next_value = start_node->left_most_ptr;
#endif
                    start_node = reinterpret_cast<Inner_Node*>(start_node->left_most_ptr);
                    //if(start_node == nullptr)
                    //    std::cout<<"find null in if 2"<<std::endl; 
                    break;
                }
                else if(i == end || start_node->node_key[0][i] < key && start_node->node_key[0][i+1] >= key)
                {
#ifdef BREAKDOWN_FIND
                    auto query_end = read_rdtsc();
                    breakdown_search += query_end - query_start;
#endif
                    start_node = reinterpret_cast<Inner_Node*>(start_node->node_key[1][i]);
                    break;
                }
            }
        }
        search_level++;  
    }
#ifdef BREAKDOWN_FIND
    auto global_end = read_rdtsc();
    //breakdown_alc += global_end - global_start;
#endif
    //leaf
    //some way to reduce query overhead 
    D_Leaf* leaf_meta = reinterpret_cast<D_Leaf*>(start_node);
    uint8_t version_before = leaf_meta->temp_insert;
    char* find_val = NULL;
    uint8_t fingerprint = hashfunc(key);
    //slow path search, caused by split process
    // lock_find.release();
    if(leaf_meta->temp_insert > 1)//is being split, find value from the alt buffer
    {
        // goto Find_begin;
#ifndef LONGVAL
        Node_entry* cow_buffer = (Node_entry*)leaf_meta->alt_ptr;
        for(int i=0; i<MAX_LEAF_KEY; i++)
        {
            if(cow_buffer[i].one_key == key)
            {
                return cow_buffer[i].val;
            }
        }
        return 0;
#endif

    }

    //fast path search, using fingerprint
    prefetch0(leaf_meta, sizeof(D_Leaf));
    for(int i=0; i<leaf_meta->max_use; i++)
    {
        if(leaf_meta->finger_print[i].sub_slot.hash_byte == fingerprint)
        {
            //size_t* kv_ptr = (size_t*)leaf_meta->finger_print[i].ptr_field;
            // if(kv_ptr[0] == key)
            //     return  (char*)kv_ptr[1];
#ifdef NO_COMPACT 
            size_t* kv_ptr = (size_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
            if(kv_ptr[0] == key)
            {
                //size_t v_ptr = (size_t)leaf_meta->finger_print[i].sub_slot.ptr_field + 16;
                return (char*)kv_ptr[1];
            }
                        
#else
                if(leaf_meta->finger_print[i].sub_slot.control_bit >= leaf_meta->max_use)
                {
                    // return (char*)leaf_meta->finger_print[i].val;
                    size_t lodging_key = leaf_meta->finger_print[leaf_meta->finger_print[i].sub_slot.control_bit].val;
                    if(lodging_key == key)
                    {
                        if(leaf_meta->finger_print[i].sub_slot.access_bit < 3)
                            leaf_meta->finger_print[i].sub_slot.access_bit++;
                        // workers[tid]->hit_count++;
                        // leaf_meta->version_read++;
                        return leaf_meta->finger_print[leaf_meta->finger_print[i].sub_slot.control_bit + 1].val;
                    }

                }
                else
                {
                    size_t* kv_ptr = (size_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
                    if(kv_ptr[0] == key)
                    {
                        if(leaf_meta->finger_print[i].sub_slot.access_bit < 3)
                            leaf_meta->finger_print[i].sub_slot.access_bit++;
                        // leaf_meta->version_read++;
                        // countshit++;
                        return  kv_ptr[1];
                    }
                        
                }
                //return (char*)leaf_meta->finger_print[i].ptr_field;

#endif
        }
    }
    // abort();
    return 0;

#ifdef DEBUG
    if(find_val == nullptr)
    {
        liner_find(key);
        std::cout<<"nothing find!"<<std::endl;
    }
#endif
    //shit_mutex.unlock();
}


uint64_t Tree::find_insert(Key_t key, Cursor* path_array){
    if(temp_level == 0)
    {
        return (uint64_t)root;
    }
    int search_level = 0;
    Inner_Node* start = (Inner_Node*) root;
    uint64_t next_value;//used for address->offset transfer
    int i;
    while(search_level <= temp_level - 1)//search non-leaf node,return leaf node
    {
        prefetch0(start, sizeof(Inner_Node));
        path_array[search_level].node = start;
        path_array[search_level].node_max = start->max_size;
        path_array[search_level].node_version = start->version_field;
#ifdef AVX_512
        {
#ifdef BREAKDOWN_FIND
            auto query_start = read_rdtsc();
#endif
            int size_max = start->max_size;
            int find_pos = linear_search_avx_512ul(start->node_key[0], size_max, key);
#ifdef BREAKDOWN_FIND
            auto query_end = read_rdtsc();
            breakdown_split += query_end - query_start;
#endif
            if(find_pos == -1)
            {
                start = reinterpret_cast<Inner_Node*>(start->left_most_ptr);
                path_array[search_level].node_pos = -1;
            }
            else
            {
                start = reinterpret_cast<Inner_Node*>(start->node_key[1][find_pos]);
                path_array[search_level].node_pos = find_pos;
            }
        }

#else
        {
            int start_pos = 0;
            int end_pos = path_array[search_level].node_max - 1;
#ifdef BIN_SEARCH_INSERT
            binary_search(key, start, start_pos, end_pos);
#endif

#ifdef BREAKDOWN_FIND
            auto query_start = read_rdtsc();
#endif
            for(i=start_pos; i<=end_pos; i++)
            {
                if(start->node_key[0][i] >= key && i == 0)//left most case
                {
#ifdef BREAKDOWN_FIND
            auto query_end = read_rdtsc();
            breakdown_split += query_end - query_start;
#endif

                    start = reinterpret_cast<Inner_Node*>(start->left_most_ptr);
                    path_array[search_level].node_pos = -1;
                    //if(start == nullptr)
                    //    std::cout<<"find null in if 2"<<std::endl; 
                    break;
                }
                else if(i == path_array[search_level].node_max - 1 || start->node_key[0][i] < key && start->node_key[0][i+1] >= key)
                {
#ifdef BREAKDOWN_FIND
            auto query_end = read_rdtsc();
            breakdown_split += query_end - query_start;
#endif

                    start = reinterpret_cast<Inner_Node*>(start->node_key[1][i]);
                    path_array[search_level].node_pos = i;
                    //if(start == nullptr)
                    //    std::cout<<"find null in if 1"<<std::endl;
                    break;
                }
            }
        }
#endif
        search_level++;
    }
    //end of inner, return leaf node
#ifdef DEBUG
    if(start == nullptr){
        std::cout<<"duplicate key outside loop"<<std::endl;
    }
#endif
    return (uint64_t)start;
}

bool Tree::insert(Key_t key, void* val, int value_len){
    tbb::speculative_spin_rw_mutex::scoped_lock lock_insert;
    memset(root_path, 0, temp_level);
restart:
    lock_insert.acquire(function_lock, false);
#ifdef BREAKDOWN_FIND
    auto search_start = read_rdtsc();
#endif
    uint64_t leaf_offset = find_insert(key, root_path);
#ifdef BREAKDOWN_FIND
    auto search_end = read_rdtsc();
    breakdown_search+=(search_end - search_start);
    auto except_search_start = read_rdtsc();
#endif
    D_Leaf* leaf_meta = (D_Leaf*) leaf_offset;
#ifdef DEBUG
    if(leaf_meta->pleaf == nullptr)//debug, output error
    {
        std::cout<<"error key, nullptr detacted"<<std::endl;
        abort();
    }
#endif
    bool lock_state = __sync_bool_compare_and_swap(&leaf_meta->temp_insert, 0, 1);
    if(!lock_state)//check than insert
    {   
        lock_insert.release();
        goto restart;
    }
    lock_insert.release();
    //now get real leaf
#ifdef BREAKDOWN_FIND
    auto check_start = read_rdtsc();
#endif
    
    //enter critical section
    prefetch0(leaf_meta, sizeof(D_Leaf));
    
    bool duplicate = false;
    uint8_t fingerprint = hashfunc(key);
    int compact_flag=0;
    for(int i=0; i<leaf_meta->max_use; i++)
    {
        if(leaf_meta->finger_print[i].sub_slot.hash_byte == fingerprint)
        {
            //countshit++;
            if(leaf_meta->finger_print[i].sub_slot.control_bit > leaf_meta->max_use)
            {
                size_t lodging_key = leaf_meta->finger_print[leaf_meta->finger_print[i].sub_slot.control_bit].val;
                if(lodging_key == key)
                    return false;
            }
            else
            { 
                size_t* kv_ptr = (size_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
                if(kv_ptr[0] == key)
                    return false;
            }

            // else
            // {
            //     leaf_meta->finger_print[i].sub_slot.access_bit = 1;
            //     duplicate = true;
            // }
        }
    }

#ifdef BREAKDOWN_FIND
    auto check_end = read_rdtsc();
    breakdown_check += check_end - check_start;
#endif
    if(leaf_meta->max_use < MAX_LEAF_KEY)
    {
#ifdef BREAKDOWN
        auto write_start = read_rdtsc();
#endif

        //first meta
        //uint32_t version_now = leaf_meta->version_field;
        if(tid == 114514)
            tid = gettid() % run_thread;
        if(workers[tid] == nullptr)
        {
            DSeg *sl = new DSeg();
            workers[tid] = sl;
        }
        void* kv_addr = workers[tid]->append_one(key, (size_t)val, leaf_meta->alt_ptr, LOG_INSERT, value_len);
        //void* kv_addr = sl.append_one(key, (size_t)val, (Leaf_Node*)leaf_meta->alt_ptr, LOG_INSERT, 1);
        int insert_pos = leaf_meta->max_use;
        leaf_meta->max_use++;
        if(leaf_meta->max_key < key)
            leaf_meta->max_key = key;
        //write inline data
        leaf_meta->finger_print[insert_pos].sub_slot.hash_byte = fingerprint;
        leaf_meta->finger_print[insert_pos].sub_slot.ptr_field = (uint64_t)kv_addr;
        leaf_meta->finger_print[insert_pos].sub_slot.control_bit = 0;
        leaf_meta->finger_print[insert_pos].sub_slot.access_bit = 0;
        leaf_meta->temp_insert = 0;
        //write log
        
        
#ifdef BREAKDOWN
        auto write_end = read_rdtsc();
        timer_write += write_end - write_start;
#endif
    }
    else
    {

        //leaf is full, need split
        P_Leaf* insert_leaf = (P_Leaf*)leaf_meta->alt_ptr;
        //prefetch0(insert_leaf, sizeof(Leaf_Node));
        //store kv to dram
        Node_entry dram_buffer[MAX_LEAF_KEY];
        int results[MAX_LEAF_KEY];
        int count_result = 0;
        for(int i=0; i<MAX_LEAF_KEY; i++)
        {
            uint64_t* kv_ptr = (uint64_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
            dram_buffer[i].one_key = kv_ptr[0];
            dram_buffer[i].val = kv_ptr[1];
            finger_buffer[i] = leaf_meta->finger_print[i];
            finger_buffer[i].sub_slot.control_bit = 0;
        }
        //sort
        
        qsort(0, MAX_LEAF_KEY - 1, dram_buffer, finger_buffer);
        leaf_meta->alt_ptr = dram_buffer;
        persist_fence();
        leaf_meta->temp_insert++;
        // for(int i=0; i<MAX_LEAF_KEY; i++)
        // {
        //     if(finger_buffer[i].sub_slot.access_bit == 1)
        //     {
        //         results[count_result] = i;//position
        //         count_result++;
        //     }
        // }
#ifdef BREAKDOWN
        auto start_split = read_rdtsc();
#endif
        // if(count_result != 0)//has duplicate fingerprint, need filter
        //     finger_differ(finger_buffer, results, count_result);
        //cow kv pair to log buffer
        //nt_64((char*)sl.split_log->buffer, (char*)dram_buffer, sizeof(Node_entry) * MAX_LEAF_KEY);
        //sl.split_log->alt = leaf_meta->version_field;
        //sl.split_log->pleaf = insert_leaf;
        //execute split process

  
        D_Leaf* leaf_meta2 = (D_Leaf*)split_leaf(insert_leaf, leaf_meta, finger_buffer, dram_buffer);
        persist(insert_leaf);
        persist(leaf_meta2->alt_ptr);
#ifdef BREAKDOWN
        auto end_split = read_rdtsc();
        timer_split += end_split - start_split;
#endif

        uint64_t split_key = dram_buffer[SPLIT_POS].one_key;
        
        if(temp_level == 0)//root case split
        {
            //shit_mutex.lock();
            lock_insert.acquire(function_lock);
            //shit_mutex.lock();
            //std::unique_lock<std::shared_mutex> smo_lock(mut_);
            Inner_Node* new_root = (Inner_Node*) malloc(sizeof(Inner_Node));
#ifdef SPACE
            count_dram += sizeof(Inner_Node);
#endif
            //Inner_Node* new_root = (Inner_Node*) inner_alloc(sizeof(Inner_Node));

            memset(new_root->node_key[0], 0xff, (MAX_LEAF_KEY + 1) * sizeof(size_t));
            new_root->node_key[0][0] = dram_buffer[SPLIT_POS].one_key;
            new_root->left_most_ptr = leaf_offset;
            new_root->node_key[1][0] = (uint64_t)leaf_meta2;
            new_root->max_size = 1;
            root = (char*) new_root;
            temp_level++;
           //temp_leaf++;
            new_root->version_field = 0;
            //lock_insert.release();
            leaf_meta->temp_insert = 0;
            leaf_meta->alt_ptr = insert_leaf;
            lock_insert.release();
#ifdef BREAKDOWN_FIND
            auto except_search_end = read_rdtsc();
            breakdown_split2+=(except_search_end - except_search_start);
#endif
            goto restart;
            //return true;
        }
        //std::unique_lock<std::shared_mutex> smo_lock22(mut_);
        memset(root_path, 0, temp_level);
        lock_insert.acquire(function_lock);
        find_insert(key, root_path);

        splitInnerNode(root_path, split_key, (uint64_t)leaf_meta, (uint64_t)leaf_meta2);
        leaf_meta->temp_insert = 0;
        leaf_meta->alt_ptr = insert_leaf;
        lock_insert.release();
        goto restart;
    }
#ifdef BREAKDOWN_FIND
    auto except_search_end = read_rdtsc();
    breakdown_split2+=(except_search_end - except_search_start);
#endif
    return true;
}

bool Tree::update(Key_t key, void* val, bool is_upsert){
    //update val ptr
    tbb::speculative_spin_rw_mutex::scoped_lock lock_update;
    uint8_t fingerprint = hashfunc(key);
Update_begin:
    lock_update.acquire(function_lock, false);
    Inner_Node* start_node = (Inner_Node*)root;
    int search_level = 0;
    while(search_level <= temp_level-1)
    {
        prefetch0(start_node, sizeof(Inner_Node));
        int start=0;
        int end = start_node->max_size - 1;
        for(int i=start; i<=end; i++)
        {
            if(start_node->node_key[0][i] >= key && i == 0)
            {
                start_node = reinterpret_cast<Inner_Node*>(start_node->left_most_ptr);
                break;
            }
            else if(i == end || start_node->node_key[0][i] < key && start_node->node_key[0][i+1] >= key)
            {
                start_node = reinterpret_cast<Inner_Node*>(start_node->node_key[1][i]);
                break;
            }
        }

        search_level++;
    }   

    D_Leaf* leaf_meta = (D_Leaf*) start_node;
    //check lock state of leaf
    if(leaf_meta->temp_insert > 1)
    {
        lock_update.release();
        goto Update_begin;
    }
    else
    {
        lock_update.release();
    }

    bool is_updated = false;
    for(int i=0; i<leaf_meta->max_use; i++)
    {
        if(leaf_meta->finger_print[i].sub_slot.hash_byte == fingerprint)
        {
            if(leaf_meta->finger_print[i].sub_slot.access_bit == 0)//newly
            {
                if(tid == 114514)
                {
                    tid = gettid() % run_thread;
                }
                if(workers[tid] == nullptr)
                {
                    DSeg* new_worker = new DSeg();
                    workers[tid] = new_worker;
                }
                workers[tid]->append_one(key, (size_t)val, leaf_meta->alt_ptr, LOG_UPDATE, 0);
                is_updated = true;
            }
            else
            {
                uint64_t* kv_ptr = (uint64_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
                if(kv_ptr[0] == key)
                {
                    kv_ptr[1] = (uint64_t)val;
                    persist(kv_ptr);
                    is_updated = true;                     
                    break;
                } 
            }
        }
    }
    if(!is_updated)
    {
        return false;
    }
    return true;
}

int Tree::scan(Key_t start_key, int scan_count, Key_t* scan_array){
    memset(root_path, 0, temp_level);
    tbb::speculative_spin_rw_mutex::scoped_lock lock_scan;
    int count = 0;
Retry_scan:
    lock_scan.acquire(function_lock, false);
    uint64_t leaf_offset = find_insert(start_key, root_path);
    D_Leaf* leaf_meta = (D_Leaf*)leaf_offset;
    //first check lock state
    //bool lock_state = __sync_bool_compare_and_swap(&leaf_meta->temp_insert, 0, 1);
    // if(!lock_state)
    // {     
    //     lock_scan.release();
    //     goto Retry_scan;
    // }
    lock_scan.release();
    while(count < scan_count && leaf_meta != nullptr)
    {
        if(leaf_meta->temp_insert > 1)
        {
Fast_scan:
            //countshit++;
            D_Leaf* sibling = leaf_meta->next;
            Node_entry* dram_buffer = (Node_entry*)leaf_meta->alt_ptr;
            for(int i=0; i<MAX_LEAF_KEY; i++)
            {
                if(start_key <= dram_buffer[i].one_key)
                {
                    scan_array[count] = (size_t)dram_buffer[i].val;
                    count++;
                    if(count >= scan_count)
                    {
                        return count;
                    }
                }
            }
            leaf_meta = sibling;
        }
        else
        {
            while(1)
            {
                if(leaf_meta->temp_insert > 1)
                    goto Fast_scan;
                if(__sync_bool_compare_and_swap(&leaf_meta->temp_insert, 0, 1))
                    break;
            }
            for(int i=0; i<leaf_meta->max_use; i++)
            {
                //int control_value = leaf_meta->finger_print[i].sub_slot.control_bit;
                if(i + LEFT_KEY + 1 >= leaf_meta->max_use && i + LEFT_KEY + 1 < MAX_LEAF_KEY)//value can be found in dram
                {
                    scan_array[count] = leaf_meta->finger_print[i + LEFT_KEY].val;
                    count++;
                }
                else
                {
                    uint64_t* kv_ptr = (uint64_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
                //if(start_key <= kv_ptr[0])
                    {
                        scan_array[count] = kv_ptr[1];
                    //scan_array[count] = leaf_meta->finger_print[i].ptr_field;
                        count++;
                    }
                }
                if(count >= scan_count)
                {    
                    leaf_meta->temp_insert = 0;
                    return count;
                }      
            }
            leaf_meta->temp_insert = 0;
        }
        if(leaf_meta->next == nullptr)
        {
            break;
        }
        //move to next leaf node
        D_Leaf* sibling = leaf_meta->next;
        //require next lock
        //while(!__sync_bool_compare_and_swap(&sibling->temp_insert, 0, 1));
        //release temp lock
        //leaf_meta->temp_insert = 0;
        //move on
        leaf_meta = sibling;
    }
    // leaf_meta->temp_insert = 0;
    return count;
}

//simply clear key, without inner node merge
bool Tree::simply_remove(Key_t key){
    tbb::speculative_spin_rw_mutex::scoped_lock simply_remove_lock;
Retry_simply_remove:    
    simply_remove_lock.acquire(function_lock, false);
    uint64_t leaf_offset = find_insert(key, root_path);
    D_Leaf* leaf_meta = (D_Leaf*)leaf_offset;
    bool lock_state = __sync_bool_compare_and_swap(&leaf_meta->temp_insert, 0, 1);
    if(!lock_state)
    {
        simply_remove_lock.release(); 
        goto Retry_simply_remove;
    }
    else
        simply_remove_lock.release();
    
    prefetch0(leaf_meta, sizeof(D_Leaf));
    uint8_t fingerprint = hashfunc(key);
    uint64_t remove_val;
    for(int i=0; i<leaf_meta->max_use; i++)
    {
        if(leaf_meta->finger_print[i].sub_slot.hash_byte == fingerprint)
        {
            uint64_t* kv_ptr = (uint64_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
            if(kv_ptr[0] == key)
            {
                uint32_t * log_start = (uint32_t*)kv_ptr - 16;
                log_start[0] = 0;//invalid log;
                if(tid == 114514)
                {
                    tid = gettid() % run_thread;
                }
                if(mmanagers[tid] == nullptr)
                {
                    N_alc* new_alloc = new N_alc(pm_pool_size);
                    mmanagers[tid] = new_alloc;
                }
                mmanagers[tid]->n_free(log_start);
                //sl.append_one(key, remove_val, (Leaf_Node*)leaf_meta->alt_ptr, LOG_REMOVE, 1);
                int j=leaf_meta->max_use-1;
                if(i != j)
                    leaf_meta->finger_print[i] = leaf_meta->finger_print[j];
                leaf_meta->max_use--;
                // int j=i;
                // for(; j<leaf_meta->max_use; j++)
                // {
                //     leaf_meta->buffer[j] = leaf_meta->buffer[j+1];
                //     leaf_meta->hash_byte[j] = leaf_meta->hash_byte[j+1];
                // }
                // leaf_meta->hash_byte[j] = 0;
                // leaf_meta->max_use--;
                
                if(leaf_meta->max_use == 0)
                {
                    P_Leaf* pleaf = (P_Leaf*)leaf_meta->alt_ptr;
                    pleaf->max_key = 0;
                    pleaf->version_field++;
                    persist(pleaf);
                    persist_fence();
                }
                leaf_meta->temp_insert = 0;
                return true;
            }
        }
    }
    
    leaf_meta->temp_insert = 0;
    return false;

}   


bool Tree::remove(Key_t key){
//     tbb::speculative_spin_rw_mutex::scoped_lock remove_lock;
//     D_Leaf* left_sibling=nullptr;
// Retry_remove:
//     remove_lock.acquire(function_lock, false);
//     uint64_t leaf_offset = find_insert(key, root_path);
//     D_Leaf* leaf_meta = (D_Leaf*) leaf_offset;
//     bool lock_state = __sync_bool_compare_and_swap(&leaf_meta->temp_insert, 0, 1);
//     if(!lock_state)//check than insert
//     {   
//         remove_lock.release();
//         goto Retry_remove;
//     }
//     else
//     {
//         if(leaf_meta->max_use == 1 && leaf_meta->pleaf != leaf_start)
//             left_sibling = beforeMerge(root_path);    
//         remove_lock.release();
//     }
    
//     //start concurrent
//     Leaf_Node* this_leaf = leaf_meta->pleaf;
//     uint8_t fingerprint = fp_hash(key);
// #ifdef DEBUG
//     if(this_leaf == nullptr)
//     {
//         std::cout<<"error, nullptr detected"<<std::endl;
//         return false;
//     }
// #endif
//     for(int i=0; i<leaf_meta->max_use; i++)
//     {
//         if(leaf_meta->hash_byte[i] == fingerprint)
//         {
//             if(leaf_meta->buffer[i].one_key == key)
//             {
//                 int j=i;
//                 for(; j<leaf_meta->max_use; j++)
//                 {
//                     leaf_meta->buffer[j] = leaf_meta->buffer[j+1];
//                     leaf_meta->hash_byte[j] = leaf_meta->hash_byte[j+1];
//                 }
//                 leaf_meta->hash_byte[j] = 0;
//                 leaf_meta->max_use--;
//                 break;
//             }
//         }
//     }
    
//     if(leaf_meta->max_use == 0)// node delete
//     {
//         if(left_sibling == nullptr)//delete the left most ptr
//         {
//             leaf_start = left_sibling->next->pleaf;
//         }
//         else//update left sibling 's ptr than continue delete
//         {
//             left_sibling->next = leaf_meta->next;
//             left_sibling->pleaf->next = leaf_meta->next->pleaf;
//             persist(left_sibling->pleaf);
//             left_sibling->temp_insert = 0;
//         }
// #ifdef USE_PMDK
//         pmdk_free(leaf_meta->pleaf);
// #else
//         tree_alloc.n_free(leaf_meta->pleaf, sizeof(Leaf_Node));
// #endif

// #ifdef DALC
//         tree_dlc.huge_free(leaf_meta);
// #else
//         free(leaf_meta); 2376117
// #endif
//         remove_lock.acquire(function_lock, false);
//         mergeInnerNode(root_path);
//         remove_lock.release();
//     }

}



uint64_t Tree::split_leaf(P_Leaf* leaf, D_Leaf* leaf_meta, adaptive_slot* dram_buffer, Node_entry* keys){
    //split left, return right
#ifdef USE_PMDK
    P_Leaf* new_leaf = (P_Leaf*)pmdk_alloc(sizeof(P_Leaf), pop);
#else
    if(tid == 114514)
        tid = gettid() % run_thread;
#ifdef CXL
    P_Leaf* new_leaf =(P_Leaf*) malloc(sizeof(P_Leaf));
#else
    if(mmanagers[tid] == nullptr)
    {
        N_alc *new_manager = new N_alc(pm_pool_size);
        mmanagers[tid] = new_manager;
    }
    P_Leaf* new_leaf = (P_Leaf*)mmanagers[tid]->n_allocate(sizeof(P_Leaf));
#endif
#endif
    D_Leaf* new_meta = (D_Leaf*) malloc(sizeof(D_Leaf));
#ifdef SPACE
    count_dram += sizeof(D_Leaf);
#endif

    D_Leaf* old_meta = leaf_meta;
    //first sync with old leaf
    int compact_pos = MAX_LEAF_KEY - 1;
    int do_nothing = 1;
    for(int i=0; i<LEFT_KEY; i++)
    {
        old_meta->finger_print[i] = dram_buffer[i];
        
        if(compact_pos - 1 >= LEFT_KEY && old_meta->finger_print[i].sub_slot.access_bit > 1 )
        {
          old_meta->finger_print[compact_pos].val = keys[i].val;
          compact_pos--;
          old_meta->finger_print[compact_pos].val = keys[i].one_key;
          old_meta->finger_print[i].sub_slot.control_bit = compact_pos;
          compact_pos--;
        }
    }

    if(compact_pos - 1 >= LEFT_KEY)
    {
        for(int i=0; i<LEFT_KEY; i++)
        {
            if(compact_pos - 1 >= LEFT_KEY && old_meta->finger_print[i].sub_slot.access_bit <= 1)
            {
                old_meta->finger_print[compact_pos].val = keys[i].val;
                compact_pos--;
                old_meta->finger_print[compact_pos].val = keys[i].one_key;
                old_meta->finger_print[i].sub_slot.control_bit = compact_pos;
                compact_pos--;
            }
        }
    }
    //next sync with new leaf
    compact_pos = MAX_LEAF_KEY - 1;
    for(int i=RIGHT_KEY; i<MAX_LEAF_KEY; i++)
    {
        new_meta->finger_print[i-LEFT_KEY] = dram_buffer[i];
        
        if(compact_pos - 1 >= LEFT_KEY && new_meta->finger_print[i-LEFT_KEY].sub_slot.access_bit > 1)
        {
          new_meta->finger_print[compact_pos].val = keys[i].val;
          compact_pos--;
          new_meta->finger_print[compact_pos].val = keys[i].one_key;
          new_meta->finger_print[i-LEFT_KEY].sub_slot.control_bit = compact_pos;
          compact_pos--;
        }     
    }

    if(compact_pos - 1 >= LEFT_KEY)
    {
        for(int i=0; i<LEFT_KEY; i++)
        {
            if(compact_pos - 1 >= LEFT_KEY && new_meta->finger_print[i].sub_slot.access_bit <= 1)
            {
                new_meta->finger_print[compact_pos].val = keys[i + LEFT_KEY].val;
                compact_pos--;
                new_meta->finger_print[compact_pos].val = keys[i + LEFT_KEY].one_key;
                new_meta->finger_print[i].sub_slot.control_bit = compact_pos;
                compact_pos--;
            }
        }
    }

    //update dram metadata
    new_meta->max_use = RIGHT_KEY;
    //new_meta->alt_ptr = new_leaf;
    new_meta->temp_insert = 0;
    new_meta->next = old_meta->next;
    new_meta->alt_ptr = new_leaf;
    new_meta->max_key = keys[MAX_LEAF_KEY-1].one_key;

    old_meta->max_use = LEFT_KEY;
    old_meta->next = new_meta;
    old_meta->max_key = keys[LEFT_KEY - 1].one_key;
    // old_meta->version_write++;
    //update pmem metadata
    new_leaf->version_field = 0;
    new_leaf->next = leaf->next;
    new_leaf->max_key = new_meta->max_key;

    leaf->next = new_leaf;
    leaf->version_field++;
    leaf->max_key = old_meta->max_key;

    return (uint64_t)new_meta;
}



// lock the left sibling ptr of current leaf node
D_Leaf* Tree::beforeMerge(Cursor* path_array)
{
    if(temp_level == 0)
        return nullptr;
    D_Leaf* left_sibling;
    if(root_path[temp_level - 1].node_pos == -1)
    {
        int stop_level = temp_level - 1;
        while(stop_level >=0 && root_path[stop_level].node_pos == -1)
            stop_level--;
        if(stop_level >= 0)
        {
            Inner_Node* re = (Inner_Node*)root_path[stop_level].node;
            if(root_path[stop_level].node_pos == 0)//left sibling is in left_most ptr
                re = (Inner_Node*)re->left_most_ptr;
            else//left sibling is node_pos - 1 ptr
                re = (Inner_Node*)re->node_key[1][root_path[stop_level].node_pos - 1];
            for(int i=stop_level+1; i<=temp_level - 1; i++)
            {
                re = (Inner_Node*)re->node_key[1][re->max_size-1];
            }
            left_sibling = (D_Leaf*)re;
        }
    }
    else
    {
        Inner_Node* ancestor = (Inner_Node*) root_path[temp_level - 1].node;
        left_sibling = (D_Leaf*)ancestor->node_key[1][root_path[temp_level-1].node_pos - 1];
    }
    while(!__sync_bool_compare_and_swap(&left_sibling->temp_insert, 0, 1));
    return left_sibling;
}

void Tree::splitInnerNode(Cursor* root_path, Key_t split_key, uint64_t left_ptr, uint64_t right_ptr){
    int last_level = temp_level -1;//start from last non-leaf level
    Key_t this_level_key = split_key;
    while(last_level>=0)
    {
        Inner_Node* this_node = reinterpret_cast<Inner_Node*>(root_path[last_level].node);
        prefetch0(this_node, sizeof(Inner_Node));
        int this_pos = root_path[last_level].node_pos;
        uint32_t this_max = this_node->max_size;
        bool success = false;
        if(this_pos == -1)//insert into left most ptr
        {
            for(int i=this_node->max_size - 1; i>=0; i--)
            {
                this_node->node_key[1][i+1] = this_node->node_key[1][i];
                this_node->node_key[0][i+1] = this_node->node_key[0][i];
                
            }
            this_node->node_key[1][0] = right_ptr;
            this_node->node_key[0][0] = this_level_key;
            
        }
        else if(this_pos == this_max - 1)//insert into right most directly
        {
            this_node->node_key[1][this_pos + 1] = right_ptr;
            this_node->node_key[0][this_pos + 1] = this_level_key;
        }
        else
        {
            //std::copy(this_entry+this_pos+1, this_entry+this_max, this_entry+this_pos+2);
            //std::copy(this_node->node_key[this_pos + 1], this_node->node_key[this_max], this_node->node_key[this_pos + 2]);
            for(int i=this_node->max_size - 1; i>this_pos; i--)//should insert into right pos
            {
                this_node->node_key[1][i+1] = this_node->node_key[1][i];
                this_node->node_key[0][i+1] = this_node->node_key[0][i];
                
            }
            this_node->node_key[1][this_pos+1] = right_ptr;
            this_node->node_key[0][this_pos+1] = this_level_key;
        }
        //node pos record search process's decision
        //first, insert newly key, inner node has one extra slot for shift
        //shift_write(this_entry, this_node->max_size, this_level_key, (char*)right_ptr);
        //bit_set(this_node->bitmap, this_max, 1);
        this_node->max_size++;
        if(this_node->max_size>MAX_LEAF_KEY)//need split
        {
            //std::cout<<"split inner "<<std::endl;
            auto this_entry = this_node->node_key;
            this_level_key = this_entry[0][SPLIT_POS+1];//get split key
            uint64_t this_val = this_entry[1][SPLIT_POS+1];
#ifdef DALC
            Inner_Node* new_node = (Inner_Node*)tree_dlc.huge_malloc(sizeof(Inner_Node));
#else
            Inner_Node* new_node = (Inner_Node*)malloc(sizeof(Inner_Node));
#ifdef SPACE
            count_dram+= sizeof(Inner_Node);
#endif
            //Inner_Node* new_node = (Inner_Node*)inner_alloc(sizeof(Inner_Node));
#endif       
            
            memset(new_node->node_key[0] + RIGHT_KEY, 0xff, sizeof(size_t) * (LEFT_KEY + 1));

            std::copy(this_entry[0]+LEFT_KEY+1 , this_entry[0]+MAX_LEAF_KEY+1, new_node->node_key[0]);
            std::copy(this_entry[1]+LEFT_KEY+1 , this_entry[1]+MAX_LEAF_KEY+1, new_node->node_key[1]);
            memset(this_entry[0] + LEFT_KEY, 0xff, sizeof(size_t) * (LEFT_KEY));
            memset(this_entry[1] + LEFT_KEY, 0, sizeof(size_t) * (LEFT_KEY));
            new_node->max_size = RIGHT_KEY;
            this_node->max_size = LEFT_KEY;
            this_node->version_field++;
            //split key's son goes to right's left most ptr
            new_node->left_most_ptr = this_val;
            new_node->version_field = 0;
            right_ptr = (uint64_t)new_node;
            if(last_level == 0)// root case, new root required
            {
#ifdef DALC
                Inner_Node* new_root = (Inner_Node*)tree_dlc.huge_malloc(sizeof(Inner_Node));      
#else
                //Inner_Node* new_root = (Inner_Node*) inner_alloc(sizeof(Inner_Node));
                Inner_Node* new_root = (Inner_Node*)malloc(sizeof(Inner_Node));
#ifdef SPACE
                count_dram += sizeof(Inner_Node);
#endif
#endif
                memset(new_root->node_key[0], 0xff, sizeof(size_t)* (MAX_LEAF_KEY+1));
                //Inner_Node* new_root = (Inner_Node*)malloc(sizeof(Inner_Node));
                new_root->node_key[0][0] = this_level_key;
                new_root->node_key[1][0] = right_ptr;
                new_root->max_size = 1;
                new_root->left_most_ptr = (uint64_t)root;
                new_root->version_field = 0;
                //new_root->bitmap[0]=1;
                persist_fence();
                root = (char*)new_root;
                temp_level++;
                persist_fence();
                //this_node->version_field[0] = 0;
                //this_node->version_field++;
                new_root->version_field = 0;
                return;   
            }
            last_level--;
            //this_node->version_field[0] = 0; // unlock this node
        }
        else //no split in this token, inner split finish
        {
            break;
        }
    }
    return;
}

void Tree::mergeInnerNode(Cursor* root_path){
    Inner_Node* merge_start = (Inner_Node*)root_path[temp_level - 1].node;
    Key_t merge_key = merge_start->node_key[0][root_path[temp_level-1].node_pos];
    int merge_level = temp_level - 1;
    while(merge_level != 0)
    {
        int this_pos = root_path[merge_level].node_pos;
        if(merge_start->max_size == 1)//onlt one key, may be delete this node, move to next level
        {
            if(this_pos == -1)
            {
                if(merge_start->node_key[0][0] == ~0ULL)//completly empty, remove it
                {
                    free(merge_start);
                    merge_level--;
                }
                else//reserve one ptr, mark the tombstone
                {
                    merge_start->node_key[0][0] = ~0ULL;
                    merge_start->left_most_ptr = merge_start->node_key[1][0];
                    break;
                }
            }
            else//mark a tombstone and reserve left most ptr
            {
                merge_start->node_key[0][0] = ~0ULL;
                merge_start->node_key[0][1] = 0;
                break;
            }
        }
        else// still have space, reserve this node 
        {
            
            if(this_pos == -1)//delete left most, means key[0] is no longer needed
            {
                merge_start->left_most_ptr = merge_start->node_key[1][0];
                for(int i=1; i<merge_start->max_size; i++)
                {
                    merge_start->node_key[0][i-1] = merge_start->node_key[0][i];
                    merge_start->node_key[1][i-1] = merge_start->node_key[1][i];
                }
            }
            else if(this_pos == merge_start->max_size - 1)//delete right most, mark a tomb stone
            {
                merge_start->node_key[0][this_pos] = ~0ULL;
                merge_start->node_key[1][this_pos] = 0;
            }
            else//normal case, simply shift
            {
                for(int i=this_pos+1; i<merge_start->max_size - 1; i++)
                {
                    merge_start->node_key[0][i-1] = merge_start->node_key[0][i];
                    merge_start->node_key[1][i-1] = merge_start->node_key[1][i];
                }
                merge_start->node_key[0][merge_start->max_size] = ~0ULL;
                merge_start->node_key[1][merge_start->max_size] = 0;
            }
            merge_start->max_size--;
            break;
        }
    }
    return;
}




void Tree::bulkload(clht_t* elysia, size_t leafcount){
    std::vector<Key_t> min_key_array;
    
    //D_Leaf* danchor = (D_Leaf*)clht_get(elysia->ht, (size_t)leaf_start);
    //size_t leaf_count = clht_size(elysia->ht);
    size_t leaf_count = leafcount;
    P_Leaf* panchor = (P_Leaf*)leaf_start;
    //std::cout<<node_count<<std::endl;
    //get min key
    D_Leaf* danchor = (D_Leaf*)clht_get(elysia->ht, (size_t)leaf_start);
    //size_t min_key_before = danchor->min_key;
    size_t max_key_before = danchor->max_key;
    size_t count_anchor = 0;
    // while(danchor!=nullptr){
    //     if(danchor->max_use != 0)
    //         abort();
    //     danchor = danchor->next;
    //     panchor = panchor->next;
    //     if(danchor == nullptr)
    //         break;
    //     // size_t min_key_after = danchor->min_key;
    //     // size_t max_key_after = danchor->max_key;
    //     // if(max_key_before > min_key_after) // unorder state detected
    //     // {
    //     //     count_anchor++;
    //     // }
    //     // min_key_before = min_key_after;
    //     // max_key_before = max_key_after;
    //     //count_anchor++;
    //     //panchor = panchor->next;
    // }
    std::cout<<"start bulk loading!"<<std::endl;
    //std::cout<<"anchor count is "<<count_anchor<<std::endl;
    //return;
    //init necessary parameter
    int load_level = 0;//the total load level
    int every_level[MAX_TREE_LEVEL];//every level's node number
    void* every_ptr[MAX_TREE_LEVEL];//every level's temp node ptr
    every_ptr[0] = danchor;
    for(int i=1; i<MAX_TREE_LEVEL; i++)
        every_ptr[i] = nullptr;
    int every_offset[MAX_TREE_LEVEL];
    int max_node = ceiling(leaf_count, MAX_LEAF_KEY+ 1); //border for bulkloading
    every_level[0] = leaf_count;
    //init left most node for every level
    for(int i=1; i<MAX_TREE_LEVEL && max_node > 1; i++)
    {
        load_level = i;
        every_offset[i] = 0;
        every_level[i] = ceiling(every_level[i-1], MAX_LEAF_KEY + 1);
        every_ptr[i] = malloc(sizeof(Inner_Node));
        memset(every_ptr[i], 0, sizeof(Inner_Node));
        // Inner_Node* left_node = (Inner_Node*)every_ptr[i];
        // left_node->left_most_ptr = (uint64_t)every_ptr[i - 1];
        //9533237, 307524, 9921, 321, 11, 1
        // left_node->max_size = 0;
        max_node = every_level[i];
    }
    root = (char*)every_ptr[load_level];
    temp_level = load_level;
    //D_Leaf* danchor = (D_Leaf*)clht_get(elysia->ht, (size_t)leaf_start);
    //start traverse
    int inner_count = 5;
    for(int i=0; i<leaf_count; i++)
    {  
        // if(i == leaf_count - 1)
        // {
        //      int stop = 1;
        // }    
        //size_t child_max_key = danchor->max_key;//left most ptr is max_key, other is min key
        //split key is the left child's max key
        size_t child_max_key = danchor->max_key;
        // size_t max_use = danchor->max_use;
        // if(max_use != 0)
        // {
        //     std::cout<<"error!!"<<std::endl;
        // }
        // if(i == 9235209)
        // {
        //     int stop2 = 114514;
        // }
        size_t left_child_ptr = (size_t)danchor;
        size_t right_child_ptr = (size_t)danchor->next;
        int j = 1;
        //for(int j=1; j<=load_level; j++)
        while(1)
        {
            //Inner_Node* temp_node = (Inner_Node*)(every_ptr[j] + every_offset[j]);
            Inner_Node* temp_node = (Inner_Node*)every_ptr[j];
            if(temp_node->left_most_ptr == 0)//empty node, link to left most
            {
                temp_node->left_most_ptr = left_child_ptr;
                temp_node->node_key[0][0] = child_max_key;
                temp_node->node_key[1][0] = right_child_ptr;
                temp_node->max_size++;
                //left most key doesn't need to move to upper level
                //update child ptr before move to uppper level
                //child_ptr = (size_t) temp_node;
                break;
            }
            else//normal case, insert key and link child
            {
                //both key and ptr updated, now move to next slot
                if(temp_node->max_size == MAX_LEAF_KEY)
                {
                    //explicated, move to upper level, key is same, only child ptr updated
                    left_child_ptr = (size_t) every_ptr[j];
                        //create new inner node for this level
                    every_ptr[j] = malloc(sizeof(Inner_Node));
                    memset(every_ptr[j], 0, sizeof(Inner_Node));
                    Inner_Node* new_node = (Inner_Node*)every_ptr[j];
                    new_node->left_most_ptr = right_child_ptr;
                    right_child_ptr = (size_t) every_ptr[j];
                    every_offset[j]++;
                    //inner_count++;
                    if(every_offset[3] == 160 && j >=3)
                        int stop = 1;
                    //temp_node = (Inner_Node*) every_ptr[j];
                    //update level307464 9918 319 10
                    j++;
                }
                else
                {
                    temp_node->node_key[0][temp_node->max_size] = child_max_key;
                    temp_node->node_key[1][temp_node->max_size] = right_child_ptr;
                    temp_node->max_size++;
                    break;
                }
            }
        }
        danchor = danchor->next;
        if(danchor->next == nullptr)
            break;
    }
    //the right-most anchor is reserved, it will be used to balance the inner node key
    for(int i=1; i<=temp_level; i++)
    {
        Inner_Node* this_node = (Inner_Node*) every_ptr[i];
        if(this_node->max_size == 0)
        {
            this_node->node_key[0][0] = danchor->max_key;
            this_node->max_size++;
            break;
        }
    }
    //update root parameter
    // Inner_Node* border = (Inner_Node*)every_ptr[1];
    // if(border->node_key[0][border->max_size - 1] != 0
    //     && border->node_key[1][border->max_size - 1] == 0)
    // {
    //     border->max_size--;
    // }
    for(int i=1; i<temp_level; i++)
        std::cout<<every_offset[i]<<" ";
    std::cout<<std::endl;
    return;
}


void Tree::recover(){
#ifdef RECOVERY
    if(root != nullptr)
    {
        std::cout<<"no heap, fresh start"<<std::endl;
        return;
    }
// #ifdef NO_ELY
//     clht_t* ely;
//     Leaf_Node* anchor;
//     size_t thread_num = 56;
//     size_t recover_anchor=0;
//     //parallel recover pm block
//     #pragma omp parallel num_threads(thread_num)
//     {
//         int thread_num = omp_get_thread_num();
//         tree_alloc.recover_init(ely, recover_anchor, thread_num);
//     }
//     leaf_start = (Leaf_Node*)recover_anchor;
//     //bulkload(ely);
//     return;
// #else
    //basic init
    P_Leaf* anchor;
    size_t thread_num = 56;
    //parallel recover pm block
    std::thread threads[thread_num];
    for(int i=0; i<thread_num; i++)
    {
        mmanagers[i] = new N_alc(pm_pool_size);
        workers[i] = new DSeg();
    }
#ifdef IGNORE
    //ignore recover, re-construct the tree from nothing
    int tid = gettid() % thread_num;
    //first root
    temp_level=0;
    D_Leaf* new_meta =(D_Leaf*) malloc(sizeof(D_Leaf));
    new_meta->max_use = 0;
    new_meta->temp_insert = 0;
    //new_meta->min_key = ~0ULL;
    new_meta->max_key = 0;
    root = (char*)new_meta;

    for(int i=0; i<thread_num; i++)
    {
        threads[i] = std::thread([=](){
            int this_tid = gettid() % thread_num;
            mmanagers[this_tid]->ignore_recover();
dummy_restart:
            thread_fence.lock();
            if(new_meta->alt_ptr == nullptr)
            {
                P_Leaf* new_root = (P_Leaf*)mmanagers[this_tid]->n_allocate(sizeof(P_Leaf));
                new_root->next = nullptr;
                new_root->version_field = 0;
                persist_fence();
                new_meta->alt_ptr = new_root;
            }
            thread_fence.unlock();
            for(auto iter=mmanagers[this_tid]->nlog_list.begin(); iter!= mmanagers[this_tid]->nlog_list.end(); iter++)
            {
                Base_Meta* block_address = iter->second;

                size_t offset = (size_t)block_address->start_address;
                if(block_address->bitmap == 0)
                    continue;
                // void* kv_start_address = offset;
                for(int j=0; j<workers[i]->log_end; j++)
                {
                    Log_Node* kv_log = (Log_Node*)offset;
                    if(kv_log->version_number == 0)
                        break;
                    else
                    {
                        size_t key = kv_log->key;
                        void* value = &kv_log->key;
                        quick_insert(key, value); 
                    }
                    offset += sizeof(Log_Node);
                }

            }
        });
    }
    for(int i=0; i<thread_num; i++)
    {
        threads[i].join();
    }
    return;
#endif
    clht_t* ely = clht_create(32*1024*64);
    assert(ely != NULL);
    for(int i=0; i<thread_num; i++)
    {
        threads[i] = std::thread([=](){
            int this_tid = gettid() % thread_num;
            size_t rec = 0;
            mmanagers[this_tid]->recover_init(ely, rec, thread_num);
            if(rec != 0 )
            {
                leaf_start = (P_Leaf*)rec;
                dstart = (D_Leaf*)clht_get(ely->ht, (size_t)leaf_start);
            }
                
            //mmanagers[tid]->link_ptr(hash);
        });
    }
    for(int i=0; i<thread_num; i++)
    {
        threads[i].join();
    }
    std::cout<<"finish init, size of ely is "<<clht_size(ely->ht)<<std::endl;
    //std::cout<<"atomic size is "<<leaf_number<<std::endl;
    std::cout<<"panchor is "<<leaf_start<<std::endl;
    dstart = (D_Leaf*)clht_get(ely->ht, (size_t)leaf_start);
    std::cout<<"dleaf size is "<<dstart->max_key<<std::endl;
    if(dstart->max_key == 0)
        abort();
    std::thread link_threads[thread_num];
    for(int i=0; i<thread_num; i++)
    {
        link_threads[i] = std::thread([=](){
            int this_tid = gettid() % thread_num;
            mmanagers[this_tid]->link_ptr(ely);
        });
    }
    for(int i=0; i<thread_num; i++)
    {
        link_threads[i].join();
    }
    // #pragma omp parallel num_threads(thread_num)
    // {
    //     int thread_num = omp_get_thread_num();
    //     tree_alloc.recover_init(ely, recover_anchor, thread_num);
    // }
    //leaf_start = (P_Leaf*)recover_anchor;
    // std::cout<<"finish init, size of ely is "<<clht_size(ely->ht)<<std::endl;
    // //std::cout<<"atomic size is "<<leaf_number<<std::endl;
    // std::cout<<"panchor is "<<leaf_start<<std::endl;
    //std::cout<<"origin shit size is "<<shit_size<<std::endl;
    //link all the ptr for dleaf
    //size_t node_count = clht_size(ely->ht);
    //Node_entry* unsort = (Node_entry*)malloc(sizeof(Node_entry) * node_count);
    //exit(1);
    // #pragma omp parallel num_threads(thread_num)
    // {
    //     tree_alloc.link_ptr(ely);
    // }
    //std::cout<<"link done"<<std::endl;
    //return;
    //bulk load DRAM inner node and reply kv pair in log
    //TODO : async for bulk load and log reply
    bulkload(ely, clht_size(ely->ht));
    std::cout<<"start kv replay"<<std::endl;
    std::thread threads2[thread_num];
    for(int i=0; i<thread_num; i++)
    {
        threads2[i] = std::thread([&](){
            std::vector<Key_t> key_vec;
            std::vector<finger_array> finger_vec;
            int this_tid = gettid()%thread_num;
            if(workers[this_tid] == nullptr)
            {
                DSeg* new_sl = new DSeg();
                workers[this_tid] = new_sl;
            }
            workers[this_tid]->replay(finger_vec, key_vec);
            //std::cout<<finger_vec.size()<<" "<<key_vec.size()<<" "<<this_tid<<std::endl;
            Cursor local_path[MAX_TREE_LEVEL];
            memset(local_path, 0, sizeof(Cursor) * MAX_TREE_LEVEL);
            for(int i=0; i<key_vec.size(); i++)
                log_insert(key_vec[i], finger_vec[i], local_path);
        });
    }
    for(int i=0; i<thread_num; i++)
        threads2[i].join();
    // #pragma omp parallel num_threads(thread_num)
    // {
    //     std::vector<Key_t> key_vec;
    //     std::vector<finger_array> finger_vec;
    //     sl.replay(finger_vec, key_vec, ely);
    //     for(int i=0; i<key_vec.size(); i++)
    //     {
    //         log_insert(key_vec[i], finger_vec[i], ely);
    //     }
    // }
    //clear hash table
    clht_gc_destroy(ely);

#endif
}

void Tree::log_insert(Key_t key,  finger_array ptr, Cursor* local_path){
    D_Leaf* dleaf = (D_Leaf*)find_insert(key, local_path);
    int insert_pos = __sync_fetch_and_add(&dleaf->max_use, 1);
    // D_Leaf* hash_dleaf = (D_Leaf*)clht_get(ely->ht, (size_t)dleaf->alt_ptr);
    // if(hash_dleaf != dleaf)
    // {
    //     std::cout<<"leaf not existed! "<<std::endl;
    //     abort();
    // }
    if(insert_pos >=MAX_LEAF_KEY)
    {   
        std::cout<<"out of bound !!"<<std::endl;
        abort();
    }
    dleaf->finger_print[insert_pos].sub_slot = ptr;

}

void Tree::quick_insert(Key_t key, void* val){
    tbb::speculative_spin_rw_mutex::scoped_lock lock_quick_insert;
    memset(root_path, 0, temp_level);
restart:
    lock_quick_insert.acquire(function_lock, false);
    uint64_t leaf_offset = find_insert(key, root_path);
    D_Leaf* leaf_meta = (D_Leaf*)leaf_offset;
    bool lock_state = __sync_bool_compare_and_swap(&leaf_meta->temp_insert, 0, 1);
    if(!lock_state)//check than insert
    {   
        lock_quick_insert.release();
        goto restart;
    }
    lock_quick_insert.release();
    prefetch0(leaf_meta, sizeof(D_Leaf));
    
    bool duplicate = false;
    uint8_t fingerprint = hashfunc(key);
    for(int i=0; i<leaf_meta->max_use; i++)
    {
        if(leaf_meta->finger_print[i].sub_slot.hash_byte == fingerprint)
        {
            //countshit++;
            size_t* kv_ptr = (size_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
            if(kv_ptr[0] == key)
                abort();
            else
            {
                leaf_meta->finger_print[i].sub_slot.access_bit = 1;
                duplicate = true;
            }
        }
    }
    if(leaf_meta->max_use < MAX_LEAF_KEY)
    {
        int insert_pos = leaf_meta->max_use;
        leaf_meta->max_use++;
        if(leaf_meta->max_key < key)
            leaf_meta->max_key = key;
        leaf_meta->finger_print[insert_pos].sub_slot.hash_byte = fingerprint;
        leaf_meta->finger_print[insert_pos].sub_slot.ptr_field = (uint64_t)val;
        if(duplicate == true)
            leaf_meta->finger_print[insert_pos].sub_slot.access_bit = 1;
        else
            leaf_meta->finger_print[insert_pos].sub_slot.access_bit = 0;
        if(insert_pos >= LEFT_KEY)
        {
            leaf_meta->finger_print[insert_pos - LEFT_KEY].sub_slot.control_bit = 0;
        }
        leaf_meta->temp_insert = 0;
    }
    else
    {   
        P_Leaf* insert_leaf = (P_Leaf*)leaf_meta->alt_ptr;
        Node_entry dram_buffer[MAX_LEAF_KEY];
        int results[MAX_LEAF_KEY];
        int count_result = 0;
        for(int i=0; i<MAX_LEAF_KEY; i++)
        {
            uint64_t* kv_ptr = (uint64_t*)leaf_meta->finger_print[i].sub_slot.ptr_field;
            dram_buffer[i].one_key = kv_ptr[0];
            dram_buffer[i].val = kv_ptr[1];
            finger_buffer[i] = leaf_meta->finger_print[i];
            finger_buffer[i].sub_slot.control_bit = 0;
        }
        //sort
        
        qsort(0, MAX_LEAF_KEY - 1, dram_buffer, finger_buffer);
        leaf_meta->alt_ptr = dram_buffer;
        persist_fence();
        leaf_meta->temp_insert++;
        for(int i=0; i<MAX_LEAF_KEY; i++)
        {
            if(finger_buffer[i].sub_slot.access_bit == 1)
            {
                results[count_result] = i;
                count_result++;
            }
        }
        if(count_result != 0)//has duplicate fingerprint, need filter
            finger_differ(finger_buffer, results, count_result);

    
        D_Leaf* leaf_meta2 = (D_Leaf*)split_leaf(insert_leaf, leaf_meta, finger_buffer, dram_buffer);
        //quick_split
        // D_Leaf* new_meta = (D_Leaf*) malloc(sizeof(D_Leaf));
        // D_Leaf* old_meta = leaf_meta;
        // for(int i=0; i<LEFT_KEY; i++)
        // {
        //     old_meta->finger_print[i] = finger_buffer[i];
        //     old_meta->finger_print[i].sub_slot.control_bit = i + LEFT_KEY;
        //     old_meta->finger_print[i + LEFT_KEY].val = finger_buffer[i].val;
        // }
        // for(int i=RIGHT_KEY; i<MAX_LEAF_KEY; i++)
        // {
        //     new_meta->finger_print[i-LEFT_KEY] = finger_buffer[i];
        //     new_meta->finger_print[i-LEFT_KEY].sub_slot.control_bit = i;
        //     new_meta->finger_print[i].val = finger_buffer[i].val;
        // }
        // new_meta->max_use = RIGHT_KEY;
        // new_meta->temp_insert = 0;
        // new_meta->next = old_meta->next;
        // // new_meta->alt_ptr = new_leaf;
        // new_meta->max_key = dram_buffer[MAX_LEAF_KEY-1].one_key;

        // old_meta->max_use = LEFT_KEY;
        // old_meta->next = new_meta;
        // old_meta->max_key = dram_buffer[LEFT_KEY - 1].one_key;
        // D_Leaf* leaf_meta2 = new_meta;

        uint64_t split_key = dram_buffer[SPLIT_POS].one_key;
        
        if(temp_level == 0)//root case split
        {
            lock_quick_insert.acquire(function_lock);
            Inner_Node* new_root = (Inner_Node*) malloc(sizeof(Inner_Node));
            memset(new_root->node_key[0], 0xff, (MAX_LEAF_KEY + 1) * sizeof(size_t));
            new_root->node_key[0][0] = dram_buffer[SPLIT_POS].one_key;
            new_root->left_most_ptr = leaf_offset;
            new_root->node_key[1][0] = (uint64_t)leaf_meta2;
            new_root->max_size = 1;
            root = (char*) new_root;
            temp_level++;
           //temp_leaf++;
            new_root->version_field = 0;
            //lock_insert.release();
            leaf_meta->temp_insert = 0;
            leaf_meta->alt_ptr = insert_leaf;
            lock_quick_insert.release();
            goto restart;
            //return true;
        }
        //std::unique_lock<std::shared_mutex> smo_lock22(mut_);
        memset(root_path, 0, temp_level);
        lock_quick_insert.acquire(function_lock);
        find_insert(key, root_path);

        splitInnerNode(root_path, split_key, (uint64_t)leaf_meta, (uint64_t)leaf_meta2);
        leaf_meta->temp_insert = 0;
        leaf_meta->alt_ptr = insert_leaf;
        lock_quick_insert.release();
        goto restart;
    }
}




Tree::~Tree(){
    size_t hit_total = 0;
    for(int i=0; i<run_thread; i++)
        hit_total += workers[i]->hit_count;
    std::cout<<"hit count ios "<<hit_total<<std::endl;
    liner_find(0);
#ifdef BREAKDOWN_FIND
    std::cout<<"temp leaf is "<<temp_leaf<<std::endl;
    std::cout<<"time search "<<breakdown_search<<std::endl;
    std::cout<<"time check "<<breakdown_check<<std::endl;
    std::cout<<"time write "<<breakdown_write<<std::endl;
    std::cout<<"time split "<<breakdown_split<<std::endl;
    std::cout<<"time excpt "<<breakdown_split2<<std::endl;
#endif
#ifdef USE_PMDK
    pmemobj_close(pop);
#endif
    std::cout<<"leaf count "<<countshit<<std::endl;
    std::cout<<"destruct"<<std::endl;
    std::cout<<"temp level is "<<temp_level<<std::endl;
    for(int i=0; i<run_thread; i++)
    {
        N_alc* alloc = mmanagers[i];
#ifdef SPACE
        std::cout<<"alloc: "<<alloc->nlog_list.size() << " " <<alloc->nfree_list.size()<<std::endl;
#endif
        delete alloc;
        DSeg* sl = workers[i];
        delete sl;
        //delete mmanagers[tid];
    }
    std::cout<<"timer write "<<timer_write<<std::endl;
    std::cout<<"timer split "<<timer_split<<std::endl;
    std::cout<<"space dram "<<count_dram<<std::endl;
}
//66196507344
//28873447826
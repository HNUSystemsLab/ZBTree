// #define CXL
#ifdef USE_FPTREE
#include "fptree.h"
#elif USE_NBTREE
#include "nbtree.h"
#elif USE_FASTFAIR
#ifdef CXL
    #include "fastfair_dram.h"
#else
#include "fastfair.h"
#endif
#elif USE_ROART
#include "Tree.h"
#include "threadinfo.h"
#elif USE_DPTREE
#include "concur_dptree.hpp"
#include "btreeolc.hpp"
#elif USE_UTREE
#include "utree.h"
#elif USE_PACTREE
#include "pactree.h"
#include <numa-config.h>
#include "pactreeImpl.h"
#include "common.h"

#else
#include "zbtree.h"

#endif
#include <string>
#include <iostream>
#include <fstream>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef USE_FASTFAIR 
using namespace std;
#endif

#ifdef USE_ROART
using namespace PART_ns;
const char* pool_path_ = "/mnt/pmem/roart";
size_t pool_size_ = 1024*1024*1024*32UL;
#endif

#ifdef USE_DPTREE
int parallel_merge_worker_num;
const char* pool_path_ = "/mnt/pmem/dptree";
#ifdef LONG_VAL
size_t pool_size_ = 1024*1024*1024*128UL;
#else
size_t pool_size_ = 1024*1024*1024*32UL;
#endif
#endif

#ifdef USE_PACTREE
std::atomic<uint64_t> dram_allocated(0);
std::atomic<uint64_t> pmem_allocated(0);
std::atomic<uint64_t> dram_freed(0);
std::atomic<uint64_t> pmem_freed(0);
size_t pool_size_ = 1024*1024*1024*32ULL;
std::string *pool_dir_ = new std::string("/mnt/pmem");
#endif




// #define LONG_VAL
int val_len;
size_t pm_pool_size;
int run_thread;
class Tree_api{
    public:
        virtual ~Tree_api(){};

        virtual bool find(size_t key, size_t sz) = 0;

        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) = 0;

        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) = 0;

        virtual bool remove(size_t key, size_t key_sz) = 0;
        /**
         * @brief Scan records starting from record with given key.
         *
         * @param[in] key Pointer to the beginning of key of first record.
         * @param[in] key_sz Size of key in bytes of first record.
         * @param[in] scan_sz Amount of following records to be scanned.
         * @param[out] values_out Pointer to location of scanned records.
         * @return int Amount of records scanned.
         *
         * The implementation of scan must set 'values_out' internally to point to
         * a memory region containing the resulting records. The wrapper must
         * guarantee that this memory region is not deallocated and that access to
         * it is protected (i.e., not modified by other threads). The expected
         * contents of the memory is a contiguous sequence of <key><value>
         * representing the scanned records in ascending key order.
         *
         * A simple implementation of the scan method could be something like:
         *
         * static thread_local std::vector<std::pair<K,V>> results;
         * results.clear();
         *
         * auto it = tree.lower_bound(key);
         *
         * int scanned;
         * for(scanned=0; (scanned < scan_sz) && (it != map_.end()); ++scanned,++it)
         *     results.push_back(std::make_pair(it->first, it->second));
         *
         * values_out = results.data();
         * return scanned;
         */
        virtual int scan(size_t key, size_t key_sz, int scan_sz) = 0;
};


#ifdef USE_FPTREE
class FingerTree : public Tree_api{
    public:
        FingerTree();
        virtual ~FingerTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
        FPtree fp_tree;
};

FingerTree::FingerTree(){
}

FingerTree::~FingerTree(){

}


bool FingerTree::find(size_t key, size_t sz){
    uint64_t result = 0;
    result = fp_tree.find(key);
#ifdef LONG_VAL
    char buffer[val_len];
    memcpy(buffer, (char*)result, sz);
    if(buffer[0] == ' ')
        return false;
    else
        return true;
#else
    if(result != 0)
        return true;
    else
    {
        //std::cout<<"key "<<key<<"Not found! "<<std::endl;
        return false;
    }
#endif
}

bool FingerTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
    
#ifdef LONG_VAL
    PMEMoid val_addr;
    pmemobj_alloc(pop, &val_addr, value_sz, 0, nullptr, nullptr);
    pmemobj_memcpy_persist(pop, pmemobj_direct(val_addr), value, value_sz);
    uint64_t long_val = (uint64_t) pmemobj_direct(val_addr);
    KV kv = KV(key, long_val);
#else
    KV kv =KV(key, (uint64_t)value);    
#endif

    return fp_tree.insert(kv);
}

bool FingerTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    KV kv = KV(key, (uint64_t)value);
    return fp_tree.update(kv);
}

bool FingerTree::remove(size_t key, size_t key_sz){
    return fp_tree.deleteKey(key);
}

int FingerTree::scan(size_t key, size_t key_sz, int scan_sz){
    return fp_tree.rangeScan(key, scan_sz, nullptr);
}


#elif USE_NBTREE
class NBTree : public Tree_api{
    public:
        NBTree();
        virtual ~NBTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
        btree* nbtree;
};

NBTree::NBTree(){
#ifdef LONG_VAL
    size_t pool_size_ = 1024*1024*1024*200UL;
#else
    size_t pool_size_ = 1024*1024*1024*64UL;
#endif
    const char* pool_path_ = "/mnt/pmem/nbtree";
    nbpop = pmemobj_create(pool_path_, "nbtree", pool_size_, 0666);
#ifdef USE_PMDK
#elif USE_NOALC
    
    int fd = open(pool_path_,  O_RDWR|O_CREAT, 0666);
	if (fd < 0)
	{
		printf("[NVM MGR]\tfailed to open nvm file\n");
		exit(-1);
	}
	if (ftruncate(fd, pool_size_) < 0)
	{
		printf("[NVM MGR]\tfailed to truncate file\n");
		exit(-1);
	}
    void *pmem = mmap(NULL, pool_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	memset(pmem, 0, pool_size_);
	heap_start = (char *)pmem;
    temp_start = heap_start;
#endif
    nbtree = new btree();
}

NBTree::~NBTree(){
    delete nbtree;
}

bool NBTree::find(size_t key, size_t sz){
#ifdef VAR_KEY
#endif
    char* value = nbtree->search(key);
#ifdef LONG_VAL
    char buffer[val_len];
    //codec 
    size_t value_64 = (size_t) value;
    size_t value_48 = value_64 & 0xffffffffffff;
    memcpy(buffer, (char*)value_48, val_len);
    
    if(buffer[0] == ' ')
    {
        return false;
    }
    else
        return true;
#else
    if(value == nullptr)
    {
        return false;
    }
    return true;
#endif
}

bool NBTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
#ifdef LONG_VAL
    PMEMoid val_addr;
    pmemobj_alloc(nbpop, &val_addr, value_sz, 0, nullptr, nullptr);
    void* real_addr = pmemobj_direct(val_addr);
    memcpy(real_addr, value, value_sz);
    pmem_persist(real_addr, value_sz);
    return nbtree->insert(key, (char*)real_addr);
#endif
    return nbtree->insert(key, (char*)value);
}

bool NBTree::remove(size_t key, size_t key_sz){
    return nbtree->remove(key);
}

bool NBTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    return nbtree->update(key, (char*)value);
}

int NBTree::scan(size_t key, size_t key_sz, int scan_sz){
    return 1;
}
#elif USE_FASTFAIR
class FastTree:public Tree_api{
    public:
        FastTree();
        virtual ~FastTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
#ifdef CXL
        btree bt;
#else
        TOID(btree) bt = TOID_NULL(btree);
        PMEMobjpool *pop;
#endif
};

FastTree::FastTree(){
#ifdef CXL
    std::cout<<"init cxl fast fair version"<<std::endl; 
#else
#ifndef LONG_VAL
    pop = pmemobj_create("/mnt/pmem/fast", "btree", 1024*1024*1024*64UL,
                            0666); // make 1GB memory pool
#else
    pop = pmemobj_create("/mnt/pmem/fast", "btree", 1024*1024*1024*128UL, 0666);
#endif
    bt = POBJ_ROOT(pop, btree);
    D_RW(bt)->constructor(pop);
#endif
}

FastTree::~FastTree(){
#ifndef CXL
    D_RW(bt)->show_space();
#endif
}

bool FastTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
#ifdef LONG_VAL
    PMEMoid pmem_ptr;
    pmemobj_alloc(pop, &pmem_ptr, value_sz, 0, nullptr, nullptr);
    void* normal_ptr = pmemobj_direct(pmem_ptr);
    memcpy(normal_ptr, value, value_sz);
    pmem_persist(normal_ptr, value_sz);
    D_RW(bt)->btree_insert(key, (char*)normal_ptr);
    return true;
#endif

#ifdef CXL
    bt.btree_insert(key, const_cast<char*>(value));
#else
    D_RW(bt)->btree_insert(key, const_cast<char*>(value));
#endif
    return true;
}

bool FastTree::find(size_t key, size_t key_sz){
#ifdef LONG_VAL
    char buffer[val_len];
    char* value = D_RW(bt)->btree_search(key);
    if(value == nullptr)
        return false;
    memcpy(buffer, value, val_len);
    return true;
#endif

#ifdef CXL
    char* value = bt.btree_search(key);
    if(value == nullptr)
        return false;
    return true;
#else
    char* value = D_RW(bt)->btree_search(key);
    if(value == nullptr)
        return false;
    return true;
#endif
}

bool FastTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
#ifndef CXL
    D_RW(bt)->btree_insert(key, const_cast<char*>(value));
    return true;
#endif
//TODO
}

bool FastTree::remove(size_t key, size_t key_sz){
#ifndef CXL
    D_RW(bt)->btree_delete(key);
    return true;
#endif
}

int FastTree::scan(size_t key, size_t key_sz, int scan_sz){
    
    size_t scan_array[scan_sz+100];
    //D_RW(bt)->btree_search_range(key, ~0ULL>>1, scan_array, scan_sz);
    return 1;
}
#elif USE_ROART

struct ThreadHelper
{
  ThreadHelper()
  {
    NVMMgr_ns::register_threadinfo();
  }
  ~ThreadHelper()
  {
    // NVMMgr_ns::unregister_threadinfo();
  }
};

class Roart:public Tree_api{
    public:
        Roart();
        virtual ~Roart();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
        Tree roart;
        //std::unordered_map<int, ThreadHelper*> worker;

};

struct KV
{
  uint64_t key;
  uint64_t val;
};


Roart::Roart(){
}

Roart::~Roart(){
}

bool Roart::find(size_t key, size_t sz){
    char value_out[val_len];
    thread_local ThreadHelper t;
    // int worker_id = gettid();
    // if(worker[worker_id] == nullptr)
    // {
    //     ThreadHelper* new_worker = new ThreadHelper();
    //     worker[worker_id] = new_worker;
    // }
    Key k;
#ifndef VAR_TEST
    k.Init((char*)&key, sz, const_cast<char*>(value_out), 8);
#else
    k.Init((char*)key, 8, const_cast<char*>(value_out), sz);
#endif
    auto leaf = roart.lookup(&k);
    if (leaf != nullptr)
    {
        return true;
    }
#ifdef DEBUG_MSG
  std::cout << "Key not found!\n";
#endif 
    return false;
/*#ifdef LONG_KEY
  Key k;
  k.Init(const_cast<char*>(key), key_sz, const_cast<char*>(value_out), 8);
#else
  #ifdef KEY_INLINE
    Key k = Key(*reinterpret_cast<const uint64_t*>(key), key_sz, 0);
  #else
    Key k;
    k.Init(const_cast<char*>(key), key_sz, const_cast<char*>(value_out), 8);
  #endif
#endif

  auto leaf = roart.lookup(&k);

  if (leaf != nullptr)
  {
    memcpy(value_out, leaf->GetValue(), key_sz);
    return true;
  }
#ifdef DEBUG_MSG
  std::cout << "Key not found!\n";
#endif 
  return false;*/// 8 B6 E7 B8 5B EE 3E 9F
}

bool Roart::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
    thread_local ThreadHelper t;
    // int worker_id = gettid();
    // if(worker[worker_id] == nullptr)
    // {
    //     ThreadHelper* new_worker = new ThreadHelper();
    //     worker[worker_id] = new_worker;
    // }
    Key k;
    //char* str_key = new char [9];
    //memcpy(str_key, (char*)&key, 8);
#ifndef VAR_TEST
    k.Init((char*)&key, key_sz, const_cast<char*>(value), value_sz);
#else
    k.Init((char*)key, key_sz, const_cast<char*>(value), value_sz);
#endif
    //Key k = Key(key, key_sz, *reinterpret_cast<const uint64_t*>(value));
    Tree::OperationResults result = roart.insert(&k);
    if (result != Tree::OperationResults::Success)
    {
#ifdef DEBUG_MSG
        std::cout << "Insert failed!\n";
#endif
        return false;
    }
    return true;
/*#ifdef LONG_KEY
    Key k;
    k.Init(const_cast<char*>(key), key_sz, const_cast<char*>(value), value_sz);
#else
  #ifdef KEY_INLINE
    Key k = Key(*reinterpret_cast<const uint64_t*>(key), key_sz, *reinterpret_cast<const uint64_t*>(value));
  #else
    Key k;
    k.Init(const_cast<char*>(key), key_sz, const_cast<char*>(value), value_sz);
  #endif*/
//#endif
}

bool Roart::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    thread_local ThreadHelper t;
    Key k;
    k.Init((char*)&key, key_sz, const_cast<char*>(value), value_sz);
    Tree::OperationResults result = roart.update(&k);
    if(result != Tree::OperationResults::Success)
        return false;
    return true;
}

bool Roart::remove(size_t key, size_t key_sz){
    thread_local ThreadHelper t;
    Key k;
    char value_out[8];
    k.Init((char*)&key, key_sz, const_cast<char*>(value_out), 8);
    Tree::OperationResults result = roart.remove(&k);
    if(result != Tree::OperationResults::Success)
        return false;
    return true;
}

int Roart::scan(size_t key, size_t key_sz, int scan_sz){
    thread_local ThreadHelper t;
    constexpr size_t ONE_MB = 1ULL << 20;
    static thread_local char results[ONE_MB];
    size_t scanned = 0;
    uint64_t max = (uint64_t)-1;
    Key k, end_k;
#ifdef VAR_TEST
    k.Init((char*)key, key_sz, (char*)key, key_sz);
#else
    k.Init((char*)&key, key_sz, (char*)&key, key_sz);
#endif
    end_k.Init((char*)&max, key_sz, (char*)&max, key_sz);

    roart.lookupRange(&k, &end_k, nullptr, (PART_ns::Leaf**)&results, scan_sz, scanned);
    // auto arr = (KV*)results;
    // std::sort(arr, arr + scanned, [] (const KV l1, const KV l2) {
    //           return l1.key < l2.key;
    //   });
#ifdef DEBUG_MSG
    if (scanned != 100)
        printf("%d records scanned.\n", scanned);
#endif
    return scanned; 
}

#elif USE_DPTREE
class DPTree : public Tree_api{
    public:
        DPTree();
        virtual ~DPTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
        dptree::concur_dptree<uint64_t, uint64_t> my_dptree; 

};

DPTree::DPTree(){
    std::cout<<"Init dptree!"<<std::endl;
}


DPTree::~DPTree(){

}


bool DPTree::find(size_t key, size_t key_sz){
    size_t val=0;
    my_dptree.lookup(key, val);
    if(val != 0)
    {
        return true;
    }
    return false;
}

bool DPTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
    size_t k = key;
    size_t val = *reinterpret_cast<size_t*>(const_cast<char*>(value));
    my_dptree.insert(k, val);
    return true;
}

bool DPTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    size_t k = key;
    size_t val = *reinterpret_cast<size_t*>(const_cast<char*>(value));
    my_dptree.upsert(k, val);
    return true;
}

bool DPTree::remove(size_t key, size_t key_sz){
    size_t k = key;
    //return my_dptree.remove(k);
}

int DPTree::scan(size_t key, size_t key_sz, int scan_sz){
    static thread_local std::vector<uint64_t> v(scan_sz*2);
    v.clear();
    my_dptree.scan(key, scan_sz, v);
    scan_sz = v.size() / 2;
#ifdef DEBUG_MSG
    if (scan_sz != 100)
        printf("%d records scanned!\n", scan_sz);
#endif
    return scan_sz;
}
#elif USE_UTREE
class UTree : public Tree_api{
    public:
        UTree();
        virtual ~UTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
        btree my_utree;

};

UTree::UTree(){

}

UTree::~UTree(){

}

bool UTree::find(size_t key, size_t sz){
    char* value = my_utree.search(key, sz);
#ifdef LONG_VAL
    char buffer[val_len];
    memcpy(buffer, value, val_len);
    if(buffer[0] == ' ')
    {
        return false;
    }
    return true;
#else
    if(value == nullptr)
        return false;
    return true;
#endif
}

bool UTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
#ifdef LONG_VAL
    my_utree.insert(key, const_cast<char*>(value), value_sz);
#else
    my_utree.insert(key, const_cast<char*>(value));
#endif
    return true;
}

bool UTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    my_utree.update(key, const_cast<char*>(value));
    return true;
}

bool UTree::remove(size_t key, size_t key_sz){
    my_utree.remove(key);
    return true;
}

int UTree::scan(size_t key, size_t key_sz, int scan_sz){
    size_t scan_array[scan_sz];
    return my_utree.scan(key, scan_sz, scan_array);
    
}
#elif USE_PACTREE
class PACTree : public Tree_api{
    public:
        PACTree();
        virtual ~PACTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;

    private:
        pactree *tree_ = nullptr;
        thread_local static bool thread_init;

};
thread_local bool PACTree::thread_init = false;
struct ThreadHelper
{
    ThreadHelper(pactree* t){
        t->registerThread();
	// int id = omp_get_thread_num();
        // printf("Thread ID: %d\n", id);
    }
    ~ThreadHelper(){}
    
};
PACTree::PACTree(){
    tree_ = new pactree(1);
}

PACTree::~PACTree(){
    if(tree_!=nullptr)
        delete tree_;
}

bool PACTree::find(size_t key, size_t key_sz){
    thread_local ThreadHelper t(tree_);
    Val_t val = tree_->lookup(key);
    if(val == 0)
        return false;
    return true;
}

bool PACTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
    thread_local ThreadHelper t(tree_);
    if(!tree_->insert(key, *reinterpret_cast<Val_t*>(const_cast<char*>(value))))
    {
        return false;
    }
    return true;
}

bool PACTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    return true;
}

bool PACTree::remove(size_t key, size_t key_sz){
    return true;
}

int PACTree::scan(size_t key, size_t key_sz, int scan_sz){
    return 0;
}


#else


class ZBTree : public Tree_api{
    public:
        ZBTree();
        virtual ~ZBTree();
        virtual bool find(size_t key, size_t sz) override;
        virtual bool insert(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool update(size_t key, size_t key_sz, const char* value, size_t value_sz) override;
        virtual bool remove(size_t key, size_t key_sz) override;
        virtual int scan(size_t key, size_t key_sz, int scan_sz) override;
    private:
        Tree zb_tree;
};

/*void reply(Nyx_na* nyx_ptr, Log_Node* start, int log_end){
    for(int i=0; i<log_end; i++)
    {
        Log_Node* log = &start[i];
        if(log->version_number >= log->pptr->version_field)
        {
            Leaf_Node* log_leaf = log->pptr;
            if(log->op_kind == LOG_INSERT)
            {
                nyx_ptr->insert(log->key, (void*)log->val);
            }
            else if(log->op_kind == LOG_UPDATE)
            {
                nyx_ptr->update(log->key, (void*)log->val);
            }
            else
            {
                nyx_ptr->remove(log->key);
            }
        }
    }
}*/



ZBTree::ZBTree(){
    std::cout<<"construct"<<std::endl;
}

ZBTree::~ZBTree(){

}

bool ZBTree::find(size_t key, size_t sz){
    char* result = nullptr;
#ifdef VAR_TEST
    result = (char*) zb_tree.find(key, sz);
#else
    result = (char*) zb_tree.find(key, sz);
#endif  
#ifdef LONG_VAL
    char buffer[val_len];
    memcpy(buffer, result, val_len);
    if(buffer!= nullptr)
    {
        return true;
    }
    else
        return false;
#else    
    if(result != nullptr)
    {
        return true;
    }
        
    else
    {
        //std::cout<<"not found!"<<std::endl;
        //abort();
        return false;
    }
#endif
}

bool ZBTree::insert(size_t key, size_t key_sz, const char* value, size_t value_sz){
#ifdef VAR_TEST
    return zb_tree.insert(key, (void*)value, key_sz);
#else
    return zb_tree.insert(key, (void*)value, value_sz);
#endif
}

bool ZBTree::update(size_t key, size_t key_sz, const char* value, size_t value_sz){
    return zb_tree.update(key, (void*)value);
}

bool ZBTree::remove(size_t key, size_t key_sz){
    return zb_tree.simply_remove(key);
}

int ZBTree::scan(size_t key, size_t key_sz, int scan_sz){
    size_t scan_array[scan_sz];
#ifdef VAR_TEST
    //return zb_tree.scan(key, scan_sz, scan_array, key_sz);
#else
    return zb_tree.scan(key, scan_sz, scan_array);
#endif
}

#endif

extern "C" Tree_api* get_tree() {
    
#ifdef USE_FPTREE
#ifdef LONG_VAL
    pool_size_ = 1024*1024*1024*128UL;
#else
    pool_size_ = 1024*1024*1024*16UL;
#endif
    pool_path_ = "/mnt/pmem/fptree";
    return new FingerTree();
#elif USE_NBTREE
    return new NBTree();
#elif USE_ROART
    return new Roart();
#elif USE_FASTFAIR
    return new FastTree();
#elif USE_DPTREE
    return new DPTree();
#elif USE_UTREE
    return new UTree();
#elif USE_PACTREE
    return new PACTree();
#else
    if(run_thread > 28)
    {
        if(val_len > 200 )
            pm_pool_size = 1024*1024*1024*4UL;
        else
            pm_pool_size = 1024*1024*1024*2UL;
        
    }
        
    else if(run_thread  == 28)
        pm_pool_size = 1024*1024*1024*2UL;
    else
        pm_pool_size = 1024*1024*1024*64UL / run_thread;
    std::cout<<"local pool size is "<<pm_pool_size<<std::endl;
    return new ZBTree();
#endif
}


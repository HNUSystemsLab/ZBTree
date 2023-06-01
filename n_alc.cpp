#include "n_alc.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <string.h>
#include <thread>
#include <fstream>
#include <mutex>

#define EADR
#define HEAP_PATH "/mnt/pmem/zbtree/data"


/**************************** N_ALC *****************************************/


//global id variable, shared among every thread's local allocator

//std::atomic<uint32_t> ndecision_id[2];
std::atomic<uint32_t> global_id(0);
std::mutex shit_mutex;
static const size_t mmap_start = 0x50000000ULL;
std::atomic<size_t> atm_pos(0); 



N_alc::N_alc(size_t fixed_size){
    alc_id = global_id.fetch_add(1);
    //std::cout<<"alc id is"<<alc_id<<std::endl;
    std::string local_name = HEAP_PATH + std::to_string(alc_id);

    if(test_exist(local_name.c_str()))
    {
#ifndef RECOVERY
        std::cout<<"pmem file exist!"<<std::endl;
        exit(1);
#else
        create_pool(local_name.c_str(), fixed_size);
#endif
    }
    else
    {
        create_pool(local_name.c_str(), fixed_size);
        init();
    }
    
    //std::cout<<"init allocator"<<std::endl;
}

N_alc::~N_alc(){
    if(alc_id == 0)
    {
        std::cout<<"end address "<<(void*)end_address<<std::endl;
        std::cout<<"start address "<<start_address<<std::endl;
    }
    //shit_mutex.lock();
    //std::cout<<"alc id "<<alc_id<<" exit "<<std::endl;
    //pmem_unmap(start_address, pool_len);
    //shit_mutex.unlock();
}

bool N_alc::test_exist(const char *pmem_name){
    std::ifstream f1(pmem_name);
    if(f1.good())
        return true;
    return false;
}


void N_alc::create_pool(const char* name, size_t pool_size){
    int fd = -1;
    std::string heap_name;
    if(name == nullptr){
        heap_name = "/mnt/pmem/nvmpool";
    }
    else
        heap_name = name;
    pool_len = pool_size;
    if ((fd = open(heap_name.c_str(), O_RDWR | O_CREAT, 0666)) < 0){
        std::cerr<<"create pmem file failed"<<std::endl;
        exit(1);
    }
    if(posix_fallocate(fd, 0, pool_len)!=0){
        std::cout<<"posix failed"<<std::endl;
    }
    size_t local_mmap_start = mmap_start + pool_len * alc_id;
    //start_address = pmem_map_file(heap_name.c_str(), pool_size, PMEM_FILE_CREATE, 0666, &pool_len, nullptr);
    start_address = mmap((void*)local_mmap_start, pool_len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0); //last parameter alter the start address value
    if((size_t)start_address != local_mmap_start)
    {
        std::cout<<"start address is "<<start_address<<std::endl;
    }
}

void N_alc::init(){
    
    //initialize the basic metadata of allocator
    //stroe meta data in first 1kb region
    end_address = reinterpret_cast<void*>((size_t)start_address + 1024);
    
    page_id[0] = 0;
    page_id[1] = 0;
    decision_id[1] = 0;
    decision_id[0] = 0;
    get_more_chunk(4, nfree_list, 0);
    get_more_chunk(4, nlog_list, 1);
    //get_more_chunk(16, log_list);
    //std::cout<<"end address is"<<end_address<<std::endl;
    /*for(int i=0; i<free_list.size(); i++)
    */
}


void N_alc::recover_init(clht_t* fast_hash, size_t& anchor, int thread_num){

//fast recovery method, without pleaf init
// #ifdef RECOVERY
//     // void * recover_start = start_address;
//     // size_t* dummy_array = (size_t*)(recover_start + 64);
//     // if(dummy_array[0] != 0)
//     // {
//     //     std::cout<<"get anchor !"<<std::endl;
//     //     anchor = dummy_array[0];
//     // }
//     // void * recover_end = reinterpret_cast<void*>((size_t)recover_start + pool_len);
//     // recover_start += 1024;    
//     // //first, collect every block and rebuild thread local hash map
//     // while(recover_start < start_address + pool_len)
//     // {
//     //     Base_Meta* one_header = (Base_Meta*)recover_start;
//     //     if(one_header->block_type == 0)
//     //     {
//     //         break;
//     //     }
//     //     else if(one_header->block_type == 1)//tree_block
//     //     {   
//     //         nfree_list[one_header->block_id] = one_header;
//     //         page_id[0]++;
//     //         if(one_header->bitmap!=0 && one_header->block_id > decision_id[0])
//     //             decision_id[0] = one_header->block_id;
//     //     }
//     //     else//log_block
//     //     {
//     //         nlog_list[one_header->block_id] = one_header;
//     //         page_id[1]++;
//     //         if(one_header->bitmap!=0 && one_header->block_id > decision_id[1])
//     //             decision_id[1] = one_header->block_id;
//     //     }
//     //     recover_start += CHUNK_SIZE;
           
//     // }
//     //next, rebuld DRAM leaf based on PLeaf and insert into concurrent hash map
//     //#pragma omp parallel num_threads(28)
//     //return;
// #endif

#ifdef RECOVERY
    clht_gc_thread_init(fast_hash, thread_num);
    void * recover_start = start_address;
    size_t* dummy_array = (size_t*)(recover_start + 64);
    if(dummy_array[0] != 0)
    {
        std::cout<<"get anchor !"<<std::endl;
        anchor = dummy_array[0];
    }
    void * recover_end = reinterpret_cast<void*>((size_t)recover_start + pool_len);
    recover_start += 1024;    
    //first, collect every block and rebuild thread local hash map
    while(recover_start < start_address + pool_len)
    {
        Base_Meta* one_header = (Base_Meta*)recover_start;
        if(one_header->bitmap == 0)
        {
            int do_nothing = 0;
        }
        else if(one_header->block_type == NODE_BLK)//tree_block
        {   
            nfree_list[one_header->block_id] = one_header;
            page_id[0]++;
            if(one_header->bitmap!=0 && one_header->block_id > decision_id[0])
                decision_id[0] = one_header->block_id;
        }
        else if(one_header->bitmap == 114514)//log_block
        {
            nlog_list[one_header->block_id] = one_header;
            page_id[1]++;
            if(one_header->bitmap!=0 && one_header->block_id > decision_id[1])
                decision_id[1] = one_header->block_id;
        }
        recover_start += CHUNK_SIZE;
           
    }
    //next, rebuld DRAM leaf based on PLeaf and insert into concurrent hash map
    //#pragma omp parallel num_threads(28)
    //{
        for(auto iter=nfree_list.begin(); iter!=nfree_list.end(); iter++)
        {
            Base_Meta* one_chunk = iter->second;
            size_t data_start = (size_t)one_chunk + 64;
            for(int i=0; i<one_chunk->bitmap; i+=sizeof(Dummy_PLeaf2))
            {
                //atm_pos++;
                Dummy_PLeaf2* pleaf = (Dummy_PLeaf2*)(data_start + i);
                //prefetch0(pleaf, sizeof(Dummy_pleaf));
                Dummy_DLeaf2* dleaf = (Dummy_DLeaf2*)malloc(sizeof(Dummy_DLeaf2));
                dleaf->alt_ptr = pleaf;
                //dleaf->version_field = pleaf->version_field;
                dleaf->temp_insert = 0;
                dleaf->max_use = 0;
                //dleaf->min_key = pleaf->min_key;
                dleaf->max_key = pleaf->max_key;
#ifdef SNAP
                for(int j=0; j<DUMMY_MAX_LEAF_KEY; j++)
                {
                    if((pleaf->bitmap>>j)&1 == 1)
                    {
                        uint8_t fingerprint = fp_hash(pleaf->slot_all[j].one_key);
                        if(pleaf->slot_all[j].one_key < dleaf->min_key)
                            dleaf->min_key = pleaf->slot_all[j].one_key;
                        dleaf->hash_byte[j] = fingerprint;
                        dleaf->buffer[j] = pleaf->slot_all[j];
                        dleaf->max_use++;
                    }
                }
#endif
                //rebuild_map[pleaf] = dleaf;
                clht_put(fast_hash, (clht_addr_t)pleaf, (clht_val_t)dleaf);
            }
        }
    //}
    return;

#endif
}

void N_alc::link_ptr(clht_t* fast_hash){
#ifdef RECOVERY
    for(auto iter = nfree_list.begin(); iter != nfree_list.end(); iter++){
        Base_Meta* one_chunk = iter->second;
        size_t data_start = (size_t)one_chunk + 64;
        for(int i=0; i<one_chunk->bitmap; i+=sizeof(Dummy_PLeaf2))
        {
            Dummy_PLeaf2 * pleaf = (Dummy_PLeaf2*)(data_start + i);
            Dummy_DLeaf2* dleaf = (Dummy_DLeaf2*)clht_get(fast_hash->ht, size_t(pleaf));
            void* pnext = pleaf->next;
            if(pnext != nullptr)
                dleaf->next = (Dummy_DLeaf2*)clht_get(fast_hash->ht, (size_t)pnext);
            else
                dleaf->next = nullptr;
            //dleaf->next = (Dummy_dleaf*)rebuild_map[pnext];      
            
        }
    }
#endif
}

void N_alc::get_more_chunk(int chunk_num, std::unordered_map<uint32_t, Base_Meta*> &temp_list, int pos){
    for(int i=0; i<chunk_num; i++)
    {
        Base_Meta* new_chunk = reinterpret_cast<Base_Meta*>(end_address);
        new_chunk->bitmap = 0;//size allocated
        new_chunk->start_address = reinterpret_cast<void*>((size_t)end_address + sizeof(Base_Meta));
        end_address = reinterpret_cast<void*>((size_t)end_address + CHUNK_SIZE);   
        new_chunk->block_id = page_id[pos];
        if(pos == 0)
            new_chunk->block_type = NODE_BLK;
        else
            new_chunk->block_type = LOG_BLK;
#ifndef EADR
        persist(new_chunk);
#endif
        //std::pair<uint32_t, Base_Meta*> one_pair;
        //one_pair.first = page_id[pos];
        //one_pair.second = new_chunk;
        temp_list[page_id[pos]] = new_chunk;
        //temp_list.emplace(page_id[pos], new_chunk);
        page_id[pos]++;
        //__sync_fetch_and_add(&page_id[pos], 1);
        //page_id[pos]++;
    }
    persist_fence();
}
/*void PM_allocator::extend_heap(){

}*/
Base_Meta* N_alc::get_free_block(std::unordered_map<uint32_t, Base_Meta*> &temp_list, int pos){
    Base_Meta* new_meta = temp_list[decision_id[pos]];
    if(new_meta == nullptr)
    {
        //temp_list.erase(decision_id[pos]);
        get_more_chunk(4, temp_list, pos);
        new_meta = temp_list[decision_id[pos]];
    }
    return new_meta;
    /*for(auto iter = temp_list.begin(); iter != temp_list.end(); iter++){
    
        {
            new_meta = iter->second;
            break;
        }
    }*/
}


void* N_alc::n_allocate(uint32_t sz, int log_alc){
    int free_pos = 0;
    Base_Meta* free_block;
    if(log_alc == 0) //tree allocate
    {
        if(release_list.size()!=0)//fast path (maybe)
        {
            auto return_address = release_list.begin();
            Base_Meta* free_block = return_address->block_id;
            free_block->bitmap+=sz;
            persist(free_block);
#ifdef EADR
            persist_fence();
#endif
            void* return2user = return_address->address;
            release_list.pop_front();
            return return2user;
        }
        free_block = nfree_list[decision_id[0]];
    }
    else //log allocate
    {
        free_block = get_free_block(nlog_list, 1);
        decision_id[1]++;
        
    }
    if(free_block->bitmap + sz > CHUNK_SIZE - sizeof(Base_Meta) && log_alc == 0) // no enough space for object
    {
        //free_list.erase(decision_id);
        decision_id[0]++;
        //__sync_fetch_and_add(&decision_id[0], 1);
        //decision_id[0]++;
        free_block = get_free_block(nfree_list, 0);
        free_block->block_type = NODE_BLK;
    }
    void* return_address = reinterpret_cast<void*>((size_t)free_block->start_address + free_block->bitmap);
    if(log_alc != 0)
    {
        free_block->block_type = LOG_BLK;
        free_block->block_id = decision_id[1];
        free_block->bitmap = 114514;
    }
    else
    {
        free_block->bitmap += sz;
        persist(free_block);
    }
#ifdef EADR
    persist_fence();
#endif
    return return_address;
    //}
}

void N_alc::n_free(void* ptr, size_t sz){
    //change meta
    uint64_t ptr_offset = (reinterpret_cast<uint64_t>(ptr)>>CHUNK_SHIFT) - (reinterpret_cast<uint64_t>(start_address + 1024)>>CHUNK_SHIFT);
    void* free_address = reinterpret_cast<void*>((size_t)(start_address + 1024) + ptr_offset * CHUNK_SIZE);
    //uint64_t free_pos = (reinterpret_cast<uint64_t>(ptr) - reinterpret_cast<uint64_t>(free_address)) / grid;
    if(ptr_offset < 0)
    {
        std::cout<<"invalid address"<<std::endl;
        exit(1);
    }
    Base_Meta* header = reinterpret_cast<Base_Meta*>(free_address);
    if(sz == 0)//free one log chunk, just erase it and readd to map
    {
        uint32_t blk_id = header->block_id;
        nlog_list.erase(blk_id);
        header->block_id = page_id[1];
        header->bitmap = 0;
        persist(header);
#ifdef EADR
        persist_fence();
#endif
        nlog_list[page_id[1]] = header;
        page_id[1]++;
    }   
    else//free leaf node, fixed size required
    {
//          header->bitmap[1] += sz;
//          if(header->bitmap[1] == 0)//completely empty, add to map
//          {
//             uint32_t blk_id = header->block_id;
//             nfree_list.erase(blk_id);
//             header->block_id = page_id[0];
//             persist(header);
// #ifdef EADR
//             persist_fence();
// #endif
//             nfree_list[page_id[0]] = header;
//             page_id[0]++;
//             //narrow down the fast path's size
//             //181243799828
//             //129746018818
//             //185232154996
//             //133316543418
//             auto iter = release_list.begin();
//             while(iter != release_list.end())
//             {
//                 if(iter->block_id == header)
//                 {
//                     release_list.erase(iter++);
//                 }
//                 else
//                     ++iter;
//             }
         //}
        //  else //partical free, add to fast path
        //  {
        //     List_node new_node;
        //     new_node.address = ptr;
        //     new_node.block_id = header;
        //     release_list.push_back(new_node);
        //  }
         return;
    }
}


void N_alc::ignore_recover(){
    void * recover_start = start_address;
    size_t* dummy_array = (size_t*)(recover_start + 64);
    void * recover_end = reinterpret_cast<void*>((size_t)recover_start + pool_len);
    recover_start += 1024;    
    //first, collect every block and rebuild thread local hash map
    while(recover_start < start_address + pool_len)
    {
        Base_Meta* one_header = (Base_Meta*)recover_start;
        if(one_header->bitmap == 0)
        {
            int do_nothing = 0;
        }
        else if(one_header->block_type == NODE_BLK)//tree_block
        {   
            nfree_list[one_header->block_id] = one_header;
            page_id[0]++;
            if(one_header->bitmap!=0 && one_header->block_id > decision_id[0])
                decision_id[0] = one_header->block_id;
        }
        else if(one_header->bitmap == 114514)//log_block
        {
            nlog_list[one_header->block_id] = one_header;
            page_id[1]++;
            if(one_header->bitmap!=0 && one_header->block_id > decision_id[1])
                decision_id[1] = one_header->block_id;
        }
        recover_start += CHUNK_SIZE;    
    }

}



/**************************** D_ALC *****************************************/
#ifdef DALC
Base_Meta* D_alc::get_free_block(std::unordered_map<uint32_t, Base_Meta*> &temp_list, int pos){
    Base_Meta* return_block = nullptr;
    return_block = temp_list[decision_id[pos]];
    if(return_block == nullptr)
    {
        get_push_back(temp_list, 4, pos);
        return_block = temp_list[decision_id[pos]];
    }
    return return_block;
}

void D_alc::get_push_back(std::unordered_map<uint32_t, Base_Meta*> &temp_list, int blk_num, int pos){
    for(int i=0; i<blk_num; i++)
    {
        Base_Meta* new_block = (Base_Meta*)huge_require(CHUNK_SIZE);
        new_block->bitmap = 0;
        new_block->bitmap = 0;
        new_block->start_address = reinterpret_cast<void*>((size_t) new_block + sizeof(Base_Meta));
        new_block->block_id = page_id[pos];
        //std::pair<uint32_t, Base_Meta*> map_pair;
        //map_pair.first = page_id[pos];
        //map_pair.second = new_block;
        temp_list[page_id[pos]] = new_block;
        //page_id[pos]++;
        __sync_fetch_and_add(&page_id[pos], 1);
    }
}


void* D_alc::huge_require(uint32_t require_size){//require huge page from system
    void* return_ptr = nullptr;
    posix_memalign(&return_ptr, CACHELINE, require_size);//align require_size memory to times of CHUNK_SIZE
    //return_ptr = malloc(require_size);//get huge pages
    if(return_ptr == nullptr){
        std::cerr<<"bad alloc"<<std::endl;
        throw std::bad_alloc();
    }
    return return_ptr;
}

void D_alc::huge_release(void* huge_ptr){//return huge page to system
    std::free(huge_ptr);
}

uint64_t D_alc::huge_malloc(uint32_t size, int variable_flag){//distribute hugepage to exact user
    if(variable_flag != 0)//used to store variable key
    {
        Base_Meta* head = var_list[decision_id[1]];
        if(head->bitmap + size > CHUNK_SIZE - sizeof(Base_Meta))
        {
            //var_list.erase(decision_id);
            __sync_fetch_and_add(&decision_id[1], 1);
            //decision_id[1]++;
            head = get_free_block(var_list, 1);
        }
        uint64_t offset = (head->block_id << CHUNK_SHIFT) + head->bitmap + CACHELINE;
        head->bitmap += size;
        return offset;
    }
    //return malloc(size);
    Base_Meta* this_meta = dfree_list[decision_id[0]];//TODO: add way to locate base meta
    //int pos = meta_find_slot(this_meta);
    if(this_meta->bitmap + size > CHUNK_SIZE - sizeof(Base_Meta))
    {
        //dfree_list.erase(decision_id);
        __sync_fetch_and_add(&decision_id[0], 1);
        //decision_id[0]++;
        this_meta = get_free_block(dfree_list, 0);
    }
    uint64_t offset = (uint64_t)this_meta->start_address + this_meta->bitmap;
    this_meta->bitmap += size;

    return offset;
}    

void D_alc::huge_free(void* ptr){//return huge page to huge manager
    //locate
    uint64_t pure_val_ptr = reinterpret_cast<uint64_t>(ptr)>>CHUNK_SHIFT;
    uint64_t pure_val_ptr2 = pure_val_ptr - 1;
    uint64_t result = hash_decode(pure_val_ptr);
    uint64_t result2 = hash_decode(pure_val_ptr2);
    Base_Meta* this_meta;
    if(result == 0 && result2 == 0)
    {
        std::cout<<"bad alc"<<std::endl;
        return;
    }
    else if(result!=0 && result2 == 0)
        this_meta = (Base_Meta*)result;
    else if(result==0 && result2 != 0)
        this_meta = (Base_Meta*)result2;
    else
    {
        uint64_t true_result = (result > (uint64_t)ptr ? result2 : result);
        this_meta = (Base_Meta*)true_result;
    }
    if(this_meta->bitmap >= CHUNK_SIZE * 0.75)//reach threshold, reclaim
        huge_release(this_meta);
    else // partical empty
    {
        /*int find_pos=1;
        bit_set(this_meta->bitmap, find_pos, 1);*/
    }
    return;
}

void* D_alc::address_trans(uint64_t ptr, int list_flag){
    void* header;
    size_t arc_header;
    if(list_flag !=0)
        arc_header = (size_t)var_list[ptr>>CHUNK_SHIFT];
    else
        arc_header = (size_t)dfree_list[ptr>>CHUNK_SHIFT];
    header = (void*) (arc_header + (ptr & MASK));
    return header;
}

D_alc::D_alc(){
    //get_push_back();
    page_id[0] = 0;
    decision_id[0] = 0;
    get_push_back(dfree_list, 4, 0);
    //std::cout<<"DSTART at "<< address_trans(0, 0) <<std::endl;
#ifdef VAR_KEY
    page_id[1] = 0;
    decision_id[1] = 0;
    get_push_back(var_list, 16, 1);
#endif
    //std::cout<<dfree_list.size();
}

D_alc::~D_alc(){
}

#endif
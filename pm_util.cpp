#include "pm_util.h"


#define EADR


void persist(void* data){

#ifdef EADR
	SFENCE();
	return;
#endif

#ifdef USE_CLWB
	asm_clwb(data);
#elif USE_NTSTORE
	_mm_srli_si128(data);
#elif USE_PMEM
	pmem_persist(data, CACHELINE);
#elif USECLFLUSH
	CLFLUSH(data);
#endif
}

void persist_fence(){SFENCE();}

void persist_range(void* data, size_t sz){
#ifdef EADR
	SFENCE();
	return;
#endif
#ifdef USE_PMEM
	pmem_persist(data, sz);
	return;
#elif USE_CLWB
    volatile char *ptr = (char *)((unsigned long)data & ~(CACHELINE- 1));
	for (; ptr < data + sz; ptr += CACHELINE) {
		persist((void*)data);
	}
	SFENCE();
#endif
}

void prefetch(void* data){
	__builtin_prefetch(data, 1, 3);
}

void prefetch0(void* data, size_t len){
	prefetcht0(*((char*)data + len - CACHELINE));
}

int bit_get(uint8_t* bitmap, int pos){
	int offset = pos % 8;
	int result = bitmap[pos>>3]>>offset&1;
	return result;
}

void bit_set(uint8_t* bitmap, int pos, int set_val){ // 10010011 5  1111 111
	uint8_t offset = pos % 8;
	if(set_val == 1)
		bitmap[pos>>3] |= (1<<offset);
	else if(set_val == 0)
		bitmap[pos>>3] &= ~(1<<offset);
}

int bit_find0(uint8_t* bitmap, int size){ // return first 0's pos
	
	int find_result=0;
	int i;
	for(i=0; i<size; i++)
	{
		find_result =  __builtin_ffs(bitmap[i]^MAX_UINT8);
		if(find_result != 0)
			break;
	}
	if(find_result == 0)
		return -1;
	return 8*i + find_result-1;
}

int bit_find1(uint8_t* bitmap, int bit_size){
	int find_result = 0;
	int i;
	for(i=0; i<bit_size; i++)
	{
		find_result = __builtin_ffs(bitmap[i]);
		if(find_result != 0)
			break;
	}
	if(find_result == 0)
		return -1;
	return 8*i + find_result - 1;
}


bool bit_full0(uint8_t* bitmap, int size){
	for(int i=0; i<size; i++)
	{
		if(bitmap[i] != 0)
			return false;
	}
	return true;
}

bool bit_full1(uint8_t* bitmap, int size){
	for(int i=0; i<size; i++)
	{
		if(~bitmap[i] != 0)
			return false;
	}
	return true;
}

uint64_t mur_hash(void* key, int len, uint64_t seed){
	const uint64_t m = BIG_CONSTANT(0xc6a4a7935bd1e995);
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t *data = (const uint64_t *)key;
    const uint64_t *end = data + (len / 8);

    while (data != end)
    {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char *data2 = (const unsigned char *)data;

    switch (len & 7)
    {
    case 7:
        h ^= uint64_t(data2[6]) << 48;
    case 6:
        h ^= uint64_t(data2[5]) << 40;
    case 5:
        h ^= uint64_t(data2[4]) << 32;
    case 4:
        h ^= uint64_t(data2[3]) << 24;
    case 3:
        h ^= uint64_t(data2[2]) << 16;
    case 2:
        h ^= uint64_t(data2[1]) << 8;
    case 1:
        h ^= uint64_t(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

uint64_t read_rdtsc(){
	unsigned long var;
    unsigned int hi, lo;
    asm volatile("rdtsc"
                 : "=a"(lo), "=d"(hi));
    var = ((unsigned long long int)hi << 32) | lo;

    return var;
}

uint64_t hash_encode(void* ptr){
	return mur_hash(ptr, 8, read_rdtsc());
}
uint64_t hash_decode(uint64_t val){
	return mur_hash((void*)val, 8, read_rdtsc());
}

uint8_t fp_hash(uint64_t key){
	uint8_t onebyte_hash = std::_Hash_bytes(&key, sizeof(key), 1) & 0xff;
	return onebyte_hash;
}

void bit_multi_set(uint8_t* bitmap, int len, int val){// set lower len bit in bitmap to val
	uint8_t max_32 = ~0;
	uint8_t temp = max_32 >> (32-len);
	if(val == 1)
		bitmap[0] = temp;
	else
	{
		bitmap[0] = bitmap[0] & temp;
	}
}
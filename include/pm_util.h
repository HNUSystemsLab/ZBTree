#include <iostream>
#include <hash_fun.h>
#include <emmintrin.h>
#include <x86intrin.h>
#include <unistd.h>
#include "libpmem.h"
#ifndef _PM_UTIL_H
#define _PM_UTIL_H
#define CACHELINE 64
#define PM_SIZE 6*1024*1024*1024UL
#define PM_POOL "/mnt/pmem/nyx"
#define CLFLUSH(add) asm volatile ("clflush (%0)" :: "r" (add)) 
#define CLWB(add) asm volatile(".byte 0x66; xsaveopt %0" : "+m" (add))
#define SFENCE() asm volatile("mfence" ::: "memory")
#define asm_clwb(addr) asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)addr));


#define BIG_CONSTANT(x) (x##LLU)

#define MAX_UINT64 ~0
#define MAX_UINT8 0xff
#define MAX_UINT32 0xffffffff
#define prefetcht0(mem_var)               \
     __asm__ __volatile__("prefetcht0 %0" \
                          :               \
                          : "m"(mem_var))


void persist(void* data);
void persist_range(void* data, size_t sz);
void persist_fence();
//void ntstore(void* data);

void prefetch(void* data);


void start_count();
void end_count();

uint64_t mur_hash(void* key,  int len, uint64_t seed);
uint64_t hash_encode(void* ptr);
uint64_t hash_decode(uint64_t val);
uint8_t fp_hash(uint64_t key);

uint64_t read_rdtsc();
int bit_get(uint8_t* bitmap, int pos);
void bit_set(uint8_t* bitmap, int pos, int set_val);
int bit_find0(uint8_t* bitmap, int size);
int bit_find1(uint8_t* bitmap, int size);
bool bit_full0(uint8_t* bitmap, int size);
bool bit_full1(uint8_t* bitmap, int size);
void bit_multi_set(uint8_t* bitmap, int len, int val);
uint64_t read_rdtsc();
void prefetch0(void* ptr, size_t len);


#endif
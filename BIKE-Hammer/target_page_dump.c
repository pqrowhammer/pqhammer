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

void main(void){
    int fp = open("./bike-test", O_RDONLY);

    struct stat file_stat;
    int x = fstat(fp,&file_stat);
    
    char*  file_addr = (char*) mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED|MAP_POPULATE, fp, 0);

    char target_page_data[4096];
    memcpy(target_page_data,file_addr+0x1000,4096);

    // for(int i=0x848; i<0x850; i++){
    //     printf("%hhx\n",target_page_data[i]);
    // }



    FILE* destf = fopen("./target_page", "w");
    // int results = fputs(target_page_data, destf);
    fwrite(target_page_data,4096,1,destf);
    
    close(fp);
    fclose(destf); 
    
    
    
}


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

size_t mem_size;
char* memory;
double fraction_of_physical_memory = 0.8;

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

void main(void){
    setupMapping();
    char* mem_arr[2000];
    for(int i=0; i<2000; i++){
        mem_arr[i] =  (char *) mmap(0, 1<<21, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE| MAP_POPULATE, -1, 0);

    }
}


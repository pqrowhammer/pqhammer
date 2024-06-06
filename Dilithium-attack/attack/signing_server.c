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
    int fp = open("./keys/original_sk.bin", O_RDONLY);
    struct stat file_stat;
    int x = fstat(fp,&file_stat);

    char m[MLEN*SIGNUM];
    memset(m,0x00,MLEN*SIGNUM);

    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    int skt;
    socklen_t addrlen = sizeof(addr);

    if((skt = socket(AF_INET, SOCK_STREAM, 0))<0){
        printf("socket error\n");
        exit(0);
    }
    
    if(bind(skt,(struct sockaddr*)&addr,sizeof(addr)) < 0){
        printf("Bind Error\n");
        exit(0);
    }

    if(listen(skt,100) < 0){
        printf("Listen Error\n");
        exit(0);
    }
    printf("Listening....\n");

    int connection = accept(skt, (struct sockaddr*)&addr, &addrlen);
    uint8_t*  sk = (uint8_t*) mmap(NULL, file_stat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE, fp, 0);
    
    int ret = read(connection,m,MLEN*SIGNUM);
    

    
    // sign the messages and save to files 
    size_t mlen, smlen;
    uint8_t* sm = (uint8_t*)malloc((MLEN + CRYPTO_BYTES)*SIGNUM*sizeof(uint8_t));
    int sm_round[SIGNUM] = {0};
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        ret = pqcrystals_dilithium5_ref(&sm[sig_index*(MLEN + CRYPTO_BYTES)], &smlen, &m[sig_index*MLEN], MLEN, sk);
        sm_round[sig_index] = ret;
    }

    // save signatures
    char filename[200];
    FILE *sig_out;
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        sprintf(filename, "./sigs/sig_%d.bin", sig_index);
        sig_out = fopen(filename,"w+");
        fwrite(sm + ((MLEN+CRYPTO_BYTES)*sig_index), 1, (MLEN+CRYPTO_BYTES), sig_out);
        fclose(sig_out);
    }

    // for(int s=0; s<SIGNUM;s++){
    //     printf("--------------Message--------------------\n");
    //     for(int i=0; i<MLEN; i++){
    //         printf("%hhx\t",m[i+ MLEN*s]);
    //     }
    //     printf("\n-------------------------------------------\n");
    // }

    // print rho
    printf("--------------hammered rho--------------------\n");
    for(int i=0; i<32; i++){
        printf("%hhx\t",sk[i]);
    }
    printf("\n");
    FILE *sk_out = fopen("./keys/hammered_sk.bin","w+");
    fwrite(sk, sizeof(uint8_t), SECRETKEY_BYTES, sk_out);
    fclose(sk_out);
    
    close(fp);
    close(connection);
    close(skt);
}
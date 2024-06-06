#include "api.h"
#include <stdio.h>
#include "../dilithium/ref/randombytes.h"
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include "params.h"

/*
Generate SIGNUM messages, send them to signing server for signing, save messages to files.
*/

void main(void){
    int ret;
    uint8_t m[MLEN*SIGNUM] = {0};
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        randombytes(&m[sig_index*MLEN], MLEN);
    }
   

    // for(int s=0; s<SIGNUM;s++){
    //     printf("--------------Message--------------------\n");
    //     for(int i=0; i<MLEN; i++){
    //         printf("%hhx\t",m[i+ MLEN*s]);
    //     }
    //     printf("\n-------------------------------------------\n");
    // }

     printf("saving\n");
    // save messages 
    FILE *all_msgs = fopen("./messages/all_messages.bin","w+");
    fwrite(m, sizeof(uint8_t), MLEN*SIGNUM, all_msgs);
    fclose(all_msgs);

     printf("saved all\n");
    char filename[200];
    FILE *msg_out;
    for(int sig_index = 0; sig_index < SIGNUM; sig_index++){
        sprintf(filename, "./messages/message_%d.bin", sig_index);
        msg_out = fopen(filename,"w+");
        fwrite(m+(MLEN*sig_index), 1, MLEN, msg_out);
        fclose(msg_out);
    }
    
    printf("saved\n");
   

    // send the message to the signing server

    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
   
    addr.sin_port = htons(8080);
    int skt;
    socklen_t addrlen = sizeof(addr);

    if((skt = socket(AF_INET, SOCK_STREAM, 0))<0){
        printf("socket error\n");
        exit(0);
    }
    ret = inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    ret = connect(skt,(struct sockaddr*)&addr,addrlen);
    printf("Connected to the server\n");
    // sleep(50);
    printf("Sending Message\n");
    send(skt,m,MLEN*SIGNUM,0);
    close(skt);
    


    printf("\n");
}
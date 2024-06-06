#include "api.h"
#include <stdio.h>
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include "params.h"

void main(void){
    uint8_t sm[MLEN+CRYPTO_BYTES];
    FILE *signed_msg = fopen("./sigs/sig_0.bin","r");
    fread(sm,sizeof(uint8_t),MLEN+CRYPTO_BYTES,signed_msg);
    fclose(signed_msg);

    uint8_t pk[PUBLICKEY_BYTES];
    FILE *pk_file = fopen("./keys/pk.bin","r");
    fread(pk,sizeof(uint8_t),PUBLICKEY_BYTES,pk_file);
    fclose(pk_file);


    size_t mlen, smlen;
 
   
    
    smlen=MLEN+CRYPTO_BYTES;
    uint8_t m[MLEN+CRYPTO_BYTES];
    int verify = pqcrystals_dilithium5_ref_open(m, &mlen, sm, smlen, pk);
    
    if(verify){
        printf("Message did not verify\n");
    }else{
        printf("Message verified\n");
    }

    printf("--------------Message--------------------\n");
    for(int i=0; i<MLEN; i++){
        printf("%hhx\t",m[i]);
    }
    printf("\n-------------------------------------------\n");

   
    

}